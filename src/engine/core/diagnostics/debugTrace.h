#ifndef _DEBUG_TRACE_H__
#define _DEBUG_TRACE_H__

////////////////////////////////////////////////////////////////////////////
// Debug tracing switches + a file sink
////////////////////////////////////////////////////////////////////////////

#include "core/fileSystem.h"
#include "core/fdatetime.h"

#include <cstdlib>
#include <fstream>

// IS THIS TRACE ON? Read once, from the environment, so a build does not have to be repeated to
// answer a question — and so the answer costs one bool test on the hot path rather than a string
// lookup. OFF unless the variable is present and is not "0" / "false"; callers keep it in a
// function-local static (`OES_TRACE_VALUES`, `OES_TRACE_TYPES` — see compiler/value.cpp and
// compiler/valueFactory.cpp, whose counters run either way, only their output is conditional).
inline bool ibDebugTraceEnabled(const char* envVar)
{
	const char* const set = std::getenv(envVar);
	if (set == nullptr)
		return false;

	ibString value = ibString::FromUTF8(set);
	value.Trim(true).Trim(false).MakeLower();
	return !value.IsEmpty() && value != ibT("0") && value != ibT("false") && value != ibT("off");
}

// DIAGNOSTICS THAT SOMEBODY CAN ACTUALLY READ. Writes one line to `oes-debug.log`, appending.
// Inside an installed macOS bundle that file is ~/Library/Logs/OES/oes-debug.log, because the
// folder beside the .app is often /Applications and a normal user cannot write there. Everywhere
// else it is the program folder.
//
// Deliberately NOT wxLogError / wxLogMessage / wxFAIL_MSG / OutputDebugString: the applications
// are normally run WITHOUT a debugger attached, so anything sent to the logger or to the debug
// output is invisible to the person watching the screen — and in a GUI build wxLog even queues
// the message and shows it later, by which time an assert further up the stack has already fired
// and named nobody. A file survives the run and can be read (or sent) afterwards.
//
// Not a general logging facility and not meant to become one — no levels, no categories. Use it
// for the answers a diagnostic pass must deliver (WHICH object, WHICH id, WHICH branch), and
// remove the call when its question is answered.
inline void ibTraceToFile(const ibString& text)
{
	// Inside a macOS bundle the folder is ~/Library/Logs/OES, which nothing else may have made yet.
	const std::filesystem::path folder = ibPathOf(ibDiagnosticFolder());
	std::error_code error;
	std::filesystem::create_directories(folder, error);

	std::ofstream file(folder / "oes-debug.log", std::ios::binary | std::ios::app);
	if (file)
		file << (ibDateTime::Now().Format(ibT("%Y-%m-%d %H:%M:%S")) + ibT("  ") + text + ibT("\n")).ToUtf8();
}

#endif
