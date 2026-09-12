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

#include <algorithm>   // the bar is the best score there was
#include <cmath>       // std::log - what a word is worth


namespace {

// How much of what was asked for does this tool answer to? Matched against the NAME, the
// DESCRIPTION and WHAT THE TOOL CARRIES (GetSearchText), because a caller looking for "locks"
// knows the word and not the family, one looking for "what is locked" knows neither, and one
// looking for "how much is left" is asking about something no verb here is named after — it is
// written inside `pattern_read`.
//
// 🛑 IT USED TO BE A VERDICT, AND EVERY WORD HAD TO LAND. Two words are a caller narrowing down,
// so an AND was the right instinct — but the words come from the caller's world and the text from
// ours, and one noun that never made it across sank the whole query to nothing. Nothing is the
// one answer a finder must not give lightly: it does not read as "say it differently", it reads
// as "this platform has no such thing", and the caller goes off and builds it by hand.
//
// ⭐ SO THE COUNT DECIDES, NOT A YES — and since 2026-09-11 not the count either, but what the words
// found are WORTH (see Call): a word every tool carries tells no tool apart.
wxString Haystack(const ibMcpTool* tool)
{
	// The name, the description and the corpus are searched as ONE text: all three answer "is this
	// the tool", and a match in any is the same answer. Joined here rather than walked three
	// times, so the one rule in ibMcpWordsFound has a single haystack to work on.
	wxString haystack = tool->GetName() + wxT("\n") + tool->GetDescription();

	// ⭐⭐ AND WHAT ITS ARGUMENTS ARE CALLED, because half of what a verb does is said there and
	// nowhere else. `report_select` takes a `variant`; its description never says the word, so a
	// caller asking about "report variant selected fields" was answered `report_filter` — whose
	// only advantage was that one of those words is in its NAME — and the two verbs the question
	// was actually about did not appear at all (measured live, 2026-09-02, the second time the
	// same query missed them).
	//
	// A schema is what a caller reads before deciding; searching everything BUT the schema means
	// the finder knows less about a tool than the answer it hands back does.
	for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments())
		haystack << wxT("\n") << argument.Name() << wxT(" ") << argument.Description();

	const wxString carried = tool->GetSearchText();
	if (!carried.IsEmpty())
		haystack << wxT("\n") << carried;

	return haystack;
}

// WHAT THE TOOL SAYS IT IS: its name, and the first sentence of its description - the summary every
// description here opens with ("Add a metadata object, ...", "Ask the RUNNING APPLICATION for a picture
// of its window"). Read apart from the rest by the finder (see Call).
wxString Identity(const ibMcpTool* tool)
{
	const wxString description = tool->GetDescription();
	size_t end = description.length();
	for (size_t i = 0; i + 1 < description.length(); ++i) {
		const wxChar c = description[i];
		if (c == wxT('\n')
			|| ((c == wxT('.') || c == wxT('?') || c == wxT('!')) && wxIsspace(description[i + 1]))) {
			end = i;
			break;
		}
	}
	return tool->GetName() + wxT("\n") + description.Left(end);
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgSchema()
{
	static const ibArg s_a(wxT("schema"), ibArg::Kind::Flag,
		ibMcpText("Include each tool's input schema. On by default, because the schema is what a call "
		  "is built from; turn it off to survey what exists without paying for the detail."));
	return s_a;
}

const ibArg& ArgQuery()
{
	static const ibArg s_a(wxT("query"), ibArg::Kind::Text,
		ibMcpText("What the job is, in words. Ranked by what the words that land in a tool's name, its "
			  "description or the text it carries are WORTH: a word few tools carry counts for much, "
			  "one nearly every tool carries for almost nothing - best first, and whatever comes close "
			  "to the best; a partial match is marked '2 of your 4 words'. A REGULAR EXPRESSION "
			  "is read as one whenever it carries | \\ [ ] ^ $ .* - 'lock|block', 'report.*print'. "
			  "\nASK FOR THE WHOLE FAMILY AT ONCE when you are about to work in one - 'metadata_.*', "
			  "'report_.*', 'form_.*' - and the schemas of all of them come back together. Tools of "
			  "a family are used in sequence and their arguments differ in small ways, so fetching "
			  "them one at a time costs a refusal per difference; one call up front costs nothing "
			  "and answers them all.\n"
			  "The answer also names PLACES IN THE PATTERNS that speak about it, which is where "
			  "questions of the trade ('how much is left') are answered rather than by any verb. "
			  "Omit to list every tool."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("One tool by its exact name, when it is already known - the shortest way to get a "
			  "schema back."));
	return s_a;
}

const ibArg& ArgTool()
{
	static const ibArg s_a(wxT("tool"), ibArg::Kind::Text,
		ibMcpText("Which tool - the exact name, as mcp_search gives it."), /*required*/ true);
	return s_a;
}

// 🛑 IT IS AN OBJECT, AND SAYING `string` HERE CLOSED THE ONLY DOOR THERE IS. The server reads the
// inner arguments as a NODE (ibMcpServer::Answer: `given->FindChild("arguments")`), so a caller
// that believed the declared type and sent the schema as a JSON string handed over something
// FindChild cannot see - and every tool answered "needs 'text', and it did not come" about an
// argument that was right there, one encoding away.
//
// ⚠ WHAT MAKES IT WORSE THAN AN ORDINARY WRONG TYPE: this is the envelope. A client that follows
// the schema it is given - which is what a good one does - cannot make a SINGLE call, and the
// refusal it gets names the inner tool's argument, pointing at the letter while the fault is in
// the envelope. Found by Claude Code on 2026-09-02, first minute of its first session here.
const ibArg& ArgArguments()
{
	static const ibArg s_a(wxT("arguments"), ibArg::Kind::Node,
		ibMcpText("What to pass it, shaped by that tool's own input schema - an OBJECT, the schema filled "
			  "in, not a string containing one. An argument the tool does not declare is refused "
			  "by name, so a guess is answered rather than swallowed."));
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
			? ibMcpText("listing what it can do")
			: wxString::Format(ibMcpText("looking for a way to %s"), query);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("FIND THE TOOL FOR A JOB, then call it through mcp_call. This platform exposes "
			"far more verbs than are worth handing over at once - metadata and forms, queries and "
			"reports, the debugger, the journal, users and locks, spreadsheets, the configuration "
			"itself - so they are found rather than announced. Ask in the words of the job "
			"('lock', 'print a report', 'what changed'), as a regular expression if that is "
			"easier, or with no query at all to see everything. The answer carries each tool's "
			"full input schema, which is what mcp_call then needs - and, beside it, the PLACES in "
			"the pattern corpus that speak about the same words, because half of what a caller "
			"needs is a passage rather than a verb.\n"
			"⭐ ABOUT TO WORK IN ONE AREA? ASK FOR THE FAMILY, NOT THE VERB - 'metadata_.*', "
			"'report_.*', 'debug_.*' - and every schema in it arrives together. These verbs are "
			"used in sequence and differ from each other in small ways, so learning them one "
			"refusal at a time is the slow road, and it is the one a caller takes by default.");
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

		// SCORED FIRST, CHOSEN SECOND. Which tools are good enough cannot be known while they are
		// still being read — the bar is the best score there was, and that is only final once
		// every tool has answered.
		//
		// ⭐⭐ A WORD COUNTS FOR AS MUCH AS IT TELLS THE TOOLS APART. Counted, every word weighed the same, and
		// the words a question is mostly made of - "how", "was", "to", "each", met by nearly every description
		// (a short one as a piece of a longer: "to" in "tool", "each" in "reach") - gave every tool four or
		// five points for nothing, the longest descriptions the most; and the list came in registry order.
		// The verb asked about stood first in 2 of 24 real questions, behind compose_run and code_run
		// (measured 2026-09-11). So a word is worth what it separates: its worth falls with the number of
		// tools that carry it - one every tool carries is worth nearly nothing, one two tools carry decides -
		// and the list comes best first. The number is the tools' own, so there is no list of little words.
		struct ibScored { const ibMcpTool* m_tool; size_t m_found; double m_worth; };

		std::vector<ibScored> scored;
		size_t asked = 0;

		if (!wanted.IsEmpty() || query.IsEmpty()) {
			for (const ibMcpTool* tool : ibMcpTools())
				if (wanted.IsEmpty() || tool->GetName().IsSameAs(wanted, false))   // no query: the whole list
					scored.push_back({ tool, 0, 0.0 });
		}
		else {
			std::vector<const ibMcpTool*> tools;
			std::vector<std::vector<bool>> met, named;
			std::vector<double> length;
			for (const ibMcpTool* tool : ibMcpTools()) {
				const wxString haystack = Haystack(tool);
				tools.push_back(tool);
				length.push_back((double)haystack.length());
				met.emplace_back();
				ibMcpWordsPresent(haystack, query, met.back());
				named.emplace_back();
				ibMcpWordsPresent(Identity(tool), query, named.back());
			}
			asked = met.empty() ? 0 : met.front().size();

			double average = 0.0;
			for (const double one : length)
				average += one;
			average = length.empty() ? 1.0 : std::max(1.0, average / (double)length.size());

			// How many tools carry each word, and what the word is worth for it (the usual inverse document
			// frequency: log(1 + (all - carriers + 0.5) / (carriers + 0.5))).
			std::vector<size_t> carriers(asked, 0);
			for (const std::vector<bool>& words : met)
				for (size_t w = 0; w < asked; ++w)
					carriers[w] += words[w] ? 1 : 0;

			const double all = (double)tools.size();
			std::vector<double> worth(asked, 0.0);
			for (size_t w = 0; w < asked; ++w)
				worth[w] = std::log(1.0 + (all - (double)carriers[w] + 0.5) / ((double)carriers[w] + 0.5));

			// ⭐ AND A WORD IN WHAT THE TOOL SAYS IT IS COUNTS TWICE - its name and the first sentence of its
			// description (Identity). That is the tool saying what it is in a line; the rest says it in two
			// hundred, and a word can stand there for any reason. When every word of a question is a common
			// one - "read the text of the configuration module" - worth alone cannot tell the tools apart,
			// and module_read is the one whose NAME says both.
			//
			// ⭐ …AND A LONG TEXT MEETS WORDS BY CHANCE. What the description finds is scaled by its length,
			// the way BM25 does it: a text three times the average length meets a word about as often by
			// chance as by meaning, so what it meets counts for about half, and a short one's for more.
			// Without it compose_run and code_run - the two longest descriptions here - stood in front of
			// the verb asked about in most of the questions they led. The identity line is not scaled: it
			// says the same whatever the description around it - without that, a long description's own
			// opening ("Add a metadata object") lost to a short neighbour's (predefined_add), measured.
			constexpr double k1 = 1.2, b = 0.75;
			for (size_t t = 0; t < tools.size(); ++t) {
				const double scale = (k1 + 1.0) / (1.0 + k1 * (1.0 - b + b * length[t] / average));
				size_t landed = 0;
				double sum = 0.0;
				for (size_t w = 0; w < asked; ++w) {
					if (met[t][w]) {
						landed++;
						sum += worth[w] * scale + (named[t][w] ? worth[w] : 0.0);
					}
				}
				if (landed > 0)
					scored.push_back({ tools[t], landed, sum });
			}

			std::stable_sort(scored.begin(), scored.end(),
				[](const ibScored& a, const ibScored& b) { return a.m_worth > b.m_worth; });
		}

		const double best = scored.empty() ? 0.0 : scored.front().m_worth;

		// ⭐ THE RUNNERS-UP, WHEN THEY ARE CLOSE. Taking only the best is wrong when two verbs answer one
		// question: "report variant selected fields filter" is about report_select AND report_variant, and a
		// best-only answer hid them (measured 2026-09-02). So whatever comes within kNear of the best stays -
		// a handful, never a list the answer has to be looked for in again.
		constexpr double kNear = 0.6;
		constexpr size_t kMost = 8;

		std::vector<ibDataValue> found;

		for (const ibScored& candidate : scored) {

			if (!query.IsEmpty() && wanted.IsEmpty()
				&& (candidate.m_worth < best * kNear || found.size() >= kMost))
				break;   // best first, so nothing after this one is closer

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), candidate.m_tool->GetName());
			entry->SetValue(wxT("description"), candidate.m_tool->GetDescription());

			// Said on the ENTRY and not only in a note, because a partial answer that looks like
			// an exact one is worse than no answer: it is acted on.
			if (!query.IsEmpty() && wanted.IsEmpty() && candidate.m_found < asked)
				entry->SetValue(wxT("matched"), wxString::Format(
					ibMcpText("%i of your %i words"), (int)candidate.m_found, (int)asked));

			if (withSchema)
				candidate.m_tool->DescribeInput(entry->Child(wxT("inputSchema")));

			found.push_back(ibDataValue::Child(entry));
		}

		// ⭐⭐ ONE DOOR, TWO KINDS OF ADDRESS. A question is rarely "which verb" or "where is this
		// written" — it is usually both, and a caller cannot tell which of the two doors to knock
		// on before they know the answer. So the tools that carry a body of text are asked to
		// point INTO it with the same words, and the places come back beside the verbs.
		std::vector<ibDataValue> places;

		if (!query.IsEmpty() && wanted.IsEmpty())
			for (const ibMcpTool* tool : ibMcpTools())
				tool->FindInside(query, places);

		// ⭐ THIS CONFIGURATION'S OWN ANSWER FIRST. A place that names an object (it carries the object's `id` -
		// note_read's) is where the thing asked about is built HERE; a pattern says how such a thing is built
		// anywhere. Both are worth reading, but the trim below must not cut the first for the second.
		std::stable_partition(places.begin(), places.end(), [](const ibDataValue& place) {
			const std::shared_ptr<ibDataNode>& node = place.AsChild();
			return node && node->FindField(wxT("id")) != nullptr;
		});

		// ⚠ A HANDFUL, BECAUSE THIS IS THE TOOL FINDER. The corpus can answer a common word from
		// two dozen places, and that list arriving under a question about VERBS buries the verbs.
		// Whoever wants all of them asks the corpus directly — pattern_read {query} — which is
		// said in the note below.
		const bool trimmed = places.size() > 8;
		if (trimmed)
			places.resize(8);

		result.AddField(wxT("found"), ibDataValue::Int((s64)found.size()));
		result.AddField(wxT("tools"), ibDataValue::Array(found));

		if (!places.empty()) {
			result.AddField(wxT("places"), ibDataValue::Array(places));
			result.SetValue(wxT("reading"), wxString(trimmed
				? ibMcpText("`places` are passages that answer this in words, not verbs to call - the "
				  "first few of more. Read one with pattern_read {name, topic}, quoting the topic "
				  "given; pattern_read {query} with the same words lists them all.")
				: ibMcpText("`places` are passages that answer this in words, not verbs to call. Read one "
				  "with pattern_read {name, topic}, quoting the topic given."))
				+ ibMcpText(" A place with an `id` is an object of THIS configuration whose help or notes "
				  "speak to the question - where it is built here: note_read {id, help: true} reads its "
				  "texts, metadata_get the object itself, and when it is a report, compose_run reads the "
				  "answer out of it."));
		}

		if (found.empty() && places.empty()) {
			// A NAME THAT MATCHES NOTHING IS DIFFERENT FROM A QUERY THAT DOES, and a caller acts
			// differently on each: one is a typo, the other is a job this platform has no verb
			// for. Said apart rather than answered with the same empty list.
			refusal = wanted.IsEmpty()
				? wxString::Format(
					ibMcpText("Not one word of '%s' appears anywhere here - in a tool's name, its "
					  "description, or the patterns. Say it in other words, or ask with none to "
					  "see everything there is."), query)
				: wxString::Format(
					ibMcpText("There is no tool called '%s'. Ask by what the job IS instead of by name."),
					wanted);
			return false;
		}

		result.SetValue(wxT("call"),
			ibMcpText("Invoke any of these through mcp_call: {tool: <name>, arguments: {...}}."));

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
		return ibMcpText("calling a tool");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Invoke any tool this platform has, by name: {tool: 'metadata_create', "
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
		refusal = ibMcpText("mcp_call is unwrapped by the server and has no body of its own.");
		return false;
	}
};

MCP_TOOL_REGISTER(ibMcpToolCall);
