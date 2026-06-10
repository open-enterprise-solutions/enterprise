////////////////////////////////////////////////////////////////////////////
//	L4-1 — runtime script value objects "Query" / "QueryResult" / "QuerySelect"
////////////////////////////////////////////////////////////////////////////

#include "valueQuery.h"

#include "backend/query/queryParser.h"
#include "backend/compiler/typeCtor.h"   // VALUE_TYPE_REGISTER / SYSTEM_TYPE_REGISTER / ENUM_TYPE_REGISTER
#include "backend/appData.h"             // appData->DesignerMode()

//////////////////////////////////////////////////////////////////////
// ibValueQueryExec — script "Query"
//////////////////////////////////////////////////////////////////////

void ibValueQueryExec_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(1, wxT("Query(text : string)"));
	helper.AppendProc(wxT("SetParameter"), 2, wxT("SetParameter(name : string, value)"));
	helper.AppendFunc(wxT("Execute"), wxT("Execute()"));
}

bool ibValueQueryExec::Init(ibValue** paParams, const long lSizeArray)
{
	m_text = (lSizeArray >= 1) ? paParams[0]->GetString() : wxString();

	// An empty query is a valid, AST-less object: its method surface (SetParameter / Execute) comes from
	// the static bind, NOT from the AST — so `New Query()` still completes and introspects. Execute() on an
	// AST-less Query yields an empty QueryResult.
	if (m_text.IsEmpty()) {
		m_ast = nullptr;
		return true;
	}

	// Designer: tolerate a malformed / half-typed query so the value (and its method chain) stays reachable
	// for autocomplete. Runtime: a syntax error throws ibBackendException (line:pos) as before.
	if (appData->DesignerMode()) {
		try { m_ast = ibQueryParser().Parse(m_text); }
		catch (const ibBackendException&) { m_ast = nullptr; }
		return true;
	}

	m_ast = ibQueryParser().Parse(m_text);
	return m_ast != nullptr;
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

bool ibValueQueryExec::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                  ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	if (lMethodNum != enExecute)
		return false;

	if (m_ast) {
		// Run the read; a TOTALS query stamps the fold config on the result. res.Select() turns it into the
		// walkable selection (flat OR grouped). One pipeline: Query.Execute() -> QueryResult -> .Select().
		try {
			std::vector<ibQueryLowering::OutputColumn> schema;
			ibDataQueryResult r = m_ast->m_hasTotals ? ibQueryLowering::ExecuteTotals(*m_ast, m_params, schema)
			                                         : ibQueryLowering::Execute(*m_ast, m_params, schema);
			pvarRetValue = new ibValueQueryResult(std::move(r), std::move(schema), m_ast->m_hasTotals);
			return true;
		}
		catch (...) {
			if (!appData->DesignerMode())
				throw;   // runtime: surface the real execution error (rethrows the in-flight exception, type intact)
			// designer: editing / autocomplete has no live session to read from, and a session-less read can throw
			// beyond ibBackendException — degrade to an empty (method-only) result instead of failing, so the
			// .Execute().Select()… chain stays introspectable. (The editor's own eval would otherwise swallow the
			// throw and leave the chain object undefined → no Select in the dropdown.)
		}
	}

	// AST-less (empty query) OR designer-degraded — an empty QueryResult keeps the method chain alive.
	pvarRetValue = new ibValueQueryResult();
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
                                       std::vector<ibQueryLowering::OutputColumn> schema, bool hasTotals)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_result(std::make_unique<ibDataQueryResult>(std::move(result)))
	, m_schema(std::move(schema))
	, m_hasTotals(hasTotals)
{
	m_members.Bind(this, &ibValueQueryResult::FillMembers);
}

ibValueQueryResult::~ibValueQueryResult() = default;

void ibValueQueryResult::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Select"), 1, wxT("Select(method?)"));   // -> a QuerySelect (consumes the result)
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
	if (kind == ibSelectKind::ibSelectKind_Direct && !m_hasTotals)
		pvarRetValue = new ibValueQuerySelect(std::move(m_result), m_schema);
	else
		pvarRetValue = new ibValueQuerySelect(m_result->Select(kind), m_schema);
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
                                       std::vector<ibQueryLowering::OutputColumn> schema)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_flat(std::move(flat))
	, m_schema(std::move(schema))
{
	m_members.Bind(this, &ibValueQuerySelect::FillMembers);
}

ibValueQuerySelect::ibValueQuerySelect(ibSelector&& tree,
                                       std::vector<ibQueryLowering::OutputColumn> schema)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
	, m_tree(std::make_unique<ibSelector>(std::move(tree)))
	, m_schema(std::move(schema))
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
	helper.AppendFunc(wxT("Select"),   1, wxT("Select(method?)"));           // descend into the node's child sub-selection
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
		pvarRetValue = new ibValueQuerySelect(m_tree->Select(kind), m_schema);
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

VALUE_TYPE_REGISTER(ibValueQueryExec, "Query", string_to_clsid("VL_QURY"));
SYSTEM_TYPE_REGISTER(ibValueQueryResult, "QueryResult", string_to_clsid("VL_QRES"));
SYSTEM_TYPE_REGISTER(ibValueQuerySelect, "QuerySelect", string_to_clsid("VL_QSEL"));
ENUM_TYPE_REGISTER(ibValueEnumQuerySelectKind, "QueryResultIteration", string_to_clsid("EN_QSEL"));
