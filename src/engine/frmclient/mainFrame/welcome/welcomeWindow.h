#ifndef _FRMCLIENT_WELCOME_WINDOW_H__
#define _FRMCLIENT_WELCOME_WINDOW_H__

#include <utility>
#include <vector>

#include <wx/wx.h>
#include "deskgui/univ/panel.h"

#include "frmclient/frmclient.h"

// THE WORKSPACE WHILE NO TAB IS OPEN — a sheet in its frame where the tabs stand (ibFrontendMainFrame: in the client
// window, shown while it holds no tab), greeting the person the session is by the time of day and asking where to start;
// a mode's window adds what it has to offer under the question (the designer's keys). Where a configuration has a start
// page, that page is the workspace and the welcome is never seen (Max, 2026-11-13: "the greeting is for when there is no
// desktop; a desktop replaces it").
//
// Its sheet and frame are the theme's: a panel's colour (FRAME), a simple border. Drawn whole on each paint, the greeting
// taken from the clock then, so a window left open from morning says the afternoon's.
class FRMCLIENT_API ibWelcomeWindow : public ibPanel {
public:

	// The person the session is (the login's answer, FullName): none - the greeting alone.
	ibWelcomeWindow(wxWindow* parent, const wxString& fullName);

	// A sheet to look at: nothing in it takes the keys.
	virtual bool AcceptsFocus() const override { return false; }

protected:

	// The keys shown under the question, in order: what a command does and the key it has now. Default: none.
	virtual std::vector<std::pair<wxString, wxString>> GetKeys() const { return {}; }

private:

	// The greeting by the time of day, the question under it, the keys under that — the block centred across, a third of
	// the way down.
	void OnPaint(wxPaintEvent& event);

	wxString m_fullName;
};

#endif
