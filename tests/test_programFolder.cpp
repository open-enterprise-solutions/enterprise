// =============================================================================
// OES Enterprise — the program folder
//
// One rule for "where the libraries and the shared files are". Inside a macOS
// bundle the binary is X.app/Contents/MacOS/<name> and that folder is the
// directory containing X.app. The third directory has to end in .app.
// Everywhere else it is the binary's own directory.
//
// These paths are synthetic. wxFileName writes the platform separator, so the
// comparisons go through SameAs rather than the spelling of the string.
// =============================================================================

#include <gtest/gtest.h>

#include "core/programFolder.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace {

void ExpectFolder(const wxString& executablePath, const wxString& folder)
{
	const wxString got = ibProgramFolder(executablePath);
	EXPECT_TRUE(wxFileName::DirName(got).SameAs(wxFileName::DirName(folder)))
		<< got.ToStdString() << " vs " << folder.ToStdString();
}

} // namespace

TEST(ProgramFolder, ABundleResolvesBesideTheApp) {
	ExpectFolder(
		wxT("/Applications/enterprise.app/Contents/MacOS/enterprise"),
		wxT("/Applications"));
}

TEST(ProgramFolder, ABundleAtTheRootResolvesToTheRoot) {
	ExpectFolder(wxT("/enterprise.app/Contents/MacOS/enterprise"), wxT("/"));
}

TEST(ProgramFolder, AFlatBinaryResolvesToItsDirectory) {
	ExpectFolder(wxT("/opt/oes/bin/enterprise"), wxT("/opt/oes/bin"));
}

TEST(ProgramFolder, MacOSAloneIsNotABundle) {
	ExpectFolder(wxT("/opt/MacOS/enterprise"), wxT("/opt/MacOS"));
}

TEST(ProgramFolder, ContentsWithoutMacOSIsNotABundle) {
	ExpectFolder(
		wxT("/opt/oes/Contents/bin/enterprise"),
		wxT("/opt/oes/Contents/bin"));
}

// Contents/MacOS under a directory that is not a bundle is not a bundle.
TEST(ProgramFolder, ContentsMacOSWithoutAnAppIsNotABundle) {
	ExpectFolder(wxT("/opt/foo/Contents/MacOS/bin"), wxT("/opt/foo/Contents/MacOS"));
}

// The bundle names are the ones Apple writes. A different case is a directory
// that happens to be spelled that way, and it is left alone.
TEST(ProgramFolder, TheBundleNamesAreCaseSensitive) {
	ExpectFolder(
		wxT("/Applications/enterprise.app/contents/macos/enterprise"),
		wxT("/Applications/enterprise.app/contents/macos"));
}

// The folder holds this process's binary, or the .app that holds it. Comparing
// the two overloads of ibProgramFolder with each other would not.
TEST(ProgramFolder, TheFolderHoldsThisBinaryOrItsApp) {
	const wxString exe = wxStandardPaths::Get().GetExecutablePath();
	ASSERT_FALSE(exe.IsEmpty());

	const wxFileName executable(exe);
	const wxString folder = ibProgramFolder();
	const wxFileName beside(folder, executable.GetFullName());
	const bool holdsBinary = beside.FileExists() && beside.SameAs(executable);

	bool holdsApp = false;
	const wxArrayString& dirs = executable.GetDirs();
	for (size_t i = 0; i < dirs.GetCount(); ++i) {
		if (!dirs[i].EndsWith(wxT(".app")))
			continue;
		wxFileName app = wxFileName::DirName(folder);
		app.AppendDir(dirs[i]);
		holdsApp = app.DirExists();
		break;
	}
	EXPECT_TRUE(holdsBinary || holdsApp)
		<< folder.ToStdString() << " does not hold " << exe.ToStdString();
}

// Off a bundle the diagnostics stay in the program folder. An installed bundle
// on macOS uses ~/Library/Logs/OES, which this process is not.
TEST(ProgramFolder, DiagnosticsFollowTheBundle) {
	const wxString exe = wxStandardPaths::Get().GetExecutablePath();
	const wxFileName executable(exe);
	const wxArrayString& dirs = executable.GetDirs();
	bool bundle = false;
	if (dirs.GetCount() >= 3) {
		const size_t n = dirs.GetCount();
		bundle = dirs[n - 1].IsSameAs(wxT("MacOS"))
			&& dirs[n - 2].IsSameAs(wxT("Contents"))
			&& dirs[n - 3].EndsWith(wxT(".app"));
	}

	if (!bundle) {
		EXPECT_TRUE(wxFileName::DirName(ibDiagnosticFolder())
			.SameAs(wxFileName::DirName(ibProgramFolder())));
		return;
	}

	wxFileName logs;
	logs.AssignDir(wxGetHomeDir());
	logs.AppendDir(wxT("Library"));
	logs.AppendDir(wxT("Logs"));
	logs.AppendDir(wxT("OES"));
	EXPECT_TRUE(wxFileName::DirName(ibDiagnosticFolder()).SameAs(logs))
		<< ibDiagnosticFolder().ToStdString();
}
