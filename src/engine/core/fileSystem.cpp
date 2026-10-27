#include "core/fileSystem.h"

#include <chrono>
#include <cstdlib>
#include <vector>

#if defined(__IBMSW__)
#include <windows.h>
#elif defined(__IBOSX__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace {

// .../X.app/Contents/MacOS — both names, and the third directory ends in .app.
// A directory that merely happens to be called MacOS, or Contents/MacOS under
// something that is not a bundle, is left alone.
bool LastDirsAreBundle(const std::filesystem::path& folder)
{
	const std::filesystem::path macOS = folder.filename();
	const std::filesystem::path contents = folder.parent_path().filename();
	const std::filesystem::path bundle = folder.parent_path().parent_path().filename();
	return macOS == "MacOS" && contents == "Contents" && bundle.extension() == ".app";
}

ibString TextOf(const std::filesystem::path& path)
{
	return ibString(path.wstring());
}

// wx's wildcards: `*` any run of characters, `?` any one, the case folded. A `*` that the rest fails to follow is
// let take one more character, and the rest is tried again.
bool MatchesSpec(const std::wstring& name, const ibString& spec)
{
	const size_t specLength = spec.Len();
	size_t n = 0, s = 0, star = std::wstring::npos, resume = 0;
	while (n < name.size()) {
		if (s < specLength && spec[s] == ibT('*')) {
			star = s++;
			resume = n;
		}
		else if (s < specLength && (spec[s] == ibT('?') || ibToLower(spec[s]) == ibToLower(name[n]))) {
			++n;
			++s;
		}
		else if (star != std::wstring::npos) {
			s = star + 1;
			n = ++resume;
		}
		else
			return false;
	}
	while (s < specLength && spec[s] == ibT('*'))
		++s;
	return s == specLength;
}

// Whether the walk lists this entry at all, by its kind — the spec is asked apart.
bool ListsKind(const std::filesystem::directory_entry& entry, int flags)
{
	std::error_code error;
	if (entry.is_directory(error))
		return (flags & ibDIR_DIRS) != 0;
	return (flags & ibDIR_FILES) != 0 && entry.is_regular_file(error);
}

} // namespace

// The folder the binary is IN: the path's parent, never the path itself read as a folder.
ibString ibProgramFolder(const ibString& executablePath)
{
	std::filesystem::path folder = ibPathOf(executablePath).parent_path();
	if (LastDirsAreBundle(folder))
		folder = folder.parent_path().parent_path().parent_path();   // MacOS, Contents, X.app
	return TextOf(folder);
}

ibString ibProgramFolder()
{
	return ibProgramFolder(ibExecutablePath());
}

ibString ibDiagnosticFolder()
{
#if defined(__IBOSX__)
	// An installed bundle's program folder is beside the .app. That is
	// /Applications for a copy a person installed, and a normal user cannot
	// write there — the journal would open, fail, and stay silent. Logs go
	// to the user's Library instead. A binary that is not inside a bundle
	// keeps the program folder, which is the directory of the executable.
	const char* const home = std::getenv("HOME");
	if (home != nullptr && LastDirsAreBundle(ibPathOf(ibExecutablePath()).parent_path()))
		return TextOf(std::filesystem::path(home) / "Library" / "Logs" / "OES");
#endif
	return ibProgramFolder();
}

ibString ibExecutablePath()
{
#if defined(__IBMSW__)
	std::vector<wchar_t> path(MAX_PATH);
	for (;;) {
		const DWORD length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0)
			return ibString();
		if (length < path.size())
			return ibString(path.data(), length);
		path.resize(path.size() * 2);   // cut short: the room was too small
	}
#elif defined(__IBOSX__)
	uint32_t size = 0;
	::_NSGetExecutablePath(nullptr, &size);
	std::vector<char> path(size + 1);
	if (::_NSGetExecutablePath(path.data(), &size) != 0)
		return ibString();
	std::error_code error;
	const std::filesystem::path real = std::filesystem::canonical(path.data(), error);
	return TextOf(error ? std::filesystem::path(path.data()) : real);
#else
	std::error_code error;
	return TextOf(std::filesystem::read_symlink("/proc/self/exe", error));
#endif
}

ibString ibGetCwd()
{
	std::error_code error;
	return ibString(std::filesystem::current_path(error).wstring());
}

ibString ibBackendConfPath()
{
	const ibString working = ibGetCwd() + ibFILE_SEP_PATH + ibT("backend.conf");
	if (ibFileExists(working))
		return working;
	// Inside a bundle the program folder is the one that contains the .app, which is where the file is shipped.
	const ibString beside = ibProgramFolder() + ibFILE_SEP_PATH + ibT("backend.conf");
	return ibFileExists(beside) ? beside : ibString();
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

// The file clock counts from an epoch of its own (1601 on Windows), and C++20's clock_cast is not on every
// library this builds with: the moment crosses as its distance from now, which both clocks agree on.
ibDateTime ibFileModificationTime(const ibString& path)
{
	std::error_code error;
	const std::filesystem::file_time_type written = std::filesystem::last_write_time(ibPathOf(path), error);
	if (error)
		return ibDateTime();
	const auto instant = std::chrono::system_clock::now()
		+ std::chrono::duration_cast<std::chrono::system_clock::duration>(written - std::filesystem::file_time_type::clock::now());
	return ibDateTime::OfInstant(std::chrono::duration_cast<std::chrono::milliseconds>(instant.time_since_epoch()).count());
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

ibDir::ibDir(const ibString& dir)
	: m_dir(ibPathOf(dir))
{
}

bool ibDir::IsOpened() const
{
	std::error_code error;
	return std::filesystem::is_directory(m_dir, error);
}

bool ibDir::GetFirst(ibString* filename, const ibString& filespec, int flags)
{
	m_filespec = filespec;
	m_flags = flags;
	std::error_code error;
	m_walk = std::filesystem::directory_iterator(m_dir, error);
	if (error)
		m_walk = std::filesystem::directory_iterator();
	return GetNext(filename);
}

bool ibDir::GetNext(ibString* filename)
{
	while (m_walk != std::filesystem::directory_iterator()) {
		const std::filesystem::directory_entry entry = *m_walk;
		std::error_code error;
		m_walk.increment(error);
		if (error)   // the folder stopped answering: what was read is the last of it
			m_walk = std::filesystem::directory_iterator();
		const std::wstring name = entry.path().filename().wstring();
		if (ListsKind(entry, m_flags) && MatchesSpec(name, m_filespec)) {
			if (filename != nullptr)
				*filename = ibString(name);
			return true;
		}
	}
	return false;
}

// A folder inside is walked for its files whatever its name: the spec is the files', as wx has it.
size_t ibDir::GetAllFiles(const ibString& dirname, std::vector<ibString>* files, const ibString& filespec, int flags)
{
	size_t added = 0;
	std::error_code error;
	for (std::filesystem::directory_iterator it(ibPathOf(dirname), error), end; !error && it != end; it.increment(error)) {
		std::error_code kind;
		if (it->is_directory(kind)) {
			if ((flags & ibDIR_DIRS) != 0)
				added += GetAllFiles(TextOf(it->path()), files, filespec, flags);
		}
		else if ((flags & ibDIR_FILES) != 0 && it->is_regular_file(kind) && MatchesSpec(it->path().filename().wstring(), filespec)) {
			files->push_back(TextOf(it->path()));
			++added;
		}
	}
	return added;
}

std::filesystem::path ibPathOf(const ibString& text)
{
	return std::filesystem::path(text.c_str(), text.c_str() + text.Len());
}
