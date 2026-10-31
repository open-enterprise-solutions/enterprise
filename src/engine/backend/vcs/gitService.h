////////////////////////////////////////////////////////////////////////////
//	Description : Git service — a thin, GUI-free wrapper over the `git` CLI for
//	              version-controlling a configuration working copy from the
//	              Designer. Lives in the backend (wxBase only) so the Designer,
//	              the daemon, and the future web client can all reuse it.
//
//	Workflow it serves: the Designer exports the configuration to a working dir
//	as OES-XML / OES-JSON (already supported), git tracks those text files, and
//	this service drives status / diff / commit / branch / push-pull from the IDE.
////////////////////////////////////////////////////////////////////////////
#ifndef _OES_GIT_SERVICE_H_
#define _OES_GIT_SERVICE_H_

#include <wx/string.h>
#include <wx/arrstr.h>
#include <vector>

#include "backend/backend_core.h"          // BACKEND_API — the Designer links this across the DLL

// Outcome of a single git invocation. Never throws — git failures are normal
// control flow (not a repo, nothing to commit, push rejected), so the caller
// inspects `ok` + `error`.
struct ibGitResult {
	bool         ok = false;       // exit code 0
	long         exitCode = -1;
	ibString     output;           // joined stdout
	ibString     error;            // joined stderr
};

// One porcelain status entry.
struct ibGitStatusEntry {
	ibString path;
	ibString xy;                   // two-char XY code from `git status --porcelain` (e.g. " M", "??", "A ")
	bool     staged = false;       // X column non-space and not '?'
	bool     untracked = false;    // "??"
};

// One log entry.
struct ibGitLogEntry {
	ibString hash;                 // short hash
	ibString author;
	ibString date;                 // ISO
	ibString subject;
};

class BACKEND_API ibGitService {
public:
	// `workdir` is the configuration working copy (where XML/JSON is exported).
	explicit ibGitService(const ibString& workdir);

	// `git` reachable on PATH at all?
	static bool IsGitAvailable();

	// Repo lifecycle ------------------------------------------------------
	bool      IsRepo() const;                          // workdir inside a git work-tree
	ibGitResult Init();                                // git init
	ibGitResult Clone(const ibString& url);            // git clone <url> into workdir

	// Inspection ----------------------------------------------------------
	std::vector<ibGitStatusEntry> Status() const;
	std::vector<ibGitLogEntry>    Log(int maxCount = 50) const;
	ibString    CurrentBranch() const;                 // empty on detached / non-repo
	std::vector<ibString> Branches() const;                    // local branch names
	ibString    Diff(const ibString& path = ibEmptyString) const;  // unified diff, optionally one path

	// Mutation ------------------------------------------------------------
	ibGitResult StageAll();                            // git add -A
	ibGitResult Stage(const std::vector<ibString>& paths);
	ibGitResult Commit(const ibString& message);       // commits the staged set
	ibGitResult CommitAll(const ibString& message);    // stage -A then commit
	ibGitResult CreateBranch(const ibString& name, bool checkout = true);
	ibGitResult Checkout(const ibString& branch);
	ibGitResult Push(const ibString& remote = ibT("origin"), const ibString& branch = ibEmptyString);
	ibGitResult Pull(const ibString& remote = ibT("origin"), const ibString& branch = ibEmptyString);

	const ibString& Workdir() const { return m_workdir; }

private:
	// Run `git -C <workdir> <args...>` synchronously, capturing stdout/stderr.
	ibGitResult Run(const std::vector<ibString>& args) const;

	ibString m_workdir;
};

#endif // _OES_GIT_SERVICE_H_
