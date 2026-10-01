#ifndef OUTPUT_WINDOW_H__
#define OUTPUT_WINDOW_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "backend/system/systemEnum.h"
#include "frontend/frontend.h"

#include <map>

//
// Forward declarations.
//

#define outputWindow ibOutputWindow::GetOutputWindow()

/**
 *
 */
class ibOutputWindow : public wxStyledTextCtrl {
	
	/**
	* Constructor.
	*/
	ibOutputWindow(class FRONTEND_API ibFrontendMainFrame* parent, wxWindowID winid);

private:

	struct lineOutputData_t {

		wxString m_fileName;
		wxString m_docPath;
		int m_currLine;

		lineOutputData_t(const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
			int currLine = wxNOT_FOUND) : m_fileName(strFileName), m_docPath(strDocPath),
			m_currLine(currLine)
		{
		}
	};

	std::map<long, lineOutputData_t> m_listCodeInfo;

public:

	static ibOutputWindow* GetOutputWindow();

	/**
	 * WHERE A LINE CAME FROM, when it came from somewhere addressable.
	 *
	 * A message raised out of a module is recorded with that module and the line
	 * inside it — the pair a double-click navigates by. That pair is the whole
	 * difference between a complaint that is a sentence and one that is an
	 * address, and it was reachable only from the double-click handler.
	 *
	 * Plain values rather than the private record: a caller wants the module and
	 * the line, not the shape they happen to be stored in.
	 *
	 * Returns false for an ordinary line — most of them — leaving both untouched.
	 */

	/**
	 * Sets the font and color settings used in the output window.
	 */
	void SetFontColorSettings(const class ibFontColorSettings& settings);

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
	 * Empties the pane AND the record it feeds — they are one act.
	 *
	 * ⚠ NAMED APART from wxStyledTextCtrl::ClearAll rather than shadowing it: the base clears a
	 * text control, this clears the OUTPUT — the pane, the line origins and the message store
	 * together. One name for one meaning, and no reader has to work out which of the two they got.
	 */
	void ClearOutput();

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

	friend class ibFrontendMainFrameDesigner;

	/**
	 * Outputs text using the passed in text attribute. This method will only
	 * cause the output window to scroll down if the insertion point is already
	 * at the bottom of the text.
	 */
	void SharedOutput(const wxString& message, ibStatusMessage status,
		const wxString &strFileName = wxEmptyString, const wxString &strDocPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

protected:

	wxDECLARE_EVENT_TABLE();
};

#endif