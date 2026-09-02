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
		ibMcpText("The whole module text. Write it in the syntax form this configuration uses - "
			  "help_search answers which one that is."), /*required*/ true);
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

		ibValueMetaObjectModuleBase* module =
			dynamic_cast<ibValueMetaObjectModuleBase*>(object);

		if (module == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a module."), object->GetName());
			return false;
		}

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
		return ibMcpText("Write the text of a module - a document's object module, a common module, a "
			"manager module. The text is compiled afterwards IN ITS OWN CONTEXT and the "
			"diagnostics come back, so a mistake is known immediately. The text is kept either "
			"way, exactly as it would be if a person typed it.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgText() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// The shared door, as module_patch beside it uses — one lookup, one refusal, and the
		// open-configuration test comes with it.
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		ibValueMetaObjectModuleBase* module =
			dynamic_cast<ibValueMetaObjectModuleBase*>(object);
		if (module == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a module."), object->GetName());
			return false;
		}

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

		return ibMcpWriteModule(module, incoming->AsString(), result, refusal);
	}
};

MCP_TOOL_REGISTER(ibMcpToolModuleWrite);
