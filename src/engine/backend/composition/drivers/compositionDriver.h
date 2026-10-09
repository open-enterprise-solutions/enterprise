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

#include <wx/colour.h>   // ibCompositionAttr — what a cell is drawn with
#include <wx/font.h>
#include <wx/string.h>
#include <map>        // ibCompositionRowAttr — a kept row's, by column id
#include <memory>     // ibCompositionAttr::m_format — one parsed Format, shared by every cell it is laid on
#include <optional>
#include <utility>
#include <vector>

struct ibCompositionTheme;   // compositionTheme.h — only a driver that paints has to know what a palette holds
class ibFormatString;        // formatString.h — a Format, handed over parsed (ibCompositionAttr::m_format)

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
	// …and HOW MANY OUTPUTS THE RUN DRAWS, this one among them. A caption tells one block from the others,
	// and a run of one has nothing to tell apart (ibSpreadsheetComposeDriver::WriteOutputCaption).
	size_t                                     m_outputCount = 1;
	// ⭐ …AND THE PALETTE IT IS PAINTED IN — the output's own theme, else the setting's (ibOutputParameter::Theme).
	// Its header, its grid and every line whose node names no theme of its own. Null = the platform's first.
	const ibCompositionTheme*                  m_theme = nullptr;

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

	// ⭐⭐ AND THE FIELD ITSELF — the path each column is a reading of, one entry per schema column.
	//
	// The title above is computed FROM this path and handed over alone, which is enough for anything
	// that only PRINTS the column. A reader that has to act on the field — a details parameter,
	// which becomes a grouping and a filter line — needs the path, and both are written with paths
	// (ibGroupDescription::Append, ibFilterDescription::Append). Handed only the title it would have
	// to hunt the path back through the resources and the levels: the same answer computed twice, by
	// the tier that knows it least.
	//
	// Empty where a column stands for no field of the composition — a projected column, or one whose
	// level a user's own grouping replaced. Empty means "cannot say", and a reader treats it as a
	// cell with nothing to follow rather than guessing a path out of the name.
	std::vector<wxString>                      m_paths;

	// …ASKED, like the title, and for the same reason.
	wxString PathOf(size_t column) const {
		return column < m_paths.size() ? m_paths[column] : wxString();
	}

	// ⭐ AND HOW IT IS SHOWN — the appearance of the field the column reads (ibAppearanceDescription), one
	// entry per schema column; an empty one = the value shows itself its own way. Worked out with the
	// title, from the same field (AppearanceForPath).
	std::vector<ibAppearanceDescription>       m_appearances;
	ibAppearanceDescription AppearanceOf(size_t column) const {
		return column < m_appearances.size() ? m_appearances[column] : ibAppearanceDescription();
	}

	// ⭐⭐ AND WHETHER THE COLUMN IS SHOWN AT ALL — one entry per schema column.
	//
	// Reading and showing are two questions over one query: the read fetches everything ANY node
	// needs, the fields a filter and a sort stand on included, and what is PRINTED is what somebody
	// chose. Without this the driver laid a column out for every column of the schema, so a report
	// that selected nothing still grew headed, empty columns for the fields it merely filtered by
	// (Max, 2026-08-29: "I have already set a user setting where it is empty — it must blank the
	// columns even if they were there").
	//
	// A DIMENSION and a MEASURE are always shown — a heading prints its key and a fold prints its
	// figure — so only projected fields are ever false here.
	std::vector<bool>                          m_shown;

	bool ShowsColumn(size_t column) const {
		return column >= m_shown.size() || m_shown[column];
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
// ⭐⭐ WHAT A LINE IS — said once, as a thing, rather than as six arguments in a row. The list had
// grown to where the reader counts commas to find out which `int` is which, and two of them were
// numbers of the same kind standing next to each other — which is exactly how they came to be added
// together at the callsite and could never be told apart again.
//
//   * `m_level`  — the RUNG of the ladder: WHICH grouping this line carries. That is what says whose
//                  key belongs in which column, and what the settings and the sort are asked by.
//   * `m_indent` — how far INSIDE that rung it stands. A hierarchy recurses within one level, so a
//                  sub-folder is the same rung one step further in; everything else is 0.
//   * `Page()`   — where the line is DRAWN, which is the two added. A printer indents and folds by
//                  this; nothing else may.
//
// 🛑 Handed the sum alone, a printer answers both questions from it and gets both wrong as soon as a
// tree goes deeper than one step: a nested folder arrives as "level 2" where a single dimension
// exists, so its key is written to no column at all, and the sheet folds it INTO the line above as
// though it were the next grouping (Max, 2026-08-29, live: the nested element with an empty
// reference, and row 5 counted as a group).
// ⭐ A FONT SAID PART BY PART — what a rule's font changes of the ORDINARY font it was chosen from (the report's own,
// ibDefaultSpreadsheetFont: the font window and the MCP words both start from it), and nothing else. It is laid OVER
// the font the cell already has (Over), so a bold heading made italic stays bold and a report keeps its size (Max,
// 2026-09-30, the run: a whole font took the headings' bold and turned 8pt into the system's 9). The flags are said
// only as ON — a rule makes a line bold or italic, it does not take a heading's bold away.
struct ibCompositionFont {
	int         m_pointSize = 0;                   // 0 = not said
	int         m_weight = 0;                      // numeric, 700 = bold; 0 = not said
	wxFontStyle m_style = wxFONTSTYLE_MAX;         // MAX = not said
	wxString    m_face;                            // empty = not said
	bool        m_underlined = false;
	bool        m_strikethrough = false;

	// WHAT `font` SAYS beyond `ordinary` — made once, when a rule is made ready.
	static ibCompositionFont Of(const wxFont& font, const wxFont& ordinary) {
		ibCompositionFont said;
		if (!font.IsOk())
			return said;
		if (font.GetPointSize() != ordinary.GetPointSize())
			said.m_pointSize = font.GetPointSize();
		if (font.GetNumericWeight() != ordinary.GetNumericWeight())
			said.m_weight = font.GetNumericWeight();
		if (font.GetStyle() != ordinary.GetStyle())
			said.m_style = font.GetStyle();
		if (!font.GetFaceName().IsSameAs(ordinary.GetFaceName(), false))
			said.m_face = font.GetFaceName();
		said.m_underlined = font.GetUnderlined() && !ordinary.GetUnderlined();
		said.m_strikethrough = font.GetStrikethrough() && !ordinary.GetStrikethrough();
		return said;
	}

	bool IsSaid() const {
		return m_pointSize > 0 || m_weight > 0 || m_style != wxFONTSTYLE_MAX || !m_face.IsEmpty()
			|| m_underlined || m_strikethrough;
	}
	// …read as the two flags a grid and a word have: bold (semibold and heavier), italic (or slanted).
	bool IsBold() const { return m_weight >= wxFONTWEIGHT_SEMIBOLD; }
	bool IsItalic() const { return m_style == wxFONTSTYLE_ITALIC || m_style == wxFONTSTYLE_SLANT; }
	// …each part another says replaces this one's, the rest stands — a bold rule and an italic one make bold italic.
	void Say(const ibCompositionFont& other) {
		if (other.m_pointSize > 0)                 m_pointSize = other.m_pointSize;
		if (other.m_weight > 0)                    m_weight = other.m_weight;
		if (other.m_style != wxFONTSTYLE_MAX)      m_style = other.m_style;
		if (!other.m_face.IsEmpty())               m_face = other.m_face;
		m_underlined = m_underlined || other.m_underlined;
		m_strikethrough = m_strikethrough || other.m_strikethrough;
	}
	// THE CELL'S FONT with what is said laid on it.
	wxFont Over(wxFont font) const {
		if (m_pointSize > 0)                 font.SetPointSize(m_pointSize);
		if (m_weight > 0)                    font.SetNumericWeight(m_weight);
		if (m_style != wxFONTSTYLE_MAX)      font.SetStyle(m_style);
		if (!m_face.IsEmpty())               font.SetFaceName(m_face);
		if (m_underlined)                    font.SetUnderlined(true);
		if (m_strikethrough)                 font.SetStrikethrough(true);
		return font;
	}
};

// ⭐⭐ WHAT A COMPOSITION DRAWS A CELL WITH — its conditional appearance made into values, in the driver contract's
// own words and nobody else's (Max, 2026-09-30: "the drivers work with a structure of their own, and the list simply
// moves it"). EACH DRIVER TAKES IT IN ITS OWN TERMS — a list into its own row, a report onto its sheet's cells — and
// what a driver makes of it is that driver's business, not the contract's. A report cell may come to want more — a
// border, an indent — and that is added HERE, where every driver reads it. Unset — not IsOk, wxALIGN_INVALID, no
// text, no format — is the driver's own.
struct BACKEND_API ibCompositionAttr {
	wxColour m_backgroundColour;
	wxColour m_textColour;
	ibCompositionFont m_font;                           // said part by part, over the cell's own (Over)
	int      m_horizontalAlignment = wxALIGN_INVALID;   // wxALIGN_LEFT / wxALIGN_CENTER_HORIZONTAL / wxALIGN_RIGHT
	std::optional<wxString> m_text;                    // what the cell says in place of its value — an empty one too
	// …else the Format its value is written in, instead of its column's — parsed once, when the rule was made ready
	// (ibCompositionRulesOf), and shared by every cell it is laid on; the text a driver writes with it anyway.
	std::shared_ptr<const ibFormatString> m_format;

	bool IsDefault() const {
		return !m_backgroundColour.IsOk() && !m_textColour.IsOk() && !m_font.IsSaid()
			&& m_horizontalAlignment == wxALIGN_INVALID && !m_text && !m_format;
	}

	// …AND EVERYTHING ANOTHER SAYS, said over this — each thing `other` sets replaces this one's, the rest stands: how
	// the rules that hold on a line lay over one another (the door ibParameterValuesDescription::Say is for a setting).
	void Say(const ibCompositionAttr& other) {
		if (other.m_backgroundColour.IsOk())
			m_backgroundColour = other.m_backgroundColour;
		if (other.m_textColour.IsOk())
			m_textColour = other.m_textColour;
		m_font.Say(other.m_font);
		if (other.m_horizontalAlignment != wxALIGN_INVALID)
			m_horizontalAlignment = other.m_horizontalAlignment;
		if (other.m_text)
			m_text = other.m_text;
		if (other.m_format)
			m_format = other.m_format;
	}

	// …AND WHAT A CELL SAYS UNDER IT — its Text where one is in force, else `value` in its Format. False where neither
	// is: the cell says its value as its own column writes it (the driver's own format).
	bool TextOf(const ibValue& value, wxString& text) const;
};

// …AS A DRIVER KEEPS A ROW OF THEM — the row's own and each column's under the id the reader files the column's value
// under (a list: its fetch driver's row, then its node, the one moved into the other as it is). Held by pointer, null
// where no rule held: a row with no conditional appearance carries nothing for it.
struct ibCompositionRowAttr {
	ibCompositionAttr                     m_line;
	std::map<ibMetaID, ibCompositionAttr> m_columns;
};

// …A LINE OF THEM — the line's own, and those of the columns a rule of their own held on. The walk makes them per line
// (ibDataComposer::ConditionalAppearance::AttrFor, compositionCondition.cpp) in a buffer it keeps, so a line allocates
// nothing: a report reads a million of them.
struct ibCompositionLineAttr {
	ibCompositionAttr m_line;   // every cell of the line, where its column has nothing of its own
	// …and a column's own, by its schema index — the line's with the column's over it. ONLY the columns that have one,
	// never a slot per column: a line painted whole is one attribute however wide the report.
	std::vector<std::pair<size_t, ibCompositionAttr>> m_cells;

	// THE ATTRIBUTE OF ONE COLUMN, asked like the field appearance beside it (ibCompositionOutputInfo::AppearanceOf).
	const ibCompositionAttr& AttrOf(size_t column) const {
		for (const std::pair<size_t, ibCompositionAttr>& cell : m_cells)
			if (cell.first == column)
				return cell.second;
		return m_line;
	}
};

struct ibCompositionLine {
	int                m_level  = 0;
	int                m_indent = 0;
	ibSelectorNodeKind m_kind = ibSelectorNodeKind::Group;
	// The FOLD's fact: does this node stand over anything at all.
	bool               m_hasChildren = false;
	// …and the OUTPUT's promise: will what is under it actually be printed. Not the same question —
	// see the note on OnGroupBegin.
	bool               m_showsWhatIsUnder = false;
	// …and the output's LADDER: does it read further down than this heading's rung — another level, or its
	// records. A heading with nothing under it is still one of its level's headings: a period the fold filled in
	// because nothing moved in it (PERIODS) reads as a month like its neighbours, not as a record (Max, 2026-09-29).
	bool               m_levelReadsDeeper = false;
	// …and the palette ITS NODE paints its lines in, where the node ticked a theme of its own — the way a node's
	// own sort orders its headings. Null = the output's (ibCompositionOutputInfo::m_theme).
	const ibCompositionTheme* m_theme = nullptr;
	// …and the ATTRIBUTES its conditional appearance draws its cells with, where a rule's condition held. Null =
	// nothing applied. Made by the walk for this call and lives as long as it. EACH DRIVER STORES ITS OWN (Max,
	// 2026-09-30): one that keeps the line TAKES them — moves them into its own storage, never copies (a list's row,
	// then its node, one to one); one that draws at once reads them (a report, onto its sheet's own properties).
	ibCompositionLineAttr* m_attr = nullptr;

	int Page() const { return m_level + m_indent; }
};

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
	virtual void OnGroupBegin(const ibCompositionLine& line, const std::vector<ibValue>& values) = 0;

	// A ROW — a detail record, written down the page under whatever opened above it. It carries no
	// kind: a row is a row, and what makes a heading different is that it is a PAIR.
	virtual void OnRow(const ibCompositionLine& line, const std::vector<ibValue>& values) = 0;

	// A COLUMN of a cross-table — a heading that reads ACROSS the page, or a record laid out as a
	// column of its own. Default: nothing, because a driver that draws no table has nowhere across to
	// write (a list is rows, and rows only).
	virtual void OnColumn(const ibCompositionLine& /*line*/, const std::vector<ibValue>& /*values*/) {}

	// A HEADING CLOSES — every row and column under it has been written, so its figures are final and
	// its section can be ended. This is where a total that belongs at the BOTTOM goes. Default:
	// nothing, for the readers that draw a heading as it opens and never look back.
	virtual void OnGroupEnd(const ibCompositionLine& /*line*/, const std::vector<ibValue>& /*values*/) {}

	// ⭐⭐ ALL FOUR TAKE THE LINE, and that is the point of having one. A line answers three questions
	// about a node — which RUNG it stands on, how far INTO that rung's tree it is, and what KIND it is
	// — and they are not derivable from one another. Handed over as loose numbers, each verb had to be
	// told which of them it was getting, and every caller decided for itself.
	//
	// 🛑 AND THEY DECIDED DIFFERENTLY. `OnGroupEnd` was passed `line.Page()` by the walk
	// (dataComposerRun) and `line.m_level` by the RAM composer — two different numbers into one
	// parameter, agreeing only where indent happens to be zero, which is everywhere it has been looked
	// at so far and nowhere it has not. The same for `OnColumn`: the cross heading reached it as
	// `Page()` here and as `m_level` from `OnGroupBegin`, so one helper was reading two scales.
	//
	// With the line handed over whole, the DRIVER asks the question it actually means — "is this the
	// root" is about the rung, "where do I draw it" is about the page — and a caller cannot answer it
	// on the driver's behalf, correctly or otherwise.

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

// (COMPARING A VALUE, and reading a condition or a rule against a row in hand, moved out on 2026-09-30 into the
//  composer's evaluator, compositionCondition.h: none of it is anything a driver is handed.)

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
// the settings, DB-vs-RAM by the realisation. (See docs/private/ram-composer-decoupling.md.)

#endif
