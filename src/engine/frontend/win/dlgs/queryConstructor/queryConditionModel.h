#ifndef __QUERY_CONDITION_MODEL_H__
#define __QUERY_CONDITION_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// The CONDITIONS grid — a dataview model over the WHERE clause's AND chain.
////////////////////////////////////////////////////////////////////////////
//
// A WHERE is a tree; the grid shows the one shape a grid can show honestly — the top-level AND
// chain, one condition per row. Flattening and folding are inverses done on demand, so there is no
// second copy of the filter anywhere: what the rows show is read out of the AST at fill time and
// written back the same way.
//
// TWO COLUMNS, and the second one is not decoration. "Arbitrary" says whether the row could be
// read as *field · comparison · value* at all — a `CASE`, an `OR`, a comparison between two
// expressions cannot, and marking it is what explains why that row reads the way it does. It is
// computed from the condition, never stored: a flag somebody has to keep in step would be wrong
// the first time a row was edited.
//
// Every row is edited in THE expression editor (queryExpressionDialog.h) — the same window a
// join's ON and a totals expression open. There used to be a condition dialog with a "simple"
// mode and a "free" mode beside it; the free mode was the editor, and the simple mode was a
// second, weaker way to write the same text. The field list on the left of the tab is what the
// simple mode was actually FOR, and it is still there.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"

#include "backend/query/queryAst.h"

#include <functional>
#include <vector>

// Column 0 is reserved by the ibDataViewCtrl fork (a 0 model column paints blank rows).
enum ibQueryConditionColumn {
	kConditionColNumber = 1,
	kConditionColArbitrary,
	kConditionColText,
};

class ibQueryConditionModel : public ibDataViewVirtualListModel
{
public:
	ibQueryConditionModel() = default;

	// The select being edited — BORROWED. The rows are the top-level AND chain of its WHERE.
	void SetContent(ibQuerySelect* select);

	// The conditions as rows, and the inverse. Public because the dialog's add / edit / delete
	// verbs work in these terms and then hand the result straight back.
	std::vector<ibQueryAstExprPtr> Rows() const;
	void SetRows(const std::vector<ibQueryAstExprPtr>& rows);

	ibQueryAstExprPtr RowAt(unsigned int row) const;

	// Can this condition be read as field · comparison · value? Asked in one place so the column
	// and anything else that wants to know cannot disagree.
	static bool IsSimple(const ibQueryAstExprPtr& condition);

	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }
	// Where the ENGINE'S complaint goes when a condition typed into a cell does not parse. The model
	// does not judge the text and does not reword what it is told — it hands the message on.
	void SetOnError(std::function<void(const wxString&)> onError) { m_onError = std::move(onError); }

	// ---- ibDataViewListModel ----
	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override;
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override;

private:
	ibQuerySelect*        m_select = nullptr;   // borrowed
	std::function<void()> m_onChanged;
	std::function<void(const wxString&)> m_onError;
};

#endif // __QUERY_CONDITION_MODEL_H__
