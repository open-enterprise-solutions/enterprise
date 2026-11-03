#include "predefinedDescription.h"

////////////////////////////////////////////////////////////////////////
//                             node form                              //
////////////////////////////////////////////////////////////////////////

namespace {

ibDataValue WriteAttribute(const ibPredefinedAttributeDescription& attribute)
{
	auto node = std::make_shared<ibDataNode>();
	node->SetValue(ibT("Id"), (std::int32_t)attribute.m_id);
	node->SetValue(ibT("Name"), attribute.m_name);
	node->SetValue(ibT("Title"), attribute.m_title);
	node->SetValue(ibT("Help"), attribute.m_help);

	ibDataValue type;
	ibTypeDescriptionMemory::WriteNode(type, attribute.m_type);
	node->AddField(ibT("Type"), type);
	node->SetValue(ibT("HoldsType"), attribute.m_holdsType);
	node->SetValue(ibT("Required"), attribute.m_required);
	if (attribute.m_default) {
		auto entry = std::make_shared<ibDataNode>();
		entry->SetValue(ibT("Title"), attribute.m_default->m_title);
		entry->SetValue(ibT("Name"), attribute.m_default->m_name);
		entry->AddField(ibT("Value"), ibDataValue::Child(std::make_shared<ibDataNode>(attribute.m_default->m_value)));
		node->AddField(ibT("Default"), ibDataValue::Child(entry));
	}

	std::vector<ibDataValue> offered;
	for (const ibClassID& clsid : attribute.m_offered)
		offered.push_back(ibDataValue::UInt(clsid));
	node->AddField(ibT("Offered"), ibDataValue::Array(std::move(offered)));

	std::vector<ibDataValue> choices;
	for (const ibPredefinedChoiceDescription& choice : attribute.m_choices) {
		auto entry = std::make_shared<ibDataNode>();
		entry->SetValue(ibT("Title"), choice.m_title);
		entry->SetValue(ibT("Name"), choice.m_name);
		entry->AddField(ibT("Value"), ibDataValue::Child(std::make_shared<ibDataNode>(choice.m_value)));
		choices.push_back(ibDataValue::Child(entry));
	}
	node->AddField(ibT("Choices"), ibDataValue::Array(std::move(choices)));
	return ibDataValue::Child(node);
}

bool ReadAttribute(const ibDataValue& value, ibPredefinedAttributeDescription& attribute)
{
	const std::optional<ibDataNode> node = value.AsChild();
	if (!node)
		return false;

	attribute.m_id = (ibMetaID)node->GetValue<std::int32_t>(ibT("Id"));
	attribute.m_name = node->GetValue<ibString>(ibT("Name"));
	attribute.m_title = node->GetValue<ibString>(ibT("Title"));
	attribute.m_help = node->GetValue<ibString>(ibT("Help"));

	const auto type = node->FindField(ibT("Type"));
	if (!type || !ibTypeDescriptionMemory::ReadNode(*type, attribute.m_type))
		return false;
	attribute.m_holdsType = node->GetValue<bool>(ibT("HoldsType"));
	attribute.m_required = node->GetValue<bool>(ibT("Required"));
	if (const auto defaulted = node->FindField(ibT("Default"))) {
		const std::optional<ibDataNode> read = defaulted->AsChild();
		const auto value = read ? read->FindField(ibT("Value")) : std::nullopt;
		const std::optional<ibDataNode> packed = value ? value->AsChild() : std::nullopt;
		if (!packed)
			return false;
		attribute.m_default = ibPredefinedChoiceDescription{ read->GetValue<ibString>(ibT("Title")), *packed,
			read->GetValue<ibString>(ibT("Name")) };
	}

	if (const auto offered = node->FindField(ibT("Offered")); offered && offered->Kind() == ibDataKind::Array)
		for (const ibDataValue& clsid : offered->AsArray())
			attribute.m_offered.push_back(static_cast<ibClassID>(clsid.AsUInt()));

	if (const auto choices = node->FindField(ibT("Choices")); choices && choices->Kind() == ibDataKind::Array) {
		for (const ibDataValue& entry : choices->AsArray()) {
			const std::optional<ibDataNode> read = entry.AsChild();
			const auto chosen = read ? read->FindField(ibT("Value")) : std::nullopt;
			const std::optional<ibDataNode> packed = chosen ? chosen->AsChild() : std::nullopt;
			if (!packed)
				return false;
			attribute.m_choices.push_back({ read->GetValue<ibString>(ibT("Title")), *packed, read->GetValue<ibString>(ibT("Name")) });
		}
	}
	return true;
}

// A row of cells: an Array of {Id, Title, Value} — Title absent where the cell has none.
ibDataValue WriteCells(const ibPredefinedRowDescription& row)
{
	std::vector<ibDataValue> cells;
	for (const auto& cell : row) {
		auto entry = std::make_shared<ibDataNode>();
		entry->SetValue(ibT("Id"), (std::int32_t)cell.first);
		if (!cell.second.m_title.IsEmpty())
			entry->SetValue(ibT("Title"), cell.second.m_title);
		entry->AddField(ibT("Value"), ibDataValue::Child(std::make_shared<ibDataNode>(cell.second.m_value)));
		cells.push_back(ibDataValue::Child(entry));
	}
	return ibDataValue::Array(std::move(cells));
}

bool ReadCells(const ibDataValue& value, ibPredefinedRowDescription& row)
{
	if (value.Kind() != ibDataKind::Array)
		return false;
	for (const ibDataValue& entry : value.AsArray()) {
		const std::optional<ibDataNode> read = entry.AsChild();
		const auto held = read ? read->FindField(ibT("Value")) : std::nullopt;
		const std::optional<ibDataNode> packed = held ? held->AsChild() : std::nullopt;
		if (!packed)
			return false;
		row.insert_or_assign((ibMetaID)read->GetValue<std::int32_t>(ibT("Id")),
			ibPredefinedCellDescription{ read->GetValue<ibString>(ibT("Title")), *packed });
	}
	return true;
}

}

bool ibPredefinedItemDescriptionMemory::WriteNode(ibDataValue& value, const ibPredefinedItemDescription& item)
{
	auto node = std::make_shared<ibDataNode>();
	node->SetValue(ibT("Guid"), item.m_guid);
	node->SetValue(ibT("Name"), item.m_name);
	node->SetValue(ibT("Code"), item.m_code);
	node->SetValue(ibT("Description"), item.m_description);
	node->SetValue(ibT("IsFolder"), item.m_folder);
	if (item.m_parent.isValid())
		node->SetValue(ibT("Parent"), item.m_parent);
	if (!item.m_row.empty())
		node->AddField(ibT("Row"), WriteCells(item.m_row));
	if (!item.m_tables.empty()) {
		std::vector<ibDataValue> tables;
		for (const auto& table : item.m_tables) {
			std::vector<ibDataValue> rows;
			for (const auto& line : table.second) {
				auto row = std::make_shared<ibDataNode>();
				row->AddField(ibT("Row"), WriteCells(line));
				rows.push_back(ibDataValue::Child(row));
			}
			auto entry = std::make_shared<ibDataNode>();
			entry->SetValue(ibT("Table"), (std::int32_t)table.first);
			entry->AddField(ibT("Rows"), ibDataValue::Array(std::move(rows)));
			tables.push_back(ibDataValue::Child(entry));
		}
		node->AddField(ibT("Tables"), ibDataValue::Array(std::move(tables)));
	}
	value = ibDataValue::Child(node);
	return true;
}

bool ibPredefinedItemDescriptionMemory::ReadNode(const ibDataValue& value, ibPredefinedItemDescription& item)
{
	const std::optional<ibDataNode> node = value.AsChild();
	if (!node)
		return false;

	item = ibPredefinedItemDescription();
	item.m_guid = node->GetValue<ibGuid>(ibT("Guid"));
	item.m_name = node->GetValue<ibString>(ibT("Name"));
	item.m_code = node->GetValue<ibString>(ibT("Code"));
	item.m_description = node->GetValue<ibString>(ibT("Description"));
	item.m_folder = node->GetValue<bool>(ibT("IsFolder"));
	if (node->Has(ibT("Parent")))
		item.m_parent = node->GetValue<ibGuid>(ibT("Parent"));
	if (const auto row = node->FindField(ibT("Row")); row && !ReadCells(*row, item.m_row))
		return false;
	if (const auto tables = node->FindField(ibT("Tables"))) {
		if (tables->Kind() != ibDataKind::Array)
			return false;
		for (const ibDataValue& table : tables->AsArray()) {
			const std::optional<ibDataNode> entry = table.AsChild();
			const auto rows = entry ? entry->FindField(ibT("Rows")) : std::nullopt;
			if (!rows || rows->Kind() != ibDataKind::Array)
				return false;
			auto& lines = item.m_tables[(ibMetaID)entry->GetValue<std::int32_t>(ibT("Table"))];
			for (const ibDataValue& row : rows->AsArray()) {
				const std::optional<ibDataNode> line = row.AsChild();
				const auto cells = line ? line->FindField(ibT("Row")) : std::nullopt;
				if (!cells || !ReadCells(*cells, lines.emplace_back()))
					return false;
			}
		}
	}
	return true;
}

bool ibPredefinedDescriptionMemory::WriteNode(ibDataValue& value, const ibPredefinedDescription& description)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(ibT("Parent"), description.m_parent);
	root->SetValue(ibT("Folders"), description.m_folders);
	root->SetValue(ibT("NextCode"), description.m_nextCode);

	std::vector<ibDataValue> attributes;
	for (const ibPredefinedAttributeDescription& attribute : description.m_attributes)
		attributes.push_back(WriteAttribute(attribute));
	root->AddField(ibT("Attributes"), ibDataValue::Array(std::move(attributes)));

	std::vector<ibDataValue> tables;
	for (const ibPredefinedTableDescription& table : description.m_tables) {
		auto node = std::make_shared<ibDataNode>();
		node->SetValue(ibT("Id"), (std::int32_t)table.m_id);
		node->SetValue(ibT("Name"), table.m_name);
		node->SetValue(ibT("Title"), table.m_title);
		std::vector<ibDataValue> columns;
		for (const ibPredefinedAttributeDescription& column : table.m_columns)
			columns.push_back(WriteAttribute(column));
		node->AddField(ibT("Columns"), ibDataValue::Array(std::move(columns)));
		tables.push_back(ibDataValue::Child(node));
	}
	root->AddField(ibT("Tables"), ibDataValue::Array(std::move(tables)));

	std::vector<ibDataValue> items;
	for (const ibPredefinedItemDescription& item : description.m_items)
		ibPredefinedItemDescriptionMemory::WriteNode(items.emplace_back(), item);
	root->AddField(ibT("Items"), ibDataValue::Array(std::move(items)));

	value = ibDataValue::Child(root);
	return true;
}

bool ibPredefinedDescriptionMemory::ReadNode(const ibDataValue& value, ibPredefinedDescription& description)
{
	description = ibPredefinedDescription();

	// A configuration saved before the one schema kept its items alone, an Array of them.
	if (value.Kind() == ibDataKind::Array) {
		for (const ibDataValue& entry : value.AsArray())
			if (!ibPredefinedItemDescriptionMemory::ReadNode(entry, description.m_items.emplace_back()))
				return false;
		return true;
	}

	const std::optional<ibDataNode> root = value.AsChild();
	if (!root)
		return false;

	description.m_parent = root->GetValue<bool>(ibT("Parent"));
	description.m_folders = root->GetValue<bool>(ibT("Folders"));
	description.m_nextCode = root->GetValue<ibString>(ibT("NextCode"));

	if (const auto attributes = root->FindField(ibT("Attributes")); attributes && attributes->Kind() == ibDataKind::Array) {
		for (const ibDataValue& entry : attributes->AsArray()) {
			ibPredefinedAttributeDescription attribute;
			if (!ReadAttribute(entry, attribute))
				return false;
			description.m_attributes.push_back(std::move(attribute));
		}
	}

	if (const auto tables = root->FindField(ibT("Tables")); tables && tables->Kind() == ibDataKind::Array) {
		for (const ibDataValue& entry : tables->AsArray()) {
			const std::optional<ibDataNode> node = entry.AsChild();
			if (!node)
				return false;
			ibPredefinedTableDescription table;
			table.m_id = (ibMetaID)node->GetValue<std::int32_t>(ibT("Id"));
			table.m_name = node->GetValue<ibString>(ibT("Name"));
			table.m_title = node->GetValue<ibString>(ibT("Title"));
			if (const auto columns = node->FindField(ibT("Columns")); columns && columns->Kind() == ibDataKind::Array) {
				for (const ibDataValue& column : columns->AsArray()) {
					ibPredefinedAttributeDescription attribute;
					if (!ReadAttribute(column, attribute))
						return false;
					table.m_columns.push_back(std::move(attribute));
				}
			}
			description.m_tables.push_back(std::move(table));
		}
	}

	if (const auto items = root->FindField(ibT("Items")); items && items->Kind() == ibDataKind::Array)
		for (const ibDataValue& entry : items->AsArray())
			if (!ibPredefinedItemDescriptionMemory::ReadNode(entry, description.m_items.emplace_back()))
				return false;
	return true;
}
