////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

//application data
#include "backend/appData.h"

#include "frontend/session/guiSession.h"

//common 
#include "frontend/docView/docManager.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "frontend/win/theme/luna_tabart.h"
#include "frontend/win/theme/luna_dockart.h"

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

ibFrontendDocMDIFrame* ibFrontendDocMDIFrame::s_instance = nullptr;

void ibFrontendDocMDIFrame::InitFrame(ibFrontendDocMDIFrame* frame)
{
	if (s_instance == nullptr && frame != nullptr) {
		s_instance = frame;
		wxTheApp->SetTopWindow(s_instance);
	}
}

bool ibFrontendDocMDIFrame::ShowFrame()
{
	if (s_instance != nullptr && !s_instance->IsShown() && !ibSession::IsCurrentForceExit()) {

		s_instance->CreateGUI();

		s_instance->SetClientSize(s_instance->FromDIP(wxSize(800, 600)));
		s_instance->SetFocus();

		s_instance->Center();

		if (s_instance->Show()) {
			s_instance->Raise();
			return true;
		}

		return false;
	}

	return false;
}

void ibFrontendDocMDIFrame::DestroyFrame()
{
	if (s_instance != nullptr) {
		s_instance->Destroy();
	}
}

//***********************************************************************************
//*                                 Constructor                                     *
//***********************************************************************************

ibFrontendDocMDIFrame::ibFrontendDocMDIFrame(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName) : wxDocParentFrameAnyBase(this),
	m_objectInspector(nullptr), m_docToolbar(nullptr), m_mainFrameToolbar(nullptr),
	m_callRaiseFrame(false), m_callUpdateFrameManager(false)
{
	Create(title, pos, size, style | wxNO_FULL_REPAINT_ON_RESIZE);
}

#include "backend/compiler/value.h"

bool ibFrontendDocMDIFrame::Create(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName)
{
	if (!wxAuiMDIParentFrame::Create(nullptr, wxID_ANY, title, pos, size, style, strName))
		return false;

	m_docManager = nullptr;

	this->Bind(wxEVT_MENU, &ibFrontendDocMDIFrame::OnExit, this, wxID_EXIT);
	this->Bind(wxEVT_CLOSE_WINDOW, &ibFrontendDocMDIFrame::OnCloseWindow, this);

	this->SetArtProvider(new wxAuiLunaTabArt());

	// notify wxAUI which valueForm to use
	m_mgr.SetManagedWindow(this);
	m_mgr.SetArtProvider(new wxAuiLunaDockArt());

	// Disable live pane-border resize — wxAUI_MGR_LIVE_RESIZE is part
	// of wxAUI_MGR_DEFAULT and triggers a full Layout pass through
	// every pane on every mouse-move during border drag, which is
	// expensive on complex pane content (Syntax Helper especially)
	// and shows up as a "jelly" lag. Ghost-rect drag + commit on
	// mouse-up matches VS / Eclipse and stays lag-free.
	m_mgr.SetFlags(m_mgr.GetFlags() & ~wxAUI_MGR_LIVE_RESIZE);

#ifdef __WXMSW__
	SetIcon(wxICON(oes));
#endif
	return true;
}

ibFrontendWindow* ibFrontendDocMDIFrame::CreateChildFrame(ibMetaView* view, const wxPoint& pos, const wxSize& size, long style)
{
	// create a child valueForm of appropriate class for the current mode
	ibMetaDocument* document = view->GetDocument();

	if ((style & wxCREATE_SDI_FRAME) != 0) {

		wxWindow* parent = wxTheApp->GetTopWindow();

		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					parent = window; break;
				}
			}
		}

		ibDialogDocChildFrame* subframe = new ibDialogDocChildFrame(document, view, parent, wxID_ANY, document->GetTitle(), pos, size, style & ~wxCREATE_SDI_FRAME);
		subframe->SetIcon(document->GetIcon());
		subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
		subframe->Center();
		return subframe;
	}

	CAuiDocChildFrame* subframe = new CAuiDocChildFrame(document, view, s_instance, wxID_ANY, document->GetTitle(), pos, size, style);
	subframe->SetIcon(document->GetIcon());
	subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	return subframe;
}

void ibFrontendDocMDIFrame::RefreshFrame()
{
	if (m_docManager != nullptr) {
		for (auto& doc : m_docManager->GetDocumentsVector())
			doc->UpdateAllViews();
	}

	Refresh();
}

void ibFrontendDocMDIFrame::RaiseFrame()
{
	if (!m_callRaiseFrame && ibFrontendDocMDIFrame::IsFocusable()) {
		CallAfter([&]() {
			if (!ibSession::IsCurrentForceExit())
				ibFrontendDocMDIFrame::Raise();
			m_callRaiseFrame = false;
			}
		);
		m_callRaiseFrame = true;
	}
}

#if wxUSE_MENUS
void ibFrontendDocMDIFrame::SetMenuBar(wxMenuBar* pMenuBar)
{
	if (m_pMyMenuBar == nullptr) {

		//Remove the Window menu from the old menu bar
		RemoveWindowMenu(GetMenuBar());

		//Add the Window menu to the new menu bar.
		AddWindowMenu(pMenuBar);
	}

	wxFrame::SetMenuBar(pMenuBar);
}
#endif // wxUSE_MENUS

wxAuiMDIClientWindow* ibFrontendDocMDIFrame::OnCreateClient()
{
	class wxAuiMDIClientWindowImpl : public wxAuiMDIClientWindow {
	public:
		wxAuiMDIClientWindowImpl() : wxAuiMDIClientWindow() {}
		wxAuiMDIClientWindowImpl(wxAuiMDIParentFrame* parent, long style = 0) : wxAuiMDIClientWindow(parent, style) {
#ifdef __WXOSX__
			// Use the system background instead of the old hard-coded blue —
			// respects dark/light mode.
			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
			SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#else
			// Powder-blue MDI workspace — interior-design dominant
			// chrome tone (the "walls" of the application). Forms /
			// docs sit on this as a calm cool backdrop. The historical
			// dark-navy AppWorkspace would otherwise leak through, so
			// we override both paint + erase below.
			SetBackgroundColour(wxColour(184, 201, 212));  // #B8C9D4 powder blue
#endif
			Bind(wxEVT_PAINT, &wxAuiMDIClientWindowImpl::OnPaint, this);
			Bind(wxEVT_ERASE_BACKGROUND, &wxAuiMDIClientWindowImpl::OnEraseBackground, this);
		}

	protected:

		//A general selection function
		virtual int DoModifySelection(size_t n, bool events) {
			wxAuiNotebook::Freeze();
			const int selection = wxAuiNotebook::DoModifySelection(n, events);
			wxAuiNotebook::Thaw();
			return selection;
		}

		void OnPaint(wxPaintEvent& event) {
			wxPaintDC dc(this);
			dc.SetBackground(wxBrush(GetBackgroundColour()));
			dc.Clear();
			event.Skip();
		}

		void OnEraseBackground(wxEraseEvent& event) {
			wxDC* dc = event.GetDC();
			if (dc) {
				dc->SetBackground(wxBrush(GetBackgroundColour()));
				dc->Clear();
			}
		}
	};

	return new wxAuiMDIClientWindowImpl(this);
}

// bring window to front
void ibFrontendDocMDIFrame::Raise()
{
#if __WXMSW__
	// Simulate a key press
	::keybd_event((BYTE)0, 0, 0 /* key press */, 0);
	::keybd_event((BYTE)0, 0, KEYEVENTF_KEYUP, 0);
#endif

	wxAuiMDIParentFrame::Raise();
}

bool ibFrontendDocMDIFrame::Destroy()
{
	wxAuiMDIClientWindow* client_window = GetClientWindow();
	wxCHECK_MSG(client_window, false, wxS("Missing MDI Client Window"));

	client_window->Freeze();

	bool success = m_docManager != nullptr ?
		m_docManager->CloseDocuments() : true;

	if (success && m_docManager != nullptr) {
#if wxUSE_CONFIG
		m_docManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
		wxDELETE(m_docManager);
	}

	client_window->Thaw();

	return success &&
		wxAuiMDIParentFrame::Destroy();
}

ibFrontendDocMDIFrame::~ibFrontendDocMDIFrame()
{
	if (s_instance == this) s_instance = nullptr;

	// Notify the owning session so its observer-pointer doesn't dangle when
	// wx destroys top-level frames before wxApp::OnExit fires Session::Close.
	if (m_guiSession != nullptr) m_guiSession->DetachFrame(this);

	// deinitialize the valueForm manager
	m_mgr.UnInit();
}

void ibFrontendDocMDIFrame::UpdateFrameManager()
{
	unsigned int view_count = 0;

	if (m_docManager != nullptr) {
		for (auto& doc : m_docManager->GetDocumentsVector()) {
			for (auto& view : doc->GetViewsVector()) view_count++;
		}
	}

	if (view_count == 0) m_docToolbar->Clear();

	wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_docToolbar);
	infoToolBar.Show(m_docToolbar->GetToolCount() > 0);

	infoToolBar.BestSize(m_docToolbar->GetSize());
	infoToolBar.FloatingSize(
		m_docToolbar->GetSize().x,
		m_docToolbar->GetSize().y + 25
	);

	m_mgr.Update();
	m_callUpdateFrameManager = false;
}

//********************************************************************************
//*                                    System                                    *
//********************************************************************************

void ibFrontendDocMDIFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	this->Close();
}

void ibFrontendDocMDIFrame::OnCloseWindow(wxCloseEvent& event)
{
	bool allowClose = event.CanVeto() ? AllowClose() : true;
	// The user decided not to close finally, abort.
	if (allowClose) event.Skip();
	// Just skip the event, base class handler will destroy the window.
	else event.Veto();
}