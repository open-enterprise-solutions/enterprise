////////////////////////////////////////////////////////////////////////////
//	Description : changing part of a module without rewriting it
////////////////////////////////////////////////////////////////////////////
//
// 🛑 WRITING A WHOLE MODULE TO CHANGE ONE LINE IS NOT A COST, IT IS A HAZARD. `module_write`
// replaces the text with whatever the caller remembers — so anything a PERSON edited in the
// designer since that text was read is gone, silently, and the answer says "written: true".
// Nobody finds out until the change they made is missing.
//
// ⭐ A PATCH CANNOT DO THAT, and that is the whole point: it names the text it expects to replace.
// If the module has moved on, the fragment is not there and the patch REFUSES — the collision that
// a whole-file write cannot even detect becomes an ordinary, readable answer.
//
// ⚠ AND IT REFUSES ON AMBIGUITY TOO. A fragment occurring twice would be an edit to whichever one
// the search happened to reach first, which is a coin toss dressed as an edit. Say more of the
// surrounding text and it becomes a single place.
//
// The compiler still judges the result, exactly as it does for a whole write: a patch that applies
// cleanly and produces code that does not compile is reported with its diagnostics, not stored
// quietly.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/compiler/scriptCheck.h"   // ibCheckModule — the verdict the designer's Syntax gives
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"

namespace {

// ⭐⭐ THE ONE WAY A MODULE IS WRITTEN — store the text, mark the configuration, compile it WHERE IT
// LIVES, and answer with what came of it.
//
// Both verbs in this file end here: module_write decides the text from an argument, module_patch
// decides it by replacing something inside the old text, and after that there is nothing different
// left to do. A second copy of these fifteen lines would be a second answer shape for one action,
// and the caller would have to learn both.
//
// 🛑 IT WAS CALLED AND NEVER WRITTEN. module_patch has always called `ibMcpWriteModule`, and no such
// function existed anywhere in the tree — which stood only because the file it lives in was never
// listed in backend.vcxproj and therefore never compiled (found 2026-09-01, the same day the file
// was added to the project). Adding it to the build is what asked the question.
bool ibMcpWriteModule(ibValueMetaObjectModuleBase* module, const wxString& text,
	ibDataNode& result, wxString& refusal)
{
	if (module == nullptr) {
		refusal = ibMcpText("There is no module to write to.");
		return false;
	}

	module->SetModuleText(text);
	activeMetaData->Modify(true);

	// CHECKED WHERE IT LIVES, not as a loose string: the module sees its owner's attributes and
	// every exported name in the configuration, so this is the same verdict the designer's Syntax
	// control gives.
	const ibScriptCheckAnswer checked = ibCheckModule(module);

	result.AddField(wxT("written"), ibDataValue::Bool(true));

	// ⭐ HOW MUCH IS ACTUALLY IN THERE, read back from the module rather than counted off the
	// argument. "Checked and clean" is true of an EMPTY module too, so on its own it cannot tell a
	// caller whether their text arrived — this is the field that can, and it costs a getter.
	result.AddField(wxT("characters"),
		ibDataValue::Int((s64)module->GetModuleText().Length()));

	result.AddField(wxT("checked"),
		ibDataValue::Bool(checked.m_outcome == ibScriptCheckOutcome::Checked));

	// 🛑 `checked: false` IS NOT `diagnostics: []` — AND A BARE FLAG LETS IT BE READ AS ONE. Nothing
	// was compiled: no verdict was reached, and the empty diagnostics list beside it says only that
	// nobody looked. Measured on a live configuration, four documents answered `true` and the fifth
	// `false`, with no visible difference between them and nothing to act on.
	//
	// ⭐ AND THE REASON IS ALWAYS THE SAME ONE: a module reaches the compile cache in its owner's
	// OnAfterRunMetaObject, so a module that is not in it belongs to an object that has not been
	// RUN. That is a fact about the object, not about the text, and it is what the caller needs to
	// hear — the text IS stored either way.
	if (checked.m_outcome != ibScriptCheckOutcome::Checked)
		result.SetValue(wxT("checkedNote"),
			ibMcpText("The text is written, and NOTHING WAS COMPILED - so the empty `diagnostics` "
			  "means nobody looked, not that the module is clean. A module is compiled where it "
			  "lives, and it gets there when its owner is run; an object created but never run has "
			  "no compile module yet. Re-open the object (or apply the configuration) and write "
			  "again to get a verdict, and until then treat this module as unverified."));

	std::vector<ibDataValue> diagnostics;
	for (const ibDiagnostic& diagnostic : checked.m_diagnostics) {

		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->AddField(wxT("line"), ibDataValue::Int((s64)diagnostic.m_line));
		entry->SetValue(wxT("message"), diagnostic.m_message);
		if (!diagnostic.m_codeLine.IsEmpty())
			entry->SetValue(wxT("codeLine"), diagnostic.m_codeLine);

		diagnostics.push_back(ibDataValue::Child(entry));
	}

	result.AddField(wxT("diagnostics"), ibDataValue::Array(diagnostics));
	return true;
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		// ⚠ THE MODULE'S, NOT ITS OWNER'S — the one question a caller actually has here, and the
		// sentence used to answer it with "the same one module_write takes", inside module_write.
		// metadata_properties on the object names it: ObjectModule / ManagerModule carry their own id.
		ibMcpText("The MODULE's NodeId - not the object's. metadata_properties on the owner answers with it: "
		  "ObjectModule and ManagerModule each carry an id of their own."), /*required*/ true);
	return s_a;
}

const ibArg& ArgFind()
{
	static const ibArg s_a(wxT("find"), ibArg::Kind::Text,
		ibMcpText("The exact text to replace, whitespace and all. Include enough of the surrounding "
			  "lines to make it occur only once."), /*required*/ true);
	return s_a;
}

const ibArg& ArgReplace()
{
	static const ibArg s_a(wxT("replace"), ibArg::Kind::Text,
		ibMcpText("What to put there. Empty deletes the fragment."));
	return s_a;
}

const ibArg& ArgText()
{
	static const ibArg s_a(wxT("text"), ibArg::Kind::Text,
		ibMcpText("The whole module text - this REPLACES what is there, it does not add to it "
			  "(module_patch edits a part, module_read shows what is there now). Write it in "
			  "the syntax form this configuration uses - syntax_search answers which one that is."),
		/*required*/ true);
	return s_a;
}

const ibArg& ArgClear()
{
	static const ibArg s_a(wxT("clear"), ibArg::Kind::Flag,
		ibMcpText("Yes, erase this module's code - required when `text` is empty, so that emptying a "
			  "module is something asked for rather than something that happens."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// module_patch
//---------------------------------------------------------------------------

class ibMcpToolModulePatch : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("module_patch"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("patching the module '%s'"), ibMcpNameOf(params));
	}

	wxString GetDetail(const ibDataNode& params) const override
	{
		// WHAT IS GOING IN, shown the way the whole-write shows its text — a person watching should
		// see the change, not just that one happened.
		return ibMcpFencedExcerpt(ArgReplace().Text(params), wxT("oes"));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Replace ONE exact fragment of a module's text, leaving the rest untouched. Prefer "
			"this to module_write for anything short of a rewrite: a whole-module write silently "
			"discards whatever somebody edited in the designer since you read it, and this cannot "
			"- if the fragment is no longer there, it refuses and tells you. Refuses just as firmly "
			"when the fragment appears more than once, because then there is no single place it "
			"means. The result is compiled and the diagnostics come back, same as module_write.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgFind(), ArgReplace() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectModuleBase* module = ibMcpModuleOf(object, refusal);
		if (module == nullptr)
			return false;

		const wxString find = ArgFind().Text(params);

		if (find.IsEmpty()) {
			refusal = ibMcpText("Say what to replace - an empty `find` matches everywhere and nowhere.");
			return false;
		}

		const wxString before = module->GetModuleText();

		// ⭐ COUNTED, NOT JUST FOUND. "Is it there" and "is there exactly one" are different
		// questions, and only the second one licenses an edit.
		size_t occurrences = 0;
		size_t at = before.find(find);
		const size_t first = at;

		while (at != wxString::npos) {
			occurrences++;
			at = before.find(find, at + 1);
		}

		if (occurrences == 0) {
			refusal = ibMcpText("That text is not in the module. It may have been edited since you read "
				"it - read it again rather than writing the whole module over what is there now.");
			return false;
		}

		if (occurrences > 1) {
			refusal = wxString::Format(
				ibMcpText("That text appears %u times, so there is no single place it means. Include more "
				  "of the surrounding lines."), (unsigned)occurrences);
			return false;
		}

		wxString after = before;
		after.replace(first, find.length(), ArgReplace().Text(params));

		// THE SAME DOOR THE WHOLE WRITE USES, so a patched module is compiled, stored and reported
		// exactly like a written one — there is no second road here, only a different way of
		// deciding what the text should be.
		if (!ibMcpWriteModule(module, after, result, refusal))
			return false;

		result.SetValue(wxT("module"), module->GetName());
		result.AddField(wxT("replaced"), ibDataValue::Int(1));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolModulePatch);


//---------------------------------------------------------------------------
// module_read
//---------------------------------------------------------------------------
//
// 🛑⭐⭐ THE VERB THAT WAS MISSING, AND ITS ABSENCE COST A MODULE. There was a way to WRITE a module
// and a way to PATCH one, and no way to simply look at it: `module_outline` takes a TEXT, not an
// id, so "let me see what is in this module" lands on the only verb that takes an id and deals in
// module text — the writer. With an empty text a legitimate write, one call wipes it.
//
// It happened here, on 2026-09-02, to the assistant that wrote these tools: reaching for
// module_write to READ a document's posting module. Nothing was lost — the module was already
// empty, which is why the documents posted nothing — but that was luck, not design.
//
// ⭐ A WRITE WITH NO READING SIBLING INVITES EXACTLY THIS. A caller does not choose a verb out of a
// list; they reach for the one that names the thing they are holding. If the only verb that names
// a module is the one that replaces it, that is the verb they will reach for.
class ibMcpToolModuleRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("module_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("reading the module '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The text of a module, as it stands - an object module, a manager module, a "
			"common module. Line numbers are the ones the editor and debug_breakpoint count "
			"by, so a line read here is the line a breakpoint goes on. Read before writing: "
			"module_write REPLACES the whole text, and module_patch is the one that edits a "
			"part of it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		const ibValueMetaObjectModuleBase* module = nullptr;
		if (!object->ConvertToValue(module)) {
			refusal = wxString::Format(ibMcpText("'%s' is not a module."), object->GetName());
			return false;
		}

		const wxString text = module->GetModuleText();

		ibMcpSayObject(object, result);
		result.SetValue(wxT("text"), text);
		result.AddField(wxT("lines"), ibDataValue::Int(
			(s64)(text.IsEmpty() ? 0 : text.Freq('\n') + 1)));

		// ⭐ AN EMPTY MODULE IS AN ANSWER, and one worth spelling out: it looks exactly like a
		// module whose code failed to arrive, and the difference decides what to do next.
		if (text.IsEmpty())
			result.SetValue(wxT("note"),
				ibMcpText("This module is empty - it has no code at all. If something was expected to "
				  "happen here (a document posting, a form event), that is why it does not."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolModuleRead);

//---------------------------------------------------------------------------
// module_write
//---------------------------------------------------------------------------
class ibMcpToolModuleWrite : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("module_write"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		// ⭐ HOW MUCH CODE, not just which module. "Writing the module 'ObjectModule'" is the same
		// sentence for a one-line fix and for a rewrite of the whole thing — and those are the two
		// cases a person watching most wants to tell apart. The text is right here in the
		// arguments, so the size costs nothing to say.
		const wxString text = ArgText().Text(params);

		if (text.IsEmpty())
			return wxString::Format(ibMcpText("emptying the module '%s'"), ibMcpNameOf(params));

		return wxString::Format(ibMcpText("writing %u lines into the module '%s'"),
			(unsigned)(text.Freq('\n') + 1), ibMcpNameOf(params));
	}

	// THE CODE ITSELF, under that headline. This is the one a developer most wants to see without
	// going to the editor: what was actually put in their module, in their own language.
	wxString GetDetail(const ibDataNode& params) const override
	{
		return ibMcpFencedExcerpt(ArgText().Text(params), wxT("oes"));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("REPLACE the text of a module - a document's object module, a common module, a "
			"manager module. The whole text, not an addition: module_patch edits a part and "
			"module_read shows what is there now. Compiled afterwards IN ITS OWN CONTEXT and "
			"the diagnostics come back, so a mistake is known immediately. The text is kept "
			"either way, exactly as it would be if a person typed it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText(), ArgClear() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// The shared door, as module_patch beside it uses — one lookup, one refusal, and the
		// open-configuration test comes with it.
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectModuleBase* module = ibMcpModuleOf(object, refusal);
		if (module == nullptr)
			return false;

		// ⚠ A TEXT THAT IS NOT A STRING IS A REFUSAL, NOT AN EMPTY MODULE.
		//
		// GetValue<wxString> answers empty for anything that is not a String, so a caller who sent
		// an object — `"text": {"value": "Procedure …"}`, which is what a PowerShell object
		// serialises to when it was meant to be a plain string — had the module CLEARED and was
		// told `"written": true, "checked": true, diagnostics: []`. An empty module compiles
		// cleanly, so the success looked like the module's own: the same silence as `checked:
		// false`, wearing the other flag (2026-08-31, an evening lost to it).
		//
		// Emptiness itself stays legal — clearing a module is a real thing to ask for. The KIND is
		// what is checked, which is the same rule ibMcpSetProperty already applies to properties.
		const ibDataValue* incoming = params.FindField(ArgText().Name());

		if (incoming == nullptr || incoming->Kind() != ibDataKind::String) {
			refusal = ibMcpText("'text' must be a string holding the module's source. Nothing was written.");
			return false;
		}

		// 🛑⭐ AND AN EMPTY TEXT IS NOW ASKED FOR OUT LOUD. Clearing a module stays a real thing to
		// want - but it is not what somebody means when they arrive at this verb wanting to LOOK at
		// the module, which is where a caller with no reading verb ends up (module_read now exists
		// beside this one, and did not on the day this was written). The cost of the two is not
		// symmetric: a refused clear is one more call, a mistaken one is somebody's code.
		if (incoming->AsString().IsEmpty() && !ArgClear().Flag(params)) {
			refusal = ibMcpText("An empty text would erase this module's code. If that is what you mean, "
				"pass clear:true; if you wanted to SEE what is in it, module_read answers that. "
				"Nothing was written.");
			return false;
		}

		return ibMcpWriteModule(module, incoming->AsString(), result, refusal);
	}
};

MCP_TOOL_REGISTER(ibMcpToolModuleWrite);
