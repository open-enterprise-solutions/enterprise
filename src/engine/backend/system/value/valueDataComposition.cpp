#include "backend/system/value/valueDataComposition.h"
#include "backend/appData.h"                         // appData / GetActiveMetaData
#include "backend/query/queryable.h"                 // ibBackendQueryable
#include "backend/query/queryableFactory.h"          // ibQueryableSourceDescriptor (source holder + its command surface)
#include "backend/query/queryColumn.h"               // ibBackendQueryColumn::GetColumnId
#include "backend/query/queryParser.h"               // the query is READ by the engine's own parser
#include "backend/query/queryLexer.h"                // ibQueryLexer::ParamNames — the parameters the TEXT asks for
#include "backend/system/systemManager.h"            // system functions
#include "backend/compiler/procUnit.h"                // ibProcUnit::Evaluate — a parameter expression is run here
#include "backend/compiler/procContext.h"             // ibRunContext — the frame an expression is evaluated in
#include "backend/moduleManager/moduleManager.h"       // the session root: common modules + the environment
#include "backend/session/session.h"                   // ibSession::Current / GetPUState — where the root lives
#include "backend/system/value/valueType.h"           // ibValueTypeDescription::AdjustValue — the result decides the type
#include "backend/query/queryConstructorModel.h"      // ibQueryConstructorModel — the fields a window offers
#include "backend/query/queryRender.h"               // ibRenderQuery / ibQueryColumnFromPath — the seed query, written out
#include "backend/backend_exception.h"               // the engine's verdict on a query that will not resolve
#include "backend/srcDataObject.h"                   // ibSourceExplorer
#include "backend/serialize/dataBuilder.h"           // ibDataNode (object-level save/load)
#include "backend/metadataConfiguration.h"           // ibMetaDataConfigurationBase (GetSourceMetaData)
#include "backend/composition/spreadsheetComposeDriver.h"   // the OUTPUT — composition → document
#include "backend/metaCollection/partial/dataReport.h"      // the owning report, whose Composing speaks first
#include "backend/system/value/valueSpreadsheet.h"   // ibValueSpreadsheetDocument — the script-side document
#include "backend/value_cast.h"                      // CastValue — the script argument to its type
#include "backend/job/jobManager.h"                  // ibJobManager / ibBackgroundRun — the rented read

namespace {

// Columns over the QUERY's output schema. A query's column is an ibBackendQueryColumn exactly as a
// queryable's is (the lowering hands one back for every output, synthesising it where the column is
// computed) — the same idea of a column, reached a different way.
// (GONE with the list surface — see the note below.)

// The same collection straight off the SOURCE — what the composer offers between picking a source and
// the query resolving (a query being edited, or one the engine refused).
// ⏳ THE DATAVIEW SURFACE IS GONE, and it was never a composition's. Two COLUMN COLLECTIONS (one over
// the query's output schema, one straight off the source) and a script RETURN LINE lived here so a
// composition could be shown in a table — copied, with the cursor model, from the dynamic list.
//
// A composition is not read row by row into a grid: it is read ONCE and written into a SHEET. What
// the query produces is kept as the output SCHEMA (m_querySchema) and laid out by the compose driver,
// which places it BY ROLE — dimensions down one column, measures each in their own (Max, 2026-08-20:
// "a spreadsheet document has none of that — no dataview, no nodes").
} // namespace

// ===========================================================================
//  ibValueDataComposition
// ===========================================================================

ibValueDataComposition::ibValueDataComposition(const ibBackendQueryable* queryable)
	: ibValueSpreadsheetModel(), ibSourceDataObject()
{
	// The sheet exists from the start — a control bound to a composition that has never been run
	// still has something to show, and it is the object it will stay subscribed to.
	// The script surface — Filter / Order / Group / Settings / Refresh / Compose. It used to arrive
	// with the model base that is gone; bound here, or GetPropVal / CallAsProc dispatch on indices
	// nothing ever registered.
	m_members.Bind(this, &ibValueDataComposition::FillMembers);

	// ⭐ THE LIVE SETTINGS — a FACADE over this composition's OWN store, the same TYPE a list carries
	// and nothing more: filter and sort are what the two worlds have in common (Max, 2026-08-20:
	// "two different worlds — a dynamic list has its own grouping; what is really shared is filter and
	// sort"). The composition's fold is its own, and lives in its structure, not here.
	//
	// ⚠ NO REFRESH here, and that is the other half of the difference. A list re-reads the moment its
	// settings change, because what it shows IS the query's current answer. A report does not: the
	// sheet on screen is the one that was BUILT, and it is rebuilt when somebody says so.
	//
	// ⭐ WHAT THE CALLBACK CARRIES INSTEAD IS THE CHANGE SIGNAL. The hook was already here and stood
	// empty; a filter or a sort written through the live facade is a change like any other, and
	// saying so is not the same act as re-reading (Max, 2026-08-20: "we changed the value, we do not
	// have to press OK — it counts as changed already").
	//
	// Built here, not lazily: unlike a model, this composition's store is its OWN member and already
	// exists by this line.
	m_listSettings = new ibValueListSettings(m_composer, [this] { OnChildChanged(); });

	// THERE IS ALWAYS A VARIANT, from the first moment — a composition built from script or
	// generated by a report form has one too, so no window has to create it.
	EnsureVariant();
	if (queryable != nullptr)
		SetSourceQueryable(queryable);   // null → set later via SetSource
}

// 🛑 THE READ IS WAITED OUT BEFORE THE COMPOSITION GOES. The run holds this object's settings, its
// parameters and the sheet it is filling; letting it outlive the composition is a use-after-free with
// a delay on it. Cancel is cooperative and the walk polls per row, so this returns at the next one.
ibValueDataComposition::~ibValueDataComposition() { CancelFetch(); }

// --- source & query ---------------------------------------------------------

void ibValueDataComposition::SetSource(const wxString& ns, const wxString& name)
{
	const ibBackendQueryable* before = GetSourceQueryable();
	m_propertySource->SetSource(ns, name);   // resolves the queryable INTO the property variant
	if (GetSourceQueryable() != before) {    // only on a REAL change — re-picking the SAME source keeps the settings
		RebuildSource();
		OnChildChanged();
	}
}

void ibValueDataComposition::SetSourceQueryable(const ibBackendQueryable* queryable)
{
	if (queryable == GetSourceQueryable())   // same source — leave columns / composer / settings untouched
		return;
	m_propertySource->SetQueryable(queryable);   // the queryable lives in the property, not here
	RebuildSource();
	OnChildChanged();
}

void ibValueDataComposition::SetQueryText(const wxString& text)
{
	if (text == GetQueryText())
		return;
	m_propertyQuery->SetValue(text);

	// ⚠ NO SIGNAL HERE, deliberately — the CALLER raises it. This runs on every keystroke, and what
	// hears the signal is not always cheap: a composition held by a form attribute re-renders the
	// whole form editor when it does (control tree, object tree, attribute tree, drop targets), so a
	// per-character signal made typing in a query redraw a form per character. The caller already
	// coalesces the work onto the pause after typing, which is where saying "it changed" belongs
	// too — and to a person "when I stop typing for an instant" is still immediate.

	// ⭐⭐ AND IT DOES NOT RE-READ THE SOURCE. Storing what the text IS and working out what it MEANS
	// are two acts, and the second one is expensive — it re-describes the query, re-syncs the
	// parameters and prunes settings that no longer resolve. They were one call because the only
	// caller pressed a button; now the editor stores on every keystroke and asks for the meaning
	// when the typing stops, so the two moments are no longer the same moment.
	//
	// The verb for the second act already exists and is public: ApplySource(). Nothing new was
	// added here — one call was moved out to the caller that always made it anyway.
}

void ibValueDataComposition::RebuildSource()
{
	// STORE the source config into the metadata VARIABLE, taken from the VALUE — the picked queryable's
	// metaobject knows its config (the edited one in the designer, the copy's on a copy). Held so
	// GetSourceMetaData stays TERMINAL: it never re-resolves the queryable through the attach owner,
	// which walks the form and would come back here.
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable != nullptr) {
		if (const ibValueMetaObjectGenericData* mo = queryable->GetSourceMetaObject())
			m_sourceMetaData = mo->GetMetaData();
	}

	// Thread the stored config into the composer so the query text resolves its by-name FROM against
	// THIS config rather than the global factory.
	GetModelComposer().SetMetaData(GetSourceMetaData());

	// SEED ON FIRST SIGHT OF A SOURCE. A composer declares what to read; a source with no query text
	// reads nothing, and an empty text is not a query. So the moment a source exists and the text does
	// not, the source writes the query it would write for itself — something that already runs and can
	// be opened in the query constructor as it stands.
	if (queryable != nullptr && GetQueryText().IsEmpty()) {
		const wxString seed = SeedQuery();
		if (!seed.IsEmpty())
			m_propertyQuery->SetValue(seed);
	}

	m_querySchema.clear();
	m_queryError.clear();

	const wxString queryText = GetQueryText();

	// ⭐ THE PARAMETERS ARE READ FROM THE TEXT FIRST — before anything tries to describe the query.
	// A `&Ref` in the text IS a parameter of this composition, whether or not anybody filled it in,
	// so the page shows it the moment the text mentions it (and it cannot be removed there: the text
	// is what put it in).
	SyncParametersWithQuery();

	if (!queryText.IsEmpty()) {
		GetModelComposer().FromText(queryText);

		// The names resolve against the SOURCE's config, the same one the composer was just handed.
		const ibSourceMetaDataScope scope(GetSourceMetaData());
		try {
			ibQueryParser parser;
			const ibQuerySelectPtr ast = parser.Parse(queryText);
			// 🛑 A COMPOSITION DOES NOT TAKE `TOTALS` (Max, 2026-08-19). Its totals ARE the
			// resources, and its levels ARE the groupings in the settings — the composer renders
			// the TOTALS clause itself from those two. A TOTALS written into the text would be the
			// same setting stated twice, in a place no window can show or edit, and the two would
			// disagree the moment either side changed.
			//
			// Refused OUT LOUD, in the query error, rather than ignored: a clause that is silently
			// dropped looks like a clause that did not work.
			if (ast && (!ast->m_totalsBy.empty() || !ast->m_totalsAggregates.empty() || ast->m_totalsOverall))
				ibBackendCoreException::Error(
					_("A composition does not take TOTALS: its totals are the resources and its levels are the groupings"));
			// ⭐ …AND THE PARAMETERS GO WITH IT. Describing a query resolves its names, and a name it
			// cannot resolve is a parameter nobody has set — which is exactly what an unfilled `&Ref`
			// is while the query is still being WRITTEN (Max: "how is it supposed to be set here, this
			// is the constructor?"). So every parameter the composition knows about is handed over,
			// with whatever it holds — an empty value included. Empty is an ANSWER: the shape of the
			// result does not depend on it, and describing the shape is all this is for.
			if (ast)
				ibQueryLowering::DescribeOutput(*ast, ParameterValues(), m_querySchema);
		}
		catch (const ibBackendException& error) {
			// AT ONCE, AND IN THE ENGINE'S WORDS. A query that cannot be described is a query that
			// cannot be run, and learning that when the report is first composed — in front of a user
			// rather than its author — is the thing worth avoiding.
			m_queryError = error.GetErrorDescription();
			m_querySchema.clear();
		}
	}

	// (No column COLLECTION is built any more — what resolved is kept as the output SCHEMA above,
	//  and every field list in the settings window reads that. The collection existed only to feed
	//  a dataview.)

	if (queryText.IsEmpty() && queryable != nullptr)
		GetModelComposer().FromSource(queryable);   // no text yet — read the source plainly

	PruneUnresolvedSettings();   // the query decides which fields exist; the settings follow it
}

// THE QUERY THE SOURCE WOULD WRITE FOR ITSELF — a real query over the real source, rendered by the same
// renderer the constructor round-trips through. Every column spelled out rather than `SELECT *`: the
// point of a composer is to CHANGE what is read, and changing a list of fields you can see is an edit,
// while changing a star is a rewrite.
wxString ibValueDataComposition::SeedQuery() const
{
	const ibBackendQueryable* queryable = GetSourceQueryable();
	if (queryable == nullptr)
		return wxEmptyString;

	// THE NAME THE LANGUAGE KNOWS IT BY — `Catalog.Products`, which is what the lowering resolves a FROM
	// against. Not the physical table: a query written against one is a query the config cannot be
	// restructured under.
	const ibQueryableSourceDescriptor* descriptor = GetSourceDescriptor();
	if (descriptor == nullptr || descriptor->GetName().IsEmpty())
		return wxEmptyString;

	ibQuerySelect select;
	if (!descriptor->GetNamespace().IsEmpty())
		select.m_from.m_name.push_back(descriptor->GetNamespace());
	select.m_from.m_name.push_back(descriptor->GetName());

	// NAMED, AND EVERY FIELD QUALIFIED BY THAT NAME — a bare `Code` is unambiguous only until a SECOND
	// table joins, and at that moment every field written before it becomes ambiguous at once. The same
	// rule the query constructor holds to when it adds a table.
	select.m_from.m_alias = descriptor->GetName();
	const wxString prefix = select.m_from.m_alias + wxT(".");

	for (const ibBackendQueryColumn* column : queryable->GetColumns()) {
		if (column == nullptr || column->GetName().IsEmpty() || !column->IsAllowed())
			continue;
		ibQueryProjection projection;
		projection.m_expr = ibQueryColumnFromPath(prefix + column->GetName());
		select.m_projections.push_back(std::move(projection));
	}
	if (select.m_projections.empty())
		select.m_selectAll = true;   // a source that vends no columns: read it whole rather than read nothing

	return ibRenderQuery(select);
}

std::vector<ibQueryConstructorField> ibValueDataComposition::GetConstructorFields() const
{
	std::vector<ibQueryConstructorField> fields;

	const wxString text = GetQueryText();
	if (text.IsEmpty())
		return fields;

	// The names resolve against the SOURCE's config — the same one the composer was handed.
	const ibSourceMetaDataScope scope(GetSourceMetaData());
	try {
		ibQueryParser parser;
		const ibQueryPackage package = parser.ParsePackage(text);
		if (package.m_statements.empty())
			return fields;

		// THE LAST STATEMENT is the one that produces the result — a package builds temp tables
		// and reads them at the end, so its fields are the ones a resource or a level is written
		// over. Earlier statements are handed in as context so a temp table's own fields resolve.
		const size_t last = package.m_statements.size() - 1;
		const ibQuerySelectPtr select = package.m_statements[last].m_select;
		if (!select)
			return fields;

		const ibQueryConstructorModel model(GetSourceMetaData());
		fields = model.FieldsOfSelect(*select, package, last);
	}
	catch (const ibBackendException&) {
		fields.clear();   // half-typed text offers nothing yet — see the header
	}
	return fields;
}

// DROP THE SETTINGS WHOSE FIELD THE COMPOSITION NO LONGER HAS — by RESOLUTION after every rebuild,
// never by chasing the change. A field removed from the query leaves a filter, a sort or a grouping
// pointing at nothing; so does an attribute renamed in the configuration. The answer comes from the
// SOURCE EXPLORER, the same list the pickers are built from, so what a person can still choose and what
// the composer still keeps are one answer rather than two.
void ibValueDataComposition::PruneUnresolvedSettings()
{
	const ibSourceExplorer* explorer = GetSourceExplorer();
	if (explorer == nullptr || explorer->GetHelperCount() == 0)
		return;   // nothing to check against is not a verdict — leave the settings alone

	GetModelComposer().PruneUnresolvedSettings([explorer](const wxString& path) {
		// THE FIRST SEGMENT is what has to exist here: the rest is a reference walk, and a walk
		// resolves through the metadata of whatever the first segment turned out to be.
		const wxString head = path.BeforeFirst(wxT('.'));
		if (head.IsEmpty())
			return true;
		return explorer->FindByName(head) != nullptr;
	});
}

// ⭐ NOTHING TO COMMIT, AND THAT IS THE POINT. The settings object is a live FACADE writing the
// composer directly and the settings window commits on OK, so by the time anybody asks, the store
// already says what the settings say.
//
// ⚠ AND NOTHING TO REFRESH EITHER — a report is not a list. What is on screen is the sheet that was
// BUILT; it is rebuilt when somebody says Compose, not the moment a condition is edited. The call
// stays because callers speak it ("apply my settings") and because that is where a future
// stale-marker belongs.
void ibValueDataComposition::RefreshComposerSettings()
{
}

// ===========================================================================
//  Variants — N snapshots of the settings, one of them active
// ===========================================================================
//
// ⭐ WHAT A VARIANT IS (Max, 2026-08-19): a SNAPSHOT of the settings — its own groupings, its own
// filter, its own sort, "as if every variant were a page of settings of its own". One report then
// answers "sales" and "sales plus turnover" by having the person pick a variant, instead of being
// two reports. Parameters will join the snapshot when the composition grows them.
//
// ⚠ THE COMPOSER HOLDS EXACTLY ONE SET OF SETTINGS, and that is deliberate: the fetch, the compose
// and every reader below it stay unaware that variants exist at all. A variant becomes real by
// being LOADED INTO the composer, and the two doors that do it are the same pair the settings
// dialog already uses — `ibCommitSettingsToComposer` (snapshot → store) and
// `ibLoadSettingsFromComposer` (store → snapshot). No third road, no copy of the rules.
namespace {
// The node a variant is written into. Not a registered value type — a variant is a record inside
// the composition, not something a script hands around.
const ibClassID g_variantNodeClsid = make_clsid(wxT("CompositionVariant"), ibClassKind_None);
const wxString  kVariantName       = wxT("Name");
const wxString  kActiveVariant     = wxT("ActiveVariant");

// ---------------------------------------------------------------------------
// THE STRUCTURE OF A VARIANT — its outputs, their levels, and the fields inside them.
//
// The flat filter / sort / grouping a variant has always written cannot say any of this: a level
// made of several fields is indistinguishable there from several levels, and an output beside the
// first — or an axis of columns — has nowhere to go at all. So the structure is written as itself,
// beside the settings rather than instead of them.
//
// Without it everything the settings window builds lives until the file is closed and no further,
// which is exactly what "external reports do not serialise their groupings" was.
// ---------------------------------------------------------------------------
const ibClassID g_outputNodeClsid = make_clsid(wxT("CompositionOutput"), ibClassKind_None);
const ibClassID g_levelNodeClsid  = make_clsid(wxT("CompositionLevel"),  ibClassKind_None);
const ibClassID g_fieldNodeClsid  = make_clsid(wxT("CompositionField"),  ibClassKind_None);
const wxString  kStructureNode  = wxT("Structure");
const wxString  kRowsNode       = wxT("Rows");
const wxString  kColumnsNode    = wxT("Columns");
const wxString  kSelectedNode   = wxT("Selected");
const wxString  kAvailableNode  = wxT("Available");
const wxString  kSourceText     = wxT("Source");
const wxString  kSelectedAuto   = wxT("SelectedAuto");
const wxString  kAvailableAuto  = wxT("AvailableAuto");
const wxString  kPathName       = wxT("Path");
const wxString  kKindName       = wxT("Kind");
// A LEVEL'S OWN SORT AND FILTER — written inside the level, because that is where they belong.
const ibClassID g_sortNodeClsid = make_clsid(wxT("CompositionSortLine"), ibClassKind_None);
const wxString  kSortNode       = wxT("Sort");
const wxString  kFilterNode     = wxT("Filter");
const wxString  kAscendingName  = wxT("Ascending");
// WHAT THE LEVEL IS — a grouping or the detail records. Absent in a file written before detail
// levels existed, and absence reads back as Grouping, which is what every level in such a file is.
const wxString  kLevelKindName  = wxT("LevelKind");

void WriteFieldList(ibDataNode& node, const wxString& name, const std::vector<wxString>& list)
{
	if (list.empty())
		return;   // an empty set writes nothing — absence reads back as absence
	ibDataNode& sub = node.Child(name);
	for (size_t i = 0; i < list.size(); ++i)
		sub.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(i)).SetValue<wxString>(kPathName, list[i]);
}

void ReadFieldList(const ibDataNode& node, const wxString& name, std::vector<wxString>& list)
{
	list.clear();
	const ibDataNode* sub = node.FindChild(name);
	if (sub == nullptr)
		return;
	for (const ibDataNode& child : sub->Children())
		if (child.GetClsid() == g_fieldNodeClsid)
			list.push_back(child.GetValue<wxString>(kPathName));
}

void WriteLevels(ibDataNode& parent, const wxString& name, const std::vector<ibDataComposer::GroupNode>& levels)
{
	if (levels.empty())
		return;
	ibDataNode& axis = parent.Child(name);
	for (size_t i = 0; i < levels.size(); ++i) {
		const ibDataComposer::GroupNode& level = levels[i];
		ibDataNode& node = axis.AddChild(g_levelNodeClsid, static_cast<ibMetaID>(i));
		node.SetValue<s32>(kLevelKindName, static_cast<s32>(level.m_kind));
		node.SetValue<bool>(kSelectedAuto, level.m_selectedAuto);
		node.SetValue<bool>(kAvailableAuto, level.m_availableAuto);
		WriteFieldList(node, kSelectedNode, level.m_selected);
		WriteFieldList(node, kAvailableNode, level.m_available);
		// ⭐ ITS OWN SORT AND ITS OWN FILTER (Max). A level's settings live where the level does, so
		// they are written INSIDE it: the sort as the plain list it is, the filter as the TREE it was
		// written as — the expression the engine runs is derived from that tree and is not saved,
		// because an expression cannot be edited back into the lines a person wrote.
		if (!level.m_sorts.empty()) {
			ibDataNode& order = node.Child(kSortNode);
			for (size_t s = 0; s < level.m_sorts.size(); ++s) {
				ibDataNode& line = order.AddChild(g_sortNodeClsid, static_cast<ibMetaID>(s));
				line.SetValue<wxString>(kPathName, level.m_sorts[s].m_path);
				line.SetValue<bool>(kAscendingName, level.m_sorts[s].m_ascending);
			}
		}
		if (!level.m_filterTree.IsEmpty())
			level.m_filterTree.Serialize(node.Child(kFilterNode));

		// THE ELEMENTS, IN ORDER — a level groups by all of them together, and each carries its own
		// unfold. Order is the order they are read in, so it is written as position.
		for (size_t f = 0; f < level.m_fields.size(); ++f) {
			ibDataNode& field = node.AddChild(g_fieldNodeClsid, static_cast<ibMetaID>(f));
			field.SetValue<wxString>(kPathName, level.m_fields[f].m_path);
			field.SetValue<s32>(kKindName, static_cast<s32>(level.m_fields[f].m_kind));
		}
	}
}

void ReadLevels(const ibDataNode& parent, const wxString& name, std::vector<ibDataComposer::GroupNode>& levels)
{
	levels.clear();
	const ibDataNode* axis = parent.FindChild(name);
	if (axis == nullptr)
		return;
	for (const ibDataNode& node : axis->Children()) {
		if (node.GetClsid() != g_levelNodeClsid)
			continue;
		ibDataComposer::GroupNode level;
		level.m_kind          = static_cast<ibCompositionLevelKind>(node.GetValue<s32>(kLevelKindName));
		level.m_selectedAuto  = node.GetValue<bool>(kSelectedAuto);
		level.m_availableAuto = node.GetValue<bool>(kAvailableAuto);
		ReadFieldList(node, kSelectedNode, level.m_selected);
		ReadFieldList(node, kAvailableNode, level.m_available);

		// ITS OWN SORT AND FILTER, back the way they were written. The filter comes back as the
		// TREE; the expression the engine runs is built from it once the composer is in hand
		// (RebuildLevelFilters), because building it needs the parameters the composer names.
		if (const ibDataNode* order = node.FindChild(kSortNode))
			for (const ibDataNode& line : order->Children()) {
				const wxString path = line.GetValue<wxString>(kPathName);
				if (!path.IsEmpty())
					level.m_sorts.push_back({ path, line.GetValue<bool>(kAscendingName) });
			}
		if (const ibDataNode* filter = node.FindChild(kFilterNode))
			level.m_filterTree = ibValue::FromNode(*filter);

		for (const ibDataNode& field : node.Children()) {
			if (field.GetClsid() != g_fieldNodeClsid)
				continue;
			ibDataComposer::TotalByItem item;
			item.m_path = field.GetValue<wxString>(kPathName);
			item.m_kind = static_cast<ibQueryDimUnfold>(field.GetValue<s32>(kKindName));
			if (!item.m_path.IsEmpty())
				level.m_fields.push_back(item);
		}
		levels.push_back(std::move(level));
	}
}

void WriteStructure(ibDataNode& node, const std::vector<ibDataComposer::Output>& outputs)
{
	ibDataNode& structure = node.Child(kStructureNode);
	for (size_t i = 0; i < outputs.size(); ++i) {
		const ibDataComposer::Output& output = outputs[i];
		ibDataNode& sub = structure.AddChild(g_outputNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<wxString>(kVariantName, output.m_name);
		sub.SetValue<wxString>(kSourceText, output.m_sourceText);
		sub.SetValue<bool>(kSelectedAuto, output.m_selectedAuto);
		sub.SetValue<bool>(kAvailableAuto, output.m_availableAuto);
		WriteFieldList(sub, kSelectedNode, output.m_selected);
		WriteFieldList(sub, kAvailableNode, output.m_available);
		WriteLevels(sub, kRowsNode, output.m_rowGroups);
		WriteLevels(sub, kColumnsNode, output.m_columnGroups);
	}
}

bool ReadStructure(const ibDataNode& node, std::vector<ibDataComposer::Output>& outputs)
{
	const ibDataNode* structure = node.FindChild(kStructureNode);
	if (structure == nullptr)
		return false;   // written before there was a structure — the caller keeps what it had

	std::vector<ibDataComposer::Output> read;
	for (const ibDataNode& sub : structure->Children()) {
		if (sub.GetClsid() != g_outputNodeClsid)
			continue;
		ibDataComposer::Output output;
		output.m_name          = sub.GetValue<wxString>(kVariantName);
		output.m_sourceText    = sub.GetValue<wxString>(kSourceText);
		output.m_selectedAuto  = sub.GetValue<bool>(kSelectedAuto);
		output.m_availableAuto = sub.GetValue<bool>(kAvailableAuto);
		ReadFieldList(sub, kSelectedNode, output.m_selected);
		ReadFieldList(sub, kAvailableNode, output.m_available);
		ReadLevels(sub, kRowsNode, output.m_rowGroups);
		ReadLevels(sub, kColumnsNode, output.m_columnGroups);
		read.push_back(std::move(output));
	}
	if (read.empty())
		return false;
	outputs = std::move(read);
	return true;
}
}

void ibValueDataComposition::EnsureVariant()
{
	// THERE IS ALWAYS ONE. Held here rather than in a window, so a composition built from script or
	// generated by a report form has it too.
	if (m_variants.empty()) {
		ibCompositionVariant first;
		first.m_name = _("Main");
		first.m_settings = new ibValueListSettings();   // BUFFER mode — own storage
		m_variants.push_back(first);
		m_activeVariant = 0;
	}
	if (m_activeVariant >= m_variants.size())
		m_activeVariant = m_variants.size() - 1;
}

wxString ibValueDataComposition::GetVariantName(size_t idx) const
{
	return idx < m_variants.size() ? m_variants[idx].m_name : wxString();
}

bool ibValueDataComposition::SetVariantName(size_t idx, const wxString& name)
{
	if (idx >= m_variants.size() || name.IsEmpty())
		return false;   // a nameless variant is unpickable — the name is how it is chosen
	m_variants[idx].m_name = name;
	OnChildChanged();
	return true;
}

// SNAPSHOT → COMPOSER. The filter travels as the TREE it is: the composer takes it as one
// expression and cannot hand it back, so the model's live settings carry the tree beside it.
void ibValueDataComposition::ApplyActiveVariant()
{
	EnsureVariant();
	ibValueListSettings* snapshot = m_variants[m_activeVariant].m_settings;
	if (snapshot == nullptr)
		return;

	ibCommitSettingsToComposer(GetModelComposer(), snapshot);

	// ⭐ THE STRUCTURE LANDS AFTER THE FLAT SETTINGS, and overrides what they rebuilt: the commit
	// above re-creates the grouping ladder from a list that cannot hold a level of several fields,
	// so applying the variant's own outputs last is what makes such a level survive a switch.
	if (!m_variants[m_activeVariant].m_structure.empty())
		GetModelComposer().Outputs() = m_variants[m_activeVariant].m_structure;

	// AND THE LIVE FILTER TREE FOLLOWS — as a COPY, and through a BUFFER.
	//
	// 🛑 NOT `ibLoadSettingsFromComposer(live, …)`. The model's settings object is a FACADE over the
	// composer: its `Clear()` clears the COMPOSER. That function clears what it is filling and then
	// reads the composer for the lines to put back — correct for a buffer, and self-erasing for a
	// facade. Applied to `live` it wiped the sorts and groupings a variant had just installed, which
	// is why switching to another variant and back came back empty (seen live 2026-08-19).
	//
	// So the copy is made into a BUFFER (which is what that function is for) and only the tree —
	// the one thing the composer cannot hand back — is put onto the live settings.
	if (ibValueListSettings* live = GetListSettings()) {
		ibValuePtr<ibValueListSettings> copy(new ibValueListSettings());
		ibLoadSettingsFromComposer(copy, GetModelComposer(), snapshot);
		if (ibValueFilterGroup* tree = copy->GetFilterRoot())
			live->SetFilterRoot(tree);
	}

	// …AND EVERY LEVEL'S OWN CONDITION, built from the tree it was written as. The structure that
	// just landed carries trees, not expressions: an expression is what the engine reads and it is
	// DERIVED, so it is made here — once, where the composer is in hand.
	RebuildLevelFilters();
}

// THE EXPRESSION IS DERIVED FROM THE TREE, everywhere and always. A level keeps the lines a person
// wrote (that is what reopens in the editor); the engine reads a condition built from them. So this
// runs wherever levels ARRIVE from outside — a file, a variant switch — and nowhere else: a level
// edited in the window commits both at once.
void ibValueDataComposition::RebuildLevelFilters()
{
	ibDataComposer& composer = GetModelComposer();
	for (ibDataComposer::Output& output : composer.Outputs()) {
		std::vector<ibDataComposer::GroupNode>* axes[] = { &output.m_rowGroups, &output.m_columnGroups };
		for (std::vector<ibDataComposer::GroupNode>* axis : axes)
			for (ibDataComposer::GroupNode& level : *axis) {
				ibValueFilterGroup* root = nullptr;
				if (!level.m_filterTree.ConvertToValue(root) || root == nullptr)
					continue;   // no tree written — the level simply has no condition of its own
				level.m_filterAst = ibBuildFilterAst(composer, root);
				++level.m_filterAstVersion;
			}
	}
}

bool ibValueDataComposition::SetActiveVariant(size_t idx)
{
	if (idx >= m_variants.size())
		return false;
	m_activeVariant = idx;
	ApplyActiveVariant();
	// ⚠ SWITCHING IS WRITING. This composition holds ONE set of settings at a time, so activating
	// another variant overwrites them — it is not a preview, which is exactly why the settings panel
	// snapshots everything on open in order to offer Cancel.
	OnChildChanged();
	return true;
}

// COMPOSER → SNAPSHOT. What a settings window edits is the composer; this is the moment those edits
// become the variant's own.
void ibValueDataComposition::CaptureActiveVariant()
{
	EnsureVariant();
	if (ibValueListSettings* snapshot = m_variants[m_activeVariant].m_settings)
		ibLoadSettingsFromComposer(snapshot, GetModelComposer(), GetListSettings());
	// THE STRUCTURE TRAVELS WITH IT. It is not expressible in the flat snapshot above — a level of
	// several fields, a second output, an axis of columns — so it is captured as itself.
	m_variants[m_activeVariant].m_structure = GetModelComposer().Outputs();
}

size_t ibValueDataComposition::AddVariant(const wxString& name, int copyFrom)
{
	EnsureVariant();

	ibCompositionVariant added;
	added.m_name = name.IsEmpty() ? _("Variant") : name;
	added.m_settings = new ibValueListSettings();

	// A COPY COPIES EVERYTHING — groupings, filter, sorts (Max). Through the node, which is the one
	// description of what a settings object consists of: a hand-written field-by-field copy is the
	// second such description and forgets whatever is added next.
	if (copyFrom >= 0 && (size_t)copyFrom < m_variants.size()) {
		if (const ibValueListSettings* source = m_variants[copyFrom].m_settings) {
			ibDataNode packed(g_variantNodeClsid, 0);
			source->WriteData(packed);
			added.m_settings->ReadData(packed);
		}
	}

	m_variants.push_back(added);
	OnChildChanged();
	return m_variants.size() - 1;
}

bool ibValueDataComposition::RemoveVariant(size_t idx)
{
	// 🛑 THE LAST ONE STAYS. A composition with no variant has no settings at all, and "there must
	// always be one, that is the whole point" (Max) — the refusal lives here so no window has to
	// remember it.
	if (idx >= m_variants.size() || m_variants.size() <= 1)
		return false;

	m_variants.erase(m_variants.begin() + idx);
	if (m_activeVariant >= m_variants.size())
		m_activeVariant = m_variants.size() - 1;
	else if (idx < m_activeVariant)
		--m_activeVariant;   // the active one shifted down with the erase

	ApplyActiveVariant();   // whatever is active now is what the composer must hold
	OnChildChanged();
	return true;
}


// ===========================================================================
//  Parameters — written beside the variants, in the same node
// ===========================================================================
//
// ⭐ A PARAMETER IS PART OF THE SETTINGS, so it is saved with them. Without this a report reopened
// with its query asking for `&Ref` and nothing to answer it: the parameter came back (the text asks
// for it) but everything a person had filled in — the value, the expression, the declared type, who
// fills it — was gone.
//
// Written as a node per parameter, beside the variant nodes and told apart by its own clsid: the
// reader walks children and takes the ones it recognises, so neither list disturbs the other.
namespace {
const ibClassID g_parameterNodeClsid = make_clsid(wxT("CompositionParameter"), ibClassKind_None);
const wxString  kParamName       = wxT("Name");
const wxString  kParamExpression = wxT("Expression");
const wxString  kParamUser       = wxT("ForUser");
const wxString  kParamFromQuery  = wxT("FromQuery");
const wxString  kParamValue      = wxT("Value");
const wxString  kParamType       = wxT("Type");
}

void ibValueDataComposition::WriteParameters(ibDataNode& node) const
{
	for (size_t i = 0; i < m_parameters.size(); ++i) {
		const ibCompositionParameter& parameter = m_parameters[i];
		ibDataNode& sub = node.AddChild(g_parameterNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<wxString>(kParamName, parameter.m_name);
		sub.SetValue<wxString>(kParamExpression, parameter.m_expression);
		sub.SetValue<s32>(kParamUser, parameter.m_userSettable ? 1 : 0);
		sub.SetValue<s32>(kParamFromQuery, parameter.m_fromQuery ? 1 : 0);
		// THE VALUE PACKS ITSELF — the same door every value serialises through, so a reference
		// travels as a reference rather than as the text it happens to render as.
		parameter.m_value.Serialize(sub.Child(kParamValue));
		ibDataValue declared;
		ibTypeDescriptionMemory::WriteNode(declared, parameter.m_type, GetSourceMetaData());
		sub.SetProperty(kParamType, declared);
	}
}

// ===========================================================================
//  Resources — the aggregates the levels fold
// ===========================================================================
//
// 🛑 THEY WERE NOT SAVED AT ALL. A resource lives in the STORE (ibDataComposer's totals) and nothing
// anywhere wrote it to a node: a variant packs only its ibValueListSettings — filter, order, group —
// and that object has no totals. So a report was saved with its groupings and reopened with the shape
// of a report and none of its numbers (found 2026-08-20, while asking why a resource edit was not
// announced: it turned out it was not even kept).
//
// ⭐ WRITTEN AT COMPOSITION LEVEL, beside the parameters, and NOT per variant — because that is what
// they actually are today: one list in one store, shared by every variant. Writing them under each
// variant would claim a snapshot the code does not take, and the next person would trust the claim.
const ibClassID g_resourceNodeClsid = make_clsid(wxT("CompositionResource"), ibClassKind_None);
const wxString  kResourceFunc = wxT("Func");
const wxString  kResourcePath = wxT("Path");

void ibValueDataComposition::WriteTotals(ibDataNode& node) const
{
	const ibDataComposer& composer = GetModelComposer();
	for (size_t i = 0; i < composer.TotalCount(); ++i) {
		wxString func, path;
		if (!composer.GetTotalAt(i, func, path) || path.IsEmpty())
			continue;
		ibDataNode& sub = node.AddChild(g_resourceNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<wxString>(kResourceFunc, func);   // empty = the path IS the whole expression
		sub.SetValue<wxString>(kResourcePath, path);
	}
}

void ibValueDataComposition::ReadTotals(const ibDataNode& node)
{
	// Read INTO the store, which is where a resource lives. Cleared first: this is a load, so what
	// the node says is the whole truth — not something to merge with whatever stood here before.
	GetModelComposer().ClearTotals();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_resourceNodeClsid)
			continue;   // variants and parameters are children here too
		const wxString path = child.GetValue<wxString>(kResourcePath);
		if (path.IsEmpty())
			continue;
		GetModelComposer().Total(child.GetValue<wxString>(kResourceFunc), path);
	}
}

void ibValueDataComposition::ReadParameters(const ibDataNode& node)
{
	std::vector<ibCompositionParameter> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_parameterNodeClsid)
			continue;   // variants are children here too — they are not parameters

		ibCompositionParameter parameter;
		parameter.m_name = child.GetValue<wxString>(kParamName);
		if (parameter.m_name.IsEmpty())
			continue;   // a nameless parameter answers nothing
		parameter.m_expression = child.GetValue<wxString>(kParamExpression);
		parameter.m_userSettable = child.GetValue<s32>(kParamUser) != 0;
		parameter.m_fromQuery = child.GetValue<s32>(kParamFromQuery) != 0;
		if (const ibDataNode* value = child.FindChild(kParamValue))
			parameter.m_value = ibValue::FromNode(*value);
		ibTypeDescriptionMemory::ReadNode(child.GetProperty(kParamType), parameter.m_type, GetSourceMetaData());
		read.push_back(parameter);
	}

	if (!read.empty())
		m_parameters = read;
}
void ibValueDataComposition::WriteVariants(ibDataNode& node) const
{
	node.SetValue<s32>(kActiveVariant, static_cast<s32>(m_activeVariant));
	for (size_t i = 0; i < m_variants.size(); ++i) {
		ibDataNode& sub = node.AddChild(g_variantNodeClsid, static_cast<ibMetaID>(i));
		sub.SetValue<wxString>(kVariantName, m_variants[i].m_name);

		// ⭐ THE ACTIVE VARIANT IS READ FROM THE COMPOSER, not from its snapshot. The composer is
		// where the active settings actually live — a script that adds a filter writes THERE, and a
		// settings window commits THERE — so the snapshot beside it is the stale copy. Reading the
		// live one here is what makes "save" mean the same thing however the change was made.
		if (i == m_activeVariant) {
			ibValuePtr<ibValueListSettings> live(new ibValueListSettings());
			ibLoadSettingsFromComposer(live, GetModelComposer(), GetListSettings());
			live->WriteData(sub);
			// …AND THE STRUCTURE, read from the composer for the same reason: the outputs it holds
			// are the live ones, and the variant's copy beside them is the stale one.
			WriteStructure(sub, GetModelComposer().Outputs());
			continue;
		}
		if (m_variants[i].m_settings)
			m_variants[i].m_settings->WriteData(sub);
		WriteStructure(sub, m_variants[i].m_structure);
	}
}

// TRUE when the node carried variants. FALSE means an older record — one set of settings, written
// before variants existed — and the caller reads it the way it was written.
bool ibValueDataComposition::ReadVariants(const ibDataNode& node)
{
	std::vector<ibCompositionVariant> read;
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() != g_variantNodeClsid)
			continue;   // the filter tree is a child of this node too — it is not a variant
		ibCompositionVariant variant;
		variant.m_name = child.GetValue<wxString>(kVariantName);
		if (variant.m_name.IsEmpty())
			variant.m_name = _("Variant");
		variant.m_settings = new ibValueListSettings();
		variant.m_settings->ReadData(child);
		ReadStructure(child, variant.m_structure);   // absent in an older file — the variant simply has none
		read.push_back(variant);
	}

	if (read.empty()) {
		EnsureVariant();
		return false;   // nothing was read — leave the composer exactly as the caller found it
	}

	m_variants = read;
	m_activeVariant = static_cast<size_t>(node.GetValue<s32>(kActiveVariant));
	EnsureVariant();
	ApplyActiveVariant();
	return true;
}


// ===========================================================================
//  Parameters — what the query asks for, and who fills it in
// ===========================================================================

// THE TEXT IS ONE OF THE TWO AUTHORS. `&Period` in the query means the composition has a parameter
// called Period, whether or not anybody added it — so the list follows the text: new names appear,
// names the text stopped mentioning disappear, and what was already FILLED IN survives an edit.
//
// Hand-made parameters are untouched: they were added deliberately (a common module reads one, or
// the text is still being written), and a re-parse is not a reason to lose them.
void ibValueDataComposition::SyncParametersWithQuery()
{
	const std::vector<wxString> named = ibQueryLexer::ParamNames(GetQueryText());

	// 1) Drop the AUTO ones the text no longer asks for.
	m_parameters.erase(
		std::remove_if(m_parameters.begin(), m_parameters.end(),
			[&named](const ibCompositionParameter& parameter) {
				if (!parameter.m_fromQuery)
					return false;
				return std::find_if(named.begin(), named.end(),
					[&parameter](const wxString& name) { return name.IsSameAs(parameter.m_name, false); }) == named.end();
			}),
		m_parameters.end());

	// 2) Add what is new. An existing entry — hand-made or already filled in — is left exactly as it
	//    is: re-reading the text must not clear a value somebody chose.
	for (const wxString& name : named) {
		const auto found = std::find_if(m_parameters.begin(), m_parameters.end(),
			[&name](const ibCompositionParameter& parameter) { return parameter.m_name.IsSameAs(name, false); });
		if (found != m_parameters.end()) {
			found->m_fromQuery = true;   // a hand-made one the text now mentions IS an auto one
			continue;
		}
		ibCompositionParameter parameter;
		parameter.m_name = name;
		parameter.m_fromQuery = true;
		m_parameters.push_back(parameter);
	}
}



// EVALUATE WHAT HAS AN EXPRESSION — once, before the read.
//
// ⭐ THE DOOR IS THE LANGUAGE'S OWN (Max, 2026-08-19): `Evaluate` — the same system function a script
// calls, so a parameter can hold `CurrentDate()`, a call into a common module, arithmetic over
// another value: anything the language can say. It is legitimate HERE and not in a computed field
// for one reason — it runs ONCE, before the query, and what reaches the query is a plain value; a
// scripted field would run per row and take the read into memory with it.
//
// ⭐ AND THE RESULT DECIDES THE TYPE. A parameter has no declared type of its own: what it IS, is
// what the expression produced — so the value is adjusted to the type of that result (Max: "then
// through AdjustValue you push it into the parameter; the type is determined automatically when it
// is evaluated"). Adjusting rather than assigning keeps one rule about how a value lands, the same
// one every typed cell in the product follows.
//
// A refusal is NOT swallowed: the expression is the author's, and an expression that cannot be
// evaluated has to say so where it was written.
// EVALUATE ONE EXPRESSION — through the SESSION'S ROOT, so the common modules and the whole
// environment answer (Max, 2026-08-19: "you have to attach to the root module, that is what makes
// all the common functions and common modules available").
//
// 🛑 WHY NOT PLAIN `Evaluate`: it runs against the CURRENT run context, and pressing "Generate" is
// not a script — nothing is executing, the context stack is empty, and the call returns false
// without a word (the same silence that made the syntax check pass everything). So when there is no
// current frame, the root module's own is used: its ProcUnit, its bytecode, its names.
static ibValue ibEvaluateInRoot(const wxString& expression)
{
	if (expression.IsEmpty())
		return ibValue();

	auto* state = ibSession::GetPUState();
	ibRunContext* current = state != nullptr ? state->GetCurrentRunContext() : nullptr;

	ibValue produced;
	if (current != nullptr) {
		ibProcUnit::Evaluate(expression, current, produced, false);
		return produced;
	}

	// NO FRAME OF OUR OWN — borrow the root module's. The context is a frame descriptor: it carries
	// the ProcUnit whose bytecode the expression is compiled against, which is exactly what "attached
	// to the root" means.
	ibSession* session = ibSession::Current();
	ibValueModuleManagerRuntimeConfiguration* root = session != nullptr ? session->GetManagerModule() : nullptr;
	const auto rootUnit = root != nullptr ? root->GetProcUnit() : nullptr;
	if (!rootUnit)
		return produced;   // no runtime at all (Designer): nothing to evaluate against, and nothing invented

	ibRunContext rootFrame;
	rootFrame.SetProcUnit(rootUnit.get());
	ibProcUnit::Evaluate(expression, &rootFrame, produced, false);
	return produced;
}
void ibValueDataComposition::EvaluateParameters() const
{
	for (ibCompositionParameter& parameter : m_parameters) {
		if (parameter.m_expression.IsEmpty())
			continue;

		const ibValue produced = ibEvaluateInRoot(parameter.m_expression);

		// THE TYPE COMES FROM WHAT WAS PRODUCED. An empty result leaves the parameter empty — that is
		// an answer too, and forcing a type onto nothing would invent one.
		// THE DECLARED TYPE WINS when there is one — that is what declaring it is FOR. With none, the
		// result decides: a parameter has no type of its own until somebody gives it one.
		ibTypeDescription target = parameter.m_type;
		if (target.GetClsidCount() == 0 && !produced.IsEmpty())
			target.SetDefaultMetaType(produced.GetClassType());

		parameter.m_value = target.GetClsidCount() > 0
			? ibValueTypeDescription::AdjustValue(target, produced, GetSourceMetaData())
			: produced;
	}
}
std::map<wxString, ibValue> ibValueDataComposition::ParameterValues() const
{
	std::map<wxString, ibValue> values;
	for (const ibCompositionParameter& parameter : m_parameters) {
		if (parameter.m_name.IsEmpty())
			continue;
		// AN EXPRESSION IS NOT EVALUATED HERE (that happens before the read, once — see Compose):
		// for describing the query the value it will produce is irrelevant, only that the name IS
		// answered. Until evaluation lands, an expression reads as its current value.
		values[parameter.m_name] = parameter.m_value;
	}
	return values;
}

// THE ONE POINT WHERE A RUN SETTLES ITS PARAMETERS: evaluate what is an expression, then hand every
// value to the composer. Called by the one reader there is now — Compose — so "what the query was
// given" is settled in exactly one place.
void ibValueDataComposition::PrepareParametersForRun() const
{
	EvaluateParameters();
	for (const auto& parameter : ParameterValues())
		GetModelComposer().Parameter(parameter.first, parameter.second);
}

// (RunComposerPage is gone: it was the LIST's fetch — a page, an anchor, a direction. A composition
//  has one read and one result, and it lives in Compose.)

wxString ibValueDataComposition::GetParameterName(size_t idx) const
{
	return idx < m_parameters.size() ? m_parameters[idx].m_name : wxString();
}

ibValue ibValueDataComposition::GetParameterValue(size_t idx) const
{
	return idx < m_parameters.size() ? m_parameters[idx].m_value : ibValue();
}

wxString ibValueDataComposition::GetParameterExpression(size_t idx) const
{
	return idx < m_parameters.size() ? m_parameters[idx].m_expression : wxString();
}

bool ibValueDataComposition::IsParameterUserSettable(size_t idx) const
{
	return idx < m_parameters.size() && m_parameters[idx].m_userSettable;
}

bool ibValueDataComposition::IsParameterFromQuery(size_t idx) const
{
	return idx < m_parameters.size() && m_parameters[idx].m_fromQuery;
}

bool ibValueDataComposition::SetParameterValue(size_t idx, const ibValue& value)
{
	if (idx >= m_parameters.size())
		return false;
	m_parameters[idx].m_value = value;
	// A VALUE AND AN EXPRESSION ARE THE SAME ANSWER GIVEN TWICE — choosing a value clears the
	// expression, so what the row shows is what will actually be sent.
	m_parameters[idx].m_expression.clear();
	OnChildChanged();
	return true;
}

bool ibValueDataComposition::SetParameterExpression(size_t idx, const wxString& expression)
{
	if (idx >= m_parameters.size())
		return false;
	m_parameters[idx].m_expression = expression;
	OnChildChanged();
	return true;
}

bool ibValueDataComposition::SetParameterUserSettable(size_t idx, bool settable)
{
	if (idx >= m_parameters.size())
		return false;
	m_parameters[idx].m_userSettable = settable;
	OnChildChanged();
	return true;
}

const ibTypeDescription& ibValueDataComposition::GetParameterType(size_t idx) const
{
	static const ibTypeDescription s_none;
	return idx < m_parameters.size() ? m_parameters[idx].m_type : s_none;
}

bool ibValueDataComposition::SetParameterType(size_t idx, const ibTypeDescription& type)
{
	if (idx >= m_parameters.size())
		return false;
	m_parameters[idx].m_type = type;
	// THE VALUE FOLLOWS THE DECLARATION at once — a value of the old type sitting under a new one is
	// the state nobody can read: the cell shows one thing and the query would receive another.
	if (type.GetClsidCount() > 0)
		m_parameters[idx].m_value = ibValueTypeDescription::AdjustValue(type, m_parameters[idx].m_value, GetSourceMetaData());
	OnChildChanged();
	return true;
}

size_t ibValueDataComposition::AddParameter(const wxString& name)
{
	const auto found = std::find_if(m_parameters.begin(), m_parameters.end(),
		[&name](const ibCompositionParameter& parameter) { return parameter.m_name.IsSameAs(name, false); });
	if (found != m_parameters.end())
		return (size_t)std::distance(m_parameters.begin(), found);

	ibCompositionParameter parameter;
	parameter.m_name = name;
	m_parameters.push_back(parameter);
	// ⚠ Safe to announce from here: the query SYNC does not come through this door — it pushes its
	// own entries (SyncParametersWithQuery) — so this only ever runs because somebody asked for a
	// parameter. A composition catching up with its own text is not a change anybody made.
	OnChildChanged();
	return m_parameters.size() - 1;
}

bool ibValueDataComposition::RemoveParameter(size_t idx)
{
	// 🛑 AN AUTO PARAMETER CANNOT BE REMOVED HERE. It is in the query text; the next re-parse would
	// put it straight back, and a command that undoes itself reads as a broken command. Take it out
	// of the text instead.
	if (idx >= m_parameters.size() || m_parameters[idx].m_fromQuery)
		return false;
	m_parameters.erase(m_parameters.begin() + idx);
	OnChildChanged();
	return true;
}
// ⭐ THE PROGRAMMATIC DOORS WRITE THE STORE AND DO NOT RE-READ. What is on screen stays the sheet
// that was built until somebody composes again — a report does not re-read itself because a line was
// added, and a generated form sets several of these in a row before it reads anything at all.
//
// ⭐⭐ THEY DO, HOWEVER, SAY THAT THEY CHANGED IT. Every door that writes this composition raises
// OnChildChanged, and that is the whole rule: a change is announced where it HAPPENS, not where some
// window later decides to commit (Max, 2026-08-20: "absolutely anything that changes in the composer
// — it does not react at all"). Announcing it at the panel's Commit covered only the settings that
// go through the panel's buffer; a resource, a parameter, a variant are written LIVE and survive
// Cancel, so a commit-time signal missed every one of them.
//
// The signal is deliberately cheap and payload-free: it says "something below me changed" and
// bubbles to whoever holds this composition — in the designer the composer metaobject, which marks
// the configuration modified. At runtime nothing holds it and the signal stops.
void ibValueDataComposition::AddFilter(const wxString& path, const wxString& op, const ibValue& value)
{
	GetModelComposer().Filter(path, op, value);
	OnChildChanged();
}

void ibValueDataComposition::AddSort(const wxString& path, bool ascending)
{
	GetModelComposer().Sort(path, ascending);
	OnChildChanged();
}

// A GROUPING IS THE FOLD, and today it is a flat ordered list — the order IS the nesting. When the
// output tree lands, a node will own its own list of these and this door takes the node; the fold
// itself does not change.
void ibValueDataComposition::AddGroup(const wxString& path, ibQueryDimUnfold kind)
{
	GetModelComposer().TotalBy(path, kind);
	OnChildChanged();
}

// A RESOURCE IS AN AGGREGATE OVER A FIELD — what the levels actually fold. Without one a grouped
// composition produces the shape of a report and none of its numbers, which is why this sits next
// to the grouping door rather than somewhere a caller has to go looking for it.
void ibValueDataComposition::AddTotal(const wxString& func, const wxString& path)
{
	GetModelComposer().Total(func, path);
	OnChildChanged();
}

bool ibValueDataComposition::SetTotal(size_t idx, const wxString& func, const wxString& path)
{
	if (!GetModelComposer().SetTotalAt(idx, func, path))
		return false;
	OnChildChanged();
	return true;
}

bool ibValueDataComposition::RemoveTotal(size_t idx)
{
	if (!GetModelComposer().RemoveTotalAt(idx))
		return false;
	OnChildChanged();
	return true;
}

// COMPOSE INTO A DOCUMENT. The composer walks its rows into the spreadsheet driver, which owns the
// whole layout decision (header + freeze, one row per row, nesting as outline groups, a bound
// parameter on every cell that stands for something openable). Nothing here knows about a window:
// the document is the result, and a view is a subscriber to it.
// ⭐⭐ READ, LAY OUT, AND FILL THE SHEET THIS MODEL HOLDS. The control is already subscribed to that
// sheet, so filling it IS what puts the report on screen — nothing is installed afterwards and the
// control is never re-pointed (Max, 2026-08-20).
//
// 🛑 IT BUILDS INTO A DOCUMENT OF ITS OWN FIRST, then swaps. Writing into the shown one would fire
// its notifiers from a worker thread — that is GUI touched off the UI thread — and would show the
// report half-drawn while it is built. The swap is one move, on a handle: the backend document
// carries a refcount of its own (wxObjectDataPtr), parallel to the value refcount, so the old sheet
// lives exactly as long as somebody still holds it.
bool ibValueDataComposition::Compose()
{
	wxObjectDataPtr<ibBackendSpreadsheetObject> composed(new ibBackendSpreadsheetObject());
	if (!Compose(composed.get()))
		return false;

	// ⭐⭐ AND THE FINISHED SHEET BECOMES THE ONE THIS MODEL HOLDS — the whole document, not its
	// description: the drill-down parameters and the read-only mode live on the OBJECT, so publishing
	// a description alone leaves every cell bound to a name nothing answers to.
	m_spreadsheetDoc = composed;
	return true;
}

// …AND THE ONE ROUTINE BOTH ENTRANCES USE. The script hands over a document it owns
// (`Compose(Document)`); the model's own verb hands over a fresh one and installs it. Nothing here
// knows which of the two it is — and no finished sheet is ever copied afterwards.
bool ibValueDataComposition::Compose(ibBackendSpreadsheetObject* target)
{
	if (target == nullptr)
		return false;

	// 🛑 A QUERY THAT COULD NOT BE DESCRIBED CANNOT BE COMPOSED — AND SAYS SO.
	//
	// RebuildSource already asked the engine to describe the query and kept its refusal verbatim
	// (m_queryError). Until now only the settings window read that field, so pressing Generate over
	// a query the engine had refused did nothing at all: no rows, no message, no clue — "I press
	// compose and it quietly dies" (Max, 2026-08-20). The engine's own words are the message.
	if (!m_queryError.IsEmpty())
		ibBackendCoreException::Error(m_queryError);

	// THE PARAMETERS ARE SETTLED FIRST — the same door the fetch uses (PrepareParametersForRun):
	// expressions evaluated once, values handed to the composer, and only then the read.
	PrepareParametersForRun();
	ibDataComposer& composer = GetModelComposer();

	// The settings are already ON the composer (the facade writes them straight through), so a
	// compose is a run — no re-application, no clearing and re-adding.
	ibSpreadsheetComposeDriver driver(target);

	// THE HEADING SAYS WHAT WAS ASKED. A report without its conditions cannot be defended a week
	// later, and the conditions are not a second store — they are the filters the composition
	// already carries, read back through the same door the settings window reads.
	driver.SetTitle(GetSourceCaption());
	for (size_t i = 0; i < composer.FilterCount(); ++i) {
		wxString path, op;
		ibValue value;
		if (!composer.GetFilterAt(i, path, op, value))
			continue;
		driver.AddHeaderLine(path + wxT(" ") + op + wxT(" ") + value.GetString());
	}

	// ⭐ EVERY OUTPUT PRINTS, one after another, onto the SAME sheet (Max: "you load the outputs and
	// then run once, and it fills the drivers"). Running the first one only is what made a second
	// output look dead: it was declared, settings and all, and never read.
	//
	// One driver for all of them — the document is one, and an output's block follows the previous
	// one down the page. A driver of its own comes when an output needs a different KIND of drawing
	// (a chart), which is a driver question, not a composition one.
	for (ibDataComposer::Output& output : composer.Outputs())
		output.m_driver = &driver;
	const bool composed = composer.Run();

	// The pointers do not outlive this call — the driver is a stack object, and an output holding a
	// dangling one would be read on the next compose.
	for (ibDataComposer::Output& output : composer.Outputs())
		output.m_driver = nullptr;

	return composed;
}

// ⭐⭐ THE COMPOSITION'S OWN FETCH — one shot, not a page.
//
// The twin of ibValueModel::SubmitFetchAsync, and deliberately NOT it: a list rents a run and then
// serialises PORTIONS through the view door's lock, because a table is read a window at a time. A
// composition is not: one request produces one sheet, so there is nothing to serialise and no door to
// lock — what is borrowed is only the background thread and a connection of its own, so the window
// stays alive while the report is built.
//
// A refusal to rent (no job manager, nothing free, a headless test) means "read it here instead" —
// never "no data".
void ibValueDataComposition::SubmitFetchAsync(std::function<void()> work)
{
	if (!work)
		return;

	// ONE SHEET, ONE SLOT: a second Compose means the first read is no longer wanted.
	CancelFetch();

	if (ibJobManager* const jobs = ibApplicationData::GetJobManager()) {
		try {
			// The handle is KEPT: CancelFetch waits on it, so a read cannot outlive this composition.
			m_fetchRun = jobs->StartBackground(
				[work](ibSession*) -> ibValue { work(); return ibValue(); },
				_("composing the report"),
				ibJobTenancy::Tenant);
			return;
		}
		catch (const ibBackendException&) {
			// Nothing to rent — fall through and read on this thread.
		}
	}

	work();
}

// Cooperative: the flag is raised and the run is waited out — a query already in flight finishes.
void ibValueDataComposition::CancelFetch()
{
	if (!m_fetchRun)
		return;
	m_fetchRun->Cancel();
	m_fetchRun->Wait();
	m_fetchRun.reset();
}

// (GetFeatures / GetValueByRow / GetValueByMetaID / GetRowAt are GONE with the list surface — a
//  composition has no dataview rows to marshal a value out of. What it produces is a SHEET.)

ibUniqueKey ibValueDataComposition::GetGuid() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return q->GetQueryTableGuid();

	return wxNullGuid;
}

// (GetItemKey / ActivateItem are gone with the list surface: they keyed a DATAVIEW row, and a
//  composition has none. The drill-down never went through them — a composed cell carries its value
//  as a document parameter and the click ends in ibValue::ShowValue.)

// Forward the source's command SET — metadata-blind, whatever the descriptor lists is what shows. This
// is what puts a working command bar on a composition dropped onto any form, with no code written.
// ⭐⭐ THE COMPOSER NAMES ITS OWN VERBS. Compose and Settings are facts about a COMPOSITION — it
// reads, and it has settings to read by — not about whatever control happens to show it (Max,
// 2026-08-20: "those you do not need; they are determined by the composer itself"). A control lays
// out what it is given, so the same two appear wherever a composition is shown, and a spreadsheet
// document offers neither because it has neither.
//
// They land in the FORM's own toolbar when the composition is the form's main view — the control
// bound to it reports itself as such, and its bar is suppressed in favour of the form's.
void ibValueDataComposition::GetCommandCollection(const ibFormID& formType, std::vector<ibCommandItem>& commands) const
{
	// COMPOSE changes what is SHOWN, not what is stored — live even on a view-only form.
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_Compose, wxT("Compose"), _("Compose"),
		ibPictureDescription(g_picGenerateCLSID), true).SetModify(false));
	commands.push_back(ibCommandItem(ibSpreadsheetModelCommand_Settings, wxT("Settings"), _("Settings"),
		ibPictureDescription(g_picStructureCLSID), false).SetModify(false));

	// …then whatever the SOURCE offers, after a rule.
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor()) {
		const size_t before = commands.size();
		holder->GetCommandCollection(formType, commands);
		if (commands.size() > before)
			commands.insert(commands.begin() + before, ibCommandItem());   // separator
	}
}

// ⚠ NO ROW KEYS. The command surface is forwarded as it always was, but the anchor and the selection
// were DATAVIEW rows — a composition has none. A command that acts on "the current row" belongs to
// whatever shows the result (the sheet knows which value a cell carries), so an empty key is the
// honest argument here rather than one invented from a row that does not exist.
//
// The gridbox hands back an id it took from this very store, so it goes straight to the source.
// (A spreadsheet document has no store and never reaches here.)
void ibValueDataComposition::CallAsModelCommand(const ibActionID& id, ibBackendValueForm* srcForm)
{
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->CallAsCommand(id, ibUniqueKey(), ibUniqueKey(), srcForm);
}

const ibSourceExplorer* ibValueDataComposition::GetSourceExplorer() const
{
	// Columns come from the QUERYABLE, not a metaobject — a composition is queryable-based.
	// ⭐ A COMPOSITION IS NOT A TABLE (Max, 2026-08-19): "the composer is seen as a table, but a
	// composer is not a table — what it is, is where it OUTPUTS: a spreadsheet document".
	//
	// The root used to be flagged a tabular section, and that one flag is what made the whole
	// product treat it as a list: `IsTableSource` reads the flag, so dragging the attribute onto a
	// form built a TABLEBOX, and a tablebox's source picker offered the composition while the
	// GRIDBOX's picker — the control that actually shows a composed report — offered nothing.
	//
	// Unflagged, it is an attribute like any other value: the box that shows it is the gridbox, and
	// what appears there is the DOCUMENT the composition composed.
	m_sourceExplorer.Reset(GetObjectTypeName(), GetObjectTypeName(), wxNOT_FOUND, g_valueDataCompositionCLSID, /*tableSection*/false);
	if (const ibQueryableSourceDescriptor* holder = GetSourceDescriptor())
		holder->FillSourceExplorer(m_sourceExplorer);

	// …AND WHAT THE QUERY ADDS ON TOP. The source's fields are there because the rows are its rows; the
	// query's output columns are there because that is what the composition actually shows. Added, not
	// substituted — and a name the source already offers is not added twice.
	for (const ibQueryLowering::OutputColumn& column : m_querySchema) {
		if (column.m_name.IsEmpty())
			continue;
		if (m_sourceExplorer.FindByName(column.m_name) != nullptr)
			continue;

		// The column's own DESCRIPTOR when the query did not rename it — that is what carries the
		// synonym and the binding. A RENAMED one (`Owner.Code AS Supplier`) is appended by name and
		// type: the new name is the query's, and it belongs to no attribute to borrow a synonym from.
		if (column.m_col != nullptr && column.m_col->GetName().IsSameAs(column.m_name, false))
			m_sourceExplorer.AppendColumn(column.m_col);
		else
			m_sourceExplorer.AppendColumn(column.m_name,
				column.m_col != nullptr ? column.m_col->GetColumnId() : wxNOT_FOUND,
				column.m_col != nullptr ? column.m_col->GetTypeDesc() : ibTypeDescription());
	}
	return &m_sourceExplorer;
}

wxString ibValueDataComposition::GetSourceCaption() const
{
	if (const ibBackendQueryable* q = GetSourceQueryable())
		return GetSourceMetaObject() ?
			stringUtils::GenerateSynonym(GetSourceMetaObject()->GetClassName()) + wxT(": ") + GetSourceMetaObject()->GetSynonym() : q->GetQueryName();

	return GetClassName();
}

const ibMetaData* ibValueDataComposition::GetMetaData() const
{
	// SELECTION — which config to PICK / resolve a source FROM: the owner FORM's config, via the attach
	// chain. Non-cyclic: the form falls back to its SOURCE (this composer) for metadata, and the
	// composer's GetSourceMetaData reads from the VALUE (terminal), so the walk ends.
	if (const ibPropertyObject* o = GetAttachOwner())
		return o->GetMetaData();

	return ibApplicationData::GetActiveMetaData();
}

const ibMetaData* ibValueDataComposition::GetSourceMetaData() const
{
	// READ — the STORED source config, captured in RebuildSource. Returned straight from the field: no
	// owner-walk, no queryable re-resolve, so a form's metadata may fall back here without looping.
	// Nothing stored yet → the ACTIVE config.
	return m_sourceMetaData != nullptr ? m_sourceMetaData : ibApplicationData::GetActiveMetaData();
}

// --- settings surface -------------------------------------------------------

void ibValueDataComposition::FillMembers(ibMemberTable& helper) const
{
	helper.AppendProp(wxT("Filter"),   true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Order"),    true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Group"),    true, false, wxNOT_FOUND);
	helper.AppendProp(wxT("Settings"), true, false, wxNOT_FOUND);
	helper.AppendProc(wxT("Refresh"), wxT("Refresh()"));
	helper.AppendProc(wxT("Compose"), wxT("Compose(Document)"));
}

bool ibValueDataComposition::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	ibValueListSettings* s = GetListSettings();
	switch (lPropNum) {
	case 0: pvarPropVal = s->GetFilter(); return true;   // Filter
	case 1: pvarPropVal = s->GetOrder();  return true;   // Order
	case 2: pvarPropVal = s->GetGroup();  return true;   // Group
	case 3: pvarPropVal = s;              return true;   // Settings (the whole object)
	}
	return false;
}

bool ibValueDataComposition::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case 0:   // Refresh — for a report that means BUILD IT AGAIN, with whatever the settings now say.
		RefreshComposerSettings();
		Compose();
		return true;
	case 1: {  // Compose(Document)
		if (lSizeArray < 1 || paParams[0] == nullptr)
			return false;
		// The argument is the script's spreadsheet document; the C++ document under it is what
		// the driver writes into, and it is the same object the grid on the form is showing.
		ibValueSpreadsheetDocument* value = CastValue<ibValueSpreadsheetDocument>(paParams[0]);
		if (value == nullptr)
			return false;
		return Compose(value->GetSpreadsheetDocument().get());
	}
	}
	return false;
}

// --- ibPropertyObject -------------------------------------------------------


void ibValueDataComposition::OnPropertyRefresh()
{
	// See the header: the inspector shows "Settings..." and nothing else. Hidden rather than
	// removed — what they hold still serialises, and the settings window still edits the query.
	HideProperty(m_propertySource, true);
	HideProperty(m_propertyQuery, true);
}
void ibValueDataComposition::OnPropertyChanged(ibProperty* property, const wxVariant& /*oldValue*/, const wxVariant& /*newValue*/)
{
	if (m_propertySource == property || m_propertyQuery == property)
		RebuildSource();
	RefreshComposerSettings();
}

bool ibValueDataComposition::ReadProperty(const ibDataNode& node)
{
	m_propertySource->SetNodeValue(node.GetProperty(m_propertySource->GetName()));   // resolves the queryable from the id
	m_propertyQuery->SetNodeValue(node.GetProperty(m_propertyQuery->GetName()));
	RebuildSource();

	// THE VARIANTS ARE THE SETTINGS NOW. A record written before they existed carries one set of
	// settings and no variant nodes at all — it is read the way it was written and becomes the first
	// variant's snapshot, so nobody's saved work turns into a migration.
	if (!ReadVariants(node)) {
		// 🛑 READ INTO A BUFFER AND COMMIT, exactly as the variants path does — NOT through the live
		// facade. The facade now carries the change signal, and a LOAD is not a change: reading an
		// old record through it announced the composition as modified the moment the report opened.
		// The variants path never had the problem because it always read into buffer-mode settings;
		// this branch is now the same shape.
		ibValuePtr<ibValueListSettings> legacy(new ibValueListSettings());   // BUFFER mode — own storage
		legacy->ReadData(node);
		// The filter TREE is handed over directly: the composer takes a filter as one expression and
		// cannot give it back, so the live settings must carry the tree beside the store. SetFilterRoot
		// installs it without going through a mutator, so it stays silent too.
		if (ibValueListSettings* live = GetListSettings())
			live->SetFilterRoot(legacy->GetFilterRoot());
		ibCommitSettingsToComposer(GetModelComposer(), legacy);
		CaptureActiveVariant();
	}
	// …AND THE PARAMETERS: values, expressions, declared types and who fills them come back with the
	// rest of the settings. Read AFTER RebuildSource, whose parameter sync has already put whatever
	// the TEXT asks for into the list — what is stored then fills those in.
	ReadParameters(node);
	// …AND THE RESOURCES. AFTER the variants, deliberately: applying a variant writes the store's
	// settings, and the store is where a resource lives — reading them first would hand them to a
	// store that is about to be rewritten.
	ReadTotals(node);
	return true;
}

bool ibValueDataComposition::WriteProperty(ibDataNode& node) const
{
	// ⚠ THE QUERY IS THE SOURCE. A dynamic list stands on a main table and refuses to serialise
	// without one — its commands, its icon and the value a choice hands back all come from there.
	// A COMPOSITION HAS NO SUCH TABLE: what it reads is the query text, and a composition is
	// complete the moment that text exists. (Max, 2026-08-18: "the composer has no source like the
	// dynamic list does — the composer's source IS the query".)
	//
	// The Source property stays, optional: picking one is what lets a row know how to open itself
	// and where its commands come from. It is a convenience over the query, never a precondition
	// for it.
	if (!m_propertySource->IsEmptyProperty())
		node.SetProperty(m_propertySource->GetName(), m_propertySource->GetNodeValue());
	node.SetProperty(m_propertyQuery->GetName(), m_propertyQuery->GetNodeValue());

	// EVERY VARIANT, and the active one straight from the composer — see WriteVariants. The old
	// single-settings write is gone: what used to be "the settings" is now variant zero.
	WriteVariants(node);
	// …AND THE PARAMETERS beside them: what the query asks for is part of the settings.
	WriteParameters(node);
	// …AND THE RESOURCES, which were written nowhere at all until 2026-08-20 — see WriteTotals.
	WriteTotals(node);
	return true;
}

// Register the type — runtime / designer know "DataComposition".
VALUE_TYPE_REGISTER(ibValueDataComposition, "DataComposition", g_valueDataCompositionCLSID);
// (The row type is gone with the dataview surface: there is no return LINE where there are no rows.)
