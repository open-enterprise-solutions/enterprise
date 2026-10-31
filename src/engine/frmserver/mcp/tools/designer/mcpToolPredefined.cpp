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
// WHAT AN ITEM DECLARES BEYOND ITS NAME. A chart of characteristic types declares each characteristic's
// value Type; a chart of accounts each account's side, off-balance flag, accounting flags and analytics
// rows. They are cells the declaration owns, like the parent (commonObject.h, ibPredefinedCells), so they
// are said here, by the attribute's own name, and the next apply writes them (#278). The object says
// which it has - this file names none of them.
//
// ONLY THE FAMILIES THAT HAVE THEM. A catalog, a chart of accounts, a chart of
// characteristic types - everything built on the hierarchy base. A document has
// no predefined items and says so instead of answering an empty list, because an
// empty list would read as "it has none" rather than "it cannot have any".
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"                          // ibCtorAbstractType — a type by its name
#include "backend/system/value/valueType.h"           // ibValueTypeDescription — a characteristic's Type, as a value
#include "core/stringUtils.h"                         // IsSameAs - how this tree compares text

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
	static const ibArg s_a(ibT("id"), ibArg::Kind::Whole,
		ibMcpText("The catalog or chart's NodeId - metadata_tree and metadata_get give it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(ibT("name"), ibArg::Kind::Text,
		ibMcpText("The name code refers to it by. English, like every other name in a configuration."), /*required*/ true);
	return s_a;
}

const ibArg& ArgDescription()
{
	static const ibArg s_a(ibT("description"), ibArg::Kind::Text,
		ibMcpText("What a person sees. May be in the configuration's own language."));
	return s_a;
}

const ibArg& ArgCode()
{
	static const ibArg s_a(ibT("code"), ibArg::Kind::Text,
		ibMcpText("The item's code, when the object has a code at all."));
	return s_a;
}

const ibArg& ArgFolder()
{
	static const ibArg s_a(ibT("folder"), ibArg::Kind::Flag,
		ibMcpText("Declare it as a folder, so other predefined items can sit under it."));
	return s_a;
}

const ibArg& ArgParent()
{
	static const ibArg s_a(ibT("parent"), ibArg::Kind::Text,
		ibMcpText("The predefined item already declared here to put this one under - a FOLDER where the object "
		          "keeps folders and items apart, and any ITEM where it is a hierarchy of items (a chart of "
		          "accounts: 631 and 632 under 63). The parent belongs to the DECLARATION, not to the data - "
		          "set as data it is blanked by the next apply, because the declaration owns that cell."));
	return s_a;
}

const ibArg& ArgValues()
{
	static const ibArg s_a(ibT("values"), ibArg::Kind::Node,
		ibMcpText("What the item declares beyond its name, by the attribute's name - the cells the DECLARATION owns, "
		          "which the next apply writes and a data edit cannot keep. A chart of characteristic types: "
		          "{\"Type\": \"String\"} - a type's name, or the whole description in the shape metadata_get "
		          "gives ({\"Types\": [\"Number\"], \"Precision\": 15, \"Scale\": 2}). A chart of accounts: "
		          "{\"AccountType\": \"Active\" | \"Passive\" | \"ActivePassive\", \"OffBalance\": true} and its "
		          "accounting flags by name ({\"Quantitative\": true}). null removes a cell. Ask predefined_get what "
		          "an object takes."));
	return s_a;
}

const ibArg& ArgLines()
{
	static const ibArg s_a(ibT("lines"), ibArg::Kind::Node,
		ibMcpText("The rows of a tabular section the item owns, by the section's name, in order - a chart of "
		          "accounts' analytics: {\"AccountDimensionKinds\": [{\"AccountDimensionKind\": \"Counterparties\", "
		          "\"SummaryOnly\": false}]}. A kind is named by the PREDEFINED item of the bound chart of "
		          "characteristic types - a declaration can only point at what is declared. The list replaces the "
		          "section's rows; an empty list stops declaring them."));
	return s_a;
}

const ibArg& ArgDelete()
{
	static const ibArg s_a(ibT("delete"), ibArg::Kind::Flag,
		ibMcpText("Remove the item of that name instead of adding one."));
	return s_a;
}

typedef ibValueMetaObjectRecordDataHierarchyMutableRef ibPredefinedOwner;
typedef ibPredefinedOwner::ibPredefinedValueObject     ibPredefinedItem;

// The object named, and refused in words when it is one that cannot hold
// predefined items at all - which is a different answer from holding none.
ibPredefinedOwner* Owner(const ibDataNode& params, ibString& refusal)
{
	if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	const std::int32_t id = (std::int32_t)ArgObject().Whole(params);
	if (id <= 0) {
		refusal = ibMcpText("Pass the object's NodeId - metadata_list gives it.");
		return nullptr;
	}

	ibValueMetaObject* object = ibFindMetaObjectById(activeMetaData, (ibMetaID)id);
	if (object == nullptr) {
		refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %i."), (int)id);
		return nullptr;
	}

	ibPredefinedOwner* owner = dynamic_cast<ibPredefinedOwner*>(object);
	if (owner == nullptr) {
		refusal = ibString::Format(
			ibMcpText("'%s' cannot have predefined items - only a catalog, a chart of accounts or a "
			  "chart of characteristic types can."), object->GetName());
		return nullptr;
	}

	return owner;
}

// WHAT A DECLARED VALUE IS SAID AS, BY ITS ATTRIBUTE'S TYPE — the caller names a word, the type decides what
// it means: a characteristic's Type takes a type's name (or a whole description), an enumeration a member,
// a reference the predefined item it points at; anything else is the platform's own conversion. Something
// the type cannot take is refused in words rather than written empty.
bool ReadCell(const ibPredefinedOwner* owner, const ibValueMetaObjectAttributeBase* attribute,
	const ibDataValue& given, ibDataValue& stored, ibString& refusal)
{
	ibMetaData* metaData = activeMetaData;
	const ibTypeDescription& type = attribute->GetTypeDesc();
	const ibClassID clsid = type.GetClsidCount() == 1 ? type.GetFirstClsid() : ibClassID(0);

	if (given.Kind() == ibDataKind::Empty) {
		stored = ibDataValue();   // removes the cell
		return true;
	}

	if (clsid == g_valueTypeDescriptionCLSID) {
		ibTypeDescription described;
		if (given.Kind() == ibDataKind::String) {
			const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(given.AsString());
			if (ctor == nullptr) {
				refusal = ibString::Format(ibMcpText("'%s' is not a type this configuration knows. type_list shows the names."),
					given.AsString());
				return false;
			}
			described.SetDefaultMetaType(ctor->GetClassType());
		}
		else if (const auto shape = given.AsChild()) {
			// A type in the list may be said by its name, the way metadata_set_type takes one.
			std::shared_ptr<ibDataNode> copy = std::make_shared<ibDataNode>(*shape);
			if (auto types = copy->FindField(ibT("Types")); types && types->Kind() == ibDataKind::Array) {
				std::vector<ibDataValue> entries;
				for (const ibDataValue& item : types->AsArray()) {
					if (item.Kind() != ibDataKind::String) {
						entries.push_back(item);
						continue;
					}
					const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(item.AsString());
					if (ctor == nullptr) {
						refusal = ibString::Format(ibMcpText("'%s' in Types is not a type this configuration knows."), item.AsString());
						return false;
					}
					const std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->AddField(ibT("TypeId"), ibDataValue::String(ibString::Format(ibT("%llu"),
						static_cast<unsigned long long>(ctor->GetClassType()))));
					entries.push_back(ibDataValue::Child(entry));
				}
				copy->SetField(ibT("Types"), ibDataValue::Array(entries));
			}
			bool readable = false;
			try { readable = ibTypeDescriptionMemory::ReadNode(ibDataValue::Child(copy), described); }
			catch (...) { readable = false; }   // a value of the wrong kind anywhere in the shape — refused below
			if (!readable) {
				refusal = ibString::Format(ibMcpText("'%s' takes a type's name or the description metadata_get shows."),
					attribute->GetName());
				return false;
			}
		}
		if (described.GetClsidCount() == 0) {
			refusal = ibString::Format(ibMcpText("'%s' takes a type's name or the description metadata_get shows."),
				attribute->GetName());
			return false;
		}
		stored = owner->PackPredefinedCell(ibValue(new ibValueTypeDescription(described)));
		return true;
	}

	if (given.Kind() == ibDataKind::String && IsEnum(clsid)) {
		ibValue members = metaData->CreateObject(clsid);
		const long found = members.FindProp(given.AsString());
		ibValue member;
		if (found < 0 || !members.GetPropVal(found, member)) {
			ibString names;
			for (long idx = 0; idx < members.GetNProps(); idx++)
				names << (names.IsEmpty() ? ibT("") : ibT(", ")) << members.GetPropName(idx);
			refusal = ibString::Format(ibMcpText("'%s' is not a member of %s - it takes: %s."),
				given.AsString(), attribute->GetName(), names);
			return false;
		}
		stored = owner->PackPredefinedCell(member);
		return true;
	}

	if (given.Kind() == ibDataKind::String && IsReference(clsid)) {
		const ibPredefinedOwner* target = dynamic_cast<const ibPredefinedOwner*>(
			ibFindMetaObjectById(metaData, (ibMetaID)clsid_metaID(clsid)));
		const auto item = target != nullptr ? target->FindPredefinedValue(given.AsString()) : wxObjectDataPtr<ibPredefinedItem>();
		if (item == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' takes a predefined item of %s by its name, and there is none named '%s'."),
				attribute->GetName(), target != nullptr ? target->GetName() : metaData->GetNameObjectFromID(clsid), given.AsString());
			return false;
		}
		stored = owner->PackPredefinedCell(ibValuePtr<ibValueReferenceDataObject>(
			ibValueReferenceDataObject::Create(target, item->GetPredefinedGuid(), ibReferenceLoad::OnDemand)));
		return true;
	}

	ibValue stated;
	switch (given.Kind()) {
	case ibDataKind::Bool:   stated = ibValue(given.AsBool());   break;
	case ibDataKind::Number: stated = ibValue(given.AsNumber()); break;
	case ibDataKind::String: stated = ibValue(given.AsString()); break;
	default:
		refusal = ibString::Format(ibMcpText("'%s' takes a plain value here - a string, a number or a flag."), attribute->GetName());
		return false;
	}
	const ibValue made = ibValueTypeDescription::AdjustValue(type, stated, metaData);
	if (made.GetClassType() != stated.GetClassType() && !stated.IsEmpty() && made.IsEmpty()) {
		refusal = ibString::Format(ibMcpText("'%s' cannot hold '%s'."), attribute->GetName(), stated.GetString());
		return false;
	}
	stored = owner->PackPredefinedCell(made);
	return true;
}

// The attribute of that name among the ones a predefined item may declare — refused naming the ones there are.
const ibValueMetaObjectAttributeBase* CellAttribute(const std::vector<ibValueMetaObjectAttributeBase*>& declarable,
	const ibString& name, const ibString& where, ibString& refusal)
{
	ibString names;
	for (const ibValueMetaObjectAttributeBase* attribute : declarable) {
		if (attribute == nullptr)
			continue;
		if (stringUtils::IsSameAs(attribute->GetName(), name))
			return attribute;
		names << (names.IsEmpty() ? ibT("") : ibT(", ")) << attribute->GetName();
	}
	refusal = names.IsEmpty()
		? ibString::Format(ibMcpText("A predefined item of '%s' declares nothing but its name - '%s' is not one of its cells."), where, name)
		: ibString::Format(ibMcpText("'%s' is not something a predefined item of '%s' declares. It declares: %s."), name, where, names);
	return nullptr;
}

// `values` and `lines` as the caller sent them, read into what the item will hold. Nothing is changed until
// all of it reads: one wrong cell refuses the whole call, so an item is never left half-declared.
bool ReadDeclared(const ibPredefinedOwner* owner, const ibDataNode& params,
	ibPredefinedOwner::ibPredefinedCells& cells, ibPredefinedOwner::ibPredefinedLines& lines, ibString& refusal)
{
	if (const auto values = params.FindChild(ArgValues().Name())) {
		const std::vector<ibValueMetaObjectAttributeBase*> declarable = owner->GetPredefinedCellAttributes();
		for (const auto& field : values->Fields()) {
			const ibValueMetaObjectAttributeBase* attribute = CellAttribute(declarable, field.first, owner->GetName(), refusal);
			if (attribute == nullptr)
				return false;
			ibDataValue stored;
			if (!ReadCell(owner, attribute, field.second, stored, refusal))
				return false;
			cells[attribute->GetMetaID()] = stored;
		}
	}

	if (const auto sections = params.FindChild(ArgLines().Name())) {
		const std::vector<ibValueMetaObjectTableDataRef*> tables = owner->GetPredefinedLineTables();
		for (const auto& field : sections->Fields()) {
			const ibValueMetaObjectTableDataRef* table = nullptr;
			ibString names;
			for (const ibValueMetaObjectTableDataRef* candidate : tables) {
				if (candidate == nullptr || candidate->IsDeleted())
					continue;
				if (stringUtils::IsSameAs(candidate->GetName(), field.first))
					table = candidate;
				names << (names.IsEmpty() ? ibT("") : ibT(", ")) << candidate->GetName();
			}
			if (table == nullptr) {
				refusal = names.IsEmpty()
					? ibString::Format(ibMcpText("A predefined item of '%s' owns no section rows."), owner->GetName())
					: ibString::Format(ibMcpText("'%s' is not a section a predefined item of '%s' owns. It owns: %s."),
						field.first, owner->GetName(), names);
				return false;
			}
			if (field.second.Kind() != ibDataKind::Array) {
				refusal = ibString::Format(ibMcpText("'%s' takes a list of rows, each {column: value}."), table->GetName());
				return false;
			}

			const std::vector<ibValueMetaObjectAttributeBase*> columns = owner->GetPredefinedLineAttributes(table);
			std::vector<ibPredefinedOwner::ibPredefinedLine>& rows = lines[table->GetMetaID()];
			for (const ibDataValue& row : field.second.AsArray()) {
				const auto given = row.AsChild();
				if (!given) {
					refusal = ibString::Format(ibMcpText("Each row of '%s' is an object, {column: value}."), table->GetName());
					return false;
				}
				ibPredefinedOwner::ibPredefinedLine line{ wxNewUniqueGuid, {} };
				for (const auto& cell : given->Fields()) {
					const ibValueMetaObjectAttributeBase* column = CellAttribute(columns, cell.first, table->GetName(), refusal);
					if (column == nullptr)
						return false;
					ibDataValue stored;
					if (!ReadCell(owner, column, cell.second, stored, refusal))
						return false;
					if (stored.Kind() == ibDataKind::Child)
						line.m_cells[column->GetMetaID()] = stored;
				}
				rows.push_back(std::move(line));
			}
		}
	}

	return true;
}

// A declared value as the caller would say it back — the word predefined_add takes, so an answer can be sent
// back as it is.
ibDataValue SayCell(const ibPredefinedOwner* owner, const ibDataValue& stored)
{
	const ibValue value = owner->UnpackPredefinedCell(stored);

	if (ibValueTypeDescription* described = nullptr; value.ConvertToValue(described) && described != nullptr) {
		ibDataValue shape;
		if (ibTypeDescriptionMemory::WriteNode(shape, described->GetTypeDesc()))
			return shape;
	}

	const ibClassID clsid = value.GetClassType();
	if (IsReference(clsid)) {
		ibValueReferenceDataObject* reference = nullptr;
		const ibPredefinedOwner* target = dynamic_cast<const ibPredefinedOwner*>(
			ibFindMetaObjectById(activeMetaData, (ibMetaID)clsid_metaID(clsid)));
		if (target != nullptr && value.ConvertToValue(reference) && reference != nullptr)
			if (const auto item = target->FindPredefinedValue(ibGuid(reference->GetGuid())))
				return ibDataValue::String(item->GetPredefinedName());
	}

	if (IsEnum(clsid)) {
		ibValue members = activeMetaData->CreateObject(clsid);
		for (long idx = 0; idx < members.GetNProps(); idx++) {
			ibValue member;
			if (members.GetPropVal(idx, member) && member.CompareValueEQ(value))
				return ibDataValue::String(members.GetPropName(idx));
		}
	}

	switch (value.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: return ibDataValue::Bool(value.GetBoolean());
	case ibValueTypes::TYPE_NUMBER:  return ibDataValue::Number(value.GetNumber());
	default:                         return ibDataValue::String(value.GetString());
	}
}

// The name a cell is known by - its attribute's, looked up among the ones this object declares.
ibString CellName(const std::vector<ibValueMetaObjectAttributeBase*>& attributes, ibMetaID id)
{
	for (const ibValueMetaObjectAttributeBase* attribute : attributes)
		if (attribute != nullptr && attribute->GetMetaID() == id)
			return attribute->GetName();
	return ibString();
}

ibDataValue ItemEntry(const ibPredefinedOwner* owner, const ibPredefinedItem* item)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	// The NAME is what code writes; the description is what a person reads. Both,
	// and named apart, for the same reason a query field keeps them apart.
	node->SetValue(ibT("name"), item->GetPredefinedName());
	if (!item->GetPredefinedCode().IsEmpty())
		node->SetValue(ibT("code"), item->GetPredefinedCode());
	if (!item->GetPredefinedDescription().IsEmpty())
		node->SetValue(ibT("description"), item->GetPredefinedDescription());

	if (item->IsPredefinedFolder())
		node->AddField(ibT("folder"), ibDataValue::Bool(true));

	const ibString parent = item->GetPredefinedParentName();
	if (!parent.IsEmpty())
		node->SetValue(ibT("parent"), parent);

	// What it declares beyond that, in the words `values` and `lines` take. A cell of an attribute that has
	// since gone has no name to be said by, and is left out.
	if (!item->GetPredefinedCells().empty()) {
		const std::vector<ibValueMetaObjectAttributeBase*> attributes = owner->GetPredefinedCellAttributes();
		std::shared_ptr<ibDataNode> values = std::make_shared<ibDataNode>();
		for (const auto& cell : item->GetPredefinedCells()) {
			const ibString name = CellName(attributes, cell.first);
			if (!name.IsEmpty())
				values->AddField(name, SayCell(owner, cell.second));
		}
		node->AddField(ibT("values"), ibDataValue::Child(values));
	}

	if (!item->GetPredefinedLines().empty()) {
		std::shared_ptr<ibDataNode> lines = std::make_shared<ibDataNode>();
		for (const ibValueMetaObjectTableDataRef* table : owner->GetPredefinedLineTables()) {
			if (table == nullptr)
				continue;
			const auto declared = item->GetPredefinedLines().find(table->GetMetaID());
			if (declared == item->GetPredefinedLines().end())
				continue;
			const std::vector<ibValueMetaObjectAttributeBase*> columns = owner->GetPredefinedLineAttributes(table);
			std::vector<ibDataValue> rows;
			for (const ibPredefinedOwner::ibPredefinedLine& line : declared->second) {
				std::shared_ptr<ibDataNode> row = std::make_shared<ibDataNode>();
				for (const auto& cell : line.m_cells) {
					const ibString name = CellName(columns, cell.first);
					if (!name.IsEmpty())
						row->AddField(name, SayCell(owner, cell.second));
				}
				rows.push_back(ibDataValue::Child(row));
			}
			lines->AddField(table->GetName(), ibDataValue::Array(rows));
		}
		node->AddField(ibT("lines"), ibDataValue::Child(lines));
	}

	return ibDataValue::Child(node);
}


} // namespace

//---------------------------------------------------------------------------
// predefined_list
//---------------------------------------------------------------------------
class ibMcpToolPredefinedList : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("predefined_list"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("looking at the predefined items of '%s'"),
			ibMcpNameOf(params, ArgObject().Name()));
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		std::vector<ibDataValue> items;
		for (const auto& item : owner->GetPredefinedValueArray()) {
			if (item != nullptr)
				items.push_back(ItemEntry(owner, item.get()));
		}

		result.AddField(ibT("count"), ibDataValue::Int((std::int64_t)items.size()));
		result.AddField(ibT("items"), ibDataValue::Array(items));

		if (items.empty())
			result.SetValue(ibT("note"),
				ibMcpText("None declared. This object can have them - nothing has been added yet."));

		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolPredefinedList);

//---------------------------------------------------------------------------
// predefined_add
//---------------------------------------------------------------------------
class ibMcpToolPredefinedAdd : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("predefined_add"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ArgDelete().Flag(params)
				? ibMcpText("removing the predefined item '%s' from '%s'")
				: ibMcpText("adding the predefined item '%s' to '%s'"),
			ArgName().Text(params),
			ibMcpNameOf(params, ArgObject().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Declare a predefined item on a catalog or a chart - exactly as adding one in "
			"the designer would. The name is what code will refer to it by and must be unique "
			"within the object; the description is what a person sees. A chart's item also "
			"declares what it is - a characteristic its value Type, an account its side, "
			"off-balance flag, accounting flags and analytics - through `values` and `lines`; "
			"predefined_set changes them on an item already declared. To remove one, pass "
			"delete:true with its name.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject(), ArgName(), ArgDescription(), ArgCode(), ArgFolder(), ArgParent(), ArgValues(), ArgLines(), ArgDelete() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		const ibString name = ArgName().Text(params);
		if (name.IsEmpty()) {
			refusal = ibMcpText("A predefined item needs a name - that is what code will call it.");
			return false;
		}

		if (ArgDelete().Flag(params)) {

			const auto existing = owner->FindPredefinedValue(name);
			if (existing == nullptr) {
				refusal = ibString::Format(
					ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
				return false;
			}

			owner->DeletePredefinedValue(existing->GetPredefinedGuid());

			result.AddField(ibT("deleted"), ibDataValue::Bool(true));
			result.SetValue(ibT("name"), name);
			return true;
		}

		// THE UNIQUENESS CHECK IS THE POINT OF ASKING FIRST. Two items of one name
		// would make the name code refers to them by ambiguous, and the ambiguity
		// would only surface when the code ran.
		if (owner->HasPredefinedValue(name)) {
			refusal = ibString::Format(
				ibMcpText("'%s' already has a predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		wxObjectDataPtr<ibPredefinedItem> parent;
		const ibString parentName = ArgParent().Text(params);
		if (!parentName.IsEmpty()) {

			parent = owner->FindPredefinedValue(parentName);
			if (parent == nullptr) {
				refusal = ibString::Format(
					ibMcpText("There is no predefined item named '%s' to put this one under."), parentName);
				return false;
			}

			// A NON-FOLDER CANNOT HOLD ANYTHING — WHERE THE ARRANGEMENT HAS FOLDERS. Asked of the
			// owner, because it is the owner's arrangement that decides: a hierarchy of FOLDERS AND
			// ITEMS keeps the two apart and only a folder may hold; a hierarchy of ITEMS has no
			// folders at all and every node may hold children — `631` and `632` sit under `63`, which
			// is an account exactly like they are (commonObject.h: "an item-subordinated hierarchy
			// has no separate container kind").
			//
			// 🛑 Refused unconditionally, a chart of accounts could not declare its own structure: the
			// parent could only be set as DATA, and the apply's seed then blanked it — the declaration
			// owns that cell — so the tree came back empty on every apply and a trial balance in the
			// hierarchy of `63` was impossible (Max, 2026-09-16, found by building exactly that).
			if (owner->HasFolders() && !parent->IsPredefinedFolder()) {
				refusal = ibString::Format(
					ibMcpText("'%s' is not a folder, so nothing can sit under it."), parentName);
				return false;
			}
		}

		// Read before anything is added: a cell the object cannot take refuses the item whole.
		ibPredefinedOwner::ibPredefinedCells cells;
		ibPredefinedOwner::ibPredefinedLines lines;
		if (!ReadDeclared(owner, params, cells, lines, refusal))
			return false;

		const ibString description = ArgDescription().Text(params);

		owner->AppendPredefinedValue(name,
			ArgCode().Text(params),
			description.IsEmpty() ? name : description,
			ArgFolder().Flag(params),
			parent);

		if (!cells.empty() || !lines.empty())
			if (const auto added = owner->FindPredefinedValue(name))
				owner->SetPredefinedCells(added->GetPredefinedGuid(), cells, lines);

		// WHAT HAPPENED, in the caller's own words. Reporting the name back rather
		// than a bare success means a caller never has to guess whether the item it
		// meant to add is the item that now exists.
		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("name"), name);
		result.SetValue(ibT("object"), owner->GetName());

		if (const auto added = owner->FindPredefinedValue(name))
			result.AddField(ibT("item"), ItemEntry(owner, added.get()));

		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolPredefinedAdd);

//---------------------------------------------------------------------------
// predefined_get
//---------------------------------------------------------------------------
class ibMcpToolPredefinedGet : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("predefined_get"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("looking at the predefined item '%s' of '%s'"),
			ArgName().Text(params), ibMcpNameOf(params, ArgObject().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("One predefined item, whole - its name, code, description, parent, and what it declares "
			"beyond them (`values`, `lines`), in the words predefined_add and predefined_set take. Also says "
			"what an item of this object CAN declare: `declares` lists the attributes, `owns` the sections and "
			"their columns.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject(), ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		const ibString name = ArgName().Text(params);
		const auto item = owner->FindPredefinedValue(name);
		if (item == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		result.AddField(ibT("item"), ItemEntry(owner, item.get()));

		std::vector<ibDataValue> declares;
		for (const ibValueMetaObjectAttributeBase* attribute : owner->GetPredefinedCellAttributes())
			if (attribute != nullptr && !attribute->IsDeleted())
				declares.push_back(ibDataValue::String(attribute->GetName()));
		result.AddField(ibT("declares"), ibDataValue::Array(declares));

		std::shared_ptr<ibDataNode> owns = std::make_shared<ibDataNode>();
		for (const ibValueMetaObjectTableDataRef* table : owner->GetPredefinedLineTables()) {
			if (table == nullptr || table->IsDeleted())
				continue;
			std::vector<ibDataValue> columns;
			for (const ibValueMetaObjectAttributeBase* column : owner->GetPredefinedLineAttributes(table))
				columns.push_back(ibDataValue::String(column->GetName()));
			owns->AddField(table->GetName(), ibDataValue::Array(columns));
		}
		result.AddField(ibT("owns"), ibDataValue::Child(owns));
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolPredefinedGet);

//---------------------------------------------------------------------------
// predefined_set
//---------------------------------------------------------------------------
class ibMcpToolPredefinedSet : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("predefined_set"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("changing the predefined item '%s' of '%s'"),
			ArgName().Text(params), ibMcpNameOf(params, ArgObject().Name()));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Change a predefined item already declared - its code, its description, and what it "
			"declares beyond its name (`values`, `lines`, as predefined_add takes them). What is not passed "
			"stays as it is: `values` merge cell by cell (null removes one), a section named in `lines` "
			"gets exactly the rows given. The name stays - it is what code refers to the item by.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgObject(), ArgName(), ArgDescription(), ArgCode(), ArgValues(), ArgLines() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibPredefinedOwner* owner = Owner(params, refusal);
		if (owner == nullptr)
			return false;

		const ibString name = ArgName().Text(params);
		const auto item = owner->FindPredefinedValue(name);
		if (item == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		ibPredefinedOwner::ibPredefinedCells cells;
		ibPredefinedOwner::ibPredefinedLines lines;
		if (!ReadDeclared(owner, params, cells, lines, refusal))
			return false;

		if (ArgCode().Given(params) || ArgDescription().Given(params)) {
			owner->SetPredefinedValue(item->GetPredefinedGuid(), item->GetPredefinedName(),
				ArgCode().Given(params) ? ArgCode().Text(params) : item->GetPredefinedCode(),
				ArgDescription().Given(params) ? ArgDescription().Text(params) : item->GetPredefinedDescription(),
				item->IsPredefinedFolder(), item->GetPredefinedParent());
		}

		owner->SetPredefinedCells(item->GetPredefinedGuid(), cells, lines);

		result.AddField(ibT("changed"), ibDataValue::Bool(true));
		result.AddField(ibT("item"), ItemEntry(owner, item.get()));
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolPredefinedSet);
