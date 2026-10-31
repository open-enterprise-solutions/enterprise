#ifndef __CALC_SCHEDULE_VARIANT_H__
#define __CALC_SCHEDULE_VARIANT_H__

#include "core/variant.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/calcScheduleDescription.h"

// The variant of a calculation register's schedule — it holds an ibCalcScheduleDescription, as
// ibVariantAttribute holds an ibTypeDescription.
class BACKEND_API ibVariantCalcSchedule : public ibVariant {
public:

	ibVariantCalcSchedule(const ibPropertyObject* prop, const ibCalcScheduleDescription& scheduleDesc = ibCalcScheduleDescription())
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_scheduleDesc(scheduleDesc) {}
	ibVariantCalcSchedule(const ibVariantCalcSchedule& src)
		: ibVariant(ibValueTypes::TYPE_VALUE), m_ownerProperty(src.m_ownerProperty), m_scheduleDesc(src.m_scheduleDesc) {}

	ibCalcScheduleDescription& GetScheduleDesc() { return m_scheduleDesc; }
	const ibCalcScheduleDescription& GetScheduleDesc() const { return m_scheduleDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantCalcSchedule* other = dynamic_cast<const ibVariantCalcSchedule*>(data.GetRef());
		return other != nullptr && m_scheduleDesc == other->m_scheduleDesc;
	}

	// The register's name — the row the inspector shows; the value and the date are its two rows below.
	virtual ibString GetString() const override;

protected:

	const ibPropertyObject*   m_ownerProperty;
	ibCalcScheduleDescription m_scheduleDesc;
};

#endif
