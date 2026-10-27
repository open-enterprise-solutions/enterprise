#include "calcScheduleDescription.h"

#include "core/serialize/dataBuilder.h"

namespace {

ibMetaID IdOf(const ibDataNode& node, const ibString& field)
{
	auto id = node.FindField(field);
	return (id && id->Kind() == ibDataKind::Number) ? (ibMetaID)id->AsInt() : 0;
}

}

bool ibCalcScheduleDescriptionMemory::ReadNode(const ibDataValue& value, ibCalcScheduleDescription& scheduleDesc)
{
	scheduleDesc.ClearSchedule();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: no schedule

	const ibDataNode node = *value.AsChild();
	scheduleDesc.SetSchedule(IdOf(node, ibT("register")), IdOf(node, ibT("value")), IdOf(node, ibT("date")));

	if (auto links = node.FindField(ibT("links"))) {
		if (links->Kind() == ibDataKind::Array) {
			for (const ibDataValue& one : links->AsArray()) {
				if (one.Kind() == ibDataKind::Child && one.AsChild())
					scheduleDesc.SetLink(IdOf(*one.AsChild(), ibT("dimension")), IdOf(*one.AsChild(), ibT("field")));
			}
		}
	}
	return true;
}

bool ibCalcScheduleDescriptionMemory::WriteNode(ibDataValue& value, const ibCalcScheduleDescription& scheduleDesc)
{
	auto node = std::make_shared<ibDataNode>();
	node->AddField(ibT("register"), ibDataValue::Int(scheduleDesc.GetRegister()));
	node->AddField(ibT("value"), ibDataValue::Int(scheduleDesc.GetValue()));
	node->AddField(ibT("date"), ibDataValue::Int(scheduleDesc.GetDate()));

	std::vector<ibDataValue> links;
	for (const ibCalcScheduleLink& link : scheduleDesc.GetLinks()) {
		auto one = std::make_shared<ibDataNode>();
		one->AddField(ibT("dimension"), ibDataValue::Int(link.m_dimension));
		one->AddField(ibT("field"), ibDataValue::Int(link.m_field));
		links.push_back(ibDataValue::Child(one));
	}
	node->AddField(ibT("links"), ibDataValue::Array(links));

	value = ibDataValue::Child(node);
	return true;
}
