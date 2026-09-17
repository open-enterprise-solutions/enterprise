#include "calcScheduleDescription.h"

#include "backend/serialize/dataBuilder.h"

namespace {

ibMetaID IdOf(const ibDataNode& node, const wxString& field)
{
	const ibDataValue* id = node.FindField(field);
	return (id != nullptr && id->Kind() == ibDataKind::Number) ? (ibMetaID)id->AsInt() : 0;
}

}

bool ibCalcScheduleDescriptionMemory::ReadNode(const ibDataValue& value, ibCalcScheduleDescription& scheduleDesc)
{
	scheduleDesc.ClearSchedule();
	if (value.Kind() != ibDataKind::Child || !value.AsChild())
		return true;   // nothing saved: no schedule

	const ibDataNode& node = *value.AsChild();
	scheduleDesc.SetSchedule(IdOf(node, wxT("register")), IdOf(node, wxT("value")), IdOf(node, wxT("date")));

	if (const ibDataValue* links = node.FindField(wxT("links"))) {
		if (links->Kind() == ibDataKind::Array) {
			for (const ibDataValue& one : links->AsArray()) {
				if (one.Kind() == ibDataKind::Child && one.AsChild())
					scheduleDesc.SetLink(IdOf(*one.AsChild(), wxT("dimension")), IdOf(*one.AsChild(), wxT("field")));
			}
		}
	}
	return true;
}

bool ibCalcScheduleDescriptionMemory::WriteNode(ibDataValue& value, const ibCalcScheduleDescription& scheduleDesc)
{
	auto node = std::make_shared<ibDataNode>();
	node->AddField(wxT("register"), ibDataValue::Int(scheduleDesc.GetRegister()));
	node->AddField(wxT("value"), ibDataValue::Int(scheduleDesc.GetValue()));
	node->AddField(wxT("date"), ibDataValue::Int(scheduleDesc.GetDate()));

	std::vector<ibDataValue> links;
	for (const ibCalcScheduleLink& link : scheduleDesc.GetLinks()) {
		auto one = std::make_shared<ibDataNode>();
		one->AddField(wxT("dimension"), ibDataValue::Int(link.m_dimension));
		one->AddField(wxT("field"), ibDataValue::Int(link.m_field));
		links.push_back(ibDataValue::Child(one));
	}
	node->AddField(wxT("links"), ibDataValue::Array(links));

	value = ibDataValue::Child(node);
	return true;
}
