#include "propertyCalcSchedule.h"
#include "backend/propertyManager/property/variant/variantCalcSchedule.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"   // the registers: their resources, dimensions and attributes

ibVariant ibPropertyCalcSchedule::CreateVariantData(ibPropertyObject* property, const ibCalcScheduleDescription& scheduleDesc)
{
	return ibVariant(new ibVariantCalcSchedule(property, scheduleDesc));
}

ibCalcScheduleDescription& ibPropertyCalcSchedule::GetValueAsScheduleDesc() const
{
	return get_cell_variant<ibVariantCalcSchedule>()->GetScheduleDesc();
}

void ibPropertyCalcSchedule::SetValue(const ibCalcScheduleDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

// The family rule — see propertyRecord.cpp.
void ibPropertyCalcSchedule::DoSetValue(const ibVariant& val)
{
	if (const ibVariantCalcSchedule* carried = find_cell_variant<ibVariantCalcSchedule>(val)) {
		SetValue(carried->GetScheduleDesc());
		return;
	}

	ibProperty::DoSetValue(val);
}

// The information registers of this configuration: one entry per register, the metaID says WHICH, the variant is
// the schedule to place — a register chosen and nothing else of it yet.
ibPropertyChoiceMode ibPropertyCalcSchedule::GetValueList(ibPropertyChoiceList& list)
{
	const ibPropertyObject* owner = m_owner;
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return ibPropertyChoiceMode::None;

	for (const ibValueMetaObject* schedule : metaData->GetAnyArrayObject(g_metaInformationRegisterCLSID)) {
		if (schedule == nullptr || schedule->IsDeleted())
			continue;
		ibCalcScheduleDescription scheduleDesc;
		scheduleDesc.SetSchedule(schedule->GetMetaID(), 0, 0);
		list.Add(schedule->GetMetaID(), schedule->GetName(), schedule->GetSynonym(),
			ibVariant(new ibVariantCalcSchedule(owner, scheduleDesc)),
			schedule->GetClassType());
	}

	return ibPropertyChoiceMode::Single;
}

// The schedule register a description names, or none.
const ibValueMetaObjectRegisterData* ibPropertyCalcSchedule::GetScheduleRegister(const ibCalcScheduleDescription& scheduleDesc) const
{
	const ibPropertyObject* owner = m_owner;
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	const ibValueMetaObjectRegisterData* schedule = metaData != nullptr && scheduleDesc.IsOk()
		? metaData->FindAnyObjectByFilter<ibValueMetaObjectRegisterData>(scheduleDesc.GetRegister()) : nullptr;
	return schedule != nullptr && !schedule->IsDeleted() ? schedule : nullptr;
}

void ibPropertyCalcSchedule::GetScheduleValueList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const
{
	if (const ibValueMetaObjectRegisterData* schedule = GetScheduleRegister(scheduleDesc)) {
		for (const ibValueMetaObjectResource* resource : schedule->GetResourceArrayObject()) {
			if (!resource->IsDeleted() && resource->GetTypeDesc().ContainType(g_valueNumberCLSID))
				list.Add(resource->GetMetaID(), resource->GetName(), resource->GetSynonym(), ibVariant(resource->GetMetaID()), resource->GetClassType());
		}
	}
}

void ibPropertyCalcSchedule::GetScheduleDateList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const
{
	if (const ibValueMetaObjectRegisterData* schedule = GetScheduleRegister(scheduleDesc)) {
		for (const ibValueMetaObjectDimension* dimension : schedule->GetDimensionArrayObject()) {
			if (!dimension->IsDeleted() && dimension->GetTypeDesc().ContainType(g_valueDateCLSID))
				list.Add(dimension->GetMetaID(), dimension->GetName(), dimension->GetSynonym(), ibVariant(dimension->GetMetaID()), dimension->GetClassType());
		}
	}
}

void ibPropertyCalcSchedule::GetScheduleLinkList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const
{
	if (const ibValueMetaObjectRegisterData* schedule = GetScheduleRegister(scheduleDesc)) {
		for (const ibValueMetaObjectDimension* dimension : schedule->GetDimensionArrayObject()) {
			if (!dimension->IsDeleted() && dimension->GetMetaID() != scheduleDesc.GetDate())
				list.Add(dimension->GetMetaID(), dimension->GetName(), dimension->GetSynonym(), ibVariant(dimension->GetMetaID()), dimension->GetClassType());
		}
	}
}

void ibPropertyCalcSchedule::GetScheduleFieldList(ibMetaID dimension, ibPropertyChoiceList& list) const
{
	const ibPropertyObject* owner = m_owner;
	const ibMetaData* metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	const ibValueMetaObjectRegisterData* calculation = dynamic_cast<const ibValueMetaObjectRegisterData*>(owner);   // the register declaring this property
	const ibValueMetaObjectAttributeBase* scheduleDimension = metaData != nullptr
		? metaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(dimension, true) : nullptr;
	if (calculation == nullptr || scheduleDimension == nullptr)
		return;

	const ibTypeDescription& wanted = scheduleDimension->GetTypeDesc();
	const auto add = [&list, &wanted](const ibValueMetaObjectAttributeBase* field) {
		if (field->IsDeleted())
			return;
		for (const ibClassID& clsid : field->GetTypeDesc().GetClsidList()) {
			if (wanted.ContainType(clsid)) {
				list.Add(field->GetMetaID(), field->GetName(), field->GetSynonym(), ibVariant(field->GetMetaID()), field->GetClassType());
				return;
			}
		}
	};
	// A DIMENSION FOR A DIMENSION — what a record is kept by answers for what the schedule is kept by; an attribute
	// of the record is not one of its keys and links nothing (Max, 2026-09-17).
	for (const ibValueMetaObjectDimension* field : calculation->GetDimensionArrayObject())
		add(field);
}

bool ibPropertyCalcSchedule::SetDataValue(const ibValue& varPropVal) { return false; }

bool ibPropertyCalcSchedule::GetDataValue(ibValue& pvarPropVal) const
{
	const ibString name = m_propValue.GetString();
	pvarPropVal = ibValue(name);
	return true;
}

bool ibPropertyCalcSchedule::ReadNodeValue(const ibDataValue& value)
{
	return ibCalcScheduleDescriptionMemory::ReadNode(value, GetValueAsScheduleDesc());
}

bool ibPropertyCalcSchedule::WriteNodeValue(ibDataValue& value) const
{
	return ibCalcScheduleDescriptionMemory::WriteNode(value, GetValueAsScheduleDesc());
}

#include "protocol/protocol.h"            // ibProtocol::ibSchema::ibProperties — the row's keys
#include "core/serialize/dataBuilder.h"

bool ibPropertyCalcSchedule::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;

	const ibCalcScheduleDescription& desc = GetValueAsScheduleDesc();
	auto lists = std::make_shared<ibDataNode>();
	const auto append = [](ibDataNode& parent, std::int32_t kind, const ibPropertyChoiceList& list) {
		for (unsigned int index = 0; index < list.GetCount(); ++index) {
			ibDataNode& item = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
			if (kind != 0)
				item.SetValue(ibProtocol::ibSchema::ibProperties::Kind, kind);
			item.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(list.GetId(index)));
			item.SetValue(ibProtocol::ibSchema::ibProperties::Title, list.GetName(index));   // names, as the desktop's cell named them
		}
	};

	ibPropertyChoiceList values, dates, links;
	GetScheduleValueList(desc, values);
	append(*lists, 1, values);
	GetScheduleDateList(desc, dates);
	append(*lists, 2, dates);
	GetScheduleLinkList(desc, links);
	for (unsigned int index = 0; index < links.GetCount(); ++index) {
		ibDataNode& link = lists->AddChild(0, static_cast<ibMetaID>(lists->Children().size() + 1));
		link.SetValue(ibProtocol::ibSchema::ibProperties::Kind, static_cast<std::int32_t>(3));
		link.SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(links.GetId(index)));
		link.SetValue(ibProtocol::ibSchema::ibProperties::Title, links.GetName(index));
		ibPropertyChoiceList fields;
		GetScheduleFieldList(links.GetId(index), fields);
		append(link, 0, fields);
	}
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Schedule, ibDataValue::Child(lists));
	return true;
}
