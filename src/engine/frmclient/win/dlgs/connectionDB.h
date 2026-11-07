#ifndef __CONNECTION_DB_H__
#define __CONNECTION_DB_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include "deskgui/univ/ctrl/staticText.h"
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include "deskgui/univ/ctrl/textCtrl.h"
#include <wx/sizer.h>
#include "deskgui/univ/ctrl/button.h"
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/frame.h>

#include "frmclient/frmclient.h"
#include "deskgui/univ/dialog.h"

// THE OLD DESKTOP'S, WHOLE. Testing the connection is a driver's, and the drivers are the
// server's: ⚠ NOT HERE YET — no request carries it; the button says so.
class FRMCLIENT_API ibDialogConnection : public ibDialog {
	ibStaticText* m_staticTextServer;
	ibTextCtrl* m_textCtrlServer;
	ibStaticText* m_staticTextPort;
	ibTextCtrl* m_textCtrlPort;
	ibStaticText* m_staticTextUser;
	ibTextCtrl* m_textCtrlUser;
	ibStaticText* m_staticTextPassword;
	ibTextCtrl* m_textCtrlPassword;
	ibStaticText* m_staticTextDataBase;
	ibTextCtrl* m_textCtrlDataBase;
	ibButton* m_buttonTestConnection;
	ibButton* m_buttonSaveConnection;
public:
	ibDialogConnection(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Connect to PostgreSQL"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxSYSTEM_MENU |  wxCLOSE_BOX |  wxCAPTION |  wxCLIP_CHILDREN | wxTAB_TRAVERSAL);

	void LoadConnectionData();
	void SaveConnectionData();

protected:
	void TestConnectionOnButtonClick(wxCommandEvent& event);
	void SaveConnectionOnButtonClick(wxCommandEvent& event);
};

#endif // !_CONNECTION_DB_