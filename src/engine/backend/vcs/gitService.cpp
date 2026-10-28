////////////////////////////////////////////////////////////////////////////
//	Description : Git service implementation — see gitService.h.
////////////////////////////////////////////////////////////////////////////
#include "gitService.h"

#include <wx/utils.h>      // wxExecute
#include <wx/filename.h>
#include "core/stringTokenizer.h"

// ---------------------------------------------------------------------------
// arg quoting — we build the command string ourselves (the portable wxExecute
// overload that captures output takes a string, not an argv), so every arg is
// wrapped to survive spaces and to block shell injection from UI-supplied
// branch names / commit messages.
// ---------------------------------------------------------------------------
static ibString QuoteArg(const ibString& a) {
#ifdef __WXMSW__
	// cmd.exe: wrap in double quotes, escape embedded quotes by doubling.
	ibString s = a; s.Replace(ibT("\""), ibT("\"\""));
	return ibT("\"") + s + ibT("\"");
#else
	// POSIX shells: single-quote, and close/escape/reopen for any embedded '.
	ibString s = a; s.Replace(ibT("'"), ibT("'\\''"));
	return ibT("'") + s + ibT("'");
#endif
}

ibGitService::ibGitService(const ibString& workdir)
	: m_workdir(workdir) {}

bool ibGitService::IsGitAvailable() {
	wxArrayString out, err;   // wxExecute's own: it writes the lines into them
	const long rc = wxExecute(wxT("git --version"), out, err, wxEXEC_SYNC | wxEXEC_NODISABLE);
	return rc == 0;
}

ibGitResult ibGitService::Run(const std::vector<ibString>& args) const {
	// git -C <workdir> <args...>
	ibString cmd = ibT("git -C ") + QuoteArg(m_workdir);
	for (size_t i = 0; i < args.size(); ++i)
		cmd += ibT(" ") + QuoteArg(args[i]);

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
	std::vector<ibString> args; args.push_back(ibT("rev-parse")); args.push_back(ibT("--is-inside-work-tree"));
	const ibGitResult r = Run(args);
	return r.ok && r.output.Strip(ibString::both) == ibT("true");
}

ibGitResult ibGitService::Init() {
	std::vector<ibString> args; args.push_back(ibT("init"));
	return Run(args);
}

ibGitResult ibGitService::Clone(const ibString& url) {
	// clone needs the parent dir; run git from there with explicit target.
	wxArrayString out, err;
	const ibString cmd = ibT("git clone ") + QuoteArg(url) + ibT(" ") + QuoteArg(m_workdir);
	const long rc = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_NODISABLE);
	ibGitResult r; r.exitCode = rc; r.ok = (rc == 0);
	r.output = wxJoin(out, '\n'); r.error = wxJoin(err, '\n');
	return r;
}

// ---- inspection -----------------------------------------------------------

std::vector<ibGitStatusEntry> ibGitService::Status() const {
	std::vector<ibGitStatusEntry> result;
	std::vector<ibString> args; args.push_back(ibT("status")); args.push_back(ibT("--porcelain"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;

	ibStringTokenizer lines(r.output, ibT("\n"));
	while (lines.HasMoreTokens()) {
		const ibString line = lines.GetNextToken();
		if (line.length() < 4) continue;          // "XY path"
		ibGitStatusEntry e;
		e.xy = line.substr(0, 2);
		e.path = line.substr(3);
		// rename form "old -> new" — keep the new path
		const int arrow = e.path.Find(ibT(" -> "));
		if (arrow != wxNOT_FOUND) e.path = e.path.substr(arrow + 4);
		e.untracked = (e.xy == ibT("??"));
		e.staged = !e.untracked && e.xy[0] != ibT(' ');
		result.push_back(e);
	}
	return result;
}

std::vector<ibGitLogEntry> ibGitService::Log(int maxCount) const {
	std::vector<ibGitLogEntry> result;
	std::vector<ibString> args;
	args.push_back(ibT("log"));
	args.push_back(ibString::Format(ibT("-n%d"), maxCount));
	// unit-separator delimited so subjects with spaces stay intact
	args.push_back(ibT("--pretty=format:%h\x1f%an\x1f%ad\x1f%s"));
	args.push_back(ibT("--date=short"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;

	ibStringTokenizer lines(r.output, ibT("\n"));
	while (lines.HasMoreTokens()) {
		const ibString line = lines.GetNextToken();
		const std::vector<ibString> f = ibStringTokenize(line, ibT("\x1f"), ibTOKEN_RET_EMPTY_ALL);
		if (f.size() < 4) continue;
		ibGitLogEntry e;
		e.hash = f[0]; e.author = f[1]; e.date = f[2]; e.subject = f[3];
		result.push_back(e);
	}
	return result;
}

ibString ibGitService::CurrentBranch() const {
	std::vector<ibString> args; args.push_back(ibT("rev-parse")); args.push_back(ibT("--abbrev-ref")); args.push_back(ibT("HEAD"));
	const ibGitResult r = Run(args);
	if (!r.ok) return ibEmptyString;
	const ibString b = r.output.Strip(ibString::both);
	return (b == ibT("HEAD")) ? ibString() : b;   // detached
}

std::vector<ibString> ibGitService::Branches() const {
	std::vector<ibString> result;
	std::vector<ibString> args; args.push_back(ibT("branch")); args.push_back(ibT("--format=%(refname:short)"));
	const ibGitResult r = Run(args);
	if (!r.ok) return result;
	ibStringTokenizer t(r.output, ibT("\n"));
	while (t.HasMoreTokens()) {
		const ibString b = t.GetNextToken().Trim();
		if (!b.empty()) result.push_back(b);
	}
	return result;
}

ibString ibGitService::Diff(const ibString& path) const {
	std::vector<ibString> args; args.push_back(ibT("diff"));
	if (!path.empty()) { args.push_back(ibT("--")); args.push_back(path); }
	const ibGitResult r = Run(args);
	return r.ok ? r.output : r.error;
}

// ---- mutation -------------------------------------------------------------

ibGitResult ibGitService::StageAll() {
	std::vector<ibString> args; args.push_back(ibT("add")); args.push_back(ibT("-A"));
	return Run(args);
}

ibGitResult ibGitService::Stage(const std::vector<ibString>& paths) {
	std::vector<ibString> args; args.push_back(ibT("add")); args.push_back(ibT("--"));
	for (size_t i = 0; i < paths.size(); ++i) args.push_back(paths[i]);
	return Run(args);
}

ibGitResult ibGitService::Commit(const ibString& message) {
	std::vector<ibString> args; args.push_back(ibT("commit")); args.push_back(ibT("-m")); args.push_back(message);
	return Run(args);
}

ibGitResult ibGitService::CommitAll(const ibString& message) {
	const ibGitResult staged = StageAll();
	if (!staged.ok) return staged;
	return Commit(message);
}

ibGitResult ibGitService::CreateBranch(const ibString& name, bool checkout) {
	std::vector<ibString> args;
	if (checkout) { args.push_back(ibT("checkout")); args.push_back(ibT("-b")); args.push_back(name); }
	else          { args.push_back(ibT("branch")); args.push_back(name); }
	return Run(args);
}

ibGitResult ibGitService::Checkout(const ibString& branch) {
	std::vector<ibString> args; args.push_back(ibT("checkout")); args.push_back(branch);
	return Run(args);
}

ibGitResult ibGitService::Push(const ibString& remote, const ibString& branch) {
	std::vector<ibString> args; args.push_back(ibT("push")); args.push_back(remote);
	if (!branch.empty()) args.push_back(branch);
	return Run(args);
}

ibGitResult ibGitService::Pull(const ibString& remote, const ibString& branch) {
	std::vector<ibString> args; args.push_back(ibT("pull")); args.push_back(remote);
	if (!branch.empty()) args.push_back(branch);
	return Run(args);
}
