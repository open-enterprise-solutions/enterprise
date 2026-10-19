#ifndef __CALC_SCHEDULE_VARIANT_H__
#define __CALC_SCHEDULE_VARIANT_H__

#include "frmclient/backend/propertyManager/propertyObject.h"
#include "core/description/calcScheduleDescription.h"

// The variant of a calculation register's schedule — the engine's ibVariantDataCalcSchedule (backend/propertyManager/
// property/variant/variantCalcSchedule.h): it holds an ibCalcScheduleDescription, as ibVariantDataAttribute holds an
// ibTypeDescription.
//
// ⚠ NOT WRITTEN AS TEXT. The engine's names its register through the owner's configuration; the client holds none —
// the cell names it by the choices its row brought (advpropCalcSchedule.cpp).
class ibVariantDataCalcSchedule : public wxVariantData {
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

	virtual wxString GetType() const override { return wxT("ibVariantDataCalcSchedule"); }

protected:

	const ibPropertyObject*   m_ownerProperty;
	ibCalcScheduleDescription m_scheduleDesc;
};

#endif
