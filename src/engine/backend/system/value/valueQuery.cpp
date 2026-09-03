////////////////////////////////////////////////////////////////////////////
//	L4-1 — runtime script value objects "Query" / "QueryResult" / "QuerySelect"
////////////////////////////////////////////////////////////////////////////

#include "valueQuery.h"

#include "backend/query/queryParser.h"
#include "backend/diagnostics/crashGuard.h"  // ibJournal — the technology journal
#include "valueArray.h"                  // ibValueArray — a package answers with results BY POSITION
#include "backend/compiler/typeCtor.h"   // VALUE_TYPE_REGISTER / SYSTEM_TYPE_REGISTER / ENUM_TYPE_REGISTER
#include "backend/backend_exception.h"   // ibBackendCoreException — a wrong TempTablesManager is told, not ignored
#include "backend/appData.h"             // appData->DesignerMode()
#include "backend/session/session.h"     // ibSession::Current — the session knows WHICH config it was opened for
#include "backend/moduleManager/moduleManager.h"  // GetMetaManager()->GetMetaData() — that config, held

//////////////////////////////////////////////////////////////////////
// ibValueQueryExec — script "Query"
//////////////////////////////////////////////////////////////////////

// WHOSE sources a hand-written query sees.
//
// Metaobject sources register into the factory of the config they belong to (ibMetaData::RegisterSource
// — "into THIS config's OWN factory"), and the resolver finds them through whatever config is in scope.
// Three callers set that scope from a config they already hold: the composer, the dynamic list, the
// constructor. A query written by hand in a module — New Query("… FROM Catalog.Catalog1") — was the
// fourth, and had nobody: it fell through to the global base factory, where no catalog has ever
// registered, and came back "unknown metaobject kind 'Catalog'" for a name the constructor reads
// without a murmur.
//
// It does not need a new field, and it must not consult anything process-wide. The SESSION was opened
// FOR a configuration and holds it fixed — the same metadata the "Metadata" and "Data" globals are
// built from. Two sessions on two configurations each answer with their own; a background job answers
// with the one it runs under.
static const ibMetaData* ibQuerySessionConfig()
{
	const ibSession* session = ibSession::Current();
	if (session == nullptr)
		return nullptr;   // designer eval / no session — the caller degrades, it does not guess

	const ibValueModuleManagerRuntimeConfiguration* mm = session->GetManagerModule();
	if (mm == nullptr)
		return nullptr;

	const ibValueModuleManager::ibValueMetadataUnit* unit = mm->GetMetaManager();
	return unit != nullptr ? unit->GetMetaData() : nullptr;
}

void ibValueQueryExec_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("Query(text : string)"));
	helper.AppendProc(wxT("SetParameter"), 2, wxT("SetParameter(name : string, value)"));
	// TWO VERBS OVER ONE MECHANISM. Every query runs as a package — an ordinary query is a package
	// of one — so Execute() is simply "the first result", and ExecuteBatch() hands back the whole
	// array to be indexed. No branch about which kind of text this is, and no verb that sometimes
	// returns a table and sometimes a number.
	// Execute() takes an OPTIONAL NAME — the one a statement gave its result with ONTO. Asking by
	// name is the point of that word: a position changes the moment somebody inserts a statement
	// above it, a name does not. Same verb, because it is the same act.
	// ⭐ EXECUTE TAKES NOTHING. "Just run it" is the whole of this verb; anything that needs to reach
	// a PARTICULAR result of several is asking about a PACKAGE, and a package answers with the array
	// — `ExecuteBatch()`. Two ways to say the same thing is how the pair drifts (Max, 2026-09-04:
	// *"execute — no name needed there, just execute and that is all, drive the rest into the batch"*).
	// ONTO still names a selection; what reads that name is LINK, inside the package text.
	helper.AppendFunc(wxT("Execute"), wxT("Execute()"));
	helper.AppendFunc(wxT("ExecuteBatch"), wxT("ExecuteBatch()"));
	// WHERE this query's temp tables live. Left unset they die with the execution; set to a
	// TempTablesManager they outlive it and other queries attached to the same manager read them.
	helper.AppendProp(wxT("TempTablesManager"), true, true, ibValueQueryExec::enPropTempTablesManager);
}

//////////////////////////////////////////////////////////////////////
// ibValueTempTablesManager — script "TempTablesManager"
//////////////////////////////////////////////////////////////////////

void ibValueTempTablesManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(0, wxT("TempTablesManager()"));
	// ONE VERB, because there is one decision to make about a set of temp tables: keep them, or
	// let them go. Everything else about them is said in the query text.
	helper.AppendProc(wxT("Close"), wxT("Close()"));
}

bool ibValueTempTablesManager::CallAsProc(const long lMethodNum, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	if (lMethodNum != enClose)
		return false;
	m_store->Close();
	return true;
}

bool ibValueQueryExec::Init(ibValue** paParams, const long lSizeArray)
{
	m_text = (lSizeArray >= 1) ? paParams[0]->GetString() : wxString();
	m_package = ibQueryPackage();

	// An empty query is a valid, statement-less object: its method surface (SetParameter / Execute /
	// ExecuteBatch) comes from the static bind, NOT from the AST — so `New Query()` still completes
	// and introspects. Execute() on a statement-less Query yields an empty QueryResult.
	if (m_text.IsEmpty())
		return true;

	// Designer: tolerate a malformed / half-typed query so the value (and its method chain) stays reachable
	// for autocomplete. Runtime: a syntax error throws ibBackendException (line:pos) as before.
	if (appData->DesignerMode()) {
		try { m_package = ibQueryParser().ParsePackage(m_text); }
		catch (const ibBackendException&) { m_package = ibQueryPackage(); }
		return true;
	}

	m_package = ibQueryParser().ParsePackage(m_text);
	return !m_package.m_statements.empty();
}

bool ibValueQueryExec::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum == enSetParameter) {
		if (lSizeArray < 2)
			return false;
		m_params[paParams[0]->GetString()] = *paParams[1];
		return true;
	}
	return false;
}

// EVERY query runs as a package. An ordinary one-statement query is a package of one, so there is a
// single execution path here and no branch about which kind of text was handed in.
//
// The array is addressed BY POSITION, in the order the statements were written, and each position
// answers with what its statement produced: a plain select its QueryResult; a select INTO a temp
// table a result of ONE column and ONE row holding the number of records placed there; a drop the
// UNDEFINED value, because a drop produces no result. The statements see each other's temp tables;
// that is what makes a package worth writing rather than merely tidy.
bool ibValueQueryExec::RunPackage(std::vector<ibValue>& out, std::vector<wxString>* names)
{
	if (m_package.m_statements.empty())
		return false;

	// The manager, when one was given. Without it the store below is the package's own and the
	// tables die with this call — which is the right default: a temp table nobody is keeping is a
	// temp table nobody can accidentally read later.
	ibValueTempTablesManager* manager = nullptr;
	ibQueryTempTableStore* store = nullptr;
	if (!m_tempTables.IsEmpty() && m_tempTables.ConvertToValue(manager) && manager != nullptr)
		store = manager->GetStore();

	// The config this query's by-name sources resolve against. An outer scope wins — a query run from
	// inside something that already said which config it means (a composer) belongs to THAT one, not to
	// whatever session happens to be current.
	const ibMetaData* config = ibSourceMetaDataScope::Get();
	if (config == nullptr)
		config = ibQuerySessionConfig();
	const ibSourceMetaDataScope resolveAgainst(config);

	// ⭐ ONE STATE FOR THE WHOLE PACKAGE, AND FOR THE ROWS DRAWN OUT OF IT AFTERWARDS. A package is
	// several statements by construction, and each result it hands back is a LIVE cursor the script
	// reads whenever it likes — so the state has to be fixed before the first statement runs and held
	// until the last reader lets go. Every result below takes a share of it; when they are all gone,
	// so is it. Null when a transaction is already open, and then this adds nothing.
	const std::shared_ptr<ibQueryReadState> readsOneState;   // ⛔ NOT OPENED — see queryReadState.h

	try {
		for (ibQueryLowering::PackageResult& r : ibQueryLowering::ExecutePackage(m_package, m_params, store)) {
			// ONE KIND PER POSITION, and only two of them. A statement that produced a table hands
			// back a RESULT — including a create-temp statement, whose result is one column and one
			// row holding the record count. A DROP has no result at all and hands back the UNDEFINED
			// value. Nothing in the array is a bare number, so walking it needs no branch.
			if (r.m_result != nullptr)
				// …and the temp-table share travels WITH the rows it belongs to: the package's own
				// store outlives this call only because every result it produced holds a piece of it.
				out.push_back(new ibValueQueryResult(std::move(*r.m_result), std::move(r.m_schema), r.m_hasTotals,
				                                     readsOneState, r.m_temps));
			else
				out.push_back(ibValue());   // a drop: a position with no result

			// The name travels ALONGSIDE, in step with the results — an unnamed statement keeps its
			// place with an empty one, so a position and a name never disagree about which is which.
			if (names != nullptr)
				names->push_back(r.m_name);
		}
		return true;
	}
	catch (...) {
		if (!appData->DesignerMode())
			throw;   // runtime: the package fails at the statement that failed, with THAT statement's message
		// designer: editing / autocomplete has no live session to read from, and a session-less read can throw
		// beyond ibBackendException — degrade instead of failing, so the .Execute().Select()… chain stays
		// introspectable. (The editor's own eval would otherwise swallow the throw and leave the chain object
		// undefined → no Select in the dropdown.)
		out.clear();
		return false;
	}
}

bool ibValueQueryExec::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                  ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != enExecute && lMethodNum != enExecuteBatch)
		return false;

	// ⛔ NO "BY NAME" HERE. Reaching a particular result of several is a question about a PACKAGE, and
	// the package answers with its array — ExecuteBatch(). Execute() is "just run it" and nothing
	// else (Max, 2026-09-04). ONTO keeps naming a selection: LINK reads that name, in the query text.
	std::vector<ibValue> results;
	RunPackage(results);

	// ExecuteBatch() — the whole array, indexed by the position each statement was written at.
	if (lMethodNum == enExecuteBatch) {
		pvarRetValue = new ibValueArray(results);
		return true;
	}

	// Execute() — THE FIRST RESULT, which for an ordinary query is the only one. Not a special
	// path: the same package ran, this verb just names the position instead of handing back an
	// array the caller would index with a constant zero every time.
	if (!results.empty()) {
		ibValueQueryResult* first = nullptr;
		if (results.front().ConvertToValue(first) && first != nullptr) {
			pvarRetValue = results.front();
			return true;
		}
	}

	// Statement-less (empty query), designer-degraded, or a first statement that is a DROP (which
	// has no result) — an empty QueryResult keeps the method chain alive.
	pvarRetValue = new ibValueQueryResult();
	return true;
}

// A STATIC member table indexes properties by POSITION — there is no per-instance table to carry
// a data tag, so the index IS the identity (the ibValuePoint / ibValueColour convention).
bool ibValueQueryExec::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (lPropNum != enPropTempTablesManager)
		return false;
	pvarPropVal = m_tempTables;
	return true;
}

bool ibValueQueryExec::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	if (lPropNum != enPropTempTablesManager)
		return false;

	// Clearing it is meaningful and therefore allowed: the query goes back to owning its own
	// temp tables for the length of one run.
	if (varPropVal.IsEmpty()) {
		m_tempTables = ibValue();
		return true;
	}

	// Anything else must BE a manager. A query told to put its tables somewhere that cannot hold
	// them would otherwise fail much later, at a statement that reads a name nothing filled.
	ibValueTempTablesManager* manager = nullptr;
	if (!varPropVal.ConvertToValue(manager) || manager == nullptr)
		ibBackendCoreException::Error(_("TempTablesManager expects a TempTablesManager value"));

	m_tempTables = varPropVal;
	return true;
}

//////////////////////////////////////////////////////////////////////
// ibValueQueryResult — script "QueryResult" (res.Select() -> a selection)
//////////////////////////////////////////////////////////////////////

ibValueQueryResult::ibValueQueryResult()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
{
	m_members.Bind(this, &ibValueQueryResult::FillMembers);
}

ibValueQueryResult::ibValueQueryResult(ibDataQueryResult&& result,
                                       std::vector<ibQueryLowering::OutputColumn> schema, bool hasTotals,
                                       std::shared_ptr<ibQueryReadState> snapshot,
                                       std::shared_ptr<ibQueryTempTableStore> temps)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_result(std::make_unique<ibDataQueryResult>(std::move(result)))
	, m_schema(std::move(schema))
	, m_hasTotals(hasTotals)
	, m_snapshot(std::move(snapshot))
	, m_temps(std::move(temps))
{
	m_members.Bind(this, &ibValueQueryResult::FillMembers);
}

ibValueQueryResult::~ibValueQueryResult() = default;

void ibValueQueryResult::FillMembers(ibMemberTable& helper) const
{
	// …AND THE BRANCH, on this level too. A SPLIT report is entered at the TOP — that is where a
	// reader starts — so "walk only this branch" has to be sayable here and not merely one level in.
	// The door has taken it all along (ibDataQueryResult::Select(kind, branch)); the verb declared one
	// argument, so a second was swallowed in silence and the walk quietly returned every branch
	// (measured 2026-09-04: a branch name nobody answers to still walked all of them).
	helper.AppendFunc(wxT("Select"), 2, wxT("Select(method?, branch?)"));   // -> a QuerySelect (consumes the result)
}

bool ibValueQueryResult::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                    ibValue** paParams, const long lSizeArray)
{
	if (lMethodNum != enSelect)
		return false;
	if (m_result == nullptr) {        // already selected (the cursor was consumed) — empty selection
		pvarRetValue = new ibValueQuerySelect();
		return true;
	}
	// Select(method) — an explicit ibSelectKind (a QueryResultIteration variant) folds the result that way.
	// DEFAULT is a DIRECT (flat) walk: a plain query streams the forward cursor; any fold (or a totals
	// result) goes through the selector. ConvertToEnumValue yields the first value (Direct) for no/non-enum arg.
	const ibSelectKind kind = (lSizeArray >= 1 && paParams[0] != nullptr)
		? paParams[0]->ConvertToEnumValue<ibSelectKind>()
		: ibSelectKind::ibSelectKind_Direct;
	if (kind == ibSelectKind::ibSelectKind_Direct && !m_hasTotals) {
		pvarRetValue = new ibValueQuerySelect(std::move(m_result), m_schema, m_snapshot, m_temps);
	}
	else {
		// Select(method, branch) — walk ONE branch of a SPLIT from the top. A name nobody answers to
		// yields an empty walk rather than falling back to all of them: asking for a branch that is
		// not there is a mistake worth seeing (ibSelector::CollectVisits says the same).
		const wxString branch = (lSizeArray >= 2 && paParams[1] != nullptr && !paParams[1]->IsEmpty())
			? paParams[1]->GetString() : wxString();
		ibSelector top = branch.IsEmpty() ? m_result->Select(kind) : m_result->Select(kind, branch);
		ibJournalInfo(wxT("query.walk"), wxT("select(%d)%s: %ld node(s) at the first level"),
			static_cast<int>(kind),
			branch.IsEmpty() ? wxString() : (wxT(" branch '") + branch + wxT("'")), top.NodeCount());
		pvarRetValue = new ibValueQuerySelect(std::move(top), m_schema, m_snapshot, m_temps);
	}
	return true;
}

//////////////////////////////////////////////////////////////////////
// ibValueQuerySelect — script "QuerySelect" (flat list OR TOTALS tree)
//////////////////////////////////////////////////////////////////////

ibValueQuerySelect::ibValueQuerySelect()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
{
	m_members.Bind(this, &ibValueQuerySelect::FillMembers);
}

ibValueQuerySelect::ibValueQuerySelect(std::unique_ptr<ibDataQueryResult> flat,
                                       std::vector<ibQueryLowering::OutputColumn> schema,
                                       std::shared_ptr<ibQueryReadState> snapshot,
                                       std::shared_ptr<ibQueryTempTableStore> temps)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_flat(std::move(flat))
	, m_schema(std::move(schema))
	, m_snapshot(std::move(snapshot))
	, m_temps(std::move(temps))
{
	m_members.Bind(this, &ibValueQuerySelect::FillMembers);
}

ibValueQuerySelect::ibValueQuerySelect(ibSelector&& tree,
                                       std::vector<ibQueryLowering::OutputColumn> schema,
                                       std::shared_ptr<ibQueryReadState> snapshot,
                                       std::shared_ptr<ibQueryTempTableStore> temps)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_tree(std::make_unique<ibSelector>(std::move(tree)))
	, m_schema(std::move(schema))
	, m_snapshot(std::move(snapshot))
	, m_temps(std::move(temps))
{
	m_members.Bind(this, &ibValueQuerySelect::FillMembers);
}

ibValueQuerySelect::~ibValueQuerySelect() = default;

ibValue ibValueQuerySelect::ReadColumn(const ibQueryLowering::OutputColumn& oc) const
{
	// A reference / enum / composite dot-walk leaf reassembles from its prefixed field spread.
	if (m_flat != nullptr) {
		if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
			return m_flat->GetColumnObject(oc.m_objectPrefix, oc.m_col);
		return oc.m_byAlias ? m_flat->GetColumn(oc.m_alias) : m_flat->GetValue(oc.m_col);
	}
	if (m_tree != nullptr)
		return oc.m_byAlias ? m_tree->GetColumn(oc.m_alias) : m_tree->GetValue(oc.m_col);
	return ibValue();
}

void ibValueQuerySelect::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Next"),        wxT("Next()"));                    // advance the cursor
	helper.AppendFunc(wxT("Reset"),       wxT("Reset()"));                   // rewind (grouped selection)
	helper.AppendFunc(wxT("HasChildren"), wxT("HasChildren()"));             // current node is an expandable folder
	// …and WHICH BRANCH, where the totals forked (SPLIT). Both optional: no method is a flat walk, no
	// branch is every branch in order.
	helper.AppendFunc(wxT("Select"),   2, wxT("Select(method?, branch?)")); // descend into the node's child sub-selection
	helper.AppendFunc(wxT("Total"),    1, wxT("Total(name : string)"));      // grand total over the whole result
	helper.AppendFunc(wxT("Level"),       wxT("Level()"));                   // node depth (0 = grand total / flat row)

	// Output columns are read DIRECTLY as attributes (s.ColumnName) — no Field() accessor.
	for (size_t i = 0; i < m_schema.size(); ++i)
		helper.AppendProp(m_schema[i].m_name, true, false, static_cast<long>(i));
}

bool ibValueQuerySelect::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                    ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enNext:
		pvarRetValue = m_flat != nullptr ? m_flat->Next() : (m_tree != nullptr && m_tree->Next());
		return true;

	case enReset:
		if (m_tree != nullptr) m_tree->Reset();
		return true;   // a forward cursor cannot rewind — re-Execute the Query to re-read

	case enLevel:   // node depth: 0 for a flat row / grand total, deeper for nested TOTALS nodes
		pvarRetValue = ibValue(static_cast<double>(m_tree != nullptr ? m_tree->Level() : 0));
		return true;

	case enHasChildren:
		pvarRetValue = (m_tree != nullptr) && m_tree->HasChildren();
		return true;

	case enSelect: {   // descend into the current node's child sub-selection (grouped); empty for a flat list
		if (m_tree == nullptr) { pvarRetValue = new ibValueQuerySelect(); return true; }
		const ibSelectKind kind = (lSizeArray >= 1 && paParams[0] != nullptr)
			? paParams[0]->ConvertToEnumValue<ibSelectKind>()
			: ibSelectKind::ibSelectKind_Direct;   // default — a direct (flat) walk of the node's children
		// ⭐⭐ …AND WHICH BRANCH OF IT — `Select(ByGroups, "ByCharacteristic")`, where the totals
		// forked (`SPLIT`). Named nothing, the walk goes THROUGH the forks in order and hands back
		// every group there is, which is what every script written before branches asks for and gets.
		const wxString branch = (lSizeArray >= 2 && paParams[1] != nullptr)
			? paParams[1]->GetString() : wxString();
		// ⭐ THE SHAPE OF THE WALK, in the journal: which level was descended from and how many rows
		// came back. "It shows one value" and "it shows the last of sixty-three" look identical in a
		// watch window, and this is the line that tells them apart without anyone guessing.
		ibSelector sub = branch.IsEmpty() ? m_tree->Select(kind) : m_tree->Select(kind, branch);
		const wxString into = branch.IsEmpty() ? wxString() : wxT(" into branch '") + branch + wxT("'");
		ibJournalInfo(wxT("query.walk"), wxT("descend from level %d%s: %ld node(s)"),
			m_tree->Level(), into, sub.NodeCount());
		pvarRetValue = new ibValueQuerySelect(std::move(sub), m_schema, m_snapshot, m_temps);
		return true;
	}

	case enTotal: {
		if (lSizeArray < 1 || m_tree == nullptr) { pvarRetValue = ibValue(); return true; }
		const wxString name = paParams[0]->GetString();
		for (const ibQueryLowering::OutputColumn& oc : m_schema)
			if (oc.m_name.CmpNoCase(name) == 0) {
				pvarRetValue = oc.m_byAlias ? m_tree->GetTotalColumn(oc.m_alias) : m_tree->GetTotal(oc.m_col);
				return true;
			}
		return false;
	}

	default:
		return false;
	}
}

bool ibValueQuerySelect::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long data = m_members.GetPropData(lPropNum);   // output column index — read the cell directly
	if (data < 0 || static_cast<size_t>(data) >= m_schema.size())
		return false;
	pvarPropVal = ReadColumn(m_schema[static_cast<size_t>(data)]);
	return true;
}

bool ibValueQuerySelect::SetPropVal(const long /*lPropNum*/, const ibValue& /*varPropVal*/)
{
	return false;   // a selection is read-only
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueQueryExec, "Query", value_to_clsid("VL_QURY"));
// CREATABLE (`New TempTablesManager()`) — it is a thing an author makes and holds, not something
// the platform hands back, so it registers as a value type like Query does.
VALUE_TYPE_REGISTER(ibValueTempTablesManager, "TempTablesManager", value_to_clsid("VL_TTMG"));
SYSTEM_TYPE_REGISTER(ibValueQueryResult, "QueryResult", system_to_clsid("VL_QRES"));
SYSTEM_TYPE_REGISTER(ibValueQuerySelect, "QuerySelect", system_to_clsid("VL_QSEL"));
ENUM_TYPE_REGISTER(ibValueEnumQuerySelectKind, "QueryResultIteration", enum_to_clsid("EN_QSEL"));
