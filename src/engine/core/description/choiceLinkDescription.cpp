#include "choiceLinkDescription.h"

#include "core/serialize/dataBuilder.h"

namespace {

ibMetaID IdOf(const ibDataNode& node, const ibString& field)
{
	auto id = node.FindField(field);
	return (id && id->Kind() == ibDataKind::Number) ? (ibMetaID)id->AsInt() : 0;
}

// The path is written and read by the mechanism that owns it — a link's path IS a source description,
// so its serialiser is the one already in the tree rather than a second spelling of the same hops.
void ReadPath(const ibDataNode& node, ibSourceDescription& source)
{
	source.ClearSource();
	const auto path = node.FindField(ibT("path"));
	if (path)
		ibSourceDescriptionMemory::ReadNode(*path, source);
}

void WritePath(ibDataNode& node, const ibSourceDescription& source)
{
	ibDataValue path;
	ibSourceDescriptionMemory::WriteNode(path, source);
	node.AddField(ibT("path"), path);
}

}

bool ibChoiceLinkDescriptionMemory::ReadNode(const ibDataValue& value, ibChoiceTypeLinkDescription& linkDesc)
{
	linkDesc.Clear();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: the field is governed by nobody

	const ibDataNode node = *value.AsChild();
	ReadPath(node, linkDesc.m_source);

	if (auto governed = node.FindField(ibT("governedType"))) {
		if (governed->Kind() == ibDataKind::Number)
			linkDesc.m_governedType = (ibClassID)governed->AsUInt();
	}

	return true;
}

bool ibChoiceLinkDescriptionMemory::WriteNode(ibDataValue& value, const ibChoiceTypeLinkDescription& linkDesc)
{
	auto node = std::make_shared<ibDataNode>();
	WritePath(*node, linkDesc.m_source);
	node->AddField(ibT("governedType"), ibDataValue::UInt(linkDesc.m_governedType));

	value = ibDataValue::Child(node);
	return true;
}

bool ibChoiceLinkDescriptionMemory::ReadNode(const ibDataValue& value, ibChoiceParametersDescription& paramsDesc)
{
	paramsDesc.Clear();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: the choice is narrowed by nothing

	auto rows = value.AsChild()->FindField(ibT("rows"));
	if (!rows || rows->Kind() != ibDataKind::Array)
		return true;

	for (const ibDataValue& one : rows->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;

		const ibDataNode node = *one.AsChild();
		ibChoiceParameterRowDescription row;
		row.m_parameter = IdOf(node, ibT("parameter"));
		ReadPath(node, row.m_source);

		// ⚠ AN ANSWER THIS BUILD DOES NOT KNOW IS LEFT AT THE DEFAULT rather than cast. Clear is the one
		// reading that cannot quietly keep a value belonging to somebody else — which is also what a row
		// stored as the dropped "pick again" (2) reads as: it always behaved as Clear.
		const ibMetaID onChange = IdOf(node, ibT("onChange"));
		if (onChange <= (ibMetaID)ibChoiceParameterOnChange::Keep)
			row.m_onChange = (ibChoiceParameterOnChange)onChange;

		paramsDesc.SetRow(row);
	}

	return true;
}

bool ibChoiceLinkDescriptionMemory::WriteNode(ibDataValue& value, const ibChoiceParametersDescription& paramsDesc)
{
	auto node = std::make_shared<ibDataNode>();

	std::vector<ibDataValue> rows;
	for (const ibChoiceParameterRowDescription& row : paramsDesc.m_rows) {
		auto one = std::make_shared<ibDataNode>();
		one->AddField(ibT("parameter"), ibDataValue::Int(row.m_parameter));
		WritePath(*one, row.m_source);
		one->AddField(ibT("onChange"), ibDataValue::Int((int)row.m_onChange));
		rows.push_back(ibDataValue::Child(one));
	}
	node->AddField(ibT("rows"), ibDataValue::Array(rows));

	value = ibDataValue::Child(node);
	return true;
}
