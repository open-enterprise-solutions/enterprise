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
//   ibDataDBComposer comp;
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
#include <functional>   // PruneUnresolvedSettings asks the HOST whether a path still resolves
#include <map>
#include <memory>
#include <vector>

class ibDataQueryResult;
struct ibReadPageRequest;
struct ibRenderedPageCache;
class ibBackendQueryable;
class ibBackendQueryColumn;

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

// ibDataComposer — the L5-1 SETTINGS store + the polymorphic Run seam. The settings vocabulary
// (select / filter / sort / total / group) is identical for every source; only the SOURCE binding and
// the EXECUTION differ. Two realisations:
//   * ibDataDBComposer (DB)  — renders the settings into L4-1 query TEXT and runs the query (parse → lower
//                            → walk). Source = a factory namespace.name, an author's verbatim text, or a
//                            live queryable registered through the per-query temp registry.
//   * ibDataRamComposer  (RAM) — filters + sorts the source's LIVE rows IN PLACE (no text, no SQL), then walks
//                            them to the driver. Source = the RAM value-storage queryable (ComputeRows).
// The model holds the base via GetModelComposer() and never cares which — list/table/tree is decided by
// the settings, DB-vs-RAM by the realisation. (See docs/ram-composer-decoupling.md.)
class BACKEND_API ibDataComposer
{
public:
	virtual ~ibDataComposer() = default;

	// --- source ---------------------------------------------------------------------
	// Source binding is REALISATION-specific (NOT on the base): ibDataDBComposer.FromSource(queryable/ns.name)/
	// FromText bind a queryable/text; ibDataRamComposer.FromModel binds the live model's rows. The model's
	// subclass ctor calls the right one on its own concrete composer — the base only needs HasSource + Run.
	virtual bool HasSource() const = 0;

	// --- settings (the user vocabulary — SHARED; stored as paths, applied by the realisation) ----------

	// The projection (no Select = ALL the first source's columns). A name may be a dot-walk path; the typed
	// overload pulls the name out of the column object.
	ibDataComposer& Select(const wxString& nameOrPath);
	ibDataComposer& Select(const ibBackendQueryColumn* col) {
		return col != nullptr ? Select(col->GetName()) : *this;
	}

	// A filter line: `path op value`. The value travels as an auto-named &parameter (DB never inlines it;
	// RAM resolves the path to a column and compares the row's value). `op` is the comparison spelling.
	ibDataComposer& Filter(const wxString& path, const wxString& op, const ibValue& value);

	// A CONDITION THAT IS ALREADY AN AST — what a filter tree really produces
	// (listFilter.h) and what `Restrict` compiles to. Handed over as it is, it
	// never becomes text and never gets parsed back: the composer ANDs it into
	// the WHERE of its own parsed query.
	//
	// The round trip through text was not wrong, only pointless — rendering an
	// expression so the parser can rebuild the same expression. It also risked
	// being lossy in the one direction that matters: anything the renderer spells
	// differently from what the parser accepts would fail far from its cause.
	ibDataComposer& FilterAst(const ibQueryAstExprPtr& condition);

	// Register a value and get the &parameter NAME that carries it (without the
	// leading &). Public because the one who BUILDS the condition is the one who
	// knows which values it mentions — a filter tree names them as it goes.
	wxString AddParam(const ibValue& value);

	// A sort line — DB → ORDER BY; RAM → an in-place multi-key compare, in call order.
	ibDataComposer& Sort(const wxString& path, bool ascending = true);

	// Totals: aggregates + dimension levels (DB → `TOTALS agg… BY dim…`, the folded tree). RAM grouping is a
	// deferred follow-up; the slice-1 RAM composer ignores totals (filter + sort only).
	ibDataComposer& Total(const wxString& func, const wxString& path);

	// A RESOURCE AS AN EXPRESSION — `SUM(Amount) / COUNT(DISTINCT Doc)`, written in the query
	// language and rendered verbatim into TOTALS.
	//
	// The two-argument form above is the DEGENERATE CASE of this one: `Total("SUM", "Amount")`
	// renders `SUM(Amount)`, which is what an expression of that shape would render to anyway. A
	// window offering ready aggregates writes the pair; a person writing a ratio writes this, and
	// the composer stores both the same way — an empty function means "the text IS the expression".
	ibDataComposer& TotalExpr(const wxString& expression) { return Total(wxEmptyString, expression); }
	// The grouping VID (kind): Elements / Hierarchy / HierarchyOnly — the ONE switch between a flat and a
	// hierarchical view (lifted to L5 — the list settings carry it).
	ibDataComposer& TotalBy(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);

	// An explicit &parameter (DB: a filter mapped INTO an author's text — virtual-table args included).
	ibDataComposer& Parameter(const wxString& name, const ibValue& value);

	ibDataComposer& ClearSettings();   // drop select/filter/sort/totals (the source stays)

	// --- facade access -------------------------------------------------------------
	// ibValueListSettings (Filter / Order / Group) is a THIN VIEW over these — the composer is the SINGLE
	// settings store (Max: "the composer is the store; the external-caller wrapper writes into it; the fetch
	// does not clear it"). The list model sets defaults in its ctor (FromSource + Sort), the UI mutates through
	// the facade, and the fetch reads a page WITHOUT clearing — a settings change just triggers a refetch that
	// sees the new state.
	void   ClearFilters() { m_filters.clear(); }
	size_t FilterCount() const { return m_filters.size(); }
	// The flat reading of a filter — path, operator, value. The TREE has no such
	// reading (that is the point of it) and is not in this list at all: it lives
	// as an AST beside it, so a caller that wants the tree reads the tree.
	bool   GetFilterAt(size_t i, wxString& path, wxString& op, ibValue& value) const {
		if (i >= m_filters.size()) return false;
		path = m_filters[i].m_path; op = m_filters[i].m_op;
		const auto it = m_params.find(m_filters[i].m_param);
		value = (it != m_params.end()) ? it->second : ibValue();
		return true;
	}
	void   ClearSorts() { m_sorts.clear(); }
	size_t SortCount() const { return m_sorts.size(); }
	bool   GetSortAt(size_t i, wxString& path, bool& ascending) const {
		if (i >= m_sorts.size()) return false;
		path = m_sorts[i].m_path; ascending = m_sorts[i].m_ascending; return true;
	}
	void   ClearGroups() { m_totalBy.clear(); }
	size_t GroupCount() const { return m_totalBy.size(); }
	bool   GetGroupAt(size_t i, wxString& path, ibQueryDimUnfold& kind) const {
		if (i >= m_totalBy.size()) return false;
		path = m_totalBy[i].m_path; kind = m_totalBy[i].m_kind; return true;
	}

	// THE PROJECTION, readable. Third member of this family to be write-only (filters and sorts
	// always had their triple, totals got theirs today, and this is the last): a settings window
	// showing WHICH FIELDS a level outputs has to read them back, and there was no way to.
	//
	// EMPTY MEANS ALL, and that is a real answer rather than a missing one — a composition that
	// selects nothing outputs every column its source has. A host shows that as "everything",
	// never as an empty list of chosen fields.
	void   ClearSelected() { m_selected.clear(); }
	size_t SelectCount() const { return m_selected.size(); }
	bool   GetSelectAt(size_t i, wxString& path) const {
		if (i >= m_selected.size()) return false;
		path = m_selected[i];
		return true;
	}
	bool   RemoveSelectAt(size_t i) {
		if (i >= m_selected.size()) return false;
		m_selected.erase(m_selected.begin() + i);
		return true;
	}

	// THE AGGREGATES, readable like every other setting. They were writable and not readable — the
	// one member of this family without the triple, which is invisible while only a fetch consumes
	// them and becomes a hole the moment a WINDOW has to show what it is folding. A report's
	// resources are exactly that window.
	void   ClearTotals() { m_totals.clear(); }
	size_t TotalCount() const { return m_totals.size(); }
	bool   GetTotalAt(size_t i, wxString& func, wxString& path) const {
		if (i >= m_totals.size()) return false;
		func = m_totals[i].m_func; path = m_totals[i].m_path; return true;
	}
	// Drop one line, keeping the order of the rest — the verb a settings window needs and the only
	// one Clear cannot express.
	bool   RemoveTotalAt(size_t i) {
		if (i >= m_totals.size()) return false;
		m_totals.erase(m_totals.begin() + i);
		return true;
	}
	// CHANGE ONE LINE IN PLACE, keeping its position. A resource that can be added and removed but
	// not EDITED sends a person round the houses to change `SUM` into `AVG`; and Remove+Add would
	// move the line to the end, which is a different report the moment order carries meaning.
	// An EMPTY func means the path IS the expression — the same rule the renderer follows.
	bool   SetTotalAt(size_t i, const wxString& func, const wxString& path) {
		if (i >= m_totals.size() || path.IsEmpty()) return false;
		m_totals[i].m_func = func; m_totals[i].m_path = path;
		return true;
	}

	// DROP EVERY SETTING WHOSE FIELD THE SOURCE NO LONGER HAS. Returns how many went.
	//
	// ⚠ BY RESOLUTION, NEVER BY CHASING THE CHANGE. A filter over a field that only the arbitrary
	// query provided has to go when that query is taken away — and so does one over a table removed
	// from the query, a renamed attribute, a deleted metaobject. A cleanup hung off each of those
	// events is a list of cases, and the case nobody thought of is the bug; re-asking "does this
	// still resolve?" has no cases. (The same idea as ibQueryLowering::PruneUnresolved, one layer up.)
	//
	// `resolves` is asked about the setting's PATH, whole. The composer has no idea what a field is —
	// that is the host's knowledge, and handing the question out is what keeps this layer blind.
	//
	// ⚠ And the same promise: what cannot be VERIFIED is left alone. A host that cannot answer must
	// return true, because "we do not know" must never delete somebody's work.
	int PruneUnresolvedSettings(const std::function<bool(const wxString& path)>& resolves);

	// --- transient scope (per-fetch drill overlay) ---------------------------------
	// The ONE genuinely per-fetch thing is the drill of the browsed parent (each expand fetches ITS children,
	// so the scope can't be a persistent setting). RunComposerPage marks the scope, adds the scope Filter(s) +
	// the level's TotalBy, runs, then restores — so the transient drill never pollutes the persistent settings.
	struct SettingsScope { size_t sel = 0, flt = 0, srt = 0, tot = 0, tby = 0; };
	SettingsScope MarkScope() const {
		return { m_selected.size(), m_filters.size(), m_sorts.size(), m_totals.size(), m_totalBy.size() };
	}
	void RestoreScope(const SettingsScope& s) {
		if (m_selected.size() > s.sel) m_selected.resize(s.sel);
		if (m_filters.size()  > s.flt) m_filters.resize(s.flt);
		if (m_sorts.size()    > s.srt) m_sorts.resize(s.srt);
		if (m_totals.size()   > s.tot) m_totals.resize(s.tot);
		if (m_totalBy.size()  > s.tby) m_totalBy.resize(s.tby);
	}

	// Grouping is the one setting whose RENDER is per-fetch (the browsed level), while its CONFIG (all dims)
	// is persistent. RunComposerPage takes the config out, renders just the level's TotalBy for the fetch,
	// then puts the config back — so the persistent grouping is never lost and the fetch sees only its level.
	std::vector<std::pair<wxString, ibQueryDimUnfold>> TakeGroups() {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> out;
		out.reserve(m_totalBy.size());
		for (const TotalByItem& t : m_totalBy) out.emplace_back(t.m_path, t.m_kind);
		m_totalBy.clear();
		return out;
	}
	void PutGroups(const std::vector<std::pair<wxString, ibQueryDimUnfold>>& saved) {
		m_totalBy.clear();
		for (const auto& g : saved) m_totalBy.push_back({ g.first, g.second });
	}

	// --- output -------------------------------------------------------------------

	// The config this query runs ON BEHALF OF — threaded into the lowering so a by-name metaobject source resolves
	// through THIS config's factory (sources register per-config). Set by whoever binds the source (the dynamic list
	// from its own config; the script query from the running one). Null = a sourceless / transient-only composer.
	ibDataComposer& SetMetaData(const class ibMetaData* metaData) { m_metaData = metaData; return *this; }
	const class ibMetaData* GetMetaData() const { return m_metaData; }

	// The driver-walk seam — the DB composer renders + walks the query to the driver. The RAM composer does NOT
	// use it (the list display calls ComputeOrder + returns the LIVE nodes), so the base default is a no-op and
	// ibDataRamComposer does not override it — L5-2 is self-contained, NO tie to L5-1 (SQL) / L4-1 (text).
	//
	// ONE Run, AND IT TAKES ITS DRIVER. There was a second, no-argument one over a stored driver
	// that a `SetDriver` filled — a whole second way to say where the output goes, and nobody ever
	// used it. Every caller has the driver in hand at the point of the call.
	virtual bool Run(ibCompositionDriver& /*driver*/) { return false; }

protected:
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
		wxString         m_path;
		ibQueryDimUnfold m_kind = ibQueryDimUnfold::Elements;   // the grouping VID (Elements / Hierarchy / HierarchyOnly)
	};

	std::vector<wxString>    m_selected;   // projection (names / dot-walk paths); empty = all
	std::vector<FilterItem>  m_filters;
	// The tree's condition, kept apart from the flat lines: it is ANDed into the
	// WHERE after the parse, so it needs no text form at all.
	ibQueryAstExprPtr        m_filterAst;
	// Bumped whenever it changes — the parse cache is keyed on the rendered text,
	// which says nothing about a condition that never becomes text.
	unsigned int             m_filterAstVersion = 0;
	std::vector<SortItem>    m_sorts;
	std::vector<TotalItem>   m_totals;
	std::vector<TotalByItem> m_totalBy;

	std::map<wxString, ibValue> m_params;
	int                         m_autoParam = 0;   // auto-name counter for filter values

	const class ibMetaData* m_metaData = nullptr;   // config the query resolves by-name sources against (SetMetaData)
};

// The DB composer — the schema verbs render into L4-1 query TEXT, then the standard parse → lower → walk
// pipeline. NO execution of its own below the rendered text. (Was the only composer; the RAM realisation
// and the shared settings moved up to ibDataComposer.)
class BACKEND_API ibDataDBComposer : public ibDataComposer
{
public:
	// --- sources ------------------------------------------------------------------

	// A registered source family (the factory's namespace.name — a metaobject, a virtual table, an external
	// source). A SECOND FromSource adds a joined source: the render emits `JOIN ns.name`.
	ibDataDBComposer& FromSource(const wxString& ns, const wxString& name);

	// The queryable itself — registered through the auxiliary per-query temp registry, rendered as "FROM
	// Temp.t0"; the lowering resolves the name straight back to the live queryable (NO metadata round-trip).
	ibDataDBComposer& FromSource(const ibBackendQueryable* queryable);

	// The author's verbatim L4-1 query — used as is, never edited. Mutually exclusive with FromSource.
	ibDataDBComposer& FromText(const wxString& text);

	// True iff a verbatim author query was installed via FromText. The dynamic list reads this to advertise
	// the CustomQuery feature.
	bool HasCustomText() const { return !m_sourceText.IsEmpty(); }

	// THE NAME AN AUTHOR'S QUERY ANSWERS TO once the settings are written over it — the alias of the
	// nested source in `SELECT * FROM (<their query>) AS AuthorQuery WHERE …`. Exposed because a host
	// that qualifies a filter path has to spell the same word the render does.
	static const wxChar* AuthorQuerySourceName();

	bool HasSource() const override { return !m_sourceText.IsEmpty() || !m_sources.empty(); }

	// Render the schema into L4-1 text (the debug view / the AI seam). Throws when it does not render.
	wxString RenderText() const;

private:
	// WHERE / ORDER BY / TOTALS — appended the same way over a composed source and over an author's
	// query, because they ARE the same settings.
	void AppendSettingsClauses(wxString& text) const;

public:

	// Render -> parse -> lower -> run. Fills the output schema; `hasTotals` reports a folded (TOTALS) result.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals) const;

	// The PAGED read — the driver's envelope threaded into the lowering's paged terminal.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
	                          const ibReadPageRequest& page) const;

	// The full cycle into the given driver.
	bool Run(ibCompositionDriver& driver) override;

private:
	// The composer owns its CACHES — the consumer stays dumb:
	//  * the parse: one AST per rendered text;
	//  * the page render (Lever 1): the door's build-once SQL keyed by the signature.
	void EnsureAst() const;
	bool BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const;

	mutable wxString                             m_renderedText;   // the AST's key
	// …and the other half of that key: the tree condition never becomes text, so
	// the text alone would say "nothing changed" after the whole filter was rewritten.
	mutable unsigned int                         m_renderedFilterAstVersion = 0;
	mutable ibQuerySelectPtr                     m_ast;
	mutable std::shared_ptr<ibRenderedPageCache> m_pageCache;
	// Set by Execute when a TOTALS fetch took the server-side single-level GROUP-BY keyset page (not the detail
	// read + fold): Run then emits the flat groups at level 1 without ByGroups. (docs: group-level paging)
	mutable bool                                 m_serverGroupedLevel = false;
	struct Source
	{
		wxString m_namespace;
		wxString m_name;        // composite for a virtual table (`Goods.Balance`)
	};

	wxString            m_sourceText;   // the author's verbatim query — wins when set
	std::vector<Source> m_sources;      // factory sources (first = FROM, rest = JOIN)

	// Transient (RAM / temp) sources registered via FromSource(queryable) without a metaobject identity —
	// keyed by the unique local name rendered into the text (t0, t1, …). NON-OWNING.
	std::map<wxString, const ibBackendQueryable*> m_directSources;
};

#endif
