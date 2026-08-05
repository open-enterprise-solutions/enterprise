////////////////////////////////////////////////////////////////////////////
//	Description : Git panel for the Designer — a dockable AUI pane that drives
//	              ibGitService against the configuration working copy (the dir
//	              where the config is exported to OES-XML / OES-JSON). First
//	              increment: status list + Refresh / Commit / Push / Pull.
//	              Diff view, log, and branch switching land as later steps.
////////////////////////////////////////////////////////////////////////////
#ifndef _OES_DESIGNER_GIT_PANEL_H_
#define _OES_DESIGNER_GIT_PANEL_H_

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/choice.h>
#include <memory>

#include "backend/vcs/gitService.h"

class ibGitPanel : public wxPanel {
public:
	// `workdir` is the configuration working copy. Empty until a config is
	// opened; SetWorkdir() rebinds the service and refreshes.
	ibGitPanel(wxWindow* parent, const wxString& workdir = wxEmptyString);

	void SetWorkdir(const wxString& workdir);
	void RefreshStatus();   // re-run git status + branches + log

private:
	void BuildUI();
	void OnRefresh(wxCommandEvent&);
	void OnCommit(wxCommandEvent&);
	void OnPush(wxCommandEvent&);
	void OnPull(wxCommandEvent&);
	void OnBranchSelected(wxCommandEvent&);   // switch to the chosen branch
	void OnNewBranch(wxCommandEvent&);        // create + checkout a branch
	void OnStatusActivated(wxListEvent&);     // double-click a file -> show its diff

	void RefreshBranches();
	void RefreshLog();
	// Show a non-fatal git error (push rejected, not a repo, etc.) in the status bar line.
	void ReportResult(const wxString& action, const ibGitResult& r);

	std::unique_ptr<ibGitService> m_git;   // null until a workdir is bound
	wxString      m_workdir;

	wxChoice*     m_branchChoice = nullptr;
	wxButton*     m_btnNewBranch = nullptr;
	wxListCtrl*   m_statusList   = nullptr;
	wxListCtrl*   m_logList      = nullptr;
	wxButton*     m_btnRefresh   = nullptr;
	wxButton*     m_btnCommit    = nullptr;
	wxButton*     m_btnPush      = nullptr;
	wxButton*     m_btnPull      = nullptr;
	wxStaticText* m_info         = nullptr;   // last-action result line
};

#endif // _OES_DESIGNER_GIT_PANEL_H_
