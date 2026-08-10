#ifndef __QUERY_LINK_MODEL_H__
#define __QUERY_LINK_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// The LINKS grid — a dataview model over `ibQuerySelect::m_joins`.
////////////////////////////////////////////////////////////////////////////
//
// One row per join, five cells: LEFT TABLE · all · RIGHT TABLE · all · CONDITION.
//
// THE TWO CHECKBOXES ARE THE JOIN KIND, and that is the whole idea of this grid. A person does not
// think "left outer join"; they think "keep every row of this table even when the other has none".
// Two boxes say exactly that, and the four states they make ARE the four kinds:
//
//     neither → Inner        left → Left ("all from the left")
//     right   → Right        both → Full
//
// The kind stays an `ibQueryJoinKindAst` in the AST — a TYPE, named once — and this grid is how it
// is read and written. Nothing here stores a pair of bools: the boxes are computed from the kind
// and fold straight back into it, so there is no second copy to drift.
//
// WHY A DATAVIEW MODEL rather than a list control filled by hand: these cells are EDITED IN PLACE
// — a toggle, a table chosen from those in the query, an expression opened in the shared editor.
// That is what `ibDataViewCtrl` + a model is for, it is what every other settings grid in the
// product is built on, and a hand-filled list can only ever answer edits with a modal per row.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"

#include "backend/query/queryAst.h"

#include <functional>
#include <vector>

// Column ids — column 0 is reserved by the ibDataViewCtrl fork (a 0 model column paints blank
// rows), so these start at 1. Same rule the filter tree follows.
enum ibQueryLinkColumn {
	kLinkColLeftTable = 1,
	kLinkColAllLeft,
	kLinkColRightTable,
	kLinkColAllRight,
	kLinkColCondition,
	// ⭐ WRITTEN BY HAND? The switch that decides which shape the condition cell takes — a closed list
	// of the field pairs the two tables offer, or free text with the "..." into the expression editor.
	// Appended rather than slotted in beside the condition: these are MODEL column ids, and the order
	// on screen is the order the columns are added in, not the order of this enum.
	kLinkColArbitrary,
};

class ibQueryLinkModel : public ibDataViewVirtualListModel
{
public:
	ibQueryLinkModel() = default;

	// The select being edited (BORROWED — the model never owns or copies it, which is what makes
	// "the grid and the text say the same thing" true by construction) and the names its sources
	// are known by, indexed as the constructor indexes them: 0 = FROM, 1..n = joins[i-1].
	void SetContent(ibQuerySelect* select, std::vector<wxString> tableNames);

	// Raised after a cell changed the AST, so the dialog refills everything and re-renders the text.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }
	// Where the ENGINE'S complaint goes when a condition typed into the cell does not parse. Handed
	// on verbatim — there is no second opinion here about what a valid condition is.
	void SetOnError(std::function<void(const wxString&)> onError) { m_onError = std::move(onError); }

	// ⭐⭐ A ROW IS A LINK, NOT A TABLE.
	//
	// In the AST every table past the first IS a join entry, so a grid over `m_joins` shows one row
	// per TABLE — which made a link appear the moment a table was added (nobody asked for it), and
	// made deleting a link mean deleting the table (nobody asked for that either).
	//
	// So the rows are the joins that CARRY A LINK: a condition, or a row the author has just added
	// and not filled in yet. A table with neither is simply a table — it sits in the query, the tab
	// still opens because there are two of them, and there is nothing about it to show here.
	size_t JoinIndexOf(unsigned int row) const;
	ibQueryAstJoin* JoinAt(unsigned int row) const;

	// The first table with no link on it — where "Add link" puts the new row. `npos` when every
	// table already carries one (or when there is no second table at all).
	size_t FirstUnlinked() const;

	// ⭐ THE TABLE OF A LINK THAT WAS STARTED AND LEFT EMPTY, or an empty string when there is none.
	//
	// Only a STARTED one counts. A table with no link at all is not unfinished — the tables are
	// multiplied, which is a whole query — so the difference is whether the author asked for a link,
	// and that is a fact only this model holds (the AST is identical either way).
	wxString UnfinishedLink() const;
	// Start a link on that table: the row appears, empty, and the cells fill it in.
	void BeginLink(size_t joinIndex);
	// AND THE LINK GOES, NOT THE TABLE. Clears the condition and forgets the pending mark; the table
	// stays exactly where it was.
	void RemoveLink(unsigned int row);

	// ⭐ COPY THIS LINK ONTO THE NEXT TABLE THAT HAS NONE. A second link is nearly always the first
	// one with a name changed — `a.Ref = b.Owner` then `a.Ref = c.Owner` — and retyping it is exactly
	// the work a copy exists to save. The row lands on the first unlinked table, carrying the same
	// condition; the cell is then edited to point at what it should. Returns false when there is
	// nothing to copy or nowhere to put it.
	bool CopyLink(unsigned int row);

	// Can this link be read as *field = field*? The one place that decides, so the switch, the cell
	// and anything else that asks cannot disagree. An EMPTY condition counts as simple: "by
	// reference" is a link the grid can offer, not something only free text can express.
	static bool IsSimple(const ibQueryAstJoin& join);

	// The observation, unless the author has asked to write this row by hand. Clearing the switch on
	// a link that is not simple is refused — see SetValueByRow.
	bool IsArbitrary(unsigned int row) const;

	// (WHETHER A SET OF LINKS CONTRADICTS ITSELF IS THE ENGINE'S QUESTION, not this grid's. A table
	//  linked to itself, or a link that says nothing about the table it is written on, are refused by
	//  ibQueryLowering::CheckNames in its own words and with its own position — and the verdict line
	//  under the tabs already shows exactly that. A second opinion here would be a second rule to
	//  keep in step, and the two would disagree the first time the language grew.)

	// ---- ibDataViewListModel ----
	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;

private:
	// The name of the table on each side of a join. The RIGHT side is the joined source itself;
	// the LEFT is what it attaches to — the FROM, which is what a reader means by "table 1".
	wxString LeftName(unsigned int row) const;
	wxString RightName(unsigned int row) const;

	ibQuerySelect*        m_select = nullptr;   // borrowed
	std::vector<wxString> m_tableNames;
	// row -> join index. Rebuilt on every fill; the two vectors below are indexed by JOIN, not by
	// row, so they survive a row appearing or going away.
	std::vector<size_t>   m_rows;
	// A link the author started and has not written yet. Without it a new row would have nothing to
	// exist BY — its condition is empty, and an empty condition is exactly "no link".
	std::vector<bool>     m_pending;
	// The links the author has asked to write by hand. Only meaningful on one that IS simple; on any
	// other the observation already answers.
	std::vector<bool>     m_freehand;
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
};

#endif // __QUERY_LINK_MODEL_H__
