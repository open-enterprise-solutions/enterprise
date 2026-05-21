#ifndef OUTPUT_WINDOW_H__
#define OUTPUT_WINDOW_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "backend/system/systemEnum.h"
#include "frontend/frontend.h"

#include <map>
#include <vector>

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
	ibOutputWindow(class FRONTEND_API ibFrontendDocMDIFrame* parent, wxWindowID winid);

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

	// AI Markers panel readback. One snapshot record per code-attached
	// output line — file, doc path, source line, message text, severity
	// ("error"|"warning"|"info"). Used by ibAiMarkersPanel to ingest the
	// output window's accumulated diagnostic state without duplicating
	// the storage. Allocated fresh on every call, so calling it on a hot
	// path would be wasteful; the panel pulls only on explicit refresh.
	struct OutputLineSnapshot {
		wxString fileName;
		wxString docPath;
		int      srcLine = 0;
		wxString message;
		wxString severity;   // "error" | "warning" | "info"
	};
	std::vector<OutputLineSnapshot> CollectAttachedLines() const;

	static ibOutputWindow* GetOutputWindow();

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

	friend class ibFrontendDocMDIFrameDesigner;

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