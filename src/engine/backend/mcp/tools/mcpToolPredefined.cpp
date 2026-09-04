////////////////////////////////////////////////////////////////////////////
//	Description : predefined items - rows a configuration declares itself
////////////////////////////////////////////////////////////////////////////
//
// A PREDEFINED ITEM IS DATA THAT THE CONFIGURATION OWNS. Not a row somebody
// typed and may delete, but one the configuration declares must exist - a
// warehouse called Main, a status called New - so that code can name it instead
// of looking it up. That is why it lives in metadata and not in a table, and why
// it is created here rather than by writing a record.
//
// WHY IT IS ITS OWN TOOL RATHER THAN A PROPERTY. The item has four things to say
// at once - the name code refers to it by, the code and description a person
// sees, and whether it is a folder - and a folder can be the parent of the next
// one. That is a small structure, not a value, and pushing it through a generic
// property door is exactly the shape that lost a type silently once already.
//
// ONLY THE FAMILIES THAT HAVE THEM. A catalog, a chart of accounts, a chart of
// characteristic types - everything built on the hierarchy base. A document has
// no predefined items and says so instead of answering an empty list, because an
// empty list would read as "it has none" rather than "it cannot have any".
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"

namespace {
using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
// ⭐ `id`, BECAUSE THAT IS WHAT IT IS. It was `object` — and `object` elsewhere in this server means
// a NAME: lock_list takes 'Catalog.Products', role_grant takes 'Catalog.Goods' or 'Configuration'.
// One word standing for a number here and a string there is the trap a caller falls into once and
// then distrusts every argument. The rule this restores is worth more than either name: `id` IS a
// NodeId, `object` and `name` ARE names.
const ibArg& ArgObject()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The catalog or chart's NodeId - metadata_tree and metadata_get give it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("The name code refers to it by. English, like every other name in a configuration."), /*required*/ true);
	return s_a;
}

const ibArg& ArgDescription()
{
	static const ibArg s_a(wxT("description"), ibArg::Kind::Text,
		ibMcpText("What a person sees. May be in the configuration's own language."));
	return s_a;
}

const ibArg& ArgCode()
{
	static const ibArg s_a(wxT("code"), ibArg::Kind::Text,
		ibMcpText("The item's code, when the object has a code at all."));
	return s_a;
}

const ibArg& ArgFolder()
{
	static const ibArg s_a(wxT("folder"), ibArg::Kind::Flag,
		ibMcpText("Declare it as a folder, so other predefined items can sit under it."));
	return s_a;
}

const ibArg& ArgParent()
{
	static const ibArg s_a(wxT("parent"), ibArg::Kind::Text,
		ibMcpText("The name of a predefined FOLDER already declared here, to put this one inside it."));
	return s_a;
}

const ibArg& ArgDelete()
{
	static const ibArg s_a(wxT("delete"), ibArg::Kind::Flag,
		ibMcpText("Remove the item of that name instead of adding one."));
	return s_a;
}

typedef ibValueMetaObjectRecordDataHierarchyMutableRef ibPredefinedOwner;
typedef ibPredefinedOwner::ibPredefinedValueObject     ibPredefinedItem;

// The object named, and refused in words when it is one that cannot hold
// predefined items at all - which is a different answer from holding none.
ibPredefinedOwner* Owner(const ibDataNode& params, wxString& refusal)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	const s32 id = (s32)ArgObject().Whole(params);
	if (id <= 0) {
		refusal = ibMcpText("Pass the object's NodeId - metadata_list gives it.");
		return nullptr;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);
	if (object == nullptr) {
		refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %i."), (int)id);
		return nullptr;
	}

	ibPredefinedOwner* owner = dynamic_cast<ibPredefinedOwner*>(object);
	if (owner == nullptr) {
		refusal = wxString::Format(
			ibMcpText("'%s' cannot have predefined items - only a catalog, a chart of accounts or a "
			  "chart of characteristic types can."), object->GetName());
		return nullptr;
	}

	return owner;
}

ibDataValue ItemEntry(const ibPredefinedItem* item)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	// The NAME is what code writes; the description is what a person reads. Both,
	// and named apart, for the same reason a query field keeps them apart.
	node->SetValue(wxT("name"), item->GetPredefinedName());
	if (!item->GetPredefinedCode().IsEmpty())
		node->SetValue(wxT("code"), item->GetPredefinedCode());
	if (!item->GetPredefinedDescription().IsEmpty())
		node->SetValue(wxT("description"), item->GetPredefinedDescription());

	if (item->IsPredefinedFolder())
		node->AddField(wxT("folder"), ibDataValue::Bool(true));

	const wxString parent = item->GetPredefinedParentName();
	if (!parent.IsEmpty())
		node->SetValue(wxT("parent"), parent);

	return ibDataValue::Child(node);
}


} // namespace

//---------------------------------------------------------------------------
// predefined_list
//---------------------------------------------------------------------------
class ibMcpToolPredefinedList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("predefined_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking at the predefined items of '%s'"),
			ibMcpNameOf(params, ArgObject().Name()));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("The predefined items an object declares - the rows the configuration itself "
			"requires to exist, each with the name code refers to it by. Ask before writing "
			"code that names one, and before adding one, since the name must be unique.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		std::vector<ibDataValue> items;
		for (const auto& item : owner->GetPredefinedValueArray()) {
			if (item != nullptr)
				items.push_back(ItemEntry(item.get()));
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)items.size()));
		result.AddField(wxT("items"), ibDataValue::Array(items));

		if (items.empty())
			result.SetValue(wxT("note"),
				ibMcpText("None declared. This object can have them - nothing has been added yet."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPredefinedList);

//---------------------------------------------------------------------------
// predefined_add
//---------------------------------------------------------------------------
class ibMcpToolPredefinedAdd : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("predefined_add"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ArgDelete().Flag(params)
				? ibMcpText("removing the predefined item '%s' from '%s'")
				: ibMcpText("adding the predefined item '%s' to '%s'"),
			ArgName().Text(params),
			ibMcpNameOf(params, ArgObject().Name()));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Declare a predefined item on a catalog or a chart - exactly as adding one in "
			"the designer would. The name is what code will refer to it by and must be unique "
			"within the object; the description is what a person sees. To remove one, pass "
			"delete:true with its name.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject(), ArgName(), ArgDescription(), ArgCode(), ArgFolder(), ArgParent(), ArgDelete() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		const wxString name = ArgName().Text(params);
		if (name.IsEmpty()) {
			refusal = ibMcpText("A predefined item needs a name - that is what code will call it.");
			return false;
		}

		if (ArgDelete().Flag(params)) {

			const auto existing = owner->FindPredefinedValue(name);
			if (existing == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
				return false;
			}

			owner->DeletePredefinedValue(existing->GetPredefinedGuid());

			result.AddField(wxT("deleted"), ibDataValue::Bool(true));
			result.SetValue(wxT("name"), name);
			return true;
		}

		// THE UNIQUENESS CHECK IS THE POINT OF ASKING FIRST. Two items of one name
		// would make the name code refers to them by ambiguous, and the ambiguity
		// would only surface when the code ran.
		if (owner->HasPredefinedValue(name)) {
			refusal = wxString::Format(
				ibMcpText("'%s' already has a predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		wxObjectDataPtr<ibPredefinedItem> parent;
		const wxString parentName = ArgParent().Text(params);
		if (!parentName.IsEmpty()) {

			parent = owner->FindPredefinedValue(parentName);
			if (parent == nullptr) {
				refusal = wxString::Format(
					ibMcpText("There is no predefined item named '%s' to put this one under."), parentName);
				return false;
			}

			// A NON-FOLDER CANNOT HOLD ANYTHING. Allowing it would build a tree the
			// designer could not show and the runtime could not walk.
			if (!parent->IsPredefinedFolder()) {
				refusal = wxString::Format(
					ibMcpText("'%s' is not a folder, so nothing can sit under it."), parentName);
				return false;
			}
		}

		const wxString description = ArgDescription().Text(params);

		owner->AppendPredefinedValue(name,
			ArgCode().Text(params),
			description.IsEmpty() ? name : description,
			ArgFolder().Flag(params),
			parent);

		// WHAT HAPPENED, in the caller's own words. Reporting the name back rather
		// than a bare success means a caller never has to guess whether the item it
		// meant to add is the item that now exists.
		result.AddField(wxT("added"), ibDataValue::Bool(true));
		result.SetValue(wxT("name"), name);
		result.SetValue(wxT("object"), owner->GetName());

		if (const auto added = owner->FindPredefinedValue(name))
			result.AddField(wxT("item"), ItemEntry(added.get()));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolPredefinedAdd);
