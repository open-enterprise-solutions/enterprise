////////////////////////////////////////////////////////////////////////////
//	Description : the journal tool - what this installation actually did
////////////////////////////////////////////////////////////////////////////
//
// THE REGISTRATION JOURNAL IS THE ONE THAT IS READ FROM OUTSIDE. The other one -
// the technological journal, plain text, one file per process run - belongs to
// the platform's own developer and stays there. What belongs here is the record
// of what the installation DID: who logged in, which document was written, which
// schema was applied, and - since the diagnostics bus is journalled - every
// script that failed while running.
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
#include "backend/logger/logger.h"
#include "backend/logger/loggerReader.h"

#include <wx/datetime.h>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments, declared once and read through the same objects — see ibMcpTool::Arguments().
const ibArg& ArgSinceMinutes() { static const ibArg a(wxT("sinceMinutes"), ibArg::Kind::Whole, ibMcpText("Only what happened in the last N minutes. This is usually the one to pass.")); return a; }
const ibArg& ArgLevel() { static const ibArg a(wxT("level"), ibArg::Kind::Text, ibMcpText("Lowest severity to report: info, warn, error, audit. Pass error for failures only.")); return a; }
const ibArg& ArgSource() { static const ibArg a(wxT("source"), ibArg::Kind::Text, ibMcpText("Only this subsystem. script = a running module, auth = logins, and so on.")); return a; }
const ibArg& ArgEvent() { static const ibArg a(wxT("event"), ibArg::Kind::Text, ibMcpText("Only this event type, for example runtime.error.")); return a; }
const ibArg& ArgUser() { static const ibArg a(wxT("user"), ibArg::Kind::Text, ibMcpText("Only this user's rows.")); return a; }
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
			"nowhere else this side of the debugger.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgSinceMinutes(), ArgLevel(), ArgSource(), ArgEvent(),
			ArgUser(), ArgContains(), ArgLimit() };
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

MCP_TOOL_REGISTER(ibMcpToolJournalRead);
