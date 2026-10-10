#ifndef __PROGRAM_FOLDER_H__
#define __PROGRAM_FOLDER_H__

#include "core/core.h"

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

#endif // __PROGRAM_FOLDER_H__
