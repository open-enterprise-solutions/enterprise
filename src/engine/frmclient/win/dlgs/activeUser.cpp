#include "activeUser.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"
#include "frmclient/win/picture.h"
#include "frmclient/visualView/ctrl/frame.h"   // wxDefaultStypeFGColour — the desktop's dialog took it from there

void ibDialogActiveUser::RefreshActiveUserTable(const ibProtocolNode& shown)
{
	const ibProtocolNode arr = shown.FindChild(ibProtocolName::Sessions);

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

		m_activeTable->AssignImageList(imageList, wxIMAGE_LIST_SMALL);

		const int imageUser =
			imageList->Add(ibProtocolPicture(shown.GetString(ibProtocolName::Picture)));

		for (const ibProtocolNode& row : arr.Children()) {

			const long index = m_activeTable->InsertItem(m_activeTable->GetItemCount(), row.GetString(ibProtocolName::User));

			m_activeTable->SetItem(index, 0, row.GetString(ibProtocolName::User), imageUser);
			m_activeTable->SetItem(index, 1, row.GetString(ibProtocolName::Application));
			m_activeTable->SetItem(index, 2, row.GetString(ibProtocolName::Type));
			m_activeTable->SetItem(index, 3, row.GetString(ibProtocolName::Started));
			m_activeTable->SetItem(index, 4, row.GetString(ibProtocolName::Computer));
			m_activeTable->SetItem(index, 5, row.GetString(ibProtocolName::Session));

			if (current_session == row.GetString(ibProtocolName::Session))
				m_activeTable->SetItemState(index, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);

		}

	}

	m_sessionArray = arr.Write();
}

void ibDialogActiveUser::RefreshLocksTable(const ibProtocolNode& shown)
{
	if (m_locksTable == nullptr) return;

	const std::vector<ibProtocolNode> rows = shown.FindChild(ibProtocolName::Locks).Children();

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
			m_locksTable->GetItemCount(), r.GetString(ibProtocolName::Namespace));
		m_locksTable->SetItem(index, 0, r.GetString(ibProtocolName::Namespace));
		m_locksTable->SetItem(index, 1, r.GetString(ibProtocolName::Key));
		m_locksTable->SetItem(index, 2, r.GetString(ibProtocolName::Mode));
		m_locksTable->SetItem(index, 3, r.GetString(ibProtocolName::User));
		m_locksTable->SetItem(index, 4, r.GetString(ibProtocolName::Acquired));
		m_locksTable->SetItem(index, 5, r.GetString(ibProtocolName::Lock));

		if (!selectedGuid.IsEmpty() && r.GetString(ibProtocolName::Lock) == selectedGuid)
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
	dlg_icon.CopyFromBitmap(ibProtocolPicture(shown.GetString(ibProtocolName::Icon)));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();

	m_activeTable->SetForegroundColour(wxDefaultStypeFGColour);
	m_locksTable->SetForegroundColour(wxDefaultStypeFGColour);

	RefreshActiveUserTable(shown);
	RefreshLocksTable(shown);

	m_activeTableScanner->Bind(wxEVT_TIMER, &ibDialogActiveUser::OnIdleHandler, this);
	m_activeTableScanner->Start(1000);
}

ibDialogActiveUser::~ibDialogActiveUser()
{
	if (m_activeTableScanner->IsRunning())  m_activeTableScanner->Stop();
	m_activeTableScanner->Unbind(wxEVT_TIMER, &ibDialogActiveUser::OnIdleHandler, this);
}

void ibDialogActiveUser::OnIdleHandler(wxTimerEvent& WXUNUSED(event))
{
	// Asked of the server again, as the desktop's read its registry again.
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	ibProtocolNode params, shown;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::ActiveUser));
	if (frame == nullptr || !frame->Call(ibProtocolMethod::Schema, params, shown))
		return;

	RefreshActiveUserTable(shown);
	RefreshLocksTable(shown);
}
