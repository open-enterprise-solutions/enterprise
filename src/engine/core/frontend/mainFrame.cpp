////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

//common 
#include "common/docManager.h"
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

CMainFrame::CMainFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
	: wxDocParentFrameAnyBase(this)
{
	Create(title, pos, size, style);
}

#include "compiler/value.h"

bool CMainFrame::Create(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	if (!wxAuiMDIParentFrame::Create(NULL, wxID_ANY, title, pos, size, style, name))
		return false;

	m_docManager = new CDocManager();

	this->Bind(wxEVT_MENU, &CMainFrame::OnExit, this, wxID_EXIT);
	this->Bind(wxEVT_CLOSE_WINDOW, &CMainFrame::OnCloseWindow, this);

	this->SetArtProvider(new CLunaTabArt());

	// notify wxAUI which valueForm to use
	m_mgr.SetManagedWindow(this);
	m_mgr.SetArtProvider(new CLunaDockArt());

	SetIcon(wxICON(oes));
	return true;
}

CDocChildFrame *CMainFrame::CreateChildFrame(CView *view, const wxPoint &pos, const wxSize &size, long style)
{
	CDocument *document = view->GetDocument();
	
	// create a child valueForm of appropriate class for the current mode
	CDocChildFrame *subvalueFrame = new CDocChildFrame(document, view, CMainFrame::Get(), wxID_ANY, document->GetTitle(), pos, size, style);

	subvalueFrame->SetIcon(document->GetIcon());

	subvalueFrame->Iconize(false); // restore the window if minimized
	subvalueFrame->SetFocus();     // focus on my window
	subvalueFrame->Raise();        // bring window to front
	subvalueFrame->Maximize();

	subvalueFrame->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	return subvalueFrame;
}

// bring window to front
void CMainFrame::Raise()
{
#if __WXMSW__
	keybd_event(0, 0, 0, 0); // Simulate a key press
#endif
	wxAuiMDIParentFrame::Raise();
}

CMainFrame::~CMainFrame()
{
	objectInspectorDestroy();
	docManagerDestroy();
	
	// deinitialize the valueForm manager
	m_mgr.UnInit();
}