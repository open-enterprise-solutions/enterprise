////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main frame window
////////////////////////////////////////////////////////////////////////////

#include "mainFrame.h"
#include "mainFrameChild.h"

#include <functional>
#include <set>

#include <wx/base64.h>
#include <wx/choicdlg.h>
#include <wx/config.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/wupdlock.h>   // wxWindowUpdateLocker — the tabs held still while one is switched to
#include <wx/evtloop.h>   // wxGUIEventLoop — a server's question waits for the calculator or the calendar

#include "output/outputWindow.h"
#include "objinspect/objinspect.h"

#include "frmclient/docView/docManager.h"
#include "frmclient/docView/templates/docViewFile.h"
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/diagnostics/journal.h"
#include "frmclient/visualView/visualHostClient.h"
#include "frmclient/win/picture.h"
#include "frmclient/win/dlgs/about.h"
#include "frmclient/win/dlgs/activeUser.h"
#include "frmclient/win/dlgs/formEditor.h"
#include "frmclient/win/dlgs/generation.h"
#include "frmclient/win/dlgs/settings/savedSettings.h"
#include "frmclient/win/dlgs/settings/list/listSettings.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // a value as the server's question carries it
#include "frmclient/visualView/ctrl/typeControl.h"      // ibTypeControlFactory::SimpleChoice — the calculator, the calendar
#include "frmclient/visualView/ctrl/frame.h"            // ibControlFrame — what they are drawn over
#include "frmclient/win/ctrls/controlTextEditor.h"      // ibControlTextEditor — the field they drop under
#include "frmclient/backend/composition/dataComposer.h"
#include "frmclient/win/ctrls/tableView.h"
#include "frmclient/win/ctrls/floatingNotebook.h"
#include "frmclient/win/theme/luna_tabart.h"
#include "frmclient/win/theme/luna_dockart.h"

namespace {

// The wx ids the server's menus take — a range of their own, given out anew each time the menu is built.
constexpr int kFirstMenuId = wxID_HIGHEST + 5000;
constexpr int kLastMenuId  = wxID_HIGHEST + 5999;

// A file goes up in parts of at most this — what the server keeps one part as (ibTempStorage::kPartSize).
constexpr std::size_t kUploadPartSize = 1024 * 1024;

// The stock picture an item is shown with — the art provider's (artProvider/), by the number the server names it by.
wxArtID ArtOf(long long picture)
{
	switch (static_cast<ibProtocolStockPicture>(picture)) {
	case ibProtocolStockPicture::New:    return wxART_NEW;
	case ibProtocolStockPicture::Open:   return wxART_FILE_OPEN;
	case ibProtocolStockPicture::Save:   return wxART_FILE_SAVE;
	case ibProtocolStockPicture::SaveAs: return wxART_FILE_SAVE_AS;
	case ibProtocolStockPicture::Close:  return wxART_CLOSE;
	case ibProtocolStockPicture::Quit:   return wxART_QUIT;
	case ibProtocolStockPicture::Undo:   return wxART_UNDO;
	case ibProtocolStockPicture::Redo:   return wxART_REDO;
	case ibProtocolStockPicture::Cut:    return wxART_CUT;
	case ibProtocolStockPicture::Copy:   return wxART_COPY;
	case ibProtocolStockPicture::Paste:  return wxART_PASTE;
	case ibProtocolStockPicture::Delete: return wxART_DELETE;
	case ibProtocolStockPicture::Find:   return wxART_FIND;
	case ibProtocolStockPicture::Print:  return wxART_PRINT;
	default:                             break;
	}
	return wxArtID();
}

// The wx command of an edit the server asks back. wxID_NONE: none.
int EditCommandOf(long long command)
{
	switch (static_cast<ibProtocolCommand>(command)) {
	case ibProtocolCommand::Cut:       return wxID_CUT;
	case ibProtocolCommand::Copy:      return wxID_COPY;
	case ibProtocolCommand::Paste:     return wxID_PASTE;
	case ibProtocolCommand::Delete:    return wxID_DELETE;
	case ibProtocolCommand::SelectAll: return wxID_SELECTALL;
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

ibProtocolMessageLevel LevelOf(long long level)
{
	if (level == static_cast<long long>(ibProtocolMessageLevel::Warning))
		return ibProtocolMessageLevel::Warning;
	if (level == static_cast<long long>(ibProtocolMessageLevel::Error))
		return ibProtocolMessageLevel::Error;
	return ibProtocolMessageLevel::Information;
}

// A menu of the frame, as a list of what it is made of — the titles, the keys, what each item does — less whether an
// item is enabled: two menus of one shape differ only in what may be pressed, and that is set, not built.
void ShapeOf(const ibProtocolNode& menu, std::string& shape)
{
	for (const ibProtocolNode& item : menu.Children()) {
		shape += '[';
		if (item.GetBool(ibProtocolName::Separator))
			shape += '-';
		shape += std::string(item.GetString(ibProtocolName::Title).utf8_str());
		shape += '|';
		shape += std::string(item.GetString(ibProtocolName::Shortcut).utf8_str());
		shape += '|';
		shape += std::to_string(item.GetInt(ibProtocolName::Command, -1)) + '|' + std::to_string(item.GetInt(ibProtocolName::Schema, -1));
		shape += '|' + std::to_string(item.GetInt(ibProtocolName::StockPicture));
		ShapeOf(item, shape);
		shape += ']';
	}
}

// A MENU REQUEST'S ITEMS, put into a popup menu as the desktop built it: an item with items of its own is a submenu,
// the others are commands — ticked, greyed as they say. The server's ids may be any number and a wx id may not, so each
// command takes the next id of the menu's own, and `ids` keeps the server's in that order.
void AppendMenuItems(wxMenu& menu, const std::vector<ibProtocolNode>& items, std::vector<long long>& ids)
{
	for (const ibProtocolNode& item : items) {
		const wxString caption = item.GetString(ibProtocolName::Caption);
		const std::vector<ibProtocolNode> subItems = item.Children();
		if (!subItems.empty()) {
			wxMenu* const submenu = new wxMenu;
			AppendMenuItems(*submenu, subItems, ids);
			menu.AppendSubMenu(submenu, caption)->Enable(item.GetBool(ibProtocolName::Enabled, true));
			continue;
		}
		const int id = wxID_HIGHEST + 1 + static_cast<int>(ids.size());
		ids.push_back(item.GetInt(ibProtocolName::Id));
		const bool checked = item.GetBool(ibProtocolName::Checked);
		wxMenuItem* const added = checked ? menu.AppendCheckItem(id, caption) : menu.Append(id, caption);
		if (checked)
			added->Check(true);
		added->Enable(item.GetBool(ibProtocolName::Enabled, true));
	}
}

// A FIELD'S VALUE FOR THE SERVER'S QUESTION — what the desktop's SimpleChoice is drawn over: the value the question carries,
// the one chosen kept, and the wait ended when the calculator or the calendar goes (ibProtocolRequestKind::SimpleChoice).
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

} // namespace

//***********************************************************************************
//*                                 mainFrame                                       *
//***********************************************************************************

ibFrontendMainFrame* ibFrontendMainFrame::s_instance = nullptr;

ibFrontendMainFrame::ibFrontendMainFrame(std::unique_ptr<ibCommunicator> communicator, ibProtocolMode mode,
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
	Bind(wxEVT_MENU, &ibFrontendMainFrame::OnMenu, this, kFirstMenuId, kLastMenuId);
	for (const int edit : { wxID_CUT, wxID_COPY, wxID_PASTE, wxID_DELETE, wxID_SELECTALL })
		Bind(wxEVT_MENU, &ibFrontendMainFrame::OnEditCommand, this, edit);

	SetArtProvider(new wxAuiLunaTabArt());

	// A tab chosen is told the server, which draws it; a tab closed is its document's close (ibFormVisualDocument::Close).
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

	wxDELETE(m_docManager);

	// deinitialize the valueForm manager
	m_mgr.UnInit();
}

wxAuiMDIClientWindow* ibFrontendMainFrame::OnCreateClient()
{
	class wxAuiMDIClientWindowImpl : public wxAuiMDIClientWindow {
	public:
		wxAuiMDIClientWindowImpl() : wxAuiMDIClientWindow() {}
		wxAuiMDIClientWindowImpl(wxAuiMDIParentFrame* parent, long style = 0) : wxAuiMDIClientWindow(parent, style) {
#ifdef __WXOSX__
			// The system background — respects dark/light mode (see the desktop's frontend/mainFrame/mainFrame.cpp).
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

	// The messages — every mode's: a tab along the bottom, as the desktop's (CreateBottomPane).
	m_outputWindow = new ibOutputWindow(this);

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

	// The bar along the bottom — the light dusty one of the desktop, its ink the deep dusty blue.
	wxStatusBar* const statusBar = CreateStatusBar();
	statusBar->SetBackgroundColour(wxColour(0xC8, 0xD6, 0xDF));
	statusBar->SetForegroundColour(wxColour(0x3F, 0x5C, 0x77));

	// The workspace — the same powder blue the tabs sit on.
	GetNotebook()->GetAuiManager().GetArtProvider()->SetColour(
		wxAUI_DOCKART_BACKGROUND_COLOUR, wxColour(0xB8, 0xC9, 0xD4));

	SetMinSize(wxSize(400, 380));

	// tell the manager to "commit" all the changes just made
	m_mgr.Update();
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
	SetMenuBar(bar);
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
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	const bool loggedIn = m_communicator->Login(m_user, m_password, m_mode, answer, refusal, error);
	m_password.clear();
	if (!loggedIn) {
		wxMessageBox(!error.IsEmpty() ? error : wxString(_("The start was refused.")), GetTitle(), wxOK | wxICON_ERROR);
		return false;
	}

	// WHAT THE SERVER SAYS UNASKED — the frame changed by itself (a report composed, a question asked by work nobody
	// called): the frame asked for, after whatever the window is doing; told on another thread, made on this one.
	m_communicator->Listen([this]() {
		CallAfter([this]() { Post(ibProtocolMethod::Frame, ibProtocolNode()); });
	});

	SetClientSize(FromDIP(wxSize(800, 600)));
	Center();
	Settle(answer);

	// The window's own parts, once the start has run and nothing of it is pending.
	CreateStartupPage();

	if (!wxAuiMDIParentFrame::Show(true))
		return false;
	SetFocus();
	Raise();
	return true;
}

bool ibFrontendMainFrame::Destroy()
{
	// The client goes with the window: its session is torn down by the server, with whatever it still had open.
	m_closingWindow = true;
	m_communicator->Logout();

	// …and the tabs' documents here with it: the frame has no tab any more, so none of them asks the server
	// (ibFormVisualDocument::Close). The files opened last stay for the next run.
	if (m_docManager != nullptr) {
		m_docManager->CloseDocuments(true);
#if wxUSE_CONFIG
		m_docManager->FileHistorySave(*wxConfig::Get());
#endif // wxUSE_CONFIG
		m_docManager->FileHistoryRemoveMenu(m_menuFile);
	}

	return wxAuiMDIParentFrame::Destroy();
}

bool ibFrontendMainFrame::Call(ibProtocolMethod method, const ibProtocolNode& params, ibProtocolNode& answer)
{
	// Under way until it has settled — its questions asked, its refusal said: a call posted meanwhile waits for it.
	++m_calling;
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	const bool called = m_communicator->Call(method, params, answer, refusal, error);
	if (called)
		Settle(answer);
	else
		Refused(refusal, error);
	--m_calling;

	// Exit done — the client goes, once nothing is under way; a close of the window that asked for it goes on by itself.
	if (m_exitGranted && m_calling == 0 && !m_closingWindow)
		CallAfter([this]() {
			if (!m_closingWindow)
				Close(true);
		});

	if (m_calling == m_asking.size() && !m_posted.empty())
		CallAfter(&ibFrontendMainFrame::CallPosted);
	return called;
}

void ibFrontendMainFrame::Post(ibProtocolMethod method, const ibProtocolNode& params)
{
	m_posted.push_back(ibPostedCall{ method, params });
	CallAfter(&ibFrontendMainFrame::CallPosted);
}

void ibFrontendMainFrame::CloseTab(long long tabId)
{
	// Once — a key held, a second Escape, the button pressed while the first close asks about what is unsaved: the
	// tab is closing until that close has been answered.
	if (!m_tabsClosing.insert(tabId).second)
		return;

	ibProtocolNode params;
	params.SetValue(ibProtocolName::Tab, tabId);
	Post(ibProtocolMethod::Close, params);
}

bool ibFrontendMainFrame::Fetch(const ibProtocolNode& params, ibProtocolNode& answer)
{
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	return m_communicator->Call(ibProtocolMethod::Fetch, params, answer, refusal, error);
}

void ibFrontendMainFrame::ShowSchema(ibProtocolSchema schema, const ibProtocolNode& shown)
{
	if (schema == ibProtocolSchema::ActiveUser) {
		ibDialogActiveUser dialog(this, shown);
		dialog.ShowModal();
	}
	else if (schema == ibProtocolSchema::About) {
		ibDialogAbout dialog(this, shown);
		dialog.ShowModal();
	}
}

bool ibFrontendMainFrame::AllowClose()
{
	// The road the menu's Exit takes — the server closes each tab but the start page, asking about what is unsaved, and
	// says Exit when every one closed (Draw). Marked WHO is asking: a refusal said while the window is closing does not
	// close it a second time. A session already gone has nothing to keep.
	m_closingWindow = true;
	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolCommand::Exit));
	Call(ibProtocolMethod::Command, params, answer);
	m_closingWindow = false;
	return m_exitGranted || !m_communicator->IsLoggedIn();
}

void ibFrontendMainFrame::Draw(const ibProtocolNode& answer)
{
	// THE PICTURES THE FRAME NAMES, given once — kept before anything is drawn: the frame names them by their ids.
	for (const ibProtocolNode& picture : answer.FindChild(ibProtocolName::Pictures).Children())
		ibProtocolPictureKeep(picture.GetString(ibProtocolName::Id), ibProtocolPicture(picture.GetString(ibProtocolName::Picture)));

	const ibProtocolNode& frame = m_communicator->GetFrame();

	// ⭐ WHAT THE ANSWER CHANGED IS DRAWN — its patch names it, and a part it does not name is as it was; a frame sent
	// whole, or none (a refusal's settling), is drawn whole.
	const ibProtocolNode patch = answer.FindChild(ibProtocolName::Patch);
	const bool whole = !patch.IsNode();

	if (whole || patch.Has(ibProtocolName::Title)) {
		const wxString title = frame.GetString(ibProtocolName::Title);
		if (!title.IsEmpty() && title != GetTitle())
			SetTitle(title);
	}
	if (GetStatusBar() != nullptr && (whole || patch.Has(ibProtocolName::Status)))
		SetStatusText(frame.GetString(ibProtocolName::Status));

	if (whole || patch.Has(ibProtocolName::Menu))
		DrawMenu(frame.FindChild(ibProtocolName::Menu));
	if (whole || patch.Has(ibProtocolName::Templates))
		DrawTemplates(frame.FindChild(ibProtocolName::Templates));
	if (whole || patch.Has(ibProtocolName::Tabs))
		DrawTabs(frame.FindChild(ibProtocolName::Tabs));
	DrawView(frame.GetInt(ibProtocolName::ActiveTab), frame.FindChild(ibProtocolName::View), patch);

	// THE EVENTS — what happened since the last answer: read from the answer, never kept in the frame. Exit done: the
	// client goes once its call has settled (Call, AllowClose).
	if (answer.GetBool(ibProtocolName::Exit))
		m_exitGranted = true;
	if (m_outputWindow != nullptr) {
		if (answer.GetBool(ibProtocolName::Clear))
			m_outputWindow->Clear();
		const std::vector<ibProtocolNode> messages = answer.FindChild(ibProtocolName::Messages).Children();
		for (const ibProtocolNode& message : messages)
			m_outputWindow->Output(message.GetString(ibProtocolName::Text), LevelOf(message.GetInt(ibProtocolName::Level)));
	}
}

void ibFrontendMainFrame::DrawMenu(const ibProtocolNode& menu)
{
	// One of the same shape — only what may be pressed is set.
	std::string shape;
	ShapeOf(menu, shape);
	if (shape == m_menuShape) {
		std::function<void(const ibProtocolNode&, int&)> enable = [this, &enable](const ibProtocolNode& items, int& id) {
			for (const ibProtocolNode& item : items.Children()) {
				if (item.GetBool(ibProtocolName::Separator))
					continue;
				if (item.Has(ibProtocolName::Command) || item.Has(ibProtocolName::Schema))
					GetMenuBar()->Enable(id++, item.GetBool(ibProtocolName::Enabled));
				else
					enable(item, id);
			}
		};
		int id = kFirstMenuId;
		enable(menu, id);
		return;
	}

	// Built anew — the window's own bar, never a view's merged one: that one is merged again from it below.
	if (m_pMyMenuBar != nullptr)
		SetChildMenuBar(nullptr);

	// Each command and schema under an id of the menu's range, its key written after its title the way wx reads one
	// (Ctrl is Cmd on a Mac — wx's own reading), its picture the art provider's (the desktop's stockPicture).
	m_menuShape = shape;
	m_menuItems.clear();
	int next = kFirstMenuId;
	std::function<void(wxMenu*, const ibProtocolNode&)> fill = [this, &fill, &next](wxMenu* into, const ibProtocolNode& items) {
		for (const ibProtocolNode& item : items.Children()) {
			if (item.GetBool(ibProtocolName::Separator)) {
				into->AppendSeparator();
				continue;
			}
			const wxString title = item.GetString(ibProtocolName::Title);
			if (item.Has(ibProtocolName::Command) || item.Has(ibProtocolName::Schema)) {
				if (next > kLastMenuId)
					continue;
				const wxString shortcut = item.GetString(ibProtocolName::Shortcut);
				const int id = next++;
				wxMenuItem* const added = into->Append(id, shortcut.IsEmpty() ? title : title + wxT("\t") + shortcut);
				const wxArtID art = ArtOf(item.GetInt(ibProtocolName::StockPicture));
				if (!art.IsEmpty())
					added->SetBitmap(wxArtProvider::GetBitmapBundle(art, wxART_MENU, wxSize(16, 16)));
				added->Enable(item.GetBool(ibProtocolName::Enabled));
				m_menuItems[id] = MenuItemOf(item);
				continue;
			}
			wxMenu* const submenu = new wxMenu();
			fill(submenu, item);
			into->AppendSubMenu(submenu, title);
		}
	};

	// File and Edit first — the window's own, moved over as they are: the files opened last are kept in File's.
	wxMenuBar* const previous = GetMenuBar();
	for (wxMenu* const own : { m_menuFile, m_menuEdit }) {
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
	for (const ibProtocolNode& top : menu.Children()) {
		wxMenu* const submenu = new wxMenu();
		fill(submenu, top);
		bar->Append(submenu, top.GetString(ibProtocolName::Title));
	}
	SetMenuBar(bar);
	delete previous;

	// The active view's own menu, merged into the new bar.
	if (ibFrontendView* const view = m_docManager->GetCurrentView())
		ActivateView(view, true);
}

ibFrontendMainFrame::ibMenuItem ibFrontendMainFrame::MenuItemOf(const ibProtocolNode& item)
{
	ibMenuItem what;
	what.schema = item.Has(ibProtocolName::Schema);
	what.number = static_cast<int>(what.schema ? item.GetInt(ibProtocolName::Schema) : item.GetInt(ibProtocolName::Command));
	return what;
}

void ibFrontendMainFrame::DrawTemplates(const ibProtocolNode& templates)
{
	// THE SERVER'S TEMPLATES, AND NO OTHERS — each opens its file on the server, or has the server make a new one; its
	// picture by its id, kept from the answer's Pictures before the frame is drawn.
	for (ibFrontendDocTemplate* const temp : m_docManager->GetTemplatesVector())
		delete temp;
	for (const ibProtocolNode& item : templates.Children()) {
		const wxString title = item.GetString(ibProtocolName::Title);
		const long flags = ibTEMPLATE_VISIBLE | (item.GetBool(ibProtocolName::OnlyOpen) ? ibTEMPLATE_ONLY_OPEN : 0);
		ibFrontendDocTemplate* const temp = new ibFrontendDocTemplate(m_docManager, title, item.GetString(ibProtocolName::Mask),
			wxEmptyString, item.GetString(ibProtocolName::Extension), title, title, CLASSINFO(ibFrontendFileDocument), nullptr, flags);
		const wxBitmap picture = ibProtocolPicture(item.GetString(ibProtocolName::Picture));
		if (picture.IsOk()) {
			wxIcon icon;
			icon.CopyFromBitmap(picture);
			temp->SetClassIcon(icon);
		}
	}
}

void ibFrontendMainFrame::DrawTabs(const ibProtocolNode& tabs)
{
	wxAuiMDIClientWindow* const client = GetClientWindow();
	if (client == nullptr)
		return;

	// The drawing changes pages, and wx says so as if the person had: those are not theirs to answer.
	m_drawing = true;
	client->Freeze();

	// EACH TAB A FORM THE SERVER RAISED — its document made here, with no template (ibFormVisualDocument): its view in a tab
	// of the window (CreateChildFrame).
	std::set<long long> drawn;
	for (const ibProtocolNode& tab : tabs.Children()) {
		const long long id = tab.GetId();
		drawn.insert(id);
		const wxString title = tab.GetString(ibProtocolName::Title);

		ibFormVisualDocument* const document = FindDocument(id);
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
		ibFormVisualDocument* const opened = new ibFormVisualDocument(id);
		m_documents[id] = opened;
		opened->SetTitle(title);
		const wxBitmap icon = ibProtocolPicture(tab.GetString(ibProtocolName::Icon));
		if (icon.IsOk()) {
			wxIcon picture;
			picture.CopyFromBitmap(icon);
			opened->SetIcon(picture);
		}
		// Under the form that owns it, when the server says it is owned — it goes with that one; the doc manager's
		// otherwise.
		if (ibFormVisualDocument* const owner = FindDocument(tab.GetInt(ibProtocolName::Parent)))
			opened->SetDocParent(owner);
		else
			m_docManager->AddDocument(opened);

		// Not made — its view was not.
		if (!opened->OnCreate(wxEmptyString, ibDOC_NEW)) {
			m_documents.erase(id);
			opened->DeleteAllViews();
			continue;
		}

		// The start page — first, and never closed: a locked tab, which wx keeps ahead of the others.
		if (tab.GetBool(ibProtocolName::Locked)) {
			const int page = client->GetPageIndex(opened->GetDocumentWindow());
			if (page != wxNOT_FOUND)
				client->SetPageKind(static_cast<size_t>(page), wxAuiTabKind::Locked);
		}
	}

	// The tabs the frame no longer has — closed by the server: their documents go here (ibFormVisualDocument::Close), each
	// once. Looked for afresh before each: a form closed takes the forms it owns with it.
	std::vector<long long> gone;
	for (const auto& kept : m_documents) {
		if (drawn.count(kept.first) == 0)
			gone.push_back(kept.first);
	}
	for (const long long id : gone) {
		if (ibFormVisualDocument* const document = FindDocument(id)) {
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
	ibFormVisualDocument* const chosen = FindDocument(active);
	ibFormVisualEditView* const shown = chosen != nullptr ? chosen->GetFirstView() : nullptr;
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

	// …and its view the doc manager's current one: a tab closed picks the page after it with the notebook's events off
	// (ibAuiDocChildFrame::Destroy), so that page's view is never told it became active.
	if (m_docManager->GetCurrentView() != shown)
		shown->Activate(true);

	// The active tab's view — the one the frame carries, drawn by what the patch changed in it: none in a frame sent
	// whole; a patch that leaves it as it was changes nothing (a node with no entries) — and a view not built yet is built
	// all the same: a tab just opened is patched from the view the client kept, another tab's, and two alike leave no View
	// in the patch. The other tabs keep what was drawn in them last.
	shown->Draw(view, !patch.IsNode() || patch.Has(ibProtocolName::View) ? patch.FindChild(ibProtocolName::View) : ibProtocolNode());

	m_drawing = false;
}

void ibFrontendMainFrame::Settle(const ibProtocolNode& answer)
{
	// The client's part of a call, beside its exchange (the communicator's line): the window drawn from the answer.
	const wxLongLong began = wxGetUTCTimeMillis();
	Draw(answer);
	ibClientJournalInfo(wxT("call"), wxT("  drawn in %lld ms"), (wxGetUTCTimeMillis() - began).GetValue());
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
		const wxString id = request.GetString(ibProtocolName::Id);
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

void ibFrontendMainFrame::TakeChosen(const ibProtocolNode& request)
{
	const ibProtocolNode chosen = request.FindChild(ibProtocolName::Chosen);
	if (m_choosing.owner == nullptr || !chosen.IsNode() || chosen.GetInt(ibProtocolName::Choice) != m_choosing.choice)
		return;
	ibControlFrame* const owner = m_choosing.owner;
	m_choosing = ibChoosing();
	ibDataNode node;
	ibReadProtocolNode(chosen.FindChild(ibProtocolName::Value), node);
	ibValue value = ibValue::FromNode(node);
	owner->ChoiceProcessing(value);
}

bool ibFrontendMainFrame::ChooseValue(ibControlFrame* owner, const ibValue& current)
{
	const ibProtocolNode request = GetRequest();
	if (owner == nullptr || m_asking.empty()
		|| static_cast<ibProtocolRequestKind>(request.GetInt(ibProtocolName::Kind)) != ibProtocolRequestKind::ListSettings)
		return false;

	m_choosing = ibChoosing{ owner, ++m_choice, m_asking.size() };

	// The type the cell stands on, and the value in it when there is one — the server's own node, given back.
	ibProtocolNode act;
	act.SetValue(ibProtocolName::Act, static_cast<long long>(ibProtocolListSettingsAct::Choose))
		.SetValue(ibProtocolName::Type, wxString() << current.GetClassType())
		.SetValue(ibProtocolName::Choice, m_choice);
	if (!current.IsEmpty()) {
		ibDataNode node;
		current.Serialize(node);
		ibWriteProtocolNode(node, act.Child(ibProtocolName::Value));
	}
	if (!Respond(act))
		return false;

	// Its quick choice asked on the way — put up and answered — or its choice form opened: either way the window's
	// question comes back, with the value when it was chosen at once.
	for (;;) {
		const ibProtocolNode asked = GetRequest();
		if (!asked.IsNode())
			return true;
		if (static_cast<ibProtocolRequestKind>(asked.GetInt(ibProtocolName::Kind)) == ibProtocolRequestKind::ListSettings) {
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
	return m_communicator->GetFrame().FindChild(ibProtocolName::Request);
}

bool ibFrontendMainFrame::Respond(const ibProtocolNode& response)
{
	const ibProtocolNode request = GetRequest();
	if (!request.IsNode())
		return false;

	const wxString id = request.GetString(ibProtocolName::Id);
	ibProtocolNode params, next;
	params.SetValue(ibProtocolName::Id, id).SetValue(ibProtocolName::Response, response);
	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	if (!m_communicator->Call(ibProtocolMethod::Respond, params, next, refusal, error)) {
		Refused(refusal, error);
		return false;
	}
	Draw(next);

	// …a window up answering along the way is asked again: the question pending now is its own.
	if (!m_asking.empty() && m_asking.back() == id)
		m_asking.back() = GetRequest().GetString(ibProtocolName::Id);
	return true;
}

void ibFrontendMainFrame::ShowRequest(const ibProtocolNode& request, ibProtocolNode& response)
{
	// Over the window of the form it is asked from — the desktop's: a table's settings over the table's own window — the
	// active tab's: the main window for a tab, the dialog for a form opened over a modal one (CreateChildFrame).
	wxWindow* parent = IsShown() ? static_cast<wxWindow*>(this) : nullptr;
	if (ibFormVisualDocument* const document = FindDocument(GetActiveTab())) {
		if (wxWindow* const window = document->GetDocumentWindow())
			parent = wxGetTopLevelParent(window);
	}

	switch (static_cast<ibProtocolRequestKind>(request.GetInt(ibProtocolName::Kind))) {
	case ibProtocolRequestKind::Message: {
		// wx's own flags and codes travel — this client is wx.
		const int button = wxMessageBox(request.GetString(ibProtocolName::Text), request.GetString(ibProtocolName::Caption),
			static_cast<int>(request.GetInt(ibProtocolName::Style, wxOK)), parent);
		response.SetValue(ibProtocolName::Button, button);
		break;
	}
	case ibProtocolRequestKind::Choice: {
		const std::vector<ibProtocolNode> items = request.Children();
		wxArrayString captions;
		int selected = 0;
		wxArrayInt ticked;
		for (std::size_t i = 0; i < items.size(); ++i) {
			captions.Add(items[i].GetString(ibProtocolName::Caption));
			if (items[i].GetBool(ibProtocolName::Selected)) {
				selected = static_cast<int>(i);
				ticked.Add(static_cast<int>(i));
			}
		}
		// SEVERAL — the desktop's wxMultiChoiceDialog, those Selected ticked; cancelled — answered with no Ids.
		if (request.GetBool(ibProtocolName::Multiple)) {
			wxMultiChoiceDialog dialog(parent, request.GetString(ibProtocolName::Caption), request.GetString(ibProtocolName::Caption),
				captions);
			dialog.SetSelections(ticked);
			if (dialog.ShowModal() == wxID_OK) {
				for (const int index : dialog.GetSelections()) {
					if (index >= 0 && index < static_cast<int>(items.size()))
						response.AddItem(ibProtocolName::Ids, items[index].GetInt(ibProtocolName::Id));
				}
			}
			break;
		}
		wxSingleChoiceDialog dialog(parent, request.GetString(ibProtocolName::Caption), request.GetString(ibProtocolName::Caption), captions);
		if (!captions.IsEmpty())
			dialog.SetSelection(selected);
		// Cancelled — answered with no Id: the server takes that for no choice.
		if (dialog.ShowModal() == wxID_OK && dialog.GetSelection() >= 0
			&& dialog.GetSelection() < static_cast<int>(items.size()))
			response.SetValue(ibProtocolName::Id, items[dialog.GetSelection()].GetInt(ibProtocolName::Id));
		break;
	}
	case ibProtocolRequestKind::Help:
		wxMessageBox(request.GetString(ibProtocolName::Text), request.GetString(ibProtocolName::Title), wxOK | wxICON_INFORMATION, parent);
		break;
	case ibProtocolRequestKind::Edit:
		// Answered with nothing: a field changed sends its Change as it does when typed into.
		EditFocused(EditCommandOf(request.GetInt(ibProtocolName::Command)));
		break;
	case ibProtocolRequestKind::File: {
		// What may be chosen, as the desktop's doc manager filtered its File → Open: "Title (Mask)|Mask" each.
		wxString wildcard;
		for (const ibProtocolNode& item : request.Children()) {
			const wxString mask = item.GetString(ibProtocolName::Mask);
			if (!wildcard.IsEmpty())
				wildcard += wxT("|");
			wildcard += item.GetString(ibProtocolName::Title) + wxT(" (") + mask + wxT(")|") + mask;
		}
		wxFileDialog dialog(parent, request.GetString(ibProtocolName::Caption), wxEmptyString, wxEmptyString,
			!wildcard.IsEmpty() ? wildcard : wxString(wxFileSelectorDefaultWildcardStr), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		// Cancelled, or not uploaded — answered with no File: the server takes that for nothing chosen.
		wxString file;
		if (dialog.ShowModal() == wxID_OK && UploadFile(dialog.GetPath(), file))
			response.SetValue(ibProtocolName::File, file);
		break;
	}
	case ibProtocolRequestKind::Generation: {
		// Cancelled — answered with no Id.
		ibDialogGeneration dialog(request);
		ibMetaID id = 0;
		if (dialog.ShowModal(id))
			response.SetValue(ibProtocolName::Id, static_cast<long long>(id));
		break;
	}
	case ibProtocolRequestKind::ViewMode: {
		// The server's mode numbers are the data view's own (ibProtocolViewMode); cancelled — answered with no Mode.
		wxTableViewModeDialog dialog(parent, wxID_ANY, static_cast<ibDataViewViewMode>(request.GetInt(ibProtocolName::Mode)),
			ibProtocolPicture(request.GetString(ibProtocolName::Picture)));
		if (dialog.ShowModal() == wxID_OK)
			response.SetValue(ibProtocolName::Mode, static_cast<long long>(dialog.GetViewMode()));
		break;
	}
	case ibProtocolRequestKind::Menu: {
		// THE DESKTOP'S POPUP MENU, where the mouse is. Dismissed — answered with no Id.
		wxMenu menu;
		std::vector<long long> ids;
		AppendMenuItems(menu, request.Children(), ids);
		const int picked = GetPopupMenuSelectionFromUser(menu) - (wxID_HIGHEST + 1);
		if (picked >= 0 && picked < static_cast<int>(ids.size()))
			response.SetValue(ibProtocolName::Id, ids[picked]);
		break;
	}
	case ibProtocolRequestKind::SimpleChoice: {
		// THE DESKTOP'S ibTypeControlFactory::SimpleChoice — a number on its calculator, a date on its calendar, dropped
		// under the field pressed; the value chosen is the answer, a window closed with none — none.
		ibDataNode stored;
		ibReadProtocolNode(request.FindChild(ibProtocolName::Value), stored);
		ibSimpleChoiceHolder holder(ibValue::FromNode(stored));
		if (ibTypeControlFactory::SimpleChoice(&holder, holder.GetValue().GetClassType(), FocusedEditor(this)))
			holder.Wait();
		if (holder.HasChosen()) {
			ibDataNode chosen;
			holder.GetChosen().Serialize(chosen);
			ibWriteProtocolNode(chosen, response.Child(ibProtocolName::Value));
		}
		break;
	}
	case ibProtocolRequestKind::ListSettings:
		// THE DESKTOP'S ibDialogListSettings::ShowUserSettings over a table's model — the model is the server's: the
		// setting and its fields come in the question, the edited setting is the answer; cancelled — answered with none.
		ibDialogListSettings::ShowUserSettings(parent, request, response);
		break;
	case ibProtocolRequestKind::FormEditor: {
		// THE DESKTOP'S ibValueForm::ChangeForm — its editor over the form of the active tab, as the form's controls hold
		// their properties (the frame's, read by ReadData). Its acts are answered on the way (UpdateForm, the setting's
		// doors); closed — answered with no Act.
		ibFormVisualDocument* const document = FindDocument(GetActiveTab());
		ibFormVisualEditView* const view = document != nullptr ? document->GetFirstView() : nullptr;
		ibValueForm* const form = view != nullptr && view->GetVisualHost() != nullptr ? view->GetVisualHost()->GetValueForm() : nullptr;
		if (form == nullptr)
			break;
		// The window's own picture, kept by its number — the one the desktop's editor asks for.
		ibProtocolPictureKeep(g_picChangeFormCLSID, ibProtocolPicture(request.GetString(ibProtocolName::Picture)));
		ibDialogFormEditor dlg(form);
		dlg.ShowModal();
		break;
	}
	case ibProtocolRequestKind::SavedSettings: {
		// THE SHELF'S WINDOW IS OPEN THROUGH EVERY ACT BUT THE LAST — each one answered on the way (Respond, inside the
		// shelf's calls: settingsComposer.h), the shelf asked again in its place. Closed — answered with no Act.
		ibDataComposer composer;
		ibDialogSavedSettings::Show(parent, composer,
			request.GetInt(ibProtocolName::Mode) == 0 ? ibDialogSavedSettings::Mode::Save : ibDialogSavedSettings::Mode::Restore,
			ibSettingsCategory::Custom, ibGuid(request.GetString(ibProtocolName::Key)), nullptr);
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
	std::vector<char> part(kUploadPartSize);
	file.clear();
	for (bool first = true;; first = false) {
		const ssize_t read = source.Read(part.data(), part.size());
		if (read == wxInvalidOffset)
			return false;
		if (read == 0 && !first)
			return true;

		ibProtocolNode params, answer;
		if (first)
			params.SetValue(ibProtocolName::Name, wxFileName(path).GetFullName());
		else
			params.SetValue(ibProtocolName::File, file);
		params.SetValue(ibProtocolName::Data, wxBase64Encode(part.data(), static_cast<std::size_t>(read)));

		ibProtocolRefusal refusal = ibProtocolRefusal::None;
		wxString error;
		if (!m_communicator->Call(ibProtocolMethod::Upload, params, answer, refusal, error)) {
			Refused(refusal, error);
			return false;
		}
		file = answer.GetString(ibProtocolName::File);
		if (static_cast<std::size_t>(read) < part.size())
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
		params.SetValue(ibProtocolName::File, file).SetValue(ibProtocolName::Part, part);

		ibProtocolRefusal refusal = ibProtocolRefusal::None;
		wxString error;
		if (!m_communicator->Call(ibProtocolMethod::Download, params, answer, refusal, error)) {
			Refused(refusal, error);
			return false;
		}
		parts = answer.GetInt(ibProtocolName::Parts);
		const wxMemoryBuffer data = wxBase64Decode(answer.GetString(ibProtocolName::Data));
		if (data.GetDataLen() != 0 && target.Write(data.GetData(), data.GetDataLen()) != data.GetDataLen())
			return false;
	}
	return target.Close();
}

void ibFrontendMainFrame::Refused(ibProtocolRefusal refusal, const wxString& error)
{
	wxMessageBox(!error.IsEmpty() ? error : wxString(_("The server refused it.")), GetTitle(),
		wxOK | (refusal == ibProtocolRefusal::NoSession ? wxICON_ERROR : wxICON_WARNING), IsShown() ? this : nullptr);

	// The session is gone — the window has nothing left to show.
	if (refusal == ibProtocolRefusal::NoSession && !m_closingWindow)
		Close(true);
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
	if (posted.method == ibProtocolMethod::Close)
		m_tabsClosing.erase(posted.params.GetInt(ibProtocolName::Tab));
}

ibFormVisualDocument* ibFrontendMainFrame::DocumentAt(int page) const
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

void ibFrontendMainFrame::RemoveDocument(const ibFormVisualDocument* document)
{
	m_documents.erase(document->GetTabId());
}

ibProtocolNode ibFrontendMainFrame::FindTab(long long tabId) const
{
	for (const ibProtocolNode& tab : m_communicator->GetFrame().FindChild(ibProtocolName::Tabs).Children()) {
		if (tab.GetId() == tabId)
			return tab;
	}
	return ibProtocolNode();
}

long long ibFrontendMainFrame::GetActiveTab() const
{
	return m_communicator->GetFrame().GetInt(ibProtocolName::ActiveTab);
}

ibFormVisualDocument* ibFrontendMainFrame::FindDocument(long long tabId) const
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
			m_docToolbar->Clear();
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

	if (view_count == 0) m_docToolbar->Clear();

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

	if (!force && !AllowClose()) {
		event.Veto();
		return;
	}

	// Deliberately NOT event.Skip(): the base would run a second closing policy over the tabs (CloseAll) — the tabs
	// are the server's, and AllowClose asked it.
	Destroy();
}

void ibFrontendMainFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
	Close();
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
	ibProtocolNode params, answer;
	if (what.schema) {
		params.SetValue(ibProtocolName::Schema, what.number);
		if (Call(ibProtocolMethod::Schema, params, answer))
			ShowSchema(static_cast<ibProtocolSchema>(what.number), answer);
	}
	else {
		params.SetValue(ibProtocolName::Command, what.number);
		Call(ibProtocolMethod::Command, params, answer);
	}
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

	const ibFormVisualDocument* const document = DocumentAt(event.GetSelection());
	if (document == nullptr || document->GetTabId() == GetActiveTab())
		return;

	// After the event: the answer redraws the tabs, which is no work for the middle of a notebook's own event.
	ibProtocolNode params;
	params.SetValue(ibProtocolName::Tab, document->GetTabId());
	Post(ibProtocolMethod::Activate, params);
}
