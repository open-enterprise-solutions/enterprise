////////////////////////////////////////////////////////////////////////////
//	Description : Git service implementation — see gitService.h.
////////////////////////////////////////////////////////////////////////////
#include "gitService.h"

#include <wx/utils.h>      // wxExecute
#include <wx/filename.h>
#include <wx/tokenzr.h>

// ---------------------------------------------------------------------------
// arg quoting — we build the command string ourselves (the portable wxExecute
// overload that captures output takes a string, not an argv), so every arg is
// wrapped to survive spaces and to block shell injection from UI-supplied
// branch names / commit messages.
// ---------------------------------------------------------------------------
static wxString QuoteArg(const wxString& a) {
#ifdef __WXMSW__
	// cmd.exe: wrap in double quotes, escape embedded quotes by doubling.
	wxString s = a; s.Replace(wxT("\""), wxT("\"\""));
	return wxT("\"") + s + wxT("\"");
#else
	// POSIX shells: single-quote, and close/escape/reopen for any embedded '.
	wxString s = a; s.Replace(wxT("'"), wxT("'\\''"));
	return wxT("'") + s + wxT("'");
#endif
}

ibGitService::ibGitService(const wxString& workdir)
	: m_workdir(workdir) {}

bool ibGitService::IsGitAvailable() {
	wxArrayString out, err;
	const long rc = wxExecute(wxT("git --version"), out, err, wxEXEC_SYNC | wxEXEC_NODISABLE);
	return rc == 0;
}

ibGitResult ibGitService::Run(const wxArrayString& args) const {
	// git -C <workdir> <args...>
	wxString cmd = wxT("git -C ") + QuoteArg(m_workdir);
	for (size_t i = 0; i < args.GetCount(); ++i)
		cmd += wxT(" ") + QuoteArg(args[i]);

	wxArrayString out, err;
	const long rc = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_NODISABLE);

	ibGitResult r;
	r.exitCode = rc;
	r.ok = (rc == 0);
	r.output = wxJoin(out, '\n');
	r.error = wxJoin(err, '\n');
	return r;
}

// ---- lifecycle ------------------------------------------------------------

bool ibGitService::IsRepo() const {
	wxArrayString args; args.Add(wxT("rev-parse")); args.Add(wxT("--is-inside-work-tree"));
	const ibGitResult r = Run(args);
	return r.ok && r.output.Strip(wxString::both) == wxT("true");
}

ibGitResult ibGitService::Init() {
	wxArrayString args; args.Add(wxT("init"));
	return Run(args);
}

ibGitResult ibGitService::Clone(const wxString& url) {
	// clone needs the parent dir; run git from there with explicit target.
	wxArrayString out, err;
	const wxString cmd = wxT("git clone ") + QuoteArg(url) + wxT(" ") + QuoteArg(m_workdir);
	const long rc = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_NODISABLE);
	ibGitResult r; r.exitCode = rc; r.ok = (rc == 0);
	r.output = wxJoin(out, '\n'); r.error = wxJoin(err, '\n');
	return r;
}

// ---- inspection -----------------------------------------------------------

std::vector<ibGitStatusEntry> ibGitService::Status() const {
	std::vector<ibGitStatusEntry> result;
	wxArrayString args; args.Add(wxT("status")); args.Add(wxT("--porcelain"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;

	wxStringTokenizer lines(r.output, wxT("\n"));
	while (lines.HasMoreTokens()) {
		const wxString line = lines.GetNextToken();
		if (line.length() < 4) continue;          // "XY path"
		ibGitStatusEntry e;
		e.xy = line.substr(0, 2);
		e.path = line.substr(3);
		// rename form "old -> new" — keep the new path
		const int arrow = e.path.Find(wxT(" -> "));
		if (arrow != wxNOT_FOUND) e.path = e.path.substr(arrow + 4);
		e.untracked = (e.xy == wxT("??"));
		e.staged = !e.untracked && e.xy[0] != wxT(' ');
		result.push_back(e);
	}
	return result;
}

std::vector<ibGitLogEntry> ibGitService::Log(int maxCount) const {
	std::vector<ibGitLogEntry> result;
	wxArrayString args;
	args.Add(wxT("log"));
	args.Add(wxString::Format(wxT("-n%d"), maxCount));
	// unit-separator delimited so subjects with spaces stay intact
	args.Add(wxT("--pretty=format:%h\x1f%an\x1f%ad\x1f%s"));
	args.Add(wxT("--date=short"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;

	wxStringTokenizer lines(r.output, wxT("\n"));
	while (lines.HasMoreTokens()) {
		const wxString line = lines.GetNextToken();
		const wxArrayString f = wxStringTokenize(line, wxT("\x1f"), wxTOKEN_RET_EMPTY_ALL);
		if (f.GetCount() < 4) continue;
		ibGitLogEntry e;
		e.hash = f[0]; e.author = f[1]; e.date = f[2]; e.subject = f[3];
		result.push_back(e);
	}
	return result;
}

wxString ibGitService::CurrentBranch() const {
	wxArrayString args; args.Add(wxT("rev-parse")); args.Add(wxT("--abbrev-ref")); args.Add(wxT("HEAD"));
	const ibGitResult r = Run(args);
	if (!r.ok) return wxEmptyString;
	const wxString b = r.output.Strip(wxString::both);
	return (b == wxT("HEAD")) ? wxString() : b;   // detached
}

wxArrayString ibGitService::Branches() const {
	wxArrayString result;
	wxArrayString args; args.Add(wxT("branch")); args.Add(wxT("--format=%(refname:short)"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;
	wxStringTokenizer t(r.output, wxT("\n"));
	while (t.HasMoreTokens()) {
		const wxString b = t.GetNextToken().Trim();
		if (!b.empty()) result.Add(b);
	}
	return result;
}

wxString ibGitService::Diff(const wxString& path) const {
	wxArrayString args; args.Add(wxT("diff"));
	if (!path.empty()) { args.Add(wxT("--")); args.Add(path); }
	const ibGitResult r = Run(args);
	return r.ok ? r.output : r.error;
}

// ---- mutation -------------------------------------------------------------

ibGitResult ibGitService::StageAll() {
	wxArrayString args; args.Add(wxT("add")); args.Add(wxT("-A"));
	return Run(args);
}

ibGitResult ibGitService::Stage(const wxArrayString& paths) {
	wxArrayString args; args.Add(wxT("add")); args.Add(wxT("--"));
	for (size_t i = 0; i < paths.GetCount(); ++i) args.Add(paths[i]);
	return Run(args);
}

ibGitResult ibGitService::Commit(const wxString& message) {
	wxArrayString args; args.Add(wxT("commit")); args.Add(wxT("-m")); args.Add(message);
	return Run(args);
}

ibGitResult ibGitService::CommitAll(const wxString& message) {
	const ibGitResult staged = StageAll();
	if (!staged.ok) return staged;
	return Commit(message);
}

ibGitResult ibGitService::CreateBranch(const wxString& name, bool checkout) {
	wxArrayString args;
	if (checkout) { args.Add(wxT("checkout")); args.Add(wxT("-b")); args.Add(name); }
	else          { args.Add(wxT("branch")); args.Add(name); }
	return Run(args);
}

ibGitResult ibGitService::Checkout(const wxString& branch) {
	wxArrayString args; args.Add(wxT("checkout")); args.Add(branch);
	return Run(args);
}

ibGitResult ibGitService::Push(const wxString& remote, const wxString& branch) {
	wxArrayString args; args.Add(wxT("push")); args.Add(remote);
	if (!branch.empty()) args.Add(branch);
	return Run(args);
}

ibGitResult ibGitService::Pull(const wxString& remote, const wxString& branch) {
	wxArrayString args; args.Add(wxT("pull")); args.Add(remote);
	if (!branch.empty()) args.Add(branch);
	return Run(args);
}
