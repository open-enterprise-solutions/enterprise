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

	ibQueryAstJoin* JoinAt(unsigned int row) const;

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
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
};

#endif // __QUERY_LINK_MODEL_H__
