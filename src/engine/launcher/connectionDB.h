#ifndef __CONNECTION_DB_H__
#define __CONNECTION_DB_H__

#include <wx/artprov.h>
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

#include <wx/wx.h>
#include "core/localization/dictionary.h"   // _() — the dialog's words
#include "launcher.h"                       // ibListInfo — what the dialog edits

class ibDialogConnection : public wxDialog {
	wxStaticText* m_staticTextName;
	wxTextCtrl* m_textCtrlName;

	wxRadioButton* m_radioFile;
	wxRadioButton* m_radioServer;
	wxRadioButton* m_radioWeb;

	// File mode controls
	wxStaticText* m_staticTextFilePath;
	wxDirPickerCtrl* m_dirPickerFile;

	// Server mode controls — the cluster, server[:port], and the base's name in it
	wxStaticText* m_staticTextServer;
	wxTextCtrl* m_textCtrlServer;
	wxStaticText* m_staticTextDataBase;
	wxTextCtrl* m_textCtrlDataBase;

	// Web mode controls — the publication's link, as the browser's address bar shows it
	wxStaticText* m_staticTextAddress;
	wxTextCtrl* m_textCtrlAddress;

	wxBoxSizer* m_sizerFileMode;
	wxBoxSizer* m_sizerServerMode;
	wxBoxSizer* m_sizerWebMode;

	wxButton* m_buttonTestConnection;
	wxButton* m_buttonSaveConnection;

	void UpdateModeVisibility();

public:

	void InitConnection(const wxString& strName, const ibListInfo& info);

	ibDialogConnection(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Configure connect"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 320), long style = wxSYSTEM_MENU | wxCLOSE_BOX | wxCAPTION | wxCLIP_CHILDREN | wxTAB_TRAVERSAL);

	wxString GetNameIB() const { return m_textCtrlName->GetValue(); }
	// The connection as the dialog's fields say it.
	ibListInfo GetListInfo() const;

protected:
	void OnModeChanged(wxCommandEvent& event);
	void TestConnectionOnButtonClick(wxCommandEvent& event);
	void SaveConnectionOnButtonClick(wxCommandEvent& event);
};

#endif // !_CONNECTION_DB_
