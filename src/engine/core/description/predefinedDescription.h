#ifndef __PREDEFINED_DESCRIPTION_H__
#define __PREDEFINED_DESCRIPTION_H__

#include "core/core.h"                          // CORE_API, ibMetaID
#include "core/guid.h"                          // ibGuid — an item, the item it stands under
#include "core/description/typeDescription.h"   // ibTypeDescription — what an attribute holds
#include "core/serialize/dataBuilder.h"         // ibDataNode — a choice's value, packed

#include <map>
#include <optional>
#include <utility>   // std::as_const - the writable find asks the read one
#include <vector>

// WHAT A PREDEFINED ITEM OF ONE KIND DECLARES — the form an item is entered in, said by its kind and read by every window.
//
// A catalog's item has its parent, its name, its code and its description. A characteristic declares its value Type as
// well, an account its side, its flags and its analytics, a calculation type the calculation types it is related to. One
// form shows all of them, built from this: a row per attribute, its editor chosen by the attribute's type, and a table per
// section. The form knows no kind, so a kind added tomorrow describes itself and the form shows it (#278).
//
// …AND THE ITEMS THEMSELVES, each value packed as every value travels — the one schema of an object's predefined items,
// as a composition is a report's (compositionDescription.h). The object KEEPS it (ibValueMetaObjectRecordDataHierarchy-
// MutableRef::m_predefinedDesc), read and written with the configuration whole (ibPredefinedDescriptionMemory): its
// items, and what its kind alone knows, set by the kind when its property changes — how many rows a section takes, the
// types a type is chosen among; the rest of the columns is read off the tables the object declares whenever it is asked
// (GetPredefinedDesc). The seed is built of it, the designer's schema sends it whole, the thin designer draws it, edits
// an item and sends that item back in its own form (ibPredefinedItemDescriptionMemory), and MCP reads its words against
// it — one schema for both sides, so it lives here, as a virtual table's parameters do (core/query/querySourceParameter.h).

// ONE VALUE OF A CLOSED SET — an enumeration's member, a predefined item, an empty reference.
struct ibPredefinedChoiceDescription {
	ibString   m_title;   // how it reads: `ChartOfCharacteristicTypesRef.Kinds.Counterparties`, `Active`
	ibDataNode m_value;   // the value itself, as every value crosses the wire (ibSerializeValue)
	ibString   m_name;    // the name code says it by — a member's, an item's: `Counterparties`, `ActivePassive`
};

// ONE ATTRIBUTE AN ITEM DECLARES — a cell of the item's own row, or a column of a section's row.
struct ibPredefinedAttributeDescription {
	ibMetaID          m_id = 0;   // the attribute: the key the item keeps the value under
	ibString          m_name;
	ibString          m_title;    // its synonym, the caption
	ibString          m_help;
	ibTypeDescription m_type;     // what it holds, and therefore how it is edited

	// ITS VALUE IS A TYPE — a characteristic's Type: picked as a type is. Said here because a window cannot tell it from
	// the classes of m_type; the value itself travels as every value does.
	bool m_holdsType = false;

	// ALWAYS DECLARED — the attribute is fill-checked (an account's side: every posting asks it). Not declaring it is not
	// offered, and an item is made declaring the default.
	bool m_required = false;
	// …the value a new item declares: the attribute's own default, where it has one.
	std::optional<ibPredefinedChoiceDescription> m_default;

	// A TYPE THE ATTRIBUTE HOLDS NAMES ONE OF THESE — the types the configuration has, which a window cannot ask for
	// itself: what its picker offers. Empty where the attribute holds no type.
	std::vector<ibClassID> m_offered;

	// A CLOSED SET, when the attribute has one: an enumeration's members, the declared values of the references it
	// admits. Empty: the value is typed in or ticked.
	std::vector<ibPredefinedChoiceDescription> m_choices;
};

// A SECTION WHOSE ROWS AN ITEM OWNS — an account's analytics, a calculation type's leading types.
struct ibPredefinedTableDescription {
	ibMetaID     m_id = 0;
	ibString     m_name;
	ibString     m_title;
	std::vector<ibPredefinedAttributeDescription> m_columns;   // every column but the line number: a row is its position

	// The column of that id — null when none.
	const ibPredefinedAttributeDescription* FindAttribute(const ibMetaID& id) const {
		for (const ibPredefinedAttributeDescription& column : m_columns)
			if (column.m_id == id)
				return &column;
		return nullptr;
	}
};

// ONE VALUE AN ITEM DECLARES — the value as every value crosses the wire (ibSerializeValue), and how it reads.
struct ibPredefinedCellDescription {
	ibString   m_title;
	ibDataNode m_value;

	// Nothing reads in it: not declared — a code that is generated, a description not given.
	bool IsEmpty() const { return m_title.IsEmpty(); }
};

// A ROW OF THEM, by the attribute's id — absent: not declared; and the rows of a section, in order — a row is its position.
typedef std::map<ibMetaID, ibPredefinedCellDescription> ibPredefinedRowDescription;
typedef std::vector<ibPredefinedRowDescription> ibPredefinedRowDescriptionVector;

// ONE ITEM — its fields, the item it stands under (none: the top), and what it declares beyond them: its row, and the rows
// of its sections by the section's id (absent: the rows are the data's).
struct ibPredefinedItemDescription {
	ibGuid   m_guid;
	ibString m_name;
	// ⭐ A CODE AND A DESCRIPTION ARE VALUES OF THEIR FIELD'S TYPE, as a cell is its attribute's (#646): a number code a
	// number. Not declared: nothing reads in them — a code is generated.
	ibPredefinedCellDescription m_code;
	ibPredefinedCellDescription m_description;
	bool     m_folder = false;
	ibGuid   m_parent;
	ibPredefinedRowDescription m_row;
	std::map<ibMetaID, ibPredefinedRowDescriptionVector> m_tables;
};

class CORE_API ibPredefinedItemDescriptionMemory {
public:
	// node form: a Child — Guid, Name, Code and Description ({Title, Value} each), IsFolder, Parent (absent at the top),
	// Row — an Array of {Id, Title, Value} — and Tables — an Array of {Table, Rows: an Array of {Row}}; Row and Tables
	// absent where it declares nothing. As a configuration keeps an item and a command carries one. Read, a Code or a
	// Description that is a string is one saved before they were values: a string value. False: a value it holds cannot
	// be read.
	static bool ReadNode(const ibDataValue& value, ibPredefinedItemDescription& item);
	static bool WriteNode(ibDataValue& value, const ibPredefinedItemDescription& item);
};

struct ibPredefinedDescription {
	bool     m_parent = false;    // the items stand under one another: Parent is asked
	bool     m_folders = false;   // …and a folder is told from an item
	ibPredefinedCellDescription m_nextCode;   // the code a new item is given, in the code's type; empty: the kind has no code
	// WHAT AN ITEM'S CODE AND DESCRIPTION ARE — their caption and type: a string or a number, and its length (#646).
	ibPredefinedAttributeDescription m_code;
	ibPredefinedAttributeDescription m_description;
	std::vector<ibPredefinedAttributeDescription> m_attributes;
	std::vector<ibPredefinedTableDescription>     m_tables;
	std::vector<ibPredefinedItemDescription>      m_items;

	// The attribute or the table of that id, among those declared — null when none.
	const ibPredefinedAttributeDescription* FindAttribute(const ibMetaID& id) const {
		for (const ibPredefinedAttributeDescription& attribute : m_attributes)
			if (attribute.m_id == id)
				return &attribute;
		return nullptr;
	}
	const ibPredefinedTableDescription* FindTable(const ibMetaID& id) const {
		for (const ibPredefinedTableDescription& table : m_tables)
			if (table.m_id == id)
				return &table;
		return nullptr;
	}

	// The item of that guid, or of that name — null when none.
	const ibPredefinedItemDescription* FindItem(const ibGuid& guid) const {
		for (const ibPredefinedItemDescription& item : m_items)
			if (item.m_guid == guid)
				return &item;
		return nullptr;
	}
	ibPredefinedItemDescription* FindItem(const ibGuid& guid) {
		return const_cast<ibPredefinedItemDescription*>(std::as_const(*this).FindItem(guid));
	}
	const ibPredefinedItemDescription* FindItem(const ibString& name) const {
		for (const ibPredefinedItemDescription& item : m_items)
			if (item.m_name == name)
				return &item;
		return nullptr;
	}
};

class CORE_API ibPredefinedDescriptionMemory {
public:
	// node form: a Child — Parent, Folders, NextCode (a cell), the Code and the Description (an attribute each), an
	// Attributes Array and a Tables Array, each attribute with its Type (ibTypeDescriptionMemory), its Offered ids and its
	// Choices — and an Items Array, each item in its own form (ibPredefinedItemDescriptionMemory). Read, an Array is the
	// items alone: a configuration saved before kept them so.
	static bool ReadNode(const ibDataValue& value, ibPredefinedDescription& description);
	static bool WriteNode(ibDataValue& value, const ibPredefinedDescription& description);
};

#endif // !__PREDEFINED_DESCRIPTION_H__
