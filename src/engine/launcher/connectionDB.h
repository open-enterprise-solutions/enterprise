#ifndef __CONNECTION_DB_H__
#define __CONNECTION_DB_H__

#include <wx/artprov.h>
#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/frame.h>

#include <wx/wx.h>
#include "core/localization/dictionary.h"   // _() — the dialog's words
#include "launcher.h"                       // ibListInfo — what the dialog edits
#include "gui/univ/ctrl/radioButton.h"  // the dialog's controls — the kit's, as the launcher's window (launcher.h)
#include "gui/univ/ctrl/textCtrl.h"

class ibDialogConnection : public wxDialog {
	ibStaticText* m_staticTextName;
	ibTextCtrl* m_textCtrlName;

	ibRadioButton* m_radioFile;
	ibRadioButton* m_radioServer;
	ibRadioButton* m_radioWeb;

	// File mode controls — the base's folder, typed or chosen in the system's folder dialog
	ibStaticText* m_staticTextFilePath;
	ibTextCtrl* m_textCtrlFilePath;
	ibButton* m_buttonFilePath;

	// Server mode controls — the cluster, server[:port], and the base's name in it
	ibStaticText* m_staticTextServer;
	ibTextCtrl* m_textCtrlServer;
	ibStaticText* m_staticTextDataBase;
	ibTextCtrl* m_textCtrlDataBase;

	// Web mode controls — the publication's link, as the browser's address bar shows it
	ibStaticText* m_staticTextAddress;
	ibTextCtrl* m_textCtrlAddress;

	wxBoxSizer* m_sizerFileMode;
	wxBoxSizer* m_sizerServerMode;
	wxBoxSizer* m_sizerWebMode;

	ibButton* m_buttonTestConnection;
	ibButton* m_buttonSaveConnection;

	void UpdateModeVisibility();

public:

	void InitConnection(const wxString& strName, const ibListInfo& info);

	ibDialogConnection(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Configure connect"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(500, 320), long style = wxSYSTEM_MENU | wxCLOSE_BOX | wxCAPTION | wxCLIP_CHILDREN | wxTAB_TRAVERSAL);

	wxString GetNameIB() const { return m_textCtrlName->GetValue(); }
	// The connection as the dialog's fields say it.
	ibListInfo GetListInfo() const;

protected:
	void OnModeChanged(wxCommandEvent& event);
	void FilePathOnButtonClick(wxCommandEvent& event);
	void TestConnectionOnButtonClick(wxCommandEvent& event);
	void SaveConnectionOnButtonClick(wxCommandEvent& event);
};

#endif // !_CONNECTION_DB_
