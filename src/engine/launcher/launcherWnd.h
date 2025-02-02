#ifndef __LAUNCHER_WND_H__
#define __LAUNCHER_WND_H__

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/string.h>
#include <wx/listbox.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/sizer.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/button.h>
#include <wx/frame.h>

class CFrameLauncher : public wxFrame {

	wxListBox* m_listIBwnd;
	wxStaticText* m_staticPath;
	wxButton* m_buttonEnterprise;
	wxButton* m_buttonDesigner;
	wxButton* m_buttonAdd;
	wxButton* m_buttonEdit;
	wxButton* m_buttonDelete;
	wxButton* m_buttonExit;

	struct listInfo_t {
		wxString m_strServer;
		wxString m_strDatabase;
		wxString m_strUser;
		wxString m_strPassword;
		wxString m_strPort;
	};

	std::vector<std::pair<wxString, listInfo_t>> m_listInfoBase;

public:

	void LoadIBList();
	void SaveIBList();

	CFrameLauncher(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Launch OES"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(450, 375), long style = wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER);
	virtual ~CFrameLauncher();

	//events:
protected:

	void OnSelectedList(wxCommandEvent& event);
	void OnSelectedDClickList(wxCommandEvent& event);

	void OnButtonEnterprise(wxCommandEvent& event);
	void OnButtonDesigner(wxCommandEvent& event);

	void OnButtonAdd(wxCommandEvent& event);
	void OnButtonEdit(wxCommandEvent& event);
	void OnButtonDelete(wxCommandEvent& event);

	void OnButtonClose(wxCommandEvent& event);
};

#endif