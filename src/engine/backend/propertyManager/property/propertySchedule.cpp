#include "propertySchedule.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/propertyManager/property/variant/variantSchedule.h"

wxVariantData* ibPropertySchedule::CreateVariantData(const ibJobScheduleDescription& schedule) const
{
	return new ibVariantDataSchedule(schedule);
}

ibJobScheduleDescription& ibPropertySchedule::GetValueAsSchedule() const
{
	return get_cell_variant<ibVariantDataSchedule>()->GetSchedule();
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
