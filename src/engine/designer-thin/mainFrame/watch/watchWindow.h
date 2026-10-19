#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

/////////////////////////////////////////////////////////////////////////////
// The debugger's Watch pane: the expressions a person typed or dropped, one
// row each, with the value and type the debugger answers. A row's tree id IS
// the expression's id on the wire — the debugger answers into it
// (SetVariable), and a row with members asks for them only when opened
// (SetExpanded). The last row is always empty: typing into it adds one.
/////////////////////////////////////////////////////////////////////////////

#include "frmclient/win/ctrls/treelistctrl.h"
#include "designer-thin/backend/debugger/debugDefs.h"

#define watchWindow	ibWatchWindow::GetWatchWindow()

class ibWatchWindow : public ibTreeListCtrl
{
	// Made by the frame, once.
	friend class ibFrontendMainFrameDesigner;
	ibWatchWindow(wxWindow* parent, wxWindowID id);

public:

	virtual ~ibWatchWindow() override;

	static ibWatchWindow* GetWatchWindow();

	// The debugger's answers: the values of watched rows, and the members of an opened one.
	void SetVariable(const ibWatchWindowData& watchData);
	void SetExpanded(const ibWatchWindowData& watchData);

	// A new row before the empty one, its value asked of the debugger.
	void AddWatch(const wxString& expression);

private:

	// Sends the row's expression to the debugger; the value comes back by SetVariable.
	void UpdateItem(const wxTreeItemId& item);

	void CreateEmptySlotIfNeeded();

	// After a row is removed or edited: the row that followed it, or the empty one.
	void SelectNext(const wxTreeItemId& next);

	// Name and value share what the type column leaves, in the proportion a person dragged them to.
	void FitColumns();

	void OnSize(wxSizeEvent& event);
	void OnColumnDragged(wxListEvent& event);
	void OnKeyDown(wxTreeEvent& event);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnItemDeleted(wxTreeEvent& event);
	void OnItemExpanding(wxTreeEvent& event);

	wxTreeItemId m_root;
	bool m_editing = false;

	// The rows are being changed by the debugger's answer, not by a person: a row deleted then
	// removes no expression, and a row expanded asks for nothing.
	bool m_updating = false;
};

#endif
