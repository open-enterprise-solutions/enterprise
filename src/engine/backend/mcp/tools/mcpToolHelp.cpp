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
// ⭐ IT IS THE SYNTAX HELPER AND NOTHING ELSE — the same corpus the designer shows beside the
// editor. What a CONFIGURATION holds is metadata_tree / metadata_get / query_fields; how something
// is USUALLY BUILT here is pattern_read. Those three questions look alike and are not, and a verb
// that answers all of them teaches a caller to ask the wrong one.
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
		ibMcpText("The entry's id, as syntax_search returned it - for example 'fn.Message' or "
			  "'meth.Catalog.Goods.Write'."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// syntax_search
//---------------------------------------------------------------------------
class ibMcpToolHelpSearch : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("syntax_search"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking up '%s' in the help"),
			ArgQuery().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("THE SYNTAX HELPER, asked by name instead of by cursor: the LANGUAGE - built-in "
			"functions, keywords, types, collections, events, operators - with one line each; "
			"syntax_get gives the full entry. Ask here before writing a name you are not certain "
			"of.\nEvery answer also names the GUIDES (`guide.`) - short articles on what the "
			"language offers rather than what a name is called, the query language among them. "
			"Read them once before writing much code: they cover ground a name search cannot, "
			"because you have to know a thing exists to search for it.\nIT IS NOT A SEARCH OVER "
			"THE CONFIGURATION, and does not pretend to be: what an "
			"object holds is answered by metadata_tree / metadata_get, its fields by query_fields, "
			"and how something is USUALLY BUILT here by pattern_read.");
	}

	// ⭐⭐ AND THE CORPUS ANSWERS WHEN IT IS NOT ASKED, which is the only way a caller learns of a
	// name it does not know exists. mcp_search already gathers PLACES from whatever carries a body
	// of text, and the once-per-family reminder in the server asks the same question of everything
	// registered - but only the pattern corpus was answering, so a caller working on a template was
	// pointed at how a blank is cut and never at `SpreadsheetDocument`, `GetArea` or `Parameters`.
	//
	// The language is a body of text like any other. Answering here costs one search and makes the
	// difference between "there is craft written about this" and "there are also WORDS for it".
	//
	// ⚠ IT ANSWERS WHAT IS THERE, AND NO MORE. Two families are not in this corpus yet - the
	// creatable types outside the Query family, and the platform enumerations - so a hint about
	// them still comes back empty. That is a gap in the CORPUS, not in this road, and it is stated
	// where a caller meets it (the `nothing` line of a search).
	void FindInside(const wxString& query, std::vector<ibDataValue>& places) const override
	{
		if (query.IsEmpty())
			return;

		wxString unused;
		std::shared_ptr<const ibHelpCorpus> corpus = Corpus(unused);
		if (!corpus)
			return;

		// The same two searches, in the same order and for the same reason: somebody who typed most
		// of a name means that name, and falling straight to tokens buries it.
		std::vector<const ibHelpEntry*> found = corpus->SearchPrefix(query);
		if (found.empty())
			found = corpus->SearchText(query);

		// A HANDFUL. This is a hint riding somebody else's answer, not a search they asked for -
		// a common word answers from dozens of entries, and that list arriving unbidden is noise.
		// Whoever wants all of them asks syntax_search, which is what the hint says to do.
		size_t taken = 0;
		for (const ibHelpEntry* entry : found) {

			if (entry == nullptr)
				continue;
			if (++taken > 4)
				break;

			std::shared_ptr<ibDataNode> hit = std::make_shared<ibDataNode>();
			hit->SetValue(wxT("syntax"), entry->id);
			hit->SetValue(wxT("name"), entry->nameLocal);
			hit->SetValue(wxT("kind"), KindWord(entry->kind));

			if (!entry->signature.IsEmpty())
				hit->SetValue(wxT("signature"), entry->signature);

			places.push_back(ibDataValue::Child(hit));
		}
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

		// ⭐ THIS IS THE SYNTAX HELPER, AND ONLY THAT — the same corpus the designer shows beside the
		// editor: how the LANGUAGE is written. It briefly also answered names out of the open
		// configuration, and that was a mixing of two different questions: what a configuration
		// HOLDS is metadata_tree / metadata_get / query_fields, and how things are USUALLY BUILT is
		// pattern_read, which is the reference of the trade. A search that answers all three teaches
		// a caller to ask the wrong one of them.
		//
		// So a miss says where the other two live, rather than looking like a platform with no such
		// word in it.
		//
		// ⚠ AND THE ISSUE NUMBER STAYS HERE, OUT OF THE ANSWER. What crosses the wire is read by a
		// client that cannot open our tracker; the two missing families are NAMED below, which is
		// the part a caller can act on, and a number would only stop being true the day the issue
		// closes. See open-enterprise-solutions/enterprise#83.
		if (entries.empty())
			result.SetValue(wxT("nothing"),
				ibMcpText("Nothing in the syntax helper by that name. This is the LANGUAGE reference - "
				  "functions, keywords, types. For what THIS configuration holds ask metadata_tree / "
				  "metadata_get / query_fields; for how such a thing is usually built, pattern_read.\n"
				  "AND TWO FAMILIES ARE NOT IN THIS CORPUS YET, so a miss on one of them is a gap "
				  "rather than a wrong word: the CREATABLE TYPES apart from the Query family (Array, "
				  "Structure, Container, Table, TypeDescription, ...) and the PLATFORM ENUMERATIONS "
				  "(AccumulationRecordType, DocumentWriteMode, HierarchyType, ...). Their names and "
				  "signatures are declared in the backend registries and none of the tools above "
				  "answer for them."));

		// The corpus's own digest. A caller that caches answers can tell whether
		// what it remembers is still what the platform would say.
		result.SetValue(wxT("corpus"), corpus->Fingerprint());

		// Answered on the FIRST call, not only in syntax_get: a caller that is
		// about to write code needs to know which dialect it is writing in
		// before it has looked anything up in full.
		result.SetValue(wxT("syntaxMode"), wxString(WritesInWords() ? wxT("words") : wxT("braces")));

		// THE SAME REASON, ONE STEP FURTHER — and it is a different question from
		// the one asked. A search answers "what is this called"; it cannot answer
		// "what does this language OFFER", and a caller who does not know that a
		// query language exists never types a word that would find it. It writes
		// a loop. So the guides — there are two, and they are cheap — are named on
		// every search, the way the dialect is: not as an answer, as the thing you
		// would have had to already know in order to ask.
		std::vector<ibDataValue> guides;
		for (const ibHelpEntry* entry : corpus->AllEntries()) {
			if (entry == nullptr || !entry->id.StartsWith(wxT("guide.")))
				continue;
			std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
			node->SetValue(wxT("id"), entry->id);
			node->SetValue(wxT("name"), entry->nameLocal);
			guides.push_back(ibDataValue::Child(node));
		}
		if (!guides.empty())
			result.AddField(wxT("guides"), ibDataValue::Array(guides));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolHelpSearch);

//---------------------------------------------------------------------------
// syntax_get
//---------------------------------------------------------------------------
class ibMcpToolHelpGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("syntax_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("reading a help article");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("One entry of the SYNTAX HELPER in full: signature, parameters, what it returns, "
			"both syntax forms, worked examples, and which runtimes it is valid in. Ids come from "
			"syntax_search - the language ones (`kw.`, `fn.`, `cls.`, `guide.`). An object of the "
			"configuration has no article here; it is read with metadata_get.");
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

			// ⭐ AN ID FROM THE CONFIGURATION HALF OF syntax_search LANDS HERE, and "no such entry" is
			// a true sentence that teaches the caller nothing: `Document.GoodsReceipt` was just
			// handed to them BY syntax_search, marked `fromConfiguration` with the verb to read it
			// beside it. There is no article to fetch — objects are read with the metadata verbs —
			// so say that, with the id those verbs take, instead of sending them back to the search
			// that already answered.
			const wxString objectName = id.AfterLast(wxT('.'));

			if (!objectName.IsEmpty() && activeMetaData != nullptr && activeMetaData->IsConfigOpen()) {
				if (const ibValueMetaObject* object =
						activeMetaData->FindAnyObjectByFilter<ibValueMetaObject>(objectName)) {

					refusal = wxString::Format(
						ibMcpText("'%s' is an object of this configuration, not an article - it has no "
						  "help text of its own. Read it with metadata_get {id: %i}, its fields with "
						  "query_fields."), id, (int)object->GetMetaID());
					return false;
				}
			}

			refusal = wxString::Format(
				ibMcpText("No help entry has the id '%s'. Use syntax_search to find one."), id);
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
