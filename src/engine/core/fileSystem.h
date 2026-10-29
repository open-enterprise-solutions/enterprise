#ifndef __FILE_SYSTEM_H__
#define __FILE_SYSTEM_H__

#include "core/core.h"
#include "core/fdatetime.h"
#include "core/fstring.h"

#include <filesystem>
#include <vector>

// The separator a path is written with here — what wx calls wxFILE_SEP_PATH.
constexpr ibChar ibFILE_SEP_PATH = static_cast<ibChar>(std::filesystem::path::preferred_separator);

// The folder that holds this program's libraries and the files the programs share.
//
// A macOS application bundle keeps the binary at X.app/Contents/MacOS/<name>.
// Everything that is not the binary — backend.conf, lang/, help/, plugins/,
// _fb/, a sibling .app, libfileserver.dylib, the application server's
// server/ — stands NEXT TO the bundle, in the same folder a Windows or Linux
// build keeps next to the executable. Looking beside the binary finds
// Contents/MacOS/ and misses all of it. This is that one walk, so a new file
// does not grow another copy. The third directory has to end in .app: a path
// that merely passes through Contents/MacOS is left alone.
//
// Off a bundle (Windows, Linux, and a macOS binary that is not inside
// X.app/Contents/MacOS) the folder is the directory of the executable.
//
// The journal, the crash dumps and oes-debug.log are not that folder when the
// binary is an installed bundle. ~/Library/Logs/OES is writable; /Applications
// is not. ibDiagnosticFolder() is that choice. Everywhere else it is the
// program folder, so Windows and Linux do not move.
//
// `executablePath` is a full path of a binary. The no-argument form asks for
// this process.
CORE_API ibString ibProgramFolder(const ibString& executablePath);
CORE_API ibString ibProgramFolder();
CORE_API ibString ibDiagnosticFolder();

// This process's binary, its full path — the one the no-argument ibProgramFolder walks from.
CORE_API ibString ibExecutablePath();
// …and the binary of the program `name` beside this one, in the program folder: the plain file (name.exe on Windows),
// else, on macOS, the one inside its bundle, name.app/Contents/MacOS/name. Neither there — the bare name, left to the
// system's search. ONE LOOKUP for whoever starts a sibling: the launcher, the engine's RunApplication, the client's.
CORE_API ibString ibExecutablePath(const ibString& name);

// The process's working directory; empty when it cannot be read.
CORE_API ibString ibGetCwd();

// THE PROCESS'S backend.conf — the working directory's, when there is one there, else the one in the program folder;
// empty when neither is. One rule for whoever reads it: the engine's host (the whole file) and the application
// cluster and its tray (the certificate its port speaks TLS over, core/server/listener.h).
CORE_API ibString ibBackendConfPath();

// wx's FILE FUNCTIONS — wxGetCwd above, wxDirExists, wxFileExists, wxRemoveFile, wxRenameFile, wxCopyFile — through
// std::filesystem. A file is a regular file only, true is "done", and a failure is the answer alone: nothing is
// written into the log on the way, as wx did.
CORE_API bool ibDirExists(const ibString& path);
CORE_API bool ibFileExists(const ibString& path);
// …and when it was last written, as this machine's clock reads it (wxFileName::GetModificationTime); the empty
// date when there is no file to ask.
CORE_API ibDateTime ibFileModificationTime(const ibString& path);
CORE_API bool ibRemoveFile(const ibString& path);
CORE_API bool ibRenameFile(const ibString& from, const ibString& to);   // replaces what `to` names
CORE_API bool ibCopyFile(const ibString& from, const ibString& to);     // replaces what `to` names

// What a walk of a folder lists — wxDir's flags.
enum {
	ibDIR_FILES   = 0x0001,   // the files
	ibDIR_DIRS    = 0x0002,   // the folders — and, to GetAllFiles, the files inside them
	ibDIR_DEFAULT = ibDIR_FILES | ibDIR_DIRS
};

// wxDir THROUGH std::filesystem — a folder's names one at a time: GetFirst the first that matches, GetNext each one
// after it, bare, without the folder. The spec is wx's: `*` any run of characters, `?` any one, the case folded; an
// empty one matches every name. A hidden name is listed like any other.
class CORE_API ibDir {
public:
	explicit ibDir(const ibString& dir);

	bool IsOpened() const;

	bool GetFirst(ibString* filename, const ibString& filespec = ibEmptyString, int flags = ibDIR_DEFAULT);
	bool GetNext(ibString* filename);

	// The full paths of the files a folder holds that match the spec, added to `files`; how many were added.
	static size_t GetAllFiles(const ibString& dirname, std::vector<ibString>* files,
		const ibString& filespec = ibEmptyString, int flags = ibDIR_DEFAULT);

private:
	std::filesystem::path               m_dir;
	std::filesystem::directory_iterator m_walk;   // the next name to look at; the end when the walk is over
	ibString                            m_filespec;
	int                                 m_flags = ibDIR_DEFAULT;
};

// THE ONE DOOR FROM TEXT TO A FILE SYSTEM PATH — every std::filesystem call takes its path from here, so a name is
// read the same way everywhere: the wide characters as they are on Windows, UTF-8 elsewhere.
CORE_API std::filesystem::path ibPathOf(const ibString& text);

#endif // __FILE_SYSTEM_H__
