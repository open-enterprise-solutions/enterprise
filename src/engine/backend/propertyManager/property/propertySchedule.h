#ifndef __PROPERTY_SCHEDULE_H__
#define __PROPERTY_SCHEDULE_H__

// A schedule as a property — WHEN a scheduled job is due, declared once and reachable from every
// surface (docs/property-system.md § 1).
//
// It owns NO serialisation of its own: the engine's schedule already has a storage door
// (ibJobScheduleDescriptionMemory, jobSchedule.h) shaped like every other description in the tree,
// and this delegates to it. One format, one place it can change — the property cannot drift from
// what the manager reads.

#include "backend/propertyManager/propertyObject.h"
#include "backend/job/jobSchedule.h"

class BACKEND_API ibPropertySchedule : public ibProperty {
	wxVariantData* CreateVariantData(const ibJobScheduleDescription& schedule = ibJobScheduleDescription()) const;
public:

	ibJobScheduleDescription& GetValueAsSchedule() const;
	void SetValue(const ibJobScheduleDescription& schedule);

	ibPropertySchedule(ibPropertyCategory* cat, const wxString& name)
		: ibProperty(cat, name, CreateVariantData())
	{
	}

	ibPropertySchedule(ibPropertyCategory* cat, const wxString& name, const wxString& label)
		: ibProperty(cat, name, label, CreateVariantData())
	{
	}

	ibPropertySchedule(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData())
	{
	}

	// Script surface — read-only for now: a schedule is a structure, and handing it to script as a
	// mutable value needs a script type of its own. The sentence is what a caller can use today.
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

	// Node I/O — delegated to the engine's own schedule serialiser.
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif // !__PROPERTY_SCHEDULE_H__
