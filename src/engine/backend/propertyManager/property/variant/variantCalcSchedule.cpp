#include "variantCalcSchedule.h"

#include "backend/metaData.h"

wxString ibVariantDataCalcSchedule::MakeString() const
{
	if (!m_scheduleDesc.IsOk() || m_ownerProperty == nullptr)
		return wxEmptyString;

	const ibMetaData* metaData = m_ownerProperty->GetMetaData();
	if (metaData == nullptr)
		return wxEmptyString;

	const ibValueMetaObject* schedule = metaData->FindAnyObjectByFilter(m_scheduleDesc.GetRegister());
	return schedule != nullptr ? schedule->GetName() : wxString();
}
