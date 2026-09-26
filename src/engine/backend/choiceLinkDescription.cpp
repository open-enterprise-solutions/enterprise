#include "choiceLinkDescription.h"

#include "backend/serialize/dataBuilder.h"

namespace {

ibMetaID IdOf(const ibDataNode& node, const wxString& field)
{
	const ibDataValue* id = node.FindField(field);
	return (id != nullptr && id->Kind() == ibDataKind::Number) ? (ibMetaID)id->AsInt() : 0;
}

// The path is written and read by the mechanism that owns it — a link's path IS a source description,
// so its serialiser is the one already in the tree rather than a second spelling of the same hops.
void ReadPath(const ibDataNode& node, ibSourceDescription& source)
{
	source.ClearSource();
	if (const ibDataValue* path = node.FindField(wxT("path")))
		ibSourceDescriptionMemory::ReadNode(*path, source);
}

void WritePath(ibDataNode& node, const ibSourceDescription& source)
{
	ibDataValue path;
	ibSourceDescriptionMemory::WriteNode(path, source);
	node.AddField(wxT("path"), path);
}

}

bool ibChoiceLinkDescriptionMemory::ReadNode(const ibDataValue& value, ibChoiceTypeLinkDescription& linkDesc)
{
	linkDesc.Clear();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: the field is governed by nobody

	const ibDataNode& node = *value.AsChild();
	ReadPath(node, linkDesc.m_source);

	if (const ibDataValue* governed = node.FindField(wxT("governedType"))) {
		if (governed->Kind() == ibDataKind::Number)
			linkDesc.m_governedType = (ibClassID)governed->AsUInt();
	}

	return true;
}

bool ibChoiceLinkDescriptionMemory::WriteNode(ibDataValue& value, const ibChoiceTypeLinkDescription& linkDesc)
{
	auto node = std::make_shared<ibDataNode>();
	WritePath(*node, linkDesc.m_source);
	node->AddField(wxT("governedType"), ibDataValue::UInt(linkDesc.m_governedType));

	value = ibDataValue::Child(node);
	return true;
}

bool ibChoiceLinkDescriptionMemory::ReadNode(const ibDataValue& value, ibChoiceParametersDescription& paramsDesc)
{
	paramsDesc.Clear();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: the choice is narrowed by nothing

	const ibDataValue* rows = value.AsChild()->FindField(wxT("rows"));
	if (rows == nullptr || rows->Kind() != ibDataKind::Array)
		return true;

	for (const ibDataValue& one : rows->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;

		const ibDataNode& node = *one.AsChild();
		ibChoiceParameterRowDescription row;
		row.m_parameter = IdOf(node, wxT("parameter"));
		ReadPath(node, row.m_source);

		// ⚠ AN ANSWER THIS BUILD DOES NOT KNOW IS LEFT AT THE DEFAULT rather than cast. Clear is the one
		// reading that cannot quietly keep a value belonging to somebody else — which is also what a row
		// stored as the dropped "pick again" (2) reads as: it always behaved as Clear.
		const ibMetaID onChange = IdOf(node, wxT("onChange"));
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
		one->AddField(wxT("parameter"), ibDataValue::Int(row.m_parameter));
		WritePath(*one, row.m_source);
		one->AddField(wxT("onChange"), ibDataValue::Int((int)row.m_onChange));
		rows.push_back(ibDataValue::Child(one));
	}
	node->AddField(wxT("rows"), ibDataValue::Array(rows));

	value = ibDataValue::Child(node);
	return true;
}
