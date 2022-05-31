#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

#include <wx/wx.h>
#include "watchctrl.h"
#include "watchWindowEvents.h"

#define watchWindow           (CWatchWindow::Get())

//
// Forward declarations.
//

class CMemoryReader;

/**
 * Window window class.
 */
class CWatchWindow : public CWatchCtrl
{
	static CWatchWindow *s_instance;

	/**
	* Constructor.
	*/
	CWatchWindow(wxWindow* window, wxWindowID winid);

public:

	static CWatchWindow* Get();

	/**
	* Called when the debugger receive an item.
	*/
	void SetVariable(CMemoryReader &reader);

	/**
	* Called when the debugger receive expanded item.
	*/
	void SetExpanded(CMemoryReader &reader);

	/**
	* Updates the value for the express in the index spot in the list.
	*/
	void UpdateItem(wxTreeItemId item);

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

	/**
	* Called when the server expanding an item.
	*/
	void OnDataExpanding(wxWatchWindowDataEvent& event);
	
	/**
	* Called when the server set var an item.
	*/
	void OnDataSetVariable(wxWatchWindowDataEvent& event);

private:

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