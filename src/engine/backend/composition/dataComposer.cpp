////////////////////////////////////////////////////////////////////////////
//	L5-1 — the data composer: schema -> rendered L4-1 text -> driver (dataComposer.h)
////////////////////////////////////////////////////////////////////////////

#include "dataComposer.h"

#include "backend/query/queryParser.h"        // ibQueryParser — text -> AST
#include "backend/query/queryable.h"          // ibBackendQueryable / ibBackendQueryColumn
#include "backend/query/queryableFactory.h"   // the source factory — the column dictionary
#include "backend/query/dataQueryBuilder.h"   // ibDataQueryResult / ibSelectKind
#include "backend/query/querySelector.h"      // ibSelector — the TOTALS pre-order walk
#include "backend/metaData.h"                 // ibMetaData::FindAnyObjectByFilter — queryable -> metaobject
#include "backend/metaCollection/metaObject.h"// ibValueMetaObject — GetClassType / GetName
#include "backend/appData.h"                  // ibApplicationData::GetQueryableFactory
#include "backend/backend_exception.h"        // ibBackendCoreException

//////////////////////////////////////////////////////////////////////
// sources
//////////////////////////////////////////////////////////////////////

ibDataComposer& ibDataComposer::FromSource(const wxString& ns, const wxString& name)
{
	m_sourceText.Clear();
	m_sources.push_back({ ns, name });
	return *this;
}

ibDataComposer& ibDataComposer::FromSource(const ibBackendQueryable* queryable)
{
	// Identity through the metadata context only — the queryable itself never flows
	// downward (the rendered NAME does, and the lowering re-resolves it).
	const ibMetaData* metaData = queryable != nullptr ? queryable->GetMetaData() : nullptr;
	const ibValueMetaObject* meta = metaData != nullptr
		? metaData->FindAnyObjectByFilter<ibValueMetaObject>(queryable->GetQueryTableId())
		: nullptr;
	if (meta == nullptr)
		ibBackendCoreException::Error(_("Composer: the queryable carries no metadata identity"));
	return FromSource(ibValue::GetNameObjectFromID(meta->GetClassType()), meta->GetName());
}

ibDataComposer& ibDataComposer::FromText(const wxString& text)
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

ibDataComposer& ibDataComposer::Filter(const wxString& path, const wxString& op, const ibValue& value)
{
	// The value travels as an auto-named &parameter — never inlined into the text.
	const wxString param = wxString::Format(wxT("__f%d"), m_autoParam++);
	m_params[param] = value;
	m_filters.push_back({ path, op, param });
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

ibDataComposer& ibDataComposer::TotalBy(const wxString& path, bool hierarchy)
{
	m_totalBy.push_back({ path, hierarchy });
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

wxString ibDataComposer::RenderText() const
{
	if (!m_sourceText.IsEmpty()) {
		// The author's text — verbatim, never edited. Settings over an author's query
		// arrive on the NEXT seam (the subquery wrap) — until then they error clearly
		// instead of being silently dropped.
		if (!m_selected.empty() || !m_filters.empty() || !m_sorts.empty() || !m_totals.empty() || !m_totalBy.empty())
			ibBackendCoreException::Error(_("Composer: settings over an author's query text are not supported yet"));
		return m_sourceText;
	}

	if (m_sources.empty())
		ibBackendCoreException::Error(_("Composer: no source is set"));
	if (!m_totals.empty() && m_totalBy.empty())
		ibBackendCoreException::Error(_("Composer: totals need at least one TotalBy dimension"));
	if (m_totals.empty() && !m_totalBy.empty())
		ibBackendCoreException::Error(_("Composer: TotalBy needs at least one Total aggregate"));

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

		ibQueryableFactory* factory = ibApplicationData::GetQueryableFactory();
		if (factory == nullptr)
			ibBackendCoreException::Error(_("Composer: the query engine is not available (no application data)"));

		const Source& s0 = m_sources.front();
		const ibBackendQueryable* src = factory->Resolve(s0.m_namespace, s0.m_name);
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

	if (!m_totals.empty()) {
		for (size_t i = 0; i < m_totals.size(); ++i) {
			text += (i == 0 ? wxT(" TOTALS ") : wxT(", "));
			text += m_totals[i].m_func + wxT("(") + m_totals[i].m_path + wxT(")");
		}
		for (size_t i = 0; i < m_totalBy.size(); ++i) {
			text += (i == 0 ? wxT(" BY ") : wxT(", "));
			text += m_totalBy[i].m_path;
			if (m_totalBy[i].m_hierarchy)
				text += wxT(" HIERARCHY");
		}
	}

	return text;
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

void ibDataComposer::EnsureAst() const
{
	const wxString text = RenderText();
	if (m_ast != nullptr && text == m_renderedText)
		return;   // the same rendered text — the cached parse stands

	m_ast = ibQueryParser().Parse(text);
	if (m_ast == nullptr)
		ibBackendCoreException::Error(_("Composer: the rendered query failed to parse"));
	m_renderedText = text;
}

bool ibDataComposer::BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const
{
	// Cache the paged hot path only; the tree's parent filter is excluded (its
	// reference blob is not signable). Anchor VALUES are deliberately not signed —
	// the cached render rebinds the anchor as a parameter (that is the lever).
	if (page.m_count <= 0 || page.m_parentFilter)
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

ibDataQueryResult ibDataComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals) const
{
	return Execute(schema, hasTotals, ibReadPageRequest{});
}

ibDataQueryResult ibDataComposer::Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
                                          const ibReadPageRequest& page) const
{
	EnsureAst();

	hasTotals = m_ast->m_hasTotals;
	if (hasTotals)
		return ibQueryLowering::ExecuteTotals(*m_ast, m_params, schema);

	wxString signature;
	if (BuildPageSignature(page, signature)) {
		if (!m_pageCache)
			m_pageCache = ibDataQueryBuilder::NewPageCache();
		return ibQueryLowering::Execute(*m_ast, m_params, schema, page, *m_pageCache, signature);
	}
	return ibQueryLowering::Execute(*m_ast, m_params, schema, page);
}

bool ibDataComposer::Run()
{
	if (m_driver == nullptr)
		return false;
	return Run(*m_driver);
}

bool ibDataComposer::Run(ibCompositionDriver& driver)
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
	if (!hasTotals) {
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
