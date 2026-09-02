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

		const std::vector<ibDiagnostic> diagnostics =
			ibCheckScript(text, module.IsEmpty() ? wxT("module") : module, context);

		result.AddField(wxT("ok"), ibDataValue::Bool(diagnostics.empty()));
		result.AddField(wxT("diagnostics"), DiagnosticsOf(diagnostics));

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
