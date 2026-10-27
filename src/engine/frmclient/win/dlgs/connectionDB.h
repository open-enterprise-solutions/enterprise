#ifndef __CONNECTION_DB_H__
#define __CONNECTION_DB_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/frame.h>

#include "frmclient/frmclient.h"

// THE OLD DESKTOP'S, WHOLE. Testing the connection is a driver's, and the drivers are the
// server's: ⚠ NOT HERE YET — no request carries it; the button says so.
class FRMCLIENT_API ibDialogConnection : public wxDialog {
	wxStaticText* m_staticTextServer;
	wxTextCtrl* m_textCtrlServer;
	wxStaticText* m_staticTextPort;
	wxTextCtrl* m_textCtrlPort;
	wxStaticText* m_staticTextUser;
	wxTextCtrl* m_textCtrlUser;
	wxStaticText* m_staticTextPassword;
	wxTextCtrl* m_textCtrlPassword;
	wxStaticText* m_staticTextDataBase;
	wxTextCtrl* m_textCtrlDataBase;
	wxButton* m_buttonTestConnection;
	wxButton* m_buttonSaveConnection;
public:
	ibDialogConnection(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Connect to PostgreSQL"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxSYSTEM_MENU |  wxCLOSE_BOX |  wxCAPTION |  wxCLIP_CHILDREN | wxTAB_TRAVERSAL);

	void LoadConnectionData();
	void SaveConnectionData();

protected:
	void TestConnectionOnButtonClick(wxCommandEvent& event);
	void SaveConnectionOnButtonClick(wxCommandEvent& event);
};

#endif // !_CONNECTION_DB_