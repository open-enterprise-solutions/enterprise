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
#include "backend/metaData.h"                      // ibCompileValueCache - where the descriptor is registered
#include "backend/moduleManager/moduleManager.h"   // ibValueModuleManagerDesigner - AddCommonModule
#include "backend/value_ptr.h"                     // ibValuePtr - metaobjects are reference-counted

#include "frontend/win/editor/codeEditor/codeEditorInterpreter.h"
#include "frontend/win/editor/codeEditor/codeEditorParser.h"

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

//---------------------------------------------------------------------------
// A MODULE THAT EXISTS FOR ONE ANSWER
//---------------------------------------------------------------------------
//
// ⭐⭐ WHY COMPLETION DID NOT WORK ON ARBITRARY CODE, and it is not what it looks like. The
// interpreter is perfectly able to complete a free-standing snippet — what it cannot do is reach
// the configuration without a METAOBJECT to reach it through. Every global it offers arrives by
// one road: `m_moduleObject->GetMetaData()` → the compile cache → the designer's module manager →
// its global names and its common modules' exports (ibPrecompileCode::Compile and
// PrepareModuleData). With no module that road is not taken at all, and the answer falls back to
// whatever the snippet itself declares. So the path breaks exactly between the global functions
// and the text being written — which is why `Catalogs.` completed inside a module and nowhere
// else (Max, 2026-09-06: *"your path breaks between the common functions and the current
// module"*).
//
// ⭐ SO ONE IS MADE FOR THE DURATION. Shaped like a common module, because that is what a body of
// free-standing statements most resembles — with the one difference that it may declare variables
// of its own. It is bound to the open configuration and to NOTHING else: not added to the tree,
// not written to the metadata, not visible to any other reader, and gone when this call returns
// (Max: *"it just lives in the moment — you computed the value, it worked, goodbye"*).
//
// ⚠ AND BINDING IT TO THE METADATA IS NOT ENOUGH, which is worth writing down because it looks
// like it would be. Two of the three things the interpreter offers do come straight off the
// metadata — the manager's global variables and its context names. The third does not: it walks
// the DESCRIPTOR chain (`FindCompileModule` → `ibRuntimeModuleDataObject::GetParent`, the loop in
// PrepareModuleData) to collect what each module above contributes. An unregistered module has no
// descriptor, so that walk starts at null and the chain to the root is simply not there.
//
// ⭐ SO IT IS REGISTERED AS A COMMON MODULE FOR THE DURATION. That is the call which builds the
// descriptor AND parents it on the root — the module object that holds Catalogs, Documents and the
// rest — so the interpreter can bend upwards from a text that belongs to nobody. It owns nothing
// up there and is written into nothing: the registration is undone on the way out, by every exit
// path, because the handle that made it is what un-makes it.
class ibScratchModule {
public:
	// ⭐ NAMED AS THE ARBITRARY-CODE MODULE IS NAMED EVERYWHERE ELSE. `JobCode` is what the syntax
	// check calls this text (ibCheckScript, mcpToolRunCode.cpp) and what the application compiles
	// it as when it runs (jobRunByteCode.cpp), so a diagnostic about it reads the same wherever it
	// was produced.
	ibScratchModule(ibMetaData* metaData, const wxString& text)
		: m_module(new ibValueMetaObjectCommonModule(wxT("JobCode")))
	{
		// The bridge to the configuration: through this the interpreter reaches the compile cache
		// and the designer's own module manager, and from there every global name there is.
		m_module->SetMetaData(metaData);

		// ⭐ AND IT CARRIES THE CODE ITSELF, so what is registered is a module in full rather than
		// an empty shell with the text smuggled in beside it. Everything that reads a module by
		// asking it — GetModuleText, the export parse, the unit compiled just below — sees the same
		// text the caller sent, which is the whole point of emulating one (Max, 2026-09-06: *"in
		// your fake metaobject there will already be the code — you emulate that the code is there,
		// so it sees it fully"*).
		m_module->SetModuleText(text);

		ibCompileValueCache* const cache = metaData != nullptr ? metaData->GetCompileCache() : nullptr;
		m_manager = cache != nullptr ? cache->GetModuleManager() : nullptr;
		if (m_manager != nullptr) {
			// runModule TRUE — compiled now, not deferred to the next CreateMainModule. A deferred
			// unit would be built after this call has already answered, which is another way of
			// saying never.
			//
			// ⚠ AND THE COMPILE IS EXPECTED TO FAIL HALF THE TIME. The text is unfinished BY
			// CONSTRUCTION — standing just after a dot is what completion means — so a refusal here
			// says nothing about whether the question can be answered. What matters is that the
			// module got registered and the descriptor exists; the interpreter compiles the text
			// again itself, up to the caret and no further.
			try {
				m_registered = m_manager->AddCommonModule(m_module, /*managerModule*/ false,
					/*runModule*/ true);
			}
			catch (...) {
				// Registered-then-threw is the dangerous shape: assume it landed, so the
				// destructor takes it back out. Removing something that was never added is a
				// no-op; leaving something that was added is a phantom module.
				m_registered = true;
			}
		}
	}

	~ibScratchModule()
	{
		// 🛑 THE REGISTRY MUST NOT OUTLIVE THE QUESTION. A module left behind here is a phantom
		// entry in the designer's own common-module list — parsed for exports on every later
		// completion, and named in a registry nobody can see it in.
		if (m_registered && m_manager != nullptr)
			m_manager->RemoveCommonModule(m_module);
	}

	ibScratchModule(const ibScratchModule&)            = delete;
	ibScratchModule& operator=(const ibScratchModule&) = delete;

	ibValueMetaObjectCommonModule* Get() const { return m_module; }

private:
	// Metaobjects are ibValues — reference-counted, with DecrRef deleting the last one out. This
	// handle holds the only reference there is, so the module dies exactly when this does.
	ibValuePtr<ibValueMetaObjectCommonModule> m_module;
	ibValueModuleManagerDesigner*             m_manager    = nullptr;
	bool                                      m_registered = false;
};

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The module this text belongs to, as NodeId - metadata_get on the owning object "
			  "lists its modules. The module decides what names are in scope, so pass it whenever "
			  "the text HAS a home: inside a document's object module `Ref` and the document's own "
			  "attributes are names, and they are names nowhere else.\n"
			  "LEAVE IT OUT for code that belongs to no module - anything you are about to send to "
			  "code_run, or a snippet you are working out. The text is then judged as free-standing "
			  "statements against the configuration's globals, which is exactly how code_run "
			  "compiles it."), /*required*/ false);
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		ibMcpText("The module text, as it would be after your edit. It is not stored."), /*required*/ true);
	return s_a;
}

const ibArg& ArgPosition()
{
	static const ibArg s_a(wxT("position"), ibArg::Kind::Whole,
		ibMcpText("Character offset to stand at - normally just after the dot you want completed."), /*required*/ true);
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
		return ibMcpText("asking the platform what can be written next");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What may be written at a given place - the same list the editor shows after "
			"a dot. Send the text you are about to write and the offset you are standing at (just "
			"after the dot), and the answer is what that expression actually offers: its methods "
			"with their call form, and its properties.\n\n"
			"USE IT INSTEAD OF GUESSING AN OBJECT'S API, and the reason is not politeness: the help "
			"corpus covers functions and keywords, and THE OBJECT MODEL IS NOT IN IT. A catalog "
			"manager's methods, what a document object offers, what a register record set will "
			"take - none of that is searchable by name anywhere. It is answerable only here, and "
			"only by asking the object itself.\n\n"
			"IT ALSO ANSWERS FOR CODE THAT BELONGS TO NO MODULE - leave `id` out and the text is "
			"treated as free-standing statements against this configuration's globals, which is "
			"exactly how code_run compiles what you send it. So the code you are about to run can "
			"be checked for names BEFORE it touches a base: script_check says whether it compiles, "
			"this says what may follow the dot you are standing after.\n\n"
			"NOTE WHAT COMPILING PROVES AND WHAT IT DOES NOT. Names after a dot are resolved at "
			"RUN time, so `Catalogs.Catalog1.NoSuchMethod()` compiles cleanly and fails only when "
			"it runs. That gap is this tool's whole reason to exist.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText(), ArgPosition() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		const wxString text = ArgText().Text(params);
		s32 position = 0;
		params.GetValue(wxT("position"), position);

		if (position < 0 || (size_t)position > text.length()) {
			refusal = wxString::Format(
				ibMcpText("Position %d is outside a text of %u characters."),
				(int)position, (unsigned)text.length());
			return false;
		}

		// TWO WAYS TO HAVE A MODULE, and the second one is why arbitrary code can be completed at
		// all. Given an id, the text is judged where it will live. Given none, a module carrying
		// this very text is made for the length of the call, so the walk up to the configuration's
		// globals has something to start from — see ibScratchModule.
		std::unique_ptr<ibScratchModule> scratch;
		ibValueMetaObjectModuleBase* module = nullptr;

		const ibDataValue* id = params.FindField(ArgId().Name());
		if (id != nullptr && id->Kind() == ibDataKind::Number) {
			ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id->AsInt());
			module = ibMcpModuleOf(object, refusal);
			if (module == nullptr)
				return false;
		}
		else {
			scratch = std::make_unique<ibScratchModule>(activeMetaData, text);
			module = scratch->Get();
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
			refusal = ibMcpText("The text could not be compiled up to that point, so there is nothing "
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
		return ibMcpText("looking at what a module is made of");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What a module text declares: its procedures, functions and variables, which of "
			"them are exported, and the lines each one occupies. Reads the text as given and does "
			"not compile it - ask this before adding a handler, to see whether it is already "
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
			refusal = ibMcpText("The text could not be read far enough to list what it declares.");
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
