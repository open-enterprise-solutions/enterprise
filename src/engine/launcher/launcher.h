#ifndef __LAUNCHER_WND_H__
#define __LAUNCHER_WND_H__

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
#include <wx/bmpbndl.h>
#include "core/localization/dictionary.h"   // _() — the dialog's words
#include "gui/univ/ctrl/button.h"       // the window's controls — the kit's (gui.h)
#include "gui/univ/ctrl/listBox.h"
#include "gui/univ/ctrl/staticText.h"

// ⭐ THE LAUNCHER'S OWN PICTURES. It links the backend alone, so the frontend's art provider — which answers the
// stock ids in the set's manner in the designer and the running application — never reaches it, and a stock id
// asked for here drew wx's own (green arrows, a blue book, a clipboard for Edit). PNG in Base64 in
// launcher_res.cpp, drawn from tools/pictures/render.js like every other picture.
enum class ibLauncherPicture { Enterprise, Designer, Web, Add, Edit, Delete, Exit };
wxBitmapBundle ibGetLauncherPicture(ibLauncherPicture which);

// HOW A BASE IS REACHED — the three a client knows: a file base, opened in its own process; a base of the server
// cluster, over its port; a web server's publication, by the link a browser shows for it. The last two are one road
// in the client (--address, protocol/connectionServer.h), the same protocol the server speaks.
enum class ibLauncherConnection { File, Server, Web };

struct ibListInfo {
	ibLauncherConnection m_connection = ibLauncherConnection::File;
	wxString m_strFilePath;   // File: the base's folder
	wxString m_strServer;     // Server: the cluster, server[:port] — the port left out is the cluster's own…
	wxString m_strDatabase;   // …and the base's name in it
	wxString m_strAddress;    // Web: the publication's link, copied from the browser's address bar

	// The address a client is started on (--address): the cluster's oes://server[:port]/<base>, the web's link as it
	// is written; empty for a file base.
	wxString GetAddress() const;
};

class ibFrameLauncher : public wxFrame {

	ibListBox* m_listIBwnd;
	ibStaticText* m_staticDBName;
	ibButton* m_buttonEnterprise;
	ibButton* m_buttonDesigner;
	ibButton* m_buttonWeb;
	ibButton* m_buttonAdd;
	ibButton* m_buttonEdit;
	ibButton* m_buttonDelete;
	ibButton* m_buttonExit;

	std::vector<std::pair<wxString, ibListInfo>> m_listInfoBase;

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