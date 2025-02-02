////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

//common 
#include "frontend/docView/docManager.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "frontend/win/theme/luna_tabart.h"
#include "frontend/win/theme/luna_dockart.h"

#include <wx/artprov.h>
#include <wx/config.h>
#include <wx/clipbrd.h>
#include <wx/cmdproc.h>

#include <wx/ffile.h>
#include <wx/tokenzr.h>
#include <wx/sysopt.h>

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

CDocMDIFrame* CDocMDIFrame::s_instance = nullptr;

void CDocMDIFrame::InitFrame(CDocMDIFrame* frame)
{
	if (s_instance == nullptr && frame != nullptr) {		
		s_instance = frame;
		wxTheApp->SetTopWindow(s_instance);
		s_instance->CreateGUI();
	}
}

bool CDocMDIFrame::ShowFrame()
{
	if (s_instance == nullptr)
		return false;

	if (!s_instance->IsShown()) {
		s_instance->SetFocus();
		s_instance->Raise();
		s_instance->Maximize();
		return s_instance->Show();
	}

	return false;
}

void CDocMDIFrame::DestroyFrame()
{
	if (s_instance != nullptr) {
		s_instance->Destroy();
	}
}

//***********************************************************************************
//*                                 Constructor                                     *
//***********************************************************************************

CDocMDIFrame::CDocMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName)
	: wxDocParentFrameAnyBase(this), m_objectInspector(nullptr)
{
	Create(title, pos, size, style | wxNO_FULL_REPAINT_ON_RESIZE);
}

#include "backend/compiler/value/value.h"

bool CDocMDIFrame::Create(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName)
{
	if (!wxAuiMDIParentFrame::Create(nullptr, wxID_ANY, title, pos, size, style, strName))
		return false;

	m_docManager = nullptr;

	this->Bind(wxEVT_MENU, &CDocMDIFrame::OnExit, this, wxID_EXIT);
	this->Bind(wxEVT_CLOSE_WINDOW, &CDocMDIFrame::OnCloseWindow, this);

	this->SetArtProvider(new wxAuiLunaTabArt());

	// notify wxAUI which valueForm to use
	m_mgr.SetManagedWindow(this);
	m_mgr.SetArtProvider(new wxAuiLunaDockArt());

	SetIcon(wxICON(oes));
	return true;
}

wxWindow* CDocMDIFrame::CreateChildFrame(CMetaView* view, const wxPoint& pos, const wxSize& size, long style)
{
	// create a child valueForm of appropriate class for the current mode
	CMetaDocument* document = view->GetDocument();

	if ((style & wxCREATE_SDI_FRAME) != 0) {

		wxWindow* parent = wxTheApp->GetTopWindow();

		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					parent = window; break;
				}
			}
		}

		CDialogDocChildFrame* subframe = new CDialogDocChildFrame(document, view, parent, wxID_ANY, document->GetTitle(), pos, size, style & ~wxCREATE_SDI_FRAME);
		subframe->SetIcon(document->GetIcon());

		subframe->Iconize(false); // restore the window if minimized
		subframe->SetFocus();     // focus on my window
		subframe->Raise();        // bring window to front
		subframe->Center();

		subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
		return subframe;
	}

	CAuiDocChildFrame* subframe = new CAuiDocChildFrame(document, view, s_instance, wxID_ANY, document->GetTitle(), pos, size, style);
	subframe->SetIcon(document->GetIcon());

	subframe->Iconize(false); // restore the window if minimized
	subframe->SetFocus();     // focus on my window
	subframe->Raise();        // bring window to front
	subframe->Maximize();

	subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	return subframe;
}

void CDocMDIFrame::RefreshFrame()
{
	for (auto doc : docManager->GetDocumentsVector()) {
		doc->UpdateAllViews();
	}

	Refresh();
}

// bring window to front
void CDocMDIFrame::Raise()
{
#if __WXMSW__
	::keybd_event(0, 0, 0, 0); // Simulate a key press
#endif
	wxAuiMDIParentFrame::Raise();
}

bool CDocMDIFrame::Destroy()
{
	if (m_docManager != nullptr) {
		if (!m_docManager->CloseDocuments()) return false;
#if wxUSE_CONFIG
		m_docManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
		wxDELETE(m_docManager);
	}

	return wxAuiMDIParentFrame::Destroy();
}

CDocMDIFrame::~CDocMDIFrame()
{
	if (s_instance == this) s_instance = nullptr;
	// deinitialize the valueForm manager
	m_mgr.UnInit();
}

//********************************************************************************
//*                                    System                                    *
//********************************************************************************

void CDocMDIFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	this->Close();
}

void CDocMDIFrame::OnCloseWindow(wxCloseEvent& event)
{
	bool allowClose = event.CanVeto() ? AllowClose() : true;
	// The user decided not to close finally, abort.
	if (allowClose) event.Skip();
	// Just skip the event, base class handler will destroy the window.
	else event.Veto();
}