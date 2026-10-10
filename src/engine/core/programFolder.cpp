#include "core/programFolder.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

namespace {

// .../X.app/Contents/MacOS — both names, and the third directory ends in .app.
// A directory that merely happens to be called MacOS, or Contents/MacOS under
// something that is not a bundle, is left alone.
bool LastDirsAreBundle(const wxFileName& folder)
{
	const wxArrayString& dirs = folder.GetDirs();
	if (dirs.GetCount() < 3)
		return false;
	const size_t n = dirs.GetCount();
	return dirs[n - 1].IsSameAs(wxT("MacOS"))
		&& dirs[n - 2].IsSameAs(wxT("Contents"))
		&& dirs[n - 3].EndsWith(wxT(".app"));
}

} // namespace

// ⚠ DirName, NOT wxFileName(path). A path with no trailing separator is read as a FILE: the last
// component becomes the name, and RemoveLastDir then strips the directory above the one that was
// meant. That is how the launcher looked for designer.app two levels too high (2026-09-22).
wxString ibProgramFolder(const wxString& executablePath)
{
	const wxFileName executable(executablePath);
	wxFileName folder = wxFileName::DirName(executable.GetPath());
	if (LastDirsAreBundle(folder)) {
		folder.RemoveLastDir(); // MacOS
		folder.RemoveLastDir(); // Contents
		folder.RemoveLastDir(); // X.app
	}
	return folder.GetPath();
}

wxString ibProgramFolder()
{
	return ibProgramFolder(wxStandardPaths::Get().GetExecutablePath());
}

wxString ibDiagnosticFolder()
{
#if defined(__WXOSX__)
	// An installed bundle's program folder is beside the .app. That is
	// /Applications for a copy a person installed, and a normal user cannot
	// write there — the journal would open, fail, and stay silent. Logs go
	// to the user's Library instead. A binary that is not inside a bundle
	// keeps the program folder, which is the directory of the executable.
	const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
	if (LastDirsAreBundle(wxFileName::DirName(executable.GetPath()))) {
		wxFileName logs;
		logs.AssignDir(wxGetHomeDir());
		logs.AppendDir(wxT("Library"));
		logs.AppendDir(wxT("Logs"));
		logs.AppendDir(wxT("OES"));
		return logs.GetPath();
	}
#endif
	return ibProgramFolder();
}
