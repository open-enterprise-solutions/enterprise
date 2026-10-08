////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the protocol's journal
////////////////////////////////////////////////////////////////////////////

#include "journal.h"

#include <mutex>

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/utils.h>

namespace {

std::mutex     s_lock;
wxFFile        s_file;
std::once_flag s_opened;

// THE RUN'S FILE — opened by the first line written, named after the program the library is loaded into: the
// process's own executable, so no program has to say who it is. Beside the GUI's and the engine's: `journal/` next to
// the binary. A machine with no writable directory there still runs: every write is then nothing.
void OpenFile()
{
	const wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
	const wxString dir = exe.GetPath() + wxFILE_SEP_PATH + wxT("journal");
	wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

	const unsigned pid = static_cast<unsigned>(wxGetProcessId());
	const wxString stamp = wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S"));
	s_file.Open(dir + wxFILE_SEP_PATH + wxString::Format(wxT("%s_%s_%u.protocol.log"), exe.GetName(), stamp, pid), wxT("w"));
}

} // namespace

void ibProtocolJournal::Write(ibProtocolJournalMark mark, const wxString& source, const wxString& message)
{
	// THE LINE: kind, time, thread, who speaks, what is said — the engine journal's columns, padded the same, so the
	// files of one run read alike.
	const wxChar* const kind =
		  mark == ibProtocolJournalMark::Error   ? wxT("[error]  ")
		: mark == ibProtocolJournalMark::Warning ? wxT("[warning]")
		                                         : wxT("[info]   ");
	const wxString line = wxString::Format(wxT("%s %s  t%-5lu  %-14s  %s\n"), kind,
		wxDateTime::UNow().Format(wxT("%H:%M:%S.%l")), static_cast<unsigned long>(wxThread::GetCurrentId()), source, message);

	const std::lock_guard<std::mutex> lock(s_lock);
	std::call_once(s_opened, OpenFile);
	if (!s_file.IsOpened())
		return;
	s_file.Write(line);
	s_file.Flush();
}
