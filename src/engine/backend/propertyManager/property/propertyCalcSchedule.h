#ifndef __PROPERTY_CALC_SCHEDULE_H__
#define __PROPERTY_CALC_SCHEDULE_H__

#include "backend/propertyManager/propertyObject.h"
#include "backend/calcScheduleDescription.h"

class ibValueMetaObjectRegisterData;

// ⭐ THE SCHEDULE A CALCULATION REGISTER IS BOUND TO — one property holding an ibCalcScheduleDescription, as the
// type property holds an ibTypeDescription: which information register holds the schedule, its resource that is
// the value, its dimension that is the date, and the links a record finds its schedule rows by. The inspector
// unfolds it into those parts (ibPGCalcScheduleProperty).
//
// ⭐ WHAT EACH PART MAY BE IS ANSWERED HERE, and nowhere else: the inspector's rows and metadata_set_schedule both
// ask. The fields a link may name are the owner register's own (GetScheduleFieldList).
class BACKEND_API ibPropertyCalcSchedule : public ibProperty {
	static wxVariantData* CreateVariantData(ibPropertyObject* property, const ibCalcScheduleDescription& scheduleDesc = ibCalcScheduleDescription());
	const ibValueMetaObjectRegisterData* GetScheduleRegister(const ibCalcScheduleDescription& scheduleDesc) const;
public:

	ibCalcScheduleDescription& GetValueAsScheduleDesc() const;
	void SetValue(const ibCalcScheduleDescription& val);

	// The information registers of this configuration — the schedule is one of them.
	virtual ibPropertyChoiceMode GetValueList(ibPropertyChoiceList& list) override;

	// Of the schedule register `scheduleDesc` names: the resources holding a number (the value), the dimensions
	// holding a date (the date), the dimensions left to link — all of them but the date.
	void GetScheduleValueList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const;
	void GetScheduleDateList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const;
	void GetScheduleLinkList(const ibCalcScheduleDescription& scheduleDesc, ibPropertyChoiceList& list) const;
	// The fields of the calculation register that can answer for a schedule dimension: its DIMENSIONS whose type
	// meets the schedule dimension's — a dimension for a dimension.
	void GetScheduleFieldList(ibMetaID dimension, ibPropertyChoiceList& list) const;

	ibPropertyCalcSchedule(ibPropertyCategory* cat, const wxString& name, const wxString& label, const wxString& helpString)
		: ibProperty(cat, name, label, helpString, CreateVariantData(cat->GetPropertyObject())) {}

	// NOTHING CHOSEN — a register whose calculations read no schedule.
	virtual bool IsEmptyProperty() const override { return !GetValueAsScheduleDesc().IsOk(); }

	// set/get property data
	virtual bool SetDataValue(const ibValue& varPropVal) override;
	virtual bool GetDataValue(ibValue& pvarPropVal) const override;

protected:

	// The family rule — a value arrives in whichever wrapper the caller was handed. See propertyRecord.h.
	virtual void DoSetValue(const wxVariant& val) override;

public:

	// readable node value
	virtual bool ReadNodeValue(const ibDataValue& value) override;
	virtual bool WriteNodeValue(ibDataValue& value) const override;
};

#endif
