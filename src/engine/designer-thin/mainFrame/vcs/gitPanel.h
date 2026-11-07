////////////////////////////////////////////////////////////////////////////
//	Description : Git panel for the Designer — a dockable AUI pane that drives
//	              ibGitService against the configuration working copy (the dir
//	              where the config is exported to OES-XML / OES-JSON). First
//	              increment: status list + Refresh / Commit / Push / Pull.
//	              Diff view, log, and branch switching land as later steps.
////////////////////////////////////////////////////////////////////////////
#ifndef _OES_DESIGNER_GIT_PANEL_H_
#define _OES_DESIGNER_GIT_PANEL_H_

#include "deskgui/univ/panel.h"
#include "deskgui/univ/ctrl/listCtrl.h"
#include "deskgui/univ/ctrl/button.h"
#include "deskgui/univ/ctrl/staticText.h"
#include "deskgui/univ/ctrl/choice.h"
#include <memory>

#include "designer-thin/backend/vcs/gitService.h"   // the engine's, living in the thin designer

// THE DESKTOP'S (designer/mainFrame/vcs/gitPanel.h) WHOLE — over the working copy beside the file base the designer was
// started on (CreateGitPane), on this machine.

class ibGitPanel : public ibPanel {
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

	ibChoice*     m_branchChoice = nullptr;
	ibButton*     m_btnNewBranch = nullptr;
	ibListCtrl*   m_statusList   = nullptr;
	ibListCtrl*   m_logList      = nullptr;
	ibButton*     m_btnRefresh   = nullptr;
	ibButton*     m_btnCommit    = nullptr;
	ibButton*     m_btnPush      = nullptr;
	ibButton*     m_btnPull      = nullptr;
	ibStaticText* m_info         = nullptr;   // last-action result line
};

#endif // _OES_DESIGNER_GIT_PANEL_H_
