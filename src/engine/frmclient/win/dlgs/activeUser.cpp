#include "activeUser.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"
#include "frmclient/win/picture.h"
#include "frmclient/visualView/ctrl/frame.h"   // wxDefaultStypeFGColour — the desktop's dialog took it from there
#include "frmclient/backend/appData.h"        // DesignerMode — whose the context menus are

#include <wx/menu.h>

namespace {

// One of the schema's commands, with its one argument. False — refused, and the person told why.
bool ActiveUserCommand(ibProtocol::ibSchema::ibCommand command, const char* name, const wxString& value)
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::ActiveUser));
	params.SetValue(ibProtocol::ibSchema::Command, static_cast<long long>(command));
	params.Child(ibProtocol::ibSchema::Args).SetValue(name, value);
	return frame->Call(ibProtocol::ibMethod::Schema, params, answer);
}

}

void ibDialogActiveUser::RefreshActiveUserTable(const ibProtocolNode& shown)
{
	const ibProtocolNode arr = shown.FindChild(ibProtocol::ibSchema::ibActiveUser::Sessions);

	if (m_sessionArray != arr.Write()) {

		wxString current_session;
		const long selected = m_activeTable->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		// Session guid column moved to index 5 to make room for the new
		// "Type" column (Server vs Client — matters for web where a
		// single process owns one Server row + N Client rows).
		if (selected != -1) current_session = m_activeTable->GetItemText(selected, 5);

		m_activeTable->ClearAll();

		wxImageList* imageList = new wxImageList(16, 16);

		m_activeTable->AppendColumn(_("User"), wxLIST_FORMAT_LEFT, 190);
		m_activeTable->AppendColumn(_("Application"), wxLIST_FORMAT_LEFT, 120);
		m_activeTable->AppendColumn(_("Type"), wxLIST_FORMAT_LEFT, 70);
		m_activeTable->AppendColumn(_("Started"), wxLIST_FORMAT_LEFT, 120);
		m_activeTable->AppendColumn(_("Computer"), wxLIST_FORMAT_LEFT, 145);
		m_activeTable->AppendColumn(_("Session"), wxLIST_FORMAT_LEFT, 0); //hide
		m_activeTable->AppendColumn(_("State"), wxLIST_FORMAT_LEFT, 160);

		m_activeTable->AssignImageList(imageList, wxIMAGE_LIST_SMALL);

		const int imageUser =
			imageList->Add(ibProtocolPicture(shown, ibProtocol::ibSchema::ibActiveUser::Picture));

		for (const ibProtocolNode& row : arr.Children()) {

			const long index = m_activeTable->InsertItem(m_activeTable->GetItemCount(), row.GetString(ibProtocol::ibSchema::ibActiveUser::User));

			m_activeTable->SetItem(index, 0, row.GetString(ibProtocol::ibSchema::ibActiveUser::User), imageUser);
			m_activeTable->SetItem(index, 1, row.GetString(ibProtocol::ibSchema::ibActiveUser::Application));
			m_activeTable->SetItem(index, 2, row.GetString(ibProtocol::ibSchema::ibActiveUser::Type));
			m_activeTable->SetItem(index, 3, row.GetString(ibProtocol::ibSchema::ibActiveUser::Started));
			m_activeTable->SetItem(index, 4, row.GetString(ibProtocol::ibSchema::ibActiveUser::Computer));
			m_activeTable->SetItem(index, 5, row.GetString(ibProtocol::ibSchema::ibActiveUser::Session));
			m_activeTable->SetItem(index, 6, row.GetString(ibProtocol::ibSchema::ibActiveUser::Activity));

			if (current_session == row.GetString(ibProtocol::ibSchema::ibActiveUser::Session))
				m_activeTable->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);

		}

	}

	m_sessionArray = arr.Write();
}

void ibDialogActiveUser::RefreshLocksTable(const ibProtocolNode& shown)
{
	if (m_locksTable == nullptr) return;

	const std::vector<ibProtocolNode> rows = shown.FindChild(ibProtocol::ibSchema::ibActiveUser::Locks).Children();

	// Only rebuild when the row count actually changed — RefreshActiveUserTable
	// gates on a content hash; sys_lock has no equivalent hash yet, so use the
	// cheap size check. Acceptable: lock churn is rare relative to the 1s tick,
	// and a stale row count quickly resolves on the next acquire/release.
	if (rows.size() == m_lastLockRowCount && m_locksTable->GetItemCount() > 0)
		return;
	m_lastLockRowCount = rows.size();

	long selectedRow = m_locksTable->GetNextItem(-1, wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	wxString selectedGuid;
	if (selectedRow != -1)
		selectedGuid = m_locksTable->GetItemText(selectedRow, 5);

	m_locksTable->ClearAll();
	m_locksTable->AppendColumn(_("Namespace"), wxLIST_FORMAT_LEFT, 180);
	m_locksTable->AppendColumn(_("Key"),       wxLIST_FORMAT_LEFT, 200);
	m_locksTable->AppendColumn(_("Mode"),      wxLIST_FORMAT_LEFT, 70);
	m_locksTable->AppendColumn(_("User"),      wxLIST_FORMAT_LEFT, 100);
	m_locksTable->AppendColumn(_("Acquired"),  wxLIST_FORMAT_LEFT, 140);
	m_locksTable->AppendColumn(_("LockGuid"),  wxLIST_FORMAT_LEFT, 0); // hidden

	for (const auto& r : rows) {
		const long index = m_locksTable->InsertItem(
			m_locksTable->GetItemCount(), r.GetString(ibProtocol::ibSchema::ibActiveUser::Namespace));
		m_locksTable->SetItem(index, 0, r.GetString(ibProtocol::ibSchema::ibActiveUser::Namespace));
		m_locksTable->SetItem(index, 1, r.GetString(ibProtocol::ibSchema::ibActiveUser::Key));
		m_locksTable->SetItem(index, 2, r.GetString(ibProtocol::ibSchema::ibActiveUser::Mode));
		m_locksTable->SetItem(index, 3, r.GetString(ibProtocol::ibSchema::ibActiveUser::User));
		m_locksTable->SetItem(index, 4, r.GetString(ibProtocol::ibSchema::ibActiveUser::Acquired));
		m_locksTable->SetItem(index, 5, r.GetString(ibProtocol::ibSchema::ibActiveUser::Lock));

		if (!selectedGuid.IsEmpty() && r.GetString(ibProtocol::ibSchema::ibActiveUser::Lock) == selectedGuid)
			m_locksTable->SetItemState(index,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}
}

ibDialogActiveUser::ibDialogActiveUser(wxWindow* parent, const ibProtocolNode& shown, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style), m_activeTableScanner(new wxTimer)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue dialog

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	m_notebook = new wxNotebook(this, wxID_ANY);
	mainSizer->Add(m_notebook, 1, wxALL | wxEXPAND, FromDIP(5));

	m_activeTable = new wxListCtrl(m_notebook, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_activeTable->SetBackgroundColour(wxColour(250, 250, 250));  // #fafafa list
	m_notebook->AddPage(m_activeTable, _("Users"), true);

	m_locksTable = new wxListCtrl(m_notebook, wxID_ANY, wxDefaultPosition,
		wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_locksTable->SetBackgroundColour(wxColour(250, 250, 250));   // #fafafa list
	m_notebook->AddPage(m_locksTable, _("Locks"), false);

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibProtocolPicture(shown, ibProtocol::ibSchema::ibActiveUser::Icon));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();

	m_activeTable->SetForegroundColour(wxDefaultStypeFGColour);
	m_locksTable->SetForegroundColour(wxDefaultStypeFGColour);

	RefreshActiveUserTable(shown);
	RefreshLocksTable(shown);

	m_activeTableScanner->Bind(wxEVT_TIMER, &ibDialogActiveUser::OnIdleHandler, this);
	m_activeTableScanner->Start(1000);

	// Right-click context menus — bound after lists exist.
	m_activeTable->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,
		&ibDialogActiveUser::OnContextMenu, this);
	m_locksTable->Bind(wxEVT_LIST_ITEM_RIGHT_CLICK,
		&ibDialogActiveUser::OnLocksContextMenu, this);
}

ibDialogActiveUser::~ibDialogActiveUser()
{
	if (m_activeTableScanner->IsRunning())  m_activeTableScanner->Stop();
	m_activeTableScanner->Unbind(wxEVT_TIMER, &ibDialogActiveUser::OnIdleHandler, this);
	m_activeTable->Unbind(wxEVT_LIST_ITEM_RIGHT_CLICK,
		&ibDialogActiveUser::OnContextMenu, this);
	if (m_locksTable != nullptr) {
		m_locksTable->Unbind(wxEVT_LIST_ITEM_RIGHT_CLICK,
			&ibDialogActiveUser::OnLocksContextMenu, this);
	}
}

void ibDialogActiveUser::OnIdleHandler(wxTimerEvent& WXUNUSED(event))
{
	// Asked of the server again, as the desktop's read its registry again.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode params, shown;
	params.SetValue(ibProtocol::ibSchema::Schema, static_cast<long long>(ibProtocol::ibSchema::ibKind::ActiveUser));
	if (frame == nullptr || !frame->Call(ibProtocol::ibMethod::Schema, params, shown))
		return;

	RefreshActiveUserTable(shown);
	RefreshLocksTable(shown);
}

namespace {
enum {
	ID_ActiveUser_Kick           = wxID_HIGHEST + 1001,
	ID_ActiveUser_Reload         = wxID_HIGHEST + 1002,
	ID_ActiveUser_ForceReleaseLock = wxID_HIGHEST + 1003,
};
} // namespace

void ibDialogActiveUser::OnContextMenu(wxListEvent& event)
{
	const long row = event.GetIndex();
	if (row < 0) return;
	// Session guid lives in the hidden last column (index 5 after the
	// "Type" column was inserted).
	const wxString guid = m_activeTable->GetItemText(row, 5);
	if (guid.IsEmpty()) return;

	// Kick / Reload are designer-only admin actions. Runtime
	// users see the dialog read-only — runtime operators don't get to
	// boot peers off. (The server refuses them to any other session too.)
	if (!appData->DesignerMode()) return;

	wxMenu menu;
	menu.Append(ID_ActiveUser_Kick,   _("Kick session"));
	menu.Append(ID_ActiveUser_Reload, _("Reload clients"));
	menu.Bind(wxEVT_MENU, &ibDialogActiveUser::OnKickSelected,   this,
		ID_ActiveUser_Kick);
	menu.Bind(wxEVT_MENU, &ibDialogActiveUser::OnReloadSelected, this,
		ID_ActiveUser_Reload);
	PopupMenu(&menu);
}

void ibDialogActiveUser::OnKickSelected(wxCommandEvent&)
{
	const long row = m_activeTable->GetNextItem(-1, wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (row < 0) return;
	const wxString guid = m_activeTable->GetItemText(row, 5);
	if (guid.IsEmpty()) return;

	// The self-kick guard is the server's: it knows which session asks.

	const wxString user = m_activeTable->GetItemText(row, 0);
	const wxString userLabel = user.IsEmpty()
		? wxString(_("(user not specified)"))
		: user;
	const int answer = wxMessageBox(
		ibString::Format(_("Kick session of '%s'?"), userLabel),
		wxTheApp->GetAppDisplayName(), wxYES_NO | wxICON_QUESTION, this);
	if (answer != wxYES) return;

	ActiveUserCommand(ibProtocol::ibSchema::ibCommand::Kick, ibProtocol::ibSchema::ibActiveUser::Session, guid);
}

void ibDialogActiveUser::OnReloadSelected(wxCommandEvent&)
{
	const long row = m_activeTable->GetNextItem(-1, wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (row < 0) return;
	const wxString guid = m_activeTable->GetItemText(row, 5);
	if (guid.IsEmpty()) return;

	// Same self-guard as kick — the server's.

	ActiveUserCommand(ibProtocol::ibSchema::ibCommand::Reload, ibProtocol::ibSchema::ibActiveUser::Session, guid);
}

void ibDialogActiveUser::OnLocksContextMenu(wxListEvent& event)
{
	const long row = event.GetIndex();
	if (row < 0) return;

	// Same admin-only gate as kick/reload — runtime users get read-only view.
	if (!appData->DesignerMode()) return;

	const wxString lockGuid = m_locksTable->GetItemText(row, 5);
	if (lockGuid.IsEmpty()) return;

	wxMenu menu;
	menu.Append(ID_ActiveUser_ForceReleaseLock, _("Force release"));
	menu.Bind(wxEVT_MENU, &ibDialogActiveUser::OnForceReleaseSelected, this,
		ID_ActiveUser_ForceReleaseLock);
	PopupMenu(&menu);
}

void ibDialogActiveUser::OnForceReleaseSelected(wxCommandEvent&)
{
	const long row = m_locksTable->GetNextItem(-1, wxLIST_NEXT_ALL,
		wxLIST_STATE_SELECTED);
	if (row < 0) return;

	const wxString lockGuid = m_locksTable->GetItemText(row, 5);
	if (lockGuid.IsEmpty()) return;

	const wxString ns   = m_locksTable->GetItemText(row, 0);
	const wxString user = m_locksTable->GetItemText(row, 3);

	const int answer = wxMessageBox(
		ibString::Format(_("Force-release lock on '%s' held by '%s'?"),
			ns, user.IsEmpty() ? wxString(_("(unknown)")) : user),
		wxTheApp->GetAppDisplayName(), wxYES_NO | wxICON_QUESTION, this);
	if (answer != wxYES) return;

	if (!ActiveUserCommand(ibProtocol::ibSchema::ibCommand::Release, ibProtocol::ibSchema::ibActiveUser::Lock, lockGuid))
		return;

	// Force immediate refresh — the 1s timer would catch this anyway,
	// but the operator just clicked, give instant feedback.
	m_lastLockRowCount = static_cast<size_t>(-1);
	wxTimerEvent tick(*m_activeTableScanner);
	OnIdleHandler(tick);
}
