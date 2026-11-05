////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the record families' PREDEFINED ITEMS - declared, checked against the columns their kind
//	              offers, written with the configuration and read back
//
//	The items are one description the object keeps (m_predefinedDesc, core/description/predefinedDescription.h); what
//	an item is entered with is read off the tables the object declares and written into it (GetPredefinedDesc). The
//	door, the checks and how the items are stored live here, apart from the object itself, so the declared values they
//	need are included by this file alone.
////////////////////////////////////////////////////////////////////////////

#include "commonObject.h"
#include "backend/backend_type.h"                                         // GetTypesByFilter - the types a type is picked among

#include "backend/metaData.h"                                             // CreateObject / GetTypeCtor - what a type offers
#include "backend/objCtor.h"                                              // ibCtorMetaValueType - a reference's metaobject
#include "backend/metadataSerialization.h"                                // ibSerializeValue / ibDeserializeValue - a declared value, written and read
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/enumeration/metaEnumObject.h"            // an enumeration's members, declared values
#include "backend/metaCollection/partial/declaredPresentation.h"         // ibDeclaredTypeName - how a declared value reads
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"  // a section's columns, its line number
#include "backend/query/schemaSnapshot.h"                                 // ibSchemaSnapshot - what an item declares is read off it
#include "backend/system/value/composition/valueComposerField.h"         // ibValueCompositionPredefined - a declared reference
#include "backend/system/value/valueType.h"                              // ibValueTypeDescription - a characteristic's declared Type
#include "core/serialize/dataBuilder.h"                                  // node serialization (WriteData / ReadData)

//***************************************************************************
//*                       Predefined values                                 *
//***************************************************************************

#define predefinedBlock 0x1234532

namespace {

// A value as every value travels, and how it reads.
ibPredefinedCellDescription Packed(const ibValue& value)
{
	ibPredefinedCellDescription cell;
	cell.m_title = value.GetString();
	ibSerializeValue(value, cell.m_value);
	return cell;
}

// ⭐ A CODE OR A DESCRIPTION AS ITS ATTRIBUTE HOLDS IT (#646) — the door insures what it is given, however it came: run
// through the attribute's own AdjustValue, a number code the number its text reads as. Not declared stays not declared.
ibPredefinedCellDescription Adjusted(const ibMetaData* metaData, const ibValueMetaObjectAttributeBase* attribute,
	const ibPredefinedCellDescription& declared)
{
	if (declared.IsEmpty())
		return declared;
	return Packed(attribute->AdjustValue(ibDeserializeValue(declared.m_value, metaData)));
}

// ⭐ THE CODE A NEW ITEM IS GIVEN — the one after the greatest its kind declares, spelled as a written item's code is
// (GenerateIdentifier). Asked of the declaration alone: what some base holds is no business of a configuration, and a
// configuration numbers its own items as they are added (Max, 2026-11-02). A code that is not a whole number (`60.01`)
// is no counter and is passed over. A value of the code's type (#646); nothing reads in it where the kind has no code.
ibPredefinedCellDescription NextPredefinedCode(const ibValueMetaObjectRecordDataHierarchyMutableRef& owner)
{
	long long greatest = 0;
	for (const ibPredefinedItemDescription& item : owner.GetPredefinedValueArray()) {
		long long code = 0;
		if (item.m_code.m_title.ToLongLong(&code) && code > greatest)
			greatest = code;
	}
	return Packed(owner.GenerateIdentifier(ibNumber(greatest + 1)));
}

// ⭐ WHERE AN ITEM MAY STAND, AS THE KIND'S HIERARCHY SAYS. A folder only where the kind tells folders from items, and one
// that holds items stays one; a parent only where items stand under one another — one of the owner's items, a folder
// where the kind keeps folders, any item where every item is of one sort (an account under an account); never itself or
// one standing under it. Refused, naming the item.
void CheckPlace(const ibValueMetaObjectRecordDataHierarchyMutableRef& owner, const ibPredefinedItemDescription& item)
{
	if (item.m_folder && !owner.HasFolders())
		ibBackendCoreException::Error(_("'%s': '%s' keeps no folders"), item.m_name, owner.GetName());
	if (!item.m_folder && owner.HasFolders())
		for (const ibPredefinedItemDescription& held : owner.GetPredefinedValueArray())
			if (held.m_parent == item.m_guid)
				ibBackendCoreException::Error(_("'%s' holds '%s', so it stays a folder"), item.m_name, held.m_name);

	if (!item.m_parent.isValid())
		return;
	if (!owner.HasParentLink())
		ibBackendCoreException::Error(_("'%s': the items of '%s' do not stand under one another"), item.m_name, owner.GetName());
	const ibPredefinedItemDescription* const parent = owner.FindPredefinedValue(item.m_parent);
	if (parent == nullptr)
		ibBackendCoreException::Error(_("'%s': '%s' has no predefined item to sit under"), item.m_name, owner.GetName());
	if (owner.HasFolders() && !parent->m_folder)
		ibBackendCoreException::Error(_("'%s' is not a folder, so nothing can sit under it"), parent->m_name);
	for (const ibPredefinedItemDescription* above = parent; above != nullptr; above = owner.FindPredefinedValue(above->m_parent))
		if (above->m_guid == item.m_guid)
			ibBackendCoreException::Error(_("'%s' cannot sit under itself"), item.m_name);
}

// ONE VALUE OF A CLOSED SET, as the form offers it: how it reads, the value, packed as every value travels, and the
// name code says it by (none for a value typed).
ibPredefinedChoiceDescription DescribeChoice(const ibValue& value, const ibString& name)
{
	ibPredefinedChoiceDescription choice;
	choice.m_title = value.GetString();
	ibSerializeValue(value, choice.m_value);
	choice.m_name = name;
	return choice;
}

// A DECLARED VALUE OF A REFERENCE, as a configuration holds one (ibValueCompositionPredefined): the metaobject, which of its
// declared values, and how it reads — written as the designer's selector writes it (selectPredefined.cpp).
ibValue DeclaredValue(const ibValueMetaObjectRecordDataRef* recordRef, const ibGuid& guid, const ibString& name)
{
	return ibValue(new ibValueCompositionPredefined(recordRef->GetMetaID(), guid,
		ibDeclaredTypeName(recordRef) + ibT(".") + name));
}

// The same value: a declared one by what it names, anything else by its value.
bool SameValue(const ibValue& given, const ibValue& offered)
{
	ibValueCompositionPredefined* lhs = nullptr;
	ibValueCompositionPredefined* rhs = nullptr;
	if (given.ConvertToValue(lhs) && offered.ConvertToValue(rhs) && lhs != nullptr && rhs != nullptr)
		return lhs->GetMetaId() == rhs->GetMetaId() && lhs->GetGuid() == rhs->GetGuid();
	return given.CompareValueEQ(offered);
}

// ONE ATTRIBUTE AS THE FORM ASKS FOR IT: its name, its caption, its type, and the closed set it is chosen from — an
// enumeration's members, and the values the references it admits declare (their predefined items, their members). A
// configuration declares only what it declares, so a reference names nothing else.
ibPredefinedAttributeDescription Describe(const ibMetaData* metaData, const ibValueMetaObjectAttributeBase* attribute)
{
	ibPredefinedAttributeDescription described;
	described.m_id = attribute->GetMetaID();
	described.m_name = attribute->GetName();
	described.m_title = attribute->GetSynonym();
	described.m_help = attribute->GetComment();
	described.m_type = attribute->GetTypeDesc();
	described.m_holdsType = described.m_type.ContainType(g_valueTypeDescriptionCLSID);
	if (described.m_holdsType)
		ibBackendTypeConfigFactory::GetTypesByFilter(ibSelectorDataType::ibSelectorDataType_any, metaData, described.m_offered);
	described.m_required = attribute->FillCheck();

	for (const ibClassID& clsid : described.m_type.GetClsidList()) {
		if (IsEnum(clsid)) {
			ibValue members = metaData->CreateObject(clsid);
			for (long idx = 0; idx < members.GetNProps(); idx++) {
				ibValue member;
				if (members.GetPropVal(idx, member))
					described.m_choices.push_back(DescribeChoice(member, members.GetPropName(idx)));
			}
			continue;
		}
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(clsid);
		const auto* recordRef = so != nullptr && so->GetMetaTypeCtor() == ibCtorObjectMetaType_Reference
			? dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject()) : nullptr;
		if (const auto* enumeration = dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(recordRef))
			for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray())
				if (member != nullptr)
					described.m_choices.push_back(DescribeChoice(
						DeclaredValue(recordRef, member->GetGuid(), member->GetName()), member->GetName()));
		if (const auto* hierarchy = dynamic_cast<const ibValueMetaObjectRecordDataHierarchyMutableRef*>(recordRef))
			for (const ibPredefinedItemDescription& item : hierarchy->GetPredefinedValueArray())
				described.m_choices.push_back(DescribeChoice(
					DeclaredValue(recordRef, item.m_guid, item.m_name), item.m_name));
	}

	// ⭐ WHAT A NEW ITEM DECLARES WHERE IT MUST DECLARE — the attribute's own default, as the set carries it: of a closed
	// set, the member it is (a declaration, never the live value); a value ticked or typed, itself. A type is picked, and
	// so is a reference outside any set: a live one is runtime, and a configuration holds none.
	if (described.m_required && !described.m_holdsType) {
		const ibValue defaulted = attribute->CreateValue();
		const ibValueTypes held = defaulted.GetType();
		if (described.m_choices.empty() && (held == ibValueTypes::TYPE_BOOLEAN || held == ibValueTypes::TYPE_NUMBER
			|| held == ibValueTypes::TYPE_DATE || held == ibValueTypes::TYPE_STRING))
			described.m_default = DescribeChoice(defaulted, ibEmptyString);
		for (const ibPredefinedChoiceDescription& choice : described.m_choices)
			if (SameValue(defaulted, ibDeserializeValue(choice.m_value, metaData))) {
				described.m_default = choice;
				break;
			}
	}
	return described;
}

// ⭐ A VALUE THE ATTRIBUTE MAY BE GIVEN, or a refusal naming the item, the attribute and the value: one of the closed set
// where the value is of a kind the set is made of, a type inside the offered ones, a value of the attribute's type
// otherwise. ⚠ The set closes only its own kind: an attribute that holds a number OR an account takes any number, and a
// declared reference is one of the set or nothing — the item it named may have left the configuration since.
void CheckDeclaredValue(const ibMetaData* metaData, const ibString& item, const ibPredefinedAttributeDescription& attribute,
	const ibPredefinedCellDescription& cell)
{
	const ibValue value = ibDeserializeValue(cell.m_value, metaData);
	ibValueCompositionPredefined* declared = nullptr;
	bool closed = value.ConvertToValue(declared) && declared != nullptr;
	for (const ibPredefinedChoiceDescription& choice : attribute.m_choices) {
		const ibValue offered = ibDeserializeValue(choice.m_value, metaData);
		if (offered.GetClassType() != value.GetClassType())
			continue;
		if (SameValue(value, offered))
			return;
		closed = true;
	}
	if (closed)
		ibBackendCoreException::Error(_("'%s': '%s' is not a value '%s' may be given"), item, value.GetString(), attribute.m_name);

	ibValueTypeDescription* type = nullptr;
	if (value.ConvertToValue(type) && type != nullptr && !attribute.m_offered.empty()) {
		for (const ibClassID& clsid : type->GetTypeDesc().GetClsidList())
			if (std::find(attribute.m_offered.begin(), attribute.m_offered.end(), clsid) == attribute.m_offered.end())
				ibBackendCoreException::Error(_("'%s': '%s' may not name the type '%s'"), item, attribute.m_name,
					ibValue::GetNameObjectFromID(clsid));
		return;
	}

	if (!attribute.m_type.ContainType(value.GetClassType()) && !attribute.m_type.ContainType(value.GetType()))
		ibBackendCoreException::Error(_("'%s': '%s' takes no value of the type '%s'"), item, attribute.m_name, value.GetClassName());
}

// ⭐ WHAT AN ITEM DECLARES, AGAINST THE COLUMNS ITS KIND OFFERS: every value of an attribute or of a section's column the
// kind offers. What it does not offer is not asked about here — the door refuses to be given it (CheckGiven), the seed
// does not write it. How many rows a section takes is the kind's check before the update (its ContributeTables).
void CheckDeclaredItem(const ibMetaData* metaData, const ibString& name, const ibPredefinedDescription& described,
	const ibPredefinedItemDescription& item)
{
	for (const auto& cell : item.m_row)
		if (const ibPredefinedAttributeDescription* attribute = described.FindAttribute(cell.first))
			CheckDeclaredValue(metaData, name, *attribute, cell.second);

	for (const auto& table : item.m_tables) {
		const ibPredefinedTableDescription* section = described.FindTable(table.first);
		if (section == nullptr)
			continue;
		for (const auto& line : table.second)
			for (const auto& cell : line)
				if (const ibPredefinedAttributeDescription* column = section->FindAttribute(cell.first))
					CheckDeclaredValue(metaData, name, *column, cell.second);
	}
}

// ⭐ WHAT AN ITEM IS GIVEN TO DECLARE — the door's check, whole before anything changes: an attribute, a section or a column
// the kind does not offer, refused by its id; then every value, as the kept ones are checked (CheckDeclaredItem). What
// an item must declare is the kind's check before the update (its ContributeTables), not the door's.
void CheckGiven(const ibMetaData* metaData, const ibString& owner, const ibPredefinedDescription& described,
	const ibPredefinedItemDescription& item)
{
	for (const auto& cell : item.m_row)
		if (described.FindAttribute(cell.first) == nullptr)
			ibBackendCoreException::Error(_("'%s': a predefined item of '%s' declares no attribute #%d"), item.m_name, owner, cell.first);

	for (const auto& table : item.m_tables) {
		const ibPredefinedTableDescription* section = described.FindTable(table.first);
		if (section == nullptr)
			ibBackendCoreException::Error(_("'%s': a predefined item of '%s' declares no section #%d"), item.m_name, owner, table.first);
		for (const auto& line : table.second)
			for (const auto& cell : line)
				if (section->FindAttribute(cell.first) == nullptr)
					ibBackendCoreException::Error(_("'%s': '%s' has no column #%d"), item.m_name, section->m_name, cell.first);
	}

	CheckDeclaredItem(metaData, item.m_name, described, item);
}

}

const ibPredefinedDescription& ibValueMetaObjectRecordDataHierarchyMutableRef::GetPredefinedDesc()
{
	// The form written into the description kept — its items are not touched.
	ibPredefinedDescription& described = m_predefinedDesc;
	described.m_attributes.clear();
	described.m_tables.clear();
	described.m_parent = HasParentLink();
	described.m_folders = HasFolders();
	described.m_nextCode = NextPredefinedCode(*this);
	described.m_code = Describe(m_metaData, GetDataCode());
	described.m_description = Describe(m_metaData, GetDataDescription());

	// ⭐ WHAT AN ITEM DECLARES IS READ OFF THE SCHEMA — the tables this object declares (the record's ContributeTables,
	// the structure alone, no seed), so the window, the door and the check offer exactly the columns the seed writes.
	// The main table's columns but the item's own fields (this level's list: its reference, name, code, description,
	// folder, parent, deletion mark, version) and a column nobody writes (IsReadOnlyAttribute: an account's kind columns,
	// a copy of its analytics); every section's table - a row found by its owner and its position (m_seedKey) - and its
	// columns but the line number, which is the row's position and not something said about it.
	//
	// Only the kind's own attributes must be declared where they are fill-checked (an account's side: every posting asks
	// it); any other is the data's until an item declares it - required, a seed would write its default over the data.
	//
	// ⭐ A TYPE THE KIND'S OWN ATTRIBUTE HOLDS IS PICKED WITHIN WHAT THE KIND'S CHARACTERISTIC ADMITS — a characteristic's
	// Type, among its chart's Types of characteristics (a family taking its members): the type registry's own answer
	// (AllowValue), which only a chart of characteristic types registers. Undefined is not among them: the gate passes it
	// as an empty VALUE, and a characteristic is never OF it.
	const ibCtorMetaValueType* const characteristic = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic);
	ibSchemaSnapshot schema;
	ibValueMetaObjectRecordDataMutableRef::ContributeTables(schema);
	std::vector<ibValueMetaObjectAttributeBase*> fields;
	ibValueMetaObjectRecordDataHierarchyMutableRef::FillArrayObjectByPredefinedAttribute(fields);
	std::vector<ibValueMetaObjectAttributeBase*> kind;
	FillArrayObjectByPredefinedAttribute(kind);
	for (const ibSchemaTable& table : schema.Tables()) {
		if (table.m_id == GetMetaID()) {
			for (const ibSchemaColumn& column : table.m_columns) {
				const ibValueMetaObjectAttributeBase* attribute = FindAnyAttributeObjectByFilter(column.m_id);
				if (attribute == nullptr || IsReadOnlyAttribute(column.m_id)
					|| std::find(fields.begin(), fields.end(), attribute) != fields.end())
					continue;
				ibPredefinedAttributeDescription& declarable = described.m_attributes.emplace_back(Describe(m_metaData, attribute));
				if (std::find(kind.begin(), kind.end(), attribute) == kind.end()) {
					declarable.m_required = false;
					declarable.m_default.reset();
				}
				else if (declarable.m_holdsType && characteristic != nullptr) {
					std::erase_if(declarable.m_offered, [characteristic](const ibClassID& clsid) {
						return clsid == g_valueUndefinedCLSID || !characteristic->AllowValue(clsid);
					});
				}
			}
			continue;
		}
		const ibValueMetaObjectTableData* section = FindTableObjectByFilter(table.m_id);
		if (section == nullptr || table.m_seedKey.empty())
			continue;
		ibPredefinedTableDescription& declarable = described.m_tables.emplace_back();
		declarable.m_id = table.m_id;
		declarable.m_name = section->GetName();
		declarable.m_title = section->GetSynonym();
		// Each column's attribute out of the list the section's table was built from — not a find among the section's
		// own: a chart of accounts carries its breakdown kinds there, objects of the chart and columns of this table.
		const std::vector<ibValueMetaObjectAttributeBase*> listed = section->GetGenericAttributeArrayObject();
		for (const ibSchemaColumn& column : table.m_columns) {
			const auto attribute = std::find_if(listed.begin(), listed.end(),
				[&column](const ibValueMetaObjectAttributeBase* object) { return object->GetMetaID() == column.m_id; });
			if (attribute != listed.end() && column.m_id != section->GetNumberLine()->GetMetaID())
				declarable.m_columns.push_back(Describe(m_metaData, *attribute));
		}
	}
	return described;
}

//append predefined value
ibGuid ibValueMetaObjectRecordDataHierarchyMutableRef::AppendPredefinedValue(const ibPredefinedItemDescription& given)
{
	ibPredefinedItemDescription item = given;
	item.m_guid = wxNewUniqueGuid;
	CheckPlace(*this, item);

	// ⭐ …DECLARING WHAT IT MUST: a required attribute it was given nothing for, its default, the one the form offers —
	// whoever makes the item, an account is born with a side.
	const ibPredefinedDescription& described = GetPredefinedDesc();
	for (const ibPredefinedAttributeDescription& attribute : described.m_attributes)
		if (attribute.m_required && attribute.m_default && item.m_row.find(attribute.m_id) == item.m_row.end())
			item.m_row.emplace(attribute.m_id, ibPredefinedCellDescription{ attribute.m_default->m_title, attribute.m_default->m_value });
	CheckGiven(m_metaData, GetName(), described, item);

	if (item.m_code.IsEmpty())
		item.m_code = described.m_nextCode;
	item.m_code = Adjusted(m_metaData, GetDataCode(), item.m_code);
	item.m_description = Adjusted(m_metaData, GetDataDescription(), item.m_description);
	m_predefinedDesc.m_items.push_back(std::move(item));

	m_metaData->Modify(true);
	return m_predefinedDesc.m_items.back().m_guid;
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::SetPredefinedValue(const ibPredefinedItemDescription& given)
{
	CheckPlace(*this, given);
	CheckGiven(m_metaData, GetName(), GetPredefinedDesc(), given);

	ibPredefinedItemDescription item = given;
	item.m_code = Adjusted(m_metaData, GetDataCode(), item.m_code);
	item.m_description = Adjusted(m_metaData, GetDataDescription(), item.m_description);
	if (ibPredefinedItemDescription* const kept = m_predefinedDesc.FindItem(item.m_guid))
		*kept = std::move(item);
	else
		m_predefinedDesc.m_items.push_back(std::move(item));

	m_metaData->Modify(true);
}

void ibValueMetaObjectRecordDataHierarchyMutableRef::DeletePredefinedValue(const ibGuid& predefinedGuid) {

	std::erase_if(m_predefinedDesc.m_items,
		[&predefinedGuid](const ibPredefinedItemDescription& item) { return predefinedGuid == item.m_guid; });

	m_metaData->Modify(true);
}

//***************************************************************************
//*                       Save & load metaData                              *
//***************************************************************************

bool ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData(ibDataNode& node) const
{
	// predefined values -> the one schema of them, kept whole (ibPredefinedDescriptionMemory)
	ibDataValue predefined;
	ibPredefinedDescriptionMemory::WriteNode(predefined, m_predefinedDesc);
	node.SetProperty(ibT("Predefined"), predefined);

	node.SetProperty(m_propertyAttributePredefined->GetName(), m_propertyAttributePredefined->GetNodeValue());
	node.SetProperty(m_propertyAttributeCode->GetName(), m_propertyAttributeCode->GetNodeValue());
	node.SetProperty(m_propertyAttributeDescription->GetName(), m_propertyAttributeDescription->GetNodeValue());
	node.SetProperty(m_propertyAttributeParent->GetName(), m_propertyAttributeParent->GetNodeValue());
	node.SetProperty(m_propertyAttributeIsFolder->GetName(), m_propertyAttributeIsFolder->GetNodeValue());
	// A property absent from this pair does not survive the session that set it, and the failure is
	// silent because the default keeps working.
	node.SetProperty(m_propertyHierarchyType->GetName(), m_propertyHierarchyType->GetNodeValue());
	node.SetProperty(m_propertyDataPresentation->GetName(), m_propertyDataPresentation->GetNodeValue());

	return ibValueMetaObjectRecordDataMutableRef::WriteData(node);
}

bool ibValueMetaObjectRecordDataHierarchyMutableRef::ReadData(const ibDataNode& node)
{
	// ⭐ WHAT CANNOT BE READ RAISES, naming the object: read as nothing, a damaged configuration would load with items
	// that declare nothing, and the next apply would stop asserting what they declared.
	const ibDataValue predefined = node.GetProperty(ibT("Predefined"));
	if (predefined.Kind() != ibDataKind::Empty && !ibPredefinedDescriptionMemory::ReadNode(predefined, m_predefinedDesc))
		ibBackendCoreException::Error(_("'%s': its predefined items cannot be read"), GetName());

	m_propertyAttributePredefined->SetNodeValue(node.GetProperty(m_propertyAttributePredefined->GetName()));
	m_propertyAttributeCode->SetNodeValue(node.GetProperty(m_propertyAttributeCode->GetName()));
	m_propertyAttributeDescription->SetNodeValue(node.GetProperty(m_propertyAttributeDescription->GetName()));
	m_propertyAttributeParent->SetNodeValue(node.GetProperty(m_propertyAttributeParent->GetName()));
	m_propertyAttributeIsFolder->SetNodeValue(node.GetProperty(m_propertyAttributeIsFolder->GetName()));

	m_propertyHierarchyType->SetNodeValue(node.GetProperty(m_propertyHierarchyType->GetName()));
	// Restate it on the Parent field at once: everything that asks what a parent may be asks the
	// FIELD, so a configuration loaded and never edited must already say what it declares.
	ApplyHierarchyType();

	// Absent from a configuration saved before it existed: the kind's own default stands (SetNodeValue
	// leaves an empty value alone) - Description for a catalog, Code for a chart of accounts.
	m_propertyDataPresentation->SetNodeValue(node.GetProperty(m_propertyDataPresentation->GetName()));

	return ibValueMetaObjectRecordDataMutableRef::ReadData(node);
}
