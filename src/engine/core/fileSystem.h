#ifndef __FILE_SYSTEM_H__
#define __FILE_SYSTEM_H__

#include "core/core.h"
#include "core/fstring.h"

#include <filesystem>

#include <wx/string.h>

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
CORE_API wxString ibProgramFolder(const wxString& executablePath);
CORE_API wxString ibProgramFolder();
CORE_API wxString ibDiagnosticFolder();

// The process's working directory; empty when it cannot be read.
CORE_API wxString ibGetCwd();

// THE PROCESS'S backend.conf — the working directory's, when there is one there, else the one in the program folder;
// empty when neither is. One rule for whoever reads it: the engine's host (the whole file) and the application
// cluster and its tray (the certificate its port speaks TLS over, core/server/listener.h).
CORE_API wxString ibBackendConfPath();

// wx's FILE FUNCTIONS — wxGetCwd above, wxDirExists, wxFileExists, wxRemoveFile, wxRenameFile, wxCopyFile — through
// std::filesystem. A file is a regular file only, true is "done", and a failure is the answer alone: nothing is
// written into the log on the way, as wx did.
CORE_API bool ibDirExists(const ibString& path);
CORE_API bool ibFileExists(const ibString& path);
CORE_API bool ibRemoveFile(const ibString& path);
CORE_API bool ibRenameFile(const ibString& from, const ibString& to);   // replaces what `to` names
CORE_API bool ibCopyFile(const ibString& from, const ibString& to);     // replaces what `to` names

// THE ONE DOOR FROM TEXT TO A FILE SYSTEM PATH — every std::filesystem call takes its path from here, so a name is
// read the same way everywhere: the wide characters as they are on Windows, UTF-8 elsewhere.
CORE_API std::filesystem::path ibPathOf(const ibString& text);

#endif // __FILE_SYSTEM_H__
