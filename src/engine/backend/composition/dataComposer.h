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
#include "backend/query/queryTempStore.h"  // ibQueryTempTableStore — what the preparing statements made
#include "drivers/compositionDriver.h"             // ibCompositionDriver / ibCompositionOutputInfo — the contract, cut out on 2026-08-28
#include "backend/compositionDescription.h"   // ibFilterDescription — a level's filter is the stored one

// A LEVEL'S ORDER IS THE SELECTION'S — declared, not included: querySelector.h drags the whole query
// tier in, and a header that only NAMES the key needs the name (the walk itself is in the .cpp).
struct ibSelectorSort;

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

// (WHAT AN OUTPUT IS, and WHAT A DRIVER IS HANDED, are stated in compositionDriver.h — cut out on
//  2026-08-28 so that everything which DRAWS a result stops including everything which produces one.)
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

	// ⭐⭐ WHO ANSWERS FOR THE SET OF COLUMNS — the SOURCE, or the person (Max, 2026-08-28).
	//
	// A LIST's columns ARE its source's: it chose them by choosing the source, and "nothing selected"
	// can only mean "everything the source has". A REPORT's columns are what somebody put in its
	// selected fields, and there "nothing selected" means exactly that — you go in and add what you
	// want to see, deliberately.
	//
	// So this is not a preference and not a switch of behaviour: it says which of the two questions
	// this composition is asking. It is set once, by the host that knows (the dynamic list), and a
	// report never touches it.
	//
	// ⚠ AND IT IS WHERE THE QUERY GETS SMALLER. With it false the read stops falling back to a star,
	// so a field nobody named is not fetched, not folded and not rendered.
	bool ReadsEveryField() const { return m_readsEveryField; }
	void SetReadsEveryField(bool yes) { m_readsEveryField = yes; }

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
	ibQueryAstExprPtr BuildFilterAst(const ibFilterDescription& filter);

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
		current.m_selected  = GetCurrentSelectedDesc();
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
	// ⭐ …AND THE FIELDS THE READER CHOSE, asked the same way as its four neighbours: theirs when
	// they said anything, the zeroth variant's when they did not. Answered PER PART, because that is
	// the rule the whole section follows — a person who chose columns said nothing about the sort.
	const std::vector<ibSelectedFieldDescription>& GetCurrentSelectedDesc() const {
		return !m_userSettings.m_selected.empty() ? m_userSettings.m_selected
		                                          : m_variants.front().m_settings.m_selected;
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
	wxString AddParam(const ibValue& value);

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
	// ⭐⭐ EMPTY MEANS ALL FOR A LIST, AND NOTHING FOR A REPORT (Max, 2026-08-28). The two are not the
	// same composition asking one question: a LIST's columns ARE its source's — it chose them by
	// choosing the source, and "nothing selected" can only mean "what the source has". A REPORT's
	// columns are what a person put in this table, and there "nothing selected" means exactly that:
	// you go into selected fields and add what you want to see, deliberately.
	//
	// Which is why the read no longer defaults to a star for a report — that is where the query gets
	// smaller: a field nobody named is not fetched and not rendered.
	void   ClearSelected() { m_commonSelected.clear(); }
	size_t SelectCount() const { return m_commonSelected.size(); }
	bool   GetSelectAt(size_t i, wxString& path) const {
		if (i >= m_commonSelected.size()) return false;
		path = m_commonSelected[i].m_path;
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
		for (std::vector<GroupNode>* chain : { &Root().m_rowGroups, &Root().m_columnGroups })
			chain->erase(std::remove_if(chain->begin(), chain->end(), [](const GroupNode& level) {
				return level.m_kind == ibCompositionLevelKind::Grouping && !level.m_settings.m_group.IsOk();
			}), chain->end());
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
	// OnDetail / OnComplete). The realisation decides HOW — rendered text for the DB composer, the
	// live rows for the RAM one.
	virtual bool RunOutput(const Output& /*output*/, ibCompositionDriver& /*driver*/) { return false; }

	// THE RUN'S OWN BRACKETS — see Run(). A realisation that can read several outputs at once builds
	// that read in BeginRun and lets it go in EndRun; the RAM composer needs neither and says so by
	// not overriding them.
	virtual void BeginRun() {}
	virtual void EndRun()   {}

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
			if (!Declares(output))
				continue;   // nothing was said about it — see below
			read = RunOutput(output, *output.m_driver) || read;
		}
		return read;
	}

	// ⭐⭐ DOES THIS OUTPUT DECLARE ANYTHING? A composition is born with one output and keeps it, so a
	// structure a person built beside it leaves that first one standing with nothing in it — no
	// levels on either axis, not a table, no fields of its own. The settings tree does not show such
	// a row (it has no children); printing it anyway put a stray block of grand totals above the
	// report, and a person looking at the structure had nothing to click to make it go away (Max,
	// 2026-08-25: "what is that rubbish at the top, there is no output there").
	//
	// ⚠ THE LONE OUTPUT IS NOT THAT CASE and must still print. A composition nobody structured is
	// exactly one empty output, and it means "the rows as they are" — which is what every list and
	// every plain report is. Emptiness only reads as "not declared" when something else WAS.
	bool Declares(const Output& output) const {
		if (m_outputs.size() == 1)
			return true;
		return !output.m_rowGroups.empty() || !output.m_columnGroups.empty()
			|| output.Kind() == ibCompositionOutputKind::Table
			|| !output.m_selected.empty();
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

		// WHAT THIS OUTPUT IS — the decision, not a reading of what has been filled in.
		//
		// 🛑 IT USED TO BE DERIVED: `m_columnGroups.empty() ? Grouping : Table`, and that was right
		// while the kind meant "has a column axis been filled in". It stopped being right the moment
		// a person could ADD A TABLE and get one that is empty on both axes — the structure tree
		// would have shown it as a plain grouping, with nowhere to put the first column heading.
		//
		// ⚠ A TABLE WITH NO COLUMN AXIS FILLED IN IS STILL A TABLE, and that is the whole point of
		// storing it: the axes are undeletable nodes of a table, so they exist before anything is in
		// them (Max, 2026-08-25). What reads the CONTENT is the printer, which asks whether there is
		// anything to lay out across the page — a different question, asked where it is answerable.
		ibCompositionOutputKind Kind() const { return m_kind; }
	};

	// THE OUTPUTS, in the order they are produced. There is always at least one — a composition that
	// has been told nothing still produces its rows — so nothing has to handle "no output at all".
	std::vector<Output>&       Outputs()       { return m_outputs; }
	const std::vector<Output>& Outputs() const { return m_outputs; }

	// WHAT THE WHOLE COMPOSITION SHOWS, readable and writable — what a settings window edits when the
	// REPORT itself is selected, and what every output and node adds to.
	std::vector<ibSelectedFieldDescription>&       CommonSelected()       { return m_commonSelected; }
	const std::vector<ibSelectedFieldDescription>& CommonSelected() const { return m_commonSelected; }

	// WHAT THE QUERY'S SELECTS SAY ABOUT THEIR FIELDS — filled from the description at a run, the
	// same road the resources and the selected fields take. A DELTA, always: a select nobody has
	// said anything about is not here at all (Max, 2026-08-26).
	std::vector<ibSelectDescription>&       Selects()       { return m_selects; }
	const std::vector<ibSelectDescription>& Selects() const { return m_selects; }

	// THE TITLE IN FORCE FOR A PATH — through the SAME function the description answers with, so the
	// two cannot drift (ibTitleForPath).
	wxString TitleForPath(const wxString& path) const { return ibTitleForPath(m_selects, path); }

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
	// ⭐ BOTH AXES, for the reason the type has one level and no second set of anything: a table
	// whose only grouping stands across the page is grouped, and a gate that looked down the page
	// only would have written no TOTALS for it — an empty report, with the setting plainly on screen.
	static bool HasGroupingFields(const Output& output) {
		for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
			for (const GroupNode& level : *axis)
				if (level.m_settings.m_group.IsOk())
					return true;
		return false;
	}

	// ⭐ THE LEVEL AT A WALK DEPTH, wherever it lives. The fold nests rows then columns (see
	// AppendSettingsClauses), so a depth past the last row level is a COLUMN level — and asking
	// `m_rowGroups[depth - 1]` there falls off the end and answers "no filter, no order", which is
	// the silent yes again: a cross-table's columns would hide and sort by nothing at all.
	//
	// Depth 0 is the grand total and belongs to no level; past the last level of either axis there
	// is nothing, and both are the same nullptr because both mean "nobody stated anything here".
	// 🛑 AND IT COUNTS THE LEVELS THAT WRITE A KEY, because those are the ones a DEPTH counts. The
	// ladder may also hold the DETAIL level, which writes no `BY` and gets no depth of its own from
	// the fold — so indexing the axis as it is written slid every level past it by one: a column
	// key's depth landed on the detail node, and the column level's own filter and sort were read
	// off the wrong node (silently, which is how "the setting does nothing" always looks).
	//
	// The detail level is reached by ASKING FOR IT (DetailLevelOf), because a node that is a record
	// says so with its kind — it does not have to be found by counting.
	static const GroupNode* LevelAt(const Output& output, int depth,
	                                ibSelectorNodeKind kind = ibSelectorNodeKind::Group) {
		// A RECORD SAYS WHAT IT IS, so it is not looked for by counting — see DetailLevelOf.
		if (kind == ibSelectorNodeKind::Detail)
			return DetailLevelOf(output);
		if (depth <= 0)
			return nullptr;
		size_t at = static_cast<size_t>(depth) - 1;
		for (const std::vector<GroupNode>* axis : { &output.m_rowGroups, &output.m_columnGroups })
			for (const GroupNode& level : *axis) {
				if (!level.m_settings.m_group.IsOk())
					continue;              // the detail records — no key, no depth
				if (at == 0)
					return &level;
				--at;
			}
		return nullptr;
	}

	// THE LEVEL THE ROWS THEMSELVES BELONG TO, wherever the author put it. Either axis may declare
	// it (a table's columns may end in detail records), and there is at most one that matters: the
	// first one found is the one the walk is standing on.
	// ⭐⭐ IS THIS LEVEL THE RECORDS — asked ONE way, because it was being asked two (Max, 2026-08-28,
	// live: a node showing `<detail records>` printed no records at all).
	//
	// The settings window calls a level the detail records when it GROUPS BY NOTHING — that is the
	// caption in its Field cell — while the engine looked only at the level's KIND, the one a person
	// sets by adding a records node on purpose. A grouping somebody left fieldless is the first to a
	// reader and a grouping to the fold, so the window promised rows and the read never asked for
	// them.
	//
	// Max's own words, the same evening: *"a detail record is a level with no grouping fields, not
	// the absence of a level"*. So the fieldless level IS the records, and the KIND stays what it
	// always was — the way to say so deliberately, on a level that is one before anything is typed.
	// (IsDetailLevel is a FREE function of the module — nothing outside the composer asks whether a
	//  level is the records, so nothing outside needs to be told that it can.)
	static const GroupNode* DetailLevelOf(const Output& output);

	// ⭐ …AND WHICH WAY THAT LEVEL READS. A record declared on the COLUMN axis is a column of its
	// own — "exactly as a detail record is in the rows, so in the columns" (Max, 2026-08-26) — and
	// the fold has to be told, because the level itself is written last either way.
	//
	// Rows when nobody declared one: the records are read regardless (they are what the totals are
	// made of), and down the page is where a report puts them.
	static ibTotalsAxis DetailAxisOf(const Output& output);

	// HOW MANY DIMENSIONS AN AXIS CONTRIBUTES — and it counts levels that actually WRITE A KEY, not
	// levels. A level with no fields is the detail records: it writes no `BY` (see
	// AppendSettingsClauses) and never becomes a dimension, so counting it here would move the seam
	// between the axes by one and print a row heading across the page.
	static size_t DimensionCount(const std::vector<GroupNode>& axis) {
		size_t count = 0;
		for (const GroupNode& level : axis)
			if (level.m_settings.m_group.IsOk())
				++count;
		return count;
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
	// 🛑 …AND AN OUTPUT THAT DECLARED A DETAIL NODE WANTS ITS ROWS EVEN WITH NO GROUPINGS. The
	// paragraph above is right about a FLAT read and wrong the moment there is a resource: a
	// composition with one detail node and one figure still writes `TOTALS … BY OVERALL`, so the
	// read FOLDS — 125 rows into one node — and the rows the author asked for by adding that node
	// are never fetched. What printed was the grand total, twice, where every record should have
	// stood (Max, 2026-08-28, live).
	//
	// The question is not "does this output group by anything" but "is there anywhere to show rows",
	// and a detail node IS that somewhere. It says what it is, so it is asked rather than counted.
	static bool WantsDetails(const Output& output) {
		return HasGroupingFields(output) || DetailLevelOf(output) != nullptr;
	}

	// ⭐⭐ HOW MANY LEVELS READ DOWN THE PAGE — the one place the seam of a cross-table is decided,
	// and the number the FOLD is told (ibQueryLowering::ExecuteTotals) so the cells hang under every
	// row heading. Zero means nothing reads across: an ordinary report, or a table whose settings
	// were re-grouped by hand (a flat list of lines cannot say "these read across the page", so
	// everything it names is the rows' — the same rule AppendSettingsClauses follows).
	size_t RowLevelsFor(const Output& output) const {
		if (GetCurrentGroupDesc().IsOk() || output.m_columnGroups.empty())
			return 0;
		return DimensionCount(output.m_rowGroups);
	}

	// ⭐⭐ THE WHOLE LAYOUT, as one answer (ibTotalsLayout). The count alone could not say it: an
	// output whose ROWS axis is empty and whose COLUMNS carry everything is a legitimate table —
	// "the resources laid out across the page" — and it counts zero row levels, exactly like an
	// ordinary report. `m_hasColumns` is the fact that tells them apart, and it is a fact about the
	// STRUCTURE, not about a number.
	ibTotalsLayout LayoutFor(const Output& output) const {
		ibTotalsLayout layout;
		layout.m_hasColumns  = !GetCurrentGroupDesc().IsOk() && !output.m_columnGroups.empty();
		layout.m_rowLevels   = layout.m_hasColumns ? DimensionCount(output.m_rowGroups) : 0;
		layout.m_detailsAxis = DetailAxisOf(output);
		return layout;
	}

	// (AND THE DETAIL LEVEL DOES NOT MOVE THIS SEAM. It is written LAST in the fold's config and
	//  numbered past the last dimension, so a column key's level means the same whether or not the
	//  rows were read — see ibDataQueryBuilder::TotalsDetails. Where it HANGS is another question,
	//  and it is the fold's: under the last ROW heading, with the columns standing across it.)

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
	// ⭐⭐ DOES THIS COLUMN ANSWER TO THIS PATH — and the two are NOT spelled the same.
	//
	// A path is what a person picked: `Ref.Date`, `Sales.Qty`. The OUTPUT NAME the engine gives it
	// drops a leading source qualifier and CONCATENATES the walk (`ibQueryProposedName`): `RefDate`,
	// `Qty`. Comparing the two as strings therefore misses every dot-walk — and a row whose every
	// cell was blanked is a row the printer does not draw at all, so a detail level printed nothing
	// while its fields were plainly in the list (found live, 2026-08-28).
	//
	// ⚠ BOTH READINGS ARE ACCEPTED because the composer cannot tell here which segment named a
	// source: `Sales.Qty` over a linked package drops `Sales`, while `Ref.Date` over one source
	// keeps `Ref`. Asking the query tier would mean holding its select; accepting both spellings of
	// one name costs nothing and cannot say yes to a different field — the segments are the same
	// words in the same order either way.
	static void AppendFields(std::vector<wxString>& into, const std::vector<wxString>& added) {
		for (const wxString& field : added)
			if (std::find(into.begin(), into.end(), field) == into.end())
				into.push_back(field);
	}
	// ⭐⭐ WHAT IS SHOWN, AND WHERE THE BODIES LIVE.
	//
	// The inheritance rule itself — an empty table inherits, an `Auto` row is WHERE the inherited set
	// lands, a table without one states this node's composition whole and its children inherit THAT —
	// is written in dataComposer.cpp, beside the walk that uses it. This header states only what
	// other tiers ask for (Max, 2026-08-28: a static helper only the composer uses has no business in
	// a header the whole backend and frontend include — every edit to its body costs them all a full
	// rebuild).
	//
	// (SelectedUnder — what is in force UNDER a node, given what was in force above it — is a free
	//  function of the module for the same reason. The chain is CARRIED by whoever walks the tree,
	//  and the only walker is in the module.)

	// The four storeys resolved down to this output: composition (the AUTHOR) → the setting in force
	// (the READER) → output. A node's own table is not here — that is what SelectedUnder answers.
	std::vector<wxString> SelectedFor(const Output& output) const;
	// ⚠ ONE STOREY DOWN FROM THE OUTPUT — right for a node standing on an axis, and NOT a general
	// answer for a node deeper in the tree: that one's chain runs through its parents, and the walk
	// carries it (SelectedUnder). Kept for the callers that hold an output and a top-level node.
	std::vector<wxString> SelectedFor(const Output& output, const GroupNode& level) const;

	// ⭐⭐ WHAT THE READ OWES — every name anything above it will ask for BY NAME, which is NOT the
	// same list as what the report shows. A level hides on a field, orders on a field and selects
	// fields of its own, and each of those is answered off the row already in hand. A name the read
	// did not fetch cannot be answered on.
	//
	// 🛑 AND THE ANSWER IS NOT AN ERROR — IT IS A SILENT YES. That is why nothing showed: a level
	// filter that cannot find its column hides nothing (see ibLevelNodeShows), a sort key missing
	// from the schema orders nothing (LevelOrder), and a field a node selected simply never arrives.
	// The setting stays on screen, saves, travels through variants, and means nothing.
	//
	// (Held back on 2026-08-24 to keep the diagnostic build cheap. The invariant was never in doubt
	//  and stood written beside it: the inner query becomes a derived table and the outer folds over
	//  it, so anything the outer mentions, the inner owes.)
	//
	// ⭐ BOTH AXES, because a level is the same thing on either one — a cross-table's columns hide,
	// order and select exactly as its rows do. A projection that knew only about rows would be a
	// second place to remember when the other axis lands, and it would be remembered late.
	static void AppendFilterFields(std::vector<wxString>& into, const ibFilterNodeDescription& node) {
		if (node.m_kind == ibFilterNodeKind_Group) {
			for (const ibFilterNodeDescription& child : node.m_children)
				AppendFilterFields(into, child);
			return;
		}
		// A SWITCHED-OFF LINE STILL OWES ITS COLUMN. `m_use` is a checkbox on a line that is already
		// written; turning it back on must not need a re-read to start meaning something.
		if (node.m_left.IsField())  AppendFields(into, { node.m_left.m_path });
		if (node.m_right.IsField()) AppendFields(into, { node.m_right.m_path });
	}
	// ONE NODE AND EVERYTHING UNDER IT — what it shows, what it hides on, what it orders by, and the
	// same three of every node beneath. `above` is the set in force where this node stands.
	//
	// 🛑 THE CHILDREN WERE NOT WALKED AT ALL. Only the top level of each axis was read, so a field
	// selected on a nested grouping — or its filter's column, or its sort key — never reached the
	// query. The setting saved, travelled through variants and meant nothing, which is the same
	// silent yes this function's own note warns about one storey up.
	// (CollectProjection — one node and everything under it — and GroupingFieldsOf are free
	//  functions of the module too: they need no object and no caller outside it.)

	// WHAT THE READ OWES — asked by the tests, so it is stated here; the body is in the module.
	std::vector<wxString> ProjectionFor(const Output& output) const;

protected:
	// Always non-empty (see Outputs) — the one output every composition starts with.
	std::vector<Output>      m_outputs = std::vector<Output>(1);
	std::vector<ibResourceDescription> m_resources;   // common to every output
	// THE QUERY'S SELECTS, and what has been said about their fields — a delta, not a copy of the
	// schema. Common to every output, like the resources and for the same reason: a field is one
	// field however many outputs print it.
	std::vector<ibSelectDescription>   m_selects;

	// ⭐ THE SELECTED FIELDS OF THE COMPOSITION — what everything shows unless it says otherwise
	// (Max: set them at the root and they spread over all the tables underneath). An output may
	// override them, and a level inside it may override again; the narrowest statement wins, and
	// where nothing was said the answer comes from here.
	//
	// EMPTY MEANS ALL, as it always did — a composition that selects nothing outputs every column
	// its source has.
	std::vector<ibSelectedFieldDescription> m_commonSelected;
	// See ReadsEveryField — false is the REPORT's answer, and it is the default because a
	// composition that nobody told otherwise is composed by a person, not dictated by a table.
	bool                                    m_readsEveryField = false;

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

	// ⭐⭐ NOT `mutable` ANY MORE, and the methods that fill it stopped claiming `const` (Max,
	// 2026-08-28: heal the mutable). A filter's value travels as a named parameter, so BUILDING the
	// query REGISTERS values — `AddParam`, `BuildFilterAst`, `EnsureAst`, `Execute`. Every one of
	// them was const, and every one of them wrote; `mutable` was not a cache here, it was the cover
	// over a signature that said the opposite of what the code did.
	std::map<wxString, ibValue> m_params;   // the rendered query's own values — filled while a query is being made
	int                         m_autoParam = 0;   // auto-name counter for filter values (see m_params)

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
	// ⭐ ONE OUTPUT OR SEVERAL — the same render either way. Several are written as BRANCHES of one
	// query, so what a report with three tables sends to the engine is one text, and what a list
	// sends is that text with a single ladder in it. There is no second renderer for the shared case.
	wxString RenderTextFor(const std::vector<const Output*>& outputs) const;
	wxString RenderTextFor(const Output& output) const { return RenderTextFor(std::vector<const Output*>{ &output }); }

	// ⭐⭐ A SETTING REACHES THE READ BY ONE OF TWO ROADS, and this is the second one. A sort and the
	// composition's scope are WRITTEN INTO THE TEXT (AppendSettingsClauses); a FILTER is built as an
	// expression and ANDed into the parsed query — never rendered, never re-parsed, because a tree
	// condition put through text and back is a condition retyped by a machine.
	//
	// 🛑 SO A NEW WAY OF READING MUST TAKE BOTH. The shared read rendered the text, parsed it, and
	// stopped — and the report's own filter, the one a person sets at the very top and by which
	// "everything below reads only this", quietly did nothing (Max, live, 2026-08-27). Written here
	// once, so the next road cannot take one half and miss the other.
	static void AndWhere(ibQuerySelect& ast, const ibQueryAstExprPtr& condition);

	// Read one output — see the base declaration. The first output rides the cached parse (a list
	// re-reads it on every page); any other renders and parses on the spot.
	//
	// One fold and the ending — a table's column totals come out of the same tree now (see the body).
	bool RunOutput(const Output& output, ibCompositionDriver& driver) override;

	// ONE FOLD OF ONE OUTPUT, handed to the driver in the node language.
	bool RunOutputPass(const Output& output, ibCompositionDriver& driver, bool& hasTotalsOut);

	// Execute for ONE output: the cached parse for the first, a fresh render + parse for any other.
	// `serverGrouped` — see the Execute overload that answers it: the fact belongs to THIS read.
	ibDataQueryResult ExecuteFor(const Output& output, std::vector<ibQueryLowering::OutputColumn>& schema,
		bool& hasTotals, const ibReadPageRequest& page, bool& serverGrouped);

private:
	// ⭐⭐ WHICH OUTPUTS CAN BE READ TOGETHER — the question that decides whether a composition costs
	// one query or N.
	//
	// They share a read when they differ only in HOW THEY FOLD it: same source, same WHERE, same
	// ORDER BY. So an output with a filter of its own is out (its filter is ANDed into the text and
	// would narrow everyone else's rows), and so is one with a sort of its own by a FIELD (one read
	// has one order). What is left — outputs that state only their groupings — is the ordinary case,
	// and it is exactly what a report with several tables in it looks like.
	//
	// ⚠ AND IT NEEDS A NAME TO BE ADDRESSED BY. A branch is walked by the name it was given
	// (`SPLIT … ONTO <name>`), so an output whose name is not a plain identifier reads alone rather
	// than being silently renamed into something the query can spell.
	std::vector<const Output*> BranchableOutputs() const;
	// ⭐⭐ THE NAME A BRANCH IS ADDRESSED BY — the output's own, or one made up from its POSITION.
	//
	// 🛑 REQUIRING A STORED NAME QUIETLY DISABLED THE WHOLE THING. Outputs saved before they were
	// ever named carry an empty one (seen live 2026-08-27: `output ''` beside `output 'Output2'` in
	// the journal), so the shared read was refused and every composition went on reading once per
	// output — the mechanism built, collected and never engaged.
	//
	// A branch needs a name only to be ASKED FOR, and the asking happens twice in one run: the
	// render writes it, the walk repeats it. So an unnamed output gets `Output<n>` derived from where
	// it stands, and both sides derive it the same way — nothing is stored, nothing is migrated, and
	// a report saved last year folds like one made today.
	wxString BranchNameFor(const Output& output, size_t at) const;
	// …and where it stands among the shared read's branches, which is what that name is made from.
	size_t BranchIndexOf(const Output& output) const;
	// The read this output should walk — built on first ask, null when it reads for itself.
	ibDataQueryResult* SharedReadFor(const Output& output,
		std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals);
	// …and released when the last branch has been served.
	void ReleaseSharedRead(const Output& output);
	// Is this output one of the branches of the shared read? (Null / empty share = nobody is.)
	bool ReadsAsBranch(const Output& output) const;
	// ⭐ THE COLUMNS THIS OUTPUT ACTUALLY SHOWS, out of the shared read's schema. One read publishes
	// every branch's keys; a branch prints its OWN — otherwise each table on the sheet would carry an
	// empty column for every heading of its neighbours.
	std::vector<ibQueryLowering::OutputColumn> SchemaFor(const Output& output,
		const std::vector<ibQueryLowering::OutputColumn>& shared) const;

	// ⭐ THE SHARED READ, BUILT BY WHOEVER NEEDS IT FIRST and released by the last branch that used
	// it. There is no "the run is starting" hook and there does not need to be one: the outputs are
	// read in order, so the first branch to ask builds it and the last to be served lets it go.
	// (Max, 2026-08-27: the driver already has the events for an output beginning and ending — a
	// second set of brackets on the composer would say the same thing again.)
	//
	// ⚠ NOT `mutable`, and the const went off ExecuteFor instead. Building a read CHANGES this
	// composer — it opens a cursor and remembers it — so the honest thing is to say so in the
	// signature. `mutable` would have kept a const method that quietly does not behave like one,
	// which is a lie told to the compiler to avoid rewording one declaration.
	std::shared_ptr<ibDataQueryResult>         m_sharedRead;
	std::vector<ibQueryLowering::OutputColumn> m_sharedSchema;
	std::vector<const Output*>                 m_sharedBranches;
	size_t                                     m_branchesServed = 0;

	// WHERE / ORDER BY / TOTALS — appended the same way over a composed source and over an author's
	// query, because they ARE the same settings.
	// ONE LIST, however many there are: a single output writes its levels as it always did, and
	// several write `BY SPLIT … ONTO <name>` apiece. The settings that are not a ladder — the scope
	// filter, the order — come off the first, which is sound precisely because outputs share a read
	// only when they agree about them.
	void AppendSettingsClauses(wxString& text, const std::vector<const Output*>& outputs) const;

	// DOES THE LEVEL AT `depth` SHOW THIS HEADING? `depth` is the walk's own (1 = the first
	// grouping), and only THAT level's filter is asked — the ones above have already had their say.
	//
	// This is the display half of a filter, and the reason a level's filter never reaches the WHERE:
	// a heading nobody wants to look at (an empty recorder, an opening balance) is hidden, while its
	// rows stay in every total above it. A depth with no level of its own shows everything.
	//
	// ⚠ AND THE NODE'S KIND TRAVELS WITH THE DEPTH: a RECORD's level is not found by counting, since
	// it writes no key and has no depth of its own (see LevelAt).
	bool LevelShows(const Output& output, int depth, ibSelectorNodeKind kind,
		const std::vector<ibQueryLowering::OutputColumn>& schema, const std::vector<ibValue>& row) const;

	// …AND IN WHAT ORDER DOES IT HAND THEM OVER? The twin of LevelShows, and the half a level's
	// settings carried without anybody reading it: the window writes a sort onto the node a person
	// selected, and the walk went on visiting headings in fold order.
	//
	// The two are twins for a reason — both are the DISPLAY half of a setting. A level's filter does
	// not reach the WHERE and a level's sort does not reach the ORDER BY, because both are about how
	// one level is presented while the figures above it stay computed from every row.
	std::vector<ibSelectorSort> LevelOrder(const Output& output, int depth,
		const std::vector<ibQueryLowering::OutputColumn>& schema) const;

public:

	// Render -> parse -> lower -> run. Fills the output schema; `hasTotals` reports a folded (TOTALS) result.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals);

	// The PAGED read — the driver's envelope threaded into the lowering's paged terminal.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
	                          const ibReadPageRequest& page);

	// ⭐⭐ …AND THE SAME READ THAT ALSO SAYS WHETHER THE DBMS DID THE GROUPING (Max, 2026-08-28: heal
	// the mutable). That fact used to travel in a `mutable bool` written by this const method and
	// read by the walk afterwards — a RETURN VALUE smuggled through a member. It is not a cache: it
	// is one more thing this call answers, and `schema` and `hasTotals` were already answered the
	// honest way, one line up.
	//
	// ⚠ A member made the answer OUTLIVE the question. Whoever read it later got the last read's
	// verdict whether or not it was about their output — the two overloads above forward here with a
	// local, so nothing keeps it after the caller has used it.
	ibDataQueryResult Execute(std::vector<ibQueryLowering::OutputColumn>& schema, bool& hasTotals,
	                          const ibReadPageRequest& page, bool& serverGrouped);

	// The full cycle into the given driver.
	bool Run(ibCompositionDriver& driver) override;

private:
	// The composer owns its CACHES — the consumer stays dumb:
	//  * the parse: one AST per rendered text;
	//  * the page render (Lever 1): the door's build-once SQL keyed by the signature.
	// ⚠ NOT const, and that is the point: it REGISTERS the parameter values the query will bind
	// (AddParam). A const method that writes is a const method that lied, and `mutable` was the cover.
	void EnsureAst();
	bool BuildPageSignature(const ibReadPageRequest& page, wxString& signature) const;

	// ⭐⭐ THE AUTHOR'S TEXT, SPLIT INTO WHAT PREPARES AND WHAT IS READ — the seam that lets a
	// composition stand over a PACKAGE (docs/query-language-arc.md § 24.4b).
	//
	// One query is read as a nested source, as it always was: `FROM (<the text>) AS AuthorQuery`.
	// A package is not — a `;` inside brackets is not a query — so its statements stay AHEAD of the
	// composer's own select, and what that select reads FROM is:
	//
	//   * the selections the `LINK` section relates, joined as it says, when there is one;
	//   * else the LAST statement as a nested source — the one that produces the result, which is
	//     already what the field list offers a person to pick from (ibQueryFieldsOfText).
	//
	// So the composer goes on writing TEXT and nothing below it learns a new road: what it writes
	// is simply a package whose last statement is the one carrying the settings.
	void SplitSourceText() const;

	// The SELECT list when the output has chosen no fields — see the definition. A list reads
	// everything; a report reads what it folds by.
	wxString WhenNothingChosen(const Output& output) const;

	// ⭐⭐ RUN WHAT PREPARES — the package's `INTO` statements, once per source text, into a store
	// this composer owns for the whole of that text's life.
	//
	// Max, 2026-08-27: *"INTO is not in the link and should not be — INTO is for the ONTO
	// selections, they can use it there."* The two words are not rivals: a `LINK` relates NAMES, and
	// the selections it names may read a table an earlier statement MADE. So the tables have to be
	// standing before anything is lowered — a named selection is declared to the server as `WITH`,
	// and what is inside it resolves then.
	//
	// ONCE PER TEXT, not per fetch or per output: a report reads several outputs and a list pages,
	// and rebuilding the tables under each of them would be the same work again for the same rows —
	// and, worse, would pull the ground out from under a shared read that is still open. The signal
	// that a run is starting is `FromText`, which every compose calls.
	void EnsureTempTables() const;

	// TEXT -> THE STATEMENT THAT CARRIES THE SETTINGS, with the package's own named results beside
	// it. All three execution roads write text the same way, so they read it back the same way too.
	// `package` owns the statements `named` points into — both must outlive the lowering call.
	ibQuerySelectPtr ParseComposed(const wxString& text, ibQueryPackage& package,
	                               std::map<wxString, const ibQuerySelect*>& named) const;

	// ⭐⭐ THREE CACHES, AND EACH CARRIES ITS OWN KEY (Max, 2026-08-28: *"a great many mutables, which
	// is worrying"*). There were SEVENTEEN `mutable` members here, and they were not seventeen
	// decisions — they were these three answers, each of which had arrived field by field: its
	// storage, its key, and its "already computed" flag, all separate.
	//
	// What that cost is not memory but INVALIDATION: forgetting stopped being possible only if you
	// remembered five names. `SplitSourceText` cleared five fields by hand, and the day one more was
	// added it had to be remembered again — which is a rule that lives in a person's head.
	//
	// Now each answer is ONE object holding the key it was computed from. "Is it stale" is a
	// comparison with that key; "throw it away" is an assignment. There are no separate fields left
	// to forget.

	// EVERYTHING A SOURCE TEXT RESOLVES TO — worked out once per text (SplitSourceText).
	struct SourceResolution
	{
		wxString       m_ofText;      // THE KEY: the text this was worked out from, and nothing else
		ibQueryPackage m_package;     // …parsed, which is where the preparing statements are found
		wxString       m_preamble;    // the statements that prepare (empty for a plain query)
		wxString       m_from;        // what the composer's own SELECT reads FROM
		// ⭐ WHAT IT SELECTS WHEN NOBODY HAS CHOSEN ANYTHING. Empty means `*`, which is right over ONE
		// source and wrong over a JOIN: a star there publishes both sides' columns under their BARE
		// names, so two `Attribute2` answer to one name and a read by name lands on whichever came
		// first. The engine's own package assembly refuses to rely on it and qualifies instead
		// (ExecutePackage); so does this. Filled on the link road only.
		wxString       m_allFields;
	};
	mutable SourceResolution m_resolved;

	// WHAT THE SETTINGS RENDERED TO, and what it was rendered FROM.
	//
	// ⭐ THE KEY IS THE TEXT **AND** THE FILTER, and not "the variant" — there is no such thing (Max,
	// 2026-08-24). Everything a setting decides EXCEPT the filter ends up in the text, so the text
	// answers for it; a filter TREE never becomes text — it is ANDed into the parsed WHERE — so the
	// text alone would say "nothing changed" after somebody rewrote the whole filter.
	struct RenderedQuery
	{
		wxString            m_text;
		ibFilterDescription m_filter;
		// The LAST statement of what this composer wrote — the one carrying the settings — plus the
		// package that holds the statements before it (which is what keeps the selects `m_named`
		// points at alive) and the scope the lowering resolves `FROM Sales` through.
		ibQuerySelectPtr    m_ast;
		ibQueryPackage      m_package;
		std::map<wxString, const ibQuerySelect*> m_named;
	};
	mutable RenderedQuery m_rendered;

	// THE TABLES THE PREPARING STATEMENTS MADE, and the registry the lowering resolves names
	// through: this composer's own transient sources PLUS those tables. One map, because a scope
	// REPLACES the one under it — two nested ibTempSourceScopes would hide the composer's own.
	//
	// ⚠ ITS KEY IS THE RUN, not a text: rows read a minute ago are not an answer to a report being
	// asked again now, so `FromText` — which every compose calls — lets them go.
	struct PreparedTables
	{
		bool                  m_ready = false;
		ibQueryTempTableStore m_store;
		std::map<wxString, const ibBackendQueryable*> m_sources;

		// ⭐ ONE ACT, ONE NAME. "Let go of what was prepared" was written out as three lines in four
		// places — and three lines repeated four times is a verb nobody named, which is how the day
		// comes that one of the four is missing a line. (The store cannot be assigned over: it OWNS
		// its tables and is deliberately non-copyable, so this is a method rather than `= {}`.)
		void Forget() {
			m_store.Close();
			m_sources.clear();
			m_ready = false;
		}
	};
	mutable PreparedTables m_prepared;

	mutable std::shared_ptr<ibRenderedPageCache> m_pageCache;
	// Set by Execute when a TOTALS fetch took the server-side single-level GROUP-BY keyset page (not the detail
	// read + fold): Run then emits the flat groups at level 1 without ByGroups. (docs: group-level paging)
	// (⛔ `m_serverGroupedLevel` STOOD HERE — a `mutable bool` that a const Execute wrote and the walk
	//  read afterwards. It was never a cache: it was this read's SECOND ANSWER, and a member is
	//  where an answer goes to outlive its question. It is returned now.)
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
