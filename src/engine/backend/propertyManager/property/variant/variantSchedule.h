#ifndef __SCHEDULE_VARIANT_H__
#define __SCHEDULE_VARIANT_H__

// The variant payload of a schedule property — an ibJobScheduleDescription living INSIDE the
// variant, never beside it (docs/property-system.md § 3). That is what enrols a schedule in all
// five property surfaces at once: the designer editor, script, serialization, clipboard and the
// configuration diff.
//
// Eq() is what makes the diff work, and it is one line because the description already has value
// semantics — two schedules are equal when every field is.

#include "backend/backend.h"
#include "backend/job/jobSchedule.h"

#include <wx/variant.h>

class BACKEND_API ibVariantDataSchedule : public wxVariantData {
	wxString MakeString() const;
public:

	ibJobScheduleDescription& GetSchedule() { return m_schedule; }
	const ibJobScheduleDescription& GetSchedule() const { return m_schedule; }

	ibVariantDataSchedule() : wxVariantData() {}
	explicit ibVariantDataSchedule(const ibJobScheduleDescription& schedule)
		: wxVariantData(), m_schedule(schedule) {}
	ibVariantDataSchedule(const ibVariantDataSchedule& src)
		: wxVariantData(), m_schedule(src.m_schedule) {}

	virtual ibVariantDataSchedule* Clone() const {
		return new ibVariantDataSchedule(*this);
	}

	// Value equality, straight through the description — this is what the configuration
	// compare reads (docs/property-system.md § 6.1).
	virtual bool Eq(wxVariantData& data) const {
		const ibVariantDataSchedule* src = dynamic_cast<const ibVariantDataSchedule*>(&data);
		return src != nullptr && src->m_schedule == m_schedule;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif
	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataSchedule"); }

protected:
	ibJobScheduleDescription m_schedule;
};

#endif // !__SCHEDULE_VARIANT_H__
