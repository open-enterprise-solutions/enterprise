// =============================================================================
// OES Enterprise — the program folder
//
// One rule for "where the libraries and the shared files are". Inside a macOS
// bundle the binary is X.app/Contents/MacOS/<name> and that folder is the
// directory containing X.app. Everywhere else it is the binary's own directory.
// These paths are synthetic: the walk does not ask the operating system, so
// Linux runs the bundle cases too.
// =============================================================================

#include <gtest/gtest.h>

#include "core/programFolder.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

// wxFileName writes the platform separator, so a synthetic "/" path comes
// back with "\" on Windows. The walk is what these tests pin; the separator
// is the platform's.
static wxString WithSlashes(wxString path)
{
	path.Replace(wxT("\\"), wxT("/"));
	return path;
}

TEST(ProgramFolder, ABundleResolvesBesideTheApp) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/Applications/enterprise.app/Contents/MacOS/enterprise"))),
		wxString(wxT("/Applications")));
}

TEST(ProgramFolder, ABundleAtTheRootResolvesToTheRoot) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/enterprise.app/Contents/MacOS/enterprise"))),
		wxString(wxT("/")));
}

TEST(ProgramFolder, AFlatBinaryResolvesToItsDirectory) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/opt/oes/bin/enterprise"))),
		wxString(wxT("/opt/oes/bin")));
}

TEST(ProgramFolder, MacOSAloneIsNotABundle) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/opt/MacOS/enterprise"))),
		wxString(wxT("/opt/MacOS")));
}

TEST(ProgramFolder, ContentsWithoutMacOSIsNotABundle) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/opt/oes/Contents/bin/enterprise"))),
		wxString(wxT("/opt/oes/Contents/bin")));
}

// The bundle names are the ones Apple writes. A different case is a directory
// that happens to be spelled that way, and it is left alone.
TEST(ProgramFolder, TheBundleNamesAreCaseSensitive) {
	EXPECT_EQ(
		WithSlashes(ibProgramFolder(wxT("/Applications/enterprise.app/contents/macos/enterprise"))),
		wxString(wxT("/Applications/enterprise.app/contents/macos")));
}

TEST(ProgramFolder, ThisProcessUsesItsOwnExecutable) {
	const wxString exe = wxStandardPaths::Get().GetExecutablePath();
	EXPECT_FALSE(exe.IsEmpty());
	EXPECT_EQ(ibProgramFolder(), ibProgramFolder(exe));
}
