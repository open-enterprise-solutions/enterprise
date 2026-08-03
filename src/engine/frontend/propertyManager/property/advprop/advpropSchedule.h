#ifndef __ADVPROP_SCHEDULE_H__
#define __ADVPROP_SCHEDULE_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "backend/backend_core.h"

class BACKEND_API ibPropertySchedule;

// -----------------------------------------------------------------------
// ibPGScheduleProperty — the frontend half of the schedule property.
//
// Modelled on ibPGDynamicListProperty: read-only, no in-place editor, the value is a click flag;
// clicking fires OnSetValue, which CallAfter-opens the schedule editor. A schedule is a structure
// with fourteen fields — it cannot be typed into a grid cell, so the row shows the SENTENCE the
// rules produce and the editing happens in a dialog.
//
// It holds the BACKEND property rather than its owner: the dialog edits the schedule value itself,
// and the owner is only needed afterwards, to raise the change.
// -----------------------------------------------------------------------

class ibPGScheduleProperty : public wxPGProperty {
public:

	ibPGScheduleProperty(ibPropertySchedule* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGScheduleProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	virtual void RefreshChildren();

protected:
	ibPropertySchedule* m_scheduleProperty = nullptr;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGScheduleProperty);
};

#endif // __ADVPROP_SCHEDULE_H__
