#ifndef __ADVPROP_CALC_SCHEDULE_H__
#define __ADVPROP_CALC_SCHEDULE_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/props.h"
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer
#include "core/serialize/dataBuilder.h"                        // ibDataNode — the lists the row brings

#include <vector>

// -----------------------------------------------------------------------
// ibPGCalcScheduleProperty
// -----------------------------------------------------------------------

// ⭐ A CALCULATION REGISTER'S SCHEDULE, AS ONE ROW THAT UNFOLDS — as the old desktop's did,
// the way a type unfolds into its qualifiers (ibPGTypeProperty). The row picks the
// information register; below it, from that register, the resource that is the value, the dimension that is the date,
// and one row per remaining dimension: which field of the calculation register answers for it. Every list is the
// property's answer — held, so the row only asks.
//
// The lists are the server's property's answers to the description (ibPropertyCalcSchedule::GetSchedule*List), brought by
// the row (`Schedule`: Kind 1 a value, 2 a date, 3 a dimension to link with its fields as children). Another register,
// another date: the row's Set is answered with the rows, and those carry the new lists. A server that brings none leaves
// the cell read-only — a register chosen here could only be left without its value and its date.
class ibPGCalcScheduleProperty : public ibPGProperty {
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGCalcScheduleProperty);
private:
	// A list the row brought, as a row's choices — nothing chosen first.
	static ibPGChoices GetChoices(const std::vector<ibDataNode>& items);
	// The row's lists of one kind (`Schedule`'s children of that Kind) — copies: a range hands each child out by value,
	// so an address taken in its loop dies with the iteration.
	std::vector<ibDataNode> GetItems(std::int32_t kind) const;
public:

	ibPGCalcScheduleProperty(ibBackendProperty* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant, const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override { return false; }

	virtual bool IntToValue(ibVariant& value, int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	// The value is a description, not an index.
	virtual int GetChoiceSelection() const override;

	virtual ibVariant ChildChanged(ibVariant& thisValue, int childIndex, ibVariant& childValue) const override;
	virtual void RefreshChildren() override;

protected:

	ibBackendProperty* m_property = nullptr;
	ibDataNode m_lists;          // the row's `Schedule`, as the server wrote it
	bool m_hasLists = false;     // …or none: an older server's row, read-only

	ibEnumProperty* m_scheduleValue = nullptr;
	ibEnumProperty* m_scheduleDate = nullptr;

	// the link rows, child index 2 + position
	std::vector<ibMetaID> m_linkDimensions;
	std::vector<ibEnumProperty*> m_linkRows;
};

#endif
