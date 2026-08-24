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
#include "backend/compositionDescription.h"   // ibFilterDescription — a level's filter is the stored one

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

// (ibCompositionLevelKind MOVED to compositionDescription.h — a level's kind is part of what a level
//  IS, so it lives with the stored shape and the composer takes it from there. It used to be declared
//  here with the description holding "the same value as a plain number" beside it: a twin vocabulary,
//  and the description now states it as the type it is.)

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
// …AND ASKED WITH THE KIND A STORED CONDITION ACTUALLY HOLDS — see the definition.
BACKEND_API bool ibCompositionCompare(const ibValue& cell, ibComparisonKind kind, const ibValue& value);

// (⛔ A "trivial accumulating driver" — `ibCompositionRowSink`, rows kept in RAM "for validation /
//  the RAM-model feed" — stood here with ONE mention in the whole tree: its own declaration. The two
//  consumers it was built for arrived as drivers of their own (`ibListFetchDriver`,
//  `ibSpreadsheetComposeDriver`), and this was never deleted.)

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
	// ⭐ THE NODE IS NOT FORWARD-DECLARED ANY MORE — it is the DESCRIPTION, which this header already
	// includes, so the alias is stated where the rest of the vocabulary is. Only the output, which is
	// still a type of its own (it carries the driver), needs the declaration ahead of its users.
	using GroupNode = ibLevelDescription;
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

	// ⭐⭐ A FILTER LINE — AND IT LANDS IN THE READER'S SECTION, like every other setting. `path op
	// value`, appended to the filter in force; the value travels as an auto-named &parameter (DB
	// never inlines it; RAM resolves the path to a column and compares the row's value).
	//
	// The reader's section is the one that counts whenever it says anything, and setting anything
	// through here makes it say something (Max, 2026-08-24: "if you set a filter somewhere, it is
	// saved into the user setting"). There is no separate store for a filter a script or a metaobject
	// declares — a declared filter and a chosen one are the same fact, and one of them being
	// invisible to the settings window is how they came to disagree.
	ibDataComposer& Filter(const wxString& path, const wxString& op, const ibValue& value);

	// ⭐ …AND THE ONE THING THAT IS NOT A SETTING: what a single READ is scoped to. The drilled
	// parent's path, the primary key of a point query — the engine's own, pushed before the read and
	// popped after it (MarkScope / RestoreScope). It is ANDed with whatever setting is in force, so
	// drilling into a folder cannot cost the reader their filter, and it is never shown, saved or
	// edited anywhere: nobody chose it.
	ibDataComposer& ScopeTo(const wxString& path, const wxString& op, const ibValue& value);

	// A FILTER DESCRIPTION, TURNED INTO THE CONDITION IT MEANS — the composer's own job: the lines
	// mention VALUES, and a value reaches a query as a named &parameter, which is this object's
	// register (AddParam). Null when the filter says nothing — every condition switched off, or none
	// finished — and a null condition is simply no condition.
	ibQueryAstExprPtr BuildFilterAst(const ibFilterDescription& filter) const;

	// ⭐⭐⭐ A COMPOSER COMPOSES ON A **SETTING**. Nothing here executes "a variant" — a variant is not
	// an executable thing (Max, 2026-08-24). It is a WRAPPER over a setting, and what it adds is a
	// name, a synonym and the setting it points at; several of them is how an author offers a choice.
	// So the array below is the AUTHOR's, it is read for its settings, and the word stops there.
	//
	// ⭐⭐ THE READER'S SETTING IS THE SAME THING A VARIANT HOLDS — an `ibSettingsDescription`, called
	// the reader's (Max, 2026-08-25: *"you use variants, but you started a heap of fields instead of
	// the same setting a variant has — it only needs to be called the user's"*). One type, one shape,
	// and a variant is that setting plus a name.
	//
	// ⭐⭐ AND IT IS A PLACE WHERE VALUES ARE PUT. Empty is its ordinary state — a person who has said
	// nothing has an empty one — and each part of it answers separately: *"while there are none, the
	// zeroth variant of the author's setting is taken"* (Max, 2026-08-25). So a click on a column
	// heading puts an ORDER there and says nothing about grouping, which goes on coming from the
	// zeroth; `ClearSorts()` takes the order back out and the author's stands again.
	//
	// 🛑 THE SHAPE WENT WRONG THREE TIMES IN ONE NIGHT, always the same way: something OTHER than the
	// part itself was asked whether the part had a value. `IsOk()` on the WHOLE setting, then a
	// `std::optional` around the whole, then four optionals beside it — a heap of fields where the
	// setting a variant already has was the answer. The part answers for the part.
	ibDataComposer& LoadVariants(const std::vector<ibVariantDescription>& variants);
	const std::vector<ibVariantDescription>& GetVariants() const { return m_variants; }

	// ⭐⭐ WHAT COMPOSES — THE ONE FUNCTION, and it has ONE name (Max, 2026-08-24: *"there is a single
	// function on the composer's side to get the current setting"*). Section by section: the reader's
	// where they stated one, `m_variants[0]`'s where they did not — and no caller has to know there
	// are two possible answers. (`m_variants` is never empty: the vector is born with one element.)
	//
	// 🛑 IT HAD TWO SPELLINGS FOR A FEW HOURS — `GetRunningSettings()` with `GetCurrentSettingsDesc()`
	// as a one-line alias, and both were called: the frontend through one, the composition value
	// through the other. Two names for one question is the very shape this arc spent a day deleting.
	// BY VALUE — it is assembled from the four answers below and is not a stored object of its own.
	// Every caller already takes a copy (the settings window edits one and hands it back).
	ibSettingsDescription GetCurrentSettingsDesc() const {
		ibSettingsDescription current;
		current.m_filter    = GetCurrentFilterDesc();
		current.m_sort      = GetCurrentSortDesc();
		current.m_group     = GetCurrentGroupDesc();
		current.m_structure = GetCurrentStructure();
		return current;
	}

	// ⭐ THE READER PRESSED OK — their setting becomes what composes, and the zeroth is dropped
	// ENTIRE. That is what "a saved setting is the whole setting" means, and it is the one door that
	// states every section at once — the empty ones included: a person who cleared the sort in the
	// window and pressed OK stated "no order", and the author's order does not come back under it.
	// The window opens on what composes, so nothing is lost by that — they saw the author's and
	// pressed OK over it.
	// ⏭ The variant PICKER is this same call with a different source (`SetUserSettingsDesc(
	// GetVariants()[n].m_settings)`), and so is restoring a saved setting at open — which is why
	// none of the three needs a mechanism of its own.
	ibDataComposer& SetUserSettingsDesc(const ibSettingsDescription& settings);
	// …and it IS what composes: the same object under the name that says whose it is.
	const ibSettingsDescription& GetUserSettingsDesc() const { return m_userSettings; }
	// …and dropping it is the reset: `[0]` composes again.
	ibDataComposer& ClearUserSettings();

	// THE READER'S SETTING, OPENED FOR WRITING — the imperative doors (a column heading clicked,
	// `Sort()` from a script) state into it. It already holds the zeroth's, so stating one thing
	// leaves everything else the report was composing on exactly where it was.
	ibSettingsDescription& UserSettings() { return m_userSettings; }

	// ⭐ WHAT IS IN FORCE — element zero's, and that is the whole of it. These stay because every
	// reader of a setting speaks them, and because naming the question is what kept a SECOND answerer
	// from creeping back in beside them (a flat sort store did exactly that, and everything written
	// through the imperative doors went silent for anyone who had settings).
	// …and its parts, each answering for itself: what the reader PUT there, and while nothing is
	// there, the zeroth variant's (Max, 2026-08-25: *"the user setting is the place where values are
	// put; while there are none, the zeroth variant of the author's setting is taken"*).
	const ibFilterDescription& GetCurrentFilterDesc() const {
		return m_userSettings.m_filter.IsOk() ? m_userSettings.m_filter : m_variants.front().m_settings.m_filter;
	}
	const ibSortDescription& GetCurrentSortDesc() const {
		return m_userSettings.m_sort.IsOk() ? m_userSettings.m_sort : m_variants.front().m_settings.m_sort;
	}
	const ibGroupDescription& GetCurrentGroupDesc() const {
		return m_userSettings.m_group.IsOk() ? m_userSettings.m_group : m_variants.front().m_settings.m_group;
	}
	const std::vector<ibOutputDescription>& GetCurrentStructure() const {
		return !m_userSettings.m_structure.empty() ? m_userSettings.m_structure
		                                           : m_variants.front().m_settings.m_structure;
	}

	// The three of them as one — what a SETTINGS WINDOW opens on. A reader who has set nothing opens
	// on the zeroth (and is meant to: "I open the user settings, I expect to see the author's, I see
	// them"), edits it, and on OK the whole of it becomes what composes. (Declared once, above.)

	// (FilterAst DELETED — a door for handing over a ready condition, and in the whole tree nobody
	//  ever went through it. What it fed was preferred over the setting in force, so had a caller
	//  appeared it would have SILENTLY REPLACED the reader's filter rather than joining it. The
	//  condition a filter means is built from the description at the render, where it belongs.)

	// Register a value and get the &parameter NAME that carries it (without the
	// leading &). Public because the one who BUILDS the condition is the one who
	// knows which values it mentions — a filter tree names them as it goes.
	wxString AddParam(const ibValue& value) const;

	// A sort line — DB → ORDER BY; RAM → an in-place multi-key compare, in call order.
	ibDataComposer& Sort(const wxString& path, bool ascending = true);

	// ⭐ A RESOURCE — what the composition aggregates. Rendered into the query's `TOTALS agg… BY dim…`
	// clause, which is where the word *total* belongs: it is the QUERY LANGUAGE's keyword, not this
	// tier's noun. Everything above the render says **resource**, the way the description
	// (`ibResourceDescription`, `m_resources`) and the settings window already did — one concept had
	// two vocabularies and they met in the middle of one function (audit, 2026-08-24).
	//
	// AN EMPTY FUNCTION MEANS THE PATH IS THE EXPRESSION: `Resource("SUM", "Amount")` renders
	// `SUM(Amount)`, and `Resource("", "SUM(Amount) / COUNT(DISTINCT Doc)")` renders itself. A window
	// offering ready aggregates writes the pair; a person writing a ratio writes the second, and the
	// store keeps both the same way.
	//
	// (`TotalExpr(expr)` DELETED — a one-line convenience for the empty-function form, with no
	//  caller. RAM grouping is still a deferred follow-up: the RAM composer reads filter and sort.)
	ibDataComposer& Resource(const wxString& func, const wxString& path);
	// The grouping VID (kind): Elements / Hierarchy / HierarchyOnly — the ONE switch between a flat and a
	// hierarchical view (lifted to L5 — the list settings carry it).
	ibDataComposer& TotalBy(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements);

	// An explicit &parameter (DB: a filter mapped INTO an author's text — virtual-table args included).
	ibDataComposer& Parameter(const wxString& name, const ibValue& value);

	ibDataComposer& ClearSettings();   // drop select/filter/sort/totals (the source stays)

	// --- facade access -------------------------------------------------------------
	// (Was: a runtime Filter / Order / Group object stood over these as a thin view. It is gone —
	//  what a composition IS is its DESCRIPTION, and SetSettings above is the one way in.)
	// The old note, kept because it still describes the read: the composer is the SINGLE
	// settings store (Max: "the composer is the store; the external-caller wrapper writes into it; the fetch
	// does not clear it"). The list model sets defaults in its ctor (FromSource + Sort), the UI mutates through
	// the facade, and the fetch reads a page WITHOUT clearing — a settings change just triggers a refetch that
	// sees the new state.
	// ⭐⭐ THE SCOPE OF ONE FETCH — and it is the ONLY thing here that is not a setting.
	//
	// The filter, the sort and the grouping are all settings, all in the two sections, and setting
	// one REPLACES what was there; where the reader's section says nothing, the author's is used
	// (Max, 2026-08-24). This list is none of that: it is what a single READ is scoped to — the
	// drilled parent's path, the primary key of a point query — pushed before the read and popped
	// after it (MarkScope / RestoreScope), and ANDed with whatever setting is in force rather than
	// standing in for it. Nobody chose it and nobody can clear it; it belongs to the fetch.
	//
	// 🛑 IT USED TO BE CALLED THE COMPOSITION'S FILTERS, and the settings road ran through it too —
	// `Filter()` wrote here, so a filter a script or a metaobject declared sat in a store the
	// settings window could neither see nor edit.
	void   ClearScope() { m_scopeConditions.clear(); }
	size_t ScopeCount() const { return m_scopeConditions.size(); }
	// The reading of one — path, operator, value. A scope condition has no tree: it is one comparison
	// the engine made up for this read, and a caller that wants the reader's FILTER reads the filter
	// description instead.
	bool   GetScopeAt(size_t i, wxString& path, wxString& op, ibValue& value) const {
		if (i >= m_scopeConditions.size()) return false;
		path = m_scopeConditions[i].m_path; op = m_scopeConditions[i].m_op;
		const auto it = m_params.find(m_scopeConditions[i].m_param);
		value = (it != m_params.end()) ? it->second : ibValue();
		return true;
	}
	// ⭐⭐ THE ORDER IS A SETTING, AND THERE IS ONLY ONE OF IT. These three doors used to speak a FLAT
	// STORE (`m_commonSorts`) beside the two settings sections — and the render preferred the
	// setting, so everything written through here was DROPPED the moment the list had a sort setting
	// of its own. Four live consequences, all of one cause (measured 2026-08-24):
	//
	//   * clicking a column HEADING did nothing — the arrow moved, the rows did not;
	//   * `AddSort` from a script did nothing, the illness `AddTotal` was cured of the same day;
	//   * `ValueTable.Sort()` did nothing;
	//   * and the KEYSET ANCHOR was built from the flat store while the SQL ordered by the setting,
	//     so paging read from the wrong place — the sort was the second answerer, and the two only
	//     disagreed for the users who had settings.
	//
	// A write is somebody STATING THE ORDER NOW, so it lands in the reader's section — where it
	// replaces the author's whole, which is the rule every part of a setting follows. Reads answer
	// what is IN FORCE, so the arrow, the anchor and the ORDER BY cannot disagree by construction.
	// ⭐ TAKES THE READER'S ORDER BACK OUT — and with nothing there, the zeroth's order composes again.
	void   ClearSorts() { m_userSettings.m_sort.Clear(); }
	size_t SortCount() const { return GetCurrentSortDesc().m_lines.size(); }
	bool   GetSortAt(size_t i, wxString& path, bool& ascending) const {
		const ibSortDescription& sort = GetCurrentSortDesc();
		if (i >= sort.m_lines.size()) return false;
		path = sort.m_lines[i].m_path; ascending = sort.m_lines[i].m_ascending; return true;
	}
	// ⭐ THE GROUPING LADDER IS A CHAIN OF OUTPUTS — one level per output, nested. What an output
	// holds in m_rowGroups is one entry PER LEVEL, and a level's own fields are grouped by their
	// tuple; "by warehouse, then by item" is therefore two entries, not two fields in one.
	//
	// These four doors speak the ladder, because that is what a list has always meant by its
	// grouping: Add appends a LEVEL. Composing several fields INTO one level is a different verb
	// (GroupFieldAdd), asked for by a report and never by a list.
	void   ClearGroups() { LevelChain().clear(); }
	// ⭐⭐ THE GROUPING IN FORCE — the reader's when they set one, the LADDER otherwise. **The same
	// rule the render follows**, and that is the whole point: `RenderTextFor` writes `BY` out of
	// `GetCurrentGroupDesc()` when it is set and out of the chain when it is not, so an answer here
	// that came only from the chain made the MODEL and the QUERY disagree about whether the read is
	// grouped at all.
	//
	// 🛑 THAT DISAGREEMENT WAS VISIBLE AS TWO SYMPTOMS AT ONCE (Max, live, 2026-08-24: "filters are
	// kept, groupings are not… as soon as a grouping appears it stops showing anything"). The
	// settings window writes the whole setting — filter, sort AND grouping — into the composer's
	// user section; the filter and the sort are read back from there, and the grouping was read from
	// a THIRD store nobody had written to. So the grouping looked unsaved, and the list emptied: the
	// query came back folded while the model, seeing no dimensions, kept only rows at level 0 and
	// threw every group header away.
	//
	// Sort and filter were brought into the sections earlier in this arc; this is the third part,
	// and leaving it out is what made the fix look arbitrary.
	size_t GroupCount() const {
		const ibGroupDescription& group = GetCurrentGroupDesc();
		return group.IsOk() ? group.m_lines.size() : LevelChain().size();
	}
	bool   GetGroupAt(size_t i, wxString& path, ibQueryDimUnfold& kind) const {
		// A READER'S GROUPING IS A FLAT LIST — one field per level, which is exactly what a LIST
		// means by grouping. (A report's level may weld several fields into one heading; that is the
		// ladder's shape, read below.)
		const ibGroupDescription& group = GetCurrentGroupDesc();
		if (group.IsOk()) {
			if (i >= group.m_lines.size() || group.m_lines[i].m_path.IsEmpty())
				return false;
			path = group.m_lines[i].m_path;
			kind = group.m_lines[i].m_kind;
			return true;
		}
		const std::vector<GroupNode>& chain = LevelChain();
		if (i >= chain.size() || chain[i].m_settings.m_group.m_lines.empty()) return false;
		// THE FIRST LINE ANSWERS FOR THE LEVEL here, and that is what a LADDER means by a grouping:
		// one level, one field. A level of several is the report's shape and is read as itself.
		path = chain[i].m_settings.m_group.m_lines.front().m_path;
		kind = chain[i].m_settings.m_group.m_lines.front().m_kind;
		return true;
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

	// THE AGGREGATES THIS RUN ROLLS — cleared and re-stated from the description at each compose.
	//
	// 🛑 A READABLE TRIPLE STOOD HERE (`TotalCount` / `GetTotalAt` / `RemoveTotalAt` / `SetTotalAt`),
	// added so a WINDOW could show what the composition folds. The window was then written against
	// the DESCRIPTION's own `m_resources` — which is right, since that is what gets saved — and the
	// four were left with no caller anywhere in the tree (audit, 2026-08-24). Same shape as the
	// value object's dead settings API: built ahead of a consumer that landed somewhere else.
	void   ClearResources() { m_resources.clear(); }

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

	// ⭐ ONE SECTION, PRUNED — the filter tree, the sort lines and the grouping lines. Both sections
	// go through it (the reader's and the author's), because a field that stopped existing stopped
	// existing for whoever named it. Returns how many lines went, the way the caller counts.
	static int PruneSettingsDesc(ibSettingsDescription& settings,
		const std::function<bool(const wxString& path)>& resolves);

	// --- transient scope (per-fetch drill overlay) ---------------------------------
	// The ONE genuinely per-fetch thing is the drill of the browsed parent (each expand fetches ITS children,
	// so the scope can't be a persistent setting). RunComposerPage marks the scope, adds the scope Filter(s) +
	// the level's TotalBy, runs, then restores — so the transient drill never pollutes the persistent settings.
	//
	// ⚠ THERE IS NO SORT SLOT. Nothing has ever pushed a transient ORDER BY — a drill fetches a
	// level's children and a point query fetches one row, and neither reorders anything. The slot
	// was here for symmetry with a flat sort store that no longer exists.
	struct SettingsScope { size_t sel = 0, cnd = 0, res = 0, tby = 0; };
	SettingsScope MarkScope() const {
		return { m_commonSelected.size(), m_scopeConditions.size(), m_resources.size(), LevelChain().size() };
	}
	void RestoreScope(const SettingsScope& s) {
		if (m_commonSelected.size() > s.sel) m_commonSelected.resize(s.sel);
		if (m_scopeConditions.size() > s.cnd) m_scopeConditions.resize(s.cnd);
		if (m_resources.size() > s.res) m_resources.resize(s.res);
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
	// ⭐⭐ AND IT TAKES THE SETTING'S GROUPING WITH IT, not just the ladder. "Take the groups out for
	// this read" has to mean ALL of them, because the render asks the SETTING first
	// (`GetCurrentGroupDesc()`) and falls back to the ladder — so emptying only the ladder left the
	// query grouped while the caller had decided it was not.
	//
	// 🛑 TWO ANSWERERS, AND THE MODEL COULD ONLY SILENCE ONE. A LIST view ignores the grouping and
	// shows detail records (Max, 2026-08-24) — the model knows it (`s_constIgnoreParent` → flatView)
	// and it took the ladder out, but the setting's grouping went on rendering `TOTALS BY`. The read
	// then came back as level-1 headings while the model kept only level 0, and every row was
	// dropped: **the list emptied**. The drilled path had the mirror of it — the setting rendered
	// EVERY level while the drill wanted the one it was standing on.
	struct TakenGroups {
		std::vector<std::pair<wxString, ibQueryDimUnfold>> m_ladder;
		ibGroupDescription                                 m_setting;   // the reader's
		ibGroupDescription                                 m_zeroth;    // …and the one it falls back to
	};
	TakenGroups TakeGroups() {
		TakenGroups out;
		// The LADDER, one entry per level — a level's head field, which is what this pair has always
		// carried. A level composed of several fields keeps them; only the ladder travels here.
		for (const GroupNode& level : LevelChain())
			if (!level.m_settings.m_group.m_lines.empty())
				out.m_ladder.emplace_back(level.m_settings.m_group.m_lines.front().m_path,
				                          level.m_settings.m_group.m_lines.front().m_kind);
		TrimLevels(0);
		// ⭐ BOTH SIDES, because the read this brackets is the DETAIL read — every row, flat — and
		// emptying only the reader's would let the zeroth's grouping rise into its place and group the
		// very read that asked not to be grouped. What is taken is "whatever would have grouped this".
		out.m_setting = m_userSettings.m_group;
		out.m_zeroth  = m_variants.front().m_settings.m_group;
		m_userSettings.m_group.Clear();
		m_variants.front().m_settings.m_group.Clear();
		return out;
	}
	void PutGroups(const TakenGroups& saved) {
		TrimLevels(0);
		for (const auto& g : saved.m_ladder)
			AppendLevel(g.first, g.second);
		m_userSettings.m_group             = saved.m_setting;
		m_variants.front().m_settings.m_group = saved.m_zeroth;
	}

	// APPEND A LEVEL to the ladder — the ordinary "group by this, then by that". The first one fills
	// the root output (which until then produced detail rows); each next becomes a child of the last.
	void AppendLevel(const wxString& path, ibQueryDimUnfold kind = ibQueryDimUnfold::Elements) {
		if (path.IsEmpty()) return;
		GroupNode level;
		level.m_settings.m_group.Append(path, kind);   // what a level folds by IS its grouping
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
			return level.m_kind == ibCompositionLevelKind::Grouping && !level.m_settings.m_group.IsOk();
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
	// ⭐ A SORT LINE HAS ONE TYPE. This was `struct SortItem { path; ascending; }` — the same pair
	// `ibSortLineDescription` already was, under a second name, so every crossing between a stored
	// setting and a running composer needed a copy loop that could only ever be identity.
	using SortItem = ibSortLineDescription;
	// ⭐⭐ A RESOURCE IS THE DESCRIPTION'S TOO — the third of these, and the last. `struct TotalItem
	// { func; path; }` was the pair `ibResourceDescription` already was, so the resources a person
	// wrote lived in the composer and the description's own list stayed empty: close the report, open
	// it again, and they were gone (Max, 2026-08-24). The ALIAS went with the vocabulary: the type
	// has a name, and a second one for it was half the reason the two words drifted apart.
	// ⭐⭐ A LEVEL'S KEY LINE IS THE DESCRIPTION'S. It was `struct TotalByItem { path; kind; }` — the
	// pair ibGroupLineDescription already was, under a second name, exactly as SortItem was before
	// it. A stored level and a running one then needed a copy loop that could only ever be identity.
	using TotalByItem = ibGroupLineDescription;

public:
	// (GroupNode = ibLevelDescription — the alias is stated at the top of this class, with the rest
	//  of the vocabulary, because members declared long before this point already hold one.
	//
	//  ⭐⭐ ONE LEVEL OF A GROUPING **IS** ITS DESCRIPTION (Max, 2026-08-23: "finish it"). It carried
	//  every field the stored level carries — its kind, what it folds by, selected/available with
	//  their auto flags, its own filter and sort — under a second set of names, so a level had two
	//  shapes and the designer's edits landed in the one the file never saw. What sat here beside
	//  them was DERIVED: a flat FilterItem list nothing ever filled (Filter() writes the
	//  composition-wide one), and the AST built from the filter with a version counter to say it was
	//  fresh. An expression built from a description is not a second state to keep.
	//
	//  A node has its own settings and its own children (Max: "a grouping holds an array of filters
	//  — nodes —, an array of groupings, of sorts, of available fields", and under it another
	//  grouping or the detail records). WHAT IT FOLDS BY is m_settings.m_group, a LIST because a
	//  level groups by all of its lines TOGETHER — partner and contract in one heading — and its key
	//  is the tuple of their values (see ibQueryTotalDim). Which of them actually divide the rows is
	//  the data's answer, not a distinction stored here.)

	// ⭐⭐ AN OUTPUT **IS** ITS DESCRIPTION, plus the one thing that cannot be stored: WHO DRAWS IT.
	//
	// Everything else it used to declare — the name, the row and column groupings, selected and
	// available with their auto flags, its own filter and sort, the source it may read — is exactly
	// what ibOutputDescription holds, and holding it twice is how a structure edited in the designer
	// never reached the file (Max, 2026-08-23). Stored by INHERITING the description: assigning the
	// base part in is loading, slicing it out is saving, and neither is a copy loop that can drift.
	//
	// (The flat FilterItem list and the cached AST are gone with the same slip: nothing ever filled
	//  the first — Filter() writes the composition-wide one — and the second is DERIVED from the
	//  filter description, so it is built where it is used.)
	//
	// AN OUTPUT WITH NO GROUPING FIELDS IS THE DETAIL ONE — the rows as they are. That is not a
	// second kind of output; it is this one with an empty key, which is why nothing below has to ask
	// which of the two it is holding.
	//
	// What is NOT here, deliberately: the RESOURCES. They are declared once for the whole
	// composition and every output computes the same ones over its own rows; an output chooses which
	// of them to SHOW through m_selected, and choosing is not the same as declaring.
	struct Output : ibOutputDescription
	{
		// ⭐ THE DRIVER BELONGS TO THE OUTPUT (Max): whoever declares the outputs also says who draws
		// each of them — a spreadsheet for one, a chart for another. The composer never routes and
		// never asks what a driver understands; it hands an output's rows to the driver that output
		// was given. NON-OWNING: the host (the list model, the report) owns its drivers and outlives
		// the read.
		//
		// NO DRIVER MEANS THE OUTPUT IS NOT READ. Nobody would take the rows, so the query is not
		// run — which is the whole point of declaring outputs rather than producing everything.
		//
		// ⚠ AND IT IS THE REASON THIS TYPE EXISTS AT ALL. A driver is a live object: it cannot be
		// saved, cannot travel to the web and has no business in a description.
		ibCompositionDriver*     m_driver = nullptr;

		// WHAT THIS OUTPUT IS — read off its groupings, never stored beside them. A column grouping
		// makes it a cross-table; without one it is an ordinary grouping (and with no row grouping
		// either, that grouping is of nothing, which is how detail rows are asked for).
		ibCompositionOutputKind Kind() const {
			return m_columnGroups.empty() ? ibCompositionOutputKind::Grouping
			                              : ibCompositionOutputKind::Table;
		}
	};

	// THE OUTPUTS, in the order they are produced. There is always at least one — a composition that
	// has been told nothing still produces its rows — so nothing has to handle "no output at all".
	std::vector<Output>&       Outputs()       { return m_outputs; }
	const std::vector<Output>& Outputs() const { return m_outputs; }

	// WHAT THE WHOLE COMPOSITION SHOWS, readable and writable — what a settings window edits when the
	// REPORT itself is selected, and what every output and node adds to.
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

	// DOES THIS OUTPUT GROUP BY ANYTHING AT ALL? A node with an empty grouping is the DETAIL records
	// — "the same grouping, only empty; there are no groupings there, only available fields, and
	// those are inherited down the tree" (Max, 2026-08-23) — so an axis made only of those groups by
	// nothing, and `TOTALS` must not be written for it. Asked here so the two places that decide it
	// cannot answer differently.
	static bool HasGroupingFields(const Output& output) {
		for (const GroupNode& level : output.m_rowGroups)
			if (level.m_settings.m_group.IsOk())
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

	// ⭐⭐ WHICH FIELDS AN OUTPUT SHOWS — EVERYTHING SAID ABOUT IT, PILED UP. The composition speaks
	// first, the output adds to that, a node adds to the output: nobody restates the list to add one
	// column, and nobody can silently drop what a storey above asked for (Max, 2026-08-24: "if a
	// sub-node has additional selected fields, they are laid on top of the existing ones").
	//
	// 🛑 IT USED TO REPLACE. `level.m_selectedAuto ? SelectedFor(output) : level.m_selected` — so the
	// moment a node named a field of its own, everything the report was told to show disappeared
	// under it. That is also why the `Auto` flag existed: to say "do not replace". Under adding
	// there is nothing to say — a node that adds nothing has an empty list — so the flag went.
	//
	// 🛑 AND THERE IS NO "AVAILABLE" ANY MORE. It was the same statement understood in a harder way,
	// it had no reader on the run path, and what a person means is SELECTED: these are the fields I
	// want to see. Asked once, here, so no caller re-implements the pile-up.
	//
	// By value, not by reference: what is in force is COMPOSED of several statements and is not any
	// one of them. Duplicates are dropped — the same field named twice is named once.
	static void AppendFields(std::vector<wxString>& into, const std::vector<wxString>& added) {
		for (const wxString& field : added)
			if (std::find(into.begin(), into.end(), field) == into.end())
				into.push_back(field);
	}
	std::vector<wxString> SelectedFor(const Output& output) const {
		// 🛑 THE BASE GOES THROUGH THE SAME SIEVE. Taking it as it stands let a duplicate that was
		// already inside it reach the SELECT list, and a derived table refuses two columns of one
		// name: "column FLD1022_TYPE was specified multiple times for derived table Q_SUB0"
		// (Firebird -104, measured 2026-08-24). A field named twice is named once.
		std::vector<wxString> selected;
		AppendFields(selected, m_commonSelected);
		AppendFields(selected, output.m_selected);
		// ⏭ AND THE FIELDS A NODE NAMES ARE **NOT** HERE YET — held back on 2026-08-24 to keep the
		// diagnostic build cheap, and to be put back once the journal says whether the projection is
		// what is actually short. The invariant is not in doubt: the inner query becomes a derived
		// table and the outer folds over it, so anything the outer mentions — what a level groups by,
		// orders by, filters on or shows — the inner owes.
		return selected;
	}
	std::vector<wxString> SelectedFor(const Output& output, const GroupNode& level) const {
		std::vector<wxString> selected = SelectedFor(output);
		AppendFields(selected, level.m_selected);
		return selected;
	}

protected:
	// Always non-empty (see Outputs) — the one output every composition starts with.
	std::vector<Output>      m_outputs = std::vector<Output>(1);
	std::vector<ibResourceDescription> m_resources;   // common to every output

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

	// ⭐ THE FILTER AND SORT THAT STAND ABOVE THE OUTPUTS (Max). What is excluded here is excluded
	// for EVERYTHING — no output can see more than this admits — and that is the difference between
	// the three places a condition can sit:
	//
	//   above the outputs : excludes, for the whole composition
	//   on an output      : excludes, within that output
	//   on a level        : hides and orders, and NEVER excludes — cut the data at a level and the
	//                       rows below it are gone, so the totals above can no longer be reconciled
	//
	// ⭐ WHAT ONE READ IS SCOPED TO — the ONLY thing on this composer that is not a setting, and the
	// reason it is a list of its own. The engine pushes and pops these per fetch through MarkScope
	// (the drilled parent's path, the primary key of a point query) and they are ANDed with whatever
	// setting is in force, never chosen between. A setting has no such stack discipline.
	//
	// 🛑 IT WAS `m_commonFilters`, AND THE SETTINGS ROAD RAN THROUGH IT — `Filter()` wrote here, so a
	// filter a script or a metaobject declared lived where the settings window could neither show it
	// nor edit it. Everything a person or the platform states is one setting now.
	//
	// 🛑 `m_commonSorts` stood beside it and was not this thing at all — see ClearSorts above for the
	// four defects that came of it. 🛑 `m_commonFilterAst` and its version counter went with it:
	// `FilterAst()` had no caller anywhere in the tree, so the branch that preferred it was reached
	// only with a null in hand, and the counter it bumped was a cache key for a value that was
	// always the same. What actually invalidates the cache is the two sections, which the render
	// already compares.
	std::vector<FilterItem>  m_scopeConditions;

	// THE USER'S SECTION — see SetUserSettingsDesc. Empty while nobody has set one, and then
	// everything above runs on what the developer declared.
	//
	// ⭐ IT LIVES ON THE BASE, so it is the SAME construction for a RAM table (Max, 2026-08-23:
	// "saved settings apply to the RAM table too"). A value table, a tabular section and a record
	// set read through this composer exactly as a list does; nothing about a saved setting is about
	// where the rows came from.
	// ⭐⭐ THE VARIANTS THE AUTHOR DECLARED — a COPY of the description's array, driven in when the
	// source is built. Always at least one element; `[0]` is what composes while the reader has set
	// nothing (Max, 2026-08-24: *"m_variants[0] is the one that composes when there is no user
	// setting"*).
	std::vector<ibVariantDescription> m_variants = std::vector<ibVariantDescription>(1);

	// …AND THE READER'S OWN — a SETTING, the same type the variants wrap. Setting a variant is
	// setting a setting (Max, 2026-08-24), so there is one shape here and no second one to convert
	// between, and nothing beside it: no flag, no cursor, no per-section twin. Empty is its ordinary
	// state and needs no marking — an empty part IS "the reader put nothing here".
	ibSettingsDescription m_userSettings;

	// (`m_standartSettings` DELETED — "the author's settings" was never a thing of its own. There is
	//  the ARRAY, and `[0]` is what composes while nobody has saved a setting; a second member
	//  holding "what the author declared" was that same element under a second name.)
	//
	// ⭐ THE ARRAY IS SERIALISED AND THE READER'S SETTING IS NOT. Not by this class — the composer is
	// never written anywhere: the array's content IS the description's, loaded when the source is
	// built and saved back through the description when an AUTHOR edits it in the designer. What a
	// reader saves has no file and no description: it stands while the report is open and is gone
	// with it.

	mutable std::map<wxString, ibValue> m_params;   // the rendered query's own values — filled while a query is being made
	mutable int                 m_autoParam = 0;   // auto-name counter for filter values

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
	// …and the other half: the USER's filter is built at the render, out of a section that is written by
	// plain assignment. Nothing bumps a counter when it changes, so the key is the setting itself —
	// which is also the honest question ("is this the same setting I rendered?").
	// …and THE FILTER it was rendered with, which is the only other half of the key.
	//
	// ⭐ NOT "the variant that was rendered" — there is no such thing (Max, 2026-08-24). Everything a
	// setting decides EXCEPT the filter ends up in the text above, so the text already answers for
	// it; a filter TREE never becomes text — it is ANDed into the parsed WHERE — so the text alone
	// would say "nothing changed" after somebody rewrote the whole filter. Two whole settings copies
	// stood here while there were two sections to compare, and most of what they compared was
	// already compared as text.
	mutable ibFilterDescription                  m_renderedFilter;
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
