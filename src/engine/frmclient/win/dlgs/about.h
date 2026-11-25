#ifndef __about__
#define __about__

#include <wx/wx.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"
#include "deskgui/univ/dialog.h"

/**
 * Class ibDialogAbout
 *
 * ABOUT — what the client is and what it works in, as the server's schema shows it (frmserver/schema/about.cpp): the
 * program and the platform's version beside the logo, the base, the connection, the person and the language, what the
 * platform is built on and whose ideas it grew from, the licence. Copy puts the facts on the clipboard as text — a
 * report names the build it came from.
 */
class FRMCLIENT_API ibDialogAbout :
	public ibDialog {
public:

	ibDialogAbout(wxWindow* parent, const ibProtocolNode& shown, int id = wxID_ANY);

private:

	// The facts, a line each — the program, its version, the rows — as Copy hands them on.
	wxString m_facts;
};

#endif //__about__
