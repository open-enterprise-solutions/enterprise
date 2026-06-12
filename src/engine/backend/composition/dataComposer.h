#ifndef __DATA_COMPOSER_H__
#define __DATA_COMPOSER_H__

// L5-1 — the data composer: a declarative settings layer OVER the text query
// language (L4-1).
//
// The composer holds a SCHEMA (sources + settings, spoken in the USER vocabulary —
// filter / sort / total, not WHERE / ORDER BY / TOTALS) and RENDERS it into
// ordinary L4-1 query text. The generated query is indistinguishable from a
// hand-written one, and the text stays the ONLY seam downward: the composer never
// touches the L3 door or composes queryables itself (a path like
// `Producer.Region.Name` is plain data here — the lowering resolves it).
// Queryables are reached READ-ONLY, through the source factory, as the column
// dictionary for rendering/describe.
//
// Output goes through a DRIVER (Run(driver)): the composer walks the executed
// result — flat rows or the folded TOTALS tree (the selector's pre-order walk) —
// and WRITES each row into the driver; the consumer then takes its output form
// (a list model, a tree, a report table) from the driver. One composer, many
// drivers — the realization dictionary (list / report / …).
//
// The composer is long-lived: it lives as long as its consumer (the list window /
// the report), keeps the attached driver, and re-runs on demand. The verbs are
// FLUENT (mirroring the L3 door's style, one tier up):
//
//   ibDataComposer comp;
//   comp.FromSource(GetQueryable())              // or FromSource("Catalog", "Goods")
//       .Filter(wxT("Producer.Region"), wxT("="), regionRef)
//       .Sort(wxT("Description"))
//       .Run(driver);                            // render -> parse -> lower -> walk -> driver
//
// See docs/query-language-arc.md §22.1b / §23.

#include "backend/backend_core.h"
#include "backend/compiler/value.h"        // ibValue — driver rows / parameters
#include "backend/query/queryAst.h"        // ibQuerySelectPtr — the cached parse
#include "backend/query/queryLowering.h"   // ibQueryLowering::OutputColumn (+ ibBackendQueryColumn)

#include <wx/string.h>
#include <map>
#include <memory>
#include <vector>

class ibDataQueryResult;
struct ibReadPageRequest;
struct ibRenderedPageCache;

// The OUTPUT DRIVER — the passive sink the composer writes the walked result into.
// Flat result: every row arrives as (level=0, hasChildren=false). A TOTALS result
// arrives as the folded tree's pre-order walk: a group node carries its subtotals
// in the aggregates' own columns (in-place), level = depth, hasChildren = folder.
class BACKEND_API ibCompositionDriver
{
public:
	virtual ~ibCompositionDriver() = default;

	// The output schema (projection order) — before any row.
	virtual void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) = 0;

	// One row / tree node. `values` follow the schema order.
	virtual void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) = 0;

	// The walk finished. `totals` — the result was a folded TOTALS tree.
	virtual void OnComplete(bool totals) {}

	// The page ENVELOPE — a paged driver (the list fetch: a stack object built per
	// Get*Fetch call carrying direction / anchor / count) fills the request and
	// returns true; the composer then runs the PAGED read. Default: full read.
	// Plain SELECT only — a TOTALS result folds the whole snapshot.
	virtual bool GetPageRequest(ibReadPageRequest& /*request*/) const { return false; }
};

// A trivial accumulating driver — rows kept in RAM (validation / RAM-model feed).
class BACKEND_API ibCompositionRowSink : public ibCompositionDriver
{
public:
	struct Row
	{
		int                  m_level = 0;
		bool                 m_hasChildren = false;
		std::vector<ibValue> m_values;
	};

	void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) override {
		m_schema = schema;
		m_rows.clear();
	}
	void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) override {
		m_rows.push_back({ level, hasChildren, values });
	}

	const std::vector<ibQueryLowering::OutputColumn>& Columns() const { return m_schema; }
	const std::vector<Row>& Rows() const { return m_rows; }

private:
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	std::vector<Row>                           m_rows;
};

// The composer — the schema verbs + render + the driver walk. NO execution of its
// own: everything below the rendered text is the standard L4-1 pipeline.
class BACKEND_API ibDataComposer
{
public:
	// --- sources ------------------------------------------------------------------

	// A registered source family (the factory's namespace.name — a metaobject, a
	// virtual table, an external source). A SECOND FromSource adds a joined source:
	// the render emits `JOIN ns.name` with the language's auto-join-by-reference
	// (explicit link conditions arrive on this seam next).
	ibDataComposer& FromSource(const wxString& ns, const wxString& name);

	// The queryable itself — the typed face for a C++ consumer (the list model holds
	// it via GetQueryable()). Holding a queryable IS the proof the source is
	// queryable — a metaobject overload would not be (reports / data processors
	// vend none). The language identity is recovered through the queryable's
	// metadata context (GetMetaData + GetQueryMetaID -> the owning metaobject);
	// READ-ONLY — the queryable is never handed to the door, only its NAME flows
	// into the text. A virtual-table companion reports its register — name such a
	// source explicitly via FromSource(ns, name).
	ibDataComposer& FromSource(const ibBackendQueryable* queryable);

	// The author's verbatim L4-1 query — used as is, never edited. Mutually
	// exclusive with FromSource (the last call wins).
	ibDataComposer& FromText(const wxString& text);

	// --- settings (the user vocabulary — rendered, not executed) -------------------

	// The projection (factory sources only; the author's text keeps its own SELECT
	// list). No Select = ALL the first source's columns. A name may be a dot-walk
	// path. The typed overload pulls the name out of the column object.
	ibDataComposer& Select(const wxString& nameOrPath);
	ibDataComposer& Select(const ibBackendQueryColumn* col) {
		return col != nullptr ? Select(col->GetName()) : *this;
	}

	// A filter line: `path op value` — AND-folded into WHERE. The value travels as
	// an auto-named &parameter (never inlined into the text). `op` is the language
	// comparison spelling (`=`, `<>`, `>`, `LIKE`, …) — the parser validates it.
	ibDataComposer& Filter(const wxString& path, const wxString& op, const ibValue& value);

	// A sort line — rendered into ORDER BY, in call order.
	ibDataComposer& Sort(const wxString& path, bool ascending = true);

	// Totals: aggregates + dimension levels — rendered into `TOTALS agg… BY dim…`,
	// the result arrives at the driver as the folded tree. `func` is the language
	// aggregate spelling (SUM / MIN / MAX / AVG / COUNT).
	ibDataComposer& Total(const wxString& func, const wxString& path);
	ibDataComposer& TotalBy(const wxString& path, bool hierarchy = false);

	// An explicit &parameter of the author's text (the schema's deep variability
	// point: a filter mapped INTO the query — virtual-table args included).
	ibDataComposer& Parameter(const wxString& name, const ibValue& value);

	ibDataComposer& ClearSettings();   // drop select/filter/sort/totals (the source stays)

	// --- output -------------------------------------------------------------------

	// The attached output place — non-owning; a long-lived consumer wires it once
	// and calls Run() on every refresh.
	ibDataComposer& SetDriver(ibCompositionDriver* driver) { m_driver = driver; return *this; }

	bool HasSource() const { return !m_sourceText.IsEmpty() || !m_sources.empty(); }

	// Render the schema into L4-1 text (the debug view / the AI seam). Throws
	// ibBackendCoreException when the schema does not render (no source, an
	// unresolvable source, totals without aggregates, …).
	wxString RenderText() const;

	// Render -> parse -> lower -> run. Fills the output schema; `hasTotals` reports
	// a folded (TOTALS) result. Used by Run and by the runtime value wrapper.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals) const;

	// The PAGED read — the driver's envelope threaded into the lowering's paged
	// terminal (a TOTALS query ignores the envelope: it folds the whole snapshot).
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
	                          const ibReadPageRequest& page) const;

	// The full cycle into the given / attached driver.
	bool Run(ibCompositionDriver& driver);
	bool Run();

private:
	// The composer owns its CACHES — the consumer stays dumb:
	//  * the parse: one AST per rendered text (a scroll tick re-renders the SAME
	//    text → no re-parse; a settings change renders a different text →
	//    re-parse, the natural invalidation);
	//  * the page render (Lever 1): the door's build-once SQL keyed by the
	//    signature = rendered text + page shape + the parameter values (a
	//    non-signable value — a reference — disables it for correctness).
	void EnsureAst() const;
	bool BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const;

	mutable wxString                             m_renderedText;   // the AST's key
	mutable ibQuerySelectPtr                     m_ast;
	mutable std::shared_ptr<ibRenderedPageCache> m_pageCache;
	struct Source
	{
		wxString m_namespace;
		wxString m_name;        // composite for a virtual table (`Goods.Balance`)
	};
	struct FilterItem
	{
		wxString m_path;
		wxString m_op;
		wxString m_param;       // the auto-named &parameter carrying the value
	};
	struct SortItem
	{
		wxString m_path;
		bool     m_ascending = true;
	};
	struct TotalItem
	{
		wxString m_func;
		wxString m_path;
	};
	struct TotalByItem
	{
		wxString m_path;
		bool     m_hierarchy = false;
	};

	wxString            m_sourceText;   // the author's verbatim query — wins when set
	std::vector<Source> m_sources;      // factory sources (first = FROM, rest = JOIN)

	std::vector<wxString>    m_selected;   // projection (names / dot-walk paths); empty = all
	std::vector<FilterItem>  m_filters;
	std::vector<SortItem>    m_sorts;
	std::vector<TotalItem>   m_totals;
	std::vector<TotalByItem> m_totalBy;

	std::map<wxString, ibValue> m_params;
	int                         m_autoParam = 0;   // auto-name counter for filter values

	ibCompositionDriver* m_driver = nullptr;
};

#endif
