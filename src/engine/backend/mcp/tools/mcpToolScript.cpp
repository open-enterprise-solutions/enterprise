////////////////////////////////////////////////////////////////////////////
//	Description : the script tools — checking, without running
////////////////////////////////////////////////////////////////////////////
//
// The verb belongs to the compiler (compiler/scriptCheck.h); this file is only
// the door onto it, plus the words a caller needs to use it correctly.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/compiler/scriptCheck.h"
#include "backend/compiler/scriptComplete.h" // ibOutlineScriptQueries — what the compiler understood
#include "backend/metadataConfiguration.h"   // the open configuration is the check's context

namespace {

using ibArg = ibMcpTool::ibMcpArgument;
const ibArg& ArgText() { static const ibArg a(wxT("text"), ibArg::Kind::Text, ibMcpText("The module text to compile."), true); return a; }
const ibArg& ArgModule() { static const ibArg a(wxT("module"), ibArg::Kind::Text, ibMcpText("The name the messages are written against - pass the name of the module being edited so the report reads the way the designer's would. Optional.")); return a; }

ibDataValue DiagnosticsOf(const std::vector<ibDiagnostic>& diagnostics)
{
	std::vector<ibDataValue> list;

	for (const ibDiagnostic& diagnostic : diagnostics) {

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

		// THE FAILURE AS DATA — an address, not a sentence. The line and the
		// module are what a caller navigates by; the message is what it reads.
		entry->AddField(wxT("line"), ibDataValue::Int((s64)diagnostic.m_line));
		entry->SetValue(wxT("module"), diagnostic.m_moduleName);
		entry->SetValue(wxT("message"), diagnostic.m_message);

		if (!diagnostic.m_codeLine.IsEmpty())
			entry->SetValue(wxT("codeLine"), diagnostic.m_codeLine);

		// The engine's own code for this failure: a translated message changes,
		// this does not.
		entry->AddField(wxT("code"), ibDataValue::Int((s64)diagnostic.m_code));

		list.push_back(ibDataValue::Child(entry));
	}

	return ibDataValue::Array(list);
}

} // namespace

//---------------------------------------------------------------------------
// script_check
//---------------------------------------------------------------------------
class ibMcpToolScriptCheck : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("script_check"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("checking a piece of code");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Compile a module text and report what is wrong with it. The text is compiled and "
			"thrown away: nothing is stored and no module is replaced. An empty diagnostics list "
			"means it compiles.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgText(), ArgModule() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString text = ArgText().Text(params);
		if (text.IsEmpty()) {
			refusal = ibMcpText("No text to check.");
			return false;
		}

		const wxString module = ArgModule().Text(params);

		// ⭐ THE OPEN CONFIGURATION IS THE CONTEXT, and it is handed over rather than reached for.
		// Without it the compile has no parent and the platform's own globals do not resolve — a
		// check that answers *"Procedure or function not detected (Message)"* about `Message`.
		const ibMetaData* const context =
			activeMetaData != nullptr && activeMetaData->IsConfigOpen() ? activeMetaData : nullptr;

		const wxString named = module.IsEmpty() ? wxT("module") : module;
		const std::vector<ibDiagnostic> diagnostics = ibCheckScript(text, named, context);

		// ⭐⭐ THE VERDICT IS ABOUT THE TEXT THAT WAS SENT, and only compiling it with the
		// configuration open showed that it had not been. Bringing the context in brings its
		// modules too, so a module somebody is editing in the designer RIGHT NOW — unfinished, as
		// an edited module is — puts its own errors in this list, and `ok` went false about a text
		// that was perfect. Measured 2026-09-08: `var x = 1;` came back false, with one diagnostic
		// naming ConfigurationModule line 8.
		//
		// Both halves are kept, because both are worth knowing: the caller's own errors decide the
		// verdict, and the configuration's are reported beside them under their own name — a query
		// written against a module that does not compile is worth a warning, not a silent pass.
		std::vector<ibDiagnostic> mine, elsewhere;
		for (const ibDiagnostic& one : diagnostics)
			(one.m_moduleName.IsSameAs(named, /*caseSensitive=*/false) ? mine : elsewhere).push_back(one);

		result.AddField(wxT("ok"), ibDataValue::Bool(mine.empty()));
		result.AddField(wxT("diagnostics"), DiagnosticsOf(mine));
		if (!elsewhere.empty())
			result.AddField(wxT("inTheConfiguration"), DiagnosticsOf(elsewhere));

		// ⚠ SAID OUT LOUD, because it is what the answer is worth. With a configuration open the
		// text is judged against its module manager — the globals, Manager, the metatype
		// collections — which is what a real module sees minus its OWN owner's names. Without one
		// it is the language and nothing else.
		//
		// 🛑 THIS FIELD READ `text-only` UNCONDITIONALLY and went on saying it after the check had
		// stopped being text-only, which is a lie in the one field a caller reads to know how much
		// to trust the verdict.
		result.SetValue(wxT("scope"), wxString(context != nullptr
			? ibMcpText("this configuration's context") : wxT("text-only")));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolScriptCheck);

//---------------------------------------------------------------------------
// script_query — what the compiler understood about the LINQ queries in a text
//---------------------------------------------------------------------------
class ibMcpToolScriptQuery : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("script_query"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("taking a query apart");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("TAKE A LINQ QUERY APART INSTEAD OF GUESSING AT IT. Compile a module text and answer "
			"with the SHAPE of every `from ... select ...` in it - and of every `restrict ...` too, "
			"which is a query that narrows another one: which names it binds and where each "
			"came from (a source, a `let`, a `join`, a `group ... into`, the restricted row), which "
			"COLUMNS the answer will have, whether it groups and whether it orders - and, for each "
			"bound name, WHAT IT OFFERS: the fields of the row that name stands for.\n\n"
			"WHAT IT IS FOR: writing a query against what is actually bound. A name you can see in "
			"the outline is a name you can write; a field under `offers` is a field you can read off "
			"it; a column you can see is a column the answer will really carry. Reach for it after "
			"writing a query and before running one - and when a query answers with something "
			"unexpected, this says what the COMPILER read, which is the half a run cannot show you.\n\n"
			"The text is compiled and thrown away, exactly as `script_check` compiles it: nothing is "
			"stored and no module is replaced. A text that does not compile still answers - the "
			"queries read before the refusal are what it understood, and mid-text is where a person "
			"writing one always is. Ask `script_check` for the refusal itself.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgText(), ArgModule() };
		return s_arguments;
	}

	// ⭐⭐ THE WORDS A CALLER ARRIVES WITH, WHICH ARE NOT THE WORDS THIS FILE IS WRITTEN IN. The
	// description says what the tool DOES, in English and in our vocabulary; somebody looking for
	// it types what they want, often in the language the product speaks. Measured 2026-09-08:
	// `mcp_search "linq"` found this tool, while the Russian for "query" and for "how do I write a
	// query" — the words a person using this product actually types — found
	// NOTHING - and nothing does not read as "ask differently", it reads as "there is no such
	// thing" (see ibMcpTool::GetSearchText, where the same lesson is written for the corpus).
	//
	// Not a keyword list bolted on: these are the words for the very things the tool answers about
	// - a query, its clauses, the two forms it may be written in.
	wxString GetSearchText() const override
	{
		return wxT("linq query queries from where select join group orderby restrict "
			"clause clauses bindings take a query apart what the compiler understood "
			"query tree parse stopped row level access");
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString text = ArgText().Text(params);
		if (text.IsEmpty()) {
			refusal = ibMcpText("No text to read.");
			return false;
		}

		const wxString module = ArgModule().Text(params);
		const ibMetaData* const context =
			activeMetaData != nullptr && activeMetaData->IsConfigOpen() ? activeMetaData : nullptr;

		const std::vector<ibQueryOutline> outlines =
			ibOutlineScriptQueries(text, module.IsEmpty() ? wxT("module") : module, context);

		std::vector<ibDataValue> queries;
		for (const ibQueryOutline& outline : outlines) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();

			// WHICH OF THE TWO THINGS IT IS, said outright: a query, or a `restrict` that narrows
			// one. A restriction carries no columns because it has no projection - reading that as
			// "the compiler did not finish" would be wrong.
			entry->SetValue(wxT("kind"), wxString(outline.m_isRestrict
				? wxT("restrict") : wxT("query")));

			std::vector<ibDataValue> bindings;
			for (const ibQueryOutlineBinding& binding : outline.m_bindings) {
				std::shared_ptr<ibDataNode> one = std::make_shared<ibDataNode>();
				one->SetValue(wxT("name"), binding.m_name);
				one->SetValue(wxT("from"), binding.m_origin);
				one->AddField(wxT("cell"), ibDataValue::Int((s64)binding.m_rowCell));

				// What this name OFFERS — the row's own fields, so a query can be written against
				// what is really there. Absent rather than empty when the source did not resolve
				// while designing: nothing listed is a different statement from "it has no fields".
				if (!binding.m_offers.empty()) {
					std::vector<ibDataValue> offers;
					for (const wxString& field : binding.m_offers)
						offers.push_back(ibDataValue::String(field));
					one->AddField(wxT("offers"), ibDataValue::Array(offers));
				}
				bindings.push_back(ibDataValue::Child(one));
			}
			entry->AddField(wxT("binds"), ibDataValue::Array(bindings));

			std::vector<ibDataValue> columns;
			for (const wxString& column : outline.m_columns)
				columns.push_back(ibDataValue::String(column));
			entry->AddField(wxT("columns"), ibDataValue::Array(columns));

			entry->AddField(wxT("groups"), ibDataValue::Bool(outline.m_groups));
			if (!outline.m_groupInto.IsEmpty())
				entry->SetValue(wxT("into"), outline.m_groupInto);
			entry->AddField(wxT("orders"), ibDataValue::Bool(outline.m_orders));
			if (outline.m_orders)
				entry->AddField(wxT("descending"), ibDataValue::Bool(outline.m_orderDescending));

			// The query as it was written, and where it sits in the text — so a caller editing one
			// replaces exactly what it read rather than looking for it again.
			if (!outline.m_text.IsEmpty()) {
				entry->SetValue(wxT("text"), outline.m_text);
				entry->AddField(wxT("at"), ibDataValue::Int((s64)outline.m_textFrom));
				entry->AddField(wxT("to"), ibDataValue::Int((s64)outline.m_textTo));
			}

			queries.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)queries.size()));
		result.AddField(wxT("queries"), ibDataValue::Array(queries));
		result.SetValue(wxT("scope"), wxString(context != nullptr
			? ibMcpText("this configuration's context") : wxT("text-only")));

		if (queries.empty())
			result.SetValue(wxT("note"), ibMcpText("No `from ...` query and no `restrict ...` in this "
				"text - a chain written with verbs (`arr.Where(...)`) is neither; it has no bindings "
				"to name. `linq_methods` says how all three are written."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolScriptQuery);
