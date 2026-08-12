#ifndef _DEBUG_TRACE_H__
#define _DEBUG_TRACE_H__

////////////////////////////////////////////////////////////////////////////
// Debug tracing switches + a file sink
////////////////////////////////////////////////////////////////////////////

#include <wx/string.h>
#include <wx/utils.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <wx/datetime.h>

// IS THIS TRACE ON? Read once, from the environment, so a build does not have to be repeated to
// answer a question — and so the answer costs one bool test on the hot path rather than a string
// lookup. OFF unless the variable is present and is not "0" / "false"; callers keep it in a
// function-local static (`OES_TRACE_VALUES`, `OES_TRACE_TYPES` — see compiler/value.cpp and
// compiler/valueFactory.cpp, whose counters run either way, only their output is conditional).
inline bool ibDebugTraceEnabled(const char* envVar)
{
	wxString value;
	if (!wxGetEnv(wxString::FromUTF8(envVar), &value))
		return false;

	value.Trim(true).Trim(false).MakeLower();
	return !value.IsEmpty() && value != wxT("0") && value != wxT("false") && value != wxT("off");
}

// DIAGNOSTICS THAT SOMEBODY CAN ACTUALLY READ. Writes one line to `oes-debug.log` next to the
// executable, appending.
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
inline void ibTraceToFile(const wxString& text)
{
	wxFileName traceFile(wxStandardPaths::Get().GetExecutablePath());
	traceFile.SetFullName(wxT("oes-debug.log"));

	const wxString path = traceFile.GetFullPath();

	wxFile file;
	const bool opened = wxFileName::FileExists(path)
		? file.Open(path, wxFile::write_append)
		: file.Create(path, /*overwrite*/ false);
	if (!opened)
		return;

	file.Write(wxDateTime::Now().FormatISOCombined(wxT(' ')) + wxT("  ") + text + wxT("\n"));
}

#endif
