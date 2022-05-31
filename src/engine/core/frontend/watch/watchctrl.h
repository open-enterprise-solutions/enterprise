#ifndef WATCH_CTRL_H
#define WATCH_CTRL_H

#include "frontend/controls/treelistctrl.h"

//
// Forward declarations.
//

class wxXmlNode;

/**
 * Watch control. This is the base class for the tree controls used to display
 * the results of evaluating expressions.
 */
class CWatchCtrl : public wxTreeListCtrl
{

public:

	CWatchCtrl(wxWindow *parent, wxWindowID id = -1, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize,
		long style = wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT | wxTR_ROW_LINES | wxTR_HAS_BUTTONS, const wxValidator &validator = wxDefaultValidator, const wxString& name = "");

	virtual ~CWatchCtrl();

	/**
	 * Sets the font used to display values.
	 */
	void SetValueFont(const wxFont& font);

	/**
	 * Called after the user resizes one of of the column headings.
	 */
	void OnColumnEndDrag(wxListEvent& event);

	/**
	 * Called when the window changes size.
	 */
	void OnSize(wxSizeEvent& event);

private:

	static const unsigned int s_numColumns = 3;

	/**
	 * Updates the variable that stores the proprotion of the first column
	 * relative to the width of the control.
	 */
	void UpdateColumnSizes();

	/**
	 * Gets the size of each of the columns in pixels.
	 */
	void GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const;

	/**
	 * Updates the font for all values in the tree.
	 */
	void UpdateFont(const wxTreeItemId &item);

protected:

	float                       m_columnSize[s_numColumns];
	float                       m_columnProportion;
	wxFont                      m_valueFont;

	wxDECLARE_EVENT_TABLE();
};

#endif