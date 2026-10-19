////////////////////////////////////////////////////////////////////////////
//	Description : Git panel implementation — see gitPanel.h.
////////////////////////////////////////////////////////////////////////////
#include "gitPanel.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>

enum {
	ID_GIT_REFRESH = wxID_HIGHEST + 7100,
	ID_GIT_COMMIT,
	ID_GIT_PUSH,
	ID_GIT_PULL,
	ID_GIT_BRANCH,
	ID_GIT_NEWBRANCH,
	ID_GIT_STATUSLIST,
};

ibGitPanel::ibGitPanel(wxWindow* parent, const wxString& workdir)
	: wxPanel(parent, wxID_ANY)
{
	BuildUI();
	SetWorkdir(workdir);
}

void ibGitPanel::BuildUI()
{
	wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

	// Branch row: current/switch + new.
	wxBoxSizer* brow = new wxBoxSizer(wxHORIZONTAL);
	brow->Add(new wxStaticText(this, wxID_ANY, _("Branch:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_branchChoice = new wxChoice(this, ID_GIT_BRANCH);
	brow->Add(m_branchChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	m_btnNewBranch = new wxButton(this, ID_GIT_NEWBRANCH, _("New..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	brow->Add(m_btnNewBranch, 0, wxALIGN_CENTER_VERTICAL);
	root->Add(brow, 0, wxEXPAND | wxALL, 4);

	// Actions row.
	wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
	m_btnRefresh = new wxButton(this, ID_GIT_REFRESH, _("Refresh"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_btnCommit  = new wxButton(this, ID_GIT_COMMIT,  _("Commit..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_btnPush    = new wxButton(this, ID_GIT_PUSH,    _("Push"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	m_btnPull    = new wxButton(this, ID_GIT_PULL,    _("Pull"), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	bar->Add(m_btnRefresh, 0, wxRIGHT, 3);
	bar->Add(m_btnCommit,  0, wxRIGHT, 3);
	bar->Add(m_btnPush,    0, wxRIGHT, 3);
	bar->Add(m_btnPull,    0, wxRIGHT, 3);
	root->Add(bar, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	// Changes (double-click a row -> diff).
	root->Add(new wxStaticText(this, wxID_ANY, _("Changes (double-click for diff):")), 0, wxLEFT | wxRIGHT, 4);
	m_statusList = new wxListCtrl(this, ID_GIT_STATUSLIST, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_statusList->InsertColumn(0, _("State"), wxLIST_FORMAT_LEFT, 60);
	m_statusList->InsertColumn(1, _("Path"),  wxLIST_FORMAT_LEFT, 320);
	root->Add(m_statusList, 2, wxEXPAND | wxALL, 4);

	// Recent history.
	root->Add(new wxStaticText(this, wxID_ANY, _("History:")), 0, wxLEFT | wxRIGHT, 4);
	m_logList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_logList->InsertColumn(0, _("Hash"),    wxLIST_FORMAT_LEFT, 70);
	m_logList->InsertColumn(1, _("Date"),    wxLIST_FORMAT_LEFT, 90);
	m_logList->InsertColumn(2, _("Subject"), wxLIST_FORMAT_LEFT, 280);
	root->Add(m_logList, 1, wxEXPAND | wxALL, 4);

	m_info = new wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(m_info, 0, wxALL, 4);

	SetSizer(root);

	Bind(wxEVT_BUTTON, &ibGitPanel::OnRefresh,   this, ID_GIT_REFRESH);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnCommit,    this, ID_GIT_COMMIT);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnPush,      this, ID_GIT_PUSH);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnPull,      this, ID_GIT_PULL);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnNewBranch, this, ID_GIT_NEWBRANCH);
	Bind(wxEVT_CHOICE, &ibGitPanel::OnBranchSelected, this, ID_GIT_BRANCH);
	Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibGitPanel::OnStatusActivated, this, ID_GIT_STATUSLIST);
}

void ibGitPanel::SetWorkdir(const wxString& workdir)
{
	m_workdir = workdir;
	if (workdir.empty()) {
		m_git.reset();
		m_branchChoice->Clear();
		m_statusList->DeleteAllItems();
		m_logList->DeleteAllItems();
		m_info->SetLabel(_("(no repository)"));
		return;
	}
	m_git.reset(new ibGitService(workdir));
	RefreshStatus();
}

void ibGitPanel::RefreshBranches()
{
	m_branchChoice->Clear();
	if (!m_git || !m_git->IsRepo()) return;
	const wxArrayString branches = m_git->Branches();
	m_branchChoice->Append(branches);
	const wxString cur = m_git->CurrentBranch();
	const int sel = m_branchChoice->FindString(cur);
	if (sel != wxNOT_FOUND) m_branchChoice->SetSelection(sel);
}

void ibGitPanel::RefreshLog()
{
	m_logList->DeleteAllItems();
	if (!m_git || !m_git->IsRepo()) return;
	const auto log = m_git->Log(30);
	long row = 0;
	for (const auto& e : log) {
		const long i = m_logList->InsertItem(row++, e.hash);
		m_logList->SetItem(i, 1, e.date);
		m_logList->SetItem(i, 2, e.subject);
	}
}

void ibGitPanel::RefreshStatus()
{
	m_statusList->DeleteAllItems();
	if (!m_git || !m_git->IsRepo()) {
		m_branchChoice->Clear();
		m_logList->DeleteAllItems();
		m_info->SetLabel(_("(not a git repository)"));
		return;
	}
	RefreshBranches();
	RefreshLog();

	const auto entries = m_git->Status();
	long row = 0;
	for (const auto& e : entries) {
		const long i = m_statusList->InsertItem(row++, e.xy);
		m_statusList->SetItem(i, 1, e.path);
	}
	m_info->SetLabel(wxString::Format(_("%zu change(s)"), entries.size()));
}

void ibGitPanel::ReportResult(const wxString& action, const ibGitResult& r)
{
	if (r.ok) {
		m_info->SetLabel(action + _(": done"));
	} else {
		// git failures are normal control flow — surface, don't throw.
		const wxString msg = r.error.empty() ? r.output : r.error;
		m_info->SetLabel(action + _(": failed"));
		wxMessageBox(msg, action + _(" failed"), wxOK | wxICON_WARNING, this);
	}
}

void ibGitPanel::OnRefresh(wxCommandEvent&) { RefreshStatus(); }

void ibGitPanel::OnCommit(wxCommandEvent&)
{
	if (!m_git || !m_git->IsRepo()) return;
	wxTextEntryDialog dlg(this, _("Commit message:"), _("Commit"));
	if (dlg.ShowModal() != wxID_OK) return;
	const wxString msg = dlg.GetValue();
	if (msg.Strip(wxString::both).empty()) {
		wxMessageBox(_("Empty commit message."), _("Commit"), wxOK | wxICON_INFORMATION, this);
		return;
	}
	ReportResult(_("Commit"), m_git->CommitAll(msg));
	RefreshStatus();
}

void ibGitPanel::OnPush(wxCommandEvent&)
{
	if (!m_git || !m_git->IsRepo()) return;
	ReportResult(_("Push"), m_git->Push());
}

void ibGitPanel::OnPull(wxCommandEvent&)
{
	if (!m_git || !m_git->IsRepo()) return;
	ReportResult(_("Pull"), m_git->Pull());
	RefreshStatus();
}

void ibGitPanel::OnBranchSelected(wxCommandEvent&)
{
	if (!m_git || !m_git->IsRepo()) return;
	const wxString target = m_branchChoice->GetStringSelection();
	if (target.empty() || target == m_git->CurrentBranch()) return;
	const ibGitResult r = m_git->Checkout(target);
	ReportResult(wxString::Format(_("Checkout %s"), target), r);
	RefreshStatus();   // restores the combo selection if the checkout failed
}

void ibGitPanel::OnNewBranch(wxCommandEvent&)
{
	if (!m_git || !m_git->IsRepo()) return;
	wxTextEntryDialog dlg(this, _("New branch name:"), _("New branch"));
	if (dlg.ShowModal() != wxID_OK) return;
	const wxString name = dlg.GetValue().Strip(wxString::both);
	if (name.empty()) return;
	ReportResult(wxString::Format(_("Create %s"), name), m_git->CreateBranch(name, /*checkout*/true));
	RefreshStatus();
}

void ibGitPanel::OnStatusActivated(wxListEvent& event)
{
	if (!m_git || !m_git->IsRepo()) return;
	const long item = event.GetIndex();
	if (item < 0) return;
	const wxString path = m_statusList->GetItemText(item, 1);
	const wxString diff = m_git->Diff(path);

	// Read-only monospaced diff viewer.
	wxDialog dlg(this, wxID_ANY, wxString::Format(_("Diff: %s"), path),
		wxDefaultPosition, wxSize(700, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	wxTextCtrl* txt = new wxTextCtrl(&dlg, wxID_ANY,
		diff.empty() ? _("(no diff - untracked, binary, or staged)") : diff,
		wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	txt->SetFont(wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE)));
	wxBoxSizer* s = new wxBoxSizer(wxVERTICAL);
	s->Add(txt, 1, wxEXPAND | wxALL, 6);
	dlg.SetSizer(s);
	dlg.ShowModal();
}
