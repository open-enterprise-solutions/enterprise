////////////////////////////////////////////////////////////////////////////
//	Description : Git panel implementation — see gitPanel.h.
////////////////////////////////////////////////////////////////////////////
#include "gitPanel.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>

enum {
	ID_GIT_REFRESH = wxID_HIGHEST + 7100,
	ID_GIT_COMMIT,
	ID_GIT_PUSH,
	ID_GIT_PULL,
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

	m_branchLabel = new wxStaticText(this, wxID_ANY, _("(no repository)"));
	root->Add(m_branchLabel, 0, wxALL, 4);

	// Toolbar row of actions. Kept as plain buttons (not a wxToolBar) so the
	// pane stays light and the labels read in the active locale.
	wxBoxSizer* bar = new wxBoxSizer(wxHORIZONTAL);
	m_btnRefresh = new wxButton(this, ID_GIT_REFRESH, _("Refresh"));
	m_btnCommit  = new wxButton(this, ID_GIT_COMMIT,  _("Commit..."));
	m_btnPush    = new wxButton(this, ID_GIT_PUSH,    _("Push"));
	m_btnPull    = new wxButton(this, ID_GIT_PULL,    _("Pull"));
	bar->Add(m_btnRefresh, 0, wxRIGHT, 3);
	bar->Add(m_btnCommit,  0, wxRIGHT, 3);
	bar->Add(m_btnPush,    0, wxRIGHT, 3);
	bar->Add(m_btnPull,    0, wxRIGHT, 3);
	root->Add(bar, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

	m_statusList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_statusList->InsertColumn(0, _("State"), wxLIST_FORMAT_LEFT, 60);
	m_statusList->InsertColumn(1, _("Path"),  wxLIST_FORMAT_LEFT, 320);
	root->Add(m_statusList, 1, wxEXPAND | wxALL, 4);

	m_info = new wxStaticText(this, wxID_ANY, wxEmptyString);
	root->Add(m_info, 0, wxALL, 4);

	SetSizer(root);

	Bind(wxEVT_BUTTON, &ibGitPanel::OnRefresh, this, ID_GIT_REFRESH);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnCommit,  this, ID_GIT_COMMIT);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnPush,    this, ID_GIT_PUSH);
	Bind(wxEVT_BUTTON, &ibGitPanel::OnPull,    this, ID_GIT_PULL);
}

void ibGitPanel::SetWorkdir(const wxString& workdir)
{
	m_workdir = workdir;
	if (workdir.empty()) {
		m_git.reset();
		m_branchLabel->SetLabel(_("(no repository)"));
		m_statusList->DeleteAllItems();
		return;
	}
	m_git.reset(new ibGitService(workdir));
	RefreshStatus();
}

void ibGitPanel::UpdateBranchLabel()
{
	if (!m_git || !m_git->IsRepo()) {
		m_branchLabel->SetLabel(_("(not a git repository)"));
		return;
	}
	const wxString br = m_git->CurrentBranch();
	m_branchLabel->SetLabel(wxString::Format(_("Branch: %s"),
		br.empty() ? _("(detached)") : br));
}

void ibGitPanel::RefreshStatus()
{
	m_statusList->DeleteAllItems();
	UpdateBranchLabel();
	if (!m_git || !m_git->IsRepo()) return;

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
