#include "variantCalcSchedule.h"

#include "backend/metaData.h"

ibString ibVariantCalcSchedule::GetString() const
{
	if (!m_scheduleDesc.IsOk() || m_ownerProperty == nullptr)
		return ibEmptyString;

	const ibMetaData* metaData = m_ownerProperty->GetMetaData();
	if (metaData == nullptr)
		return ibEmptyString;

	const ibValueMetaObject* schedule = metaData->FindAnyObjectByFilter(m_scheduleDesc.GetRegister());
	return schedule != nullptr ? schedule->GetName() : ibString();
}
