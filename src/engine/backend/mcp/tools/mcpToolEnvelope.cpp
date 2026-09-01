////////////////////////////////////////////////////////////////////////////
//	Description : finding a tool, so the other sixty-five need not be announced
////////////////////////////////////////////////////////////////////////////
//
// ⭐ WHY THIS EXISTS. `tools/list` hands a client every tool with its full schema before any work
// begins — measured on this tree, sixty-seven of them and some fifty kilobytes of it, of which a
// session uses a dozen. The cost is paid in full, every time, for a fraction of the benefit.
//
// So the handshake answers with the two verbs that let a caller find the rest, and everything else
// is fetched by name when it is actually wanted. Same idea Claude Code uses for its own deferred
// tools, arranged on the SERVER side because a plain MCP client cannot be asked to do it.
//
// ⭐ WHAT MAKES IT SAFE IS SOMETHING THAT LANDED FIRST. A schema returned as DATA is not one the
// client can validate against, so a wrong argument would once have been swallowed. The server now
// refuses an argument no tool declared, in words, naming what it does take — so the check moved
// from the client to the place that owns the answer, and the deferral costs nothing.
//
// ⚠ AND THE FULL LIST IS STILL REACHABLE. A caller that wants everything asks with no query; a
// caller that knows the name asks for it. Nothing is hidden — only unannounced.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include <wx/tokenzr.h>   // a query is words, and every one of them has to land


namespace {

// Does this tool answer to what was asked for? Matched against the NAME and the DESCRIPTION,
// because a caller looking for "locks" knows the word and not the family, and one looking for
// "what is locked" knows neither.
bool Matches(const ibMcpTool* tool, const wxString& query)
{
	if (query.IsEmpty())
		return true;

	const wxString name = tool->GetName().Lower();
	const wxString description = tool->GetDescription().Lower();

	// 🛑 THE FORM OF THE WORD DECIDED EVERYTHING, and that is not a search — it is a quiz on the
	// exact noun somebody else happened to write. Looking a word up as a SUBSTRING only ever finds
	// the short inside the long: `delete` finds "deleted", but `deleting` finds nothing, and
	// `translate` misses "translation" though it is right there in the description.
	//
	// It matters more here than anywhere else in this server: with lazy loading, a tool that
	// cannot be FOUND does not exist. Eager listing was forgiving about vocabulary because the
	// whole surface was in front of the caller; this is not, and the finder has to make up for it.
	//
	// ⭐ SO THE QUERY IS SHORTENED, NOT THE TEXT. Trying the word, then its prefixes down to four
	// characters, meets the description's noun from the caller's verb without touching the
	// descriptions themselves — which would be the same fix written 76 times and forgotten on the
	// 77th. Four is the floor because three lets `set` reach half the tree; over-matching here
	// costs a slightly longer list, and under-matching costs a tool nobody can reach.
	const auto somewhere = [&name, &description](const wxString& word) {

		for (size_t length = word.length(); length >= 4; --length) {

			const wxString stem = word.Left(length);

			if (name.Find(stem) != wxNOT_FOUND || description.Find(stem) != wxNOT_FOUND)
				return true;
		}

		// Shorter than the floor, so it stands or falls as written — `id`, `run`, `sum`.
		return word.length() < 4
			&& (name.Find(word) != wxNOT_FOUND || description.Find(word) != wxNOT_FOUND);
	};

	// Every word must be somewhere — an AND, because two words are a caller narrowing down, and
	// an OR would answer a narrowing with MORE results than the word alone.
	wxStringTokenizer words(query.Lower(), wxT(" \t,"), wxTOKEN_STRTOK);

	while (words.HasMoreTokens())
		if (!somewhere(words.GetNextToken()))
			return false;

	return true;
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgSchema()
{
	static const ibArg s_a(wxT("schema"), ibArg::Kind::Flag,
		_("Include each tool's input schema. On by default, because the schema is what a call "
		  "is built from; turn it off to survey what exists without paying for the detail."));
	return s_a;
}

const ibArg& ArgQuery()
{
	static const ibArg s_a(wxT("query"), ibArg::Kind::Text,
		_("What the job is, in words. Every word must appear somewhere in a tool's name or "
			  "description, so two words narrow rather than widen. Omit to list them all."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		_("One tool by its exact name, when it is already known - the shortest way to get a "
			  "schema back."));
	return s_a;
}

const ibArg& ArgTool()
{
	static const ibArg s_a(wxT("tool"), ibArg::Kind::Text,
		_("Which tool - the exact name, as mcp_search gives it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgArguments()
{
	static const ibArg s_a(wxT("arguments"), ibArg::Kind::Text,
		_("What to pass it, shaped by that tool's own input schema. An argument the tool does "
			  "not declare is refused by name, so a guess is answered rather than swallowed."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// mcp_search
//---------------------------------------------------------------------------

class ibMcpToolSearch : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("mcp_search"); }

	bool IsAlwaysListed() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString query = ArgQuery().Text(params);
		return query.IsEmpty()
			? _("listing what it can do")
			: wxString::Format(_("looking for a way to %s"), query);
	}

	wxString GetDescription() const override
	{
		return _("FIND THE TOOL FOR A JOB, then call it through mcp_call. This platform exposes "
			"far more verbs than are worth handing over at once - metadata and forms, queries and "
			"reports, the debugger, the journal, users and locks, spreadsheets, the configuration "
			"itself - so they are found rather than announced. Ask in the words of the job "
			"('lock', 'print a report', 'what changed'), or with no query at all to see "
			"everything. The answer carries each tool's full input schema, which is what mcp_call "
			"then needs.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgQuery(), ArgName(), ArgSchema() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString wanted = ArgName().Text(params);
		const wxString query = ArgQuery().Text(params);

		bool withSchema = true;
		if (params.FindField(ArgSchema().Name()) != nullptr)
			withSchema = ArgSchema().Flag(params);

		std::vector<ibDataValue> found;

		for (const ibMcpTool* tool : ibMcpTools()) {

			if (!wanted.IsEmpty()) {
				if (!tool->GetName().IsSameAs(wanted, false))
					continue;
			}
			else if (!Matches(tool, query)) {
				continue;
			}

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), tool->GetName());
			entry->SetValue(wxT("description"), tool->GetDescription());

			if (withSchema)
				tool->DescribeInput(entry->Child(wxT("inputSchema")));

			found.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("found"), ibDataValue::Int((s64)found.size()));
		result.AddField(wxT("tools"), ibDataValue::Array(found));

		if (found.empty()) {
			// A NAME THAT MATCHES NOTHING IS DIFFERENT FROM A QUERY THAT DOES, and a caller acts
			// differently on each: one is a typo, the other is a job this platform has no verb
			// for. Said apart rather than answered with the same empty list.
			refusal = wanted.IsEmpty()
				? wxString::Format(
					_("Nothing here matches '%s'. Ask with fewer words, or with none to see "
					  "everything there is."), query)
				: wxString::Format(
					_("There is no tool called '%s'. Ask by what the job IS instead of by name."),
					wanted);
			return false;
		}

		result.SetValue(wxT("call"),
			_("Invoke any of these through mcp_call: {tool: <name>, arguments: {...}}."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSearch);

//---------------------------------------------------------------------------
// mcp_call
//---------------------------------------------------------------------------
//
// ⭐ A DECLARATION, NOT AN IMPLEMENTATION. This class exists so the envelope APPEARS in
// `tools/list` — a client cannot invoke what it was never told about — while the actual
// unwrapping happens in the server, before any tool is looked up.
//
// It is done there and not here on purpose. Dispatching from inside a tool would run the whole
// call INSIDE this one, and everything the server does around a tool — refusing an argument the
// target never declared, classifying the engine's exception by type, writing the activity line
// into the registration journal — would see `mcp_call` and its two arguments instead of the real
// verb and its real ones. The trail would read "mcp_call, mcp_call, mcp_call", which records
// nothing, and the argument gate would guard the envelope instead of the letter.
//
// So Call() below is unreachable in practice, and says so rather than pretending to work.
//
class ibMcpToolCall : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("mcp_call"); }

	bool IsAlwaysListed() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		// Only ever seen if the envelope was malformed — a well-formed one is unwrapped before
		// this is asked, and the ACTIVITY then comes from the real tool.
		return _("calling a tool");
	}

	wxString GetDescription() const override
	{
		return _("Invoke any tool this platform has, by name: {tool: 'metadata_create', "
			"arguments: {...}}. Use it for everything mcp_search finds - the search returns each "
			"tool's input schema, and `arguments` is that schema filled in. Refusals, errors and "
			"results come back exactly as if the tool had been called directly, because it was.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgTool(), ArgArguments() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// Unreachable: ibMcpServer::Answer replaces the name and the arguments before the registry
		// is consulted. Saying so beats a silent `return true` that would look like it worked.
		refusal = _("mcp_call is unwrapped by the server and has no body of its own.");
		return false;
	}
};

MCP_TOOL_REGISTER(ibMcpToolCall);
