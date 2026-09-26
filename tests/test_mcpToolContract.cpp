////////////////////////////////////////////////////////////////////////////
//	Description : the MCP tool CONTRACT — what every tool owes its caller,
//	              asked of every tool that is registered (backend/mcp/).
//
//	              ⭐ THE POINT IS THAT THESE TESTS DO NOT NAME ANY TOOL. They walk
//	              ibMcpTools() and hold each one to the same rules, so a tool
//	              added tomorrow is covered the day it registers and a rule
//	              cannot be kept by "everybody remembering". That is the same
//	              reasoning that put the undeclared-argument gate in the SERVER
//	              rather than in each tool: a check written per tool is a check
//	              the sixty-first tool forgets.
//
//	              What is pinned here is the part a caller cannot recover from.
//	              A machine caller reads `tools/list` and nothing else — the name,
//	              the description and the schema ARE the API. A schema that
//	              requires an argument it never declares, or a closed set listed
//	              in one place and enforced from another, sends the caller to
//	              guess; and a guess costs a round trip at best and a silently
//	              wrong write at worst. Both of those happened on 2026-08-31.
//
//	              Pure backend: no server is started, no socket is opened, no
//	              configuration is loaded. Every tool answers these questions
//	              from its own declaration.
//
//	⚠ BACKEND TOOLS ONLY. The registry is filled by the MCP_TOOL_REGISTER lines
//	  at the bottom of each tool's file, so it holds whatever is LINKED — and
//	  oes_tests links backend, not frontend or designer. The form verbs
//	  (frontend/mcp/tools) and the message verbs (designer/mcp/tools) are absent
//	  here by construction, not by oversight. The count assertion below is
//	  deliberately a floor rather than an equality for the same reason.
////////////////////////////////////////////////////////////////////////////

#include <gtest/gtest.h>

#include "backend/mcp/mcpTool.h"
#include "backend/mcp/mcpClipboard.h"
#include "backend/serialize/dataBuilder.h"

#include <wx/dir.h>        // the ASCII rule reads the MCP sources themselves
#include <wx/file.h>
#include <wx/filename.h>

#include <map>
#include <set>
#include <string>
#include <algorithm>   // the refusal must name one of the tool's OWN required arguments

namespace {

// The engine's sources, found from THIS file's location - CMake runs the binary from the build tree,
// so the path is resolved the way test_propertySerialized.cpp resolves its partials.
wxString EngineSourceDir()
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.RemoveLastDir();                      // tests -> enterprise
	here.AppendDir(wxT("src"));
	here.AppendDir(wxT("engine"));
	return here.GetPath();
}

// The line of every string literal in a C++ source that carries a byte outside ASCII. Comments are
// skipped - they are written for people and carry marks on purpose - and so are character literals;
// a quote standing between two digits (1'000) is a separator, not the start of one.
std::vector<int> NonAsciiLiteralLines(const std::string& text)
{
	std::vector<int> found;
	int line = 1;
	const size_t n = text.size();
	const auto isWord = [](char c) {
		return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
	};

	for (size_t i = 0; i < n; ++i) {
		const char c = text[i];

		if (c == '\n') {
			++line;
		}
		else if (c == '/' && i + 1 < n && text[i + 1] == '/') {
			while (i + 1 < n && text[i + 1] != '\n')
				++i;                               // the newline itself is counted by the loop
		}
		else if (c == '/' && i + 1 < n && text[i + 1] == '*') {
			i += 2;
			while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
				if (text[i] == '\n')
					++line;
				++i;
			}
			++i;                                   // onto the closing '/'
		}
		else if (c == '\'') {
			if (i > 0 && isWord(text[i - 1]))
				continue;                          // a digit separator
			for (++i; i < n && text[i] != '\'' && text[i] != '\n'; ++i) {
				if (text[i] == '\\')
					++i;
			}
		}
		else if (c == '"') {
			const int start = line;
			bool outside = false;

			// A RAW literal - R"delim( ... )delim" - ends at its own delimiter, not at the next quote.
			if (i > 0 && text[i - 1] == 'R' && (i < 2 || !isWord(text[i - 2]) || text[i - 2] == '8'
				|| text[i - 2] == 'L' || text[i - 2] == 'u' || text[i - 2] == 'U')) {
				const size_t open = text.find('(', i);
				if (open == std::string::npos)
					break;
				const std::string close = ")" + text.substr(i + 1, open - i - 1) + "\"";
				const size_t end = text.find(close, open);
				const size_t stop = end == std::string::npos ? n : end;
				for (size_t k = open; k < stop; ++k) {
					if (text[k] == '\n')
						++line;
					else if ((unsigned char)text[k] >= 0x80)
						outside = true;
				}
				i = end == std::string::npos ? n : end + close.size() - 1;
			}
			else {
				for (++i; i < n && text[i] != '"' && text[i] != '\n'; ++i) {
					if (text[i] == '\\' && i + 1 < n)
						++i;
					if ((unsigned char)text[i] >= 0x80)
						outside = true;
				}
				if (i < n && text[i] == '\n')
					++line;                        // an unterminated literal: do not lose the count
			}

			if (outside)
				found.push_back(start);
		}
	}

	return found;
}

// The schema a tool publishes, as a node — the same call the server makes when
// it answers `tools/list`, and the same one the argument gate reads.
ibDataNode SchemaOf(const ibMcpTool* tool)
{
	ibDataNode schema;
	tool->DescribeInput(schema);
	return schema;
}

// Every argument name a tool declares. The declaration is a sub-node per
// argument in `properties`, which the node keeps in its PROPERTY area.
// What KIND a tool declared an argument as, found by the name the schema publishes
// it under. The schema walk sees names and JSON fields; the kind lives on the
// argument, and one check below needs both sides of that pair.
ibMcpTool::ibMcpArgument::Kind KindOfArgument(const ibMcpTool* tool, const wxString& name)
{
	for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {
		if (argument.Name() == name)
			return argument.KindOf();
	}
	return ibMcpTool::ibMcpArgument::Kind::Text;
}

std::set<wxString> DeclaredArguments(const ibDataNode& schema)
{
	std::set<wxString> names;

	if (const ibDataNode* properties = schema.FindChild(wxT("properties"))) {
		for (const auto& entry : properties->Properties())
			names.insert(entry.first);
	}

	return names;
}

// The names a tool says it cannot work without.
std::vector<wxString> RequiredArguments(const ibDataNode& schema)
{
	std::vector<wxString> names;

	if (const ibDataValue* required = schema.FindField(wxT("required"))) {
		if (required->Kind() == ibDataKind::Array) {
			for (const ibDataValue& item : required->AsArray()) {
				if (item.Kind() == ibDataKind::String)
					names.push_back(item.AsString());
			}
		}
	}

	return names;
}

} // namespace

//---------------------------------------------------------------------------
// the registry itself
//---------------------------------------------------------------------------

TEST(McpToolRegistry, Tools_AreRegisteredByLinkingTheirOwnFile)
{
	// A floor, not an equality — see the note at the top of this file. If this
	// ever reads zero, the MCP_TOOL_REGISTER lines are being stripped by the
	// linker and every tool has silently left the build.
	EXPECT_GE(ibMcpTools().size(), 20u);
}

TEST(McpToolRegistry, EveryTool_HasANameNobodyElseHas)
{
	std::map<wxString, int> seen;

	for (const ibMcpTool* tool : ibMcpTools()) {
		ASSERT_NE(tool, nullptr);
		const wxString name = tool->GetName();

		EXPECT_FALSE(name.IsEmpty()) << "a tool with no name cannot be invoked at all";
		seen[name]++;
	}

	for (const auto& entry : seen) {
		// TWO TOOLS UNDER ONE NAME is not a duplicate — it is one tool that can
		// never be reached, and which of them it is depends on link order.
		EXPECT_EQ(entry.second, 1)
			<< "more than one tool answers to '" << entry.first.ToStdString() << "'";
	}
}

TEST(McpToolRegistry, EveryTool_IsFoundByItsOwnName)
{
	for (const ibMcpTool* tool : ibMcpTools())
		EXPECT_EQ(ibFindMcpTool(tool->GetName()), tool);
}

TEST(McpToolRegistry, AnUnknownName_FindsNothingRatherThanAnything)
{
	EXPECT_EQ(ibFindMcpTool(wxT("no_such_tool")), nullptr);
	EXPECT_EQ(ibFindMcpTool(wxEmptyString), nullptr);
}

// A client is refused everything until it has introduced itself, and these are the verbs the
// greeting itself is made of. A name here that no longer exists would shut the door with no way
// through it - which is the one failure this rule must not have.
TEST(McpGreetingGate, EveryVerbAllowedBeforeTheGreeting_IsARealTool)
{
	ASSERT_FALSE(ibMcpGreetingVerbs().empty());

	for (const wxString& verb : ibMcpGreetingVerbs())
		EXPECT_NE(ibFindMcpTool(verb), nullptr) << "greeting verb with no tool: " << verb.ToStdString();
}

// The greeting must be sayable and the room readable: without these two the rule would be a wall.
TEST(McpGreetingGate, SpeakingAndReadingTheRoom_AreAllowed)
{
	EXPECT_TRUE(ibMcpRunsBeforeGreeting(wxT("chat_say")));
	EXPECT_TRUE(ibMcpRunsBeforeGreeting(wxT("chat_history")));
	EXPECT_TRUE(ibMcpRunsBeforeGreeting(wxT("platform_state")));
	EXPECT_TRUE(ibMcpRunsBeforeGreeting(wxT("metadata_tree")));

	// …and asked the way a caller spells it, which is not always the way the table does.
	EXPECT_TRUE(ibMcpRunsBeforeGreeting(wxT("Chat_Say")));
}

// Everything that CHANGES anything waits - that is the whole point of the door.
TEST(McpGreetingGate, WritingVerbs_WaitForTheGreeting)
{
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxT("metadata_create")));
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxT("metadata_set")));
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxT("metadata_bind")));
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxT("module_write")));
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxT("config_save")));
	EXPECT_FALSE(ibMcpRunsBeforeGreeting(wxEmptyString));
}

//---------------------------------------------------------------------------
// what a caller is told
//---------------------------------------------------------------------------

TEST(McpToolContract, EveryTool_DescribesItselfForTheCaller)
{
	for (const ibMcpTool* tool : ibMcpTools()) {
		// The description is the ONLY documentation a machine caller has. An
		// empty one is a verb nobody can decide to use.
		EXPECT_FALSE(tool->GetDescription().IsEmpty())
			<< tool->GetName().ToStdString() << " has no description";
	}
}

// ⭐ AND IT ARRIVES AS IT WAS WRITTEN. A tool's text is an `ibMcpText("..." "..." "...")`, which is
// `L"..." "..." "..."` - and MSVC encodes every piece after the first through the code page (1251)
// before joining them into the wide string. A mark that page lacks - a star, a warning sign, a stop
// sign - reached the caller as `?`: fourteen texts, among them the very warnings that send a caller
// from code_run to compose_run (found 2026-09-15 by reading an answer byte by byte; the build said
// C4566 on each). The Linux build sends them intact, so no test that runs the tools can see it - only
// the SOURCE can. The rule was two comments long (mcpTool.h, mcpServer.cpp); this is where it is kept.
//
// Read from the sources of every layer that has tools, not only the backend this binary links: the form
// verbs and the message verbs are sent down the same wire.
TEST(McpToolContract, EveryTextSentDownTheWire_IsAscii)
{
	const wxString engine = EngineSourceDir();
	size_t scanned = 0;

	for (const wxString layer : { wxString(wxT("backend")), wxString(wxT("frontend")), wxString(wxT("designer")) }) {
		wxFileName dir(engine, wxEmptyString);
		dir.AppendDir(layer);
		dir.AppendDir(wxT("mcp"));
		if (!wxDirExists(dir.GetPath()))
			continue;

		wxArrayString files;
		wxDir::GetAllFiles(dir.GetPath(), &files, wxEmptyString, wxDIR_FILES | wxDIR_DIRS);

		for (const wxString& path : files) {
			const wxString ext = wxFileName(path).GetExt().Lower();
			if (ext != wxT("cpp") && ext != wxT("h"))
				continue;

			wxFile file(path);
			ASSERT_TRUE(file.IsOpened()) << path.ToStdString();
			std::string bytes((size_t)file.Length(), '\0');
			if (!bytes.empty())
				file.Read(&bytes[0], bytes.size());
			++scanned;

			for (const int line : NonAsciiLiteralLines(bytes))
				ADD_FAILURE() << path.ToStdString() << ":" << line
					<< " - a string literal carries a character outside ASCII. On Windows every piece of"
					   " an ibMcpText literal after the first is encoded through the code page, and a mark it"
					   " lacks reaches the caller as '?'. Write it in words: ' - ' for a dash, '...' for an"
					   " ellipsis, and STOP: / KEY: / NOTE: as the pattern corpus does for its marks.";
		}
	}

	EXPECT_GT(scanned, 0u) << "no MCP sources found under " << engine.ToStdString()
		<< " - the layout must have moved, and the rule is guarding nothing";
}

TEST(McpToolContract, EveryTool_PublishesAnObjectSchema)
{
	for (const ibMcpTool* tool : ibMcpTools()) {

		const ibDataNode schema = SchemaOf(tool);

		const ibDataValue* type = schema.FindField(wxT("type"));
		ASSERT_NE(type, nullptr) << tool->GetName().ToStdString() << " publishes no schema type";
		ASSERT_EQ(type->Kind(), ibDataKind::String);
		EXPECT_EQ(type->AsString(), wxT("object"))
			<< tool->GetName().ToStdString() << " must take a named-argument object";
	}
}

// 🛑 THE ENVELOPE'S OWN SCHEMA, because everything else is behind it.
//
// `mcp_call` declared `arguments` as a STRING while the server reads it as a NODE
// (ibMcpServer::Answer → `given->FindChild("arguments")`). A client that believes the schema it is
// handed — which is what a correct one does — encoded the arguments as JSON text, FindChild saw
// nothing, and every tool answered about its own first required argument: "chat_say needs 'text',
// and it did not come", of a call that carried it. Not one verb was reachable, and the refusal
// pointed at the letter while the fault was in the envelope (found on the first connection of the
// first session, 2026-09-02).
//
// Pinned by NAME here, alone among these tests, because this is not a rule every tool owes its
// caller — it is the single door they all arrive through, and its two arguments have exactly one
// correct shape each.
TEST(McpToolContract, TheEnvelope_TakesItsArgumentsAsAnObject)
{
	const ibMcpTool* envelope = ibFindMcpTool(wxT("mcp_call"));
	ASSERT_NE(envelope, nullptr) << "the envelope every deferred tool is called through is missing";

	const ibDataNode schema = SchemaOf(envelope);
	const ibDataNode* properties = schema.FindChild(wxT("properties"));
	ASSERT_NE(properties, nullptr);

	const ibDataNode* arguments = properties->FindChild(wxT("arguments"));
	ASSERT_NE(arguments, nullptr) << "mcp_call declares no `arguments`";

	const ibDataValue* type = arguments->FindField(wxT("type"));
	ASSERT_NE(type, nullptr);
	EXPECT_EQ(type->AsString(), wxT("object"))
		<< "mcp_call's `arguments` is read with FindChild and must be declared an object";

	// And the name beside it is a plain name, not a structure — the other half of the pair, so a
	// fix to one cannot silently take the other with it.
	const ibDataNode* target = properties->FindChild(wxT("tool"));
	ASSERT_NE(target, nullptr) << "mcp_call declares no `tool`";

	const ibDataValue* targetType = target->FindField(wxT("type"));
	ASSERT_NE(targetType, nullptr);
	EXPECT_EQ(targetType->AsString(), wxT("string"));
}

TEST(McpToolContract, EveryRequiredArgument_IsAlsoDeclared)
{
	// ⭐ THE ONE THAT MATTERS MOST. A schema that REQUIRES a name it never
	// DECLARES is a contradiction the caller cannot resolve: it is told the
	// argument is mandatory and given nothing about what to put there — and
	// since 2026-08-31 the server refuses undeclared names, so such a tool
	// could not be called at all. Asked of every tool, so a new one cannot
	// introduce it.
	for (const ibMcpTool* tool : ibMcpTools()) {

		const ibDataNode schema = SchemaOf(tool);
		const std::set<wxString> declared = DeclaredArguments(schema);

		for (const wxString& name : RequiredArguments(schema)) {
			EXPECT_TRUE(declared.count(name) != 0)
				<< tool->GetName().ToStdString() << " requires '" << name.ToStdString()
				<< "' and never declares it";
		}
	}
}

// ⭐ AN ARGUMENT A TOOL READS MUST BE ONE IT DECLARES. picture_set takes an image as base64 in `data` and
// refuses it without a `name` — but `name` was read (ArgName) and never listed, so the server refused
// the name as undeclared and the route was unusable. EveryRequiredArgument_IsAlsoDeclared cannot see
// this: `name` is required only WHEN `data` is given, so it is not in the schema's required list.
// The tool is named because that is the only place the fact lives; the general shape of the rule
// (a declared argument the tool reads) cannot be asked of a tool from outside.
TEST(McpToolContract, PictureSet_DeclaresTheNameItAsksForWhenTheImageComesAsData)
{
	const ibMcpTool* tool = ibFindMcpTool(wxT("picture_set"));
	ASSERT_NE(tool, nullptr);

	const std::set<wxString> declared = DeclaredArguments(SchemaOf(tool));
	for (const wxString& name : { wxT("id"), wxT("engine"), wxT("configuration"), wxT("data"), wxT("svg"),
		wxT("width"), wxT("height"), wxT("name") })
		EXPECT_TRUE(declared.count(name) != 0) << "picture_set does not declare '" << wxString(name).ToStdString() << "'";
}

// The same fact for the drawing verb: it refuses without `svg` and without `name`, and sizes by `width` /
// `height` - all four read in Call, none of them in the required list.
TEST(McpToolContract, PictureFromSvg_DeclaresEverythingItReads)
{
	const ibMcpTool* tool = ibFindMcpTool(wxT("picture_from_svg"));
	ASSERT_NE(tool, nullptr);

	const std::set<wxString> declared = DeclaredArguments(SchemaOf(tool));
	for (const wxString& name : { wxT("svg"), wxT("width"), wxT("height"), wxT("name") })
		EXPECT_TRUE(declared.count(name) != 0) << "picture_from_svg does not declare '" << wxString(name).ToStdString() << "'";
}

TEST(McpToolContract, EveryDeclaredArgument_SaysWhatItIsAndWhatItMeans)
{
	for (const ibMcpTool* tool : ibMcpTools()) {

		const ibDataNode schema = SchemaOf(tool);
		const ibDataNode* properties = schema.FindChild(wxT("properties"));
		if (properties == nullptr)
			continue;   // a verb that takes no arguments — an ordinary shape

		for (const auto& entry : properties->Properties()) {

			ASSERT_EQ(entry.second.Kind(), ibDataKind::Child)
				<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString();

			const std::shared_ptr<ibDataNode>& argument = entry.second.AsChild();
			ASSERT_NE(argument, nullptr);

			// ⭐ EXCEPT WHERE "ANY VALUE" IS THE HONEST ANSWER, and that is not a hole
			// in the rule. `metadata_set` writes an ATTRIBUTE, and an attribute is a
			// number, a string, a date, a boolean or a reference depending on which
			// one it is; the report filters take the same. Naming one type there
			// would be a lie, and naming all of them is the same statement spelled
			// longer. JSON Schema says "any" by omitting `type`, so the argument that
			// declares Kind::Any omits it — deliberately, and only it may.
			//
			// What such an argument still owes the caller is the SENTENCE below: with
			// no type to lean on, the description is the whole of what it says about
			// itself, which is why that check stays unconditional.
			const bool takesAnyValue = KindOfArgument(tool, entry.first) ==
				ibMcpTool::ibMcpArgument::Kind::Any;

			const ibDataValue* type = argument->FindField(wxT("type"));
			if (!takesAnyValue) {
				EXPECT_NE(type, nullptr)
					<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString()
					<< " has no type";
			}
			else {
				EXPECT_EQ(type, nullptr)
					<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString()
					<< " takes any value, so it must not publish one type as if it were the only one";
			}

			// ⭐ AND WHAT IT MEANS, not only its type. "integer" tells a caller
			// nothing about whether a number is a NodeId, a row or a length —
			// which is exactly how three arguments were guessed wrong in one
			// session.
			const ibDataValue* description = argument->FindField(wxT("description"));
			ASSERT_NE(description, nullptr)
				<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString()
				<< " is undescribed";
			ASSERT_EQ(description->Kind(), ibDataKind::String);
			EXPECT_FALSE(description->AsString().IsEmpty())
				<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString();
		}
	}
}

// ⭐⭐ A NAME SAID IN PROSE IS STILL A NAME, and it is the last one nothing was watching.
//
// Two kinds of name are single-sourced now: an argument is declared once as an ibMcpArgument and
// read through that same object, and a structure is written and read by one ib*Memory pair. What
// neither of those covers is a description that MENTIONS one — "ask metadata_set first", "the
// answer lists them under `choices`". That mention is a second spelling in prose, and prose is
// what nothing recompiles.
//
// It is not hypothetical: eight tools were renamed on 2026-09-01 and one description went on
// naming `metadata_synonym`, a verb that had been collapsed the same day. A caller reading it is
// sent to a door that is not there.
//
// So the rule, mechanically: a back-quoted lower_snake_case word inside a tool's description must
// be either a REGISTERED TOOL or an argument THAT TOOL declares. Anything else is a name that has
// drifted or was never right.
TEST(McpToolContract, EveryNameSaidInADescription_IsANameThatExists)
{
	for (const ibMcpTool* tool : ibMcpTools()) {

		// What this tool itself declares — its own arguments are fair game to name.
		std::set<wxString> declared;
		for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments())
			declared.insert(argument.Name());

		const wxString said = tool->GetDescription();

		size_t at = said.find(wxT('`'));
		while (at != wxString::npos) {

			const size_t close = said.find(wxT('`'), at + 1);
			if (close == wxString::npos)
				break;

			const wxString word = said.Mid(at + 1, close - at - 1);
			at = said.find(wxT('`'), close + 1);

			// Only the shape a verb has: lower_snake_case with a family prefix. A back-quoted
			// `Periodicity` is a PROPERTY and a back-quoted `all` is a word of an argument's own
			// vocabulary — neither is addressed to the registry.
			if (word.Find(wxT('_')) == wxNOT_FOUND)
				continue;

			bool looksLikeAVerb = true;
			for (size_t index = 0; index < word.length(); ++index) {
				const wxUniChar symbol = word[index];
				if (!((symbol >= wxT('a') && symbol <= wxT('z')) || symbol == wxT('_')))
					looksLikeAVerb = false;
			}
			if (!looksLikeAVerb || word.StartsWith(wxT("m_")))
				continue;

			EXPECT_TRUE(ibFindMcpTool(word) != nullptr || declared.count(word) != 0)
				<< tool->GetName().ToStdString() << " names '" << word.ToStdString()
				<< "', which is neither a tool nor one of its own arguments";
		}
	}
}

TEST(McpToolContract, EveryTool_SaysWhatItIsDoingInAPersonsWords)
{
	// The activity line is what the designer's window and the registration
	// journal both show. Asked with EMPTY arguments on purpose: it is called
	// before a tool has run, and a tool that only phrases itself when its
	// arguments are complete would leave the record blank for a refused call —
	// which is the record most worth having.
	const ibDataNode nothing;

	for (const ibMcpTool* tool : ibMcpTools()) {
		EXPECT_FALSE(tool->GetActivity(nothing).IsEmpty())
			<< tool->GetName().ToStdString() << " cannot say what it is doing";
	}
}

//---------------------------------------------------------------------------
// the wire vocabulary
//---------------------------------------------------------------------------

TEST(McpToolContract, EveryToolName_IsSpelledTheWayTheProtocolSpellsThem)
{
	// lower_snake_case, and a FAMILY prefix before the first underscore. A
	// caller reads tools/list as a map: one entry spelled differently teaches it
	// that there is no rule, and it starts guessing.
	for (const ibMcpTool* tool : ibMcpTools()) {

		const wxString name = tool->GetName();

		for (size_t index = 0; index < name.length(); ++index) {
			const wxUniChar symbol = name[index];
			const bool allowed = (symbol >= wxT('a') && symbol <= wxT('z')) || symbol == wxT('_');
			EXPECT_TRUE(allowed)
				<< "'" << name.ToStdString() << "' is not lower_snake_case";
		}

		EXPECT_NE(name.Find(wxT('_')), wxNOT_FOUND)
			<< "'" << name.ToStdString() << "' names no family";
		EXPECT_FALSE(name.StartsWith(wxT("_")));
		EXPECT_FALSE(name.EndsWith(wxT("_")));
	}
}

//---------------------------------------------------------------------------
// the schema is held to its word — for every tool, by walking the registry
//---------------------------------------------------------------------------
//
// 🛑⭐⭐ A SWEEP IS A SESSION; THIS IS A SUITE. Driving all ninety-odd tools by hand on 2026-09-01
// found real defects — a required argument nobody checked, a tool that crashed the designer with
// empty hands — and left nothing behind that would find the NEXT one. The registry enumerates
// itself, so the questions can be asked of every tool that exists and every tool added later,
// which is the only thing that keeps ninety-six honest when they become a hundred and fifty.

TEST(McpToolContract, EveryRequiredArgument_IsRefusedWhenItDoesNotCome)
{
	// The gate that answers this is ibMcpMissingArgument (mcpTool.cpp), and it is CALLED here:
	// a tool that declares something required must not be reachable without it. `form_accepts` reached the control factory with an empty name, tripped an assert
	// and took the designer down (2026-09-01) — the class, not that one tool.
	for (const ibMcpTool* tool : ibMcpTools()) {

		const std::vector<wxString> required = RequiredArguments(SchemaOf(tool));
		if (required.empty())
			continue;

		// THE GATE ITSELF, not the schema compared with itself. An earlier version of this test
		// checked that every required name is also declared — true, and not what the name of the
		// test promises. The gate could not be called then: it was `static` inside the server.
		const ibDataNode emptyHands;

		EXPECT_FALSE(ibMcpMissingArgument(tool, emptyHands).IsEmpty())
			<< tool->GetName().ToStdString()
			<< " declares a required argument and is reachable with empty hands";

		// …and it names ONE OF ITS OWN, so the refusal a caller reads points at something they can
		// send rather than at a name from somewhere else.
		const wxString named = ibMcpMissingArgument(tool, emptyHands);
		EXPECT_TRUE(std::find(required.begin(), required.end(), named) != required.end())
			<< tool->GetName().ToStdString() << " refuses by naming '" << named.ToStdString()
			<< "', which is not among the arguments it publishes as required";
	}
}

TEST(McpToolContract, EveryArgument_IsHeldToTheShapeItPublishes)
{
	// ⭐ THE READERS ANSWER WITH A DEFAULT FOR THE WRONG SHAPE — `Whole()` gives 0, `Flag()` gives
	// false, `Text()` gives empty — so an argument of the wrong kind used to reach the tool as a
	// plausible value nobody sent. Asked of every declared argument of every tool.
	for (const ibMcpTool* tool : ibMcpTools()) {
		for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {

			using Kind = ibMcpTool::ibMcpArgument::Kind;

			// A string where the schema says integer / boolean / array / object.
			// Text takes one by definition — and so does Any, which is the point of
			// it: `metadata_set`'s value is whatever the attribute's type is, and a
			// string is one of the shapes it legitimately arrives in. Refusing one
			// there would be the defect, not the check.
			if (argument.KindOf() == Kind::Text || argument.KindOf() == Kind::Any)
				continue;

			ibDataNode wrong;
			wrong.SetValue(argument.Name(), wxString(wxT("plainly a string")));

			EXPECT_FALSE(ibMcpArgumentFault(tool, wrong).IsEmpty())
				<< tool->GetName().ToStdString() << "'s '" << argument.Name().ToStdString()
				<< "' is published as something other than a string and took one";
		}
	}
}

TEST(McpToolContract, EveryClosedSet_RefusesAWordOutsideIt)
{
	// `enum` is published in the schema; a word outside it used to reach the tool, where each one
	// decided for itself — some refused, some took a default and carried on with a choice nobody
	// made. Asked of every argument that declares a set.
	for (const ibMcpTool* tool : ibMcpTools()) {
		for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {

			if (argument.Values().empty())
				continue;

			ibDataNode outside;
			outside.SetValue(argument.Name(), wxString(wxT("no-tool-declares-this-word")));

			EXPECT_FALSE(ibMcpArgumentFault(tool, outside).IsEmpty())
				<< tool->GetName().ToStdString() << "'s '" << argument.Name().ToStdString()
				<< "' publishes a closed set and took a word from outside it";

			// …and the words it DOES publish are taken, or the set is a wall rather than a contract.
			for (const wxString& word : argument.Values()) {
				ibDataNode inside;
				inside.SetValue(argument.Name(), word);
				EXPECT_TRUE(ibMcpArgumentFault(tool, inside).IsEmpty())
					<< tool->GetName().ToStdString() << " refuses '" << word.ToStdString()
					<< "', which it publishes as one of the words it takes";
			}
		}
	}
}

//---------------------------------------------------------------------------
// how a query is matched — the one rule both finders share
//---------------------------------------------------------------------------
//
// ⭐ WHAT IS PINNED HERE IS THE COUNT, not a verdict. Requiring every word to land is what made
// live questions answer nothing — "moved back and forth" found no passage though transfers are
// written up in full — and nothing is the one answer a finder must not give lightly: it reads as
// "this platform has no such thing", and the caller goes and builds it by hand.

TEST(McpSearch, AWordTooMany_NarrowsTheAnswerInsteadOfErasingIt)
{
	const wxString text = wxT("Stock is moved between warehouses with a transfer document.");

	size_t asked = 0;

	// Everything landed — the caller asked precisely, and precision still wins.
	EXPECT_EQ(ibMcpWordsFound(text, wxT("moved warehouses"), &asked), 2u);
	EXPECT_EQ(asked, 2u);

	// One word of theirs is not ours. The old rule answered 0 here, which is the defect.
	EXPECT_EQ(ibMcpWordsFound(text, wxT("stock moved back and forth"), &asked), 2u);
	EXPECT_EQ(asked, 5u);

	// Nothing of it is here at all — and THAT is still an honest nothing.
	EXPECT_EQ(ibMcpWordsFound(text, wxT("payroll vacation"), nullptr), 0u);
}

TEST(McpSearch, AWordIsMetAtItsStem_SoTheFormOfTheNounDoesNotDecide)
{
	const wxString text = wxT("Distribution of overheads by a base, lot by lot.");

	EXPECT_GT(ibMcpWordsFound(text, wxT("distributing"), nullptr), 0u);
	EXPECT_GT(ibMcpWordsFound(text, wxT("bases"), nullptr), 0u);

	// ⚠ AND HERE IS WHERE IT STOPS, which is worth pinning as much as where it works. The floor is
	// FOUR characters, so `lots` is tried as itself and never shortened to `lot` — the text says
	// "lot by lot" and this finds nothing. Three would fix that plural and let `set` reach half of
	// everything, which is not a search. Written down because the first version of this test
	// asserted the nicer story and the code was right (2026-09-02).
	EXPECT_EQ(ibMcpWordsFound(text, wxT("lots"), nullptr), 0u);
	EXPECT_EQ(ibMcpWordsFound(text, wxT("ova"), nullptr), 0u);
}

// 🛑⭐ AND A STEM MAY NOT EAT A THIRD OF THE WORD, which is the other half of the same rule and the
// one that was missing. Under a flat four-character floor `comment` was tried as `comm` and met
// `command` — sixteen tools answered a one-word query and none of them was about comments (measured
// live, 2026-09-02). An inflection changes an ending; a match that needs a third of the word gone is
// a different word.
TEST(McpSearch, AStemKeepsTwoThirdsOfItsWord_SoOneWordDoesNotBecomeAnother)
{
	const wxString commands = wxT("A command bar holds the common commands of a section.");

	EXPECT_EQ(ibMcpWordsFound(commands, wxT("comment"), nullptr), 0u);
	EXPECT_EQ(ibMcpWordsFound(commands, wxT("commission"), nullptr), 0u);

	// …while the word itself, and an ending on it, still land.
	EXPECT_GT(ibMcpWordsFound(commands, wxT("command"), nullptr), 0u);
	EXPECT_GT(ibMcpWordsFound(commands, wxT("commands"), nullptr), 0u);

	// And the inflections the rule exists for are untouched: two thirds of a long word is still
	// plenty of room for an ending to differ.
	const wxString text = wxT("Distribution of overheads by a base, lot by lot.");
	EXPECT_GT(ibMcpWordsFound(text, wxT("distributing"), nullptr), 0u);
	EXPECT_GT(ibMcpWordsFound(text, wxT("bases"), nullptr), 0u);
}

TEST(McpSearch, APatternIsReadAsOne_ButAnOrdinaryBracketIsNot)
{
	const wxString text = wxT("Write-off by FIFO takes the oldest lot first.");

	EXPECT_TRUE(ibMcpIsRegex(wxT("lot|batch|fifo")));
	EXPECT_GT(ibMcpWordsFound(text, wxT("lot|batch|fifo"), nullptr), 0u);
	EXPECT_EQ(ibMcpWordsFound(text, wxT("payroll|vacation"), nullptr), 0u);

	// ⚠ THE CASE THAT DECIDED WHERE THE LINE IS. Nearly any sentence compiles as a pattern, and
	// "cost adjustment (RAUZ)" read as one matches nothing — the brackets silently became
	// grouping. Ordinary punctuation must therefore not switch modes.
	EXPECT_FALSE(ibMcpIsRegex(wxT("cost adjustment (RAUZ)")));
	EXPECT_FALSE(ibMcpIsRegex(wxT("how much is left?")));
	EXPECT_FALSE(ibMcpIsRegex(wxT("Catalog.Warehouses")));
}

//---------------------------------------------------------------------------
// the caller's own clipboard
//---------------------------------------------------------------------------

TEST(McpClipboard, AFreshSlot_HoldsNothing)
{
	ibMcpClipboardSlot& slot = ibMcpClipboard(wxT("test.fresh"));

	EXPECT_TRUE(slot.IsEmpty());
	EXPECT_EQ(slot.m_kind, ibMcpClipboardKind::None);
}

TEST(McpClipboard, TheDefaultSlot_IsTheOneNamedByNothing)
{
	// "One buffer" and "several buffers" are the same code path with the same
	// rules — the default is a NAME, not a separate variable.
	ibMcpClipboardSlot& byNothing = ibMcpClipboard();
	ibMcpClipboardSlot& byName = ibMcpClipboard(wxT("default"));

	EXPECT_EQ(&byNothing, &byName);
}

TEST(McpClipboard, TwoNames_AreTwoSlots)
{
	ibMcpClipboardSlot& first = ibMcpClipboard(wxT("test.first"));
	ibMcpClipboardSlot& second = ibMcpClipboard(wxT("test.second"));

	EXPECT_NE(&first, &second);
}

TEST(McpClipboard, ASlot_KeepsWhatWasPutInItUntilItIsReplaced)
{
	ibMcpClipboardSlot& slot = ibMcpClipboard(wxT("test.keeps"));

	slot.m_kind = ibMcpClipboardKind::Metadata;
	slot.m_name = wxT("GoodsReceipt");
	slot.m_what = wxT("Document");
	slot.m_payload.SetValue(wxT("marker"), wxString(wxT("first")));

	// The SAME slot is handed back — a copy would make copy/paste two verbs over
	// two different things.
	ibMcpClipboardSlot& again = ibMcpClipboard(wxT("test.keeps"));

	EXPECT_FALSE(again.IsEmpty());
	EXPECT_EQ(again.m_kind, ibMcpClipboardKind::Metadata);
	EXPECT_EQ(again.m_name, wxT("GoodsReceipt"));
	EXPECT_EQ(again.m_what, wxT("Document"));
	EXPECT_EQ(again.m_payload.GetValue<wxString>(wxT("marker")), wxT("first"));
}

TEST(McpClipboard, EveryKind_NamesItselfForARefusal)
{
	// The refusal a caller reads when it pastes a control into a metadata tree
	// is built from these words, so an unnamed kind would produce a sentence
	// with a hole in it.
	EXPECT_FALSE(ibMcpClipboardKindName(ibMcpClipboardKind::None).IsEmpty());
	EXPECT_FALSE(ibMcpClipboardKindName(ibMcpClipboardKind::Metadata).IsEmpty());
	EXPECT_FALSE(ibMcpClipboardKindName(ibMcpClipboardKind::Control).IsEmpty());
	EXPECT_FALSE(ibMcpClipboardKindName(ibMcpClipboardKind::Cells).IsEmpty());

	EXPECT_NE(ibMcpClipboardKindName(ibMcpClipboardKind::Metadata),
	          ibMcpClipboardKindName(ibMcpClipboardKind::Control));
}

TEST(McpClipboard, OnlyFilledSlots_AreListed)
{
	ibMcpClipboardSlot& filled = ibMcpClipboard(wxT("test.listed"));
	filled.m_kind = ibMcpClipboardKind::Cells;

	ibMcpClipboard(wxT("test.notlisted"));   // touched, still empty

	const std::vector<wxString> slots = ibMcpClipboardSlots();

	EXPECT_NE(std::find(slots.begin(), slots.end(), wxT("test.listed")), slots.end());
	EXPECT_EQ(std::find(slots.begin(), slots.end(), wxT("test.notlisted")), slots.end());
}
