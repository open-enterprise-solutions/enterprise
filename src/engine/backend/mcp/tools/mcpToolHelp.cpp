////////////////////////////////////////////////////////////////////////////
//	Description : the syntax-helper tools — what this language actually offers
////////////////////////////////////////////////////////////////////////////
//
// WHY THIS MATTERS MORE THAN IT LOOKS. The script language here exists nowhere
// else, so a caller generating code has nothing to go on but guesswork — and
// guesswork is only caught by script_check, after the fact. The corpus is the
// answer BEFORE the fact: signatures, parameters, return values, worked
// examples, and which runtimes an entry is even valid in.
//
// AND IT KNOWS THIS CONFIGURATION. The corpus is the platform's docs merged
// with the ones built from the open configuration, so a catalog's own methods
// and attributes are in it beside `Message` and `Date`.
//
// ⚠ NO DUMP, DELIBERATELY. A corpus is thousands of entries; a verb that
// returned all of them would spend a caller's whole context on a list it has to
// read past. Search answers ONE LINE per entry — enough to choose — and get
// answers one entry in full.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/metaCollection/metaIntrospect.h"   // ibConfigurationWritesInWords — the dialect this configuration uses
#include "backend/metadataConfiguration.h"
#include "backend/syntaxHelper/helpCorpus.h"
#include "backend/syntaxHelper/helpEntry.h"
#include "backend/syntaxHelper/helpService.h"

namespace {

// The helper window already draws this distinction; a tool that ignored it
// would hand back the other dialect's spelling — code that reads correctly and
// does not compile, which is the worst kind of wrong answer to give something
// that cannot tell.
bool WritesInWords() { return ibConfigurationWritesInWords(activeMetaData); }

// The id grammar already carries the kind as a prefix ("fn.Message"), but a
// caller should not have to parse an id to learn what it is holding: the
// classification is answered outright.
wxString KindWord(ibHelpKind kind)
{
	switch (kind) {
	case ibHelpKind::kKeyword:             return wxT("keyword");
	case ibHelpKind::kSystemFunction:      return wxT("function");
	case ibHelpKind::kSystemConstant:      return wxT("constant");
	case ibHelpKind::kSystemEnum:          return wxT("enumValue");
	case ibHelpKind::kMetaObjectType:      return wxT("metaObject");
	case ibHelpKind::kMetaObjectAttribute: return wxT("attribute");
	case ibHelpKind::kMetaObjectMethod:    return wxT("method");
	case ibHelpKind::kPrimitiveType:       return wxT("primitiveType");
	case ibHelpKind::kCollection:          return wxT("collection");
	case ibHelpKind::kEvent:               return wxT("event");
	case ibHelpKind::kOperator:            return wxT("operator");
	}

	return wxT("unknown");
}

std::shared_ptr<const ibHelpCorpus> Corpus(wxString& refusal)
{
	ibHelpService* service = ibApplicationData::GetHelpService();
	if (service == nullptr) {
		refusal = ibMcpText("The syntax helper is not loaded in this process.");
		return nullptr;
	}

	// Never null by contract — a total load failure publishes an EMPTY corpus
	// whose errors are readable, rather than nothing at all.
	std::shared_ptr<const ibHelpCorpus> corpus = service->GetCorpus();
	if (corpus && corpus->EntryCount() == 0) {
		refusal = ibMcpText("The syntax helper corpus is empty - nothing was loaded for this locale.");
		return nullptr;
	}

	return corpus;
}

// ONE LINE PER ENTRY: what a caller needs to pick one, and nothing it would
// have to read past.
ibDataValue Brief(const ibHelpEntry& entry)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	node->SetValue(wxT("id"), entry.id);
	node->SetValue(wxT("name"), entry.nameLocal);
	node->SetValue(wxT("nameEn"), entry.nameEn);
	node->SetValue(wxT("kind"), KindWord(entry.kind));

	if (!entry.signature.IsEmpty())
		node->SetValue(wxT("signature"), entry.signature);

	// Whose documentation this is. A platform entry describes the engine; one
	// from the configuration describes what somebody built here, and a caller
	// weighing an answer wants to know which.
	node->AddField(wxT("fromConfiguration"), ibDataValue::Bool(entry.fromConfiguration));

	return ibDataValue::Child(node);
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgQuery()
{
	static const ibArg s_a(wxT("query"), ibArg::Kind::Text,
		ibMcpText("What to look for. Matched as a name prefix first; if nothing starts with it, "
			  "as a token across names and signatures."), /*required*/ true);
	return s_a;
}

const ibArg& ArgLimit()
{
	static const ibArg s_a(wxT("limit"), ibArg::Kind::Whole,
		ibMcpText("How many at most. Defaults to 40 - a search that answers more than that is a "
			  "search that should have been narrower."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Text,
		ibMcpText("The entry's id, as help_search returned it - for example 'fn.Message' or "
			  "'meth.Catalog.Goods.Write'."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// help_search
//---------------------------------------------------------------------------
class ibMcpToolHelpSearch : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("help_search"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking up '%s' in the help"),
			ArgQuery().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Find what this platform offers by name: built-in functions, keywords, types, "
			"collections, events, operators, and the objects, attributes and methods of the "
			"open configuration. Answers one line each - call help_get for the full entry. "
			"Ask here BEFORE writing a name you are not certain of.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgQuery(), ArgLimit() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		std::shared_ptr<const ibHelpCorpus> corpus = Corpus(refusal);
		if (!corpus)
			return false;

		const wxString query = ArgQuery().Text(params);
		if (query.IsEmpty()) {
			refusal = ibMcpText("Nothing to look for.");
			return false;
		}

		s32 limit = 40;
		params.GetValue(wxT("limit"), limit);
		if (limit <= 0)
			limit = 40;

		// PREFIX FIRST, then tokens. Somebody who typed most of a name means that
		// name; falling straight to a token search would bury it under everything
		// that merely mentions it.
		std::vector<const ibHelpEntry*> found = corpus->SearchPrefix(query);
		if (found.empty())
			found = corpus->SearchText(query);

		std::vector<ibDataValue> entries;
		for (const ibHelpEntry* entry : found) {
			if ((s32)entries.size() >= limit)
				break;
			if (entry != nullptr)
				entries.push_back(Brief(*entry));
		}

		result.AddField(wxT("found"), ibDataValue::Int((s64)found.size()));
		result.AddField(wxT("entries"), ibDataValue::Array(entries));

		// The corpus's own digest. A caller that caches answers can tell whether
		// what it remembers is still what the platform would say.
		result.SetValue(wxT("corpus"), corpus->Fingerprint());

		// Answered on the FIRST call, not only in help_get: a caller that is
		// about to write code needs to know which dialect it is writing in
		// before it has looked anything up in full.
		result.SetValue(wxT("syntaxMode"), wxString(WritesInWords() ? wxT("words") : wxT("braces")));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolHelpSearch);

//---------------------------------------------------------------------------
// help_get
//---------------------------------------------------------------------------
class ibMcpToolHelpGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("help_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("reading a help article");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("One help entry in full: signature, parameters, what it returns, both syntax "
			"forms, worked examples, and which runtimes it is valid in. Ids come from "
			"help_search.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		std::shared_ptr<const ibHelpCorpus> corpus = Corpus(refusal);
		if (!corpus)
			return false;

		const wxString id = ArgId().Text(params);
		const ibHelpEntry* entry = corpus->FindById(id);
		if (entry == nullptr) {
			refusal = wxString::Format(
				ibMcpText("No help entry has the id '%s'. Use help_search to find one."), id);
			return false;
		}

		result.SetValue(wxT("id"), entry->id);
		result.SetValue(wxT("name"), entry->nameLocal);
		result.SetValue(wxT("nameEn"), entry->nameEn);
		result.SetValue(wxT("kind"), KindWord(entry->kind));

		if (!entry->signature.IsEmpty())   result.SetValue(wxT("signature"), entry->signature);
		if (!entry->description.IsEmpty()) result.SetValue(wxT("description"), entry->description);
		if (!entry->parameters.IsEmpty())  result.SetValue(wxT("parameters"), entry->parameters);
		if (!entry->returnDescr.IsEmpty()) result.SetValue(wxT("returns"), entry->returnDescr);

		// ⚠ TWO SYNTAX FORMS, AND THE CONFIGURATION DECIDES WHICH IS RIGHT. The
		// same entry carries a C-style spelling and a word-style one; handing
		// back the other dialect's produces code that reads correctly and does
		// not compile. So `syntax` and `example` are THIS configuration's form —
		// what to write — and the other one is offered beside it, named, for a
		// caller that is reading someone else's module rather than writing.
		const bool words = WritesInWords();

		const wxString& syntaxHere  = words ? entry->syntaxBlockVes : entry->syntaxBlock;
		const wxString& syntaxOther = words ? entry->syntaxBlock    : entry->syntaxBlockVes;
		const wxString& exampleHere  = words ? entry->exampleVes : entry->example;
		const wxString& exampleOther = words ? entry->example    : entry->exampleVes;

		result.SetValue(wxT("syntaxMode"), wxString(words ? wxT("words") : wxT("braces")));

		if (!syntaxHere.IsEmpty())   result.SetValue(wxT("syntax"), syntaxHere);
		if (!exampleHere.IsEmpty())  result.SetValue(wxT("example"), exampleHere);
		if (!syntaxOther.IsEmpty())  result.SetValue(wxT("syntaxOtherMode"), syntaxOther);
		if (!exampleOther.IsEmpty()) result.SetValue(wxT("exampleOtherMode"), exampleOther);

		// ⚠ An entry with only ONE form (operators, directives) leaves `syntax`
		// empty when that one form belongs to the other dialect — said here
		// rather than left for a caller to notice, because an absent field reads
		// as "nothing to say" and this means the opposite.
		if (syntaxHere.IsEmpty() && !syntaxOther.IsEmpty())
			result.SetValue(wxT("syntaxNote"),
				ibMcpText("This entry is written down only in the other syntax form."));

		// WHERE IT IS VALID AT ALL. "Works in the daemon, not in the designer" is
		// the class of mistake that costs a run to discover.
		if (!entry->availability.IsEmpty())
			result.SetValue(wxT("availability"), entry->availability);

		std::vector<ibDataValue> related;
		for (const wxString& seeAlso : entry->seeAlso)
			related.push_back(ibDataValue::String(seeAlso));
		if (!related.empty())
			result.AddField(wxT("seeAlso"), ibDataValue::Array(related));

		result.AddField(wxT("fromConfiguration"), ibDataValue::Bool(entry->fromConfiguration));

		// ⚠ SAID PLAINLY: a draft was written by a machine and has not been
		// through a person. Passing it on as settled documentation is how a wrong
		// answer acquires authority it never earned.
		result.AddField(wxT("reviewed"), ibDataValue::Bool(entry->reviewed));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolHelpGet);
