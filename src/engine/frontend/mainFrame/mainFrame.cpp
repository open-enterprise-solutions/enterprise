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
#include <wx/stdpaths.h>   // GetExecutablePath — the reload restart re-launches THIS binary
#include <wx/filename.h>

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
	// Declaration order (mainFrame.h): m_objectInspector, … m_callRaiseFrame,
	// m_callUpdateFrameManager, m_mainFrameToolbar, m_docToolbar. Members are constructed
	// in that order whatever this list says, so the list follows it.
	m_objectInspector(nullptr),
	m_callRaiseFrame(false), m_callUpdateFrameManager(false),
	m_mainFrameToolbar(nullptr), m_docToolbar(nullptr)
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
				wxMessageBox(_("Session reloaded by an administrator. The application will restart."),
					wxTheApp->GetAppDisplayName(), wxOK | wxICON_INFORMATION);

				// RELOAD MEANS COME BACK, NOT GO AWAY. The old session ends because the metadata under it
				// moved; the user asked for none of it and should find their application running, not a
				// closed window and an instruction to re-open it from the launcher. Re-launch THIS binary
				// the same way the designer launches a client — RunApplication rebuilds the connection
				// arguments from appData and carries the session's own credentials, so the new process
				// comes up on the same database as the same user, against the metadata that just landed.
				//
				// Spawned BEFORE the close: after it there is no code of ours left to spawn anything. The
				// two processes overlap for the moment the new one spends starting, which the database is
				// fine with — it is the same overlap as the designer's "start debugging".
				const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
				appData->RunApplication(exe.GetName(), /*searchDebug=*/false);

				if (auto* frame = ibFrontendMainFrame::GetFrame())
					frame->Close(true);
			});
		});

		// A WINDOW THAT VANISHES WITHOUT A WORD READS AS A CRASH. An admin kick force-closes this session
		// from another process, so the frame goes down under the user's hands; say why first. Only the
		// closing is done elsewhere (ibSession::Close raises force-exit and then asks the session to
		// close) — this listener adds the sentence, nothing else.
		//
		// The reason is what distinguishes an admin action from an ordinary exit: a process shutting
		// itself down force-closes its sessions too and leaves the reason empty, and there we stay quiet —
		// the user pressing [X] does not need to be told the application is closing.
		StartConfigWatch();

		reg->OnForceExit([self](ibSession* target) {
			if (target != self || wxTheApp == nullptr) return;
			const wxString reason = target->Reason();
			if (reason.IsEmpty()) return;
			wxTheApp->CallAfter([reason]() {
				wxMessageBox(reason, wxTheApp->GetAppDisplayName(), wxOK | wxICON_INFORMATION);
			});
		});
	}
}

#include "backend/databaseLayer/databaseQueryBuilder.h"   // reading the deployed guid out of sys_config
#include "backend/metadataConfiguration.h"                // activeMetaData — the guid this client opened with

// How often this client asks whether the deployed configuration is still the one it opened with. Ten
// minutes: often enough that nobody spends a working day on yesterday's metadata, rare enough that a
// declined reminder does not become nagging. The query is one row of one column.
static constexpr int kConfigWatchIntervalMs = 10 * 60 * 1000;

void ibFrontendMainFrame::StartConfigWatch()
{
	// Designer publishes configurations; it must not be asked to reconnect to its own work.
	if (appData == nullptr || appData->DesignerMode())
		return;
	if (activeMetaData == nullptr)
		return;

	m_configWatchGuid = activeMetaData->GetConfigGuid().str();
	if (m_configWatchGuid.IsEmpty())
		return;   // nothing to compare against — a file base opened without a deployed guid

	m_configWatchTimer.SetOwner(this);
	Bind(wxEVT_TIMER, &ibFrontendMainFrame::OnConfigWatchTimer, this, m_configWatchTimer.GetId());
	m_configWatchTimer.Start(kConfigWatchIntervalMs);
}

void ibFrontendMainFrame::OnConfigWatchTimer(wxTimerEvent& WXUNUSED(event))
{
	if (m_configWatchAsking || m_closingWindow)
		return;

	// The DEPLOYED guid — sys_config, not sys_config_save: a designer's unsaved draft is nobody's
	// business here. A transient DB error is not an event: stay quiet and ask again next tick.
	wxString deployed;
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.ExecuteIR(ibQueryIR(ibProject(ibScan(wxT("sys_config")),
			{ { ibCol(wxT("file_guid")), wxEmptyString } })));
		if (rs.Next())
			deployed = rs.GetResultString(wxT("file_guid"));
	}
	catch (...) {
		return;
	}

	if (deployed.IsEmpty() || deployed == m_configWatchGuid)
		return;

	// ASKING, NEVER TAKING. The user may be halfway through a document; the old configuration keeps
	// working, so the honest move is an offer. "No" is remembered only until the next tick — the
	// reminder returns, because the mismatch does not go away by being declined.
	m_configWatchAsking = true;
	const int answer = wxMessageBox(
		_("The configuration has been updated by an administrator.\n\n"
		  "You are still working with the version this session started on. "
		  "Restart now to pick up the new one?"),
		wxTheApp->GetAppDisplayName(), wxYES_NO | wxCENTRE | wxICON_INFORMATION, this);
	m_configWatchAsking = false;

	if (answer != wxYES)
		return;

	// Same door the admin "reload" uses: re-launch this binary with this session's connection
	// arguments, then close. Spawned before the close, because afterwards there is no code of ours
	// left to spawn anything.
	const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
	appData->RunApplication(exe.GetName(), /*searchDebug=*/false);
	Close(true);
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
			// ⚠ NO SetBackgroundStyle() HERE. wxBookCtrlBase::HasTransparentBackground() is
			// true, so wxWindowMac::Create() has already stamped this window
			// wxBG_STYLE_TRANSPARENT — and wx forbids unsetting it. The call returned false
			// in Release and asserted in Debug; it never did anything either way. What
			// actually fills the ground is the paint / erase pair bound just below.
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
			view_count += (unsigned int)doc->GetViewsVector().size();   // the loop only counted
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
///////////////////////////////////////////////////////////////////////////

#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/dcscreen.h>

bool ibFrontendMainFrame::CaptureWindow(const wxString& reason, const wxString& area,
	const wxString& format, wxMemoryBuffer& bytes, wxString& focus)
{
	bytes.SetDataLen(0);
	focus.Clear();

	// 🛑⭐⭐ ASKED, EVERY TIME, AND IN THE PERSON'S TERMS. Not "allow screen capture?" — a technical
	// verb nobody can weigh — but WHAT is being looked for and WHY, so consent is given to a purpose.
	// Whatever is on this window right now goes with the picture: counterparties, sums, somebody's
	// pay. That is not the platform's to hand over because a caller found it useful, and it is why
	// this question is here rather than in the manners of whoever asked (Max, 2026-09-04).
	// ⭐ ASKED IN A RELEASE BUILD, SILENT IN A DEBUG ONE — and the line between them is not a
	// convenience, it is WHOSE SCREEN IT IS. A shipped installation is somebody's workplace: their
	// customers, their sums, their pay, and nobody may photograph it without being told yes. A Debug
	// binary is a developer's own machine, where they ARE the assistant's counterpart and a modal
	// per capture is a click that buys nothing (Max, 2026-09-04: *"in the release version it always
	// asks; in debug you need not ask me — it is debug"*).
	//
	// ⚠ THE JOURNAL ENTRY IS NOT UNDER THIS SWITCH. Consent varies by build; the RECORD does not —
	// debugServer writes what happened either way, so even the silent path leaves a trail.
#ifdef NDEBUG
	const wxString question = wxString::Format(
		_("The assistant asks for a picture of this window:\n\n%s\n\n"
		  "Everything visible on it right now will be sent. Allow it?"), reason);

	if (ShowModalMessage(question, _("Picture of the window"), wxYES_NO | wxICON_QUESTION) != wxYES)
		return false;   // an ordinary answer, not a failure
#else
	wxUnusedVar(reason);
#endif

	// ⭐ WHICH WINDOW — chosen by the caller, because only it knows what it is after. A form standing
	// on its own (a report, a dialog) is a separate top-level window, and photographing the main
	// frame would hand back everything except the thing asked about; "screen" is for when the person
	// is moving between windows to show a sequence (Max, 2026-09-04).
	wxWindow* target = this;
	if (!area.IsSameAs(wxT("main"), false)) {
		wxWindow* active = wxGetActiveWindow();
		if (active != nullptr && active->IsShownOnScreen())
			target = active;
	}

	const bool wholeScreen = area.IsSameAs(wxT("screen"), false);

	// ⭐⭐ THE CHEAPEST AND USUALLY THE BEST ANSWER: just the part they pointed at. A desktop is huge
	// and most of it is irrelevant — the cost of an image is its PIXELS, and so is the effort of
	// finding the one row that matters in it. Cropping to the focused control plus a margin gives a
	// picture that is small AND already about the right thing (Max, 2026-09-04: *"the job is to
	// decode the picture as efficiently as possible and see the information that is needed"*).
	wxRect crop;
	if (area.IsSameAs(wxT("focus"), false)) {
		if (const wxWindow* focused = wxWindow::FindFocus()) {
			if (focused->IsShownOnScreen()) {

				const wxPoint at = focused->GetScreenPosition() - target->GetScreenPosition();
				const wxSize  of = focused->GetSize();

				// A margin, because a control read with nothing around it loses what names it: the
				// column heading above, the label to its left, the total underneath.
				const int margin = 60;
				crop = wxRect(at.x - margin, at.y - margin,
					of.GetWidth() + margin * 2, of.GetHeight() + margin * 2);
				crop = crop.Intersect(wxRect(wxPoint(0, 0), target->GetSize()));
			}
		}
	}

	// ⚠ THE WHOLE WINDOW, FRAME AND TITLE INCLUDED — because PrintWindow below draws exactly that,
	// and a canvas cut to the CLIENT area loses the difference off the right and the bottom: the
	// last column and the scrollbar, which is the half a "why is the list wrong" question is usually
	// about (seen on the second live picture, 2026-09-04).
	const wxSize size = wholeScreen ? wxGetDisplaySize() : target->GetSize();
	if (size.GetWidth() <= 0 || size.GetHeight() <= 0)
		return false;   // minimised, or not laid out yet — nothing to draw

	// ONE WINDOW, not the desktop behind it — unless the desktop is what was asked for. Narrow by
	// default on purpose: the question is about what the application shows, and everything else on
	// that screen belongs to somebody's private day.
	wxBitmap shot(size);
	{
		wxMemoryDC memory(shot);

		if (wholeScreen) {
			wxScreenDC screen;
			memory.Blit(0, 0, size.GetWidth(), size.GetHeight(), &screen, 0, 0);
		}
		else {
			// 🛑⭐⭐ THE WINDOW DRAWS ITSELF — it is NOT a copy of the screen inside its rectangle. Blitting
			// from a wxWindowDC takes whatever pixels are there, INCLUDING ANYTHING SITTING ON TOP: a
			// chat window, somebody's mail, the terminal I happened to have open. Measured on the very
			// first picture that came back (2026-09-04) — a strip of another application's output was
			// across the top of it.
			//
			// That is wrong twice. Diagnostically, an overlapped window returns rubbish instead of the
			// form being asked about. And on consent: permission was given for THIS window, so
			// whatever else is on that screen must not travel with it — the narrower thing was
			// promised and the narrower thing has to be delivered.
			//
			// PrintWindow asks the window to render into our DC directly, so overlapping windows are
			// not in it and neither is anything behind it. PW_RENDERFULLCONTENT is what makes it work
			// for composited (DWM) surfaces, which every modern control on Windows is.
			bool drawn = false;
#ifdef __WXMSW__
			if (HWND hwnd = target->GetHWND() ? (HWND)target->GetHWND() : nullptr) {
				HDC dc = (HDC)memory.GetHDC();
				if (dc != nullptr)
					drawn = ::PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT) != FALSE;
			}
#endif
			// Everywhere else — and if the window declined to draw itself — the screen copy is still
			// better than nothing; it is simply not guaranteed to be only this window.
			if (!drawn) {
				wxWindowDC window(target);
				memory.Blit(0, 0, size.GetWidth(), size.GetHeight(), &window, 0, 0);
			}
		}

		// ⭐⭐ AND THE THING THEY CLICKED ON IS RINGED. A person showing you something puts the focus
		// on it first; drawing that rectangle turns "somewhere in this list" into "this cell". It
		// costs one DrawRectangle and saves the whole exchange where they try to describe a position.
		if (!wholeScreen) {
			wxWindow* focused = wxWindow::FindFocus();
			if (focused != nullptr && focused != target && focused->IsShownOnScreen()) {

				// ⚠ WINDOW coordinates, not client ones: the canvas holds the whole window (frame and
				// title included), so a client-relative rectangle would sit above and left of the control.
				const wxPoint at = focused->GetScreenPosition() - target->GetScreenPosition();
				const wxSize  of = focused->GetSize();

				memory.SetPen(wxPen(wxColour(220, 40, 40), 2));
				memory.SetBrush(*wxTRANSPARENT_BRUSH);
				memory.DrawRectangle(at.x - 1, at.y - 1, of.GetWidth() + 2, of.GetHeight() + 2);
			}
		}
	}

	// ⭐ WHAT HAS THE FOCUS, IN WORDS — the same fact as the rectangle, in the form a filter can use.
	// The platform's own ActiveWindow() names the FORM, which is what a caller can then look up in
	// the metadata; the control class and label say which part of it.
	{
		wxString said;

		if (const wxWindow* focused = wxWindow::FindFocus()) {
			said << _("control: ") << focused->GetClassInfo()->GetClassName();
			if (!focused->GetLabel().IsEmpty())
				said << wxT(" '") << focused->GetLabel() << wxT("'");
			if (!focused->GetName().IsEmpty() && focused->GetName() != wxT("panel"))
				said << wxT(" (") << focused->GetName() << wxT(")");
		}

		// WHICH WINDOW IT WAS, by the title the person reads on it — "Goods (list)", "Receipt 000012".
		// That is the same thing the platform's ActiveWindow() names, said in the words already on
		// screen, and it needs nothing from the metadata to be useful.
		if (const wxTopLevelWindow* top = wxDynamicCast(wxGetTopLevelParent(target), wxTopLevelWindow)) {
			if (!top->GetTitle().IsEmpty()) {
				if (!said.IsEmpty()) said << wxT("; ");
				said << _("window: '") << top->GetTitle() << wxT("'");
			}
		}

		const wxPoint mouse = ScreenToClient(wxGetMousePosition());
		if (!said.IsEmpty()) said << wxT("; ");
		said << wxString::Format(_("pointer at %d,%d"), mouse.x, mouse.y);

		focus = said;
	}

	// …and cut down to the asked-for region, once the focus ring has been drawn on it.
	if (!crop.IsEmpty() && crop.GetWidth() > 8 && crop.GetHeight() > 8)
		shot = shot.GetSubBitmap(crop);

	wxImage picture = shot.ConvertToImage();
	if (!picture.IsOk())
		return false;

	// ⭐ SCALED DOWN, BECAUSE THE READER PAYS FOR EVERY BYTE. A full-size window is megabytes of PNG
	// and most of it is flat background; at 1280 across the text is still legible and the image is a
	// fraction of the size. The point is to be READ, and an answer too big to look at is no answer.
	const int kReadableWidth = 1280;
	if (picture.GetWidth() > kReadableWidth) {
		const int height = (int)((double)picture.GetHeight() * kReadableWidth / picture.GetWidth());
		picture.Rescale(kReadableWidth, height > 0 ? height : 1, wxIMAGE_QUALITY_HIGH);
	}

	// ⭐⭐ THE FORMAT IS THE CALLER'S CHOICE, because the trade belongs to them. PNG is exact, and a
	// picture of TEXT needs that — lossy compression smears precisely the digits somebody is asking
	// about, which in a screenshot of "the numbers do not add up" is the one detail that mattered.
	// JPEG is a fraction of the size and right when the question is about LAYOUT: which panel, what
	// sits where, whether the column is even on screen (Max, 2026-09-04: *"some format that does not
	// take much room and travels fast"*).
	const bool asJpeg = format.IsSameAs(wxT("jpeg"), false) || format.IsSameAs(wxT("jpg"), false);
	if (asJpeg)
		picture.SetOption(wxIMAGE_OPTION_QUALITY, 72);

	wxMemoryOutputStream stream;
	if (!picture.SaveFile(stream, asJpeg ? wxBITMAP_TYPE_JPEG : wxBITMAP_TYPE_PNG))
		return false;

	const wxStreamBuffer* buffer = stream.GetOutputStreamBuffer();
	if (buffer == nullptr || buffer->GetBufferSize() == 0)
		return false;

	bytes.AppendData(buffer->GetBufferStart(), buffer->GetBufferSize());
	return true;
}

// ---------------------------------------------------------------------------
// ibDocBottomStatusBar — the bar along the bottom.
//
// The backend header is included HERE rather than beside the class, so the frame
// header does not carry the MCP server into every translation unit that merely
// needs a frame.
// ---------------------------------------------------------------------------

#include "backend/mcp/mcpServer.h"

namespace {

// The interior palette, spelled where it is used — the same values the rest of the
// chrome carries (see luna_dockart.cpp).
const wxColour kStatusBarGround(0xC8, 0xD6, 0xDF);   // #C8D6DF light dusty
const wxColour kStatusBarInk   (0x3F, 0x5C, 0x77);   // #3F5C77 deep dusty blue

// The lamp. Muted rather than a signal green: it reports a steady state, and the
// bar it sits in is a quiet one.
const wxColour kAssistantLampOn(0x4F, 0x8A, 0x53);   // #4F8A53 sage

// Sized to the longest thing the field ever says, so the message beside it does
// not jump when the assistant starts.
const int kAssistantFieldWidth = 210;

const int kAssistantPollMilliseconds = 1000;

} // namespace

ibDocBottomStatusBar::ibDocBottomStatusBar(wxWindow* parent, wxWindowID id, long style, const wxString& name)
	: wxStatusBar(parent, id, style, name)
{
	// Light dusty bar — sits between the powder-blue workspace and the cream content
	// panes; deep-blue text reads cleanly against it.
	SetBackgroundColour(kStatusBarGround);
	SetForegroundColour(kStatusBarInk);

	// -1 is "share what is left"; a positive number is a fixed width. So the message
	// grows with the window and the lamp keeps its place at the right-hand end.
	const int widths[FieldCount] = { -1, kAssistantFieldWidth };
	SetFieldsCount(FieldCount);
	SetStatusWidths(FieldCount, widths);

	// wxSB_FLAT for the lamp: a sunken border around a field that is usually empty
	// draws a box on the bar for no reason.
	const int styles[FieldCount] = { wxSB_NORMAL, wxSB_FLAT };
	SetStatusStyles(FieldCount, styles);

	m_assistantPoll.SetOwner(this);
	Bind(wxEVT_TIMER, &ibDocBottomStatusBar::OnAssistantPoll, this, m_assistantPoll.GetId());
	m_assistantPoll.Start(kAssistantPollMilliseconds);

	// Ask once now rather than waiting a second for the first tick: a bar that comes
	// up blank and fills in a moment later looks like something still loading.
	wxTimerEvent unused(m_assistantPoll);
	OnAssistantPoll(unused);
}

void ibDocBottomStatusBar::OnAssistantPoll(wxTimerEvent& WXUNUSED(event))
{
	const ibMcpServer* server = ibApplicationData::GetMcpServer();
	const bool running = server != nullptr && server->IsRunning();

	// ⭐ NOTHING IS TOUCHED WHILE NOTHING CHANGES. SetStatusText invalidates the field,
	// so writing the same string every second would repaint the bar once a second for
	// the life of the process.
	if (running == m_assistantRunning)
		return;

	m_assistantRunning = running;

	// The endpoint, not just the word: when an assistant cannot connect, the first
	// question is always which address and port it should have used, and the answer
	// is then already on screen. Empty when it is off — an indicator that says "off"
	// in every window of every session is noise, and its absence says the same thing.
	const wxString lamp = running
		? wxString::Format(wxT("Assistant  %s"), server->GetEndpoint())
		: wxString();

	SetStatusText(lamp, FieldAssistant);
}

#if OES_STATUSBAR_CUSTOM_INK

void ibDocBottomStatusBar::DrawFieldText(wxDC& dc, const wxRect& rect, int i, int textHeight)
{
	// ⭐ THE ONE LINE THE OLD wxStaticText EXISTED FOR. The generic bar never sets a text
	// colour, so the field is drawn in the DC's default — black — whatever the window's
	// foreground says.
	dc.SetTextForeground(GetForegroundColour());

	if (i != FieldAssistant || !m_assistantRunning) {
		wxStatusBar::DrawFieldText(dc, rect, i, textHeight);
		return;
	}

	// A lit lamp, then the text moved clear of it. The rectangle handed to the base is
	// narrowed rather than the text indented, so ellipsizing and clipping still measure
	// against the space the text actually has.
	const int diameter = wxMax(6, textHeight / 2);
	const int cx = rect.x + 8 + diameter / 2;
	const int cy = rect.y + rect.height / 2;

	dc.SetBrush(wxBrush(kAssistantLampOn));
	dc.SetPen(wxPen(kAssistantLampOn));
	dc.DrawCircle(cx, cy, diameter / 2);

	wxRect textRect(rect);
	const int taken = (cx + diameter / 2 + 4) - rect.x;
	textRect.x     += taken;
	textRect.width -= taken;

	wxStatusBar::DrawFieldText(dc, textRect, i, textHeight);
}

#endif // OES_STATUSBAR_CUSTOM_INK
