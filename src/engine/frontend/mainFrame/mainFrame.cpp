////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

//application data
#include "backend/appData.h"
#include "backend/session/sessionRegistry.h"   // OnReload subscription

#include "frontend/session/guiSession.h"

#include <wx/msgdlg.h>

//common 
#include "frontend/docView/docView.h"
#include "frontend/mainFrame/objinspect/objinspect.h"

#include "frontend/win/theme/luna_tabart.h"
#include "frontend/win/theme/luna_dockart.h"

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

ibFrontendMainFrame* ibFrontendMainFrame::s_instance = nullptr;

void ibFrontendMainFrame::InitFrame(ibFrontendMainFrame* frame)
{
	if (s_instance == nullptr && frame != nullptr) {
		s_instance = frame;
		wxTheApp->SetTopWindow(s_instance);
	}
}


//***********************************************************************************
//*                                 Constructor                                     *
//***********************************************************************************

ibFrontendMainFrame::ibFrontendMainFrame(ibSessionHolder&& holder,
	const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName) : ibBackendDocFrame(std::move(holder)),
	ibDocParentFrameAnyBase(this),
	m_objectInspector(nullptr), m_mainFrameToolbar(nullptr), m_docToolbar(nullptr),
	m_callRaiseFrame(false), m_callUpdateFrameManager(false)
{
	Create(title, pos, size, style | wxNO_FULL_REPAINT_ON_RESIZE);

	// Claim the process's main-window slot.
	InitFrame(this);

	// Admin "reload clients" on our session's row. The window is what
	// reacts — it tells the user and closes itself, and closing releases
	// the holder, which ends the session. Backend stays UI-free: it only
	// says "this session should reload", never touches a window.
	if (auto* reg = ibApplicationData::GetSessionRegistry()) {
		ibSession* const self = GetSession();
		reg->OnReload([self](ibSession* target) {
			if (target != self || wxTheApp == nullptr) return;
			wxTheApp->CallAfter([]() {
				wxMessageBox(_("Session reloaded by an administrator. The application will close - please re-open it from the launcher."),
					wxTheApp->GetAppDisplayName(), wxOK | wxICON_INFORMATION);
				if (auto* frame = ibFrontendMainFrame::GetFrame())
					frame->Close(true);
			});
		});
	}
}

#include "backend/compiler/value.h"

bool ibFrontendMainFrame::Create(const wxString& title,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& strName)
{
	if (!wxAuiMDIParentFrame::Create(nullptr, wxID_ANY, title, pos, size, style, strName))
		return false;

	m_docManager = nullptr;

	this->Bind(wxEVT_MENU, &ibFrontendMainFrame::OnExit, this, wxID_EXIT);
	this->Bind(wxEVT_CLOSE_WINDOW, &ibFrontendMainFrame::OnCloseWindow, this);

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

ibFrontendWindow* ibFrontendMainFrame::CreateChildFrame(ibView* view, const wxPoint& pos, const wxSize& size, long style)
{
	// create a child valueForm of appropriate class for the current mode.
	// Parameter and back-ref type relaxed from ibMetaView*/ibMetaDocument*
	// to ibView*/ibDocument* so the factory can serve plain (non-meta)
	// documents too — required after AuditLog / Text / Help were rebased
	// off the meta hierarchy. The child-frame ctors already accept the
	// base ibDocument*.
	ibDocument* document = view->GetDocument();

	if ((style & wxCREATE_SDI_FRAME) != 0) {

		wxWindow* parent = wxTheApp->GetTopWindow();

		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					parent = window; break;
				}
			}
		}

		wxIcon docIcon = document->GetIcon();

		ibDialogDocChildFrame* subframe = new ibDialogDocChildFrame(document, view, parent, wxID_ANY, document->GetTitle(), pos, size, style & ~wxCREATE_SDI_FRAME);
		if (docIcon.IsOk())
			subframe->SetIcon(docIcon);
		subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
		subframe->Center();
		return subframe;
	}

	wxIcon docIcon = document->GetIcon();

	ibAuiDocChildFrame* subframe = new ibAuiDocChildFrame(document, view, s_instance, wxID_ANY, document->GetTitle(), pos, size, style);
	if (docIcon.IsOk())
		subframe->SetIcon(docIcon);
	subframe->SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	return subframe;
}

void ibFrontendMainFrame::RefreshFrame()
{
	if (m_docManager != nullptr) {
		for (auto& doc : m_docManager->GetDocumentsVector())
			doc->UpdateAllViews();
	}

	Refresh();
}

void ibFrontendMainFrame::RaiseFrame()
{
	if (!m_callRaiseFrame && ibFrontendMainFrame::IsFocusable()) {
		CallAfter([&]() {
			if (!ibSession::IsCurrentForceExit())
				ibFrontendMainFrame::Raise();
			m_callRaiseFrame = false;
			}
		);
		m_callRaiseFrame = true;
	}
}

#if wxUSE_MENUS
void ibFrontendMainFrame::SetMenuBar(wxMenuBar* pMenuBar)
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

wxAuiMDIClientWindow* ibFrontendMainFrame::OnCreateClient()
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
			// Powder-blue workspace — interior-design dominant
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
void ibFrontendMainFrame::Raise()
{
#if __WXMSW__
	// Simulate a key press
	::keybd_event((BYTE)0, 0, 0 /* key press */, 0);
	::keybd_event((BYTE)0, 0, KEYEVENTF_KEYUP, 0);
#endif

	wxAuiMDIParentFrame::Raise();
}

bool ibFrontendMainFrame::Show(bool show)
{
	if (!show)
		return wxAuiMDIParentFrame::Show(false);

	// Already up — nothing to build, nothing to ask.
	if (IsShown())
		return true;

	// GUI FIRST, then the runtime, then the question. Order matters and it
	// is not arbitrary: AllowRun is where BeforeStart / OnStart run and
	// where the Designer loads its metadata tree, and all of that reaches
	// into panes that CreateGUI builds. Asking before building leaves the
	// gate dereferencing panes that do not exist yet.
	CreateGUI();

	if (!EnsureRuntime()) return false;
	if (!AllowRun()) return false;

	// The window's own tabs come AFTER the start-up script, which is the natural order:
	// BeforeStart / OnStart run on a bare window, and a script that vetoes the start never
	// gets a home page built for nothing. Being FIRST no longer depends on being created
	// first — the start page's tab is LOCKED, and wx keeps locked tabs ahead of every normal
	// one no matter when they joined (docs/home-page.md § 5).
	CreateStartupPage();

	SetClientSize(FromDIP(wxSize(800, 600)));
	SetFocus();
	Center();

	if (!wxAuiMDIParentFrame::Show(true))
		return false;
	Raise();
	return true;
}

bool ibFrontendMainFrame::AllowClose()
{
	// Each open document gets to refuse (unsaved data, an edit it will not
	// abandon), and one refusal stops the close with nothing torn down.
	// force=false is the point: this is the asking path. The unconditional
	// close of whatever is still open lives in Destroy().
	if (m_docManager == nullptr)
		return true;

	// Mark WHO is asking. Both this gate and a tab's own [x] reach a view as
	// OnClose(deleteWindow = false) — indistinguishable from inside. A document that
	// belongs to the WINDOW (the start page) refuses the user's [x] and must not refuse
	// the window; this flag is what lets it tell the two apart.
	m_closingWindow = true;
	const bool closed = m_docManager->CloseDocuments(false);
	m_closingWindow = false;

	return closed;
}

bool ibFrontendMainFrame::Destroy()
{
	wxAuiMDIClientWindow* client_window = GetClientWindow();
	wxCHECK_MSG(client_window, false, wxS("Missing MDI Client Window"));

	client_window->Freeze();

	// Past the point of no return — the close gate already asked the
	// documents and they agreed. Anything still open at this point (a
	// document opened by a script DURING teardown, or a forced close that
	// never consulted the gate) goes down unconditionally: refusing here
	// would leave the frame alive with its session already ending.
	if (m_docManager != nullptr) {
		// Same signal as in AllowClose — the forced close still ASKS each view
		// (DeleteAllViews → Close(false)), so the window-owned documents must see that it
		// is the window closing them, not a user gesture.
		m_closingWindow = true;
		m_docManager->CloseDocuments(true);
#if wxUSE_CONFIG
		m_docManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
		wxDELETE(m_docManager);
	}

	client_window->Thaw();

	return wxAuiMDIParentFrame::Destroy();
}

ibFrontendMainFrame::~ibFrontendMainFrame()
{
	if (s_instance == this) s_instance = nullptr;

	// The session's back-link is cleared by ~ibBackendDocFrame, which
	// then releases the holder — that release is what ends the session.

	// deinitialize the valueForm manager
	m_mgr.UnInit();
}

void ibFrontendMainFrame::UpdateFrameManager()
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

void ibFrontendMainFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	this->Close();
}

void ibFrontendMainFrame::OnCloseWindow(wxCloseEvent& event)
{
	// The close sequence lives here, inside the window's own close, and
	// both roads run it: the user's [X] raises this event directly, and
	// session->Close(force) raises it through frame->Close(force). One
	// place, one order, and the answer travels back out on its own — a
	// veto here makes wxWindow::Close return false, which is what the
	// session hands to its caller.
	//
	// wx carries the force flag on the event: a close that cannot be
	// vetoed (kick, process shutdown) is precisely a forced close, so
	// nobody is asked.
	const bool force = !event.CanVeto();

	if (!force && !AllowClose()) {
		event.Veto();
		return;
	}

	// Down it goes. Deliberately NOT event.Skip(): skipping hands the close
	// to the base wxAuiMDIParentFrame::OnClose, which runs a SECOND closing
	// policy over the tabs (CloseAll) on top of the one that just ran here —
	// and on a refusal it calls Veto() without asking whether the event may
	// be vetoed at all, which asserts on every forced close.
	//
	// The tabs are ours: the asking pass is AllowClose() above, the
	// unconditional pass is Destroy() below. Nobody else needs to close them.
	// Destroy() is exactly what the skipped-to default handler would end up
	// calling, so nothing is lost by naming it here — and the window's
	// destructor releases the holder, which ends the session.
	Destroy();
}