////////////////////////////////////////////////////////////////////////////
//	Description : FINDING A COMPOSITION WHEREVER IT LIVES - schema_read /
//	              schema_write. A report declares one; a dynamic list keeps
//	              one on a FORM, where nothing could reach it.
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ A FORM IS THE SAME TREE (Max, 2026-09-06: *"a form is the same tree — one tree that flows into
// another"*). Everything in this platform serialises into one universal node, and a form is not an
// exception to that, it is an instance of it: its controls are children, a dynamic list among them
// is a child, and the composition that list reads by is a subtree of THAT. Nothing has to be opened,
// and no live window has to exist.
//
// ⭐⭐ SO THE SEARCH IS ONE WALK, not a case per container. A composition writes its parts under
// SYNTHETIC clsids - CompositionVariant, CompositionOutput, CompositionParameter - and
// `ibCompositionNodeName` turns one back into its word and answers empty for anything else. That is
// the whole test: a node holding children with those names IS a schema, wherever it sits. Adding a
// second container later costs nothing here, which is the difference between a walk and a list of
// known places.
//
// 🛑 THE ADDRESS WAS WHAT WAS MISSING, NOT THE TYPE. `ibCompositionHolder` already gives a report,
// a list and a composer value one interface — but a tool finds its subject with `ibMcpObjectNamed`,
// BY NAME, in the metadata, and the only holder with a name there is the composer metaobject. A
// list's lives on a form and is not a metaobject at all. This is the address, and it is a PATH: the
// indices of the children to descend, which is what a tree gives you instead of a name.
//
// ⚠ AND WRITING IT BACK DOES NOT SAVE THE CONFIGURATION. `schema_write` puts the schema into the
// form and stops there, because applying is a separate act somebody agrees to (§ 8e.1). What it
// changes is pending until config_save, exactly like an edit made with a mouse.

#include "backend/mcp/mcpTool.h"

#include "backend/compositionDescription.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metadataConfiguration.h"

#include <memory>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The FORM's NodeId - metadata_tree and metadata_list give it. A report's own schema "
			  "needs none of this: report_get answers it directly."), /*required*/ true);
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(wxT("path"), ibArg::Kind::Text,
		ibMcpText("WHICH schema, as schema_read gave its `path` back - the indices of the children to "
			  "descend, slash-separated. A form with one list has one; a form with two has two, and "
			  "the path is the only thing that tells them apart."), /*required*/ true);
	return s_a;
}

const ibArg& ArgSchema()
{
	static const ibArg s_a(wxT("schema"), ibArg::Kind::Node,
		ibMcpText("The composition to put there, in the shape schema_read and report_get answer with. "
			  "It REPLACES what is at `path` - this is not a merge, and anything the old one had and "
			  "the new one does not is gone. The example is an empty one, written by the same reader "
			  "that will take yours."),
		/*required*/ true, std::vector<wxString>(),
		[](ibDataValue& shape) {
			return ibCompositionDescriptionMemory::WriteNode(shape, ibCompositionDescription());
		});
	return s_a;
}

// ⭐ IS THIS NODE A COMPOSITION? Asked of its CHILDREN, because that is where a composition puts
// itself: the holder node carries the parts, and the parts are what carry the synthetic types.
// `ibCompositionNodeName` answers with a word for one of those and with nothing for everything else,
// so this needs no list of its own to keep in step.
bool HoldsComposition(const ibDataNode& node)
{
	for (const ibDataNode& child : node.Children())
		if (!ibCompositionNodeName(child.GetClsid()).IsEmpty())
			return true;
	return false;
}

// The tree, walked once. `path` is where we are; a hit does not stop the descent, because a
// composition can sit inside something that holds another.
void FindSchemas(const ibDataNode& node, const wxString& path, std::vector<ibDataValue>& found)
{
	if (!path.IsEmpty() && HoldsComposition(node)) {
		std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
		entry->SetValue(wxT("path"), path);

		// WHAT IT IS, in the tree's own words when it has any — a reader deciding which of two
		// schemas to take needs something beside an index.
		const wxString kind = ibCompositionNodeName(node.GetClsid());
		if (!kind.IsEmpty())
			entry->SetValue(wxT("nodeType"), kind);

		std::shared_ptr<ibDataNode> schema = std::make_shared<ibDataNode>(node);
		entry->AddField(wxT("schema"), ibDataValue::Child(schema));

		found.push_back(ibDataValue::Child(entry));
	}

	for (size_t i = 0; i < node.Children().size(); ++i) {
		const wxString step = path.IsEmpty()
			? wxString::Format(wxT("%d"), (int)i)
			: path + wxT("/") + wxString::Format(wxT("%d"), (int)i);
		FindSchemas(node.Children()[i], step, found);
	}
}

// Descend to what `path` names, or null. Written for BOTH directions — a path that reads has to be a
// path that writes, or the pair is two addressing schemes wearing one name.
ibDataNode* NodeAt(ibDataNode& root, const wxString& path, wxString& refusal)
{
	ibDataNode* at = &root;
	wxString    rest = path;

	while (!rest.IsEmpty()) {
		wxString step = rest.BeforeFirst('/');
		rest = rest.AfterFirst('/');

		long index = -1;
		if (!step.ToLong(&index) || index < 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a path - it is the child indices to descend, like `3/1/0`, as "
					  "schema_read gives them."), path);
			return nullptr;
		}
		if ((size_t)index >= at->Children().size()) {
			refusal = wxString::Format(
				ibMcpText("There is nothing at '%s' in this form: step %d has %d child(ren). The form "
					  "has been edited since schema_read answered - read it again."),
				path, (int)index, (int)at->Children().size());
			return nullptr;
		}
		at = &at->Children()[(size_t)index];
	}

	return at;
}

// The form's control tree, as a node. A form's blob ALREADY IS the binary-provider node format, so
// this is one round trip and no window is involved.
std::shared_ptr<ibDataNode> FormTree(const ibDataNode& params, ibValueMetaObjectFormBase*& form,
	wxString& refusal)
{
	form = nullptr;

	ibValueMetaObject* const object = ibMcpObjectNamed(params, refusal);
	if (object == nullptr)
		return nullptr;

	form = object->ConvertToType<ibValueMetaObjectFormBase>();
	if (form == nullptr) {
		refusal = wxString::Format(
			ibMcpText("'%s' is not a form. A report's schema is report_get; this is for the schemas "
				  "that live on forms, which is where a dynamic list keeps its one."),
			object->GetName());
		return nullptr;
	}

	const ibDataValue tree = ibValueMetaObjectFormBase::FormBlobToNode(form->GetFormData());
	if (tree.Kind() != ibDataKind::Child || !tree.AsChild()) {
		refusal = wxString::Format(
			ibMcpText("'%s' has no form data to read - nothing has been laid out on it yet."),
			object->GetName());
		return nullptr;
	}

	return tree.AsChild();
}

//---------------------------------------------------------------------------
// schema_read
//---------------------------------------------------------------------------
class ibMcpToolSchemaRead : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("schema_read"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("reading the schemas on '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("EVERY COMPOSITION ON A FORM, with the address of each. A report declares its "
			"schema in metadata and report_get answers it; a DYNAMIC LIST keeps one on a form, "
			"and until now nothing could reach it without opening the form.\n"
			  "\n"
			"A form is the same universal tree everything else is, so this reads it - no window is "
			"opened and nothing is running. Each answer carries `path` (the child indices to "
			"descend - the address, and what schema_write takes) and `schema`, which goes straight "
			"into compose_run's `schema` to see what that list actually ANSWERS.\n"
			  "\n"
			"Two lists on one form give two answers, and the path is the only thing that tells them "
			"apart.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibValueMetaObjectFormBase* form = nullptr;
		const std::shared_ptr<ibDataNode> tree = FormTree(params, form, refusal);
		if (!tree)
			return false;

		std::vector<ibDataValue> found;
		FindSchemas(*tree, wxEmptyString, found);
		result.AddField(wxT("schemas"), ibDataValue::Array(found));

		// ⚠ NONE IS AN ANSWER, not an empty result to puzzle over: this form has no dynamic list on
		// it, which is a fact about the form rather than a failure to look.
		if (found.empty())
			result.SetValue(wxT("note"),
				ibMcpText("No composition on this form - nothing here reads by a schema. A form shows a "
					  "list only if one was put on it."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSchemaRead);

//---------------------------------------------------------------------------
// schema_write
//---------------------------------------------------------------------------
class ibMcpToolSchemaWrite : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("schema_write"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("changing a list's schema on '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("PUT A COMPOSITION BACK onto a form, at the `path` schema_read gave. That is the "
			"other half of reaching a list's schema: read it, work on it - report_* verbs shape a "
			"composition, compose_run says what it answers - and put it back.\n"
			  "\n"
			"It REPLACES what is at that path. Not a merge: whatever the old schema had and the new "
			"one does not is gone.\n"
			  "\n"
			"It does NOT save the configuration. What this changes is pending, exactly like an "
			"edit made with a mouse, until config_save applies it - and config_check says what is "
			"pending before you do.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgPath(), ArgSchema() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		const ibDataNode* given = params.FindChild(ArgSchema().Name());
		if (given == nullptr) {
			refusal = ibMcpText("No `schema` came with the call, so there is nothing to put there.");
			return false;
		}

		// ⚠ READ IT AS A COMPOSITION BEFORE WRITING IT ANYWHERE. A node that is not one would be
		// stored perfectly happily and fail later, in a form somebody opens, with no trace of where
		// it came from.
		ibCompositionDescription check;
		if (!ibCompositionDescriptionMemory::ReadNode(*given, check, activeMetaData)) {
			refusal = ibMcpText("`schema` could not be read as a composition, so it was not written. "
				"schema_read and report_get both answer with the shape one has.");
			return false;
		}

		ibValueMetaObjectFormBase* form = nullptr;
		const std::shared_ptr<ibDataNode> tree = FormTree(params, form, refusal);
		if (!tree)
			return false;

		ibDataNode* const at = NodeAt(*tree, ArgPath().Text(params), refusal);
		if (at == nullptr)
			return false;

		if (!HoldsComposition(*at)) {
			refusal = wxString::Format(
				ibMcpText("Nothing at '%s' is a composition, so writing one there would put a schema "
					  "where nothing reads it. schema_read lists the paths that are."),
				ArgPath().Text(params));
			return false;
		}

		// ⭐ THE NODE'S OWN IDENTITY STAYS. A control node is addressed by its clsid and id from the
		// tree around it; replacing those with the incoming node's would leave a schema in the right
		// place under the wrong name, which reads as the control having vanished.
		const ibClassID clsid  = at->GetClsid();
		const ibMetaID  metaId = at->GetMetaId();
		*at = *given;
		at->SetClsid(clsid);
		at->SetMetaId(metaId);

		form->SetFormData(ibValueMetaObjectFormBase::FormNodeToBlob(ibDataValue::Child(tree)));

		result.SetValue(wxT("written"), true);
		result.SetValue(wxT("note"),
			ibMcpText("Written into the form and NOT saved - config_check says what is now pending, "
				  "config_save applies it. compose_run with the schema you wrote says what it "
				  "answers before you commit to it."));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSchemaWrite);

} // namespace
