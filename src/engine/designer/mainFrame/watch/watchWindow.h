#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

#include <wx/wx.h>
#include "watchctrl.h"

#include "frontend/frontend.h"
#include "backend/debugger/debugDefs.h"

//
// Forward declarations.
//

#define watchWindow	ibWatchWindow::GetWatchWindow()

/**
 * Window window class.
 */
class ibWatchWindow : public ibWatchCtrl
{
	/**
	* Constructor.
	*/
	ibWatchWindow(wxWindow* window, wxWindowID winid);

public:

	static ibWatchWindow* GetWatchWindow();

	/**
	* Called when the debugger receive an item.
	*/
	void SetVariable(const ibWatchWindowData& watchData);

	/**
	* Called when the debugger receive expanded item.
	*/
	void SetExpanded(const ibWatchWindowData& watchData);

	/**
	* Updates the value for the express in the index spot in the list.
	*/
	void UpdateItem(const wxTreeItemId& item);

	/**
	 * Updates the values of all the expressions being watched.
	 */
	void UpdateItems();

	/**
	 * Adds a new expression to the watch window.
	 */
	void AddWatch(const wxString& expression);

	/**
	 * Called when the user presses a key.
	 */
	void OnKeyDown(wxTreeEvent& event);

	/**
	 * Called when the user begins editing a label.
	 */
	void OnBeginLabelEdit(wxTreeEvent& event);

	/**
	 * Called when the user finishes editing a label.
	 */
	void OnEndLabelEdit(wxTreeEvent& event);

	/**
	 * Called when the user selects an item.
	 */
	void OnItemSelected(wxTreeEvent& event);

	/**
	* Called when the user delete an item.
	*/
	void OnItemDeleted(wxTreeEvent& event);

	/**
	* Called when the user expanding an item.
	*/
	void OnItemExpanding(wxTreeEvent& event);

private:

	friend class ibFrontendMainFrameDesigner;

	/**
	 * Creates a blank entry at the end of the list (for easily adding new items)
	 * if there isn't one there already.
	 */
	void CreateEmptySlotIfNeeded();

private:

	bool            m_editing;
	bool            m_updating;
	wxTreeItemId    m_root;

	wxDECLARE_EVENT_TABLE();
};

#endif