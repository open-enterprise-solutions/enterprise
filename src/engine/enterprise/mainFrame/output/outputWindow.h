#ifndef OUTPUT_WINDOW_H__
#define OUTPUT_WINDOW_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "backend/systemManager/systemEnum.h"

//
// Forward declarations.
//

#define outputWindow COutputWindow::GetOutputWindow()

/**
 *
 */
class COutputWindow : public wxStyledTextCtrl {
	/**
	* Constructor.
	*/
	COutputWindow(wxWindow* parent, wxWindowID winid);

public:

	static COutputWindow* GetOutputWindow();

	/**
	 * Sets the font and color settings used in the output window.
	 */
	void SetFontColorSettings(const class FontColorSettings& settings);

	/**
	 * Adds a message to the end of the log.
	 */
	void OutputMessage(const wxString& message,
		const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

	/**
	 * Adds a warning message to the end of the log.
	 */
	void OutputWarning(const wxString& message,
		const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

	/**
	 * Adds an error message to the end of the log.
	 */
	void OutputError(const wxString& message, 
		const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

	/**
	 * Returns the line that the cursor is positioned on.
	 */
	int GetCurrentLine() const;

	/**
	* Called when the user double clicks in the window.
	*/
	void OnDoubleClick(wxMouseEvent& event);

	/**
	* Called when the user double clicks in the window.
	*/
	void OnContextMenu(wxContextMenuEvent& evt);

	/**
	* Called when the user double clicks in the window.
	*/
	void OnClearOutput(wxCommandEvent &event);

	/**
	* Called when the user double clicks in the window.
	*/
	void OnKeyDown(wxKeyEvent& evt);

private:

	friend class CDocEnterpriseMDIFrame;

	/**
	 * Outputs text using the passed in text attribute. This method will only
	 * cause the output window to scroll down if the insertion point is already
	 * at the bottom of the text.
	 */
	void SharedOutput(const wxString& message, eStatusMessage status,
		const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

protected:

	wxDECLARE_EVENT_TABLE();
};

#endif