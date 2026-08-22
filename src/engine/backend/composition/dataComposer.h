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
#include <algorithm>    // CollapseEmptyLevels drops the levels that lost their fields
#include <functional>   // PruneUnresolvedSettings asks the HOST whether a path still resolves
#include <map>
#include <memory>
#include <vector>

class ibDataQueryResult;
struct ibReadPageRequest;
struct ibRenderedPageCache;
class ibBackendQueryable;
class ibBackendQueryColumn;

// WHAT AN OUTPUT IS — the shape of the data it produces, and only that. Two shapes, because there
// are two: levels down the page, or levels down AND across.
//
// ⚠ A CHART IS NOT A THIRD SHAPE. It reads exactly what a cross-table reads — series along one
// axis, points along the other, a resource where they meet — and differs in being DRAWN as a
// picture. Drawing is the driver's business, so a chart is an output with a chart driver, not a
// kind of its own; adding one here would be a name for a difference that lives elsewhere.
enum class ibCompositionOutputKind
{
	Grouping,   // levels one under another; a level with no fields is its detail rows
	Table,      // levels down AND across — the cross-table
};

// ⭐ WHAT A LEVEL OF THE LADDER IS — a GROUPING or the DETAIL RECORDS (Max: a detail record IS an
// empty grouping). The rows themselves are a level: they sit at the bottom of the ladder, under the
// deepest heading, and the settings tree writes them as a node like any other.
//
// SAID WITH A TYPE, not with "the fields are empty". Emptiness happens by accident too — a level
// whose fields stopped resolving loses them, and CollapseEmptyLevels drops it precisely so a
// nameless heading does not swallow every row. One emptiness, two opposite meanings; the node says
// which it is, and nothing downstream has to guess.
enum class ibCompositionLevelKind
{
	Grouping,   // a heading: fold by this level's fields
	Details,    // the rows as they are, under the level above
};

// What an output tells its driver before its first row: what shape is coming and what the values
// mean. The schema is the output's OWN — two outputs of one composition show different fields.
struct ibCompositionOutputInfo
{
	ibCompositionOutputKind                    m_kind = ibCompositionOutputKind::Grouping;
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	wxString                                   m_name;   // what the output is called, when it is
};

// The OUTPUT DRIVER — the passive sink the composer writes the walked result into.
// Flat result: every row arrives as (level=0, hasChildren=false). A TOTALS result
// arrives as the folded tree's pre-order walk: a group node carries its subtotals
// in the aggregates' own columns (in-place), level = depth, hasChildren = folder.
//
// ⭐ THE NODE LANGUAGE. A composition hands over OUTPUTS, and the four verbs below are what it says
// about each: it begins, it produces groups and detail rows, it ends. They are stated on top of the
// row verbs rather than instead of them — a driver that only understands rows (a list's fetch)
// keeps working untouched, and one that wants to know whether a row was a GROUP or a DETAIL
// overrides the pair. That distinction is the one the old contract could not carry: a group and a
// detail row arrived as the same sentence, and the printer had to guess from the level.
class BACKEND_API ibCompositionDriver
{
public:
	virtual ~ibCompositionDriver() = default;

	// An output STARTS. Default: state its schema the way the row contract always did.
	virtual void OnOutputBegin(const ibCompositionOutputInfo& info) { OnColumns(info.m_schema); }

	// A GROUP of the output — its depth, its values (the level's key fields in the level's own order,
	// with the resources rolled in place), and TWO different facts about what is under it.
	//
	// ⭐⭐ THEY ARE NOT THE SAME QUESTION, and one bool answering both is how the innermost heading of
	// a printed report came out looking like a detail line. `hasChildren` is about the FOLD: does this
	// node stand over anything at all — which is what makes it a heading, and what makes the root the
	// grand total. `showsWhatIsUnder` is about the OUTPUT: will this output actually print what is
	// under it — which is what an expander triangle must promise, because a triangle that opens onto
	// nothing is worse than no triangle.
	//
	// They disagree exactly where it matters: a deepest heading over detail rows in an output that
	// declares no detail level HAS children and SHOWS nothing. Read as "heading?", that printed the
	// level untinted and unbold; read as "expandable?", a triangle would have opened onto an empty
	// space. Each consumer takes the one it means — the list the second, the printed report the first.
	virtual void OnGroup(int level, bool hasChildren, bool showsWhatIsUnder, const std::vector<ibValue>& values) {
		OnRow(level, hasChildren, values);
	}

	// A DETAIL row — a row as it is, under the level that asked for it. It is never a folder.
	virtual void OnDetail(int level, const std::vector<ibValue>& values) {
		OnRow(level, false, values);
	}

	// The output ENDED. `totals` — it produced a folded tree rather than flat rows.
	virtual void OnOutputEnd(bool totals) { OnComplete(totals); }

	// The output schema (projection order) — before any row.
	virtual void OnColumns(const std::vector<ibQueryLowering::OutputColumn>& schema) = 0;

	// One row / tree node. `values` follow the schema order.
	virtual void OnRow(int level, bool hasChildren, const std::vector<ibValue>& values) = 0;

	// The walk finished. `totals` — the result was a folded TOTALS tree.
	virtual void OnComplete(bool totals) {}

	// ⭐ DOES THIS DRIVER WANT THE GRAND TOTAL? The fold computes it either way — the root of the
	// folded tree holds the whole result's resources — so this is a question about the READER, not
	// about the data: a REPORT prints one at the bottom of every section, a list's fetch would show
	// it as a stray top-level row above the first group.
	//
	// Asked here rather than written into the query text as `BY OVERALL`, because the text is the
	// SETTINGS' — one composition feeds a list and a report, and rewriting it for the printer would
	// change what the list reads. (When the settings grow an explicit "grand totals" switch of their
	// own, this is the seam it lands on.)
	virtual bool WantsGrandTotal() const { return false; }

	// The page ENVELOPE — a paged driver (the list fetch: a stack object built per
	// Get*Fetch call carrying direction / anchor / count) fills the request and
	// returns true; the composer then runs the PAGED read. Default: full read.
	// Plain SELECT only — a TOTALS result folds the whole snapshot.
	virtual bool GetPageRequest(ibReadPageRequest& /*request*/) const { return false; }
};

// COMPARE ONE VALUE THE WAY A FILTER LINE SPELLS IT — `=`, `<>` / `!=`, the four ordered ones, and
// LIKE (whose `%` / `_` are the wildcards the query language uses). An operator nobody recognises
// answers TRUE: a filter that cannot be read must not silently hide rows.
//
// One implementation, because there is one question. The RAM composer asks it per row, the walk
// asks it per group, and a second copy of the spelling would answer one of them differently.
BACKEND_API bool ibCompositionCompare(const ibValue& cell, const wxString& op, const ibValue& value);

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
	// NAMED HERE, DEFINED BELOW — the verbs that take an output are declared long before the
	// settings structures they speak about, and a name a class has not met yet is not a type. A
	// reference to an incomplete type is all a declaration needs; the definitions follow, well
	// before the first member that holds one by value.
	struct GroupNode;
	struct Output;

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
	void   ClearFilters() { m_commonFilters.clear(); }
	size_t FilterCount() const { return m_commonFilters.size(); }
	// The flat reading of a filter — path, operator, value. The TREE has no such
	// reading (that is the point of it) and is not in this list at all: it lives
	// as an AST beside it, so a caller that wants the tree reads the tree.
	bool   GetFilterAt(size_t i, wxString& path, wxString& op, ibValue& value) const {
		if (i >= m_commonFilters.size()) return false;
		path = m_commonFilters[i].m_path; op = m_commonFilters[i].m_op;
		const auto it = m_params.find(m_commonFilters[i].m_param);
		value = (it != m_params.end()) ? it->second : ibValue();
		return true;
	}
	void   ClearSorts() { m_commonSorts.clear(); }
	size_t SortCount() const { return m_commonSorts.size(); }
	bool   GetSortAt(size_t i, wxString& path, bool& ascending) const {
		if (i >= m_commonSorts.size()) return false;
		path = m_commonSorts[i].m_path; ascending = m_commonSorts[i].m_ascending; return true;
	}
	// ⭐ THE GROUPING LADDER IS A CHAIN OF OUTPUTS — one level per output, nested. What an output
	// holds in m_rowGroups is one entry PER LEVEL, and a level's own fields are grouped by their
	// tuple; "by warehouse, then by item" is therefore two entries, not two fields in one.
	//
	// These four doors speak the ladder, because that is what a list has always meant by its
	// grouping: Add appends a LEVEL. Composing several fields INTO one level is a different verb
	// (GroupFieldAdd), asked for by a report and never by a list.
	void   ClearGroups() { LevelChain().clear(); }
	size_t GroupCount() const { return LevelChain().size(); }
	bool   GetGroupAt(size_t i, wxString& path, ibQueryDimUnfold& kind) const {
		const std::vector<GroupNode>& chain = LevelChain();
		if (i >= chain.size() || chain[i].m_fields.empty()) return false;
		path = chain[i].m_fields.front().m_path; kind = chain[i].m_fields.front().m_kind; return true;
	}

	// THE PROJECTION, readable. Third member of this family to be write-only (filters and sorts
	// always had their triple, totals got theirs today, and this is the last): a settings window
	// showing WHICH FIELDS a level outputs has to read them back, and there was no way to.
	//
	// EMPTY MEANS ALL, and that is a real answer rather than a missing one — a composition that
	// selects nothing outputs every column its source has. A host shows that as "everything",
	// never as an empty list of chosen fields.
	void   ClearSelected() { m_commonSelected.clear(); }
	size_t SelectCount() const { return m_commonSelected.size(); }
	bool   GetSelectAt(size_t i, wxString& path) const {
		if (i >= m_commonSelected.size()) return false;
		path = m_commonSelected[i];
		return true;
	}
	bool   RemoveSelectAt(size_t i) {
		if (i >= m_commonSelected.size()) return false;
		m_commonSelected.erase(m_commonSelected.begin() + i);
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
		return { m_commonSelected.size(), m_commonFilters.size(), m_commonSorts.size(), m_totals.size(), LevelChain().size() };
	}
	void RestoreScope(const SettingsScope& s) {
		if (m_commonSelected.size() > s.sel) m_commonSelected.resize(s.sel);
		if (m_commonFilters.size()  > s.flt) m_commonFilters.resize(s.flt);
		if (m_commonSorts.size()    > s.srt) m_commonSorts.resize(s.srt);
		if (m_totals.size()   > s.tot) m_totals.resize(s.tot);
		TrimLevels(s.tby);   // the ladder is a chain of outputs — trimming it cuts the chain
	}

	// Grouping is the one setting whose RENDER is per-fetch (the browsed level), while its CONFIG (all dims)
	// is persistent. RunComposerPage takes the config out, renders just the level's TotalBy for the fetch,
	// then puts the config back — so the persistent grouping is never lost and the fetch sees only its level.
	//
	// ⚠ THIS PAIR CARRIES DIMENSIONS ONLY — a level's head field and its unfold. A DETAIL level has
	// neither, so a round trip through here would not bring it back. That is why the pair belongs to
	// the browsed LIST (tabularModelDb) and not to a report: a list drills through headings and its
	// rows ARE its detail. A report never passes through here; if one ever has to, this pair grows a
	// kind rather than a flag saying "and there was an empty one at the end".
	std::vector<std::pair<wxString, ibQueryDimUnfold>> TakeGroups() {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> out;
		// The LADDER, one entry per level — a level's head field, which is what this pair has always
		// carried. A level composed of several fields keeps them; only the ladder travels here.
		for (const GroupNode& level : LevelChain())
			if (!level.m_fields.empty())
				out.emplace_back(level.m_fields.front().m_path, level.m_fields.front().m_kind);
		TrimLevels(0);
		return out;
	}
	void PutGroups(const std::vector<std::pair<wxString, ibQueryDimUnfold>>& saved) {
		TrimLevels(0);
		for (const auto& g : saved)
			AppendLevel(g.first, g.second);
	}

	// APPEND A LEVEL to the ladder — the ordinary "group by this, then by that". The first one fills
	// the root output (which until then produced detail rows); each next becomes a child of the last.
	void AppendLevel(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements) {
		if (path.IsEmpty()) return;
		GroupNode level;
		level.m_fields.push_back({ path, kind });
		LevelChain().push_back(std::move(level));
	}

	// A LEVEL THAT LOST ALL ITS FIELDS IS NOT A LEVEL. It would fold every row it sees into one
	// nameless heading, which reads as data loss rather than as a dropped setting — so it goes, and
	// the levels below simply move up.
	//
	// ⚠ THE DETAIL LEVEL IS NOT THAT. It has no fields BY CONSTRUCTION and says so with its kind,
	// so it stays exactly where the author put it — dropping it here would delete a setting nobody
	// touched, and the rows under the last heading would silently stop being printed.
	void CollapseEmptyLevels() {
		std::vector<GroupNode>& chain = LevelChain();
		chain.erase(std::remove_if(chain.begin(), chain.end(), [](const GroupNode& level) {
			return level.m_kind == ibCompositionLevelKind::Grouping && level.m_fields.empty();
		}), chain.end());
	}

	// ⚠ NO "AppendDetails" VERB. A detail level is a level with no fields, and it is made where every
	// level is made — the grouping FORM, whose empty field list IS the request (see
	// ibComposerGroupingDialog). A second door onto the same node would be a second answer to "what
	// makes a detail record", and it was never taken.

	// CUT THE LADDER to `levels` — that level and everything under it goes. Zero clears the grouping
	// altogether, and the output then produces its detail rows.
	void TrimLevels(size_t levels) {
		std::vector<GroupNode>& chain = LevelChain();
		if (levels < chain.size())
			chain.resize(levels);
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

	// READ ONE OUTPUT and hand its rows to `driver` in the node language (OnOutputBegin / OnGroup /
	// OnDetail / OnOutputEnd). The realisation decides HOW — rendered text for the DB composer, the
	// live rows for the RAM one.
	virtual bool RunOutput(const Output& /*output*/, ibCompositionDriver& /*driver*/) { return false; }

	// ⭐ RUN — load the outputs, then run ONCE and every driver gets filled (Max). Outputs are read
	// in declared order, each into the driver it was given. An output with NO DRIVER is not read at
	// all: nobody would take its rows, and not doing work whose result no one collects is the point
	// of declaring outputs in the first place.
	//
	// The one-argument Run below is the SHORT WAY IN for a caller holding a single driver — a list,
	// which has one output and says so at the call instead of setting it beforehand.
	bool Run() {
		bool read = false;
		for (Output& output : m_outputs) {
			if (output.m_driver == nullptr)
				continue;
			read = RunOutput(output, *output.m_driver) || read;
		}
		return read;
	}

public:
	// ⚠ PUBLIC, because they are what an OUTPUT is made of. A settings window edits them and a
	// variant serialises them, so keeping the parts protected while the whole is public only forced
	// every caller through `auto` and made the types unnameable where they are written to a file.
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

public:
	// ⭐ ONE LEVEL OF A GROUPING — a node with its own fields and its own settings (Max: "inside the
	// row grouping there are grouping nodes, each with its own filters, sorts and fields").
	//
	// The FIELDS are a list because a level groups by all of them TOGETHER — partner and contract in
	// one heading — and its key is the tuple of their values (see ibQueryTotalDim). Which of them
	// actually divide the rows is the data's answer, not a distinction stored here.
	struct GroupNode
	{
		// A HEADING OR THE ROWS — see ibCompositionLevelKind. A Details node carries no fields by
		// construction: there is nothing to group by, which is the whole of what it says.
		ibCompositionLevelKind   m_kind = ibCompositionLevelKind::Grouping;
		std::vector<TotalByItem> m_fields;     // the level's key, grouped as a tuple
		// ⭐ WHAT THIS LEVEL MAY SEE — its available fields, INHERITED unless it says otherwise
		// (Max: "you can set them anywhere, and each may have a set of its own"). This is not the
		// same question as what it SHOWS: a level may see a field and not print it, and a level
		// that cannot see one cannot group, filter or sort by it either.
		bool                     m_availableAuto = true;
		std::vector<wxString>    m_available;
		// ⭐ AUTO — this level shows what the level above it shows. Said with a FLAG and not with an
		// empty list, because emptiness would then mean two different things: "take them from above"
		// and "show nothing at all". Both are legitimate answers and they have to be tellable apart.
		bool                     m_selectedAuto = true;
		std::vector<wxString>    m_selected;   // its OWN fields, once it stops taking them from above
		std::vector<FilterItem>  m_filters;    // narrows THIS level — how a group with nothing in it is hidden
		ibQueryAstExprPtr        m_filterAst;  // its tree condition, ANDed in after the parse
		unsigned int             m_filterAstVersion = 0;
		std::vector<SortItem>    m_sorts;      // orders THIS level
		// ⭐ …AND THE TREE THE CONDITION WAS WRITTEN AS (Max, 2026-08-21: the sort and the filter are
		// stored on EVERY level). The AST above is what the ENGINE reads and it is DERIVED —
		// built from this tree, and rebuilt whenever the tree changes. This is what is SAVED and what
		// the editor reopens on: an expression can be run but not edited back into the lines a person
		// wrote it as, so a level whose filter travelled only as an AST came back empty.
		//
		// Held as a plain ibValue (the tree packs ITSELF — same road the composition's own filter
		// takes) rather than as a typed pointer: a filter tree is a runtime value, and naming its
		// class here would drag the whole filter header into a header everything includes.
		ibValue                  m_filterTree;
	};

	// ⭐ AN OUTPUT — one thing the composition PRODUCES: what to group by, what to show, what to
	// narrow and in what order. A LIST DECLARES ONE. A REPORT DECLARES SEVERAL, and each is read
	// and handed to the driver as a block of its own.
	//
	// AN OUTPUT WITH NO GROUPING FIELDS IS THE DETAIL ONE — the rows as they are. That is not a
	// second kind of output; it is this one with an empty key, which is why nothing below has to ask
	// which of the two it is holding.
	//
	// The grouping fields are a LIST because a level groups by all of them TOGETHER — partner and
	// contract in one heading — and the key is the tuple of their values (see ibQueryTotalDim).
	//
	// What is NOT here, deliberately: the RESOURCES. They are declared once for the whole
	// composition and every output computes the same ones over its own rows; an output chooses which
	// of them to SHOW through m_selected, and choosing is not the same as declaring.
	struct Output
	{
		// WHAT THIS OUTPUT IS CALLED — and it is the same name a statement of the package gives its
		// finished result with `ONTO`. The pair is deliberate and the two words are not the same
		// thing: `INTO` makes a TEMPORARY TABLE (something later statements read), while `ONTO`
		// NAMES A FINISHED RESULT (something an output shows). Empty is legal and ordinary: a
		// composition with one output needs no name for it.
		wxString                 m_name;

		// ⭐ THE ROW GROUPING — the levels down the page, IN ORDER. Each is a node of its own with its
		// own fields, its own filter, its own sort and its own selected fields (Max), so "warehouse,
		// then item" is two entries here rather than a tree of outputs: the order IS the nesting, and
		// a level's settings live where the level does.
		//
		// EMPTY means DETAIL ROWS — the rows as they are. Not a second kind of output, just this one
		// with nothing to group by.
		std::vector<GroupNode>   m_rowGroups;

		// ⭐ THE COLUMN GROUPING — and it is what MAKES this output a cross-table (Max): with nothing
		// here the output is an ordinary grouping down the page; with something here the grouping
		// runs across it too, and the resources land where a row and a column meet.
		//
		// So the KIND is not a setting anybody switches — it is read off these (see Kind()). A
		// separate switch beside them could disagree with them, and then one of the two would be
		// lying about what this output is.
		std::vector<GroupNode>   m_columnGroups;

		// WHAT THIS OUTPUT MAY SEE — inherited from the composition unless it narrows it, and
		// inherited FROM here by its levels.
		bool                     m_availableAuto = true;
		std::vector<wxString>    m_available;

		// AUTO, the same way a level says it — the output shows what the composition shows.
		bool                     m_selectedAuto = true;
		std::vector<wxString>    m_selected;   // its OWN fields; empty and NOT auto = show nothing
		std::vector<FilterItem>  m_filters;    // narrows the WHOLE output; a level narrows itself
		ibQueryAstExprPtr        m_filterAst;  // the tree's condition, ANDed in after the parse
		unsigned int             m_filterAstVersion = 0;
		std::vector<SortItem>    m_sorts;      // orders the whole output

		// WHAT THIS OUTPUT IS — read off its groupings, never stored beside them. A column grouping
		// makes it a cross-table; without one it is an ordinary grouping (and with no row grouping
		// either, that grouping is of nothing, which is how detail rows are asked for).
		ibCompositionOutputKind Kind() const {
			return m_columnGroups.empty() ? ibCompositionOutputKind::Grouping
			                              : ibCompositionOutputKind::Table;
		}

		// ⭐ AN OUTPUT MAY READ SOMETHING OF ITS OWN (Max): several query packages in one report — the
		// first output reads one thing, the second another, and both print onto the same sheet. EMPTY
		// means it reads the composition's own source, which is the ordinary case: several outputs
		// folding one read differently.
		//
		// The shared source is still read ONCE (materialised for the outputs that share it); an
		// output with a package of its own simply runs it.
		wxString                 m_sourceText;

		// ⭐ THE DRIVER BELONGS TO THE OUTPUT (Max): whoever declares the outputs also says who draws
		// each of them — a spreadsheet for one, a chart for another. The composer never routes and
		// never asks what a driver understands; it hands an output's rows to the driver that output
		// was given. NON-OWNING: the host (the list model, the report) owns its drivers and outlives
		// the read.
		//
		// NO DRIVER MEANS THE OUTPUT IS NOT READ. Nobody would take the rows, so the query is not
		// run — which is the whole point of declaring outputs rather than producing everything.
		ibCompositionDriver*     m_driver = nullptr;
	};

	// THE OUTPUTS, in the order they are produced. There is always at least one — a composition that
	// has been told nothing still produces its rows — so nothing has to handle "no output at all".
	std::vector<Output>&       Outputs()       { return m_outputs; }
	const std::vector<Output>& Outputs() const { return m_outputs; }

	// THE COMPOSITION-WIDE SETS, readable and writable — what a settings window edits when the
	// REPORT itself is selected. Available is what everything below may see; selected is what it
	// shows unless it says otherwise.
	std::vector<wxString>&       CommonAvailable()       { return m_commonAvailable; }
	const std::vector<wxString>& CommonAvailable() const { return m_commonAvailable; }
	std::vector<wxString>&       CommonSelected()        { return m_commonSelected; }
	const std::vector<wxString>& CommonSelected() const  { return m_commonSelected; }

	// ⚠ THE OUTPUTS AND THEIR LEVELS ARE EDITED WHERE THEY ARE HELD — `Outputs()` hands over the
	// vector, and the settings window builds a node whole (several fields, a kind, its own filter
	// and sort) before it lands. Verbs that made a one-field level or an empty output stood here for
	// a while and nobody could use them: every real caller has more to say than they could carry.
	//
	// A LEVEL IS THE SAME THING ON EITHER AXIS, which is why there is one type and no second set of
	// anything: `m_rowGroups` reads down the page, `m_columnGroups` across it, and that is the
	// renderer's business rather than the level's.

	// THE FIRST one. Every flat door below (Select / Filter / Sort / TotalBy and the facades over
	// them) writes here: a list has exactly this one, and a report's first heading is the same
	// object rather than a special case of it.
	Output&       Root()       { return m_outputs.front(); }
	const Output& Root() const { return m_outputs.front(); }

	// THE LEVEL CHAIN of the first output — root, its first child, and so on down. This is the
	// GROUPING LADDER a list speaks in, expressed over the outputs that hold it. An output with no
	// grouping fields ends the chain: it is the detail rows, and nothing groups below them.
	// THE LADDER — the first output's row grouping. It IS a list, in order, so nothing has to be
	// walked to find it; these two survive only because the flat doors below read better through a
	// name than through `m_outputs.front().m_rowGroups`.
	std::vector<GroupNode>&       LevelChain()       { return Root().m_rowGroups; }
	const std::vector<GroupNode>& LevelChain() const { return Root().m_rowGroups; }

	// THE LADDER OF ANY OUTPUT — its row grouping, same thing one output over.
	static const std::vector<GroupNode>& ChainFrom(const Output& head) { return head.m_rowGroups; }

	// DOES THIS OUTPUT GROUP BY ANYTHING AT ALL? A level with no fields is the DETAIL records, not a
	// dimension — so a ladder made only of those groups by nothing, and `TOTALS` must not be written
	// for it. Asked here so the two places that decide it cannot answer differently.
	static bool HasGroupingFields(const Output& output) {
		for (const GroupNode& level : output.m_rowGroups)
			if (!level.m_fields.empty())
				return true;
		return false;
	}

	// ⭐⭐ THE ROWS ARE NOT ASKED FOR — THEY ARE WHAT THE TOTALS ARE MADE OF. Where the groupings end,
	// the detail record follows: it is the bottom of every fold, not an option in it (Max,
	// 2026-08-22: "a detail record is a mandatory attribute — you built the totals, and the moment
	// the groupings run out, the detail record follows, showing what is left under the headings
	// above"). A total is computed FROM those rows, so a tree that dropped them kept the answer and
	// threw away what it was an answer to.
	//
	// This used to require a `Details` level written into the ladder, and an output without one
	// could not reach its rows AT ALL — not even on demand, because they were never read. That made
	// the same settings mean two different results depending on a level the author may simply not
	// have added.
	//
	// An output that groups by NOTHING is still not this: it has no headings to hang rows under, its
	// read is a flat cursor, and every row it returns is a detail row already.
	//
	// ⚠ THE PRICE: details and the DBMS's own fold are exclusive (GROUP BY ROLLUP returns aggregated
	// rows and no detail to hang), so the single-level server-side group page — the one that shows
	// twenty groups without reading a million rows — no longer fires for a grouped list. If that
	// shows up as slowness, the answer is for a DRILL to ask for one level without rows, which is a
	// different question from what a RESULT contains; it is not a reason to make the rows optional
	// again.
	static bool WantsDetails(const Output& output) {
		return HasGroupingFields(output);
	}

	// WHICH FIELDS AN OUTPUT SHOWS — the narrowest statement that was actually made. A level speaks
	// for itself, an output for its levels, and the composition for everything: asked separately,
	// every caller would re-implement this precedence, and the copies would drift.
	// WHAT A NODE MAY SEE — the same inheritance the selected fields follow, over a different
	// question: available is what it CAN use (group, filter, sort, show), selected is what it DOES
	// show. EMPTY at the top means "everything the source has", as it always did.
	const std::vector<wxString>& AvailableFor(const Output& output) const {
		return output.m_availableAuto ? m_commonAvailable : output.m_available;
	}
	const std::vector<wxString>& AvailableFor(const Output& output, const GroupNode& level) const {
		return level.m_availableAuto ? AvailableFor(output) : level.m_available;
	}

	const std::vector<wxString>& SelectedFor(const Output& output) const {
		return output.m_selectedAuto ? m_commonSelected : output.m_selected;
	}
	const std::vector<wxString>& SelectedFor(const Output& output, const GroupNode& level) const {
		return level.m_selectedAuto ? SelectedFor(output) : level.m_selected;
	}

protected:
	// Always non-empty (see Outputs) — the one output every composition starts with.
	std::vector<Output>      m_outputs = std::vector<Output>(1);
	std::vector<TotalItem>   m_totals;      // the RESOURCES — common to every output

	// ⭐ THE SELECTED FIELDS OF THE COMPOSITION — what everything shows unless it says otherwise
	// (Max: set them at the root and they spread over all the tables underneath). An output may
	// override them, and a level inside it may override again; the narrowest statement wins, and
	// where nothing was said the answer comes from here.
	//
	// EMPTY MEANS ALL, as it always did — a composition that selects nothing outputs every column
	// its source has.
	std::vector<wxString>    m_commonSelected;

	// ⭐ WHAT THE WHOLE COMPOSITION MAY SEE — the top of the available-fields inheritance. Empty
	// means everything the source offers, which is the ordinary case; narrowing it here narrows it
	// for every output and every level under them.
	std::vector<wxString>    m_commonAvailable;

	// ⭐ THE FILTER AND SORT THAT STAND ABOVE THE OUTPUTS (Max). What is excluded here is excluded
	// for EVERYTHING — no output can see more than this admits — and that is the difference between
	// the three places a condition can sit:
	//
	//   above the outputs : excludes, for the whole composition
	//   on an output      : excludes, within that output
	//   on a level        : hides and orders, and NEVER excludes — cut the data at a level and the
	//                       rows below it are gone, so the totals above can no longer be reconciled
	std::vector<FilterItem>  m_commonFilters;
	ibQueryAstExprPtr        m_commonFilterAst;
	unsigned int             m_commonFilterAstVersion = 0;
	std::vector<SortItem>    m_commonSorts;

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
	// The same rendering for ONE output — its levels, its filter, its sort, its selected fields.
	// RenderText is this over the first output, which is the only one a list has.
	wxString RenderTextFor(const Output& output) const;

	// Read one output — see the base declaration. The first output rides the cached parse (a list
	// re-reads it on every page); any other renders and parses on the spot.
	bool RunOutput(const Output& output, ibCompositionDriver& driver) override;

	// Execute for ONE output: the cached parse for the first, a fresh render + parse for any other.
	ibDataQueryResult ExecuteFor(const Output& output, std::vector<ibQueryLowering::OutputColumn>& schema,
		bool& hasTotals, const ibReadPageRequest& page) const;

private:
	// WHERE / ORDER BY / TOTALS — appended the same way over a composed source and over an author's
	// query, because they ARE the same settings.
	void AppendSettingsClauses(wxString& text, const Output& output) const;

	// DOES THE LEVEL AT `depth` SHOW THIS HEADING? `depth` is the walk's own (1 = the first
	// grouping), and only THAT level's filter is asked — the ones above have already had their say.
	//
	// This is the display half of a filter, and the reason a level's filter never reaches the WHERE:
	// a heading nobody wants to look at (an empty recorder, an opening balance) is hidden, while its
	// rows stay in every total above it. A depth with no level of its own shows everything.
	bool LevelShows(const Output& output, int depth,
		const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row) const;

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
