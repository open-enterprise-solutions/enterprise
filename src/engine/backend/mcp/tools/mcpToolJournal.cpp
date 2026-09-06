////////////////////////////////////////////////////////////////////////////
//	Description : the journal tool - what this installation actually did
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ TWO JOURNALS, AND THEY ANSWER TO DIFFERENT PEOPLE. Getting them confused costs an hour, so
// they are named for their READER rather than for their format:
//
//   journal_read — THE ACCOUNTANT'S journal (`ibLogger`, the registration log). What the
//     INSTALLATION did: who logged in, which document was written and posted, which schema was
//     applied — the record an auditor asks for. Business events, in business words.
//
//   trace_read — THE ENGINE'S journal (`ibJournal`, one plain-text file per process run). What the
//     PLATFORM did: the SQL as sent, which road a query took, which keys a stitch joined on,
//     exceptions with their module and line. Nobody audits this; it is read while something is
//     being diagnosed.
//
// 🛑 The line above used to say the engine's journal "belongs to the platform's own developer and
// stays there" — and that made it unreachable from the one place where diagnosis happens over this
// door. An assistant asked `journal_read` about a query, got the accountant's rows, and concluded
// the engine journals nothing about queries; the file had every statement in it (2026-09-04).
//
// What belongs in the accountant's journal is what the installation DID — and, since the
// diagnostics bus is journalled, every script that failed while running.
//
// WHY THAT LAST ONE MATTERS. A module that compiles clean can still fail at
// execution: the compiler checks that a call parses, not that the object allows
// it or takes that many arguments. The failure then happens in ANOTHER PROCESS,
// where nothing that wrote the module can see it. Journalled, it comes back -
// with its module, its line, the offending text and the call stack.
//
// So the loop closes without anyone reading a screen out loud: write the module,
// ask for it to be run, then ask the journal what happened.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/diagnostics/journal.h"
#include "backend/logger/logger.h"
#include "backend/logger/loggerReader.h"

#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>

#include <algorithm>
#include <map>
#include <vector>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments, declared once and read through the same objects — see ibMcpTool::Arguments().
const ibArg& ArgSinceMinutes() { static const ibArg a(wxT("sinceMinutes"), ibArg::Kind::Whole, ibMcpText("Only what happened in the last N minutes. This is usually the one to pass.")); return a; }
const ibArg& ArgLevel() { static const ibArg a(wxT("level"), ibArg::Kind::Text, ibMcpText("Which rows to report, and the four words are not one scale. info, warn and error step up a SEVERITY ladder, so error means failures and nothing else. audit is not a severity at all - it is a KIND, the business trail: logins, document writes, schema applications, what an assistant did. Ask for it BY NAME and you get that trail alone; it never arrives mixed into a severity you asked for.")); return a; }
const ibArg& ArgSource() { static const ibArg a(wxT("source"), ibArg::Kind::Text, ibMcpText("Only this subsystem. script = a running module, auth = logins, and so on.")); return a; }
const ibArg& ArgEvent() { static const ibArg a(wxT("event"), ibArg::Kind::Text, ibMcpText("Only this event type, for example runtime.error.")); return a; }
const ibArg& ArgUser() { static const ibArg a(wxT("user"), ibArg::Kind::Text, ibMcpText("Only this user's rows.")); return a; }
const ibArg& ArgSession() { static const ibArg a(wxT("session"), ibArg::Kind::Text,
	ibMcpText("Only ONE SESSION's rows, by the id in `session`. This is how you watch a background "
		  "run: code_run answers with the session it runs as, and a background session cannot "
		  "send anybody a message - it is tied to no one - so its rows here are the only account "
		  "of what it is doing. Ask again as it goes.")); return a; }
const ibArg& ArgContains() { static const ibArg a(wxT("contains"), ibArg::Kind::Text, ibMcpText("Only rows whose message contains this text - a module name, a field, a phrase.")); return a; }
const ibArg& ArgLimit() { static const ibArg a(wxT("limit"), ibArg::Kind::Whole, ibMcpText("How many rows at most. Default 50.")); return a; }

// Levels are stored as numbers and asked for as words. The word is the unit a
// caller thinks in - "show me the errors" - and the number is the file's.
wxString LevelName(int level)
{
	switch (level) {
		case 0: return wxT("info");
		case 1: return wxT("warn");
		case 2: return wxT("error");
		case 3: return wxT("audit");
	}
	return wxT("info");
}

int LevelFromWord(const wxString& word)
{
	const wxString lowered = word.Lower();
	if (lowered == wxT("info"))  return 0;
	if (lowered == wxT("warn"))  return 1;
	if (lowered == wxT("error")) return 2;
	if (lowered == wxT("audit")) return 3;
	return -1;      // any
}

//---------------------------------------------------------------------------
// The engine's journal — plain text, one file per PROCESS RUN
//---------------------------------------------------------------------------
//
// ⭐⭐ TWO DIRECTIONS AT ONCE, AND THAT IS THE WHOLE REASON THIS READS FILES. The tool answers from
// inside the DESIGNER, but the interesting run is usually the one the designer STARTED — the
// application under the debugger, a separate process writing a separate file. Asking the live
// `ibTechJournal` would therefore answer about the wrong half of the story every time. So the
// newest file of EACH application is read off the disk and every row carries the app it came from:
// one question, both sides, in one ordering.
//
// ⚠ AND IT ONLY EXISTS IN A DEBUG BUILD (crashGuard.cpp: `#ifndef NDEBUG` around Open). A Release
// binary writes no file at all, which is a different answer from "nothing matched" and is reported
// as one.

const ibArg& ArgTraceApp() { static const ibArg a(wxT("app"), ibArg::Kind::Text, ibMcpText("Only this application's run: designer, enterprise, daemon, codeRunner. Leave it out to read every application at once - that is usually what is wanted, because the run being debugged is a different process from this one.")); return a; }
const ibArg& ArgTraceSource() { static const ibArg a(wxT("source"), ibArg::Kind::Text, ibMcpText("Only this subsystem, matched by PREFIX: query catches query.sql, query.road, query.compose and query.stitch; db.firebird is the driver, exception is a raise with its site.")); return a; }
const ibArg& ArgTraceLevel() { static const ibArg a(wxT("level"), ibArg::Kind::Text, ibMcpText("Lowest severity to report: info, warning, error. The engine's journal has no audit level - that one belongs to journal_read.")); return a; }

int TraceLevelFromWord(const wxString& word)
{
	const wxString lowered = word.Lower();
	if (lowered == wxT("info"))    return 0;
	if (lowered == wxT("warn") || lowered == wxT("warning")) return 1;
	if (lowered == wxT("error"))   return 2;
	return -1;
}

int TraceLevelOfName(const wxString& name)
{
	const int level = TraceLevelFromWord(name);
	return level < 0 ? 0 : level;
}

// One parsed line. The app is not IN the file - it is the file's own name - and it is carried on
// every row because the whole point of the tool is that two processes are being read at once.
struct ibTraceRow {
	wxDateTime m_when;
	wxString   m_app;
	wxString   m_level;
	wxString   m_thread;
	wxString   m_source;
	wxString   m_message;
};

// Where the files are. `Path()` is authoritative when this process journals - the directory is a
// DECISION (beside the crash dumps) and reading it off the live path means the two can never drift
// apart. Only when there is no journal here does the convention have to be spelled out again.
wxString TraceDirectory()
{
	const wxString own = ibTechJournal::Path();
	if (!own.IsEmpty())
		return wxFileName(own).GetPath();

	return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()
		+ wxFILE_SEP_PATH + wxT("journal");
}

// `<app>_<yyyymmddThhmmss>_<pid>.log` — the app is the head, the start time is the middle.
wxString TraceAppOfFile(const wxString& fileName) { return fileName.BeforeFirst(wxT('_')); }

// ⭐ THE FILE'S NAME CARRIES THE DATE THE LINES DO NOT. A line is stamped `03:01:57.450` and no
// further — time of day is what a person compares — so the day comes from the start stamp, and a
// run that crosses midnight is caught by the clock going BACKWARDS between two lines.
wxDateTime TraceStartOfFile(const wxFileName& file)
{
	const wxString middle = file.GetName().AfterFirst(wxT('_')).BeforeLast(wxT('_'));

	wxDateTime stamp;
	if (!middle.IsEmpty() && stamp.ParseFormat(middle, wxT("%Y%m%dT%H%M%S")))
		return stamp;

	// A name that does not follow the convention still has a file behind it, and its last write is
	// a better day than no day at all.
	return file.FileExists() ? file.GetModificationTime() : wxDateTime::Now();
}

// The newest file of each application. ⚠ ONE wxDir DRIVES ONE TRAVERSAL - nothing here may open a
// second walk inside this loop.
std::map<wxString, wxFileName> TraceNewestPerApp(const wxString& directory)
{
	std::map<wxString, wxFileName> newest;
	std::map<wxString, wxDateTime> newestTime;

	wxDir dir(directory);
	if (!dir.IsOpened())
		return newest;

	wxString name;
	bool walking = dir.GetFirst(&name, wxT("*.log"), wxDIR_FILES);
	while (walking) {

		const wxFileName file(directory, name);
		if (file.FileExists()) {

			const wxString app = TraceAppOfFile(name);
			const wxDateTime when = file.GetModificationTime();

			const auto it = newestTime.find(app);
			if (it == newestTime.end() || when.IsLaterThan(it->second)) {
				newest[app]     = file;
				newestTime[app] = when;
			}
		}
		walking = dir.GetNext(&name);
	}
	return newest;
}

// ⭐⭐ WHY A JOURNAL STOPS MID-SENTENCE. A run that crashed leaves a file that simply ENDS, and
// read on its own that is indistinguishable from a run that is still going — which is the worse
// reading, because it is the one that makes a caller wait. The answer is already on disk: the
// crash guard writes its dump BESIDE the journal (`crashdumps/` next to `journal/`, one decision,
// see crashGuard.cpp), and both file names carry the pid. So the two halves of one incident are
// joined by the thing they already agree on, and nothing new has to be recorded to say "it died".
wxString TracePidOfLog(const wxFileName& file) { return file.GetName().AfterLast(wxT('_')); }

// `<exe>_[<kind>]<pid>_t<tid>_<stamp>_<seq>.dmp` — the pid is the digits ending the token before
// the thread's, which is where an optional kind suffix ("_error") leaves it.
wxString TracePidOfDump(const wxString& fileName)
{
	wxArrayString parts = wxStringTokenize(wxFileName(fileName).GetName(), wxT("_"));

	for (size_t i = 1; i < parts.GetCount(); ++i) {

		const wxString& token = parts[i];
		if (token.length() < 2 || token[0] != wxT('t') || !token.Mid(1).IsNumber())
			continue;

		wxString digits;
		const wxString& owner = parts[i - 1];
		for (size_t c = owner.length(); c > 0 && wxIsdigit(owner[c - 1]); --c)
			digits.Prepend(owner[c - 1]);

		return digits;
	}
	return wxEmptyString;
}

std::map<wxString, wxString> TraceDumpsByPid(const wxString& journalDirectory)
{
	std::map<wxString, wxString> dumps;

	const wxString directory = wxFileName(journalDirectory).GetPath()
		+ wxFILE_SEP_PATH + wxT("crashdumps");

	wxDir dir(directory);
	if (!dir.IsOpened())
		return dumps;

	wxString name;
	bool walking = dir.GetFirst(&name, wxT("*.dmp"), wxDIR_FILES);
	while (walking) {

		const wxString pid = TracePidOfDump(name);
		if (!pid.IsEmpty())
			dumps[pid] = name;      // one per pid; a second fault in one run is the same incident

		walking = dir.GetNext(&name);
	}
	return dumps;
}

// The tail of a file, as text. A journal grows without bound while a process runs, and the answer
// is always about the END of it - so a very large file is read from its last megabytes rather than
// refused or, worse, loaded whole.
wxString TraceReadTail(const wxFileName& file, wxFileOffset maxBytes)
{
	wxFFile handle(file.GetFullPath(), wxT("rb"));
	if (!handle.IsOpened())
		return wxEmptyString;

	const wxFileOffset length = handle.Length();
	const wxFileOffset from   = (length > maxBytes) ? (length - maxBytes) : 0;
	if (from > 0 && !handle.Seek(from))
		return wxEmptyString;

	const size_t want = static_cast<size_t>(length - from);
	std::vector<char> bytes(want + 1, '\0');

	const size_t got = handle.Read(bytes.data(), want);
	wxString text = wxString::FromUTF8(bytes.data(), got);

	// A tail cuts a line in half. That half would parse as a continuation and glue itself onto
	// nothing, so it goes.
	if (from > 0) {
		const int firstBreak = text.Find(wxT('\n'));
		text = (firstBreak == wxNOT_FOUND) ? wxString() : text.Mid(firstBreak + 1);
	}
	return text;
}

// `[info]    03:01:57.450  t11724  query.sql       SELECT ...` — and anything NOT starting a
// bracket belongs to the line above it, which is how a call stack stays one entry.
void TraceParseInto(const wxFileName& file, const wxString& app, std::vector<ibTraceRow>& rows)
{
	const wxString text = TraceReadTail(file, 16 * 1024 * 1024);
	if (text.IsEmpty())
		return;

	wxDateTime day = TraceStartOfFile(file);
	day.ResetTime();

	wxDateTime previous = wxDefaultDateTime;
	const size_t firstRow = rows.size();

	wxStringTokenizer lines(text, wxT("\n"), wxTOKEN_RET_EMPTY);
	while (lines.HasMoreTokens()) {

		wxString line = lines.GetNextToken();
		if (line.EndsWith(wxT("\r")))
			line.RemoveLast();

		if (!line.StartsWith(wxT("["))) {
			if (rows.size() > firstRow && !line.IsEmpty())
				rows.back().m_message += wxT("\n") + line;
			continue;
		}

		const int close = line.Find(wxT(']'));
		if (close == wxNOT_FOUND)
			continue;

		ibTraceRow row;
		row.m_app   = app;
		row.m_level = line.Mid(1, close - 1).Trim().Trim(false);

		wxString rest = line.Mid(close + 1).Trim(false);
		const wxString clock = rest.BeforeFirst(wxT(' '));
		rest = rest.AfterFirst(wxT(' ')).Trim(false);
		row.m_thread = rest.BeforeFirst(wxT(' '));
		rest = rest.AfterFirst(wxT(' ')).Trim(false);
		row.m_source = rest.BeforeFirst(wxT(' '));
		row.m_message = rest.AfterFirst(wxT(' ')).Trim(false);

		// `HH:MM:SS.mmm`, read by position rather than through ParseFormat: the millisecond field
		// is the one part of it whose format specifier is not portable, and it is the part worth
		// having - ordering two events and measuring the gap is most of what this file is for.
		long hour = 0, minute = 0, second = 0, milli = 0;
		const bool clockRead = clock.length() >= 8
			&& clock.Mid(0, 2).ToLong(&hour)
			&& clock.Mid(3, 2).ToLong(&minute)
			&& clock.Mid(6, 2).ToLong(&second);

		if (clockRead && clock.length() >= 12)
			clock.Mid(9, 3).ToLong(&milli);

		if (clockRead) {
			wxDateTime when = day + wxTimeSpan(hour, minute, second, milli);

			// Midnight. The clock only ever moves forward inside one run, so a step backwards is
			// the day turning over - not a line out of order.
			if (previous.IsValid() && when.IsEarlierThan(previous)) {
				day += wxTimeSpan::Day();
				when += wxTimeSpan::Day();
			}
			previous  = when;
			row.m_when = when;
		}
		else {
			row.m_when = previous.IsValid() ? previous : day;
		}

		rows.push_back(row);
	}
}

} // namespace

//---------------------------------------------------------------------------
// journal_read
//---------------------------------------------------------------------------
class ibMcpToolJournalRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("journal_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ArgLevel().Text(params) == wxT("error")
			? wxString(ibMcpText("looking for errors in the journal"))
			: wxString(ibMcpText("reading the journal"));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The registration journal - what this installation did, newest first: logins, "
			"document writes, schema applications, and every script that FAILED WHILE RUNNING "
			"with its module, line and call stack. Ask this after asking someone to run "
			"something: a module that compiled cleanly reports its runtime failure here and "
			"nowhere else this side of the debugger.\n\n"
			"ASK IT BEFORE ANYBODY COMPLAINS. Every runtime failure lands here as it happens, so "
			"`level: error` over the last hour tells you what went wrong in this base while nobody "
			"was looking - which document would not post, which module raised, at what line. "
			"Knowing that before the person says 'something is broken' turns the conversation from "
			"a description into a diagnosis. It is the cheapest call there is: it costs one query "
			"and needs nothing running.\n\n"
			"IT IS FOR SEARCHING, and that is the whole shape of it: a journal is not read, it is "
			"FILTERED. `session` for one background run's own lines - code_run answers with the id "
			"and this is how you watch what it is doing, since a background session can message "
			"nobody. `level: error` for failures alone. `contains` or `event` for a MARKER somebody "
			"put there on purpose. Narrow first, then read.\n\n"
			"AND THE OTHER HALF OF IT IS `WriteJournalEvent`, the global procedure configuration "
			"code calls to put a line here (syntax_get 'fn.WriteJournalEvent'). That is the pair: "
			"code writes a marked line as it goes, this verb pulls the marked lines back out. Code "
			"that runs unattended and writes nothing here cannot be followed at all.\n\n"
			"This is the ACCOUNTANT'S journal - business events, the record an auditor asks for. "
			"For what the ENGINE did - the SQL exactly as sent, the road a query took, driver "
			"traffic, a raise with its site - ask trace_read instead.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgSinceMinutes(), ArgLevel(), ArgSource(), ArgEvent(),
			ArgUser(), ArgSession(), ArgContains(), ArgLimit() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (appData == nullptr || appData->GetLogger() == nullptr) {
			refusal = ibMcpText("The logger is not running - there is no journal to read.");
			return false;
		}

		ibLogFilter filter;

		const s32 sinceMinutes = (s32)ArgSinceMinutes().Whole(params);
		if (sinceMinutes > 0) {
			const wxLongLong_t now = wxDateTime::UNow().GetValue().GetValue();
			filter.from_ms = now - (wxLongLong_t)sinceMinutes * 60000;
		}

		const wxString level = ArgLevel().Text(params);
		if (!level.IsEmpty()) {
			filter.min_level = LevelFromWord(level);

			// An unrecognised word would silently widen the answer to everything,
			// which reads as "nothing was filtered" rather than "that is not a level".
			if (filter.min_level < 0) {
				refusal = ibMcpText("Unknown level. Use info, warn, error or audit.");
				return false;
			}
		}

		filter.user_name  = ArgUser().Text(params);
		filter.session_id = ArgSession().Text(params);
		filter.source     = ArgSource().Text(params);
		filter.event_type = ArgEvent().Text(params);
		filter.search     = ArgContains().Text(params);

		const s32 limit = (s32)ArgLimit().Whole(params);
		filter.limit = (limit > 0) ? (std::size_t)limit : 50;

		ibLoggerReader reader(appData->GetLogger()->GetLogDir());

		std::vector<ibDataValue> rows;
		for (const ibLogRow& row : reader.Query(filter)) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			const wxDateTime stamp((wxLongLong)row.ts_ms);
			entry->SetValue(wxT("time"), stamp.Format(ibMcpText("%Y-%m-%d %H:%M:%S")));

			// The level goes back as the WORD it was asked for by, never the number
			// it is stored as - a reader should not have to hold the mapping.
			entry->SetValue(wxT("level"), LevelName(row.level));

			if (!row.user_name.IsEmpty())  entry->SetValue(wxT("user"), row.user_name);
			if (!row.source.IsEmpty())     entry->SetValue(wxT("source"), row.source);
			if (!row.event_type.IsEmpty()) entry->SetValue(wxT("event"), row.event_type);

			entry->SetValue(wxT("message"), row.message);

			// A row about a business object carries the object. That is what makes
			// the journal navigable rather than merely readable.
			if (!row.ref_guid.IsEmpty()) {
				entry->SetValue(wxT("refGuid"), row.ref_guid);
				entry->AddField(wxT("refMetaId"), ibDataValue::Int((s64)row.ref_meta_id));
			}

			rows.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)rows.size()));
		result.AddField(wxT("rows"), ibDataValue::Array(rows));

		// "Nothing matched" and "nothing happened" are different answers, and only
		// the first one means the question should be asked differently.
		if (rows.empty())
			result.SetValue(wxT("note"),
				ibMcpText("No rows matched. Widen the period, or drop the level filter."));

		return true;
	}
};

//---------------------------------------------------------------------------
// trace_read
//---------------------------------------------------------------------------
class ibMcpToolTraceRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("trace_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString source = ArgTraceSource().Text(params);
		return source.IsEmpty()
			? wxString(ibMcpText("reading the engine's journal"))
			: wxString::Format(ibMcpText("reading the engine's journal for %s"), source);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The TECHNOLOGY journal - what the ENGINE did, newest first: the SQL exactly "
			"as it was sent with its parameter count, which road a query took, the keys a join was "
			"stitched on, driver traffic, and raises with their site. This is the diagnostic half; "
			"journal_read is the accountant's half (logins, writes, postings) and answers a "
			"different question.\n\n"
			"It reads FILES, so it answers for the run being debugged as well as for this one - "
			"every row says which application wrote it. Ask it after running something and getting "
			"an answer you did not expect: the statement that produced it is in here verbatim.\n\n"
			"Debug builds only - a Release binary writes no journal, and says so rather than "
			"reporting an empty one.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgTraceApp(), ArgTraceSource(), ArgTraceLevel(),
			ArgSinceMinutes(), ArgContains(), ArgLimit() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString directory = TraceDirectory();
		std::map<wxString, wxFileName> files = TraceNewestPerApp(directory);

		if (files.empty()) {
			// ⭐ THE TWO EMPTIES ARE DIFFERENT ANSWERS, and only one of them means "ask again
			// differently". No directory at all, in a build that journals, means nothing has run yet.
			refusal = ibTechJournal::IsOpen()
				? wxString::Format(ibMcpText("No journal files under %s yet - nothing has run since "
					"this installation was built."), directory)
				: wxString::Format(ibMcpText("This build keeps no technology journal - it is written "
					"by Debug builds only (the switch is in crashGuard.cpp). Nothing was found under "
					"%s. The accountant's journal, journal_read, works in every build."), directory);
			return false;
		}

		// One application, when asked for. An unknown name is refused WITH THE LIST rather than
		// answered emptily - the caller cannot be expected to know what has run.
		const wxString wantApp = ArgTraceApp().Text(params);
		if (!wantApp.IsEmpty()) {

			wxString known;
			bool found = false;
			for (const auto& entry : files) {
				known += (known.IsEmpty() ? wxT("") : wxT(", ")) + entry.first;
				if (entry.first.IsSameAs(wantApp, /*caseSensitive=*/false))
					found = true;
			}

			if (!found) {
				refusal = wxString::Format(
					ibMcpText("No journal for '%s'. Applications that have run: %s."), wantApp, known);
				return false;
			}

			for (auto it = files.begin(); it != files.end(); ) {
				if (it->first.IsSameAs(wantApp, false)) ++it;
				else it = files.erase(it);
			}
		}

		const wxString wantLevel = ArgTraceLevel().Text(params);
		int minLevel = 0;
		if (!wantLevel.IsEmpty()) {
			minLevel = TraceLevelFromWord(wantLevel);
			if (minLevel < 0) {
				refusal = ibMcpText("Unknown level. Use info, warning or error.");
				return false;
			}
		}

		const wxString wantSource   = ArgTraceSource().Text(params).Lower();
		const wxString wantContains = ArgContains().Text(params);
		const s32 sinceMinutes      = (s32)ArgSinceMinutes().Whole(params);

		wxDateTime from = wxDefaultDateTime;
		if (sinceMinutes > 0)
			from = wxDateTime::Now() - wxTimeSpan::Minutes(sinceMinutes);

		std::vector<ibTraceRow> rows;
		std::vector<ibDataValue> read;

		const wxString ownFile = ibTechJournal::Path();
		const std::map<wxString, wxString> dumps = TraceDumpsByPid(directory);
		bool anyCrash = false;

		for (const auto& entry : files) {

			// A file untouched since before the window cannot hold a line inside it, and reading it
			// is the difference between an answer and a wait when a dozen runs are on disk.
			if (from.IsValid() && entry.second.GetModificationTime().IsEarlierThan(from))
				continue;

			const size_t before = rows.size();
			TraceParseInto(entry.second, entry.first, rows);

			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("app"), entry.first);
			node->SetValue(wxT("file"), entry.second.GetFullName());
			node->AddField(wxT("lines"), ibDataValue::Int((s64)(rows.size() - before)));

			// Which of these is the process answering right now. Everything else is another run,
			// and that distinction is the point of reading both.
			if (!ownFile.IsEmpty() && entry.second.GetFullPath().IsSameAs(ownFile, false))
				node->SetValue(wxT("note"), ibMcpText("this process"));

			// …and whether it ENDED, rather than merely stopping saying anything.
			const auto dump = dumps.find(TracePidOfLog(entry.second));
			if (dump != dumps.end()) {
				node->SetValue(wxT("crashDump"), dump->second);
				anyCrash = true;
			}

			read.push_back(ibDataValue::Child(node));
		}

		// Newest first, across every file at once - two processes' lines interleave by the clock,
		// which is what makes "what did the engine do when I asked" answerable in one read.
		std::stable_sort(rows.begin(), rows.end(),
			[](const ibTraceRow& l, const ibTraceRow& r) { return r.m_when.IsEarlierThan(l.m_when); });

		const s32 limitArg = (s32)ArgLimit().Whole(params);
		const size_t limit = (limitArg > 0) ? (size_t)limitArg : 50;

		std::vector<ibDataValue> out;
		size_t matched = 0;

		for (const ibTraceRow& row : rows) {

			if (from.IsValid() && row.m_when.IsEarlierThan(from))
				continue;
			if (TraceLevelOfName(row.m_level) < minLevel)
				continue;

			// The source is matched by PREFIX on purpose: the engine names its sources in a
			// hierarchy (query.sql, query.road, query.stitch), and "the query subsystem" is the
			// question far more often than any single one of them.
			if (!wantSource.IsEmpty() && !row.m_source.Lower().StartsWith(wantSource))
				continue;
			if (!wantContains.IsEmpty() && !row.m_message.Contains(wantContains))
				continue;

			++matched;
			if (out.size() >= limit)
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("time"), row.m_when.Format(wxT("%Y-%m-%d %H:%M:%S.%l")));
			entry->SetValue(wxT("app"), row.m_app);
			entry->SetValue(wxT("level"), row.m_level);
			entry->SetValue(wxT("thread"), row.m_thread);
			entry->SetValue(wxT("source"), row.m_source);
			entry->SetValue(wxT("message"), row.m_message);

			out.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)out.size()));
		result.AddField(wxT("matched"), ibDataValue::Int((s64)matched));
		result.AddField(wxT("rows"), ibDataValue::Array(out));
		result.AddField(wxT("files"), ibDataValue::Array(read));

		// ⭐ SAID EVEN WHEN NOTHING WAS ASKED ABOUT IT. A run that ended in a dump is the reason a
		// journal stops mid-sentence, and it is the one fact a reader must not have to ask for -
		// the alternative reading, "still going", is the one that makes somebody wait.
		if (anyCrash)
			result.AddField(wxT("crashed"), ibDataValue::Bool(true));

		if (matched > out.size())
			result.SetValue(wxT("note"), wxString::Format(
				ibMcpText("%u lines matched and the newest %u are here. Narrow it with source or "
					"contains rather than raising the limit - the file is long by design."),
				(unsigned)matched, (unsigned)out.size()));
		else if (out.empty())
			result.SetValue(wxT("note"),
				ibMcpText("No lines matched. Widen the period, drop the source filter, or check that "
					"the run you are asking about is one of the applications listed under files."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolJournalRead);
MCP_TOOL_REGISTER(ibMcpToolTraceRead);
