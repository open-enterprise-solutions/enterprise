////////////////////////////////////////////////////////////////////////////
//	Description : the tool registry
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metadataConfiguration.h"
#include "backend/propertyManager/property/propertyString.h"   // ibPropertyTString — the translatable one
#include "backend/propertyManager/property/variant/variantTranslate.h"   // …and the cell it holds them in
#include "backend/propertyManager/property/propertyType.h"     // ibPropertyType — the one with a verb of its own
#include "backend/propertyManager/propertyObject.h"            // the whole property list, walked below

#include "backend/backend_localization.h"                      // a caption is an array by language
#include "backend/metaCollection/metaLanguageObject.h"         // …and the languages are the config's own
#include "backend/metaCollection/metaModuleObject.h"           // ibValueMetaObjectModuleBase — the module behind an id

#include <wx/tokenzr.h>   // a query is words, and the finder counts how many of them landed
#include <wx/regex.h>     // …unless it is written as a pattern, which is also a way to ask
#include <wx/log.h>       // a query that does not compile is not the person's error to read

#include <memory>
#include <atomic>   // how deep the host is in modal dialogs — read from any thread
#include <mutex>

#include <map>   // property name -> the category it was declared into
#include "backend/restructureInfo.h"              // the ledger an object complains into

namespace {

// FUNCTION-LOCAL, deliberately. Tools register during STATIC CONSTRUCTION —
// and not all from this binary: the front registers its own (a form's verbs
// belong to the front that owns forms) when it loads. A namespace-scope vector
// would not necessarily be constructed when the first registrar of another
// module runs; a function-local static is built on first use, which is by
// definition before it is written to.
std::vector<const ibMcpTool*>& Store()
{
	static std::vector<const ibMcpTool*> s_tools;
	return s_tools;
}

} // namespace

namespace {

// The schema's own vocabulary, in one place. Not domain words — the words JSON Schema is written
// in, which every tool used to spell for itself.
const wxString kSchemaType       = wxT("type");
const wxString kSchemaObject     = wxT("object");
const wxString kSchemaProperties = wxT("properties");
const wxString kSchemaRequired   = wxT("required");

wxString SchemaWordFor(ibMcpTool::ibMcpArgument::Kind kind)
{
	switch (kind) {
		case ibMcpTool::ibMcpArgument::Kind::Whole: return wxT("integer");
		case ibMcpTool::ibMcpArgument::Kind::Flag:  return wxT("boolean");
		case ibMcpTool::ibMcpArgument::Kind::Many:  return wxT("array");
		case ibMcpTool::ibMcpArgument::Kind::Node:  return wxT("object");
		// Deliberately EMPTY, and the writer leaves `type` out when it is: JSON Schema reads a
		// missing type as "any type", which is exactly what this argument means. Naming one here
		// would be picking a lie to tell.
		case ibMcpTool::ibMcpArgument::Kind::Any:   return wxString();
		default:                                    return wxT("string");
	}
}
void ibMcpSchemaArgument(ibDataNode& properties, const wxString& name,
	const wxString& type, const wxString& description,
	const std::vector<wxString>& values)
{
	ibDataNode& argument = properties.Child(name);
	// An empty type is Kind::Any — the argument whose type this schema does not decide. Left OUT
	// rather than written blank: JSON Schema reads an absent `type` as "any", and `"type": ""` as
	// a type nobody has.
	if (!type.IsEmpty())
		argument.SetValue(wxT("type"), type);
	argument.SetValue(wxT("description"), description);

	// ⭐ THE WORDS IT ACCEPTS, WHEN THERE IS A FIXED SET OF THEM. A caller that is told the type is
	// "string" and nothing else has to guess the vocabulary and find out by being refused; told
	// the four words, it cannot get it wrong. This is the same argument the platform makes about
	// enumerated PROPERTIES, one layer out.
	if (values.empty())
		return;

	std::vector<ibDataValue> allowed;

	for (const wxString& value : values)
		allowed.push_back(ibDataValue::String(value));

	argument.AddField(wxT("enum"), ibDataValue::Array(allowed));
}


} // namespace

void ibMcpTool::ibMcpArgument::Declare(ibDataNode& schema) const
{
	// (the frame — `type: "object"` — is written by DescribeInput, which owns it: an argument
	//  cannot be the one to say that its tool takes an object, or a tool with no arguments says
	//  nothing at all. See the note there.)

	// 🛑 FOUND-OR-MADE, and it has to be said out loud: ibDataNode::Child ALWAYS MAKES A FRESH ONE.
	// It is the SAVE half of the Child/FindChild pair — "a fresh composite sub-node under `name`,
	// returned for the caller to fill" — so calling it once per argument had each argument replace
	// the `properties` node the one before it had just filled, and a tool published its LAST
	// argument only.
	//
	// It compiled, it answered, and every schema was silently one entry long: `mcp_search` offered
	// `schema` and not `query`, `mcp_call` offered `arguments` and not `tool` — so the
	// undeclared-argument gate then refused the very arguments those tools read. Caught by asking
	// the RUNNING server what it publishes (2026-09-01); nothing in the source says which of the
	// pair a name belongs to.
	ibDataNode* properties = schema.FindChild(kSchemaProperties);
	if (properties == nullptr)
		properties = &schema.Child(kSchemaProperties);

	ibMcpSchemaArgument(*properties, m_name,
		SchemaWordFor(m_kind), m_description, m_values);

	// ⭐ AND WHAT THE STRUCTURE LOOKS LIKE, when there is one — an EMPTY instance, written by
	// whatever writes that thing to a file. A caller reads the example, fills it in and sends it
	// back, and the reading half (ReadNode) is the same pair, so the round trip is closed by
	// construction rather than by two descriptions agreeing.
	if (m_shape) {
		ibDataValue empty;

		// The argument's OWN node, the one ibMcpSchemaArgument just filled — found, not remade, for
		// the same reason as above: Child() here would throw away the type and the description and
		// leave an entry that is nothing but an example.
		ibDataNode* argument = properties->FindChild(m_name);

		if (argument != nullptr && m_shape(empty))
			argument->AddField(wxT("example"), empty);
	}

	if (!m_required)
		return;

	// ⭐ THE REQUIRED LIST CANNOT NAME AN ARGUMENT THAT DOES NOT EXIST, because the same call makes
	// both entries. Written by hand it was a second spelling of the name, three lines further down,
	// and a caller told that `kind` is required while the property is called `type` has been sent
	// looking for something the tool will never read.
	std::vector<ibDataValue> required;

	if (const ibDataValue* already = schema.FindField(kSchemaRequired))
		required = already->AsArray();

	required.push_back(ibDataValue::String(m_name));
	schema.SetField(kSchemaRequired, ibDataValue::Array(required));
}

bool ibMcpTool::ibMcpArgument::Given(const ibDataNode& params) const
{
	return params.FindField(m_name) != nullptr;
}

wxString ibMcpTool::ibMcpArgument::Text(const ibDataNode& params) const
{
	return params.GetValue<wxString>(m_name);
}

s64 ibMcpTool::ibMcpArgument::Whole(const ibDataNode& params) const
{
	const ibDataValue* given = params.FindField(m_name);
	return given != nullptr && given->Kind() == ibDataKind::Number ? given->AsInt() : 0;
}

bool ibMcpTool::ibMcpArgument::Flag(const ibDataNode& params) const
{
	return params.GetValue<bool>(m_name);
}

// The three the shared doors below read. Declared beside their readers, which is what makes the
// names legitimate — see the note in mcpTool.h.
const ibMcpTool::ibMcpArgument& ibMcpIdArgument()
{
	static const ibMcpTool::ibMcpArgument s_id(wxT("id"), ibMcpTool::ibMcpArgument::Kind::Whole,
		ibMcpText("The object, as NodeId - metadata_list and metadata_get give it. It survives a rename, "
		  "which a name does not."), /*required*/ true);
	return s_id;
}

const ibMcpTool::ibMcpArgument& ibMcpValueArgument()
{
	static const ibMcpTool::ibMcpArgument s_value(wxT("value"), ibMcpTool::ibMcpArgument::Kind::Any,
		ibMcpText("What to put there, IN ITS OWN TYPE: true/false for a switch, a number for a number, the "
		  "word for a property with a closed set of them, the NAME or the id of the object for a "
		  "relationship. For a composite one, send back the same shape you were given."));
	return s_value;
}

const ibMcpTool::ibMcpArgument& ibMcpLanguageArgument()
{
	static const ibMcpTool::ibMcpArgument s_language(wxT("language"),
		ibMcpTool::ibMcpArgument::Kind::Text,
		ibMcpText("WHICH LANGUAGE this caption is written in - the two-letter code of a declared Language "
		  "(`en`, `ru`, `uk`). Fills that one and leaves the others alone.\n"
		  "OMITTING IT IS NOT 'ALL LANGUAGES': the value goes into the configuration's OWN "
		  "language, which in a fresh base is English - so a Russian caption written without this "
		  "is stored as the English one, invisible to a Russian reader and wrong for the next "
		  "developer. Say it every time; metadata_get on Synonym answers the whole set."));
	return s_language;
}

// A tool that has not been converted declares nothing here and writes its own DescribeInput; the
// empty list is what says so.
const std::vector<ibMcpTool::ibMcpArgument>& ibMcpTool::Arguments() const
{
	static const std::vector<ibMcpArgument> s_none;
	return s_none;
}

void ibMcpTool::DescribeInput(ibDataNode& schema) const
{
	// ⭐ THE FRAME BELONGS TO THE TOOL, NOT TO ITS ARGUMENTS. It used to be written by
	// ibMcpArgument::Declare, which is fine for every tool that HAS an argument and wrong for
	// every tool that does not: with nothing to declare, nothing ran, and the tool published an
	// EMPTY node — not `{"type":"object"}`, but no schema at all. A machine caller reading the
	// tool list is then told the shape of the call by nothing (`chat_take`, CI 2026-09-02).
	//
	// A verb taking no arguments is an ordinary shape and says so: an object schema with no
	// `properties`, which is exactly "an object, and I ask nothing of it". `properties` is
	// deliberately NOT created empty here — its ABSENCE is what the undeclared-argument gate
	// reads as "this tool has no opinion", and an empty one would start refusing arguments a
	// hand-written Call may still read.
	schema.SetField(kSchemaType, ibDataValue::String(kSchemaObject));

	for (const ibMcpArgument& argument : Arguments())
		argument.Declare(schema);
}


void ibRegisterMcpTool(const ibMcpTool* tool)
{
	if (tool == nullptr)
		return;

	// A NAME IS CLAIMED ONCE. Two tools under one name would make which one
	// answers depend on link order — the kind of difference that shows up as a
	// caller getting a plausible answer from the wrong door.
	const wxString name = tool->GetName();
	for (const ibMcpTool* registered : Store()) {
		if (registered->GetName() == name) {
			wxASSERT_MSG(false, wxT("an MCP tool is registered twice under one name"));
			return;
		}
	}

	Store().push_back(tool);
}

// WHAT THE HOST IS BUSY WITH, and how deep. A COUNT rather than a flag: modals nest (a warning
// raised from inside a dialog), and a flag would come back clear the moment the inner one closed
// while the outer still stood.
static std::atomic<int>& BusyDepth()
{
	static std::atomic<int> s_depth(0);
	return s_depth;
}

static std::mutex& BusyMutex()
{
	static std::mutex s_mutex;
	return s_mutex;
}

static wxString& BusyTitle()
{
	static wxString s_title;
	return s_title;
}

void ibMcpBusyEnter(const wxString& what)
{
	{
		std::lock_guard<std::mutex> lock(BusyMutex());
		if (BusyDepth().load() == 0 || !what.IsEmpty())
			BusyTitle() = what;   // the innermost one that named itself is the one in front
	}
	BusyDepth()++;
}

void ibMcpBusyLeave()
{
	if (BusyDepth().fetch_sub(1) <= 1) {
		std::lock_guard<std::mutex> lock(BusyMutex());
		BusyTitle().Clear();
	}
}

wxString ibMcpBusyWith()
{
	if (BusyDepth().load() <= 0)
		return wxEmptyString;

	std::lock_guard<std::mutex> lock(BusyMutex());

	// A dialog with no title is still a dialog in the way — saying so beats answering "free".
	return BusyTitle().IsEmpty() ? wxString(wxT("a dialog")) : BusyTitle();
}

// ⭐ THE GREETING'S OWN VERBS. Each one is here because the greeting cannot be made without it:
// chat_history to know whether something is already in progress, platform_state and metadata_tree
// to have anything true to SAY about the configuration, mcp_search to find the verb at all, and
// chat_say to speak. Nothing else — the point is that a person learns somebody is here BEFORE
// their base is read, not after.
const std::vector<wxString>& ibMcpGreetingVerbs()
{
	static const std::vector<wxString> s_verbs = {
		wxT("chat_say"), wxT("chat_history"), wxT("chat_take"),
		wxT("platform_state"), wxT("metadata_tree"), wxT("mcp_search") };

	return s_verbs;
}

bool ibMcpRunsBeforeGreeting(const wxString& toolName)
{
	for (const wxString& verb : ibMcpGreetingVerbs())
		if (verb.IsSameAs(toolName, false))
			return true;

	return false;
}


// THE SAME SHAPE FOR THE CHECKS — a function-local store, filled during static construction by
// whichever DLL is linked. No name to claim here: an audit is not addressed, it is asked.
static std::vector<const ibMcpAudit*>& AuditStore()
{
	static std::vector<const ibMcpAudit*> s_audits;
	return s_audits;
}

void ibRegisterMcpAudit(const ibMcpAudit* audit)
{
	if (audit != nullptr)
		AuditStore().push_back(audit);
}

const std::vector<const ibMcpAudit*>& ibMcpAudits()
{
	return AuditStore();
}

const std::vector<const ibMcpTool*>& ibMcpTools()
{
	return Store();
}

const ibMcpTool* ibFindMcpTool(const wxString& name)
{
	for (const ibMcpTool* tool : Store()) {
		if (tool->GetName() == name)
			return tool;
	}

	return nullptr;
}

wxString ibMcpNameOf(const ibDataNode& params, const wxString& field)
{
	const s32 id = params.GetValue<s32>(field);
	if (id <= 0)
		return wxEmptyString;

	if (activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {
		if (ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id))
			return object->GetName();
	}

	return wxString::Format(wxT("#%i"), (int)id);
}

namespace {

// ONE ENTRY IS ENOUGH OF A CACHE. A search runs ONE query over hundreds of texts — every pattern,
// every topic, every tool description — so compiling it per text is the entire cost of searching.
// Compiled once when the query changes, and the failure is remembered too: a query that is not a
// valid pattern must not be re-attempted three hundred times.
const wxRegEx* CompiledQuery(const wxString& query)
{
	// ⚠ PER THREAD. Tools run on the main thread or on the delivery thread depending on what they
	// need, and a shared cache here would be two searches writing one pointer. A regular
	// expression compiles in microseconds, so a second copy costs nothing worth measuring.
	//
	// ⚠ AND THE OBJECT IS HELD BY VALUE, not through a pointer. A heap block that lives to the end
	// of the process is reported by the CRT as a leak, and a leak line in a test run is a false
	// alarm that never goes away — measured the first time these ran (2026-09-02: 32 bytes,
	// dumped after the last test passed).
	static thread_local wxString s_source;
	static thread_local wxRegEx s_regex;
	static thread_local bool s_tried = false;
	static thread_local bool s_valid = false;

	if (!s_tried || s_source != query) {

		s_source = query;
		s_tried = true;

		// A pattern that does not compile is an ordinary outcome here — the caller wrote words
		// that happen to hold a bracket — so it must not reach the log the person is reading.
		wxLogNull quiet;

		s_valid = s_regex.Compile(query, wxRE_ADVANCED | wxRE_ICASE);
	}

	return s_valid ? &s_regex : nullptr;
}

} // namespace

bool ibMcpIsRegex(const wxString& query)
{
	// ⚠ WHAT MAKES A QUERY A PATTERN is deliberately narrow. Nearly any sentence compiles as a
	// regular expression — "cost adjustment (RAUZ)" compiles, and then matches nothing, because
	// the brackets it happens to contain silently became grouping. Ordinary punctuation therefore
	// does NOT switch modes; only the marks that have no business in prose do.
	static const wxChar* s_marks[] = { wxT("|"), wxT("\\"), wxT("["), wxT("]"),
		wxT(".*"), wxT(".+"), wxT("^"), wxT("$") };

	bool looks = false;
	for (const wxChar* mark : s_marks)
		looks = looks || query.Find(mark) != wxNOT_FOUND;

	return looks && CompiledQuery(query) != nullptr;
}

size_t ibMcpWordsFound(const wxString& haystack, const wxString& query, size_t* asked)
{
	// ⭐ A PATTERN IS ANSWERED AS A PATTERN. Whoever is on the other end of this server reads code
	// for a living and will reach for `lot|batch|fifo` the moment plain words disappoint — and
	// under a word search that query is one long word that appears nowhere. It costs one branch to
	// mean what it says instead. Matched whole: a pattern either hit or it did not, so it counts
	// as the single thing that was asked and ranks beside the exact word matches.
	if (ibMcpIsRegex(query)) {

		if (asked != nullptr)
			*asked = 1;

		const wxRegEx* compiled = CompiledQuery(query);
		return (compiled != nullptr && compiled->Matches(haystack)) ? 1 : 0;
	}

	wxStringTokenizer words(query.Lower(), wxT(" \t,"), wxTOKEN_STRTOK);

	const wxString text = haystack.Lower();
	size_t found = 0, total = 0;

	while (words.HasMoreTokens()) {

		const wxString word = words.GetNextToken();
		total++;

		bool present = false;

		// Shortening the QUERY rather than the text: `bases` reaches "base", `distributing`
		// reaches "distribution", and the corpus needs no keyword list beside it.
		//
		// 🛑 HOW MUCH MAY BE CUT IS PROPORTIONAL TO THE WORD, and the flat floor of four was not.
		// A seven-letter word was allowed down to four letters — so `comment` was tried as `comm`
		// and met `command`, `common` and `commission`: sixteen tools answered a one-word query and
		// the one that was meant was not among them (measured 2026-09-02). An inflection changes an
		// ENDING; it does not eat a third of the word.
		//
		// So a stem keeps two thirds of what was typed, and four is still the absolute floor
		// (three lets `set` reach half of everything, which is noise rather than a match). The cost
		// is honest and small: a word whose form differs by more than a third — `deleting` against
		// "deleted" — is no longer met at the stem. It is met by the OTHER words of the same query,
		// which is what asking in a sentence is for.
		const size_t keep = wxMax<size_t>(4, (word.length() * 2 + 2) / 3);

		for (size_t length = word.length(); length >= keep && !present; --length)
			present = text.Find(word.Left(length)) != wxNOT_FOUND;

		if (!present && word.length() < 4)
			present = text.Find(word) != wxNOT_FOUND;

		if (present)
			found++;
	}

	if (asked != nullptr)
		*asked = total;

	return found;
}

ibValueMetaObjectModuleBase* ibMcpModuleOf(ibValueMetaObject* object, wxString& refusal)
{
	if (object == nullptr) {
		refusal = ibMcpText("No such object.");
		return nullptr;
	}

	// ⭐ THE PLATFORM'S OWN CONVERSION, not a bare cast. `ConvertToValue` is the value's answer to
	// "are you one of these" — it unwraps a REFERENCE first, which a cast cannot, and it answers
	// true/false rather than raising the way ConvertToType does outside the designer. Same door
	// every other value question in the engine goes through.
	ibValueMetaObjectModuleBase* module = nullptr;

	if (!object->ConvertToValue(module)) {
		// ⭐ NAMED, AND WITH THE WAY OUT. A caller lands here holding an id from the tree, and the
		// most common reason is the one the tree itself shows: the modules are CHILDREN of an
		// object (ObjectModule, ManagerModule), not the object.
		refusal = wxString::Format(
			ibMcpText("'%s' is not a module. A module is a child of an object - metadata_tree on it "
			  "lists them (ObjectModule, ManagerModule, RecordSetModule) with the id each takes."),
			object->GetName());
		return nullptr;
	}

	return module;
}

wxString ibMcpMissingArgument(const ibMcpTool* tool, const ibDataNode& arguments)
{
	if (tool == nullptr)
		return wxEmptyString;

	// ⚠ ASKED OF THE ARGUMENTS THEMSELVES, not of the published schema — ibMcpTool::ibMcpArgument
	// is where `required` is stated, and Given() is the same both-areas lookup the tool's own body
	// uses, so "present" means here exactly what it means there.
	for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {
		if (argument.IsRequired() && !argument.Given(arguments))
			return argument.Name();
	}

	return wxEmptyString;
}

wxString ibMcpArgumentFault(const ibMcpTool* tool, const ibDataNode& arguments)
{
	if (tool == nullptr)
		return wxEmptyString;

	// What the caller was TOLD each shape is called, so the refusal speaks the schema's words
	// rather than the enum's.
	const auto shapeWord = [](ibMcpTool::ibMcpArgument::Kind kind) -> wxString {
		switch (kind) {
			case ibMcpTool::ibMcpArgument::Kind::Whole: return wxT("integer");
			case ibMcpTool::ibMcpArgument::Kind::Flag:  return wxT("boolean");
			case ibMcpTool::ibMcpArgument::Kind::Many:  return wxT("array");
			case ibMcpTool::ibMcpArgument::Kind::Node:  return wxT("object");
			default: break;
		}
		return wxT("string");
	};

	const auto arrived = [](const ibDataValue* value, const ibDataNode* child) -> wxString {
		if (child != nullptr)
			return wxT("an object");
		if (value == nullptr)
			return wxT("nothing");
		switch (value->Kind()) {
			case ibDataKind::String: return wxT("a string");
			case ibDataKind::Number: return wxT("a number");
			case ibDataKind::Bool:   return wxT("a boolean");
			case ibDataKind::Array:  return wxT("an array");
			case ibDataKind::Child:  return wxT("an object");
			default: break;
		}
		return wxT("an empty value");
	};

	for (const ibMcpTool::ibMcpArgument& argument : tool->Arguments()) {

		const ibDataValue* value = arguments.FindField(argument.Name());
		const ibDataNode*  child = arguments.FindChild(argument.Name());

		// ABSENT IS NOT THIS GATE'S BUSINESS — a required one that never came is answered by
		// ibMcpMissingArgument, in its own words.
		if (value == nullptr && child == nullptr)
			continue;

		const ibMcpTool::ibMcpArgument::Kind kind = argument.KindOf();
		bool fits = true;

		switch (kind) {
			case ibMcpTool::ibMcpArgument::Kind::Whole:
				fits = child == nullptr && value != nullptr && value->Kind() == ibDataKind::Number;
				break;
			case ibMcpTool::ibMcpArgument::Kind::Flag:
				fits = child == nullptr && value != nullptr && value->Kind() == ibDataKind::Bool;
				break;
			case ibMcpTool::ibMcpArgument::Kind::Many:
				fits = child == nullptr && value != nullptr && value->Kind() == ibDataKind::Array;
				break;
			case ibMcpTool::ibMcpArgument::Kind::Node:
				// A node may arrive either way — as a child, or as a value carrying one.
				fits = child != nullptr || (value != nullptr && value->Kind() == ibDataKind::Child);
				break;
			case ibMcpTool::ibMcpArgument::Kind::Any:
				// Whatever came is the right shape, because this gate is not the one that knows.
				// The TOOL checks it against the thing it is for — a property refuses a word it
				// does not have, in its own words, which is a better refusal than "takes string".
				fits = true;
				break;
			default:
				fits = child == nullptr && value != nullptr && value->Kind() == ibDataKind::String;
				break;
		}

		if (!fits) {
			return wxString::Format(
				ibMcpText("'%s' takes %s for '%s', and %s came. Nothing was done."),
				tool->GetName(), shapeWord(kind), argument.Name(), arrived(value, child));
		}

		// ⭐ AND A CLOSED SET IS CLOSED. `enum` is published in the schema, and a word outside it
		// used to reach the tool, where each one decided for itself what to do about it — some
		// refused, some took a default and carried on with a choice nobody made.
		if (argument.Values().empty() || value == nullptr || value->Kind() != ibDataKind::String)
			continue;

		const wxString said = value->AsString();
		bool known = false;
		wxString allowed;

		for (const wxString& word : argument.Values()) {
			known = known || word.IsSameAs(said, false);
			allowed += (allowed.IsEmpty() ? wxString() : wxT(", ")) + word;
		}

		if (!known) {
			return wxString::Format(
				ibMcpText("'%s' is not one of the words '%s' takes for '%s'. It takes: %s. Nothing was done."),
				said, tool->GetName(), argument.Name(), allowed);
		}
	}

	return wxEmptyString;
}

wxString ibMcpLineNaming(const wxString& text, const wxString& name)
{
	if (text.IsEmpty() || name.IsEmpty())
		return wxEmptyString;

	// A NAME IS A WORD OF THE LANGUAGE, so what may not touch it is what may be part of an
	// identifier. A dot on either side is fine and wanted: `Catalog.Goods.Ref` names Goods.
	const auto partOfAWord = [](wxUniChar symbol) {
		return wxIsalnum(symbol) != 0 || symbol == wxT('_');
	};

	const wxString wanted = name.Lower();
	size_t at = 0;

	while (at <= text.length()) {

		const size_t end = text.find(wxT('\n'), at);
		const wxString line = text.Mid(at, (end == wxString::npos ? text.length() : end) - at);
		const wxString lower = line.Lower();

		for (size_t found = lower.find(wanted); found != wxString::npos;
			 found = lower.find(wanted, found + 1)) {

			const bool leftClear = found == 0 || !partOfAWord(lower[found - 1]);
			const size_t after = found + wanted.length();
			const bool rightClear = after >= lower.length() || !partOfAWord(lower[after]);

			if (leftClear && rightClear) {
				wxString trimmed = line;
				trimmed.Trim(true).Trim(false);
				return trimmed.length() > 200 ? trimmed.Left(197) + wxT("...") : trimmed;
			}
		}

		if (end == wxString::npos)
			break;
		at = end + 1;
	}

	return wxEmptyString;
}

void ibMcpSayObject(const ibValueMetaObject* object, ibDataNode& node, bool withText)
{
	if (object == nullptr)
		return;

	// Asked of the object, in the object's own words. Nothing here knows a storage key, so a
	// metatype is free to change how it saves itself without changing what a caller reads.
	node.AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));
	node.SetValue(wxT("name"), object->GetName());
	node.SetValue(wxT("kind"), object->GetClassName());

	// Only when there is one. A synonym that equals the name says nothing, and most objects
	// never get one — an empty key per object is a line of noise in every answer.
	const wxString synonym = object->GetSynonym();
	if (!synonym.IsEmpty() && synonym != object->GetName())
		node.SetValue(wxT("synonym"), synonym);

	// ⭐ THE THIRD SURFACE, and it was the one nothing here read. Every metaobject has carried a
	// `Comment` all along — the short line that says WHY this thing exists at all, sitting in the
	// property list where whoever opens the object meets it — and no tool ever answered with it.
	// A field written by one half of the room and invisible to the other is not a surface; it is a
	// place things get written and lost (Max, 2026-09-02: *"they are everywhere, you simply have
	// not connected them yet"*).
	//
	// Said BEFORE the long texts and outside the `withText` gate, because it is one line: a
	// listing that carries it stays a listing.
	const wxString comment = object->GetComment();
	if (!comment.IsEmpty())
		node.SetValue(wxT("comment"), comment);

	if (!withText)
		return;

	const wxString help = object->GetHelpContent();
	if (!help.IsEmpty())
		node.SetValue(wxT("help"), help);

	const wxString note = object->GetNoteContent();
	if (!note.IsEmpty())
		node.SetValue(wxT("note"), note);
}

namespace {

// What a value IS, in one word, taken from the KIND the value writes itself as rather than from a
// hand-kept table of property classes: a list like that is right until the day somebody adds a class.
wxString KindOf(const ibDataValue& value)
{
	switch (value.Kind()) {
		case ibDataKind::Bool:    return wxT("boolean");
		case ibDataKind::Number:  return wxT("number");
		case ibDataKind::String:  return wxT("string");
		case ibDataKind::Date:    return wxT("date");
		case ibDataKind::Binary:  return wxT("binary");
		case ibDataKind::Child:   return wxT("structure");
		case ibDataKind::Array:   return wxT("list");
		default: break;
	}
	return wxT("empty");
}

// WHICH GROUP EACH PROPERTY IS IN — and it is worth knowing exactly how little that means.
//
// ⚠ A CATEGORY IS NOT PART OF THE MODEL. It exists so the object inspector has something to draw
// its tree of headings from (Max, 2026-09-01: *"their job is just to show elements in groups; we
// had to work around it with this category property"*) — a display skeleton, not a classification
// anything may reason about. Nothing here branches on it and nothing should: it is carried as one
// word beside a property, purely so a reader can tell belonging — this one is the caption, these
// are the settings — instead of meeting forty flat names.
//
// It is at least DECLARED rather than guessed: a property is created INTO a category
// (`CreateProperty(m_categoryCommon, …)`), categories nest, and the object holds the root. Walked
// once here into a name → group lookup.
void CollectCategories(const ibPropertyCategory* category,
	std::map<wxString, wxString>& into)
{
	if (category == nullptr)
		return;

	const wxString label = category->GetLabel();

	for (unsigned int index = 0; index < category->GetPropertyCount(); ++index)
		into[category->GetPropertyName(index)] = label;

	for (unsigned int index = 0; index < category->GetCategoryCount(); ++index)
		CollectCategories(category->GetCategory(index), into);
}

} // namespace

// ⭐⭐ A CAPTION IS SAID AS ITS CELLS, WHEREVER IT IS SAID. The walk that reads properties and the
// answer to a write are the same fact about the same property, and they were said two different
// ways: the walk unfolded the languages, the write answered with `GetNodeValue`.
//
// 🛑 AND THE NODE VALUE IS THE STORED FORM. Writing a synonym over MCP therefore answered
// `en = 'Storage sites';` to a caller who had sent "Storage sites" — the template handed back as if
// it were the value (measured live, 2026-09-03, minutes after the same confusion was renamed out of
// the property with GetValueAsRawString). One place says a caption now, and both roads call it.
void ibMcpSayCaption(const ibPropertyTString* caption, ibDataNode& into)
{
	if (caption == nullptr)
		return;

	ibDataNode& byLanguage = into.Child(wxT("value"));

	for (const ibBackendLocalizationEntry& cell : caption->GetValueAsTranslate().GetTranslations())
		byLanguage.SetValue(cell.m_code, cell.m_data);
}

void ibMcpSayProperties(const ibPropertyObject* object, ibDataNode& node,
	const wxString& only, bool editableOnly)
{
	if (object == nullptr)
		return;

	std::map<wxString, wxString> groups;
	CollectCategories(object->GetCategory(), groups);

	std::vector<ibDataValue> out;

	for (unsigned int index = 0; index < object->GetPropertyCount(); ++index) {

		ibProperty* property = object->GetProperty(index);
		if (property == nullptr)
			continue;

		const wxString name = property->GetName();
		if (!only.IsEmpty() && !name.IsSameAs(only, false))
			continue;

		const bool editable = property->IsEditable();
		if (editableOnly && !editable)
			continue;

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(wxT("name"), name);

		// The label is what a person reads and the name is what a tool writes; kept apart for the
		// same reason a query field keeps them apart.
		const wxString label = property->GetLabel();
		if (!label.IsEmpty() && label != name)
			entry->SetValue(wxT("title"), label);

		if (!property->GetHelp().IsEmpty())
			entry->SetValue(wxT("help"), property->GetHelp());

		const auto group = groups.find(name);
		if (group != groups.end())
			entry->SetValue(wxT("group"), group->second);

		// SAID EVEN WHEN TRUE IS THE DULL CASE. A property that cannot be changed is the commonest
		// reason a write does nothing, and finding that out from a silent no-op costs a round trip.
		entry->AddField(wxT("editable"), ibDataValue::Bool(editable));

		// ⚠ WRITTEN ONCE. A node APPENDS named fields rather than replacing them, so writing "kind"
		// for the storage and then again for the enumeration put BOTH into the answer — one key
		// twice, with the reader left to decide which was meant. So the classification is settled
		// first, and only then written.
		//
		// ⭐ A CLOSED SET ANSWERS WITH ITS MEMBERS. The property holds a number, is named by a word,
		// and only it knows which words. Asked here, so a caller never has to find the enumeration
		// type by guessing at its name — and asked of GetValueList, which an enumeration, a list and
		// a relationship all answer, rather than of a cast that reached only the first of the three.
		ibPropertyChoiceList choices;
		const ibPropertyChoiceMode mode = property->GetValueList(choices);

		if (mode != ibPropertyChoiceMode::None) {

			const wxVariant held = property->GetValue();

			// ⭐⭐ THE WHOLE ENTRY, NOT ONLY ITS LABEL. A label is a reading — translated, and with
			// two vocabularies for one value. The ID is what the property stores, so a caller that
			// read this back can point at an entry by number and be right whatever it is called.
			std::vector<ibDataValue> words;
			wxString current;

			// ⚠ A WIDE READ DOES NOT INLINE THEM ALL, and this is now the heaviest thing in an
			// answer about an object. A RELATIONSHIP's choices are every metaobject that may fill
			// it, so `ListOwner` and `ListGeneration` each carry the configuration's whole
			// catalogue list — twice in one answer, about one object, growing with the BASE rather
			// than with the question. Measured while driving this server (2026-09-03): once the
			// stored node became opt-in, those two were most of what was left.
			//
			// ⭐ NARROWING TO THE PROPERTY GIVES THEM ALL — and that is the call a caller makes
			// anyway, right before setting one. The same trade `places` makes in mcp_search: the
			// first few, and the count, and where to ask for the rest.
			const bool everyChoice = !only.IsEmpty();
			const size_t inlineLimit = 8;

			for (unsigned int choice = 0; choice < choices.GetCount(); ++choice) {

				if (everyChoice || words.size() < inlineLimit) {

					auto item = std::make_shared<ibDataNode>();
					item->SetValue(wxT("id"), (int)choices.GetId(choice));
					item->SetValue(wxT("name"), choices.GetName(choice));
					if (!choices.GetSynonym(choice).IsEmpty())
						item->SetValue(wxT("synonym"), choices.GetSynonym(choice));
					words.push_back(ibDataValue::Child(item));
				}

				// WHICH ONE IS CURRENT — variant against variant, whatever they hold. The old test
				// compared a member number against the property's integer, a reading only an
				// enumeration has.
				//
				// ⚠ ASKED OF EVERY CHOICE, INLINED OR NOT: the current value is not more likely to
				// be among the first eight, and answering "nothing is set" because the match sat
				// ninth would be a wrong answer rather than a short one.
				const wxVariant& carried = choices.GetValue(choice);

				if (held.IsType(carried.GetType()) && held == carried)
					current = choices.GetLabel(choice);
			}

			entry->SetValue(wxT("kind"), wxString(wxT("enumerated")));
			entry->AddField(wxT("choices"), ibDataValue::Array(words));

			if (choices.GetCount() > words.size()) {

				entry->AddField(wxT("choiceCount"), ibDataValue::Int((s64)choices.GetCount()));
				entry->SetValue(wxT("choicesNote"), wxString::Format(
					ibMcpText("The first %u of %u. Ask for this property by name - "
					  "metadata_get {id, property: \"%s\"} - for all of them."),
					(unsigned int)words.size(), (unsigned int)choices.GetCount(), name));
			}

			// ⭐ AND HOW MANY OF THEM AT ONCE — the caller's next decision is whether writing one
			// REPLACES what is there or joins it. `single` and `multiple` are the property's own
			// answer, not a guess from the shape of the value.
			entry->SetValue(wxT("select"), wxString(
				mode == ibPropertyChoiceMode::Mult ? wxT("multiple") : wxT("single")));

			// The current one as a word too, so reading and writing speak the same language.
			entry->SetValue(wxT("value"), current);
		}
		else if (const ibPropertyTString* caption =
					dynamic_cast<const ibPropertyTString*>(property)) {

			// ⭐ A CAPTION IS AN ARRAY BY LANGUAGE, so it is said as one. The stored string is the
			// cells folded together, and reading it whole is how a caller ends up believing the
			// object has one caption when it has four.
			//
			// This is what `metadata_synonym` existed for — a verb of its own, because the walk
			// could not say this and the setter could not reach it. The setter learned `language`;
			// this is the reading half, and with it the special verb has nothing left to do.
			entry->SetValue(wxT("kind"), wxString(wxT("caption")));

			// …AND THE CELLS ARE ASKED OF THE CAPTION THE CONDITION ALREADY OBTAINED. The property
			// HOLDS them (ibTranslateString), so there is nothing here to recognise or take apart —
			// which is what used to make a caption typed without a language read back as {} while
			// `stored` plainly said "Storage warehouse" (measured 2026-09-03).
			ibMcpSayCaption(caption, *entry);
		}
		else if (const ibPropertyType* typed = dynamic_cast<const ibPropertyType*>(property)) {

			// 🛑 A TYPE IS NOT A NESTED METAOBJECT, AND THE BRANCH BELOW TREATED IT AS ONE. It is a
			// Child like a catalog's `Code` is, so it fell into "a structure is NAMED, not
			// unfolded" — and a type description has neither a Name nor an Id, so every attribute
			// in this configuration reported its type as `{"name": ""}`. Set through
			// metadata_set_type, which answered with the type in full; read back through here,
			// which said nothing. Checking your own work was impossible, and that is the one thing
			// a reading side exists for (found while building a delivery note over this server,
			// 2026-09-05).
			//
			// ⭐ SAID BY THE SAME ROAD THE SETTER USES — ibTypeDescriptionMemory::WriteNode — so the
			// shape a caller reads is the shape metadata_set_type takes back, and a type can be
			// read from one attribute and placed on another without translating anything.
			entry->SetValue(wxT("kind"), wxString(wxT("type")));

			// The metadata is asked of the object itself — ibPropertyObject declares GetMetaData and
			// a metaobject answers it, so there is nothing here to recognise first. A reference
			// type's name is resolved against it.
			ibDataValue described;
			if (ibTypeDescriptionMemory::WriteNode(described, typed->GetValueAsTypeDesc(),
					object->GetMetaData()))
				entry->AddField(wxT("value"), described);
			else
				// ⚠ AND A TYPE THAT CANNOT SAY ITSELF SAYS THAT, rather than coming back as an
				// empty one - which is what "no type" looks like, and is a different fact.
				entry->SetValue(wxT("value"), wxString(ibMcpText("<the type could not be read>")));
		}
		else {
			const ibDataValue value = property->GetNodeValue();
			entry->SetValue(wxT("kind"), KindOf(value));

			// ⚠ A STRUCTURE IS NAMED, NOT UNFOLDED. A property whose value is a nested metaobject —
			// a catalog's Code, Description, Ref, its two modules — writes ITSELF whole when asked
			// for its node: guid, id, flags, and three base64 blobs of interface, roles and
			// composition. Twenty-three properties of that put six hundred lines of storage into an
			// answer about one object, and none of it is anything a caller can act on or send back.
			//
			// So the value says WHICH one it is and stops. Its whole contents are one metadata_get
			// away, addressed by the id printed here — which is the thing a caller actually needs.
			if (value.Kind() == ibDataKind::Child && value.AsChild()) {

				const ibDataNode& held = *value.AsChild();
				ibDataNode& shown = entry->Child(wxT("value"));

				// ⚠ THE NAME IS A PROPERTY, THE ID IS A FIELD — the node keeps two areas and they are
				// not interchangeable. `GetValue` reads the FIELD area, so asking it for `Name`
				// came back empty and every structure was reported with an id and no name.
				// ibValueMetaObject::SaveNode is where the split is written down: intrinsics
				// (Guid, Id, Deleted) go to fields, editable values (Name, Synonym, Comment) go to
				// properties.
				shown.SetValue(wxT("name"), held.GetProp<wxString>(wxT("Name")));
				if (const ibDataValue* id = held.FindField(wxT("Id")))
					shown.AddField(wxT("id"), *id);
			}
			else {
				entry->AddField(wxT("value"), value);
			}
		}

		out.push_back(ibDataValue::Child(entry));
	}

	node.AddField(wxT("count"), ibDataValue::Int((s64)out.size()));
	node.AddField(wxT("properties"), ibDataValue::Array(out));
}

ibValueMetaObject* ibMcpObjectNamed(const ibDataNode& params, wxString& refusal,
	const wxString& field)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	// ⚠ ASKED WHETHER IT WAS GIVEN, not just what it is. GetValue answers 0 for an argument that
	// was never sent, and 0 is a perfectly good number to go looking for — which is how "you
	// forgot the id" turned into "no object has id 0" at the callsites that skipped the guard.
	const ibDataValue* given = params.FindField(field);

	if (given == nullptr || given->Kind() != ibDataKind::Number) {
		refusal = wxString::Format(
			ibMcpText("Pass the object's NodeId as `%s` - metadata_list and metadata_get give it."),
			field);
		return nullptr;
	}

	const s64 id = given->AsInt();

	if (id <= 0) {
		refusal = wxString::Format(
			ibMcpText("`%s` must be a NodeId, and ids start at 1."), field);
		return nullptr;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);

	if (object == nullptr) {
		refusal = wxString::Format(
			ibMcpText("Nothing in this configuration has id %i."), (int)id);
		return nullptr;
	}

	return object;
}

// ⭐ WHAT THE CALL CARRIED, WHEN THE TOOL HAS NOTHING BETTER TO SHOW.
//
// The person watching sees a headline per call — "listing what it can do", "looking at the Catalog
// objects" — and three identical headlines in a row tell them nothing about WHICH object or WHICH
// kind, even though the call said so explicitly. That answer was sitting in the arguments the whole
// time; nothing was asking for it.
//
// It quotes rather than interprets: every argument, by its own name, in the order the caller wrote
// them. A tool that knows one of its arguments is the SUBSTANCE (a module's text, a note) overrides
// and renders that instead — but no tool has to, and that is the point. The old default returned
// nothing, so a tool only had a detail if somebody remembered to write one, and sixty-seven of
// seventy nobody did.
//
// ⚠ A LONG VALUE IS CUT, not dropped. An argument that is a whole module would make one line of a
// running log taller than the window; showing its head still says which module and what is at the
// top of it.
wxString ibMcpTool::GetDetail(const ibDataNode& params) const
{
	wxString out;

	for (const auto& field : params.Fields()) {

		const ibDataValue& value = field.second;
		wxString           shown;

		switch (value.Kind()) {
			case ibDataKind::String: shown = value.AsString(); break;
			case ibDataKind::Bool:   shown = value.AsBool() ? wxT("yes") : wxT("no"); break;
			case ibDataKind::Number: shown = value.AsNumber().ToString(); break;
			case ibDataKind::Array:  shown = wxString::Format(ibMcpText("%d items"), (int)value.AsArray().size()); break;

			// EMPTY / Binary / Child / Date have no short reading that is worth a line here: an
			// argument nobody passed, an opaque block, a nested structure. Naming them without a
			// value would be noise in the one place that exists to remove it.
			default: continue;
		}

		if (shown.IsEmpty())
			continue;

		// One line each, and a long one folded so the log stays a log. The head of a module still
		// identifies it; the whole of it belongs in the editor.
		if (shown.length() > 200)
			shown = shown.Left(200) + wxT("...");

		shown.Replace(wxT("\n"), wxT(" "));
		shown.Replace(wxT("\r"), wxEmptyString);

		out << wxT(" ") << field.first << wxT(": ") << shown << wxT("\n");
	}

	return out;
}

wxString ibMcpFencedExcerpt(const wxString& text, const wxString& language, size_t maxLines)
{
	if (text.IsEmpty())
		return wxEmptyString;

	wxString shown;
	size_t   lines = 0;
	size_t   total = 0;

	// COUNTED WHILE CUTTING, so the sentence below can say how much was left out rather than
	// "more" — a reader deciding whether to open the editor needs the size, not a hint.
	for (const wxString& line : wxSplit(text, wxT('\n'), wxT('\0'))) {

		total++;

		if (lines < maxLines) {
			shown << line << wxT("\n");
			lines++;
		}
	}

	// 🛑 NO FENCE. The transcript is a styled text control with a markdown lexer — it COLOURS the
	// markup and never hides it, so ```markdown``` arrived on screen as three backticks, the word,
	// and three more backticks around the text it was supposed to frame. A fence that is visible
	// is not a frame, it is litter; the language name was information for a renderer that does not
	// exist here.
	//
	// Indented instead, which is what actually sets a block apart in a plain view — and stays
	// readable if the transcript ever does learn to render.
	wxString out;

	for (const wxString& line : wxSplit(shown, wxT('\n'), wxT('\0')))
		out << wxT(" ") << line << wxT("\n");

	// ⚠ SAID WHEN IT IS TRUE, and only then. A reader who is shown everything must not be told
	// anything was hidden, or they will go looking for what is already in front of them.
	if (total > lines)
		out << wxString::Format(
			ibMcpText(" ... first %u of %u lines - the rest is in the editor\n"),
			(unsigned)lines, (unsigned)total);

	return out;
}

std::vector<wxString> ibMcpComplaints(ibValueMetaObject* object)
{
	std::vector<wxString> found;
	if (object == nullptr)
		return found;

	// ⚠ The ledger is process-wide, so it is emptied before the question and
	// after it. In a designer session nothing else is writing to it at that
	// moment; a batch apply would want its own, which is a reason not to ask this
	// during one.
	ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
	ledger.Clear();

	// ⭐ AND THE OTHER HALF IS NOT HERE. Some metatypes complain into this ledger;
	// others say it through the message pane (a chart of accounts with no chart of
	// characteristic types does), and that road ends at the designer's main
	// window, which records it. Reading it belongs to messages_read, on the side
	// that has a window — not to a second collector in the backend saying the same
	// thing twice.
	//
	// The probe below therefore FILLS BOTH: whatever it says through the pane is
	// recorded by the window as it passes, and is there to be read a moment later.
	try {
		object->OnSaveMetaObject(0);
	}
	catch (...) {
		// A refusal that throws is still a refusal; whatever was said before it
		// threw is what we came for.
	}

	for (const ibRestructureInfo::Entry& entry : ledger) {
		if (entry.type == ibRestructure::error || entry.type == ibRestructure::warning)
			found.push_back(entry.descr);
	}

	ledger.Clear();
	return found;
}

void ibMcpReportComplaints(ibDataNode& result, ibValueMetaObject* object)
{
	const std::vector<wxString> complaints = ibMcpComplaints(object);
	if (complaints.empty())
		return;

	std::vector<ibDataValue> out;
	for (const wxString& complaint : complaints)
		out.push_back(ibDataValue::String(complaint));

	result.AddField(wxT("incomplete"), ibDataValue::Array(out));

	// WHAT IT MEANS, once, rather than a caller inferring it from the wording:
	// the object stands, and the configuration will not save while it says this.
	result.SetValue(wxT("incompleteNote"),
		ibMcpText("The change was made. The object still reports this, and the configuration cannot "
		  "be saved to the database until it stops. Some objects complain through the "
		  "message pane instead - messages_read has those."));
}

// ⭐⭐ THE SAME DOOR A MOUSE CLICK USES — and it is the platform's, not this server's.
//
// This function WAS the four-step sequence, copied out of ibObjectInspector::ModifyProperty because
// a server has no inspector to call. That is precisely the seam worth removing: the sequence is a
// property-system fact, so it moved to ibPropertyGate::SetValue (propertyObject.h) and the inspector
// now calls it too. One home, two callers, no copy to drift.
//
// What stays here is the translation of "no object" into words a caller can read — the gate answers
// with a sentence when asked, and this hands it the one thing it cannot know: nothing.
bool ibMcpApplyByHand(ibProperty* property, const wxVariant& newValue, wxString& refusal)
{
	return ibPropertyGate::SetValue(property->GetPropertyObject(), property, newValue, &refusal);
}

namespace {

// (The event twin lived here too and is gone the same way: ibPropertyGate::SetEvent. It had no
// caller left in this file — writing a handler goes through the same door as writing a value.)

// ⚠ THE CLOSING HALF ALONE, for the one road that cannot ask first. A value arriving as a NODE is
// not a wxVariant until the property has taken it, so there is nothing to put before the owner as
// "this is what is coming" — the veto genuinely cannot be offered here. What CAN be honoured is
// the telling, and it is, from the same place as everywhere else.
//
// Said out loud rather than quietly skipped: a caller writing a composite value gets the side
// effects and not the refusal, and that is a real difference from writing a scalar.
} // namespace

void ibMcpNotifyChanged(ibProperty* property, const wxVariant& oldValue)
{
	if (property == nullptr)
		return;

	ibPropertyObject* owner = property->GetPropertyObject();

	if (owner == nullptr)
		return;

	owner->OnPropertyChanged(property, oldValue, property->GetValue());
	owner->OnChildChanged();
}

bool ibMcpSetProperty(ibProperty* property, const ibDataNode& params,
	ibDataNode& result, wxString& refusal)
{
	if (property == nullptr) {
		refusal = ibMcpText("There is no such property.");
		return false;
	}

	const wxString name = property->GetName();

	// ⭐⭐ A CAPTION IS TRANSLATABLE WHEREVER IT LIVES — and it lives on both trees.
	//
	// A metaobject has a Synonym; a control has a Title and a Tooltip; a command
	// has both. All of them are the same class of property, holding an ARRAY BY
	// LANGUAGE folded into one value, and a plain write flattens that array to a
	// single string — losing every other language silently.
	//
	// So the language belongs HERE, in the one place that decides how a property
	// takes a value, rather than in a verb of its own on each tree. Passing no
	// language keeps the old behaviour (write the value as it stands), which is
	// what a caller working in one language wants.
	if (const ibDataValue* language = params.FindField(ibMcpLanguageArgument().Name())) {

		// ⭐ THE TYPE SAYS WHETHER IT CARRIES LANGUAGES. `ibPropertyTString` IS the translatable
		// string — that is what the class is for, declared once where the property is created — so
		// the question goes to the property, not to the text it happens to be holding.
		//
		// 🛑 IT ASKED THE VALUE FOR A WHILE (ibBackendLocalization::IsLocalizationString), and that
		// is a heuristic wearing a check's clothes: an EMPTY caption carries no cells yet, so it
		// does not look like one, and the first translation of a fresh object could not be written
		// at all. A property that has never been filled in is exactly when this is used.
		const ibPropertyTString* caption = dynamic_cast<const ibPropertyTString*>(property);

		if (caption == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a caption, so it has no languages."), name);
			return false;
		}

		const wxString code = language->AsString();

		// ⚠ AGAINST THE CONFIGURATION'S OWN LANGUAGES. A code nobody declared stores a translation
		// nothing will ever read — silently, which is the worst way to be wrong.
		//
		// ⭐ HERE, not in the one verb that had it. This check lived in `metadata_synonym`, which
		// existed because the setter could not reach a caption at all; the setter can now, so
		// every road that writes a translation has to pass this and not just the road somebody
		// remembered to put it on.
		bool declared = false;
		wxString available;

		for (const wxString& known : ibListMetaObjectNames(activeMetaData, wxT("Language"))) {

			ibValueMetaObject* found = ibFindMetaObject(activeMetaData, wxT("Language"), known);
			ibValueMetaObjectLanguage* lang =
				found != nullptr ? found->ConvertToType<ibValueMetaObjectLanguage>() : nullptr;
			if (lang == nullptr)
				continue;

			const wxString declaredCode = lang->GetLangCode();
			available << (available.IsEmpty() ? wxT("") : wxT(", ")) << declaredCode;
			if (declaredCode.IsSameAs(code, false))
				declared = true;
		}

		if (!declared) {
			refusal = available.IsEmpty()
				? ibMcpText("This configuration declares no languages. Create one first: "
				    "metadata_create kind=Language.")
				: wxString::Format(
					ibMcpText("'%s' is not a language this configuration declares. It has: %s."),
					code, available);
			return false;
		}

		// ⚠ THE CELL IS NAMED, so THAT cell is written and no other language moves. A write naming no
		// language means the one in force, which is right for an ordinary write and wrong here.
		//
		// Edited on a COPY and handed to the same door a mouse click uses: the gate needs an old
		// value and a new one to offer the owner, and editing in place leaves it holding one.
		ibTranslateString edited = caption->GetValueAsTranslate();
		edited.SetTranslate(code, ibMcpValueArgument().Text(params));

		if (!ibMcpApplyByHand(property, wxVariant(new ibVariantDataTranslate(edited)), refusal))
			return false;

		// SAID AS THE CELLS, like every other answer about a caption — and it is worth more here
		// than an echo: the whole point of naming a language is that the OTHERS stay, and this is
		// where a caller sees that they did.
		result.SetValue(wxT("property"), name);
		result.SetValue(wxT("language"), code);
		ibMcpSayCaption(caption, result);
		return true;
	}

	// ⭐⭐ ASK THE PROPERTY WHAT IT MAY BE SET TO, AND THE ANSWER SAYS WHETHER IT IS THIS KIND AT ALL.
	//
	// 🛑 This used to be two `dynamic_cast`s — an enumeration whose choices are fixed by its type, a
	// list whose choices are filled by the metaobject that owns it. Two classes, one idea, and the
	// gate had to know both names to ask one question. It therefore knew only those two: a
	// RELATIONSHIP (which chart of characteristic types supplies an account's dimension kinds) has
	// choices exactly like the other two, was not in the pair, and fell through to the generic write
	// — which answered `wrong value kind (expected 7, got 4)`, a sentence about a serialised shape
	// that tells the caller nothing about what it did wrong.
	//
	// ⭐ `None` IS THE ANSWER "not chosen from a list", so there is nothing to test for beforehand.
	// One call decides both whether this road applies and how many values it may carry.
	ibPropertyChoiceList choices;

	if (const ibPropertyChoiceMode mode = property->GetValueList(choices);
		mode != ibPropertyChoiceMode::None) {

		const wxString word = ibMcpValueArgument().Text(params);

		// TWO VOCABULARIES FOR ONE VALUE — the inspector reads "Within second",
		// the language writes `WithinSecond`, and type_members answers with the
		// second. Compared with the spaces removed, so both are accepted without a
		// mapping table that would be wrong for the first value added after it.
		auto same = [](const wxString& a, const wxString& b) {
			wxString l(a), r(b);
			l.Replace(wxT(" "), wxEmptyString);
			r.Replace(wxT(" "), wxEmptyString);
			return l.IsSameAs(r, false);
		};

		// ⭐ THE NUMBER IS THE STEADY WAY TO NAME A CHOICE. Words are two vocabularies and a
		// translation away from each other; the id is what the property actually stores and what
		// the front compares against (advpropList's ValueToString does exactly this). A caller that
		// read the list back can point at an entry by its id and be right whatever it is called.
		const ibDataValue* asked = params.FindField(ibMcpValueArgument().Name());
		const bool byNumber = asked != nullptr && asked->Kind() == ibDataKind::Number;
		const long askedId = byNumber ? (long)asked->AsNumber().ToInt() : 0;

		for (unsigned int index = 0; index < choices.GetCount(); ++index) {

			// BY ID WHEN A NUMBER CAME, otherwise by name or by label — the name is what a script
			// writes, the label is what the designer shows, and a caller should not have to know
			// which vocabulary this property speaks.
			const bool matches = byNumber
				? choices.GetId(index) == askedId
				: same(choices.GetName(index), word) || same(choices.GetLabel(index), word);

			if (!matches)
				continue;

			// ⭐ THE CHOICE IS ITS OWN VALUE. An enumeration's holds a member id, a relationship's a
			// metadescription, a composer's a path or the composer itself. The gate places what it
			// was handed and never learns which — that is the whole reason a choice carries a
			// variant rather than a number.
			if (!ibMcpApplyByHand(property, choices.GetValue(index), refusal))
				return false;

			result.SetValue(wxT("property"), name);
			result.SetValue(wxT("value"), choices.GetLabel(index));
			return true;
		}

		wxString allowed;
		for (unsigned int index = 0; index < choices.GetCount(); ++index)
			allowed << (allowed.IsEmpty() ? wxT("") : wxT(", ")) << choices.GetLabel(index);

		// AND WHEN THERE IS NOTHING TO OFFER, SAY THAT INSTEAD. An empty list means the
		// configuration holds nothing of the kind yet — "takes one of: " with nothing after the
		// colon reads like a defect in the tool rather than an empty tree.
		refusal = allowed.IsEmpty()
			? wxString::Format(ibMcpText("'%s' is chosen from the configuration, and there is nothing of "
			                     "that kind in it yet."), name)
			: wxString::Format(ibMcpText("'%s' takes one of: %s."), name, allowed);
		return false;
	}

	// A VALUE IS EITHER A SCALAR OR A SHAPE. The parser puts scalars in the field
	// area and objects in the properties area as a child, so both are looked for —
	// a caller sending back what it read should not have to know which it holds.
	ibDataValue value;
	if (const ibDataValue* scalar = params.FindField(ibMcpValueArgument().Name()))
		value = *scalar;
	else if (const ibDataNode* composite = params.FindChild(ibMcpValueArgument().Name()))
		value = ibDataValue::Child(std::make_shared<ibDataNode>(*composite));

	// Held before the write so the closing half can report what it replaced — the same two facts
	// the other roads carry, taken the only way this one allows.
	const wxVariant before = property->GetValue();

	// ⭐ THE PROPERTY JUDGES, AND THE REFUSAL CARRIES THE SHAPE. Nothing here decides whether a
	// value is legal — SetNodeValue is the property comparing what it was handed against what it
	// holds, which is the only place that knows. But "would not take that value" on its own leaves
	// a caller guessing at the difference, and the answer was in reach the whole time: the property
	// can be asked what it holds NOW, and its kind is the shape a value has to arrive in.
	if (!property->SetNodeValue(value)) {

		// ⭐⭐ WHERE THERE IS A VERB FOR IT, THE REFUSAL NAMES THE VERB. "It holds a structure - send
		// one of that shape" is true and leaves the caller to assemble a type description by hand
		// from what they read back; `metadata_set_type` takes it in WORDS (String with a length,
		// CatalogRef.Goods) and is the road every other caller uses. A refusal that names the
		// symptom and not the cure costs a round trip at best and a hand-built shape at worst —
		// which is the lesson of the day this was written (2026-09-03), applied to itself.
		//
		// ⭐ CAST ONCE, CHECKED, THEN USED. The name obtained for the test is the one the message
		// reads from — saying what the property holds NOW turns "you used the wrong verb" into a
		// sentence an author can act on without a second call.
		const ibPropertyType* asType = dynamic_cast<const ibPropertyType*>(property);

		if (asType != nullptr) {

			// ⚠ NAMED THROUGH THE ONE RESOLVER, which knows BOTH kinds of id. A REFERENCE type is
			// constructive — its clsid's body is the metaID of the object it points at — and only
			// the metadata can name it; a PRIMITIVE is registered with the value types and only the
			// value registry can. `ibMetaTypeResolver` asks them in that order and raises for
			// neither, which is why the rendering already uses it (metaIntrospect.h).
			//
			// 🛑 BOTH HALVES WERE GOT WRONG IN TURN, in the same hour: first the value registry
			// alone (a reference would have come out unnamed), then the metadata alone — and that
			// one was measured saying "It holds nothing now" about an attribute holding String.
			const auto named = ibMetaTypeResolver(property->GetPropertyObject() != nullptr
				? property->GetPropertyObject()->GetMetaData() : nullptr);

			wxString holds;
			for (const ibClassID& clsid : asType->GetValueAsTypeDesc().m_listTypeClass) {
				const wxString word = named(clsid);
				if (word.IsEmpty())
					continue;
				holds << (holds.IsEmpty() ? wxT("") : wxT(", ")) << word;
			}

			refusal = wxString::Format(
				ibMcpText("'%s' is a TYPE, and it is not set as a value: use metadata_set_type - it takes "
				  "the type in words (String with a length, Number with precision and scale, "
				  "CatalogRef.Goods), and `description` for a composite one. It holds %s now."),
				name, holds.IsEmpty() ? ibMcpText("nothing") : holds);
			return false;
		}

		// …and only the OTHER road needs to know what shape it holds.
		const ibDataValue held = property->GetNodeValue();

		refusal = wxString::Format(
			ibMcpText("'%s' would not take that value. It holds a %s - send one of that shape, or read "
			  "metadata_properties for what it has now."), name, KindOf(held));
		return false;
	}

	ibMcpNotifyChanged(property, before);

	// ANSWERED WITH WHAT IT NOW HOLDS, read back through the property rather than
	// echoed from the request: what was asked for and what was taken are different
	// facts, and only the second one is true.
	//
	// ⚠ AND A CAPTION IS SAID AS ITS CELLS, not as its node — the node is the stored template, and
	// answering with it hands the caller the format they were spared everywhere else.
	result.SetValue(wxT("property"), name);

	if (const ibPropertyTString* caption = dynamic_cast<const ibPropertyTString*>(property))
		ibMcpSayCaption(caption, result);
	else
		result.AddField(wxT("value"), property->GetNodeValue());
	return true;
}
