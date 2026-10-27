#ifndef __CALC_SCHEDULE_VARIANT_H__
#define __CALC_SCHEDULE_VARIANT_H__

#include "backend/propertyManager/propertyValue.h"
#include "backend/propertyManager/propertyObject.h"
#include "core/description/calcScheduleDescription.h"

// The variant of a calculation register's schedule — it holds an ibCalcScheduleDescription, as
// ibPropertyValueAttribute holds an ibTypeDescription.
class BACKEND_API ibPropertyValueCalcSchedule : public ibPropertyValue {
public:

	ibPropertyValueCalcSchedule(const ibPropertyObject* prop, const ibCalcScheduleDescription& scheduleDesc = ibCalcScheduleDescription())
		: ibPropertyValue(ibValueTypes::TYPE_VALUE), m_ownerProperty(prop), m_scheduleDesc(scheduleDesc) {}

	ibCalcScheduleDescription& GetScheduleDesc() { return m_scheduleDesc; }
	const ibCalcScheduleDescription& GetScheduleDesc() const { return m_scheduleDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueCalcSchedule* other = dynamic_cast<const ibPropertyValueCalcSchedule*>(data.GetRef());
		return other != nullptr && m_scheduleDesc == other->m_scheduleDesc;
	}

	virtual ibPropertyValueCalcSchedule* Clone() const {
		return new ibPropertyValueCalcSchedule(m_ownerProperty, m_scheduleDesc);
	}

	// The register's name — the row the inspector shows; the value and the date are its two rows below.
	virtual ibString GetString() const override;

protected:

	const ibPropertyObject*   m_ownerProperty;
	ibCalcScheduleDescription m_scheduleDesc;
};

#endif
