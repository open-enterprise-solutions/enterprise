////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the thin client's journal
////////////////////////////////////////////////////////////////////////////

#include "journal.h"

#include <atomic>
#include <mutex>

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/thread.h>
#include <wx/utils.h>

namespace {

std::mutex        s_lock;
wxFFile           s_file;
std::atomic<bool> s_open{ false };

} // namespace

void ibClientJournal::Open(const wxString& exeName)
{
#ifndef NDEBUG
	// Beside the engine's: `journal/` next to the binary.
	const wxString exeDir = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
	const wxString dir = exeDir + wxFILE_SEP_PATH + wxT("journal");
	wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

	const wxString stamp = wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S"));
	{
		const std::lock_guard<std::mutex> lock(s_lock);
		if (s_file.IsOpened())
			return;
		s_file.Open(dir + wxFILE_SEP_PATH + wxString::Format(wxT("%s_%s_%u.client.log"), exeName, stamp,
			static_cast<unsigned>(wxGetProcessId())), wxT("w"));
		s_open.store(s_file.IsOpened(), std::memory_order_relaxed);
	}
	if (s_open.load(std::memory_order_relaxed))
		Write(ibClientJournalMark::Info, wxT("start"), wxString::Format(wxT("%s, process %u"), exeName,
			static_cast<unsigned>(wxGetProcessId())));
#else
	(void)exeName;
#endif
}

bool ibClientJournal::IsOpen()
{
	return s_open.load(std::memory_order_relaxed);
}

void ibClientJournal::Write(ibClientJournalMark mark, const wxString& source, const wxString& message)
{
	if (!s_open.load(std::memory_order_relaxed))
		return;

	// THE LINE: kind, time, thread, who speaks, what is said — the engine journal's columns, padded the same, so the
	// two files of one run read alike.
	const wxChar* const kind =
		  mark == ibClientJournalMark::Error   ? wxT("[error]  ")
		: mark == ibClientJournalMark::Warning ? wxT("[warning]")
		                                       : wxT("[info]   ");
	const wxString line = wxString::Format(wxT("%s %s  t%-5lu  %-14s  %s\n"), kind,
		wxDateTime::UNow().Format(wxT("%H:%M:%S.%l")), static_cast<unsigned long>(wxThread::GetCurrentId()), source, message);

	const std::lock_guard<std::mutex> lock(s_lock);
	if (!s_file.IsOpened())
		return;
	s_file.Write(line);
	s_file.Flush();
}
