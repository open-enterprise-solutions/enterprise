#ifndef OUTPUT_WINDOW_H__
#define OUTPUT_WINDOW_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include <map>

//
// Forward declarations.
//

class CMainFrame;
class COutputWindow;
class FontColorSettings;

#define outputWindow           (COutputWindow::Get())

/**
 *
 */
class COutputWindow : public wxStyledTextCtrl
{
	static COutputWindow *s_instance;

	/**
	* Constructor.
	*/
	COutputWindow(wxWindow* parent, wxWindowID winid);

private:

	enum eStatusMessage
	{
		eMessage = 1,
		eWarning,
		eError
	};

	struct code_info {

		wxString m_fileName;
		wxString m_docPath;
		int m_currLine;

		code_info(const wxString &fileName = wxEmptyString, const wxString &docPath = wxEmptyString,
			int currLine = wxNOT_FOUND) : m_fileName(fileName), m_docPath(docPath),
			m_currLine(currLine)
		{
		}
	};

	std::map<long, code_info> m_aCodeInfo;

public:

	static COutputWindow* Get();

	/**
	 * Sets the font and color settings used in the output window.
	 */
	void SetFontColorSettings(const FontColorSettings& settings);

	/**
	 * Adds a message to the end of the log.
	 */
	void OutputMessage(const wxString& message,
		const wxString &fileName = wxEmptyString, const wxString &docPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

	/**
	 * Adds a warning message to the end of the log.
	 */
	void OutputWarning(const wxString& message,
		const wxString &fileName = wxEmptyString, const wxString &docPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

	/**
	 * Adds an error message to the end of the log.
	 */
	void OutputError(const wxString& message, 
		const wxString &fileName = wxEmptyString, const wxString &docPath = wxEmptyString,
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

	/**
	 * Outputs text using the passed in text attribute. This method will only
	 * cause the output window to scroll down if the insertion point is already
	 * at the bottom of the text.
	 */
	void SharedOutput(const wxString& message, eStatusMessage status,
		const wxString &fileName = wxEmptyString, const wxString &docPath = wxEmptyString,
		int currLine = wxNOT_FOUND);

protected:

	wxDECLARE_EVENT_TABLE();
};

#endif