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

//////////////////////////////////////////////////////////////////////
// settings
//////////////////////////////////////////////////////////////////////

ibDataComposer& ibDataComposer::Select(const wxString& nameOrPath)
{
	if (!nameOrPath.IsEmpty())
		m_selected.push_back(nameOrPath);
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

ibDataComposer& ibDataComposer::Filter(const wxString& path, const wxString& op, const ibValue& value)
{
	m_filters.push_back({ path, op, AddParam(value) });
	return *this;
}

ibDataComposer& ibDataComposer::FilterAst(const ibQueryAstExprPtr& condition)
{
	m_filterAst = condition;
	++m_filterAstVersion;
	return *this;
}

ibDataComposer& ibDataComposer::Sort(const wxString& path, bool ascending)
{
	m_sorts.push_back({ path, ascending });
	return *this;
}

ibDataComposer& ibDataComposer::Total(const wxString& func, const wxString& path)
{
	m_totals.push_back({ func, path });
	return *this;
}

ibDataComposer& ibDataComposer::TotalBy(const wxString& path, ibQueryDimUnfold kind)
{
	m_totalBy.push_back({ path, kind });
	return *this;
}

ibDataComposer& ibDataComposer::Parameter(const wxString& name, const ibValue& value)
{
	m_params[name] = value;
	return *this;
}

ibDataComposer& ibDataComposer::ClearSettings()
{
	m_selected.clear();
	m_filters.clear();
	m_sorts.clear();
	m_totals.clear();
	m_totalBy.clear();
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
	const bool hasSettings = !m_selected.empty() || !m_filters.empty() || !m_sorts.empty()
	                       || !m_totals.empty() || !m_totalBy.empty();

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
		for (const wxString& name : m_selected) {
			if (!authorProj.IsEmpty())
				authorProj += wxT(", ");
			authorProj += name;
		}
		// No explicit selection: everything the nested query yields. `*` and not a reflected list,
		// because the nested query's columns are ITS business — asking what they are would mean
		// resolving the text here, and the lowering is about to do that anyway.
		wxString text = wxT("SELECT ") + (authorProj.IsEmpty() ? wxString(wxT("*")) : authorProj)
			+ wxT("\nFROM (") + m_sourceText + wxT(") AS ") + kAuthorQuerySource;
		AppendSettingsClauses(text);
		return text;
	}

	if (m_sources.empty())
		ibBackendCoreException::Error(_("Composer: no source is set"));
	if (!m_totals.empty() && m_totalBy.empty())
		ibBackendCoreException::Error(_("Composer: totals need at least one TotalBy dimension"));
	// NB: TotalBy WITHOUT an aggregate is valid — "TOTALS BY <dim>" is a pure grouping
	// / hierarchy with no rolled aggregate (a list grouped by a field).

	// --- the projection -------------------------------------------------------
	wxString proj;
	if (!m_selected.empty()) {
		for (const wxString& name : m_selected) {
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

	AppendSettingsClauses(text);
	return text;
}

// WHERE / ORDER BY / TOTALS — the settings, written the same way whichever source they stand over.
// A composed source and an author's query differ in their FROM and in nothing else: the filter a
// person set is the same filter, and two spellings of it would be two chances to drift.
void ibDataDBComposer::AppendSettingsClauses(wxString& text) const
{
	for (size_t i = 0; i < m_filters.size(); ++i) {
		const FilterItem& f = m_filters[i];
		text += (i == 0 ? wxT(" WHERE ") : wxT(" AND "));
		text += f.m_path + wxT(" ") + f.m_op + wxT(" &") + f.m_param;
	}

	for (size_t i = 0; i < m_sorts.size(); ++i) {
		const SortItem& s = m_sorts[i];
		text += (i == 0 ? wxT(" ORDER BY ") : wxT(", "));
		text += s.m_path;
		if (!s.m_ascending)
			text += wxT(" DESC");
	}

	// TOTALS [agg(path), …] BY dim [HIERARCHY], … — the aggregate list may be empty
	// (pure grouping / hierarchy), so emit the block whenever there is either an
	// aggregate OR a BY dimension.
	if (!m_totals.empty() || !m_totalBy.empty()) {
		text += wxT(" TOTALS");
		for (size_t i = 0; i < m_totals.size(); ++i) {
			text += (i == 0 ? wxT(" ") : wxT(", "));
			text += m_totals[i].m_func + wxT("(") + m_totals[i].m_path + wxT(")");
		}
		for (size_t i = 0; i < m_totalBy.size(); ++i) {
			text += (i == 0 ? wxT(" BY ") : wxT(", "));
			text += m_totalBy[i].m_path;
			if (m_totalBy[i].m_kind == ibQueryDimUnfold::Hierarchy)
				text += wxT(" HIERARCHY");
			else if (m_totalBy[i].m_kind == ibQueryDimUnfold::HierarchyOnly)
				text += wxT(" HIERARCHYONLY");
		}
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
	if (m_ast != nullptr && text == m_renderedText && m_filterAstVersion == m_renderedFilterAstVersion)
		return;   // same query, same condition — the cached parse stands

	m_ast = ibQueryParser().Parse(text);
	if (m_ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query failed to parse"));

	// THE TREE'S CONDITION GOES IN AS AN AST, ANDed with whatever the query text
	// already asked for. No rendering, no re-parsing — the expression the filter
	// built is the expression the engine lowers.
	if (m_filterAst) {
		if (m_ast->m_where) {
			ibQueryAstExprPtr both = ibQueryAstExpr::Make(ibQueryAstExprKind::Logical);
			both->m_isOr = false;
			both->m_lhs = m_ast->m_where;
			both->m_rhs = m_filterAst;
			m_ast->m_where = both;
		}
		else {
			m_ast->m_where = m_filterAst;
		}
	}

	m_renderedText = text;
	m_renderedFilterAstVersion = m_filterAstVersion;
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
		bool serverGrouped = false;
		ibDataQueryResult r = ibQueryLowering::ExecuteTotals(*m_ast, m_params, schema, page, &serverGrouped);
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

bool ibDataDBComposer::Run(ibCompositionDriver& driver)
{
	// The driver IS the envelope: a paged driver (the list fetch) vends the page
	// request; a plain driver reads everything.
	ibReadPageRequest page;
	const bool paged = driver.GetPageRequest(page);

	std::vector<ibQueryLowering::OutputColumn> schema;
	bool hasTotals = false;
	ibDataQueryResult result = paged ? Execute(schema, hasTotals, page)
	                                 : Execute(schema, hasTotals);

	driver.OnColumns(schema);

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
			driver.OnRow(1, /*hasChildren*/true, row);
		}
	}
	else if (!hasTotals) {
		// Flat result — the forward cursor; a dot-walk object leaf reassembles from
		// its prefixed field spread (mirrors the runtime selection's ReadColumn).
		while (result.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				if (!oc.m_objectPrefix.empty() && oc.m_col != nullptr)
					row[i] = result.GetColumnObject(oc.m_objectPrefix, oc.m_col);
				else
					row[i] = oc.m_byAlias ? result.GetColumn(oc.m_alias) : result.GetValue(oc.m_col);
			}
			driver.OnRow(0, false, row);
		}
	}
	else {
		// TOTALS — the folded tree; the selector's Next() is a pre-order walk over
		// EVERY node, so one loop covers groups and details, Level() = depth.
		ibSelector sel = result.Select(ibSelectKind::ibSelectKind_ByGroups);
		while (sel.Next()) {
			for (size_t i = 0; i < schema.size(); ++i) {
				const ibQueryLowering::OutputColumn& oc = schema[i];
				row[i] = oc.m_byAlias ? sel.GetColumn(oc.m_alias) : sel.GetValue(oc.m_col);
			}
			driver.OnRow(sel.Level(), sel.HasChildren(), row);
		}
	}

	driver.OnComplete(hasTotals);
	return true;
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
		for (const FilterItem& item : m_filters) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			const auto param = m_params.find(item.m_param);
			if (param != m_params.end())
				keptParams.emplace(param->first, param->second);
			kept.push_back(item);
		}
		if (kept.size() != m_filters.size()) {
			m_filters = std::move(kept);
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
		for (const SortItem& item : m_sorts) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			kept.push_back(item);
		}
		if (kept.size() != m_sorts.size())
			m_sorts = std::move(kept);
	}

	{
		std::vector<TotalByItem> kept;
		for (const TotalByItem& item : m_totalBy) {
			if (!resolves(item.m_path)) { ++dropped; continue; }
			kept.push_back(item);
		}
		if (kept.size() != m_totalBy.size())
			m_totalBy = std::move(kept);
	}

	// The rendered text and its cached parse are keyed by the settings — a dropped line changes both.
	// The rendered text and its cached parse are keyed by the settings; the version is what tells the
	// cache the tree changed when the TEXT alone would say it did not.
	if (dropped > 0)
		++m_filterAstVersion;

	return dropped;
}
