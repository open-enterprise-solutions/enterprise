#ifndef __ADVPROP_CALC_SCHEDULE_H__
#define __ADVPROP_CALC_SCHEDULE_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/props.h>
#include "frmclient/backend/backend_core.h"
#include "frmclient/backend/propertyManager/propertyObject.h"   // ibPropertyChoiceList — the property's own answer

#include <vector>

// -----------------------------------------------------------------------
// ibPGCalcScheduleProperty
// -----------------------------------------------------------------------

// ⭐ A CALCULATION REGISTER'S SCHEDULE, AS ONE ROW THAT UNFOLDS — the desktop's (frontend/propertyManager/property/
// advprop/advpropCalcSchedule.h), the way a type unfolds into its qualifiers (ibPGTypeProperty). The row picks the
// information register; below it, from that register, the resource that is the value, the dimension that is the date,
// and one row per remaining dimension: which field of the calculation register answers for it. Every list is the
// property's answer — held, so the row only asks.
//
// ⚠ READ-ONLY ON THE CLIENT, for now. The registers are the row's choices; the value, the date and the links a
// register offers are the server's property's answers to a description (ibPropertyCalcSchedule::GetSchedule*List),
// and the row does not bring them — a register chosen here could only be left without its value and its date.
class ibPGCalcScheduleProperty : public wxPGProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGCalcScheduleProperty);
private:
	// A list the property answered, as a row's choices — nothing chosen first.
	static wxPGChoices GetChoices(const ibPropertyChoiceList& list);
	// The schedule dimensions a link row stands for, in the order of the rows.
	std::vector<ibMetaID> GetLinkDimensions(const ibPropertyChoiceList& list) const;
public:

	ibPGCalcScheduleProperty(ibBackendProperty* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override { return false; }

	virtual bool IntToValue(wxVariant& value, int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	// The value is a description, not an index.
	virtual int GetChoiceSelection() const override;

	virtual wxVariant ChildChanged(wxVariant& thisValue, int childIndex, wxVariant& childValue) const override;
	virtual void RefreshChildren() override;

protected:

	ibBackendProperty* m_property = nullptr;

	wxEnumProperty* m_scheduleValue = nullptr;
	wxEnumProperty* m_scheduleDate = nullptr;

	// the link rows, child index 2 + position
	std::vector<ibMetaID> m_linkDimensions;
	std::vector<wxEnumProperty*> m_linkRows;
};

#endif
