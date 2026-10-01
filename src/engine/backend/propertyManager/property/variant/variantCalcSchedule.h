#ifndef __CALC_SCHEDULE_VARIANT_H__
#define __CALC_SCHEDULE_VARIANT_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/calcScheduleDescription.h"

// The variant of a calculation register's schedule — it holds an ibCalcScheduleDescription, as
// ibVariantDataAttribute holds an ibTypeDescription.
class BACKEND_API ibVariantDataCalcSchedule : public wxVariantData {
	wxString MakeString() const;
public:

	ibVariantDataCalcSchedule(const ibPropertyObject* prop, const ibCalcScheduleDescription& scheduleDesc = ibCalcScheduleDescription())
		: wxVariantData(), m_ownerProperty(prop), m_scheduleDesc(scheduleDesc) {}

	ibCalcScheduleDescription& GetScheduleDesc() { return m_scheduleDesc; }
	const ibCalcScheduleDescription& GetScheduleDesc() const { return m_scheduleDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	virtual bool Eq(wxVariantData& data) const override {
		const ibVariantDataCalcSchedule* other = dynamic_cast<const ibVariantDataCalcSchedule*>(&data);
		return other != nullptr && m_scheduleDesc == other->m_scheduleDesc;
	}

	virtual ibVariantDataCalcSchedule* Clone() const override {
		return new ibVariantDataCalcSchedule(m_ownerProperty, m_scheduleDesc);
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const override {
		str << MakeString();
		return true;
	}
#endif
	// The register's name — the row the inspector shows; the value and the date are its two rows below.
	virtual bool Write(wxString& str) const override {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const override { return wxT("ibVariantDataCalcSchedule"); }

protected:

	const ibPropertyObject*   m_ownerProperty;
	ibCalcScheduleDescription m_scheduleDesc;
};

#endif
