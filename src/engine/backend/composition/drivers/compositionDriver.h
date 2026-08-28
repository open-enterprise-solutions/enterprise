#ifndef __COMPOSITION_DRIVER_H__
#define __COMPOSITION_DRIVER_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT A COMPOSITION HANDS ITS DRIVER — the contract, and only that.
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ CUT OUT OF dataComposer.h ON 2026-08-28, and the reason is the include list of everything that
// draws. A driver — the spreadsheet, the list's fetch, a chart — implements a hundred lines of
// interface and needs to know nothing about how a composition renders text, resolves its selected
// fields or walks a tree. It was taking the whole fifteen-hundred-line header to get them, so every
// edit to the composer rebuilt every drawer of its results.
//
// A driver includes THIS. The composer includes it too, and adds itself.

#include "backend/backend_core.h"
#include "backend/compiler/value.h"           // ibValue — the row a driver is handed
#include "backend/query/queryLowering.h"      // ibQueryLowering::OutputColumn — the schema of an output
#include "backend/compositionDescription.h"   // ibCompositionOutputKind — what an output IS

#include <wx/string.h>
#include <vector>

// ⚠ A CHART IS NOT A THIRD SHAPE. It reads exactly what a cross-table reads — series along one
// axis, points along the other, a resource where they meet — and differs in being DRAWN as a
// picture. Drawing is the driver's business, so a chart is an output with a chart driver, not a
// kind of its own; adding one here would be a name for a difference that lives elsewhere.
// (ibCompositionOutputKind MOVED to compositionDescription.h on 2026-08-25, for the same reason the
//  level kind moved: it stopped being read off the content and became something a person DECIDES —
//  "add grouping" or "add table" — so it is part of the stored shape and the composer takes it from
//  there. A table just added is empty on both axes and is a table all the same.)

// (ibCompositionLevelKind MOVED to compositionDescription.h — a level's kind is part of what a level
//  IS, so it lives with the stored shape and the composer takes it from there. It used to be declared
//  here with the description holding "the same value as a plain number" beside it: a twin vocabulary,
//  and the description now states it as the type it is.)

// WHICH WAY A DIMENSION READS. Down the page or across it — the only thing a cross-table's printer
// needs that a grouping's printer does not, and the one thing the schema cannot say on its own: L4
// hands back a dimension's DEPTH (`m_dimLevel`), and depth alone cannot tell the third row heading
// from the first column heading.
enum class ibCompositionAxis
{
	None,      // not a dimension at all — a measure or a detail field
	Rows,      // a heading down the page
	Columns,   // a heading across it
};

// What an output tells its driver before its first row: what shape is coming and what the values
// mean. The schema is the output's OWN — two outputs of one composition show different fields.
struct ibCompositionOutputInfo
{
	ibCompositionOutputKind                    m_kind = ibCompositionOutputKind::Grouping;
	std::vector<ibQueryLowering::OutputColumn> m_schema;
	wxString                                   m_name;   // what the output is called, when it is

	// ⭐⭐ WHAT EACH COLUMN IS CALLED, one entry per schema column. The QUERY names its columns so
	// they can be read back — uniquely, one word, sometimes qualified to stay apart (`CountNumber`
	// where a level already answers to `Number`). A REPORT is read by a person, and this is what
	// they see over the column.
	//
	// Filled by the composition, because a title is the composition's own entity: it comes from the
	// FIELD a column stands for (ibCompositionDescription::TitleForPath), and a resource reaches its
	// field through the path it aggregates — `COUNT(Number)` is titled by `Number`, function and all
	// qualification left out of it (Max, 2026-08-26).
	std::vector<wxString>                      m_titles;

	// …AND IT IS ASKED, not indexed into — a schema and a list beside it can always disagree about
	// length, and the answer where they do is the column's own name.
	wxString TitleOf(size_t column) const {
		if (column < m_titles.size() && !m_titles[column].IsEmpty())
			return m_titles[column];
		return column < m_schema.size() ? m_schema[column].m_name : wxString();
	}

	// HOW MANY OF THE DIMENSIONS ARE THE ROWS'. Both axes fold in one `TOTALS BY`, rows first (see
	// AppendSettingsClauses), so this is the seam between them and nothing else marks it.
	size_t                                     m_rowLevels = 0;

	// (⚠ AND THERE IS NO "WHICH PASS IS THIS" ANY MORE. A cross-table used to be folded twice and the
	//  driver had to be told which fold it was hearing. The column totals are the cells of the
	//  heading over everything, and the fold builds them with the rest — see RunOutput.)

	// ⭐ WHICH WAY THE DETAIL RECORDS READ. Down the page each record is a LINE of the table; across
	// it each record is a COLUMN of its own (Max, 2026-08-26: "exactly as a detail record is in the
	// rows, so in the columns"). A printer cannot tell from the node — a record says it is a record,
	// not where it belongs — and the depth cannot say it either, so the output says it.
	ibTotalsAxis                               m_detailsAxis = ibTotalsAxis::Rows;

	// ⭐ AND IT IS ASKED, NOT PUBLISHED AS A NUMBER TO COMPARE AGAINST. The driver's question is
	// "which axis is this column?", so that is what it gets — handing it the seam and letting every
	// printer write `column.m_level < info.m_rowLevels` is the same rule spelled out in as many
	// places as there are drivers, and it is wrong in all of them the day a third axis exists.
	ibCompositionAxis AxisOf(const ibQueryLowering::OutputColumn& column) const {
		if (column.m_role != ibQueryLowering::ibColumnRole::Dimension || column.m_level < 0)
			return ibCompositionAxis::None;
		return (static_cast<size_t>(column.m_level) < m_rowLevels)
			? ibCompositionAxis::Rows : ibCompositionAxis::Columns;
	}
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

	// ⭐⭐ THE VOCABULARY, IN THE ORDER IT IS SPOKEN. An OUTPUT begins and ends, a GROUP begins and
	// ends, and between them rows and columns are written. Declared in that order too, so reading the
	// class is reading one sentence:
	//
	//     OnOutputBegin(info)                     an output starts — its kind, its schema, its name
	//       OnGroupBegin(level, kind, …)          a heading OPENS — a grouping, or a fork of the totals
	//         OnRow(level, values)                a row is written — DOWN the page
	//         OnColumn(level, kind, values)       a column is written — ACROSS it (a cross-table)
	//       OnGroupEnd(level, values)             …and the heading CLOSES, its figures final
	//     OnOutputEnd(totals)                     the output is finished
	//
	// ⭐ A HEADING IS A PAIR, not a row that happens to be bold. It opens, things are written under
	// it, and it closes — the only reading under which a total may be printed where a reader looks
	// for it (at the bottom) without anybody stashing it in a field between two events.
	//
	// ⭐ ROW AND COLUMN ARE TWO VERBS, and that is earned rather than habitual: a row is written down
	// the page and a column across it — different coordinates, different widths, a different act. The
	// AXIS is a fact the WALK holds (a level says which way it reads), so it is handed over rather
	// than re-derived by every driver out of a depth and a count it was told separately.
	//
	// 🛑 WHAT THIS REPLACED, and why: `OnGroup` / `OnDetail` were a base verb (`OnRow`) with two
	// richer twins, and what a heading carried over a record was its KIND — which is a TYPE, not a
	// second function. Two verbs meant every driver answered one question twice, and the day a third
	// kind arrived (a BRANCH, once the totals could fork) both would have had to learn it. Meanwhile
	// `OnOutputBegin` … `OnComplete` was not a pair at all — one name says "an output is starting",
	// the other "something finished" — and nothing said a GROUP had ended, which is exactly the event
	// the grand total needed. (Max, 2026-08-27: a detail record and a row are the same thing — what a
	// grouping adds is a KIND; and the events should read as "on handling a row", "on handling a
	// column".)
	//
	// ⚠ THE TEST FOR ADDING TO THIS VOCABULARY, since it is easy to over-apply: a verb earns its
	// place when it CARRIES MORE — an opening rather than a closing, a column rather than a row. When
	// two carry the same, one of them is a synonym, and a synonym in a virtual is a second thing to
	// keep in step for nothing. (`OnColumns` went that way: "which columns" is part of "an output is
	// starting", and the schema rides on the info.)

	// An output STARTS — its schema, its kind and its name arrive with it.
	virtual void OnOutputBegin(const ibCompositionOutputInfo& info) = 0;

	// ⭐⭐ A HEADING OPENS. `values` follow the schema order — the level's key fields, with the
	// resources rolled in place.
	//
	//   * `kind`  — what this heading IS: a GROUPING, or a BRANCH of the totals (`SPLIT`), which
	//               groups the OUTPUT rather than the values. The node says it; a printer must never
	//               infer it from the depth, which cannot answer once a tree holds both.
	//   * `hasChildren`      — the FOLD's fact: does this node stand over anything at all. That is
	//                          what makes a heading a heading, and the root the grand total.
	//   * `showsWhatIsUnder` — the OUTPUT's promise: will what is under it actually be printed. An
	//                          expander may be offered only on this one, because a triangle opening
	//                          onto nothing is worse than no triangle.
	//
	// ⚠ THE LAST TWO ARE NOT ONE QUESTION, and one bool answering both is how the innermost heading
	// of a printed report once came out looking like a detail line: a deepest heading over records,
	// in an output that declares no detail level, HAS children and SHOWS nothing. Each consumer takes
	// the one it means — a list the second, a printed report the first.
	virtual void OnGroupBegin(int level, ibSelectorNodeKind kind, bool hasChildren,
	                          bool showsWhatIsUnder, const std::vector<ibValue>& values) = 0;

	// A ROW — a detail record, written down the page under whatever opened above it. It carries no
	// kind: a row is a row, and what makes a heading different is that it is a PAIR.
	virtual void OnRow(int level, const std::vector<ibValue>& values) = 0;

	// A COLUMN of a cross-table — a heading that reads ACROSS the page, or a record laid out as a
	// column of its own. Default: nothing, because a driver that draws no table has nowhere across to
	// write (a list is rows, and rows only).
	virtual void OnColumn(int /*level*/, ibSelectorNodeKind /*kind*/, const std::vector<ibValue>& /*values*/) {}

	// A HEADING CLOSES — every row and column under it has been written, so its figures are final and
	// its section can be ended. This is where a total that belongs at the BOTTOM goes. Default:
	// nothing, for the readers that draw a heading as it opens and never look back.
	virtual void OnGroupEnd(int /*level*/, const std::vector<ibValue>& /*values*/) {}

	// The output is FINISHED. `totals` — the result was a folded TOTALS tree.
	virtual void OnOutputEnd(bool /*totals*/) {}

	// =====================================================================================
	// WHAT THIS DRIVER ASKS OF THE WALK — not events, and kept apart from them on purpose.
	//
	// Everything above is the walk TELLING the driver what it just wrote. Everything here is the
	// driver telling the walk what to hand it in the first place: a page rather than everything, the
	// grand total or not. They read as one list only if you do not look — one group is called when
	// something happened, the other is asked BEFORE anything does (Max, 2026-08-27: WantsGrandTotal
	// reads as a FLAG — "what is supported" — rather than as an event).
	// =====================================================================================

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

	// (⚠ AND NO `WantsColumnTotals`. It used to buy a SECOND FOLD of the whole output, on the
	//  argument that "one tree cannot hold both" sets of subtotals — the rows' and the columns'.
	//  One CHAIN cannot; a tree whose every heading carries its own column branch holds both, and
	//  the column totals are simply the branch under the root. The figures are still never computed
	//  FROM the cells — an average of averages is not the average — they are folded, like all the
	//  others, from the rows.)

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

#endif
