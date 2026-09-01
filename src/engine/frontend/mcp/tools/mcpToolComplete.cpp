////////////////////////////////////////////////////////////////////////////
//	Description : what is valid HERE — the editor's own answer, without a caret
////////////////////////////////////////////////////////////////////////////
//
// ⭐ A PORT OF WHAT THE EDITOR ALREADY DOES. Pressing a dot in the code editor
// does not guess from the text: it compiles the module UP TO THE CARET, takes
// the VALUE the compiler arrived at, and lists that value's members
// (ibCodeEditor::LoadIntelliList). This is the same three calls with a position
// handed in instead of read off a widget.
//
// WHY IT MATTERS MORE THAN THE HELP CORPUS. The corpus documents functions and
// keywords; the OBJECT MODEL is not in it. "How are register movements written"
// has no entry — but the object knows, and it will say so the moment it is
// asked at a place where it exists.
//
// ⚠ IT LIVES IN THE FRONT because ibPrecompileCode does — by location, not by
// nature: the class touches no widget, it is simply where the editor kept it.
// The tool registry is exported from the backend, so registering from here
// works exactly as it does there.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"

#include "frontend/win/editor/codeEditor/codeEditorInterpreter.h"
#include "frontend/win/editor/codeEditor/codeEditorParser.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		_("The module this text belongs to, as NodeId — metadata_get on the owning object "
			  "lists its modules. The module decides what names are in scope."), /*required*/ true);
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		_("The module text, as it would be after your edit. It is not stored."), /*required*/ true);
	return s_a;
}

const ibArg& ArgPosition()
{
	static const ibArg s_a(wxT("position"), ibArg::Kind::Whole,
		_("Character offset to stand at — normally just after the dot you want completed."), /*required*/ true);
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// script_complete
//---------------------------------------------------------------------------
class ibMcpToolScriptComplete : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("script_complete"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("asking the platform what can be written next");
	}

	wxString GetDescription() const override
	{
		return _("What may be written at a given place in a module — the same list the editor "
			"shows after a dot. Send the text you are about to write and the offset you are "
			"standing at (just after the dot), and the answer is what that expression actually "
			"offers: its methods with their call form, and its properties.\n\n"
			"Use it instead of guessing an object's API. The help corpus covers functions and "
			"keywords; the object model is only answerable this way.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText(), ArgPosition() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = _("No configuration is open.");
			return false;
		}

		const ibDataValue* id = params.FindField(ArgId().Name());
		if (id == nullptr || id->Kind() != ibDataKind::Number) {
			refusal = _("No module id given.");
			return false;
		}

		ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id->AsInt());
		ibValueMetaObjectModuleBase* module = dynamic_cast<ibValueMetaObjectModuleBase*>(object);
		if (module == nullptr) {
			refusal = _("That id is not a module.");
			return false;
		}

		const wxString text = ArgText().Text(params);
		s32 position = 0;
		params.GetValue(wxT("position"), position);

		if (position < 0 || (size_t)position > text.length()) {
			refusal = wxString::Format(
				_("Position %d is outside a text of %u characters."),
				(int)position, (unsigned)text.length());
			return false;
		}

		// THE EDITOR'S THREE CALLS. Compiled and discarded: the module is not
		// written, and the configuration never learns this happened.
		ibPrecompileCode precompile(module);
		precompile.Load(text);

		// ⚠ AND THE LEXEMS PREPARED, which is not optional. The editor does
		// Load → PrepareLexem → compile, and the middle step is easy to miss
		// because it looks like an optimisation: it is not. Without it the
		// compile has no token stream to walk and answers false, which reads as
		// "your text is wrong" when the text was never read.
		precompile.PrepareLexem();

		precompile.SetCurrentPos((unsigned int)position);
		precompile.SetCalcValue(true);

		bool compiled = false;
		try {
			compiled = precompile.Compile();
		}
		catch (...) {
			// A refusal mid-way is ordinary here — the text is unfinished by
			// construction, that is what standing after a dot means.
		}

		precompile.SetCalcValue(false);

		if (!compiled) {
			refusal = _("The text could not be compiled up to that point, so there is nothing "
				"to offer. Check it with script_check first.");
			return false;
		}

		const ibValue value = precompile.GetComputeValue();

		std::vector<ibDataValue> methods;
		for (long i = 0; i < value.GetNMethods(); i++) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), value.GetMethodName(i));

			const wxString helper = value.GetMethodHelper(i);
			if (!helper.IsEmpty())
				entry->SetValue(wxT("signature"), helper);

			// Function or procedure: calling a procedure where a value is wanted
			// is a compile error, and only this tells them apart.
			entry->AddField(wxT("returnsValue"), ibDataValue::Bool(value.HasRetVal(i)));
			methods.push_back(ibDataValue::Child(entry));
		}

		std::vector<ibDataValue> properties;
		for (long i = 0; i < value.GetNProps(); i++) {
			// The same filter the editor applies: a scope-local name belongs to
			// the frame it was declared in, not to the object reached through a
			// chain.
			if (value.IsPropScoped(i))
				continue;
			properties.push_back(ibDataValue::String(value.GetPropName(i)));
		}

		result.AddField(wxT("methods"), ibDataValue::Array(methods));
		result.AddField(wxT("properties"), ibDataValue::Array(properties));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolScriptComplete);

//---------------------------------------------------------------------------
// module_outline
//---------------------------------------------------------------------------
//
// WHAT A MODULE ALREADY DECLARES — its procedures, functions and variables,
// each with the lines it occupies. The parser is the editor's
// (ibParserModule): the same walk that fills the "Procedures and functions"
// window, so a caller sees exactly what a person opening that window sees.
//
// It reads TEXT, not a saved module, and it does not compile — so it answers
// about work in progress, including text that does not compile yet. That is
// the point: the question "what is in here already" comes BEFORE the question
// "is it correct".
//
class ibMcpToolModuleOutline : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("module_outline"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return _("looking at what a module is made of");
	}

	wxString GetDescription() const override
	{
		return _("What a module text declares: its procedures, functions and variables, which of "
			"them are exported, and the lines each one occupies. Reads the text as given and does "
			"not compile it — ask this before adding a handler, to see whether it is already "
			"written.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const wxString text = ArgText().Text(params);

		ibParserModule parser;
		if (!parser.ParseModule(text)) {
			refusal = _("The text could not be read far enough to list what it declares.");
			return false;
		}

		std::vector<ibDataValue> declared;
		for (const ibModuleElement& element : parser.GetAllContent()) {

			wxString kind;
			bool exported = false;

			switch (element.m_eType) {
			case eVariable:        kind = wxT("variable");  break;
			case eExportVariable:  kind = wxT("variable");  exported = true; break;
			case eProcedure:       kind = wxT("procedure"); break;
			case eExportProcedure: kind = wxT("procedure"); exported = true; break;
			case eFunction:        kind = wxT("function");  break;
			case eExportFunction:  kind = wxT("function");  exported = true; break;
			case eLambda:          kind = wxT("lambda");    break;
			default: continue;
			}

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("name"), element.m_name);
			entry->SetValue(wxT("kind"), kind);
			entry->AddField(wxT("exported"), ibDataValue::Bool(exported));
			entry->AddField(wxT("lineFrom"), ibDataValue::Int((s64)(element.m_lineStart + 1)));
			entry->AddField(wxT("lineTo"), ibDataValue::Int((s64)(element.m_lineEnd + 1)));

			declared.push_back(ibDataValue::Child(entry));
		}

		result.AddField(wxT("declares"), ibDataValue::Array(declared));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolModuleOutline);
