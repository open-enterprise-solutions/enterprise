////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

//common 
#include "core/frontend/docView/docManager.h"
#include "theme/luna_auitabart.h"
#include "theme/luna_dockart.h"
#include "frontend/objinspect/objinspect.h"
#include "frontend/visualView/visualEditor.h"
#include "frontend/mainFrame.h"

#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/clipbrd.h>
#include <wx/cmdproc.h>

#include <wx/ffile.h>
#include <wx/tokenzr.h>
#include <wx/sysopt.h>

//***********************************************************************************
//*                                 Constructor                                     *
//***********************************************************************************

wxAuiDocMDIFrame::wxAuiDocMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
	: wxDocParentFrameAnyBase(this), m_objectInspector(NULL)
{
	Create(title, pos, size, style | wxNO_FULL_REPAINT_ON_RESIZE);
}

#include "core/compiler/value.h"

bool wxAuiDocMDIFrame::Create(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	if (!wxAuiMDIParentFrame::Create(NULL, wxID_ANY, title, pos, size, style, name))
		return false;

	m_docManager = new CDocManager;

	this->Bind(wxEVT_MENU, &wxAuiDocMDIFrame::OnExit, this, wxID_EXIT);
	this->Bind(wxEVT_CLOSE_WINDOW, &wxAuiDocMDIFrame::OnCloseWindow, this);

	this->SetArtProvider(new CLunaTabArt());

	// notify wxAUI which valueForm to use
	m_mgr.SetManagedWindow(this);
	m_mgr.SetArtProvider(new CLunaDockArt());

	SetIcon(wxICON(oes));
	return true;
}

wxWindow* wxAuiDocMDIFrame::CreateChildFrame(CView* view, const wxPoint& pos, const wxSize& size, long style)
{
	// create a child valueForm of appropriate class for the current mode
	CDocument* document = view->GetDocument();

	if ((style & wxCREATE_SDI_FRAME) != 0) {

		wxWindow* parent = wxTheApp->GetTopWindow();

		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					parent = window; break;
				}
			}
		}

		wxDialogDocChildFrame* subframe = new wxDialogDocChildFrame(document, view, parent, wxID_ANY, document->GetTitle(), pos, size, style & ~wxCREATE_SDI_FRAME);
		subframe->SetIcon(document->GetIcon());

		subframe->Iconize(false); // restore the window if minimized
		subframe->SetFocus();     // focus on my window
		subframe->Raise();        // bring window to front
		subframe->Center();

		subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
		return subframe;
	}

	wxAuiMDIDocChildFrame* subframe = new wxAuiMDIDocChildFrame(document, view, mainFrame, wxID_ANY, document->GetTitle(), pos, size, style);
	subframe->SetIcon(document->GetIcon());

	subframe->Iconize(false); // restore the window if minimized
	subframe->SetFocus();     // focus on my window
	subframe->Raise();        // bring window to front
	subframe->Maximize();

	subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	return subframe;
}

// bring window to front
void wxAuiDocMDIFrame::Raise()
{
#if __WXMSW__
	::keybd_event(0, 0, 0, 0); // Simulate a key press
#endif
	wxAuiMDIParentFrame::Raise();
}

wxAuiDocMDIFrame::~wxAuiDocMDIFrame()
{
	if (s_instance == this)
		s_instance = NULL;
	
	//destroy manager
	docManagerDestroy();

	// deinitialize the valueForm manager
	m_mgr.UnInit();
}