#ifndef __SCHEDULE_VARIANT_H__
#define __SCHEDULE_VARIANT_H__

// The variant payload of a schedule property — an ibJobScheduleDescription living INSIDE the
// variant, never beside it (docs/private/property-system.md § 3). That is what enrols a schedule in all
// five property surfaces at once: the designer editor, script, serialization, clipboard and the
// configuration diff.
//
// Eq() is what makes the diff work, and it is one line because the description already has value
// semantics — two schedules are equal when every field is.

#include "backend/propertyManager/propertyValue.h"
#include "backend/backend.h"
#include "core/job/jobSchedule.h"

#include <wx/variant.h>

class BACKEND_API ibPropertyValueSchedule : public ibPropertyValue {
public:

	ibJobScheduleDescription& GetSchedule() { return m_schedule; }
	const ibJobScheduleDescription& GetSchedule() const { return m_schedule; }

	ibPropertyValueSchedule() : ibPropertyValue(ibValueTypes::TYPE_VALUE) {}
	explicit ibPropertyValueSchedule(const ibJobScheduleDescription& schedule)
		: ibPropertyValue(ibValueTypes::TYPE_VALUE), m_schedule(schedule) {}
	ibPropertyValueSchedule(const ibPropertyValueSchedule& src)
		: ibPropertyValue(ibValueTypes::TYPE_VALUE), m_schedule(src.m_schedule) {}

	virtual ibPropertyValueSchedule* Clone() const {
		return new ibPropertyValueSchedule(*this);
	}

	// Value equality, straight through the description — this is what the configuration
	// compare reads (docs/private/property-system.md § 6.1).
	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const const ibPropertyValueSchedule* src = dynamic_cast<const ibPropertyValueSchedule*>(data.GetRef());
		return src != nullptr && src->m_schedule == m_schedule;
	}

	virtual ibString GetString() const override;

protected:
	ibJobScheduleDescription m_schedule;
};

#endif // !__SCHEDULE_VARIANT_H__
