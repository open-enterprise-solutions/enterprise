#include "core/fileSystem.h"

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

wxString ibGetCwd()
{
	std::error_code error;
	return wxString(std::filesystem::current_path(error).wstring());
}

wxString ibBackendConfPath()
{
	const wxString working = ibGetCwd() + wxFILE_SEP_PATH + wxT("backend.conf");
	if (ibFileExists(working))
		return working;
	// Inside a bundle the program folder is the one that contains the .app, which is where the file is shipped.
	const wxString beside = ibProgramFolder() + wxFILE_SEP_PATH + wxT("backend.conf");
	return ibFileExists(beside) ? beside : wxString();
}

bool ibDirExists(const ibString& path)
{
	std::error_code error;
	return std::filesystem::is_directory(ibPathOf(path), error);
}

bool ibFileExists(const ibString& path)
{
	std::error_code error;
	return std::filesystem::is_regular_file(ibPathOf(path), error);
}

// A FILE: remove() alone would take an empty folder too.
bool ibRemoveFile(const ibString& path)
{
	const std::filesystem::path file = ibPathOf(path);
	std::error_code error;
	return std::filesystem::is_regular_file(file, error) && std::filesystem::remove(file, error);
}

bool ibRenameFile(const ibString& from, const ibString& to)
{
	const std::filesystem::path source = ibPathOf(from);
	const std::filesystem::path target = ibPathOf(to);
	std::error_code error;
	std::filesystem::rename(source, target, error);
	if (!error)
		return true;
	// Another volume: a rename cannot cross it, so the file is copied and the original removed — as wx did.
	if (!std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error))
		return false;
	std::filesystem::remove(source, error);
	return true;
}

bool ibCopyFile(const ibString& from, const ibString& to)
{
	std::error_code error;
	return std::filesystem::copy_file(ibPathOf(from), ibPathOf(to), std::filesystem::copy_options::overwrite_existing, error);
}

std::filesystem::path ibPathOf(const ibString& text)
{
	return std::filesystem::path(text.wc_str(), text.wc_str() + text.Len());
}
