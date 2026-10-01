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
#include <wx/bmpbndl.h>

// ⭐ THE LAUNCHER'S OWN PICTURES. It links the backend alone, so the frontend's art provider — which answers the
// stock ids in the set's manner in the designer and the running application — never reaches it, and a stock id
// asked for here drew wx's own (green arrows, a blue book, a clipboard for Edit). PNG in Base64 in
// launcher_res.cpp, drawn from tools/pictures/render.js like every other picture.
enum class ibLauncherPicture { Enterprise, Designer, Web, Add, Edit, Delete, Exit };
wxBitmapBundle ibGetLauncherPicture(ibLauncherPicture which);

struct CListInfo {
	bool m_bFileMode = false;
	wxString m_strFilePath;
	wxString m_strServer;
	wxString m_strDatabase;
	wxString m_strUser;
	wxString m_strPassword;
	wxString m_strPort;
};

class ibFrameLauncher : public wxFrame {

	wxListBox* m_listIBwnd;
	wxStaticText* m_staticDBName;
	wxButton* m_buttonEnterprise;
	wxButton* m_buttonDesigner;
	wxButton* m_buttonWeb;
	wxButton* m_buttonAdd;
	wxButton* m_buttonEdit;
	wxButton* m_buttonDelete;
	wxButton* m_buttonExit;

	std::vector<std::pair<wxString, CListInfo>> m_listInfoBase;

public:

	void LoadListIB();
	void SaveListIB();

	ibFrameLauncher(wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Launch OES"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize(520, 420), long style = wxDEFAULT_FRAME_STYLE | wxRESIZE_BORDER);
	virtual ~ibFrameLauncher();

	//events:
protected:

	void OnSelectedList(wxCommandEvent& event);
	void OnSelectedDClickList(wxCommandEvent& event);

	void OnButtonEnterprise(wxCommandEvent& event);
	void OnButtonDesigner(wxCommandEvent& event);
	void OnButtonWeb(wxCommandEvent& event);

	void OnButtonAdd(wxCommandEvent& event);
	void OnButtonEdit(wxCommandEvent& event);
	void OnButtonDelete(wxCommandEvent& event);

	void OnButtonClose(wxCommandEvent& event);

private:

	// Start the application `appName` (enterprise, designer) on the selected base and close — or, when it could
	// not be started, say so and STAY: the launcher closing either way made a failed start look exactly like a
	// click that did nothing.
	void StartApplication(const wxString& appName);
};

#endif