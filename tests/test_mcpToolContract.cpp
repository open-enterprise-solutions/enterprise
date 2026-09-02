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

#include <map>
#include <set>

namespace {

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

			const ibDataValue* type = argument->FindField(wxT("type"));
			EXPECT_NE(type, nullptr)
				<< tool->GetName().ToStdString() << " / " << entry.first.ToStdString()
				<< " has no type";

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
	EXPECT_GT(ibMcpWordsFound(text, wxT("lots"), nullptr), 0u);

	// Three letters would let `set` reach half of everything, so the floor is four: `over` is a
	// stem of "overheads" and matches, `ove` is never tried on its own.
	EXPECT_EQ(ibMcpWordsFound(text, wxT("ova"), nullptr), 0u);
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
