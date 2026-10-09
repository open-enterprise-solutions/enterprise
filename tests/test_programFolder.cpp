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

TEST(ProgramFolder, ABundleResolvesBesideTheApp) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/Applications/enterprise.app/Contents/MacOS/enterprise")),
		wxT("/Applications"));
}

TEST(ProgramFolder, ABundleAtTheRootResolvesToTheRoot) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/enterprise.app/Contents/MacOS/enterprise")),
		wxT("/"));
}

TEST(ProgramFolder, AFlatBinaryResolvesToItsDirectory) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/opt/oes/bin/enterprise")),
		wxT("/opt/oes/bin"));
}

TEST(ProgramFolder, MacOSAloneIsNotABundle) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/opt/MacOS/enterprise")),
		wxT("/opt/MacOS"));
}

TEST(ProgramFolder, ContentsWithoutMacOSIsNotABundle) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/opt/oes/Contents/bin/enterprise")),
		wxT("/opt/oes/Contents/bin"));
}

// The bundle names are the ones Apple writes. A different case is a directory
// that happens to be spelled that way, and it is left alone.
TEST(ProgramFolder, TheBundleNamesAreCaseSensitive) {
	EXPECT_EQ(
		ibProgramFolder(wxT("/Applications/enterprise.app/contents/macos/enterprise")),
		wxT("/Applications/enterprise.app/contents/macos"));
}

TEST(ProgramFolder, ThisProcessUsesItsOwnExecutable) {
	const wxString exe = wxStandardPaths::Get().GetExecutablePath();
	EXPECT_FALSE(exe.IsEmpty());
	EXPECT_EQ(ibProgramFolder(), ibProgramFolder(exe));
}
