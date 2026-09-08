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
// ⭐ AND IT LIVES BESIDE THE COMPILER, which it did not always. It sat in the front for one reason:
// the editor's own precompiler was there, and this verb called it. That precompiler is gone — both
// answers now come from the compiler itself (ibValueAtCaret / ibNamesAtCaret) and the token stream
// from the lexer next to it — so the tool followed the mechanism it asks (Max, 2026-09-08:
// *"autocomplete is part of the backend now — the tool can live there"*).
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/backend_exception.h"              // ibEvalModeScope - which kind of evaluation this call is
#include "backend/compiler/scriptComplete.h"        // ibValueAtCaret / ibFieldTypesOf - off the bytecode and the metadata
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaData.h"                      // ibCompileValueCache - where the descriptor is registered
#include "backend/moduleManager/moduleManager.h"   // ibValueModuleManagerDesigner - AddCommonModule
#include "backend/value_ptr.h"                     // ibValuePtr - metaobjects are reference-counted

#include "backend/compiler/translateCode.h"        // the lexem stream - what the caret stands in
#include "backend/compiler/scriptParseCode.h"

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

//---------------------------------------------------------------------------
// A MODULE THAT EXISTS FOR ONE ANSWER
//---------------------------------------------------------------------------
//
// ⭐⭐ WHY COMPLETION DID NOT WORK ON ARBITRARY CODE, and it is not what it looks like. The walk is
// perfectly able to complete a free-standing snippet — what it cannot do is reach the configuration
// without a METAOBJECT to reach it through. Every global it offers arrives by one road:
// `moduleObject->GetMetaData()` → the compile cache → the designer's module manager → its global
// names and its common modules' exports. With no module that road is not taken at all, and the
// answer falls back to whatever the snippet itself declares. So the path breaks exactly between the
// global functions and the text being written — which is why `Catalogs.` completed inside a module
// and nowhere else (Max, 2026-09-06: *"your path breaks between the common functions and the
// current module"*).
//
// ⭐ SO ONE IS MADE FOR THE DURATION. Shaped like a common module, because that is what a body of
// free-standing statements most resembles — with the one difference that it may declare variables
// of its own. It is bound to the open configuration and to NOTHING else: not added to the tree,
// not written to the metadata, not visible to any other reader, and gone when this call returns
// (Max: *"it just lives in the moment — you computed the value, it worked, goodbye"*).
//
// ⚠ AND BINDING IT TO THE METADATA IS NOT ENOUGH, which is worth writing down because it looks
// like it would be. Two of the three things the answer offers do come straight off the metadata —
// the manager's global variables and its context names. The third does not: it walks the DESCRIPTOR
// chain (`FindCompileModule` → `ibRuntimeModuleDataObject::GetParent`) to collect what each module
// above contributes. An unregistered module has no descriptor, so that walk starts at null and the
// chain to the root is simply not there.
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
		ibMcpText("Character offset to stand at - just after the dot you want completed, or anywhere "
			"in open code to be told what is in scope there. Pass 0 with empty `text` to ask what "
			"a module starts with."), /*required*/ true);
	return s_a;
}

// WHAT MAY BE WRITTEN IN OPEN CODE — the caret is not after a dot, so the answer is not the members
// of anything: it is the names that ARE in scope right there. The editor's other list
// (ibCodeEditor::LoadSysKeyword), which this verb never had.
//
// ⚠ KEYWORDS ARE LEFT OUT ON PURPOSE, and that is the one place this deliberately says less than the
// dropdown. The editor lists them because a person is choosing from one popup; a caller here has
// syntax_search, which answers "what does this language have" properly — with the help text, the
// call form and the kind — and would only be repeated worse by a bare list of words. Names in scope,
// by contrast, are answerable NOWHERE else.
// ⭐⭐ AND EACH NAME SAYS WHERE IT CAME FROM, which is what makes a list of a hundred and seventy
// usable at all. A hundred and forty of them are the same at every caret in every module; the ten
// that make THIS place different are what a caller is actually looking for, and without the origin
// they sit in the middle of the alphabet indistinguishable from `Chars` and `BeginTransaction`.
// Sorting is then the caller's to do, on a fact rather than on a guess about names.
wxString ibOriginName(ibNameOrigin origin)
{
	switch (origin) {
	case ibNameOrigin::Platform:     return wxT("platform");
	case ibNameOrigin::Global:       return wxT("global");
	case ibNameOrigin::GlobalModule: return wxT("globalModule");
	case ibNameOrigin::Context:      return wxT("context");
	case ibNameOrigin::Bound:        return wxT("bound");
	case ibNameOrigin::Member:       return wxT("member");
	case ibNameOrigin::Inherited:    return wxT("inherited");
	case ibNameOrigin::Keyword:      return wxT("keyword");
	default:                         return wxT("declared");
	}
}

// ⭐⭐ THE LIST, OFF THE SAME BYTECODE THE PATH IS WALKED OVER. Nothing here reads the text: the
// names are the symbol tables of the compiled snippet and of every module above it in the ladder,
// and the declaration gate comes free — the text is compiled UP TO the caret, so a name written
// below it was never declared (scriptComplete.cpp).
// 🛑 DESCRIBING A VALUE MUST NOT MAKE IT READ ITSELF. Asking a value what CLASS it is looks free
// and is not: a reference names its class through the metadata it points at, and for a document
// created and not written that reaches the database and throws — taking the caret answer with it
// (measured 2026-09-08: two chains that had worked all evening turned into refusals the moment the
// question was added). So a value that will not describe itself simply describes itself less. The
// members are the answer; the type beside them is a courtesy, and a courtesy must not be able to
// cost the answer.
//
// The other half of that debt is paid rather than guarded: what a field is DECLARED as is asked of
// the METADATA now (ibFieldTypesOf), where it cannot touch a database at all.
wxString ClassNameOf(const ibValue& value)
{
	try {
		return value.GetClassName();
	}
	catch (...) {
		return wxEmptyString;
	}
}

bool ibAnswerWithScope(const wxString& text, int caretPos,
	const ibValueMetaObject* module, ibDataNode& result)
{
	std::vector<ibCaretName> found;
	if (!ibNamesAtCaret(text, (unsigned int)caretPos, module, found))
		return false;

	std::vector<ibDataValue> names;
	names.reserve(found.size());

	for (const ibCaretName& name : found) {

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(wxT("name"), name.m_name);

		// Function or procedure: calling a procedure where a value is wanted is a compile error,
		// and this is what tells them apart — the same distinction the member answer carries as
		// `returnsValue`.
		// wxString, not a bare literal: the ternary would settle on `const wchar_t*`, which the
		// data-node codec has no encoding for — and outside MSVC the two branches are ambiguous
		// against each other (portability.md §1.10, the same trap eight times over).
		entry->SetValue(wxT("kind"), wxString(!name.m_callable ? wxT("variable")
			: name.m_returnsValue ? wxT("function") : wxT("procedure")));

		if (!name.m_signature.IsEmpty())
			entry->SetValue(wxT("signature"), name.m_signature);
		entry->AddField(wxT("exported"), ibDataValue::Bool(name.m_exported));

		// ⭐ PROTECTED IS SAID ONLY WHEN IT IS TRUE, because it is the rare one and a field that is
		// false on nine names in ten is noise on every one of them. It is not the opposite of
		// `exported` either: exported reaches the whole configuration, protected reaches the
		// modules BELOW this one and stops there, and what a list must never do is offer a name
		// that cannot be written — see ibNamesAtCaret's note on the runtime's own rule.
		if (name.m_protected)
			entry->AddField(wxT("protected"), ibDataValue::Bool(true));

		// TWO SEPARATE FACTS, kept apart on purpose. `origin` says where the name came FROM; `local`
		// says it belongs to the body the caret is standing in rather than to the module. Folding
		// "local" into the origin list would have made a frame position look like a source.
		entry->SetValue(wxT("origin"), ibOriginName(name.m_origin));
		if (name.m_local)
			entry->AddField(wxT("local"), ibDataValue::Bool(true));

		names.push_back(ibDataValue::Child(entry));
	}

	result.AddField(wxT("names"), ibDataValue::Array(names));
	return true;
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
		return ibMcpText("What may be written at a given place - the list the editor shows there. "
			"Send the text you are about to write and the offset you are standing at, and WHERE YOU "
			"STAND DECIDES WHICH ANSWER YOU GET. Just after a dot: what that expression offers - "
			"`methods` with their call form and whether each returns a value, and `properties`. "
			"Anywhere else: `names`, everything in scope at that point - the variables and the "
			"functions, including the ones this module declares above the caret. The general "
			"keywords are not in it; syntax_search answers those properly.\n\n"
			"THE QUERY KEYWORDS ARE THE ONE EXCEPTION, and they carry `origin: keyword`. A query is "
			"WRITTEN in them - `from o in Catalogs.Goods where … select …` - so which of them may be "
			"written where the caret stands is part of this question, not a lookup: inside a query "
			"you are offered its clauses, inside a `restrict` the two it takes, and in open code the "
			"two openers. `linq_methods` gives the full form of each clause.\n\n"
			"`place` SAYS WHICH OF THOSE YOU GOT, so you never have to work it out from which field "
			"came back: `afterDot` (members of the expression to the left, which `expression` names), "
			"`inKeyword` (a keyword or call whose own domain is being completed - `keyword` names it), "
			"or `openCode` (everything in scope). `word` is the identifier under the caret, which is "
			"what a list filters by rather than part of the question.\n\n"
			"READ `origin` BEFORE READING THE LIST. Most of those names are the same at every caret "
			"in every module, and the few that make THIS place different are the point: `member` is "
			"THE OBJECT'S OWN SURFACE - its attributes, its tabular sections and its methods "
			"(Write / Fill / Lock on a document); `context` is the binding itself, ThisObject / "
			"ThisForm; `bound` is a handle this module was given, such as a constant's Value; "
			"`declared` is written in this text (with `local` true when it belongs to the body you "
			"are standing in); `inherited` comes from a module above this one. `platform`, `global` "
			"and `globalModule` are the ones you already know - the configuration's collections, "
			"the system functions, global constants and common modules.\n\n"
			"EVERY NAME IN IT CAN BE WRITTEN HERE - it is what the runtime itself sees at that "
			"point, not everything that exists. This text's own names, the globals, and from each "
			"module above only what that module lets out: its exports and its `protected` (flagged, "
			"and reaching the modules below it rather than the whole configuration). A parent's "
			"private names are absent, because writing one is an error rather than a completion.\n\n"
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

		// ⭐⭐ EVERYTHING THIS CALL RUNS, RUNS AS A COMPLETION. Answering takes real execution — a
		// module is registered and compiled to reach the configuration, values are constructed,
		// methods on them are called — and each of those asks the session what kind of evaluation
		// it is inside. Saying it once, here, means no evaluation started by this call can be
		// mistaken for a watch or for ordinary work (backend_core.h, eval_complete).
		const ibBackendException::ibEvalModeScope answering(eval_complete);

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

		// ⭐⭐ WHICH OF THE TWO QUESTIONS IS BEING ASKED, and the TOKENS are what answer it — the
		// same way the editor decides between its two lists. This verb only ever answered the first
		// half, so standing anywhere but just after a dot produced an empty answer that read like
		// "nothing here" — including at the very start of an empty module, where in fact the whole
		// configuration is available.
		//
		// A lexer, not a compiler: what the text MEANS is asked below, of the compiler itself.
		ibTranslateCode lexem;
		lexem.SetLexemMode(ibLexemMode::Editing);
		lexem.Load(text);
		lexem.PrepareLexem();

		const ibTranslateCode::ibCaretText at = lexem.CaretAt((unsigned int)position);

		// ⭐⭐ THE ANSWER SAYS WHICH QUESTION IT ANSWERED. Two shapes come back from one verb, and
		// until now a caller told them apart by which FIELD happened to be present — inferring the
		// question from the shape of the answer, which is the same guessing this arc exists to
		// remove. `place` names it outright; `word` is what a list filters by and `expression` what
		// stands to the left, both of which a caller would otherwise re-derive from the text it
		// already sent.
		result.SetValue(wxT("place"),
			at.m_place == ibTranslateCode::ibCaretPlace::AfterDot   ? wxString(wxT("afterDot"))
			: at.m_place == ibTranslateCode::ibCaretPlace::InKeyword ? wxString(wxT("inKeyword"))
			:                                                          wxString(wxT("openCode")));

		if (!at.m_word.IsEmpty())
			result.SetValue(wxT("word"), at.m_word);
		if (!at.m_expression.IsEmpty())
			result.SetValue(wxT("expression"), at.m_expression);
		if (!at.m_keyword.IsEmpty())
			result.SetValue(wxT("keyword"), at.m_keyword);

		if (at.m_place != ibTranslateCode::ibCaretPlace::AfterDot)
			return ibAnswerWithScope(text, (int)position, module, result);

		// ⭐⭐ AND THE MEMBERS COME FROM THE COMPILER, not from a second reader of the same text.
		// The text is compiled tolerantly and the walk steps over the INSTRUCTIONS it produced:
		// `Catalogs.Goods.` emits "member Catalogs of the scope" and "attribute Goods" before the
		// trailing dot refuses, so what the caret stands on is already there to be read. Whatever
		// the language grows next — a type a plugin registered, a new step in a chain — is answered
		// here the moment the compiler understands it, because it IS the compiler
		// (compiler-ast-arc.md § Update 2026-09-07).
		// The position is handed over AS THE CALLER GAVE IT: what a caret means — a word still
		// being typed, the text read only up to it — is the walk's own definition and lives there
		// (scriptComplete.cpp), not in each caller that happens to hold a position.
		// ⭐⭐ AND WHEN IT REFUSES, IT SAYS WHERE IT BROKE. The compile keeps every refusal it made,
		// with a position on each; handing that back turns "no" into something a caller can act on
		// — and tells the two faults apart, because they need different fixes: a text that would
		// not compile, and a text that compiled while the chain stopped part way along it.
		std::vector<ibCaretValue> values;
		wxString refused;
		if (!ibValueAtCaret(text, (unsigned int)position, module, values, &refused)) {
			refusal = refused.IsEmpty()
				? ibMcpText("That text compiles, but the expression at that position does not "
					"resolve to a value — so there is nothing to offer its members. Something in "
					"the chain leads nowhere: a member that is not there, a method that returns "
					"nothing, a name this module was never given.")
				: wxString::Format(
					ibMcpText("Nothing at that position resolves to a value, and the text did not "
						"compile up to it:%s%s"), wxT("\n"), refused);
			return false;
		}

		// ⭐⭐ ONE ENTRY PER BRANCH, because a composite path arrives at more than one thing and each
		// of them is a different set of members. Merging them would hand back a list nothing
		// actually has; naming only the first would pick a branch on the caller behalf. One branch
		// is the ordinary case and reads exactly as a single answer always did (Max, 2026-09-08:
		// "two references are two separate outputs").
		std::vector<ibDataValue> branches;
		for (ibCaretValue& branchValue : values) {

			ibValue& value = branchValue.m_value;
			std::shared_ptr<ibDataNode> branch = std::make_shared<ibDataNode>();

			// ⭐⭐ WHOSE THIS VALUE IS, said in the same words the name list uses. The members say
			// what may be written next and the type says what it is; neither says whether the
			// reader is looking at the configuration's own data, at something this module bound, or
			// at the platform's built-in surface — and that is usually the first thing a reader
			// wants to know. It is read off the STEP that produced the value, so it costs nothing
			// and cannot be recovered anywhere else (scriptComplete.h).
			branch->SetValue(wxT("origin"), ibOriginName(branchValue.m_origin));

			// ⭐⭐ WHAT THE EXPRESSION TURNED OUT TO BE, AND IT IS SAID PER BRANCH. The members alone
			// say what may be written next; they do not say WHAT is being written on, and a caller
			// reading only a member list has to infer the thing from its parts — a catalog manager
			// from `CreateElement`, an array from `Add`. That is the same guessing this arc removes
			// everywhere else, so the value says its own name: it is the one fact the walk holds and
			// nobody else can recover. Said once for the whole answer it would name the last branch
			// and describe the others wrongly, which is worse than saying nothing.
			const wxString className = ClassNameOf(value);
			if (!className.IsEmpty())
				branch->SetValue(wxT("type"), className);

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

			// 🛑 WHAT A FIELD IS DECLARED AS IS ASKED OF THE METADATA, and that is a measurement,
			// not a preference: building a source explorer on a LIVE object reaches the database,
			// and for an object with no record yet it throws — leaving the object half-built, so
			// the very next question about it throws too, outside any guard. Three chains that had
			// answered all evening began refusing. The same door the walk widens through answers it
			// here for free (ibFieldTypesOf), so the type beside a name and the branch stepped into
			// can never disagree.
			std::vector<ibDataValue> properties;
			for (long i = 0; i < value.GetNProps(); i++) {
				// The same filter the editor applies: a scope-local name belongs to
				// the frame it was declared in, not to the object reached through a
				// chain.
				if (value.IsPropScoped(i))
					continue;

				const wxString name = value.GetPropName(i);

				std::vector<ibFieldType> types;
				ibFieldTypesOf(module, value, name, types);

				// ⭐⭐ A COMPOSITE FIELD ARRIVES ONCE PER TYPE. It is one name in the text and
				// several things to walk into: `Owner` that may be a Goods or a Warehouses
				// reference offers two different sets of members, and one entry saying "it is both"
				// leaves the next hop to be guessed. Repeating the name, each with the type it
				// stands for, makes the branch a CHOICE the caller can make instead of an ambiguity
				// it has to resolve (Max, 2026-09-08: *"composite fields should be duplicated"*).
				if (!types.empty()) {
					for (const ibFieldType& type : types) {
						std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
						entry->SetValue(wxT("name"), name);
						if (!type.m_name.IsEmpty())
							entry->SetValue(wxT("type"), type.m_name);
						properties.push_back(ibDataValue::Child(entry));
					}
					continue;
				}

				// Nothing declares it — a property of a platform object rather than a field of a
				// source.
				properties.push_back(ibDataValue::String(name));
			}

			branch->AddField(wxT("methods"), ibDataValue::Array(methods));
			branch->AddField(wxT("properties"), ibDataValue::Array(properties));
			branches.push_back(ibDataValue::Child(branch));
		}

		result.AddField(wxT("values"), ibDataValue::Array(branches));
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
// (ibParseCode): the same walk that fills the "Procedures and functions"
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

		ibParseCode parser;
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
