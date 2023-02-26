#ifndef WATCH_WINDOW_H
#define WATCH_WINDOW_H

#include <wx/wx.h>
#include "watchctrl.h"

//
// Forward declarations.
//

struct watchWindowData_t {

	wxTreeItemId m_itemID;
	
	struct watchWindowItem_t {

		wxTreeItemId m_itemID;
		wxString m_name;
		wxString m_value;
		wxString m_type;
		bool m_hasAttributes;

		watchWindowItem_t(const wxString& n, const wxString& v, const wxString& t, bool a, const wxTreeItemId& i) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a), m_itemID(i) {}
	};

	std::vector<watchWindowItem_t> m_expressions;

public:

	watchWindowData_t(const wxTreeItemId& item = NULL) : m_itemID(item) {}

	wxTreeItemId GetItem() const {
		return m_itemID;
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_expressions.emplace_back(name, value, type, hasAttributes, m_itemID);
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes, const wxTreeItemId& item) {
		m_expressions.emplace_back(name, value, type, hasAttributes, item);
	}

	wxTreeItemId GetItem(unsigned int idx) const {
		return m_expressions[idx].m_itemID;
	}

	wxString GetName(unsigned int idx) const {
		return m_expressions[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_expressions[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_expressions[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_expressions[idx].m_hasAttributes;
	}

	unsigned int GetWatchCount() const {
		return m_expressions.size();
	}
};

#define watchWindow	CWatchWindow::GetWatchWindow()

/**
 * Window window class.
 */
class CWatchWindow : public CWatchCtrl
{
	/**
	* Constructor.
	*/
	CWatchWindow(wxWindow* window, wxWindowID winid);

public:

	static CWatchWindow* GetWatchWindow();

	/**
	* Called when the debugger receive an item.
	*/
	void SetVariable(const watchWindowData_t& watchData);

	/**
	* Called when the debugger receive expanded item.
	*/
	void SetExpanded(const watchWindowData_t& watchData);

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

	friend class wxAuiDocDesignerMDIFrame;

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