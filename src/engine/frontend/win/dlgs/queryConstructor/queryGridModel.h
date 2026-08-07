#ifndef __QUERY_GRID_MODEL_H__
#define __QUERY_GRID_MODEL_H__

////////////////////////////////////////////////////////////////////////////
// ONE GRID MODEL for every plain list in the constructor.
////////////////////////////////////////////////////////////////////////////
//
// The links, the conditions and the unions each earned a model of their own: they hold a shape (a
// join has two sides and two "all" boxes; a union has one column per branch) that a general model
// would only obscure. Everything else in this window is the same thing N times — a list of rows,
// a few columns, cells read from the AST and some of them written back.
//
// Those were wxListBox and wxListCtrl, and that is what made the window read as several dialogs
// stitched together: three panes with grid lines and in-place editing, six without. The look is the
// smaller half of it. The bigger half is that a listbox cannot be edited in place, so every one of
// those panes needed a dialog to change one word — the alias, the direction, the totals kind.
//
// So: one model, told how to READ a cell and how to WRITE one. The AST stays the single copy of the
// truth (the callbacks reach into it directly), the dialog keeps its per-pane knowledge, and every
// list in the window is an ibDataViewCtrl with the same grid lines, the same row height and the
// same editing behaviour.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/win/ctrls/dataview/dataview.h"

#include <functional>

// Column 0 is reserved by the ibDataViewCtrl fork — the first column a pane can use is 1.
enum ibQueryGridColumn {
	kGridCol1 = 1,
	kGridCol2,
	kGridCol3,
};

class ibQueryGridModel : public ibDataViewVirtualListModel
{
public:
	// What a cell SAYS. Asked live, so a pane never carries a stale copy of the AST.
	using Reader = std::function<wxString(unsigned int row, unsigned int col)>;
	// What a cell DOES when it is typed into. Returning false refuses the edit (the old text stays)
	// — which is the honest answer for a name already taken or an expression the engine rejects.
	using Writer = std::function<bool(unsigned int row, unsigned int col, const wxString& text)>;

	ibQueryGridModel() = default;

	void SetReader(Reader reader) { m_reader = std::move(reader); }
	void SetWriter(Writer writer) { m_writer = std::move(writer); }
	// Run after an accepted write — the dialog refills, so the text pane and every other tab follow.
	void SetOnChanged(std::function<void()> onChanged) { m_onChanged = std::move(onChanged); }

	// How many rows there are now. The pane's Fill() calls this and nothing else.
	void SetRowCount(unsigned int count) { Reset(count); }

	// A COLUMN THAT CARRIES A PICTURE. The field grids show paths into the data, and a row without
	// the field picture reads as a different kind of thing from the same field in the tree beside
	// it — which is the whole of "the designer catches the eye". The icon is the model's because the
	// renderer asks the model for the cell's VALUE, picture included (ibDataViewIconText).
	void SetIconColumn(unsigned int column, const wxIcon& icon)
	{
		m_iconColumn = column;
		m_icon = icon;
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override
	{
		if (!m_reader)
			return;

		const wxString text = m_reader(row, col);
		if (col == m_iconColumn && m_icon.IsOk()) {
			variant << ibDataViewIconText(text, m_icon);
			return;
		}
		variant = text;
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override
	{
		if (!m_writer || !m_writer(row, col, variant.GetString()))
			return false;
		if (m_onChanged)
			m_onChanged();
		return true;
	}

private:
	Reader                m_reader;
	Writer                m_writer;
	std::function<void()> m_onChanged;
	unsigned int          m_iconColumn = 0;   // 0 is the fork's reserved column — never a real one
	wxIcon                m_icon;
};

#endif // __QUERY_GRID_MODEL_H__
