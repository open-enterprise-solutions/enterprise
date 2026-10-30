////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

#include <functional>
#include <set>

#include <wx/choicdlg.h>
#include <wx/config.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — the tabs held still while one is switched to
#include <wx/evtloop.h>   // wxGUIEventLoop — a server's question waits for the calculator or the calendar

#include "output/outputWindow.h"
#include "objinspect/objinspect.h"
#include "settings/settingsxml.h"


#include "core/exception.h"   // ibCoreException — the engine's exception a fetch was refused with
#include "frmclient/docView/docManager.h"
#include "frmclient/docView/templates/docViewFile.h"
#include "frmclient/docView/templates/docViewAuditLog.h"   // the journal's tab — a view of its own (the tab's Kind)
#include "frmclient/docView/templates/docViewHomePage.h"   // the start page's tab — its forms, laid out
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/visualView/visualHostClient.h"
#include "frmclient/win/picture.h"
#include "frmclient/win/dlgs/about.h"
#include "frmclient/win/dlgs/activeUser.h"
#include "frmclient/win/dlgs/authorization.h"
#include "frmclient/win/dlgs/enterpriseOption.h"
#include "frmclient/win/dlgs/errorDialog.h"
#include "frmclient/win/dlgs/formEditor.h"
#include "frmclient/win/dlgs/generation.h"
#include "frmclient/win/dlgs/jobSchedule/jobScheduleSettings.h"
#include "frmclient/win/editor/gridEditor/gridPrintout.h"   // a script's sheet printed — and the preview's page (ApplyFitToPageWidth)
#include "frmclient/win/dlgs/settings/savedSettings.h"
#include "frmclient/win/dlgs/settings/list/listSettings.h"
#include "frmclient/win/dlgs/settings/composer/composerSettings.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // a value as the server's question carries it
#include "frmclient/backend/metaCollection/metaObject.h"  // the inspector's rows an answer brought, kept for their object
#include "frmclient/backend/backend_localization.h"     // the session's language — the frame says it
#include "frmclient/visualView/ctrl/typeControl.h"      // ibTypeControlFactory::SimpleChoice — the calculator, the calendar
#include "frmclient/visualView/ctrl/frame.h"            // ibControlFrame — what they are drawn over
#include "frmclient/win/ctrls/controlTextEditor.h"      // ibControlTextEditor — the field they drop under
#include "frmclient/backend/composition/dataComposer.h"
#include "frmclient/win/ctrls/tableView.h"
#include "frmclient/win/ctrls/floatingNotebook.h"
#include "frmclient/win/theme/luna_tabart.h"
#include "frmclient/win/theme/luna_dockart.h"

namespace {

// The wx ids the server's menus take — a range of their own: a command's by its number, a schema's by its own past the
// commands', a schema's command by both past those, so an item has the same id each run and a key the person gave it
// holds (UpdateHotKeys).
constexpr int kFirstMenuId              = wxID_HIGHEST + 5000;
constexpr int kFirstSchemaMenuId        = wxID_HIGHEST + 5500;
constexpr int kFirstSchemaCommandMenuId = wxID_HIGHEST + 6000;
constexpr int kSchemaCommandMenuIds     = 32;   // the ids a schema's commands take — past the most a schema has
constexpr int kLastMenuId               = wxID_HIGHEST + 6999;

// The Window menu's Close All — wxAuiMDIParentFrame's own id, which tabmdi.cpp keeps to itself (the desktop designer's
// mainFrameDesignerMenu.cpp names it as well).
constexpr int kWindowCloseAllId = 4002;

// The Window menu out of a bar BY THE MENU — wx's RemoveWindowMenu looks for it by `_("&Window")` as said at that moment.
// ⚠ THE WORD CHANGES WITH THE THREAD'S LANGUAGE: put in as "Окно" under a base's, it is looked for as "Window" at the
// exit, stays in the bar, and goes twice — by ~wxAuiMDIParentFrame, then by the bar (a fall at every such exit, 2026-10-30).
void RemoveWindowMenuOf(wxMenuBar* bar, wxMenu* windowMenu)
{
	if (bar == nullptr || windowMenu == nullptr)
		return;
	for (size_t pos = 0; pos < bar->GetMenuCount(); pos++) {
		if (bar->GetMenu(pos) == windowMenu) {
			bar->Remove(pos);
			return;
		}
	}
}

// The person's options file, beside the desktop designer's options.xml: the thin client's commands go by other ids.
constexpr const wxChar* kOptionsFile = wxT("options-thin.xml");

// A file goes up in parts of at most this — what the server keeps one part as (ibTempStorage::kPartSize).
constexpr std::size_t kUploadPartSize = 1024 * 1024;

// The stock picture an item is shown with — the art provider's (artProvider/), by the number the server names it by.
wxArtID ArtOf(long long picture)
{
	switch (static_cast<ibProtocol::ibFrame::ibMenu::ibStockPicture>(picture)) {
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::New:    return wxART_NEW;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Open:   return wxART_FILE_OPEN;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Save:   return wxART_FILE_SAVE;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::SaveAs: return wxART_FILE_SAVE_AS;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Close:  return wxART_CLOSE;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Quit:   return wxART_QUIT;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Undo:   return wxART_UNDO;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Redo:   return wxART_REDO;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Cut:    return wxART_CUT;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Copy:   return wxART_COPY;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Paste:  return wxART_PASTE;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Delete: return wxART_DELETE;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Find:   return wxART_FIND;
	case ibProtocol::ibFrame::ibMenu::ibStockPicture::Print:  return wxART_PRINT;
	default:                             break;
	}
	return wxArtID();
}

// The wx command of an edit the server asks back. wxID_NONE: none.
int EditCommandOf(long long command)
{
	switch (static_cast<ibProtocol::ibFrame::ibCommand>(command)) {
	case ibProtocol::ibFrame::ibCommand::Cut:       return wxID_CUT;
	case ibProtocol::ibFrame::ibCommand::Copy:      return wxID_COPY;
	case ibProtocol::ibFrame::ibCommand::Paste:     return wxID_PASTE;
	case ibProtocol::ibFrame::ibCommand::Delete:    return wxID_DELETE;
	case ibProtocol::ibFrame::ibCommand::SelectAll: return wxID_SELECTALL;
	default:                           break;
	}
	return wxID_NONE;
}

// THE FIELD UNDER THE FOCUS DOES IT — wx's own command, which a text field answers (wxTextCtrl's EVT_MENU; its Delete is
// wxID_CLEAR). To the field alone: not sent on up to the window, which is where an Edit menu's command came from.
void EditFocused(int command)
{
	wxWindow* const focus = wxWindow::FindFocus();
	if (focus == nullptr || command == wxID_NONE)
		return;

	wxCommandEvent event(wxEVT_MENU, command == wxID_DELETE ? wxID_CLEAR : command);
	event.SetEventObject(focus);
	focus->GetEventHandler()->ProcessEventLocally(event);
}

ibProtocol::ibFrame::ibMessage::ibLevel LevelOf(long long level)
{
	if (level == static_cast<long long>(ibProtocol::ibFrame::ibMessage::ibLevel::Warning))
		return ibProtocol::ibFrame::ibMessage::ibLevel::Warning;
	if (level == static_cast<long long>(ibProtocol::ibFrame::ibMessage::ibLevel::Error))
		return ibProtocol::ibFrame::ibMessage::ibLevel::Error;
	return ibProtocol::ibFrame::ibMessage::ibLevel::Information;
}

// A menu of the frame, as a list of what it is made of — the titles, the keys, what each item does — less whether an
// item is enabled: two menus of one shape differ only in what may be pressed, and that is set, not built.
void ShapeOf(const ibProtocolNode& menu, ibString& shape)
{
	for (const ibProtocolNode& item : menu.Children()) {
		shape << wxT('[');
		if (item.GetBool(ibProtocol::ibFrame::ibMenu::Separator))
			shape << wxT('-');
		shape << item.GetString(ibProtocol::ibFrame::ibMenu::Title) << wxT('|')
			<< item.GetString(ibProtocol::ibFrame::ibMenu::Shortcut) << wxT('|')
			<< item.GetInt(ibProtocol::ibFrame::ibMenu::Command, -1) << wxT('|')
			<< item.GetInt(ibProtocol::ibFrame::ibMenu::Schema, -1) << wxT('|')
			<< item.GetInt(ibProtocol::ibFrame::ibMenu::StockPicture);
		ShapeOf(item, shape);
		shape << wxT(']');
	}
}

// The wx id of a command or a schema of the server's menu — by its number in the protocol.
int MenuIdOf(const ibProtocolNode& item)
{
	if (item.Has(ibProtocol::ibFrame::ibMenu::Schema) && item.Has(ibProtocol::ibFrame::ibMenu::Command))
		return kFirstSchemaCommandMenuId + static_cast<int>(item.GetInt(ibProtocol::ibFrame::ibMenu::Schema)) * kSchemaCommandMenuIds
			+ static_cast<int>(item.GetInt(ibProtocol::ibFrame::ibMenu::Command));
	if (item.Has(ibProtocol::ibFrame::ibMenu::Schema))
		return kFirstSchemaMenuId + static_cast<int>(item.GetInt(ibProtocol::ibFrame::ibMenu::Schema));
	return kFirstMenuId + static_cast<int>(item.GetInt(ibProtocol::ibFrame::ibMenu::Command));
}

// A MENU REQUEST'S ITEMS, put into a popup menu as the desktop built it: an item with items of its own is a submenu,
// the others are commands — ticked, greyed as they say. The server's ids may be any number and a wx id may not, so each
// command takes the next id of the menu's own, and `ids` keeps the server's in that order.
void AppendMenuItems(wxMenu& menu, const std::vector<ibProtocolNode>& items, std::vector<long long>& ids)
{
	for (const ibProtocolNode& item : items) {
		const wxString caption = item.GetString(ibProtocol::ibRequest::ibMenu::Caption);
		const std::vector<ibProtocolNode> subItems = item.Children();
		if (!subItems.empty()) {
			wxMenu* const submenu = new wxMenu;
			AppendMenuItems(*submenu, subItems, ids);
			menu.AppendSubMenu(submenu, caption)->Enable(item.GetBool(ibProtocol::ibRequest::ibMenu::Enabled, true));
			continue;
		}
		const int id = wxID_HIGHEST + 1 + static_cast<int>(ids.size());
		ids.push_back(item.GetInt(ibProtocol::ibRequest::ibMenu::Id));
		const bool checked = item.GetBool(ibProtocol::ibRequest::ibMenu::Checked);
		wxMenuItem* const added = checked ? menu.AppendCheckItem(id, caption) : menu.Append(id, caption);
		if (checked)
			added->Check(true);
		added->Enable(item.GetBool(ibProtocol::ibRequest::ibMenu::Enabled, true));
	}
}

// A FIELD'S VALUE FOR THE SERVER'S QUESTION — what the desktop's SimpleChoice is drawn over: the value the question carries,
// the one chosen kept, and the wait ended when the calculator or the calendar goes (ibProtocol::ibRequest::ibKind::SimpleChoice).
class ibSimpleChoiceHolder : public ibControlFrame {
public:

	explicit ibSimpleChoiceHolder(const ibValue& value) : m_value(value) {}

	virtual bool GetControlValue(ibValue& pvarControlVal) const override { pvarControlVal = m_value; return true; }
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue()) override {
		m_chosen = varControlVal;
		m_hasChosen = true;
		return true;
	}

	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

	virtual bool HasQuickChoice() const override { return true; }
	virtual void ChoiceProcessing(ibValue& vSelected) override { SetControlValue(vSelected); }
	virtual void ChoiceDismissed() override {
		m_dismissed = true;
		if (m_loop != nullptr)
			m_loop->Exit();
	}

	const ibValue& GetValue() const { return m_value; }
	bool HasChosen() const { return m_hasChosen; }
	const ibValue& GetChosen() const { return m_chosen; }

	// Until the window goes — it may have gone already.
	void Wait() {
		if (m_dismissed)
			return;
		wxGUIEventLoop loop;
		m_loop = &loop;
		loop.Run();
		m_loop = nullptr;
	}

private:

	ibValue        m_value;
	ibValue        m_chosen;
	bool           m_hasChosen = false;
	bool           m_dismissed = false;
	wxGUIEventLoop* m_loop = nullptr;
};

// The field the person pressed Select on — where the window drops down from, as the desktop's drops under its editor.
wxWindow* FocusedEditor(wxWindow* otherwise)
{
	for (wxWindow* window = wxWindow::FindFocus(); window != nullptr; window = window->GetParent()) {
		if (wxDynamicCast(window, ibControlTextEditor) != nullptr)
			return window;
	}
	return wxWindow::FindFocus() != nullptr ? wxWindow::FindFocus() : otherwise;
}

// A SETTINGS WINDOW'S QUESTION — a list's or a report's: the one a value of the server's choosing is chosen for on the
// way (ibProtocol::ibRequest::ibListSettings::ibAct::Choose), and asked again with it.
bool IsSettingsRequest(const ibProtocolNode& request)
{
	const ibProtocol::ibRequest::ibKind kind = static_cast<ibProtocol::ibRequest::ibKind>(request.GetInt(ibProtocol::ibRequest::Kind));
	return kind == ibProtocol::ibRequest::ibKind::ListSettings || kind == ibProtocol::ibRequest::ibKind::ComposerSettings;
}

} // namespace

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

ibFrontendMainFrame* ibFrontendMainFrame::s_instance = nullptr;

ibFrontendMainFrame::ibFrontendMainFrame(std::unique_ptr<ibCommunicator> communicator, ibProtocol::ibLogin::ibMode mode,
	const wxString& user, const wxString& password, const wxString& title,
	const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: ibDocParentFrameAnyBase(this),
	m_communicator(std::move(communicator)), m_mode(mode), m_user(user), m_password(password)
{
	wxAuiMDIParentFrame::Create(nullptr, wxID_ANY, title, pos, size, style | wxNO_FULL_REPAINT_ON_RESIZE, name);

	// Claim the process's main-window slot.
	if (s_instance == nullptr) {
		s_instance = this;
		wxTheApp->SetTopWindow(this);
	}

	Bind(wxEVT_CLOSE_WINDOW, &ibFrontendMainFrame::OnCloseWindow, this);
	Bind(wxEVT_MENU, &ibFrontendMainFrame::OnExit, this, wxID_EXIT);
	Bind(wxEVT_MENU, &ibFrontendMainFrame::OnCloseAll, this, kWindowCloseAllId);
	Bind(wxEVT_MENU, &ibFrontendMainFrame::OnMenu, this, kFirstMenuId, kLastMenuId);
	for (const int edit : { wxID_CUT, wxID_COPY, wxID_PASTE, wxID_DELETE, wxID_SELECTALL })
		Bind(wxEVT_MENU, &ibFrontendMainFrame::OnEditCommand, this, edit);

	SetArtProvider(new wxAuiLunaTabArt());

	// A tab chosen is told the server, which draws it; a tab closed is its document's close (ibTabDocument::Close).
	if (wxAuiMDIClientWindow* const client = GetClientWindow())
		client->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibFrontendMainFrame::OnTabChanged, this);

	// notify wxAUI which valueForm to use
	m_mgr.SetManagedWindow(this);
	m_mgr.SetArtProvider(new wxAuiLunaDockArt());

	// Disable live pane-border resize — wxAUI_MGR_LIVE_RESIZE is part of wxAUI_MGR_DEFAULT and triggers a full Layout
	// pass through every pane on every mouse-move during border drag. Ghost-rect drag + commit on mouse-up stays lag-free.
	m_mgr.SetFlags(m_mgr.GetFlags() & ~wxAUI_MGR_LIVE_RESIZE);

#ifdef __WXMSW__
	SetIcon(wxICON(oes));
#endif
}

ibFrontendMainFrame::~ibFrontendMainFrame()
{
	if (s_instance == this)
		s_instance = nullptr;

	// ⚠ THE SLOT GOES BEFORE THE CHILDREN: they die after this body (~wxWindow), the inspector among them and in any
	// order — a metaobject the designer's tree holds asks the slot as it dies (~ibPropertyObject → GetProperty), and met
	// an inspector already freed (an exit, 2026-10-14). Unhooked, the two settle it between them (the notifier).
	m_objectInspector = nullptr;

	wxDELETE(m_docManager);

	// deinitialize the valueForm manager
	m_mgr.UnInit();

	// ~wxAuiMDIParentFrame deletes the Window menu itself: it leaves no bar still holding it. While a view's bar is shown,
	// the window's own holds it (SetMenuBar).
	RemoveWindowMenuOf(m_pMyMenuBar != nullptr ? m_pMyMenuBar : GetMenuBar(), GetWindowMenu());
}

wxAuiMDIClientWindow* ibFrontendMainFrame::OnCreateClient()
{
	class wxAuiMDIClientWindowImpl : public wxAuiMDIClientWindow {
	public:
		wxAuiMDIClientWindowImpl() : wxAuiMDIClientWindow() {}
		wxAuiMDIClientWindowImpl(wxAuiMDIParentFrame* parent, long style = 0) : wxAuiMDIClientWindow(parent, style) {
#ifdef __WXOSX__
			// The system background — respects dark/light mode.
			SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_APPWORKSPACE));
#else
			// Powder-blue workspace — the "walls" of the application; forms sit on it as a calm cool backdrop.
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

#ifndef __WXOSX__
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
#endif

void ibFrontendMainFrame::RaiseFrame()
{
	if (!m_callRaiseFrame && ibFrontendMainFrame::IsFocusable()) {
		CallAfter([&]() {
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
		RemoveWindowMenuOf(GetMenuBar(), GetWindowMenu());

		//Add the Window menu to the new menu bar.
		AddWindowMenu(pMenuBar);
	}

	wxFrame::SetMenuBar(pMenuBar);
}
#endif // wxUSE_MENUS

void ibFrontendMainFrame::CreatePropertyPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_PROPERTY).IsOk())
		return;

	m_objectInspector = new ibObjectInspector(this, wxID_ANY);

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_PROPERTY);
	paneInfo.CloseButton(true);
	paneInfo.MinimizeButton(false);
	paneInfo.MaximizeButton(false);
	paneInfo.DestroyOnClose(false);
	paneInfo.Caption(_("Properties"));
	paneInfo.MinSize(300, 0);
	paneInfo.Right();
	paneInfo.Show(false);

	m_mgr.AddPane(m_objectInspector, paneInfo);
}

void ibFrontendMainFrame::CreateBottomPane()
{
	if (m_mgr.GetPane(wxAUI_PANE_BOTTOM).IsOk())
		return;

	wxAuiPaneInfo paneInfo;
	paneInfo.Name(wxAUI_PANE_BOTTOM);
	paneInfo.Bottom();
	paneInfo.PinButton(false);
	paneInfo.CloseButton(false);
	paneInfo.Resizable(false);
	paneInfo.Movable(false);
	paneInfo.MinSize(-1, 30);

	ibFloatingNotebook* auiNotebook = new ibFloatingNotebook(&m_mgr, paneInfo.name,
		wxID_ANY,
		wxDefaultPosition,
		wxDefaultSize,
		wxAUI_NB_BOTTOM | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);

	auiNotebook->SetArtProvider(new wxAuiLunaTabArt());
	{
		wxWindowUpdateLocker freeze(auiNotebook);
		auiNotebook->AddPage(m_outputWindow, _("Messages"), false, wxArtProvider::GetBitmapBundle(wxART_MESSAGE, wxART_SERVICE, wxSize(16, 16)));
		auiNotebook->SetNullSelection();
	}

	m_mgr.AddPane(auiNotebook, paneInfo);
}

bool ibFrontendMainFrame::IsShownInspector()
{
	const wxAuiPaneInfo propertyPane = m_mgr.GetPane(wxAUI_PANE_PROPERTY);
	if (!propertyPane.IsOk()) return false;
	return propertyPane.IsShown();
}

void ibFrontendMainFrame::ShowInspector()
{
	wxAuiPaneInfo& propertyPane = m_mgr.GetPane(wxAUI_PANE_PROPERTY);
	if (!propertyPane.IsOk())
		return;
	if (!propertyPane.IsShown()) {
		propertyPane.Show();
		m_objectInspector->SetFocus();
		m_objectInspector->Raise();
		m_mgr.Update();
	}
}

ibPropertyObject* ibFrontendMainFrame::GetProperty() const
{
	if (m_objectInspector != nullptr)
		return m_objectInspector->GetSelectedObject();

	return nullptr;
}

bool ibFrontendMainFrame::SetProperty(ibPropertyObject* prop)
{
	if (m_objectInspector != nullptr) {
		m_objectInspector->SelectObject(prop);
		return true;
	}

	return false;
}

void ibFrontendMainFrame::CreateGUI()
{
	// The person's options first: the panes below show code and messages in their font, the menu takes their keys.
	LoadOptions();

	// The toolbar under the menu — the desktop's main toolbar (CreateWideGui): the doc manager's commands, enabled as it
	// answers.
	m_mainFrameToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_mainFrameToolbar->SetToolBitmapSize(wxSize(16, 16));

	m_mainFrameToolbar->AddTool(wxID_NEW, _("New"), wxArtProvider::GetBitmapBundle(wxART_NEW, wxART_FRAME_ICON, wxSize(16, 16)), _("New"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_OPEN, _("Open"), wxArtProvider::GetBitmapBundle(wxART_FILE_OPEN, wxART_FRAME_ICON, wxSize(16, 16)), _("Open"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVE, _("Save"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE, wxART_FRAME_ICON, wxSize(16, 16)), _("Save"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_SAVEAS, _("Save as"), wxArtProvider::GetBitmapBundle(wxART_FILE_SAVE_AS, wxART_FRAME_ICON, wxSize(16, 16)), _("Save as"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_FIND, _("Find"), wxArtProvider::GetBitmapBundle(wxART_FIND, wxART_FRAME_ICON, wxSize(16, 16)), _("Find"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddSeparator();
	m_mainFrameToolbar->AddTool(wxID_REDO, _("Redo"), wxArtProvider::GetBitmapBundle(wxART_REDO, wxART_FRAME_ICON, wxSize(16, 16)), _("Redo"), wxItemKind::wxITEM_NORMAL);
	m_mainFrameToolbar->AddTool(wxID_UNDO, _("Undo"), wxArtProvider::GetBitmapBundle(wxART_UNDO, wxART_FRAME_ICON, wxSize(16, 16)), _("Undo"), wxItemKind::wxITEM_NORMAL);

	m_mainFrameToolbar->Realize();

	wxAuiPaneInfo m_infoDefault;
	m_infoDefault.Name(wxT("mainTool"));
	m_infoDefault.Caption(wxT("Default"));
	m_infoDefault.ToolbarPane();
	m_infoDefault.Top();
	m_infoDefault.Row(1);
	m_infoDefault.Position(1);
	m_infoDefault.CloseButton(false);
	m_infoDefault.DestroyOnClose(false);

	m_mgr.AddPane(m_mainFrameToolbar, m_infoDefault);

	// …and the toolbar of the active view, beside it — shown while that view has tools (ActivateView).
	m_docToolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_LAYOUT);
	m_docToolbar->SetToolBitmapSize(wxSize(16, 16));

	wxAuiPaneInfo m_infoAdditional;
	m_infoAdditional.Name(wxT("additional"));
	m_infoAdditional.Caption(wxT("Additional"));
	m_infoAdditional.ToolbarPane();
	m_infoAdditional.Top();
	m_infoAdditional.Row(1);
	m_infoAdditional.Position(1);
	m_infoAdditional.CloseButton(false);
	m_infoAdditional.DestroyOnClose(false);
	m_infoAdditional.Hide();

	m_mgr.AddPane(m_docToolbar, m_infoAdditional);

	CreatePropertyPane();

	InitializeDefaultMenu();

	// The messages — every mode's: a tab along the bottom, as the desktop's.
	m_outputWindow = new ibOutputWindow(this);
	CreateBottomPane();

	// The bar along the bottom — the desktop's.
	SetStatusBar(new ibDocBottomStatusBar(this));

	// The workspace — the same powder blue the tabs sit on.
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(
		wxAUI_DOCKART_BACKGROUND_COLOUR, wxColour(0xB8, 0xC9, 0xD4));

	SetMinSize(wxSize(400, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
}

void ibFrontendMainFrame::LoadOptions()
{
	// Disable logging since it's ok if the options file is not there.
	wxLogNull logNo;

	// The person's directory, where the desktop designer keeps its options.xml — the thin client's file beside it. The
	// thin designer writes it (SaveOptions).
	wxFileName fileName(wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache), kOptionsFile);
	fileName.AppendDir(wxT("OES"));

	wxXmlDocument document;
	if (!document.Load(fileName.GetFullPath()) || document.GetRoot()->GetName() != wxT("options"))
		return;

	for (wxXmlNode* node = document.GetRoot()->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() == wxT("editor"))
			m_editorSettings.Load(node);
		else if (node->GetName() == wxT("fontcolor"))
			m_fontColorSettings.Load(node);
	}

	// …and the keys kept as read: they go over the defaults each time the menu is built (UpdateHotKeys).
	m_options = document;
}

void ibFrontendMainFrame::SaveOptions()
{
	// Disable logging since it's ok if the options file saving isn't successful.
	wxLogNull logNo;

	wxXmlDocument document;
	wxXmlNode* const root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("options"));
	document.SetRoot(root);

	root->AddChild(m_fontColorSettings.Save(wxT("fontcolor")));
	root->AddChild(m_editorSettings.Save(wxT("editor")));
	root->AddChild(m_keyBinder.Save(wxT("shortcuts")));

	// Make sure the directory exists.
	wxFileName fileName(wxStandardPaths::Get().GetUserDir(wxStandardPaths::Dir::Dir_Cache), kOptionsFile);
	fileName.AppendDir(wxT("OES"));
	fileName.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
	document.Save(fileName.GetFullPath());

	// Kept as written: the keys go over the defaults each time the menu is built (UpdateHotKeys).
	m_options = document;
}

void ibFrontendMainFrame::InitializeDefaultMenu()
{
	// ONE COMMAND, ONE PICTURE, wherever it is pressed from — wx's own stock pictures, and wx's own stock labels and keys.
	const auto stockPicture = [](wxMenuItem* item, const wxArtID& art) {
		item->SetBitmap(wxArtProvider::GetBitmapBundle(art, wxART_MENU, wxSize(16, 16)));
		return item;
	};

	m_menuFile = new wxMenu();
	stockPicture(m_menuFile->Append(wxID_NEW), wxART_NEW);
	stockPicture(m_menuFile->Append(wxID_OPEN), wxART_FILE_OPEN);

	stockPicture(m_menuFile->Append(wxID_CLOSE), wxART_CLOSE);
	stockPicture(m_menuFile->Append(wxID_SAVE), wxART_FILE_SAVE);
	stockPicture(m_menuFile->Append(wxID_SAVEAS), wxART_FILE_SAVE_AS);
	m_menuFile->Append(wxID_REVERT, _("Re&vert..."));

	m_menuFile->AppendSeparator();
	stockPicture(m_menuFile->Append(wxID_PRINT), wxART_PRINT);
	m_menuFile->Append(wxID_PRINT_SETUP, _("Print &Setup..."));
	m_menuFile->Append(wxID_PREVIEW);

	m_menuFile->AppendSeparator();
	stockPicture(m_menuFile->Append(wxID_EXIT), wxART_QUIT);

	// A history of files visited — the person's, kept on this computer: the server knows nothing of it.
	m_docManager->FileHistoryUseMenu(m_menuFile);
#if wxUSE_CONFIG
	m_docManager->FileHistoryLoad(*wxConfig::Get());
#endif // wxUSE_CONFIG

	m_menuEdit = new wxMenu();
	stockPicture(m_menuEdit->Append(wxID_UNDO), wxART_UNDO);
	stockPicture(m_menuEdit->Append(wxID_REDO), wxART_REDO);
	m_menuEdit->AppendSeparator();
	stockPicture(m_menuEdit->Append(wxID_CUT), wxART_CUT);
	stockPicture(m_menuEdit->Append(wxID_COPY), wxART_COPY);
	stockPicture(m_menuEdit->Append(wxID_PASTE), wxART_PASTE);
	stockPicture(m_menuEdit->Append(wxID_DELETE), wxART_DELETE);
	m_menuEdit->Append(wxID_SELECTALL);
	m_menuEdit->AppendSeparator();
	stockPicture(m_menuEdit->Append(wxID_FIND), wxART_FIND);

	wxMenuBar* const bar = new wxMenuBar();
	bar->Append(m_menuFile, wxGetStockLabel(wxID_FILE));
	bar->Append(m_menuEdit, wxGetStockLabel(wxID_EDIT));
	if (m_menuConfiguration != nullptr)
		bar->Append(m_menuConfiguration, _("Configuration"));
	if (m_menuDebug != nullptr)
		bar->Append(m_menuDebug, _("Debug"));
	if (m_menuHelp != nullptr)
		bar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));
	SetMenuBar(bar);
	UpdateHotKeys();
}

void ibFrontendMainFrame::SetDefaultHotKeys()
{
	// Setup the hotkeys.
	m_keyBinder.SetShortcut(wxID_NEW, wxT("Ctrl+N"));
	m_keyBinder.SetShortcut(wxID_SAVE, wxT("Ctrl+S"));
	m_keyBinder.SetShortcut(wxID_UNDO, wxT("Ctrl+Z"));
	m_keyBinder.SetShortcut(wxID_REDO, wxT("Ctrl+Y"));

	m_keyBinder.SetShortcut(wxID_CUT, wxT("Ctrl+X"));
	m_keyBinder.SetShortcut(wxID_COPY, wxT("Ctrl+C"));
	m_keyBinder.SetShortcut(wxID_PASTE, wxT("Ctrl+V"));
	m_keyBinder.SetShortcut(wxID_SELECTALL, wxT("Ctrl+A"));
	m_keyBinder.SetShortcut(wxID_FIND, wxT("Ctrl+F"));
}

void ibFrontendMainFrame::UpdateHotKeys()
{
	wxMenuBar* const bar = GetMenuBar();
	if (bar == nullptr)
		return;

	// Built anew with the bar: its commands, every default — and then the person's keys over them, as the desktop
	// designer's LoadOptions puts them (a command they changed takes theirs, the rest keep the default).
	m_keyBinder.ClearCommands();
	m_keyBinder.AddCommandsFromMenuBar(bar);

	SetDefaultHotKeys();
	for (const auto& item : m_menuItems) {
		if (!item.second.shortcut.IsEmpty())
			m_keyBinder.SetShortcut(item.first, item.second.shortcut);
	}

	wxXmlNode* const root = m_options.GetRoot();
	wxXmlNode* const shortcuts = root != nullptr ? FindChildNode(root, wxT("shortcuts")) : nullptr;
	if (shortcuts != nullptr)
		m_keyBinder.Load(shortcuts);

	m_keyBinder.UpdateWindow(this);
	m_keyBinder.UpdateMenuBar(bar);
}

bool ibFrontendMainFrame::Show(bool show)
{
	if (!show)
		return wxAuiMDIParentFrame::Show(false);

	// Already up — nothing to build, nothing to ask.
	if (IsShown())
		return true;

	// GUI FIRST, then the login: the start runs there (BeforeStart, OnStart), and what it says and asks is drawn into
	// the panes the GUI builds.
	CreateGUI();

	ibProtocolNode answer;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	bool loggedIn = m_communicator->Login(m_user, m_password, m_mode, answer, refusal, error, GetStartSchemas());

	// A REFUSED NAME OR PASSWORD ASKS THE PERSON — the desktop's login window, with the names the server offers, asked
	// again until the login goes through or the person cancels it. A list the server could not read offers none: the
	// name is typed.
	bool cancelled = false;
	std::vector<ibString> users;
	for (bool asked = false; !loggedIn && refusal == ibProtocol::ibRefusal::LoginRefused; asked = true) {
		if (asked) {
			wxMessageBox(_("Wrong user or password entered!"), GetTitle(), wxOK | wxICON_ERROR);
		}
		else {
			ibProtocol::ibRefusal usersRefusal = ibProtocol::ibRefusal::None;
			ibString usersError;
			m_communicator->Users(users, usersRefusal, usersError);
		}
		ibDialogAuthentication dlg(users);
		dlg.SetLogin(m_user);
		if (dlg.ShowModal() == wxID_CANCEL) {
			cancelled = true;
			break;
		}
		m_user = dlg.GetLogin();
		m_password = dlg.GetPassword();
		loggedIn = m_communicator->Login(m_user, m_password, m_mode, answer, refusal, error, GetStartSchemas());
	}
	m_password.clear();
	if (!loggedIn) {
		// A start that ended the run itself (its BeforeStart cancelled it, a debugger's Stop in it) is no failure to say:
		// the person ended it, or saw what it said — the desktop's start goes as quietly (enterprise/mainApp.cpp, Cancelled).
		// Nor is a login window the person cancelled.
		if (!cancelled && refusal != ibProtocol::ibRefusal::StartRefused)
			wxMessageBox(!error.IsEmpty() ? error : ibString(_("The start was refused.")), GetTitle(), wxOK | wxICON_ERROR);
		return false;
	}

	// WHAT THE SERVER SAYS UNASKED — the frame changed by itself (a report composed, a question asked by work nobody
	// called): the frame asked for, after whatever the window is doing; told on another thread, made on this one.
	m_communicator->Listen([this]() {
		CallAfter([this]() { Post(ibProtocol::ibMethod::Frame, ibProtocolNode()); });
	});

	SetClientSize(FromDIP(wxSize(800, 600)));
	Center();
	Settle(answer);

	// EXIT ALREADY — the start went no further (a debugger's Stop at a breakpoint in it): the window is not shown at all,
	// as the desktop's is not when its start ends the run, and whoever showed it takes it down. Shown, it flashed up and
	// went with the Exit the same answer carried.
	if (m_exitGranted)
		return false;

	// The window's own parts, once the start has run and nothing of it is pending.
	CreateStartupPage();

	if (!wxAuiMDIParentFrame::Show(true))
		return false;
	SetFocus();
	Raise();
	// …and the messages its start said, opened below — the desktop's (ibFrontendMainFrameEnterprise::Show): drawn before
	// the window was shown, they took no focus (ibOutputWindow::Output).
	if (m_outputWindow != nullptr && !m_outputWindow->IsEmpty())
		m_outputWindow->SetFocus();

	// …and what the login read beside the frame (GetStartSchemas), each shown as a call of its own would have been.
	for (const ibProtocolNode& read : answer.FindChild(ibProtocol::ibLogin::Read).Children())
		ShowSchema(static_cast<ibProtocol::ibSchema::ibKind>(read.GetInt(ibProtocol::ibLogin::Schema)), read);
	return true;
}

bool ibFrontendMainFrame::Destroy()
{
	// The client goes with the window: its session is torn down by the server, with whatever it still had open.
	m_closingWindow = true;
	m_communicator->Logout();

	// …and the tabs' documents here with it: the frame has no tab any more, so none of them asks the server
	// (ibTabDocument::Close). The files opened last stay for the next run.
	if (m_docManager != nullptr) {
		m_docManager->CloseDocuments(true);
#if wxUSE_CONFIG
		m_docManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
		m_docManager->FileHistoryRemoveMenu(m_menuFile);
	}

	return wxAuiMDIParentFrame::Destroy();
}

bool ibFrontendMainFrame::Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer)
{
	// Under way until it has settled — its questions asked, its refusal said: a call posted meanwhile waits for it.
	++m_calling;
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	const bool called = m_communicator->Call(method, params, answer, refusal, error);
	if (called)
		Settle(answer);
	else
		Refused(refusal, error);
	--m_calling;

	CallSettled();
	return called;
}

bool ibFrontendMainFrame::Call(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	++m_calling;
	const bool called = m_communicator->Call(method, params, answer, refusal, error);
	if (called)
		Settle(answer);
	else if (refusal == ibProtocol::ibRefusal::NoSession)
		Refused(refusal, error);
	--m_calling;

	CallSettled();
	return called;
}

void ibFrontendMainFrame::CallSettled()
{
	// Exit done — the client goes, once nothing is under way; a close of the window that asked for it goes on by itself.
	if (m_exitGranted && m_calling == 0 && !m_closingWindow)
		CallAfter([this]() {
			if (!m_closingWindow)
				Close(true);
		});

	if (m_calling == m_asking.size() && !m_posted.empty())
		CallAfter(&ibFrontendMainFrame::CallPosted);
}

void ibFrontendMainFrame::Post(ibProtocol::ibMethod method, const ibProtocolNode& params)
{
	m_posted.push_back(ibPostedCall{ method, params });
	CallAfter(&ibFrontendMainFrame::CallPosted);
}

bool ibFrontendMainFrame::Hold(const ibProtocolNode& event)
{
	// A call posted and not made yet goes first — a text committed as the focus leaves its field: the focus is held once
	// that call is made (CallPosted).
	if (!m_posted.empty()) {
		m_posted.back().held.push_back(event);
		return true;
	}
	return m_communicator->Hold(event);
}

void ibFrontendMainFrame::CloseTab(long long tabId)
{
	// Once — a key held, a second Escape, the button pressed while the first close asks about what is unsaved: the
	// tab is closing until that close has been answered.
	if (!m_tabsClosing.insert(tabId).second)
		return;

	ibProtocolNode params;
	params.SetValue(ibProtocol::ibFrame::Tab, tabId);
	Post(ibProtocol::ibMethod::Close, params);
}

bool ibFrontendMainFrame::Fetch(ibProtocol::ibMethod method, const ibProtocolNode& params, ibProtocolNode& answer)
{
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	if (m_communicator->Call(method, params, answer, refusal, error))
		return true;
	// ⭐ THE ENGINE'S EXCEPTION IS THE CLIENT'S — a read that failed came back as false, and a table took it for no rows:
	// a folder read anew showed empty, and nobody was told why (2026-10-29).
	if (refusal == ibProtocol::ibRefusal::Exception)
		ibCoreException::Error(ibT("%s"), error);
	return false;
}

void ibFrontendMainFrame::ShowSchema(ibProtocol::ibSchema::ibKind schema, const ibProtocolNode& shown)
{
	if (schema == ibProtocol::ibSchema::ibKind::ActiveUser) {
		ibDialogActiveUser dialog(this, shown);
		dialog.ShowModal();
	}
	else if (schema == ibProtocol::ibSchema::ibKind::About) {
		ibDialogAbout dialog(this, shown);
		dialog.ShowModal();
	}
	else if (schema == ibProtocol::ibSchema::ibKind::Debugging) {
		ibDialogEnterpriseOption dialog(this, shown);
		dialog.ShowModal();
	}
}

bool ibFrontendMainFrame::AllowClose()
{
	// The road the menu's Exit takes — the server closes each tab but the start page, asking about what is unsaved, and
	// says Exit when every one closed (Draw). A session already gone has nothing to keep.
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibFrame::Command, static_cast<long long>(ibProtocol::ibFrame::ibCommand::Exit));
	Call(ibProtocol::ibMethod::Command, params, answer);
	return m_exitGranted || !m_communicator->IsLoggedIn();
}

void ibFrontendMainFrame::Draw(const ibProtocolNode& answer)
{
	// ⭐ AN ANSWER THAT IS NOT A FRAME — a schema's tree, a fetch's rows — CHANGED NOTHING IN THE WINDOW: the frame held
	// stays (ibCommunicator::Take asks the same). Drawn whole, it rebuilt the inspector under the property window that
	// asked it (Synonym's "..." → its languages) — the grid's rows freed under the button still dispatching (2026-10-14).
	if (!answer.Has(ibProtocol::ibFrame::Frame))
		return;

	// THE INSPECTOR'S ROWS OF WHAT A DOOR ACTED ON (an edit, a new object, a tab opened over one) — received for the
	// object before anything is drawn: the tree drawn below may select it, and its object takes them in instead of asking.
	for (const ibProtocolNode& rows : answer.FindChild(ibProtocol::ibFrame::Properties).Children())
		ibValueMetaObject::Receive(rows.GetInt(ibProtocol::ibFrame::Tab), static_cast<ibMetaID>(rows.GetId()), rows);

	const ibProtocolNode& frame = m_communicator->GetFrame();

	// ⭐ WHAT THE ANSWER CHANGED IS DRAWN — its patch names it, and a part it does not name is as it was; a frame sent
	// whole is drawn whole.
	const ibProtocolNode patch = answer.FindChild(ibProtocol::ibFrame::Patch);
	const bool whole = !patch.IsNode();

	// THE LANGUAGE TEXT IS READ IN — the session's, as the server says it, before anything is drawn: a caption held
	// whole (a property's Synonym) reads in it, as the desktop's read in its configuration's.
	if (whole || patch.Has(ibProtocol::ibFrame::Language))
		ibBackendLocalization::SetUserLanguage(frame.GetString(ibProtocol::ibFrame::Language));

	if (whole || patch.Has(ibProtocol::ibFrame::Title)) {
		const wxString title = frame.GetString(ibProtocol::ibFrame::Title);
		if (!title.IsEmpty() && title != GetTitle())
			SetTitle(title);
	}
	if (GetStatusBar() != nullptr && (whole || patch.Has(ibProtocol::ibFrame::Status)))
		SetStatusText(frame.GetString(ibProtocol::ibFrame::Status));
	// …and beside it the assistant's lamp. The endpoint, not just the word: when an assistant cannot connect, the first
	// question is always which address and port it should have used, and the answer is then already on screen. Empty
	// when it is off — its absence says the same thing.
	if (GetStatusBar() != nullptr && (whole || patch.Has(ibProtocol::ibFrame::Endpoint))) {
		const wxString endpoint = frame.GetString(ibProtocol::ibFrame::Endpoint);
		SetStatusText(endpoint.IsEmpty() ? wxString() : wxString::Format(wxT("Assistant  %s"), endpoint),
			ibDocBottomStatusBar::FieldAssistant);
	}

	if (whole || patch.Has(ibProtocol::ibFrame::Menu))
		DrawMenu(frame.FindChild(ibProtocol::ibFrame::Menu));
	if (whole || patch.Has(ibProtocol::ibFrame::Templates))
		DrawTemplates(frame.FindChild(ibProtocol::ibFrame::Templates));

	// ⭐ A TAB AND ITS VIEW COME UP AS ONE. A view is built when its first frame is drawn, after the tab: thawed between
	// the two, the person watched the composer's panel be born — a dot in the corner, then its pages, then its layout.
	{
		wxWindowUpdateLocker freeze;
		wxAuiMDIClientWindow* const client = GetClientWindow();
		if (client != nullptr && (whole || patch.Has(ibProtocol::ibFrame::Tabs)))
			freeze.Lock(client);

		if (whole || patch.Has(ibProtocol::ibFrame::Tabs))
			DrawTabs(frame.FindChild(ibProtocol::ibFrame::Tabs));
		DrawFrame(frame, patch);
		DrawView(frame.GetInt(ibProtocol::ibFrame::ActiveTab), frame.FindChild(ibProtocol::ibFrame::View), patch);
	}

	// THE EVENTS — what happened since the last answer: read from the answer, never kept in the frame. Exit done: the
	// client goes once its call has settled (Call, AllowClose).
	if (answer.GetBool(ibProtocol::ibFrame::Exit))
		m_exitGranted = true;
	// A debugger left its loop — the window raised, as the desktop's raised its own after the loop.
	if (answer.GetBool(ibProtocol::ibFrame::Raise))
		RaiseFrame();
	if (m_outputWindow != nullptr) {
		if (answer.GetBool(ibProtocol::ibFrame::Clear))
			m_outputWindow->Clear();
		const std::vector<ibProtocolNode> messages = answer.FindChild(ibProtocol::ibFrame::Messages).Children();
		for (const ibProtocolNode& message : messages)
			m_outputWindow->Output(message.GetString(ibProtocol::ibFrame::ibMessage::Text), LevelOf(message.GetInt(ibProtocol::ibFrame::ibMessage::Level)));
	}
}

void ibFrontendMainFrame::DrawMenu(const ibProtocolNode& menu)
{
	// One of the same shape — only what may be pressed is set.
	ibString shape;
	ShapeOf(menu, shape);
	if (shape == m_menuShape) {
		std::function<void(const ibProtocolNode&)> enable = [this, &enable](const ibProtocolNode& items) {
			for (const ibProtocolNode& item : items.Children()) {
				if (item.GetBool(ibProtocol::ibFrame::ibMenu::Separator))
					continue;
				if (item.Has(ibProtocol::ibFrame::ibMenu::Command) || item.Has(ibProtocol::ibFrame::ibMenu::Schema))
					GetMenuBar()->Enable(MenuIdOf(item), item.GetBool(ibProtocol::ibFrame::ibMenu::Enabled));
				else
					enable(item);
			}
		};
		enable(menu);
		return;
	}

	// Built anew — the window's own bar, never a view's merged one: that one is merged again from it below.
	if (m_pMyMenuBar != nullptr)
		SetChildMenuBar(nullptr);

	// Each command and schema under its id of the menu's range (MenuIdOf), its picture the art provider's (the
	// desktop's stockPicture) or its own by its id (the desktop's SetBitmap — an engine's, kept here); its key is put on
	// once the bar is up (UpdateHotKeys).
	m_menuShape = shape;
	m_menuItems.clear();
	std::function<void(wxMenu*, const ibProtocolNode&)> fill = [this, &fill](wxMenu* into, const ibProtocolNode& items) {
		for (const ibProtocolNode& item : items.Children()) {
			if (item.GetBool(ibProtocol::ibFrame::ibMenu::Separator)) {
				into->AppendSeparator();
				continue;
			}
			const wxString title = item.GetString(ibProtocol::ibFrame::ibMenu::Title);
			if (item.Has(ibProtocol::ibFrame::ibMenu::Command) || item.Has(ibProtocol::ibFrame::ibMenu::Schema)) {
				const int id = MenuIdOf(item);
				wxMenuItem* const added = into->Append(id, title);
				const wxArtID art = ArtOf(item.GetInt(ibProtocol::ibFrame::ibMenu::StockPicture));
				if (!art.IsEmpty())
					added->SetBitmap(wxArtProvider::GetBitmapBundle(art, wxART_MENU, wxSize(16, 16)));
				else if (item.Has(ibProtocol::ibFrame::ibMenu::Picture)) {
					const wxBitmap picture = ibProtocolPicture(item, ibProtocol::ibFrame::ibMenu::Picture);
					if (picture.IsOk())
						added->SetBitmap(picture);
				}
				added->Enable(item.GetBool(ibProtocol::ibFrame::ibMenu::Enabled));
				m_menuItems[id] = MenuItemOf(item);
				continue;
			}
			wxMenu* const submenu = new wxMenu();
			fill(submenu, item);
			into->AppendSubMenu(submenu, title);
		}
	};

	// File and Edit first — the window's own, moved over as they are: the files opened last are kept in File's. A mode's
	// Configuration and Debug go over with them, after Edit; its Help, last.
	wxMenuBar* const previous = GetMenuBar();
	for (wxMenu* const own : { m_menuFile, m_menuEdit, m_menuConfiguration, m_menuDebug, m_menuHelp }) {
		for (size_t pos = 0; previous != nullptr && pos < previous->GetMenuCount(); ++pos) {
			if (previous->GetMenu(pos) == own) {
				previous->Remove(pos);
				break;
			}
		}
	}

	wxMenuBar* const bar = new wxMenuBar();
	bar->Append(m_menuFile, wxGetStockLabel(wxID_FILE));
	bar->Append(m_menuEdit, wxGetStockLabel(wxID_EDIT));
	if (m_menuConfiguration != nullptr)
		bar->Append(m_menuConfiguration, _("Configuration"));
	if (m_menuDebug != nullptr)
		bar->Append(m_menuDebug, _("Debug"));
	for (const ibProtocolNode& top : menu.Children()) {
		wxMenu* const submenu = new wxMenu();
		fill(submenu, top);
		bar->Append(submenu, top.GetString(ibProtocol::ibFrame::ibMenu::Title));
	}
	if (m_menuHelp != nullptr)
		bar->Append(m_menuHelp, wxGetStockLabel(wxID_HELP, wxSTOCK_NOFLAGS));
	SetMenuBar(bar);
	delete previous;
	UpdateHotKeys();

	// The active view's own menu, merged into the new bar.
	if (ibFrontendView* const view = m_docManager->GetCurrentView())
		ActivateView(view, true);
}

ibFrontendMainFrame::ibMenuItem ibFrontendMainFrame::MenuItemOf(const ibProtocolNode& item)
{
	ibMenuItem what;
	what.schema = item.Has(ibProtocol::ibFrame::ibMenu::Schema);
	what.number = static_cast<int>(what.schema ? item.GetInt(ibProtocol::ibFrame::ibMenu::Schema) : item.GetInt(ibProtocol::ibFrame::ibMenu::Command));
	what.command = what.schema ? static_cast<int>(item.GetInt(ibProtocol::ibFrame::ibMenu::Command)) : 0;
	what.shortcut = item.GetString(ibProtocol::ibFrame::ibMenu::Shortcut);
	return what;
}

void ibFrontendMainFrame::DrawTemplates(const ibProtocolNode& templates)
{
	// THE SERVER'S TEMPLATES, AND NO OTHERS — each opens its file on the server, or has the server make a new one; its
	// picture by its id, the pictures of all asked for at once.
	for (ibFrontendDocTemplate* const temp : m_docManager->GetTemplatesVector())
		delete temp;
	const std::vector<ibProtocolNode> items = templates.Children();
	ibProtocolPictureReceive(templates);
	for (const ibProtocolNode& item : items) {
		const wxString title = item.GetString(ibProtocol::ibFile::Title);
		const long flags = ibTEMPLATE_VISIBLE | (item.GetBool(ibProtocol::ibFile::OnlyOpen) ? ibTEMPLATE_ONLY_OPEN : 0);
		ibFrontendDocTemplate* const temp = new ibFrontendDocTemplate(m_docManager, title, item.GetString(ibProtocol::ibFile::Mask),
			wxEmptyString, item.GetString(ibProtocol::ibFile::Extension), title, title, CLASSINFO(ibFrontendFileDocument), nullptr, flags);
		const wxBitmap picture = ibProtocolPicture(item, ibProtocol::ibFile::Picture);
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			temp->SetClassIcon(icon);
		}
	}
}

ibTabDocument* ibFrontendMainFrame::CreateDocument(ibProtocol::ibView::ibKind kind, long long tabId)
{
	if (kind == ibProtocol::ibView::ibKind::AuditLog)
		return new ibAuditLogDocument(tabId);
	if (kind == ibProtocol::ibView::ibKind::HomePage)
		return new ibHomePageDocument(tabId);
	return new ibFormVisualDocument(tabId);
}

void ibFrontendMainFrame::DrawTabs(const ibProtocolNode& tabs)
{
	wxAuiMDIClientWindow* const client = GetClientWindow();
	if (client == nullptr)
		return;

	// The drawing changes pages, and wx says so as if the person had: those are not theirs to answer.
	m_drawing = true;
	client->Freeze();

	// EACH TAB A DOCUMENT THE SERVER RAISED — made here, with no template (ibTabDocument), by the tab's Kind: a form, or a
	// view of its own; its view in a tab of the window (CreateChildFrame).
	std::set<long long> drawn;
	for (const ibProtocolNode& tab : tabs.Children()) {
		const long long id = tab.GetId();
		drawn.insert(id);
		const wxString title = tab.GetString(ibProtocol::ibFrame::ibTab::Title);

		ibTabDocument* const document = FindDocument(id);
		if (document != nullptr) {
			if (document->GetTitle() != title) {
				document->SetTitle(title);
				document->OnChangeFilename(true);
			}
			continue;
		}
		// …and a tab closing is not opened again — its form went with the window it was shown in (ibDialogDocChildFrame).
		if (m_tabsClosing.count(id) != 0)
			continue;
		ibTabDocument* const opened = CreateDocument(static_cast<ibProtocol::ibView::ibKind>(tab.GetInt(ibProtocol::ibFrame::ibTab::Kind)), id);
		m_documents[id] = opened;
		opened->SetTitle(title);
		const wxBitmap icon = ibProtocolPicture(tab, ibProtocol::ibFrame::ibTab::Icon);
		if (icon.IsOk()) {
			wxIcon picture;
			picture.CopyFromBitmap(icon);
			opened->SetIcon(picture);
		}
		// Under the form that owns it, when the server says it is owned — it goes with that one; the doc manager's
		// otherwise.
		if (ibTabDocument* const owner = FindDocument(tab.GetInt(ibProtocol::ibFrame::ibTab::Parent)))
			opened->SetDocParent(owner);
		else
			m_docManager->AddDocument(opened);

		// Not made — its view was not. Made with the State the frame drew it in, when it is the tab the frame shows.
		const ibProtocolNode& frame = m_communicator->GetFrame();
		if (id == frame.GetInt(ibProtocol::ibFrame::ActiveTab))
			opened->SetOpeningState(frame.FindChild(ibProtocol::ibFrame::View).FindChild(ibProtocol::ibView::State));
		const bool made = opened->OnCreate(wxEmptyString, ibDOC_NEW);
		opened->SetOpeningState(ibProtocolNode());
		if (!made) {
			m_documents.erase(id);
			opened->DeleteAllViews();
			continue;
		}
		// The command processor after the views, as the template's documents get theirs (InitDocument): a view's own
		// undo (the code editor's) needs the view built.
		opened->SetCommandProcessor(opened->OnCreateCommandProcessor());

		// The start page — first, and never closed: a locked tab, which wx keeps ahead of the others.
		if (tab.GetBool(ibProtocol::ibFrame::ibTab::Locked)) {
			const int page = client->GetPageIndex(opened->GetDocumentWindow());
			if (page != wxNOT_FOUND)
				client->SetPageKind(static_cast<size_t>(page), wxAuiTabKind::Locked);
		}
	}

	// The tabs the frame no longer has — closed by the server: their documents go here (ibTabDocument::Close), each
	// once. Looked for afresh before each: a form closed takes the forms it owns with it.
	std::vector<long long> gone;
	for (const auto& kept : m_documents) {
		if (drawn.count(kept.first) == 0)
			gone.push_back(kept.first);
	}
	for (const long long id : gone) {
		if (ibTabDocument* const document = FindDocument(id)) {
			document->Close();
			document->DeleteAllViews();
		}
	}

	client->Thaw();
	m_drawing = false;
}

void ibFrontendMainFrame::DrawView(long long active, const ibProtocolNode& view, const ibProtocolNode& patch)
{
	wxAuiMDIClientWindow* const client = GetClientWindow();
	ibTabDocument* const chosen = FindDocument(active);
	ibFrontendView* const shown = chosen != nullptr ? chosen->GetFirstView() : nullptr;
	if (client == nullptr || shown == nullptr)
		return;

	// The drawing changes pages, and wx says so as if the person had: those are not theirs to answer.
	m_drawing = true;

	// Switched to only when the notebook shows another: a page selected again takes the focus (wxAuiNotebook), and a tab
	// just opened was selected as it was added — a choice form opened by a cell's "..." must leave the cell's editor its
	// focus, as the desktop's did.
	wxAuiMDIChildFrame* const child = dynamic_cast<wxAuiMDIChildFrame*>(shown->GetFrame());
	if (child != nullptr && client->GetSelection() != client->GetPageIndex(child)) {
		wxWindowUpdateLocker freeze(client);
		child->Activate();
	}

	// …and its view the doc manager's current one when the active tab is another — the patch names ActiveTab: a tab
	// closed picks the page after it with the notebook's events off (ibAuiDocChildFrame::Destroy), so that page's view is
	// never told it became active. Not on any other answer, nor on a frame drawn whole: the person may be in a view that is
	// no tab (the configuration's tree), and the answer to their edit must leave them there.
	if (patch.Has(ibProtocol::ibFrame::ActiveTab) && m_docManager->GetCurrentView() != shown)
		shown->Activate(true);

	// The active tab's view — the one the frame carries, drawn by what the patch changed in it: none in a frame sent
	// whole; a patch that leaves it as it was changes nothing (a node with no entries) — and a view not built yet is built
	// all the same: a tab just opened is patched from the view the client kept, another tab's, and two alike leave no View
	// in the patch. The other tabs keep what was drawn in them last.
	chosen->DrawView(view,!patch.IsNode() || patch.Has(ibProtocol::ibFrame::View) ? patch.FindChild(ibProtocol::ibFrame::View) : ibProtocolNode());

	m_drawing = false;
}

void ibFrontendMainFrame::Settle(const ibProtocolNode& answer)
{
	// The client's part of a call, beside its exchange (the communicator's line): the window drawn from the answer.
	const wxLongLong began = wxGetUTCTimeMillis();
	// The pictures it names asked for at once, before a part of the window asks for its own: the designer's start was two
	// picture exchanges, the templates' and the tree's.
	ibProtocolPictureReceive(answer);
	Draw(answer);
	ibJournalInfo(wxT("call"), wxT("  drawn in %lld ms"), (wxGetUTCTimeMillis() - began).GetValue());
	Ask();
}

void ibFrontendMainFrame::Ask()
{
	// THE SERVER'S CODE WAITS WHERE IT ASKED. Each question is put to the person and answered back; the work goes on
	// from there and may ask again — until nothing is pending.
	for (;;) {
		const ibProtocolNode request = GetRequest();
		if (!request.IsNode())
			return;

		// THE WINDOW UP ALREADY — a call made while it is up (a choice form's, over it) settles to its question: the window
		// answers it itself; what was chosen for its cell is handed over. Another question is another window (the choice
		// form's own settings), put up over it.
		const wxString id = request.GetString(ibProtocol::ibRequest::Id);
		if (!m_asking.empty() && m_asking.back() == id) {
			TakeChosen(request);
			return;
		}

		ibProtocolNode response;
		m_asking.push_back(id);
		ShowRequest(request, response);
		m_asking.pop_back();
		// …and a choice asked from inside it ends with it.
		if (m_choosing.depth > m_asking.size())
			m_choosing = ibChoosing();

		// …the answer to the question pending NOW: a window that answered along the way (Respond) was asked again.
		if (!Respond(response))
			return;
	}
}

void ibFrontendMainFrame::TakeChosen(const ibProtocolNode& asked)
{
	const ibProtocolNode chosen = asked.FindChild(ibProtocol::ibRequest::ibListSettings::Chosen);
	if (m_choosing.owner == nullptr || !chosen.IsNode() || chosen.GetInt(ibProtocol::ibRequest::ibListSettings::Choice) != m_choosing.choice)
		return;
	ibControlFrame* const owner = m_choosing.owner;
	m_choosing = ibChoosing();
	ibDataNode node;
	ibReadProtocolNode(chosen.FindChild(ibProtocol::ibRequest::ibListSettings::Value), node);
	ibValue value = ibValue::FromNode(node);
	owner->ChoiceProcessing(value);
}

bool ibFrontendMainFrame::ChooseValue(ibControlFrame* owner, const ibValue& current)
{
	// ⭐ NO WINDOW ASKS — the cell is on the active tab (a designer's composer panel; 2026-10-29: a parameter's "..." and a
	// condition's opened nothing): the same act goes to the tab, whose view chooses through the configuration there.
	const bool asking = !m_asking.empty() && IsSettingsRequest(GetRequest());
	const long long tab = asking ? 0 : GetActiveTab();
	if (owner == nullptr || (!asking && tab == 0))
		return false;

	m_choosing = ibChoosing{ owner, ++m_choice, m_asking.size() };

	// The type the cell stands on, and the value in it when there is one — the server's own node, given back.
	ibProtocolNode act;
	act.SetValue(ibProtocol::ibRequest::ibListSettings::Act, static_cast<long long>(ibProtocol::ibRequest::ibListSettings::ibAct::Choose))
		.SetValue(ibProtocol::ibRequest::ibListSettings::Type, wxString() << current.GetClassType())
		.SetValue(ibProtocol::ibRequest::ibListSettings::Choice, m_choice);
	if (!current.IsEmpty()) {
		ibDataNode node;
		current.Serialize(node);
		ibWriteProtocolNode(node, act.Child(ibProtocol::ibRequest::ibListSettings::Value));
	}

	// …on the tab: the value comes back with its view (State Chosen), which its document hands to TakeChosen as it draws.
	if (!asking) {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibEvent::Tab, tab)
			.SetValue(ibProtocol::ibEvent::Event, static_cast<int>(ibProtocol::ibEvent::ibKind::Select))
			.SetValue(ibProtocol::ibEvent::Args, act);
		return Call(ibProtocol::ibMethod::Event, params, answer);
	}

	if (!Respond(act))
		return false;

	// Its quick choice asked on the way — put up and answered — or its choice form opened: either way the window's
	// question comes back, with the value when it was chosen at once.
	for (;;) {
		const ibProtocolNode asked = GetRequest();
		if (!asked.IsNode())
			return true;
		if (IsSettingsRequest(asked)) {
			TakeChosen(asked);
			return true;
		}
		ibProtocolNode response;
		ShowRequest(asked, response);
		if (!Respond(response))
			return true;
	}
}

ibProtocolNode ibFrontendMainFrame::GetRequest() const
{
	return m_communicator->GetFrame().FindChild(ibProtocol::ibFrame::Request);
}

bool ibFrontendMainFrame::Respond(const ibProtocolNode& response)
{
	const ibProtocolNode request = GetRequest();
	if (!request.IsNode())
		return false;

	const wxString id = request.GetString(ibProtocol::ibRequest::Id);
	ibProtocolNode params, next;
	params.SetValue(ibProtocol::ibRequest::Id, id).SetValue(ibProtocol::ibRequest::Response, response);
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	if (!m_communicator->Call(ibProtocol::ibMethod::Respond, params, next, refusal, error)) {
		Refused(refusal, error);
		return false;
	}
	Draw(next);

	// …a window up answering along the way is asked again: the question pending now is its own.
	if (!m_asking.empty() && m_asking.back() == id)
		m_asking.back() = GetRequest().GetString(ibProtocol::ibRequest::Id);
	return true;
}

void ibFrontendMainFrame::ShowRequest(const ibProtocolNode& request, ibProtocolNode& response)
{
	// Over the window of the form it is asked from — the desktop's: a table's settings over the table's own window — the
	// active tab's: the main window for a tab, the dialog for a form opened over a modal one (CreateChildFrame).
	wxWindow* parent = IsShown() ? static_cast<wxWindow*>(this) : nullptr;
	if (ibTabDocument* const document = FindDocument(GetActiveTab())) {
		if (wxWindow* const window = document->GetDocumentWindow())
			parent = wxGetTopLevelParent(window);
	}

	switch (static_cast<ibProtocol::ibRequest::ibKind>(request.GetInt(ibProtocol::ibRequest::Kind))) {
	case ibProtocol::ibRequest::ibKind::Message: {
		// wx's own flags and codes travel — this client is wx.
		const int button = wxMessageBox(request.GetString(ibProtocol::ibRequest::ibMessage::Text), request.GetString(ibProtocol::ibRequest::ibMessage::Caption),
			static_cast<int>(request.GetInt(ibProtocol::ibRequest::ibMessage::Style, wxOK)), parent);
		response.SetValue(ibProtocol::ibRequest::ibMessage::Button, button);
		break;
	}
	case ibProtocol::ibRequest::ibKind::Error: {
		// THE DESKTOP'S ERROR WINDOW over a script's failure — the act chosen is the answer (ibProtocol::ibRequest::ibError::ibAct); closed —
		// answered with no Act, which the server takes for Close window.
		ibDialogError dialog(this);
		dialog.SetErrorMessage(request.GetString(ibProtocol::ibRequest::ibError::Text));
		const int act = dialog.ShowModal();
		if (act == static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::CloseWindow) || act == static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::GotoDesigner)
			|| act == static_cast<int>(ibProtocol::ibRequest::ibError::ibAct::CloseProgram))
			response.SetValue(ibProtocol::ibRequest::ibError::Act, act);
		break;
	}
	case ibProtocol::ibRequest::ibKind::Print: {
		// THE DESKTOP'S PrintSpreadsheetDocument over a script's sheet, read into a document of its own as a spreadsheet's
		// fetch is (ibValueGridBox): the page as the preview was last left, the same as File -> Print.
		auto document = std::make_shared<ibSpreadsheetObject>();
		auto sheet = std::make_shared<ibDataNode>();
		ibReadProtocolNode(request.FindChild(ibProtocol::ibRequest::ibPrint::Sheet), *sheet);
		ibSpreadsheetDescriptionMemory::ReadNode(ibDataValue::Child(sheet), document->GetSpreadsheetDesc());

		wxScopedPtr<ibGridEditorPrintout> printout(new ibGridEditorPrintout(document));
		ibPrintPreviewFrame::ApplyFitToPageWidth(printout.get());
		wxPrintDialogData printDialogData(m_docManager->GetPageSetupDialogData().GetPrintData());
		wxPrinter printer(&printDialogData);
		response.SetValue(ibProtocol::ibRequest::ibPrint::Done, printer.Print(parent, printout.get(), true));
		break;
	}
	case ibProtocol::ibRequest::ibKind::Schedule: {
		// THE DESKTOP'S ibDialogJobSchedule — it edits a buffer, the schedule as it came; OK answers the one edited,
		// cancelled — none.
		ibDataNode stored;
		ibReadProtocolNode(request.FindChild(ibProtocol::ibRequest::ibSchedule::Schedule), stored);
		ibJobScheduleDescription schedule;
		ibJobScheduleDescriptionMemory::ReadNode(ibDataValue::Child(std::make_shared<ibDataNode>(stored)), schedule);

		ibDialogJobSchedule dialog(parent, schedule);
		if (dialog.ShowModal() != wxID_OK)
			break;
		ibDataValue edited;
		if (ibJobScheduleDescriptionMemory::WriteNode(edited, dialog.GetSchedule()) && edited.Kind() == ibDataKind::Child)
			ibWriteProtocolNode(*edited.AsChild(), response.Child(ibProtocol::ibRequest::ibSchedule::Schedule));
		break;
	}
	case ibProtocol::ibRequest::ibKind::Choice: {
		const std::vector<ibProtocolNode> items = request.Children();
		wxArrayString captions;
		int selected = 0;
		wxArrayInt ticked;
		for (std::size_t i = 0; i < items.size(); ++i) {
			captions.Add(items[i].GetString(ibProtocol::ibRequest::ibChoice::Caption));
			if (items[i].GetBool(ibProtocol::ibRequest::ibChoice::Selected)) {
				selected = static_cast<int>(i);
				ticked.Add(static_cast<int>(i));
			}
		}
		// SEVERAL — the desktop's wxMultiChoiceDialog, those Selected ticked; cancelled — answered with no Ids.
		if (request.GetBool(ibProtocol::ibRequest::ibChoice::Multiple)) {
			wxMultiChoiceDialog dialog(parent, request.GetString(ibProtocol::ibRequest::ibChoice::Caption), request.GetString(ibProtocol::ibRequest::ibChoice::Caption),
				captions);
			dialog.SetSelections(ticked);
			if (dialog.ShowModal() == wxID_OK) {
				for (const int index : dialog.GetSelections()) {
					if (index >= 0 && index < static_cast<int>(items.size()))
						response.AddItem(ibProtocol::ibRequest::ibChoice::Ids, items[index].GetInt(ibProtocol::ibRequest::ibChoice::Id));
				}
			}
			break;
		}
		wxSingleChoiceDialog dialog(parent, request.GetString(ibProtocol::ibRequest::ibChoice::Caption), request.GetString(ibProtocol::ibRequest::ibChoice::Caption), captions);
		if (!captions.IsEmpty())
			dialog.SetSelection(selected);
		// Cancelled — answered with no Id: the server takes that for no choice.
		if (dialog.ShowModal() == wxID_OK && dialog.GetSelection() >= 0
			&& dialog.GetSelection() < static_cast<int>(items.size()))
			response.SetValue(ibProtocol::ibRequest::ibChoice::Id, items[dialog.GetSelection()].GetInt(ibProtocol::ibRequest::ibChoice::Id));
		break;
	}
	case ibProtocol::ibRequest::ibKind::Edit:
		// Answered with nothing: a field changed sends its Change as it does when typed into.
		EditFocused(EditCommandOf(request.GetInt(ibProtocol::ibRequest::ibEdit::Command)));
		break;
	case ibProtocol::ibRequest::ibKind::File: {
		// What may be chosen, as the desktop's doc manager filtered its File → Open: "Title (Mask)|Mask" each.
		wxString wildcard;
		for (const ibProtocolNode& item : request.Children()) {
			const wxString mask = item.GetString(ibProtocol::ibRequest::ibFile::Mask);
			if (!wildcard.IsEmpty())
				wildcard += wxT("|");
			wildcard += item.GetString(ibProtocol::ibRequest::ibFile::Title) + wxT(" (") + mask + wxT(")|") + mask;
		}
		// A file of the server's named — taken down where the person chooses (the desktop's save dialog); none — one of
		// the person's chosen and uploaded (its open dialog).
		const wxString taken = request.GetString(ibProtocol::ibRequest::ibFile::File);
		wxFileDialog dialog(parent, request.GetString(ibProtocol::ibRequest::ibFile::Caption), wxEmptyString, wxEmptyString,
			!wildcard.IsEmpty() ? wildcard : wxString(wxFileSelectorDefaultWildcardStr),
			taken.IsEmpty() ? (wxFD_OPEN | wxFD_FILE_MUST_EXIST) : (wxFD_SAVE | wxFD_OVERWRITE_PROMPT));
		if (!taken.IsEmpty()) {
			// Cancelled, or not taken down — answered with no Done.
			if (dialog.ShowModal() == wxID_OK && DownloadFile(taken, dialog.GetPath()))
				response.SetValue(ibProtocol::ibRequest::ibFile::Done, true);
			break;
		}
		// Cancelled, or not uploaded — answered with no File: the server takes that for nothing chosen.
		wxString file;
		if (dialog.ShowModal() == wxID_OK && UploadFile(dialog.GetPath(), file))
			response.SetValue(ibProtocol::ibRequest::ibFile::File, file);
		break;
	}
	case ibProtocol::ibRequest::ibKind::Generation: {
		// Cancelled — answered with no Id.
		ibDialogGeneration dialog(request);
		ibMetaID id = 0;
		if (dialog.ShowModal(id))
			response.SetValue(ibProtocol::ibRequest::ibGeneration::Id, static_cast<long long>(id));
		break;
	}
	case ibProtocol::ibRequest::ibKind::ViewMode: {
		// The server's mode numbers are the data view's own (ibProtocol::ibForm::ibTable::ibViewMode); cancelled — answered with no Mode.
		wxTableViewModeDialog dialog(parent, wxID_ANY, static_cast<ibDataViewViewMode>(request.GetInt(ibProtocol::ibRequest::ibViewMode::Mode)),
			ibProtocolPicture(request, ibProtocol::ibRequest::ibViewMode::Picture));
		if (dialog.ShowModal() == wxID_OK)
			response.SetValue(ibProtocol::ibRequest::ibViewMode::Mode, static_cast<long long>(dialog.GetViewMode()));
		break;
	}
	case ibProtocol::ibRequest::ibKind::Menu: {
		// THE DESKTOP'S POPUP MENU, where the mouse is. Dismissed — answered with no Id.
		wxMenu menu;
		std::vector<long long> ids;
		AppendMenuItems(menu, request.Children(), ids);
		const int picked = GetPopupMenuSelectionFromUser(menu) - (wxID_HIGHEST + 1);
		if (picked >= 0 && picked < static_cast<int>(ids.size()))
			response.SetValue(ibProtocol::ibRequest::ibMenu::Id, ids[picked]);
		break;
	}
	case ibProtocol::ibRequest::ibKind::SimpleChoice: {
		// THE DESKTOP'S ibTypeControlFactory::SimpleChoice — a number on its calculator, a date on its calendar, dropped
		// under the field pressed; the value chosen is the answer, a window closed with none — none.
		ibDataNode stored;
		ibReadProtocolNode(request.FindChild(ibProtocol::ibRequest::ibSimpleChoice::Value), stored);
		ibSimpleChoiceHolder holder(ibValue::FromNode(stored));
		if (ibTypeControlFactory::SimpleChoice(&holder, holder.GetValue().GetClassType(), FocusedEditor(this)))
			holder.Wait();
		if (holder.HasChosen()) {
			ibDataNode chosen;
			holder.GetChosen().Serialize(chosen);
			ibWriteProtocolNode(chosen, response.Child(ibProtocol::ibRequest::ibSimpleChoice::Value));
		}
		break;
	}
	case ibProtocol::ibRequest::ibKind::ListSettings:
		// THE DESKTOP'S ibDialogListSettings::ShowUserSettings over a table's model — the model is the server's: the
		// setting and its fields come in the question, the edited setting is the answer; cancelled — answered with none.
		ibDialogListSettings::ShowUserSettings(parent, request, response);
		break;
	case ibProtocol::ibRequest::ibKind::ComposerSettings:
		// THE DESKTOP'S ibDialogComposerSettings::ShowUserSettings over a report's model — the same road: the setting, the
		// composition and its fields come in the question, the edited setting is the answer; cancelled — none.
		ibDialogComposerSettings::ShowUserSettings(parent, request, response);
		break;
	case ibProtocol::ibRequest::ibKind::FormEditor: {
		// THE DESKTOP'S ibValueForm::ChangeForm — its editor over the form of the active tab; what a person may arrange of
		// each control comes in the question (Controls). Its acts are answered on the way (UpdateForm, the setting's
		// doors); closed — answered with no Act.
		// ⚠ Asked only of a form (ibValueForm::ChangeForm) — a form's tab, or a cell of the start page: the form the person
		// is in, its view's host (the command bar took the focus as it was pressed).
		const ibTabDocument* const document = FindDocument(GetActiveTab());
		const ibVisualHostView* const view = document != nullptr ? dynamic_cast<const ibVisualHostView*>(document->GetFirstView()) : nullptr;
		ibValueForm* const form = view != nullptr && view->GetVisualHost() != nullptr ? view->GetVisualHost()->GetValueForm() : nullptr;
		if (form == nullptr)
			break;
		ibDialogFormEditor dlg(form);
		dlg.ShowModal();
		break;
	}
	case ibProtocol::ibRequest::ibKind::SavedSettings: {
		// THE SHELF'S WINDOW IS OPEN THROUGH EVERY ACT BUT THE LAST — each one answered on the way (Respond, inside the
		// shelf's calls: settingsComposer.h), the shelf asked again in its place. Closed — answered with no Act.
		ibDataComposer composer;
		ibDialogSavedSettings::Show(parent, composer,
			request.GetInt(ibProtocol::ibRequest::ibSavedSettings::Mode) == static_cast<int>(ibProtocol::ibRequest::ibSavedSettings::ibMode::Save)
				? ibDialogSavedSettings::Mode::Save : ibDialogSavedSettings::Mode::Restore,
			ibSettingsCategory::Custom, ibGuid(request.GetString(ibProtocol::ibRequest::ibSavedSettings::Key)), nullptr);
		break;
	}
	default:
		// A kind this client does not know — answered with nothing, which the server takes for cancelled.
		break;
	}
}

bool ibFrontendMainFrame::UploadFile(const wxString& path, wxString& file)
{
	wxFile source(path);
	if (!source.IsOpened())
		return false;   // said to the person by wx's own log

	// In parts, as the server keeps them: the first names the file and is answered its id, each next one names the id.
	// An empty file is its first call with nothing in it.
	file.clear();
	for (bool first = true;; first = false) {
		ibMemoryBuffer part;
		const ssize_t read = source.Read(part.GetWriteBuf(kUploadPartSize), kUploadPartSize);
		if (read == wxInvalidOffset)
			return false;
		part.UngetWriteBuf(static_cast<std::size_t>(read));
		if (read == 0 && !first)
			return true;

		ibProtocolNode params, answer;
		if (first)
			params.SetValue(ibProtocol::ibFile::Name, wxFileName(path).GetFullName());
		else
			params.SetValue(ibProtocol::ibFile::File, file);
		params.SetValue(ibProtocol::ibFile::Data, part);

		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		ibString error;
		if (!m_communicator->Call(ibProtocol::ibMethod::Upload, params, answer, refusal, error)) {
			Refused(refusal, error);
			return false;
		}
		file = answer.GetString(ibProtocol::ibFile::File);
		if (static_cast<std::size_t>(read) < kUploadPartSize)
			return true;
	}
}

bool ibFrontendMainFrame::DownloadFile(const wxString& file, const wxString& path)
{
	wxFile target;
	if (!target.Create(path, true))
		return false;   // said to the person by wx's own log

	// In parts, as the server keeps them — the first answer says how many; an empty file is its part 0, empty.
	for (long long part = 0, parts = 1; part < parts; ++part) {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibFile::File, file).SetValue(ibProtocol::ibFile::Part, part);

		ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
		ibString error;
		if (!m_communicator->Call(ibProtocol::ibMethod::Download, params, answer, refusal, error)) {
			Refused(refusal, error);
			return false;
		}
		parts = answer.GetInt(ibProtocol::ibFile::Parts);
		const ibMemoryBuffer data = answer.GetBinary(ibProtocol::ibFile::Data);
		if (data.GetDataLen() != 0 && target.Write(data.GetData(), data.GetDataLen()) != data.GetDataLen())
			return false;
	}
	return target.Close();
}

void ibFrontendMainFrame::Refused(ibProtocol::ibRefusal refusal, const wxString& error)
{
	const wxString said = !error.IsEmpty() ? error : wxString(_("The server refused it."));

	// THE SESSION IS GONE — the server's, or the connection to it: the window has nothing left to show. It goes FIRST and
	// says why after, ONCE. Said before it went, the message box ran a message loop in which every call due (a window's
	// own refresh, a timer's) met the same refusal and put up a box of its own, one after another; the window going, a
	// refusal it meets is said by nobody.
	if (refusal == ibProtocol::ibRefusal::NoSession) {
		if (m_closingWindow)
			return;
		Close(true);
		wxMessageBox(said, GetTitle(), wxOK | wxICON_ERROR);
		return;
	}

	wxMessageBox(said, GetTitle(), wxOK | wxICON_WARNING, IsShown() ? this : nullptr);
}

void ibFrontendMainFrame::CallPosted()
{
	// The session gone — the window is closing: there is nobody to call.
	if (!m_communicator->IsLoggedIn()) {
		m_posted.clear();
		return;
	}
	// Every call under way stands at a question — the person works in what its window lets them reach (Post).
	if (m_calling != m_asking.size() || m_posted.empty())
		return;

	const ibPostedCall posted = std::move(m_posted.front());
	m_posted.pop_front();
	ibProtocolNode answer;
	Call(posted.method, posted.params, answer);

	// A tab's close answered — closed, or kept by the person: it may be closed again.
	if (posted.method == ibProtocol::ibMethod::Close)
		m_tabsClosing.erase(posted.params.GetInt(ibProtocol::ibFrame::Tab));

	// What was held while it waited, held now — after it, as it was done. A server that takes none held is called with
	// each, before what was posted later.
	auto next = m_posted.begin();
	for (const ibProtocolNode& event : posted.held) {
		if (m_communicator->Hold(event))
			continue;
		next = m_posted.insert(next, ibPostedCall{ ibProtocol::ibMethod::Event, event }) + 1;
		CallAfter(&ibFrontendMainFrame::CallPosted);
	}
}

ibTabDocument* ibFrontendMainFrame::DocumentAt(int page) const
{
	wxAuiMDIClientWindow* const client = GetClientWindow();
	if (client == nullptr || page == wxNOT_FOUND)
		return nullptr;

	// The form whose window is that page — of the frame's own, an owned one among them.
	for (const auto& kept : m_documents) {
		if (client->GetPageIndex(kept.second->GetDocumentWindow()) == page)
			return kept.second;
	}
	return nullptr;
}

void ibFrontendMainFrame::RemoveDocument(const ibTabDocument* document)
{
	m_documents.erase(document->GetTabId());
}

ibProtocolNode ibFrontendMainFrame::FindTab(long long tabId) const
{
	for (const ibProtocolNode& tab : m_communicator->GetFrame().FindChild(ibProtocol::ibFrame::Tabs).Children()) {
		if (tab.GetId() == tabId)
			return tab;
	}
	return ibProtocolNode();
}

long long ibFrontendMainFrame::GetActiveTab() const
{
	return m_communicator->GetFrame().GetInt(ibProtocol::ibFrame::ActiveTab);
}

long long ibFrontendMainFrame::GetFrameNumber() const
{
	return m_communicator->GetFrameNumber();
}

ibTabDocument* ibFrontendMainFrame::FindDocument(long long tabId) const
{
	// The frame's own — a tab's, or a dialog's over a modal (CreateChildFrame).
	const auto found = m_documents.find(tabId);
	return found != m_documents.end() ? found->second : nullptr;
}

wxMenu* ibFrontendMainFrame::GetDefaultMenu(int idMenu) const
{
	if (idMenu == wxID_FILE)
		return m_menuFile;
	if (idMenu == wxID_EDIT)
		return m_menuEdit;
	return nullptr;
}

wxWindow* ibFrontendMainFrame::CreateChildFrame(ibFrontendView* view, const wxPoint& pos, const wxSize& size, long style)
{
	// The desktop's: a form opened while a modal dialog is open (wxCREATE_SDI_FRAME, the document's OnCreate) is a dialog
	// over it — a tab could not be reached until the modal closed.
	ibFrontendDocument* const document = view->GetDocument();

	if ((style & wxCREATE_SDI_FRAME) != 0) {

		wxWindow* parent = wxTheApp->GetTopWindow();

		// Over the INNERMOST modal — the last one opened (the list runs in the order they were made): over the first, a
		// form chosen from a dialog opened by a modal window (a condition's, in the list's settings) stood behind it.
		for (wxWindow* window : wxTopLevelWindows) {
			if (window->IsKindOf(CLASSINFO(wxDialog))) {
				if (((wxDialog*)window)->IsModal()) {
					parent = window;
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

namespace {

// THE TOOLBAR EMPTIED as wxToolBar::ClearTools empties one — the windows of its controls too: wxAuiToolBar::Clear lets go
// of the items alone, and a control left behind (a sheet's Zoom) stayed on show over the next view's tools, and took
// clicks for a view no longer shown (2026-10-20).
void ClearTools(wxAuiToolBar& toolbar)
{
	for (size_t idx = 0; idx < toolbar.GetToolCount(); ++idx)
		if (wxWindow* const window = toolbar.FindToolByIndex(static_cast<int>(idx))->GetWindow())
			window->Destroy();
	toolbar.Clear();
}

} // namespace

void ibFrontendMainFrame::ActivateView(ibFrontendView* view, bool activate)
{
	if (m_docToolbar == nullptr)
		return;

	m_docToolbar->Freeze();

	if (activate) {

		wxFrame* viewFrame = dynamic_cast<wxFrame*>(view->GetFrame());
#if wxUSE_MENUS
		if (viewFrame != nullptr) viewFrame->SetMenuBar(view->CreateMenuBar());
#endif
		if (viewFrame != nullptr) {
			ClearTools(*m_docToolbar);
			view->OnCreateToolbar(m_docToolbar);
			m_docToolbar->Realize();
		}
	}
	else {

#if wxUSE_MENUS
		wxFrame* viewFrame = dynamic_cast<wxFrame*>(view->GetFrame());

		if (viewFrame != nullptr) {

			class ibProcSubMenu {

				static void SetChildEnable(wxMenu* dst, bool enable = false) {

					for (const auto it : dst->GetMenuItems()) {
						if (!it->IsSubMenu()) it->Enable(enable);
						if (it->IsSubMenu()) SetChildEnable(it->GetSubMenu(), enable);
					}
				}

			public:

				static void SetMenuEnabled(wxMenuBar* menuBar, bool enable = false) {
					for (size_t idx = 0; idx < menuBar->GetMenuCount(); idx++) {
						ibProcSubMenu::SetChildEnable(menuBar->GetMenu(idx), enable);
					}
				}
			};

			wxMenuBar* menuBar = view->CreateMenuBar();
			if (menuBar != nullptr)
				ibProcSubMenu::SetMenuEnabled(menuBar);

			viewFrame->SetMenuBar(menuBar);
		}
#endif
		for (size_t idx = 0; idx < m_docToolbar->GetToolCount(); idx++) {
			m_docToolbar->EnableTool(m_docToolbar->FindToolByIndex(idx)->GetId(), false);
		}
	}

	m_docToolbar->Thaw();

	// update frame manager
	UpdateManager();
}

void ibFrontendMainFrame::UpdateFrameManager()
{
	unsigned int view_count = 0;

	if (m_docManager != nullptr) {
		for (auto& doc : m_docManager->GetDocumentsVector()) {
			view_count += (unsigned int)doc->GetViewsVector().size();
		}
	}

	if (view_count == 0) ClearTools(*m_docToolbar);

	wxAuiPaneInfo& infoToolBar = m_mgr.GetPane(m_docToolbar);
	const bool showToolBar = m_docToolbar->GetToolCount() > 0;

	// ⭐ THE TOOLBAR THAT COMES AND GOES STANDS LAST IN ITS ROW. A toolbar row keeps its panes by PIXEL offset, and every
	// layout writes the offsets it arrived at back into them. Appearing before another toolbar, this one pushed it along;
	// hidden again, it left that one where it had been pushed to — a gap as wide as itself. Placed after everything
	// already in the row, its coming moves nothing and its going leaves nothing behind.
	if (showToolBar && !infoToolBar.IsShown()) {
		int rowEnd = 0;
		const wxAuiPaneInfoArray& panes = m_mgr.GetAllPanes();
		for (size_t i = 0; i < panes.GetCount(); ++i) {
			const wxAuiPaneInfo& pane = panes.Item(i);
			if (&pane == &infoToolBar || !pane.IsShown() || !pane.IsToolbar())
				continue;
			if (pane.dock_direction == infoToolBar.dock_direction && pane.dock_layer == infoToolBar.dock_layer
				&& pane.dock_row == infoToolBar.dock_row && pane.dock_pos + pane.rect.width > rowEnd)
				rowEnd = pane.dock_pos + pane.rect.width;
		}
		infoToolBar.Position(rowEnd);
	}
	infoToolBar.Show(showToolBar);

	infoToolBar.BestSize(m_docToolbar->GetSize());
	infoToolBar.FloatingSize(
		m_docToolbar->GetSize().x,
		m_docToolbar->GetSize().y + 25
	);

	m_mgr.Update();
	m_callUpdateFrameManager = false;
}

//********************************************************************************
//*                                    Events                                    *
//********************************************************************************

void ibFrontendMainFrame::OnCloseWindow(wxCloseEvent& event)
{
	// wx carries the force flag on the event: a close that cannot be vetoed is a forced close, so nobody is asked.
	const bool force = !event.CanVeto();

	// ⭐ ONE CLOSE AT A TIME. The asking pass puts questions up ("save the changes?"), and a question runs a message
	// loop — in which a SECOND close can arrive; the close that is asking decides (the desktop's own lesson, 2026-09-16).
	if (m_closingWindow && !force) {
		event.Veto();
		return;
	}

	// Marked WHO is asking, for the whole of AllowClose — a mode's own questions before the base's too (the designer's
	// Save?): a refusal said while the window is closing does not close it a second time.
	if (!force) {
		m_closingWindow = true;
		const bool allowed = AllowClose();
		m_closingWindow = false;
		if (!allowed) {
			event.Veto();
			return;
		}
	}

	// Deliberately NOT event.Skip(): the base would run a second closing policy over the tabs (CloseAll) — the tabs
	// are the server's, and AllowClose asked it.
	Destroy();
}

void ibFrontendMainFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	Close();
}

void ibFrontendMainFrame::OnCloseAll(wxCommandEvent& WXUNUSED(event))
{
	// The server closes them, in one call — not the base's loop over the active child (wxAuiMDIParentFrame::CloseAll):
	// a tab's Close only posts its close, which that loop took for a refusal, and it stopped at the first.
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibFrame::Command, static_cast<long long>(ibProtocol::ibFrame::ibCommand::CloseAll));
	Call(ibProtocol::ibMethod::Command, params, answer);
}

void ibFrontendMainFrame::OnMenu(wxCommandEvent& event)
{
	// An item of the server's menus — a command or a schema, by the wx id given it.
	const auto found = m_menuItems.find(event.GetId());
	if (found == m_menuItems.end()) {
		event.Skip();
		return;
	}

	const ibMenuItem what = found->second;
	if (what.schema && what.command != 0) {
		// The schema's command — done by the server, which asks the person what it needs to.
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(what.number));
		params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(what.command));
		Call(ibProtocol::ibMethod::Schema, params, answer);
	}
	else if (what.schema) {
		OpenSchema(static_cast<ibProtocol::ibSchema::ibKind>(what.number));
	}
	else {
		ibProtocolNode params, answer;
		params.SetValue(ibProtocol::ibSchema::Command, what.number);
		Call(ibProtocol::ibMethod::Command, params, answer);
	}
}

void ibFrontendMainFrame::OpenSchema(ibProtocol::ibSchema::ibKind schema)
{
	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(schema));
	// The journal shows nothing of its own: its command opens its tab, which the frame brings.
	if (schema == ibProtocol::ibSchema::ibKind::AuditLog) {
		params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(ibProtocol::ibSchema::ibCommand::Open));
		Call(ibProtocol::ibMethod::Schema, params, answer);
	}
	else if (Call(ibProtocol::ibMethod::Schema, params, answer))
		ShowSchema(schema, answer);
}

void ibFrontendMainFrame::OnEditCommand(wxCommandEvent& event)
{
	EditFocused(event.GetId());
}

void ibFrontendMainFrame::OnTabChanged(wxAuiNotebookEvent& event)
{
	// The MDI client keeps its active child by this event too.
	event.Skip();
	if (m_drawing)
		return;

	const ibTabDocument* const document = DocumentAt(event.GetSelection());
	if (document == nullptr || document->GetTabId() == GetActiveTab())
		return;

	// After the event: the answer redraws the tabs, which is no work for the middle of a notebook's own event.
	ibProtocolNode params;
	params.SetValue(ibProtocol::ibFrame::Tab, document->GetTabId());
	Post(ibProtocol::ibMethod::Activate, params);
}

// ---------------------------------------------------------------------------
// ibDocBottomStatusBar — the bar along the bottom.
// ---------------------------------------------------------------------------

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
}

#if OES_STATUSBAR_CUSTOM_INK

void ibDocBottomStatusBar::DrawFieldText(wxDC& dc, const wxRect& rect, int i, int textHeight)
{
	// ⭐ THE ONE LINE THE OLD wxStaticText EXISTED FOR. The generic bar never sets a text
	// colour, so the field is drawn in the DC's default — black — whatever the window's
	// foreground says.
	dc.SetTextForeground(GetForegroundColour());

	if (i != FieldAssistant || GetStatusText(FieldAssistant).IsEmpty()) {
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
