#ifndef __about__
#define __about__

#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/statline.h>
#include <wx/hyperlink.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"

/**
 * Class ibDialogAbout
 *
 * ABOUT — what the client works in, as the server's schema shows it (frmserver/schema/about.cpp): the desktop's
 * dialog (frontend/win/dlgs/about), drawn from what it is answered instead of reading the process itself.
 */
class FRMCLIENT_API ibDialogAbout :
	public wxDialog {
public:

	ibDialogAbout(wxWindow* parent, const ibProtocolNode& shown, int id = wxID_ANY);

private:

	wxStaticText* m_staticTextHeader;
	wxStaticText* m_staticTextFramework;
	wxStaticText* m_staticTextCommunity;
	wxStaticText* m_staticTextThanks;
	wxStaticLine* m_staticlineHeader;
	wxTextCtrl* m_textCtrlContributors;

	wxStaticText* m_staticDataBaseInfo;
	wxTextCtrl* m_textCtrl1;
	wxStaticText* m_staticAppInfo;
	wxTextCtrl* m_textCtrl2;
	wxStaticText* m_staticUserInfo;
	wxTextCtrl* m_textCtrl3;
	wxStaticText* m_staticLocaleInfo;
	wxTextCtrl* m_textCtrl4;
	wxButton* m_buttonOK;
};

#endif //__about__

