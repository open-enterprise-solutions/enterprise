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
// rows. They are cells the declaration owns, like the parent (commonObject.h, m_predefinedDesc), so they
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

#include <algorithm>   // std::find - a type among the ones offered

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/metadataSerialization.h"            // ibSerializeValue / ibDeserializeValue — a declared value, as the description carries it
#include "backend/objCtor.h"                          // ibCtorAbstractType — a type by its name
#include "backend/system/value/valueType.h"           // ibValueTypeDescription — a characteristic's Type, as a value
#include "core/description/predefinedDescription.h"   // ibPredefinedDescription — what an item of the kind declares
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
		          "{\"Type\": \"String\"} - a type's name among the chart's types of characteristics, or the whole "
		          "description in the shape metadata_get gives ({\"Types\": [\"Number\"], \"Precision\": 15, "
		          "\"Scale\": 2}). A chart of accounts: {\"AccountType\": \"Active\" | \"Passive\" | \"ActivePassive\", "
		          "\"OffBalance\": true} and its accounting flags by name ({\"Quantitative\": true}). A reference names "
		          "a PREDEFINED item of the object it points at. null removes a cell. predefined_get says what each "
		          "attribute takes."));
	return s_a;
}

const ibArg& ArgLines()
{
	static const ibArg s_a(ibT("lines"), ibArg::Kind::Node,
		ibMcpText("The rows of a tabular section the item owns, by the section's name, in order - a chart of "
		          "accounts' analytics: {\"AccountDimensionKinds\": [{\"AccountDimensionKind\": \"Counterparties\", "
		          "\"SummaryOnly\": false}]}, with the chart's breakdown accounting flags by name beside them; a chart "
		          "of calculation types' related types: {\"Displacing\": [{\"CalculationType\": \"Salary\"}]} (and "
		          "Base, Leading). A kind or a type is named by a PREDEFINED item - a declaration can only point at "
		          "what is declared. The list replaces the rows the item declares; [] declares none, null stops "
		          "declaring the section. A row it stops declaring stays in the base, the data's."));
	return s_a;
}

const ibArg& ArgDelete()
{
	static const ibArg s_a(ibT("delete"), ibArg::Kind::Flag,
		ibMcpText("Remove the item of that name instead of adding one."));
	return s_a;
}

typedef ibValueMetaObjectRecordDataHierarchyMutableRef ibPredefinedOwner;

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
			ibMcpText("'%s' cannot have predefined items - only a catalog, a chart of accounts, a "
			  "chart of characteristic types or a chart of calculation types can."), object->GetName());
		return nullptr;
	}

	return owner;
}

// WHAT AN ATTRIBUTE TAKES, by the names a caller says: the types a type is chosen among (a characteristic's, its chart's
// types of characteristics), or the values of its closed set (a member's name, a predefined item's). Empty: a value of
// its type, said as it is.
std::vector<ibString> Takes(const ibMetaData* metaData, const ibPredefinedAttributeDescription& attribute)
{
	std::vector<ibString> takes;
	if (attribute.m_holdsType)
		for (const ibClassID& clsid : attribute.m_offered)
			if (const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(clsid))
				takes.push_back(ctor->GetClassName());
	for (const ibPredefinedChoiceDescription& choice : attribute.m_choices)
		takes.push_back(choice.m_name.IsEmpty() ? choice.m_title : choice.m_name);
	return takes;
}

// …as one line of a refusal.
ibString TakesText(const std::vector<ibString>& takes)
{
	ibString text;
	for (const ibString& name : takes)
		text << (text.IsEmpty() ? ibT("") : ibT(", ")) << name;
	return text;
}

// WHAT A DECLARED VALUE IS SAID AS, BY WHAT THE KIND OFFERS (GetPredefinedDesc) — the caller names a word, the
// description decides what it means: a type its name (or a whole description), one of a closed set its name (a member's,
// a predefined item's — a declaration can only point at what is declared), anything else the platform's own conversion.
// A word the description cannot take is refused in words here; what it takes, the door checks again when it is written.
bool ReadCell(const ibMetaData* metaData, const ibPredefinedAttributeDescription& attribute,
	const ibDataValue& given, ibValue& stored, ibString& refusal)
{
	if (given.Kind() == ibDataKind::Empty) {
		stored = ibValue();   // removes the cell
		return true;
	}

	if (attribute.m_holdsType) {
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
					attribute.m_name);
				return false;
			}
		}
		if (described.GetClsidCount() == 0) {
			refusal = ibString::Format(ibMcpText("'%s' takes a type's name or the description metadata_get shows."),
				attribute.m_name);
			return false;
		}
		// Only among the types offered — a characteristic's Type within its chart's types of characteristics.
		if (!attribute.m_offered.empty())
			for (const ibClassID& clsid : described.GetClsidList())
				if (std::find(attribute.m_offered.begin(), attribute.m_offered.end(), clsid) == attribute.m_offered.end()) {
					const ibCtorAbstractType* const ctor = metaData->GetAvailableCtor(clsid);
					refusal = ibString::Format(ibMcpText("'%s' may not name the type '%s' - it names: %s."), attribute.m_name,
						ctor != nullptr ? ctor->GetClassName() : ibString(), TakesText(Takes(metaData, attribute)));
					return false;
				}
		stored = ibValue(new ibValueTypeDescription(described));
		return true;
	}

	if (given.Kind() == ibDataKind::String && !attribute.m_choices.empty()) {
		for (const ibPredefinedChoiceDescription& choice : attribute.m_choices)
			if (stringUtils::IsSameAs(choice.m_name, given.AsString()) || stringUtils::IsSameAs(choice.m_title, given.AsString())) {
				stored = ibDeserializeValue(choice.m_value, metaData);
				return true;
			}
		refusal = ibString::Format(ibMcpText("'%s' is not a value %s takes - it takes: %s."),
			given.AsString(), attribute.m_name, TakesText(Takes(metaData, attribute)));
		return false;
	}

	ibValue stated;
	switch (given.Kind()) {
	case ibDataKind::Bool:   stated = ibValue(given.AsBool());   break;
	case ibDataKind::Number: stated = ibValue(given.AsNumber()); break;
	case ibDataKind::String: stated = ibValue(given.AsString()); break;
	default:
		refusal = ibString::Format(ibMcpText("'%s' takes a plain value here - a string, a number or a flag."), attribute.m_name);
		return false;
	}
	const ibValue made = ibValueTypeDescription::AdjustValue(attribute.m_type, stated, metaData);
	if (made.GetClassType() != stated.GetClassType() && !stated.IsEmpty() && made.IsEmpty()) {
		// Where no plain value fits, the attribute is a closed set with nothing in it yet: a reference to an object that
		// declares no item.
		bool plain = false;
		for (const ibClassID& clsid : attribute.m_type.GetClsidList()) {
			const ibValueTypes held = ibValue::GetVTByID(clsid);
			plain = plain || held == ibValueTypes::TYPE_BOOLEAN || held == ibValueTypes::TYPE_NUMBER
				|| held == ibValueTypes::TYPE_DATE || held == ibValueTypes::TYPE_STRING;
		}
		refusal = plain
			? ibString::Format(ibMcpText("'%s' cannot hold '%s'."), attribute.m_name, stated.GetString())
			: ibString::Format(ibMcpText("'%s' names a predefined item, and the object it points at declares none yet - "
				"declare one there first."), attribute.m_name);
		return false;
	}
	stored = made;
	return true;
}

// The attribute of that name among the ones the kind describes — refused naming the ones there are.
const ibPredefinedAttributeDescription* CellAttribute(const std::vector<ibPredefinedAttributeDescription>& declarable,
	const ibString& name, const ibString& where, ibString& refusal)
{
	ibString names;
	for (const ibPredefinedAttributeDescription& attribute : declarable) {
		if (stringUtils::IsSameAs(attribute.m_name, name))
			return &attribute;
		names << (names.IsEmpty() ? ibT("") : ibT(", ")) << attribute.m_name;
	}
	refusal = names.IsEmpty()
		? ibString::Format(ibMcpText("A predefined item of '%s' declares nothing but its name - '%s' is not one of its cells."), where, name)
		: ibString::Format(ibMcpText("'%s' is not something a predefined item of '%s' declares. It declares: %s."), name, where, names);
	return nullptr;
}

// A value read, as the description keeps it: packed as every value travels, with how it reads.
ibPredefinedCellDescription Packed(const ibValue& value)
{
	ibPredefinedCellDescription cell;
	cell.m_title = value.GetString();
	ibSerializeValue(value, cell.m_value);
	return cell;
}

// A code or a description as the caller said it, read by what the kind says it is (described.m_code, m_description, #646)
// as a cell is: a number code the number the word reads as, a word it cannot hold refused. Said empty: not declared.
bool ReadField(const ibMetaData* metaData, const ibPredefinedAttributeDescription& field, const ibDataValue& given,
	ibPredefinedCellDescription& cell, ibString& refusal)
{
	if (given.Kind() == ibDataKind::String && given.AsString().IsEmpty()) {
		cell = ibPredefinedCellDescription();   // not a number 0: nothing said
		return true;
	}
	ibValue stored;
	if (!ReadCell(metaData, field, given, stored, refusal))
		return false;
	cell = stored.GetType() == ibValueTypes::TYPE_EMPTY ? ibPredefinedCellDescription() : Packed(stored);
	return true;
}

// `values` and `lines` as the caller sent them, laid over what the item declares (nothing, for an item being added) by
// what the kind offers: a value null removes its cell, a section named gets exactly the rows given. Nothing is changed
// here: the door takes the whole of it or none, so an item is never left half-declared.
bool ReadDeclared(const ibPredefinedOwner* owner, const ibPredefinedDescription& described, const ibDataNode& params,
	ibPredefinedItemDescription& item, ibString& refusal)
{
	const ibMetaData* const metaData = owner->GetMetaData();

	if (const auto values = params.FindChild(ArgValues().Name())) {
		for (const auto& field : values->Fields()) {
			const ibPredefinedAttributeDescription* attribute = CellAttribute(described.m_attributes, field.first, owner->GetName(), refusal);
			if (attribute == nullptr)
				return false;
			ibValue stored;
			if (!ReadCell(metaData, *attribute, field.second, stored, refusal))
				return false;
			if (stored.GetType() == ibValueTypes::TYPE_EMPTY)
				item.m_row.erase(attribute->m_id);
			else
				item.m_row.insert_or_assign(attribute->m_id, Packed(stored));
		}
	}

	if (const auto sections = params.FindChild(ArgLines().Name())) {
		for (const auto& field : sections->Fields()) {
			const ibPredefinedTableDescription* table = nullptr;
			ibString names;
			for (const ibPredefinedTableDescription& candidate : described.m_tables) {
				if (stringUtils::IsSameAs(candidate.m_name, field.first))
					table = &candidate;
				names << (names.IsEmpty() ? ibT("") : ibT(", ")) << candidate.m_name;
			}
			if (table == nullptr) {
				refusal = names.IsEmpty()
					? ibString::Format(ibMcpText("A predefined item of '%s' owns no section rows."), owner->GetName())
					: ibString::Format(ibMcpText("'%s' is not a section a predefined item of '%s' owns. It owns: %s."),
						field.first, owner->GetName(), names);
				return false;
			}
			if (field.second.Kind() == ibDataKind::Empty) {
				item.m_tables.erase(table->m_id);   // not declared: its rows are the data's
				continue;
			}
			if (field.second.Kind() != ibDataKind::Array) {
				refusal = ibString::Format(ibMcpText("'%s' takes a list of rows, each {column: value}, or null."), table->m_name);
				return false;
			}

			auto& rows = item.m_tables[table->m_id];
			rows.clear();
			for (const ibDataValue& entry : field.second.AsArray()) {
				const auto given = entry.AsChild();
				if (!given) {
					refusal = ibString::Format(ibMcpText("Each row of '%s' is an object, {column: value}."), table->m_name);
					return false;
				}
				ibPredefinedRowDescription line;
				for (const auto& cell : given->Fields()) {
					const ibPredefinedAttributeDescription* column = CellAttribute(table->m_columns, cell.first, table->m_name, refusal);
					if (column == nullptr)
						return false;
					ibValue stored;
					if (!ReadCell(metaData, *column, cell.second, stored, refusal))
						return false;
					if (stored.GetType() != ibValueTypes::TYPE_EMPTY)
						line.insert_or_assign(column->m_id, Packed(stored));
				}
				rows.push_back(std::move(line));
			}
		}
	}

	return true;
}

// A declared value as the caller would say it back — the word predefined_add takes, so an answer can be sent back as it
// is: a type as its shape, one of a closed set by its name, anything else as it is.
ibDataValue SayCell(const ibMetaData* metaData, const ibPredefinedAttributeDescription& attribute, const ibPredefinedCellDescription& cell)
{
	const ibValue value = ibDeserializeValue(cell.m_value, metaData);
	if (ibValueTypeDescription* described = nullptr; value.ConvertToValue(described) && described != nullptr) {
		ibDataValue shape;
		if (ibTypeDescriptionMemory::WriteNode(shape, described->GetTypeDesc()))
			return shape;
	}

	for (const ibPredefinedChoiceDescription& choice : attribute.m_choices)
		if (!choice.m_name.IsEmpty() && choice.m_title == cell.m_title)
			return ibDataValue::String(choice.m_name);

	switch (value.GetType()) {
	case ibValueTypes::TYPE_BOOLEAN: return ibDataValue::Bool(value.GetBoolean());
	case ibValueTypes::TYPE_NUMBER:  return ibDataValue::Number(value.GetNumber());
	default:                         return ibDataValue::String(value.GetString());
	}
}

// One item, in the words the tools take — what it declares said by the columns the kind offers (`described`).
ibDataValue ItemEntry(const ibPredefinedOwner* owner, const ibPredefinedDescription& described, const ibPredefinedItemDescription& item)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();

	// The NAME is what code writes; the description is what a person reads. Both,
	// and named apart, for the same reason a query field keeps them apart.
	node->SetValue(ibT("name"), item.m_name);
	if (!item.m_code.IsEmpty())
		node->AddField(ibT("code"), SayCell(owner->GetMetaData(), described.m_code, item.m_code));
	if (!item.m_description.IsEmpty())
		node->AddField(ibT("description"), SayCell(owner->GetMetaData(), described.m_description, item.m_description));

	if (item.m_folder)
		node->AddField(ibT("folder"), ibDataValue::Bool(true));

	if (const ibPredefinedItemDescription* parent = owner->FindPredefinedValue(item.m_parent))
		node->SetValue(ibT("parent"), parent->m_name);

	// What it declares beyond that, in the words `values` and `lines` take, by what the kind offers. A cell of an
	// attribute that has since gone is not the kind's any more, and is left out.
	const ibMetaData* const metaData = owner->GetMetaData();

	if (!item.m_row.empty()) {
		std::shared_ptr<ibDataNode> values = std::make_shared<ibDataNode>();
		for (const auto& cell : item.m_row)
			if (const ibPredefinedAttributeDescription* attribute = described.FindAttribute(cell.first))
				values->AddField(attribute->m_name, SayCell(metaData, *attribute, cell.second));
		node->AddField(ibT("values"), ibDataValue::Child(values));
	}

	if (!item.m_tables.empty()) {
		std::shared_ptr<ibDataNode> lines = std::make_shared<ibDataNode>();
		for (const ibPredefinedTableDescription& table : described.m_tables) {
			const auto declared = item.m_tables.find(table.m_id);
			if (declared == item.m_tables.end())
				continue;
			std::vector<ibDataValue> rows;
			for (const auto& line : declared->second) {
				std::shared_ptr<ibDataNode> row = std::make_shared<ibDataNode>();
				for (const auto& cell : line)
					if (const ibPredefinedAttributeDescription* column = table.FindAttribute(cell.first))
						row->AddField(column->m_name, SayCell(metaData, *column, cell.second));
				rows.push_back(ibDataValue::Child(row));
			}
			lines->AddField(table.m_name, ibDataValue::Array(rows));
		}
		node->AddField(ibT("lines"), ibDataValue::Child(lines));
	}

	return ibDataValue::Child(node);
}

// One attribute or column an item may declare, and what it takes (Takes) — in the names predefined_add and predefined_set
// accept, so a caller picks instead of guessing; `required` where the kind's item must declare it.
ibDataValue AttributeEntry(const ibMetaData* metaData, const ibPredefinedAttributeDescription& attribute)
{
	std::shared_ptr<ibDataNode> node = std::make_shared<ibDataNode>();
	node->SetValue(ibT("name"), attribute.m_name);
	if (attribute.m_required)
		node->AddField(ibT("required"), ibDataValue::Bool(true));
	std::vector<ibDataValue> takes;
	for (const ibString& name : Takes(metaData, attribute))
		takes.push_back(ibDataValue::String(name));
	if (!takes.empty())
		node->AddField(ibT("takes"), ibDataValue::Array(std::move(takes)));
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

		const ibPredefinedDescription& described = owner->GetPredefinedDesc();
		std::vector<ibDataValue> items;
		for (const ibPredefinedItemDescription& item : described.m_items)
			items.push_back(ItemEntry(owner, described, item));

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

			const ibPredefinedItemDescription* const existing = owner->FindPredefinedValue(name);
			if (existing == nullptr) {
				refusal = ibString::Format(
					ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
				return false;
			}

			owner->DeletePredefinedValue(existing->m_guid);

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

		ibPredefinedItemDescription item;
		item.m_name = name;
		item.m_folder = ArgFolder().Flag(params);

		const ibString parentName = ArgParent().Text(params);
		if (!parentName.IsEmpty()) {

			const ibPredefinedItemDescription* const parent = owner->FindPredefinedValue(parentName);
			if (parent == nullptr) {
				refusal = ibString::Format(
					ibMcpText("There is no predefined item named '%s' to put this one under."), parentName);
				return false;
			}
			item.m_parent = parent->m_guid;

			// Whether the arrangement holds it there — only a folder where it keeps folders and items apart, any
			// item where it is a hierarchy of items (`631` and `632` under `63`) — is the engine's door to say
			// (AppendPredefinedValue), for this road and the designer's window alike.
		}

		// Read before anything is added: a word the object cannot take refuses the item whole. No code: the next one; no
		// description: its name.
		const ibPredefinedDescription& described = owner->GetPredefinedDesc();
		const ibString description = ArgDescription().Text(params);
		if (!ReadField(owner->GetMetaData(), described.m_code, ibDataValue::String(ArgCode().Text(params)), item.m_code, refusal)
			|| !ReadField(owner->GetMetaData(), described.m_description, ibDataValue::String(description.IsEmpty() ? name : description),
				item.m_description, refusal)
			|| !ReadDeclared(owner, described, params, item, refusal))
			return false;

		// Its fields and what it declares, through the one door: added whole, or refused and nothing added.
		const ibGuid added = owner->AppendPredefinedValue(item);

		// WHAT HAPPENED, in the caller's own words. Reporting the name back rather
		// than a bare success means a caller never has to guess whether the item it
		// meant to add is the item that now exists.
		result.AddField(ibT("added"), ibDataValue::Bool(true));
		result.SetValue(ibT("name"), name);
		result.SetValue(ibT("object"), owner->GetName());

		if (const ibPredefinedItemDescription* const made = owner->FindPredefinedValue(added))
			result.AddField(ibT("item"), ItemEntry(owner, described, *made));

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
			"their columns - each with what it `takes` (the types it may name, the values it may be given) and "
			"whether it is `required`.");
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
		const ibPredefinedItemDescription* const item = owner->FindPredefinedValue(name);
		if (item == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		// What an item of this object can declare — as the kind offers it (GetPredefinedDesc), the one schema the
		// designer's window is built of.
		const ibPredefinedDescription& described = owner->GetPredefinedDesc();
		result.AddField(ibT("item"), ItemEntry(owner, described, *item));

		const ibMetaData* const metaData = owner->GetMetaData();
		std::vector<ibDataValue> declares;
		for (const ibPredefinedAttributeDescription& attribute : described.m_attributes)
			declares.push_back(AttributeEntry(metaData, attribute));
		result.AddField(ibT("declares"), ibDataValue::Array(declares));

		std::shared_ptr<ibDataNode> owns = std::make_shared<ibDataNode>();
		for (const ibPredefinedTableDescription& table : described.m_tables) {
			std::vector<ibDataValue> columns;
			for (const ibPredefinedAttributeDescription& column : table.m_columns)
				columns.push_back(AttributeEntry(metaData, column));
			owns->AddField(table.m_name, ibDataValue::Array(columns));
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
		const ibPredefinedItemDescription* const item = owner->FindPredefinedValue(name);
		if (item == nullptr) {
			refusal = ibString::Format(ibMcpText("'%s' has no predefined item named '%s'."), owner->GetName(), name);
			return false;
		}

		// What is not passed stays as it is.
		ibPredefinedItemDescription changed = *item;
		const ibPredefinedDescription& described = owner->GetPredefinedDesc();
		if (ArgCode().Given(params)
			&& !ReadField(owner->GetMetaData(), described.m_code, ibDataValue::String(ArgCode().Text(params)), changed.m_code, refusal))
			return false;
		if (ArgDescription().Given(params) && !ReadField(owner->GetMetaData(), described.m_description,
				ibDataValue::String(ArgDescription().Text(params)), changed.m_description, refusal))
			return false;
		if (!ReadDeclared(owner, described, params, changed, refusal))
			return false;

		// The whole of it through the one door, which changes nothing when it refuses — and answered as the door kept it.
		owner->SetPredefinedValue(changed);

		result.AddField(ibT("changed"), ibDataValue::Bool(true));
		if (const ibPredefinedItemDescription* const kept = owner->FindPredefinedValue(changed.m_guid))
			result.AddField(ibT("item"), ItemEntry(owner, described, *kept));
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolPredefinedSet);
