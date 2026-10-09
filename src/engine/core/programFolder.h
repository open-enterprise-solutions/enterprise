#ifndef __PROGRAM_FOLDER_H__
#define __PROGRAM_FOLDER_H__

#include "core/core.h"

#include <wx/string.h>

// The folder that holds this program's libraries and the files the programs share.
//
// A macOS application bundle keeps the binary at X.app/Contents/MacOS/<name>.
// Everything that is not the binary — backend.conf, lang/, help/, plugins/,
// _fb/, journal/, crashdumps/, a sibling .app, libfileserver.dylib — stands
// NEXT TO the bundle, in the same folder a Windows or Linux build keeps next
// to the executable. Looking beside the binary finds Contents/MacOS/ and misses
// all of it. This is that one walk, so a new file does not grow another copy.
//
// Off a bundle (Windows, Linux, and a macOS binary that is not inside
// Contents/MacOS) the folder is the directory of the executable.
//
// `executablePath` is a full path of a binary. The no-argument form asks for
// this process.
CORE_API wxString ibProgramFolder(const wxString& executablePath);
CORE_API wxString ibProgramFolder();

#endif // __PROGRAM_FOLDER_H__
