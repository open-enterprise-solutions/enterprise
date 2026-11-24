#include "choicesDescription.h"

bool ibChoicesDescriptionMemory::ReadNode(const ibDataValue& value, ibChoicesDescription& choicesDesc)
{
	choicesDesc.Clear();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: the switch offers nothing

	auto rows = value.AsChild()->FindField(ibT("rows"));
	if (!rows || rows->Kind() != ibDataKind::Array)
		return true;

	for (const ibDataValue& one : rows->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;

		const ibDataNode node = *one.AsChild();
		ibChoiceDescription row;

		if (auto item = node.FindField(ibT("value")); item && item->Kind() == ibDataKind::Child && item->AsChild())
			row.m_value = *item->AsChild();

		if (auto presentation = node.FindField(ibT("presentation")); presentation && presentation->Kind() == ibDataKind::String)
			row.m_presentation = presentation->AsString();

		choicesDesc.m_rows.push_back(std::move(row));
	}

	return true;
}

bool ibChoicesDescriptionMemory::WriteNode(ibDataValue& value, const ibChoicesDescription& choicesDesc)
{
	auto node = std::make_shared<ibDataNode>();

	std::vector<ibDataValue> rows;
	for (const ibChoiceDescription& row : choicesDesc.m_rows) {
		auto one = std::make_shared<ibDataNode>();
		one->AddField(ibT("value"), ibDataValue::Child(std::make_shared<ibDataNode>(row.m_value)));
		one->AddField(ibT("presentation"), ibDataValue::String(row.m_presentation));
		rows.push_back(ibDataValue::Child(one));
	}
	node->AddField(ibT("rows"), ibDataValue::Array(std::move(rows)));

	value = ibDataValue::Child(node);
	return true;
}
