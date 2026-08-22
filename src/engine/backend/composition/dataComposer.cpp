////////////////////////////////////////////////////////////////////////////
//	L5-1 — the data composer: schema -> rendered L4-1 text -> driver (dataComposer.h)
////////////////////////////////////////////////////////////////////////////

#include "dataComposer.h"

#include "backend/query/queryParser.h"        // ibQueryParser — text -> AST
#include "backend/query/queryable.h"          // ibBackendQueryable / ibBackendQueryColumn
#include "backend/query/queryableFactory.h"   // the source factory — the column dictionary
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryResult / ibSelectKind
#include "backend/query/querySelector.h"      // ibSelector — the TOTALS pre-order walk
#include "backend/appData.h"                  // ibApplicationData::GetQueryableFactory
#include "backend/metaData.h"                 // ibMetaData::GetSourceFactory — resolve by-name sources per-config
#include "backend/backend_exception.h"        // ibBackendCoreException
#include "backend/query/queryReadState.h"  // ibQueryReadState — one build, one state of the data

//////////////////////////////////////////////////////////////////////
// sources
//////////////////////////////////////////////////////////////////////

// The marker namespace any FromSource(queryable) source renders under ("Temp.t0"). Not a real
// metaobject kind — the factory never resolves it; the auxiliary registry (ibTempSourceScope) catches
// the name first and returns the registered queryable directly. Kept opaque on purpose: the value is
// irrelevant to resolution (ResolveSource keys on the name alone), it only has to parse as <ns>.<name>.
static const wxChar* const s_tempSourceNamespace = wxT("Temp");

ibDataDBComposer& ibDataDBComposer::FromSource(const wxString& ns, const wxString& name)
{
	// SET the single FROM source (symmetric with FromText, which clears too). Append-WITHOUT-clear made the
	// persistent model composer ACCUMULATE one duplicate source per fetch — RunComposerPage rebinds the source
	// every page over the SAME composer, and ClearSettings does not touch m_sources — which renders as a growing
	// self-JOIN (m_sources: [0]=FROM, [1..]=JOIN). Multi-source JOINs are built on the query BUILDER (.Join),
	// not by repeated FromSource, so resetting here is safe.
	m_sourceText.Clear();
	m_sources.clear();
	m_directSources.clear();   // a transient registry belongs to ONE source set — reset it in lock-step
	m_sources.push_back({ ns, name });
	return *this;
}

ibDataDBComposer& ibDataDBComposer::FromSource(const ibBackendQueryable* queryable)
{
	if (queryable == nullptr)
		ibBackendCoreException::Error(_("Composer: a null queryable was given as the source"));

	// FROM resets the source set — the verbatim text, the factory sources, AND the transient registry.
	m_sourceText.Clear();
	m_sources.clear();
	m_directSources.clear();

	// Source the LIVE queryable DIRECTLY through the auxiliary per-query registry: register it under an
	// auto-numbered per-query name (t0, t1, …) and render "FROM Temp.t0"; ResolveSource hands it straight
	// back, unchanged. The NUMBER is how the query tells transient sources apart — a future JOIN of a second
	// temp table just takes the next slot (t1, t2) over the same registry.
	//
	// NO metadata round-trip. The caller was JUST handed a complete L3 queryable — it carries its own
	// columns + provider + reconstruction context — so the composer has no business searching the metadata
	// to recover a metaobject identity for it, then converting that to a "kind.name" the lowering only
	// re-resolves to the very same queryable. Worse, the old recovery used ibValue::GetNameObjectFromID, a
	// STATIC-registry lookup that returns nothing for a class registered in the METADATA (every dynamic
	// catalog / document) — so it was outright broken for those. A named metaobject source, when one is
	// genuinely wanted (readable text), is requested via FromSource(ns, name).
	const wxString name = wxString::Format(wxT("t%u"), static_cast<unsigned int>(m_directSources.size()));
	m_directSources[name] = queryable;
	m_sources.push_back({ s_tempSourceNamespace, name });
	return *this;
}

ibDataDBComposer& ibDataDBComposer::FromText(const wxString& text)
{
	m_sourceText = text;
	m_sources.clear();
	return *this;
}

bool ibCompositionCompare(const ibValue& cell, const wxString& op, const ibValue& value)
{
	if (op == wxT("="))                            return cell == value;
	if (op == wxT("<>") || op == wxT("!="))        return cell != value;
	if (op == wxT(">"))                            return cell >  value;
	if (op == wxT(">="))                           return cell >= value;
	if (op == wxT("<"))                            return cell <  value;
	if (op == wxT("<="))                           return cell <= value;
	if (op.CmpNoCase(wxT("LIKE")) == 0) {
		wxString pattern = value.GetString();
		pattern.Replace(wxT("%"), wxT("*"));
		pattern.Replace(wxT("_"), wxT("?"));
		return cell.GetString().Lower().Matches(pattern.Lower());
	}
	return true;   // unknown operator → do not hide anything over a line nobody can read
}

//////////////////////////////////////////////////////////////////////
// settings
//////////////////////////////////////////////////////////////////////

ibDataComposer& ibDataComposer::Select(const wxString& nameOrPath)
{
	if (!nameOrPath.IsEmpty())
		m_commonSelected.push_back(nameOrPath);
	return *this;
}

wxString ibDataComposer::AddParam(const ibValue& value)
{
	// The value travels as an auto-named &parameter — never inlined into the
	// text. That is what keeps a string value from being read as syntax and a
	// date from being read in somebody's locale.
	const wxString param = wxString::Format(wxT("__f%d"), m_autoParam++);
	m_params[param] = value;
	return param;
}

// ⭐⭐ A SETTING WITH NO FIELD IS NOT A SETTING — IT IS THE ABSENCE OF ONE, and it stops here.
//
// These three verbs write a PATH into the rendered text, and an empty one renders a clause with a hole
// in it: `ORDER BY ` (nothing after it), `WHERE  = &p0`, `TOTALS BY `. The parser then refuses the whole
// query — correctly, it is not a query — and the list that asked shows the same blank a source with no
// rows shows. That is how one unset field turned an ENUM's choice list into an empty window: its default
// sort named a presentation column the enum has no physical field for, so the path arrived empty, and
// `SELECT Ref FROM Temp.t0 ORDER BY ` died at the space after BY.
//
// Dropping is the right answer rather than raising: "sort by nothing" HAS a meaning — the source's own
// order — while a refusal would only trade an empty list for an error on lists that are otherwise fine.
// The façade already guarded one of its two doors this way (ibValueSortList::Add writes the composer
// only `if (!field.IsEmpty())`, while its buffer branch stored the empty item that later came here), and
// a guard living in one of two doors is the shape of this defect, not its fix.
ibDataComposer& ibDataComposer::Filter(const wxString& path, const wxString& op, const ibValue& value)
{
	if (path.IsEmpty())
		return *this;
	m_commonFilters.push_back({ path, op, AddParam(value) });
	return *this;
}

ibDataComposer& ibDataComposer::FilterAst(const ibQueryAstExprPtr& condition)
{
	m_commonFilterAst = condition;
	++m_commonFilterAstVersion;
	return *this;
}

ibDataComposer& ibDataComposer::Sort(const wxString& path, bool ascending)
{
	if (path.IsEmpty())
		return *this;   // see the note above Filter
	m_commonSorts.push_back({ path, ascending });
	return *this;
}

ibDataComposer& ibDataComposer::Total(const wxString& func, const wxString& path)
{
	// A RESOURCE WITH NOTHING TO AGGREGATE is the absence of one — the same rule the sort and the
	// filter follow above. It would render `SUM()` (or a bare comma with the expression form) and
	// the parser would refuse the whole query, which the caller then sees as an empty result
	// rather than as the empty setting it actually was.
	if (path.IsEmpty())
		return *this;
	m_totals.push_back({ func, path });
	return *this;
}

ibDataComposer& ibDataComposer::TotalBy(const wxString& path, ibQueryDimUnfold kind)
{
	if (path.IsEmpty())
		return *this;   // see the note above Filter
	// A LEVEL, not a field: "group by this, then by that". Welding several fields into ONE heading
	// is the report's own act and it happens in the grouping form, over the level's field list.
	AppendLevel(path, kind);
	return *this;
}

ibDataComposer& ibDataComposer::Parameter(const wxString& name, const ibValue& value)
{
	m_params[name] = value;
	return *this;
}

ibDataComposer& ibDataComposer::ClearSettings()
{
	m_commonSelected.clear();
	m_commonFilters.clear();
	m_commonSorts.clear();
	m_totals.clear();
	TrimLevels(0);
	return *this;
}

//////////////////////////////////////////////////////////////////////
// render
//////////////////////////////////////////////////////////////////////

// The nested source's alias when the settings are written over an author's query. ASCII, a legal
// identifier, and unlikely to collide with anything a person names a table.
static const wxChar* kAuthorQuerySource = wxT("AuthorQuery");

const wxChar* ibDataDBComposer::AuthorQuerySourceName() { return kAuthorQuerySource; }

wxString ibDataDBComposer::RenderText() const
{
	// THE FIRST OUTPUT is what "the composer's query" has always meant — a list has exactly one.
	return RenderTextFor(Root());
}

// RENDER ONE OUTPUT: its own levels, its own filter and sort, its own selected fields. The
// RESOURCES are the composition's and every output rolls the same ones, which is why they are read
// off `m_totals` here rather than off the output.
wxString ibDataDBComposer::RenderTextFor(const Output& output) const
{
	// Anything asked of this read — by the composition above it or by the output itself. Miss one
	// and the author's verbatim text is handed back unchanged, with the setting silently dropped.
	const bool hasSettings = !SelectedFor(output).empty()
	                       || !output.m_filters.empty() || !output.m_sorts.empty()
	                       || !m_commonFilters.empty() || !m_commonSorts.empty()
	                       || !m_totals.empty() || !ChainFrom(output).empty();

	if (!m_sourceText.IsEmpty() && !hasSettings)
		return m_sourceText;   // the author's text, verbatim — nothing is being asked of it

	// ⭐ SETTINGS OVER AN AUTHOR'S QUERY — the seam this used to refuse.
	//
	// The author's query is not edited. It becomes a NESTED SOURCE, and the filter, the sort and the
	// grouping are written OVER it:
	//
	//     SELECT * FROM (<the author's query>) AS AuthorQuery WHERE … ORDER BY … TOTALS …
	//
	// which is the only reading that is right in every case: a WHERE injected INTO the author's text
	// would run before their own aggregates, their DISTINCT and their TOP, and would quietly answer a
	// different question than the one the user typed into the filter.
	//
	// Nothing new was needed for it. A subquery source round-trips (queryRender / queryParser), the
	// lowering realises it (WrapSelectAsQueryable), and the optimizer's FROM-subquery flattening folds
	// the plain case straight back into ONE server-side SELECT — so a simple author query with a
	// filter costs exactly what it did before there was a filter. (queryRewrite.h rule 2.)
	//
	// The paths the settings name are the query's OUTPUT names, which is what a host offers a person
	// to pick from (a dynamic list vends them as its column collection) — the same names, both sides.
	if (!m_sourceText.IsEmpty()) {
		wxString authorProj;
		for (const wxString& name : SelectedFor(output)) {
			if (!authorProj.IsEmpty())
				authorProj += wxT(", ");
			authorProj += name;
		}
		// No explicit selection: everything the nested query yields. `*` and not a reflected list,
		// because the nested query's columns are ITS business — asking what they are would mean
		// resolving the text here, and the lowering is about to do that anyway.
		wxString text = wxT("SELECT ") + (authorProj.IsEmpty() ? wxString(wxT("*")) : authorProj)
			+ wxT("\nFROM (") + m_sourceText + wxT(") AS ") + kAuthorQuerySource;
		AppendSettingsClauses(text, output);
		return text;
	}

	if (m_sources.empty())
		ibBackendCoreException::Error(_("Composer: no source is set"));
	// (RESOURCES WITH NO GROUPING ARE NO LONGER REFUSED. They are a grand total, and the clause
	//  writer says so with `BY OVERALL` — see AppendSettingsClauses. Refusing them was a rule about
	//  what the RENDERER could write, stated as if it were a rule about what a report may ask for.)
	//
	// NB: TotalBy WITHOUT an aggregate is valid — "TOTALS BY <dim>" is a pure grouping
	// / hierarchy with no rolled aggregate (a list grouped by a field).

	// --- the projection -------------------------------------------------------
	wxString proj;
	if (!SelectedFor(output).empty()) {
		for (const wxString& name : SelectedFor(output)) {
			if (!proj.IsEmpty())
				proj += wxT(", ");
			proj += name;
		}
	}
	else {
		// No explicit selection: ALL the (single) source's columns — READ-ONLY
		// reflection through the factory; execution still flows through the text.
		if (m_sources.size() > 1)
			ibBackendCoreException::Error(_("Composer: joined sources need an explicit Select list"));

		const Source& s0 = m_sources.front();

		// A TRANSIENT (RAM / temp) source registered via FromSource(queryable) — reflect its
		// columns straight off the live queryable (the factory carries no descriptor for it).
		// Otherwise the factory resolves the metaobject source by name (READ-ONLY dictionary).
		const ibBackendQueryable* src = nullptr;
		const auto dit = m_directSources.find(s0.m_name);
		if (dit != m_directSources.end())
			src = dit->second;
		else {
			// Resolve through THIS query's OWN config factory (per-config sources); no config → the global base.
			ibQueryableFactory* factory = m_metaData != nullptr ? m_metaData->GetSourceFactory() : nullptr;
			if (factory == nullptr)
				factory = ibApplicationData::GetQueryableFactory();
			if (factory == nullptr)
				ibBackendCoreException::Error(_("Composer: the query engine is not available (no application data)"));
			src = factory->Resolve(s0.m_namespace, s0.m_name);
		}
		if (src == nullptr)
			ibBackendCoreException::Error(_("Composer: unknown source '%s.%s'"), s0.m_namespace, s0.m_name);

		for (const ibBackendQueryColumn* col : src->GetColumns()) {
			if (col == nullptr || col->GetName().IsEmpty())
				continue;
			if (!proj.IsEmpty())
				proj += wxT(", ");
			proj += col->GetName();
		}
		if (proj.IsEmpty())
			ibBackendCoreException::Error(_("Composer: source '%s.%s' exposes no columns"), s0.m_namespace, s0.m_name);
	}

	// --- the clauses ----------------------------------------------------------
	wxString text = wxT("SELECT ") + proj
		+ wxT(" FROM ") + m_sources[0].m_namespace + wxT(".") + m_sources[0].m_name;

	// Additional sources: the language's auto-join-by-reference (ON omitted).
	for (size_t i = 1; i < m_sources.size(); ++i)
		text += wxT(" JOIN ") + m_sources[i].m_namespace + wxT(".") + m_sources[i].m_name;

	AppendSettingsClauses(text, output);
	return text;
}

// WHERE / ORDER BY / TOTALS — the settings, written the same way whichever source they stand over.
// A composed source and an author's query differ in their FROM and in nothing else: the filter a
// person set is the same filter, and two spellings of it would be two chances to drift.
void ibDataDBComposer::AppendSettingsClauses(wxString& text, const Output& output) const
{
	// ⭐ WHERE A FILTER SITS DECIDES WHAT IT DOES (Max):
	//
	//   * ON THE OUTPUT — it REMOVES ROWS. This is the report's subject ("only this warehouse"), and
	//     everything, totals included, is computed over what is left. That is the WHERE below.
	//   * ON A LEVEL — it HIDES HEADINGS, and the rows keep counting. Deleting five rows three
	//     levels down would move the grand total, and a total that shifts because somebody tidied a
	//     sub-heading is a number nobody can defend. Applied on the WALK instead (see RunOutput).
	//
	// So a level's filter is deliberately NOT written here.
	// THE COMPOSITION'S OWN FILTER FIRST, then the output's. Both exclude; the first excludes for
	// every output there is, so no output can see more than it admits.
	bool wrote = false;
	auto appendFilters = [&text, &wrote](const std::vector<FilterItem>& filters) {
		for (const FilterItem& f : filters) {
			text += (wrote ? wxT(" AND ") : wxT(" WHERE "));
			text += f.m_path + wxT(" ") + f.m_op + wxT(" &") + f.m_param;
			wrote = true;
		}
	};
	appendFilters(m_commonFilters);
	appendFilters(output.m_filters);

	// THE ORDER: the output's own when it stated one, otherwise the composition's. A level's sort
	// orders that level's headings and is applied on the walk — it is not this clause.
	const std::vector<SortItem>& sorts = output.m_sorts.empty() ? m_commonSorts : output.m_sorts;
	for (size_t i = 0; i < sorts.size(); ++i) {
		const SortItem& s = sorts[i];
		text += (i == 0 ? wxT(" ORDER BY ") : wxT(", "));
		text += s.m_path;
		if (!s.m_ascending)
			text += wxT(" DESC");
	}

	// TOTALS [agg(path), …] BY dim [HIERARCHY], … — the aggregate list may be empty
	// (pure grouping / hierarchy), so emit the block whenever there is either an
	// aggregate OR a BY dimension.
	if (!m_totals.empty() || HasGroupingFields(output)) {
		text += wxT(" TOTALS");
		for (size_t i = 0; i < m_totals.size(); ++i) {
			text += (i == 0 ? wxT(" ") : wxT(", "));
			// NO FUNCTION MEANS THE TEXT IS THE EXPRESSION. `Total("SUM", "Amount")` renders
			// `SUM(Amount)`; `TotalExpr("SUM(Amount) / COUNT(DISTINCT Doc)")` renders itself.
			// One store, because the first is what the second would have been written as.
			text += m_totals[i].m_func.IsEmpty()
				? m_totals[i].m_path
				: m_totals[i].m_func + wxT("(") + m_totals[i].m_path + wxT(")");
		}
		// ONE LEVEL PER OUTPUT IN THE CHAIN, and a level's own fields inside it. Several fields are
		// written in BRACKETS — `BY (Partner, Contract), Store` — which is what says they are one
		// heading; a single field keeps the bare spelling it always had.
		// ⚠ A LEVEL WITH NO FIELDS IS NOT A DIMENSION — it is the DETAIL records, and it has nothing
		// to write here. Writing it anyway produced `BY ` with nothing after it and the parser
		// refused the whole query, which the caller then saw as an empty report.
		const std::vector<GroupNode>& chain = ChainFrom(output);
		bool wroteBy = false;
		for (size_t i = 0; i < chain.size(); ++i) {
			const std::vector<TotalByItem>& fields = chain[i].m_fields;
			if (fields.empty())
				continue;
			text += (wroteBy ? wxT(", ") : wxT(" BY "));
			wroteBy = true;
			const bool bracketed = fields.size() > 1;
			if (bracketed)
				text += wxT("(");
			for (size_t f = 0; f < fields.size(); ++f) {
				if (f > 0)
					text += wxT(", ");
				text += fields[f].m_path;
				if (fields[f].m_kind == ibQueryDimUnfold::Hierarchy)
					text += wxT(" HIERARCHY");
				else if (fields[f].m_kind == ibQueryDimUnfold::HierarchyOnly)
					text += wxT(" HIERARCHYONLY");
			}
			if (bracketed)
				text += wxT(")");
		}

		// ⭐⭐ RESOURCES WITH NO GROUPING ARE A GRAND TOTAL — `TOTALS COUNT(x) BY OVERALL`, which is
		// exactly "one row over everything" and is what the author asked for by declaring resources
		// and no level (Max, 2026-08-22: added the resources and composing refused).
		//
		// Written here rather than refused, because there is nothing wrong with the request. What
		// WAS wrong is what came out: `TOTALS COUNT(x)` with no BY at all — a query the engine's own
		// parser throws back ("expected BY in TOTALS"), on a composition nobody typed by hand. The
		// guard that catches this on the other road never runs for a composition over an AUTHOR'S
		// TEXT, which is what every report's composer is.
		if (!wroteBy)
			text += wxT(" BY OVERALL");
	}
}

//////////////////////////////////////////////////////////////////////
// execute / the driver walk
//////////////////////////////////////////////////////////////////////

namespace {

// Page-cache signature helpers — a value the signature can't render losslessly
// (a reference / an object) disables caching for that run; correctness over speed.
bool ValueSignable(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: case TYPE_NUMBER: case TYPE_DATE:
	case TYPE_STRING:  case TYPE_NULL:   case TYPE_EMPTY:
		return true;
	default:
		return false;
	}
}

wxString ValueSig(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: return v.GetBoolean() ? wxT("B1") : wxT("B0");
	case TYPE_NUMBER:  return wxT("N") + v.GetNumber().ToString();
	case TYPE_DATE:    return wxT("D") + v.GetDateTime().GetValue().ToString();
	case TYPE_STRING:  return wxT("S") + v.GetString();
	default:           return wxT("_");
	}
}

} // namespace

void ibDataDBComposer::EnsureAst() const
{
	// The cache key is the rendered text PLUS the tree condition's version: that
	// condition never becomes text, so the text alone would say "nothing changed"
	// after the user rewrote the whole filter.
	const wxString text = RenderText();
	if (m_ast != nullptr && text == m_renderedText && m_commonFilterAstVersion == m_renderedFilterAstVersion)
		return;   // same query, same condition — the cached parse stands

	m_ast = ibQueryParser().Parse(text);
	if (m_ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query failed to parse"));

	// THE TREE'S CONDITION GOES IN AS AN AST, ANDed with whatever the query text
	// already asked for. No rendering, no re-parsing — the expression the filter
	// built is the expression the engine lowers.
	if (m_commonFilterAst) {
		if (m_ast->m_where) {
			ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
			both->m_isOr = false;
			both->m_lhs = m_ast->m_where;
			both->m_rhs = m_commonFilterAst;
			m_ast->m_where = both;
		}
		else {
			m_ast->m_where = m_commonFilterAst;
		}
	}

	m_renderedText = text;
	m_renderedFilterAstVersion = m_commonFilterAstVersion;
}

bool ibDataDBComposer::BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const
{
	// Cache the paged hot path only; the tree's parent filter is excluded (its
	// reference blob is not signable). An ANCHORED page is NOT cached: the anchor
	// keyset EMBEDS its values (a reference key renders as its _RRRef blob, not an
	// ibParam), so the SQL is per-anchor — a shared cached render would replay a
	// stale anchor. The unanchored first page still caches.
	if (page.m_count <= 0 || page.m_hierarchyFilter || page.m_hasAnchor)
		return false;
	for (const auto& p : m_params)
		if (!ValueSignable(p.second))
			return false;

	signature << wxT("c") << page.m_count
	          << wxT("d") << static_cast<int>(page.m_direction)
	          << wxT("a") << (page.m_hasAnchor ? 1 : 0)
	          << wxT("r") << (page.m_reverseSort ? 1 : 0)
	          << wxT("|T") << m_renderedText << wxT("|P");
	for (const auto& p : m_params)
		signature << wxT(";") << p.first << wxT("=") << ValueSig(p.second);
	return true;
}

ibDataQueryResult ibDataDBComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals) const
{
	return Execute(schema, hasTotals, ibReadPageRequest{});
}

ibDataQueryResult ibDataDBComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
                                          const ibReadPageRequest& page) const
{
	EnsureAst();

	// The auxiliary registry of transient (RAM / temp) sources is live for THIS execution: the
	// lowering resolves the rendered "FROM Temp.t0" directly to the registered queryable. Source
	// resolution happens entirely inside the lowering call below, so this scope covers it; the
	// returned result holds the queryable already bound (no re-resolution during the row walk).
	ibTempSourceScope tempScope(m_directSources);
	// Thread THIS query's config into the lowering (parallel to the temp-source scope) — ResolveSource resolves a
	// by-name metaobject source against it, not the global factory.
	ibSourceMetaDataScope mdScope(m_metaData);

	hasTotals = m_ast->m_hasTotals;
	m_serverGroupedLevel = false;
	if (hasTotals) {
		// Pass THIS fetch's page so a single-scalar-dim TOTALS drill can page its groups server-side
		// (m_serverGroupedLevel then tells Run to emit the flat groups at level 1, skipping the fold).
		//
		// ⭐⭐ A PAGE MEANS A LEVEL, AND A LEVEL HAS NO ROWS IN IT. This is the line where a RESULT and
		// a DRILL stop being the same question. A report asks for the whole thing and its rows are the
		// bottom of it — always, unconditionally, that is what a total is made of. A browsed list asks
		// for ONE FLOOR at a time: RunComposerPage renders just the browsed level's TotalBy, and the
		// rows under the deepest heading arrive as their own flat fetch with a parent filter, not as
		// detail nodes hanging off this one ("a list drills through headings and its rows ARE its
		// detail" — the note above TakeGroups, which is where this became visible).
		//
		// Asking for details here cost everything and bought nothing: details and the DBMS's own fold
		// are exclusive, so every page of a grouped list fell back to reading the WHOLE table and
		// folding it in memory, to show twenty headings. The page was never applied to the detail read
		// either, so it was the whole table per page. On a register of a million rows that is not a
		// slow list, it is an unusable one.
		//
		// ⚠ THE TEST IS THE PAGE, not the caller. A door that asked "am I a report or a list?" would
		// be answering by who is calling instead of by what was asked for, and the next caller would
		// have to be added to it by hand.
		const bool wholeResult = (page.m_count == 0);
		bool serverGrouped = false;
		ibDataQueryResult r = ibQueryLowering::ExecuteTotals(*m_ast, m_params, schema, page, &serverGrouped,
			wholeResult && WantsDetails(Root()));
		m_serverGroupedLevel = serverGrouped;
		return r;
	}

	wxString signature;
	if (BuildPageSignature(page, signature)) {
		if (!m_pageCache)
			m_pageCache = ibDataQueryBuilder::NewPageCache();
		return ibQueryLowering::Execute(*m_ast, m_params, schema, page, *m_pageCache, signature);
	}
	return ibQueryLowering::Execute(*m_ast, m_params, schema, page);
}

// ⭐ WHICH NODE KINDS AN OUTPUT WRITES. Asked BY KIND rather than by a yes/no flag, so the walk and
// the ladder speak one vocabulary: a node knows what it is (ibSelectorNodeKind), a level knows what
// it declares (ibCompositionLevelKind), and this is the one place the two are matched up.
//
// Headings are what a fold is FOR, so an output always writes them. Rows are written by an output
// whose ladder names them — the level a person adds when the report should print what it counted.
// It lives here, beside the walk, because it is a question about traversal and not a property of
// the output; `WantsDetails` in the header answers the other question, whether they are READ, and
// that one is now always yes.
static bool OutputWrites(const ibDataComposer::Output& output, ibSelectorNodeKind kind)
{
	if (kind != ibSelectorNodeKind::Detail)
		return true;
	for (const ibDataComposer::GroupNode& level : output.m_rowGroups)
		if (level.m_kind == ibCompositionLevelKind::Details)
			return true;
	return false;
}

bool ibDataDBComposer::LevelShows(const Output& output, int depth,
	const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row) const
{
	// Depth 0 is the grand total and belongs to no level; past the last level there is nothing left
	// to hide by.
	if (depth <= 0 || static_cast<size_t>(depth) > output.m_rowGroups.size())
		return true;

	const GroupNode& level = output.m_rowGroups[static_cast<size_t>(depth) - 1];
	for (const FilterItem& filter : level.m_filters) {
		// The filter names an OUTPUT column — the same names a person picked from. A name this
		// result does not carry cannot hide anything: it says nothing about the rows in hand, and
		// hiding on it would be hiding for a reason nobody can see.
		size_t at = schema.size();
		for (size_t i = 0; i < schema.size(); ++i) {
			if (schema[i].m_name.IsSameAs(filter.m_path, false)
			    || schema[i].m_alias.IsSameAs(filter.m_path, false)) {
				at = i;
				break;
			}
		}
		if (at >= schema.size() || at >= row.size())
			continue;

		const auto value = m_params.find(filter.m_param);
		if (!ibCompositionCompare(row[at], filter.m_op, value != m_params.end() ? value->second : ibValue()))
			return false;
	}
	return true;
}

bool ibDataDBComposer::Run(ibCompositionDriver& driver)
{
	// ⭐⭐ ONE BUILD, ONE STATE OF THE DATA. A composition is not one query — it is a dozen: the source
	// itself, a join the server would not take stitched from two reads, a subquery promoted to a temp
	// table, a page at a time, and a fetch per reference whose presentation is printed. Under
	// read-committed each of those reads whatever has committed by the moment it starts, so a batch of
	// documents posted mid-build lands in some parts of the answer and not others — the total in the
	// header stops matching the rows beneath it, with nothing to say which half is which. A snapshot
	// makes the whole build read one state; see ibDbTxOptions::snapshot.
	//
	// ⚠ HERE AND NOT IN L3, and the reason is worth keeping: L3's ExecuteRead does not finish the read
	// it starts. It hands back a live cursor and the caller draws the rows afterwards, so a
	// transaction ending when that function returns kills the cursor its own result depends on —
	// measured on 2026-08-22 as "-504, cursor is not open" on the first row of the first query. The
	// transaction has to outlive the RESULT, and this is the nearest place that does.
	//
	// ⚠ AROUND THE BUILD, NOT AROUND THE WINDOW. Holding one state costs the server the record
	// versions that state needs. A build ends; a list left open and scrolled for minutes does not, and
	// must not hold one — between its pages the data legitimately moves.
	//
	// A build reads its rows before it returns, so holding the snapshot in a local is enough here —
	// unlike the script's query door, where the answer outlives the call and the snapshot travels
	// with it. Same object either way; only who holds it differs. A transaction already open makes
	// this null, and a null holder holds nothing.
	const std::shared_ptr<ibQueryReadState> readsOneState;   // ⛔ NOT OPENED — see queryReadState.h

	// THE FIRST OUTPUT — what a list has, and what "run the composer" has always meant.
	return RunOutput(Root(), driver);
}

bool ibDataDBComposer::RunOutput(const Output& output, ibCompositionDriver& driver)
{
	// ONE READ PER REFERENCE — and nothing declared here to arrange it. The reference knows whether it
	// has read (its own initialised flag), and there is one of it per identity per session, so forty
	// printed lines naming the same object hold one object and cost one query. This function briefly
	// opened a scope to bound that; the scope was the wrong shape, because knowing every place a
	// reference gets reused is knowing nearly every place there is.

	// The driver IS the envelope: a paged driver (the list fetch) vends the page
	// request; a plain driver reads everything.
	ibReadPageRequest page;
	const bool paged = driver.GetPageRequest(page);

	std::vector<ibQueryLowering::OutputColumn> schema;
	bool hasTotals = false;
	ibDataQueryResult result = ExecuteFor(output, schema, hasTotals, page);

	// WHICH SHAPE THIS OUTPUT IS ABOUT TO BE READ IN. Three facts decide everything below — whether a
	// page was asked for, whether the query folds, and whether the DBMS already did the folding — and
	// a report that comes out wrong is almost always wrong about one of them.
	ibJournalInfo(wxT("composer"), wxT("output '%s': %s, totals %s, server-grouped %s, %u columns"),
		output.m_name,
		paged ? wxT("paged") : wxT("whole"),
		hasTotals ? wxT("yes") : wxT("no"),
		m_serverGroupedLevel ? wxT("yes") : wxT("no"),
		static_cast<unsigned>(schema.size()));

	// WHAT IS COMING, said before the first row: the output's kind, its own schema and its name.
	// Two outputs of one composition show different fields, so the schema belongs to the output and
	// not to the composition.
	ibCompositionOutputInfo info;
	info.m_kind   = output.Kind();   // read off its fields — a column grouping is what makes it a cross-table
	info.m_schema = schema;
	info.m_name   = output.m_name;
	driver.OnOutputBegin(info);

	std::vector<ibValue> row(schema.size());
	if (m_serverGroupedLevel) {
		// Server-paged GROUPS (one grouping level, keyset-paged by the DB) — already grouped, so emit each as a
		// level-1 DRILLABLE group node WITHOUT the ByGroups fold (which folds a flat detail snapshot). The row
		// reads exactly like the flat cursor. (⚠ a reference-spread group value needs m_objectPrefix in the
		// schema — a follow-up; a scalar dim reads straight. docs: group-level paging)
		while (result.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
					row[i] = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
				else
					row[i] = oc.m_byAlias ? result.GetColumn(oc.m_alias) : result.GetValue(oc.m_col);
			}
			// GROUPS the server already folded — a group, said as one. It stands over nothing HERE
			// (the server returned the folded rows, not what went into them), so it is a heading with
			// nothing to open: a list must not offer an expander, a printed report must still style it
			// as the heading it is. Which is exactly why the two answers travel separately.
			driver.OnGroup(1, /*hasChildren*/true, /*showsWhatIsUnder*/false, row);
		}
	}
	else if (!hasTotals) {
		// Flat result — the forward cursor; a dot-walk object leaf reassembles from
		// its prefixed field spread (mirrors the runtime selection's ReadColumn).
		//
		// hasChildren = KNOWN TO HAVE CHILDREN, and a flat cursor never knows: finding out costs an
		// EXISTS per row. So it answers `false` and does not guess.
		//
		// It must not be pressed into answering "may this row be entered" either. That was the shape
		// of the first fix here — a level read reported every row as having children, which is true
		// of an ITEM hierarchy (a chart of accounts: an account is subordinate to an account) and
		// false of a folders+items one, where only a folder may be entered. One flag, two meanings,
		// so every item in a catalog grew an expander.
		//
		// Being ENTERABLE is decided where the source is known — the model reads the hierarchy KIND
		// off the queryable (IsItemHierarchy) and the folder flag off the row, and ORs this in.
		while (result.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
					row[i] = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
				else
					row[i] = oc.m_byAlias ? result.GetColumn(oc.m_alias) : result.GetValue(oc.m_col);
			}
			// NOTHING WAS GROUPED, so every row is a DETAIL row — which is exactly what an output
			// with no grouping fields is for. Said as a detail rather than as a level-0 group,
			// because a printer lays the two out differently and should not have to infer which
			// it got from the depth.
			driver.OnDetail(0, row);
		}
	}
	else {
		// TOTALS — the folded tree; the selector's Next() is a pre-order walk over
		// EVERY node, so one loop covers groups and details, Level() = depth.
		//
		// ⭐ A LEVEL'S FILTER IS APPLIED HERE, and hiding is all it does: the fold has already run,
		// so a heading that fails its level's filter simply is not written, and every total above it
		// keeps the rows it was computed from. `hiddenAbove` carries that down — what hangs under a
		// hidden heading is hidden with it, since printing a child of an unprinted parent would put
		// it under the wrong heading.
		ibSelector sel = result.Select(ibSelectKind::ibSelectKind_ByGroups);
		// ⭐ THE GRAND TOTAL IS PART OF THE WALK WHEN THE READER WANTS ONE. It is the tree's root and
		// the fold already rolled every row into it; asking for it here is what puts it in front of
		// the driver, which prints it at the BOTTOM of the section (a pre-order walk hands it over
		// first — see ibSpreadsheetComposeDriver).
		if (driver.WantsGrandTotal())
			sel.WalkOverall();

		// ⭐⭐ ONE LOOP PER LEVEL, NESTED — the shape the language itself reads: walk the groups, and
		// for each of them walk what is under it. A selection now visits its OWN level only, so the
		// walk descends instead of relying on a single pre-order cursor and a depth counter.
		//
		// The hidden-heading rule falls out of the shape rather than being carried in a variable: a
		// heading its level's filter rejects is simply not descended into, so nothing under it is
		// written. `hiddenAbove` existed to say that in a flat walk, and there is nothing left for it
		// to say here.
		std::function<void(ibSelector&)> walk = [&](ibSelector& level) {
			while (level.Next()) {
				for (size_t i = 0; i < schema.size(); ++i) {
					const ibQueryLowering::OutputColumn& oc = schema[i];
					row[i] = oc.m_byAlias ? level.GetColumn(oc.m_alias) : level.GetValue(oc.m_col);
				}

				if (!LevelShows(output, level.Level(), schema, row))
					continue;                   // hidden heading — and with it everything beneath
			// ⭐ A HEADING OR A ROW — the NODE says which, and the driver is told in its own words.
			// The fold produces headings for every level of the BY list and one node per source row
			// under the deepest one. A printer lays the two out differently, so it must not have to
			// infer the difference from the depth — a depth cannot answer it once the tree holds both.
			//
			// ⭐⭐ AND WHAT IS WRITTEN IS CHOSEN BY THE NODE'S KIND, not by whether the rows were read.
			// The rows are ALWAYS there now — they are what the totals were made of — so an output
			// that never declared a Details level meets them here and simply steps over them. Reading
			// and printing are two questions: the tree holds everything, the ladder says which kinds
			// this output writes.
				if (level.Kind() == ibSelectorNodeKind::Detail) {
					if (!OutputWrites(output, ibSelectorNodeKind::Detail))
						continue;
					driver.OnDetail(level.Level(), row);
					continue;                   // a row has nothing under it
				}

				// A heading, and then whatever it stands over — including the GRAND TOTAL, which is
				// a level like any other: the one that groups by nothing. Descending into it is how
				// the first dimension level is reached when a report asked for it.
				// ⚠ EXPANDABLE MEANS "THERE IS SOMETHING TO SHOW", not "there is something there".
				// The rows are always folded in now, so the deepest heading always HAS children —
				// but an output whose ladder never declared a Details level does not write them, and
				// a triangle that opens onto nothing is worse than no triangle. So the flag asks the
				// same question the writing does.
				ibSelector under = level.Select(ibSelectKind::ibSelectKind_ByGroups);

				// Asked by LOOKING: step onto the first child, read what kind it is, and rewind. The
				// selection is already folded, so this costs a pointer move — and it is the only way
				// to know, because a heading two levels up stands over headings while the deepest one
				// stands over rows, and nothing on the node itself says which.
				bool showsWhatIsUnder = false;
				if (under.Next()) {
					showsWhatIsUnder = under.Kind() != ibSelectorNodeKind::Detail
						|| OutputWrites(output, ibSelectorNodeKind::Detail);
					under.Reset();
				}

				// BOTH answers travel — see ibCompositionDriver::OnGroup. HasChildren() is the fold's
				// own fact and is what makes a heading a heading; showsWhatIsUnder is this output's
				// promise and is what an expander may offer.
				driver.OnGroup(level.Level(), level.HasChildren(), showsWhatIsUnder, row);
				walk(under);
			}
		};
		walk(sel);
	}


	driver.OnOutputEnd(hasTotals);
	return true;
}

// EXECUTE FOR ONE OUTPUT. The first output rides the CACHED parse — a list re-reads it on every
// page, and re-parsing per page is what that cache exists to avoid. Any other output renders and
// parses on the spot: keying one cache by which output asked would be a second question for it to
// answer, and outputs past the first are read once per composition, not once per scroll.
ibDataQueryResult ibDataDBComposer::ExecuteFor(const Output& output,
	std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals, const ibReadPageRequest& page) const
{
	if (&output == &Root())
		return Execute(schema, hasTotals, page);

	const wxString text = RenderTextFor(output);
	ibQuerySelectPtr ast = ibQueryParser().Parse(text);
	if (ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query of an output failed to parse"));

	// The output's own tree condition, ANDed into what the text already asks — the same rule the
	// first output follows in EnsureAst, and for the same reason: a condition built as an expression
	// is never rendered and re-parsed.
	if (output.m_filterAst) {
		if (ast->m_where) {
			ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
			both->m_isOr = false;
			both->m_lhs  = ast->m_where;
			both->m_rhs  = output.m_filterAst;
			ast->m_where = both;
		}
		else {
			ast->m_where = output.m_filterAst;
		}
	}

	ibTempSourceScope     tempScope(m_directSources);
	ibSourceMetaDataScope mdScope(m_metaData);

	hasTotals = ast->m_hasTotals;
	m_serverGroupedLevel = false;   // group-level paging belongs to the paged list, not to a report's output
	// Same rule as the root output above: a fetch that carries a page is asking for ONE LEVEL, and a
	// level has no rows in it. Written the same way in both places on purpose — one question, one
	// answer, wherever it is asked from.
	return hasTotals
		? ibQueryLowering::ExecuteTotals(*ast, m_params, schema, page, nullptr,
			page.m_count == 0 && WantsDetails(output))
		: ibQueryLowering::Execute(*ast, m_params, schema, page);
}

//////////////////////////////////////////////////////////////////////
// PruneUnresolvedSettings — the settings, re-asked rather than chased
//////////////////////////////////////////////////////////////////////

int ibDataComposer::PruneUnresolvedSettings(const std::function<bool(const wxString&)>& resolves)
{
	if (!resolves)
		return 0;   // no host answer = no verdict, and no verdict means nothing is dropped

	int dropped = 0;

	// Read the survivors out, then put them back. There is no remove-one on this store by design —
	// the lines are a LIST the fetch reads in order, and a rebuild keeps that order exact.
	{
		std::vector<FilterItem> kept;
		std::map<wxString, ibValue> keptParams;
		for (const FilterItem& item : m_commonFilters) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			const auto param = m_params.find(item.m_param);
			if (param != m_params.end())
				keptParams.emplace(param->first, param->second);
			kept.push_back(item);
		}
		if (kept.size() != m_commonFilters.size()) {
			m_commonFilters = std::move(kept);
			// A parameter belongs to the line that bound it; the ones whose line went are gone with
			// it. Left behind they would be bound into a query that never mentions them.
			for (auto it = m_params.begin(); it != m_params.end(); ) {
				// Only the AUTO-named ones (AddParam: `__f<n>`) belong to a filter line. A parameter the
				// caller named itself is theirs, and dropping it here would be this pass reaching outside
				// what it was asked about.
				it = (it->first.StartsWith(wxT("__f")) && keptParams.find(it->first) == keptParams.end())
					? m_params.erase(it) : std::next(it);
			}
		}
	}

	{
		std::vector<SortItem> kept;
		for (const SortItem& item : m_commonSorts) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			kept.push_back(item);
		}
		if (kept.size() != m_commonSorts.size())
			m_commonSorts = std::move(kept);
	}

	// EVERY LEVEL OF THE LADDER, and every field inside it. A level that loses ALL its fields loses
	// itself and the levels below move up — the author's deeper grouping is not what stopped
	// resolving, so it is not what should disappear.
	for (GroupNode& level : LevelChain()) {
		std::vector<TotalByItem> kept;
		for (const TotalByItem& item : level.m_fields) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			kept.push_back(item);
		}
		if (kept.size() != level.m_fields.size())
			level.m_fields = std::move(kept);
	}
	CollapseEmptyLevels();

	// The rendered text and its cached parse are keyed by the settings — a dropped line changes both.
	// The rendered text and its cached parse are keyed by the settings; the version is what tells the
	// cache the tree changed when the TEXT alone would say it did not.
	if (dropped > 0)
		++m_commonFilterAstVersion;

	return dropped;
}
