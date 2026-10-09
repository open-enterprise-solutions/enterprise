#ifndef _FRMCLIENT_OUTPUT_WINDOW_H__
#define _FRMCLIENT_OUTPUT_WINDOW_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "protocol/protocol.h"   // ibProtocolMessageLevel

#include "frmclient/frmclient.h"

class ibFontColorSettings;

// THE MESSAGES — what the server says to the person (the answer's Messages), one line each, its level the picture in
// the margin. The desktop's ibOutputWindow (enterprise/mainFrame/output) carried over: a run's messages travel to
// whoever debugs it on the server now, so the window only shows them — in the font the code editor shows code in, the
// window's (ibFrontendMainFrame::GetFontColorSettings).
class FRMCLIENT_API ibOutputWindow : public wxStyledTextCtrl {
public:

	explicit ibOutputWindow(class ibFrontendMainFrame* parent, wxWindowID winid = wxID_ANY);

	void SetFontColorSettings(const ibFontColorSettings& settings);

	// A message at the end — the view follows it when it was at the end already.
	void Output(const wxString& message, ibProtocolMessageLevel level);
	void Clear();

private:

	void OnContextMenu(wxContextMenuEvent& event);
	void OnClearOutput(wxCommandEvent& event);

	wxDECLARE_EVENT_TABLE();
};

#endif
