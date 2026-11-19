#ifndef _FRMCLIENT_OUTPUT_WINDOW_H__
#define _FRMCLIENT_OUTPUT_WINDOW_H__

#include <map>

#include <wx/wx.h>
#include "deskgui/univ/stc/stc.h"

#include "protocol/protocol.h"   // ibProtocol::ibFrame::ibMessage::ibLevel

#include "frmclient/frmclient.h"

class ibFontColorSettings;

// THE MESSAGES — what the server says to the person (the answer's Messages), one line each, its level the picture in
// the margin. The desktop's ibOutputWindow (enterprise/mainFrame/output) carried over: a run's messages travel to
// whoever debugs it on the server now, so the window only shows them — in the font the code editor shows code in, the
// window's (ibFrontendMainFrame::GetFontColorSettings).
class FRMCLIENT_API ibOutputWindow : public ibStyledTextCtrl {
public:

	explicit ibOutputWindow(class ibFrontendMainFrame* parent, wxWindowID winid = wxID_ANY);

	void SetFontColorSettings(const ibFontColorSettings& settings);

	// A message at the end — the view follows it when it was at the end already.
	void Output(const wxString& message, ibProtocol::ibFrame::ibMessage::ibLevel level);
	// …and one with its place — a script's failure: `file` the external data tool's or report's (none: the application's),
	// `module` by its address, `line` from 1. Its lines, double-clicked, open the module there (the desktop's).
	void Output(const wxString& message, ibProtocol::ibFrame::ibMessage::ibLevel level,
		const wxString& file, const wxString& module, int line);
	void Clear();

private:

	// Where a message with a place was said — the lines it took up to `last`, and the module at the line.
	struct Place {
		wxString file;
		wxString module;
		int      line;
		int      last;
	};

	void OnDoubleClick(wxMouseEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnClearOutput(wxCommandEvent& event);

	// By the message's first line.
	std::map<int, Place> m_places;

	wxDECLARE_EVENT_TABLE();
};

#endif
