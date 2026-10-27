#include "propertySchedule.h"
#include "core/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantSchedule.h"

ibVariant ibPropertySchedule::CreateVariantData(const ibJobScheduleDescription& schedule)
{
	return ibVariant(new ibVariantSchedule(schedule));
}

ibJobScheduleDescription& ibPropertySchedule::GetValueAsSchedule() const
{
	return get_cell_variant<ibVariantSchedule>()->GetSchedule();
}

void ibPropertySchedule::SetValue(const ibJobScheduleDescription& schedule)
{
	m_propValue = CreateVariantData(schedule);
}

bool ibPropertySchedule::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertySchedule::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibJobScheduleRules::Describe(GetValueAsSchedule());
	return true;
}

bool ibPropertySchedule::ReadNodeValue(const ibDataValue& value)
{
	return ibJobScheduleDescriptionMemory::ReadNode(value, GetValueAsSchedule());
}

bool ibPropertySchedule::WriteNodeValue(ibDataValue& value) const
{
	return ibJobScheduleDescriptionMemory::WriteNode(value, GetValueAsSchedule());
}
