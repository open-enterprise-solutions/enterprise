#ifndef __about__
#define __about__

#include <wx/wx.h>
#include <wx/panel.h>
#include "gui/univ/ctrl/button.h"
#include "gui/univ/ctrl/staticLine.h"
#include <wx/hyperlink.h>

#include "frmclient/frmclient.h"
#include "protocol/protocolNode.h"
#include "gui/univ/ctrl/staticText.h"
#include "gui/univ/ctrl/textCtrl.h"
#include "gui/univ/dialog.h"

/**
 * Class ibDialogAbout
 *
 * ABOUT — what the client works in, as the server's schema shows it (frmserver/schema/about.cpp): the old desktop's
 * dialog, drawn from what it is answered instead of reading the process itself.
 */
class FRMCLIENT_API ibDialogAbout :
	public ibDialog {
public:

	ibDialogAbout(wxWindow* parent, const ibProtocolNode& shown, int id = wxID_ANY);

private:

	ibStaticText* m_staticTextHeader;
	ibStaticText* m_staticTextFramework;
	ibStaticText* m_staticTextCommunity;
	ibStaticText* m_staticTextThanks;
	ibStaticLine* m_staticlineHeader;
	ibTextCtrl* m_textCtrlContributors;

	ibStaticText* m_staticDataBaseInfo;
	ibTextCtrl* m_textCtrl1;
	ibStaticText* m_staticAppInfo;
	ibTextCtrl* m_textCtrl2;
	ibStaticText* m_staticUserInfo;
	ibTextCtrl* m_textCtrl3;
	ibStaticText* m_staticLocaleInfo;
	ibTextCtrl* m_textCtrl4;
	ibButton* m_buttonOK;
};

#endif //__about__

