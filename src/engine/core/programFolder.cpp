#include "core/programFolder.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

// ⚠ DirName, NOT wxFileName(path). A path with no trailing separator is read as a FILE: the last
// component becomes the name, and RemoveLastDir then strips the directory above the one that was
// meant. That is how the launcher looked for designer.app two levels too high (2026-09-22).
wxString ibProgramFolder(const wxString& executablePath)
{
	const wxFileName executable(executablePath);
	wxFileName folder = wxFileName::DirName(executable.GetPath());

	// .../X.app/Contents/MacOS/<binary> → the directory that contains X.app.
	// Both names, so a directory that merely happens to be called MacOS is left alone.
	const wxArrayString& dirs = folder.GetDirs();
	if (dirs.GetCount() >= 3
		&& dirs[dirs.GetCount() - 1].IsSameAs(wxT("MacOS"))
		&& dirs[dirs.GetCount() - 2].IsSameAs(wxT("Contents"))) {
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
