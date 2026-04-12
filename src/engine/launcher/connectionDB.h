#ifndef __CONNECTION_DB_H__
#define __CONNECTION_DB_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
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
#include <wx/radiobut.h>
#include <wx/filepicker.h>

#include "frontend/frontend.h"

class ibDialogConnection : public wxDialog {
	wxStaticText* m_staticTextName;
	wxTextCtrl* m_textCtrlName;

	wxRadioButton* m_radioFile;
	wxRadioButton* m_radioServer;

	// File mode controls
	wxStaticText* m_staticTextFilePath;
	wxDirPickerCtrl* m_dirPickerFile;

	// Server mode controls
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

	wxBoxSizer* m_sizerFileMode;
	wxBoxSizer* m_sizerServerMode;

	wxButton* m_buttonTestConnection;
	wxButton* m_buttonSaveConnection;

	void UpdateModeVisibility();

public:

	void InitConnection(
		const wxString &strName,
		bool bFileMode,
		const wxString& strFilePath,
		const wxString &strServer, const wxString& strPort,
		const wxString& strDatabase,
		const wxString& strUser, const wxString& strPassword
	);

	ibDialogConnection(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Configure connect"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 320), long style = wxSYSTEM_MENU | wxCLOSE_BOX | wxCAPTION | wxCLIP_CHILDREN | wxTAB_TRAVERSAL);

	bool IsFileMode() const { return m_radioFile->GetValue(); }
	wxString GetNameIB() const { return m_textCtrlName->GetValue(); }
	wxString GetFilePath() const { return m_dirPickerFile->GetPath(); }
	wxString GetServer() const { return m_textCtrlServer->GetValue(); }
	wxString GetPort() const { return m_textCtrlPort->GetValue(); }
	wxString GetDBName() const { return m_textCtrlDataBase->GetValue(); }
	wxString GetUser() const { return m_textCtrlUser->GetValue(); }
	wxString GetPassword() const { return m_textCtrlPassword->GetValue(); }

protected:
	void OnModeChanged(wxCommandEvent& event);
	void TestConnectionOnButtonClick(wxCommandEvent& event);
	void SaveConnectionOnButtonClick(wxCommandEvent& event);
};

#endif // !_CONNECTION_DB_
