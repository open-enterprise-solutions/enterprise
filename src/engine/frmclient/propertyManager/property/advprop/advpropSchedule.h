#ifndef __ADVPROP_SCHEDULE_H__
#define __ADVPROP_SCHEDULE_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGScheduleProperty — the frontend half of the schedule property, the desktop's (frontend/propertyManager/property/
// advprop/advpropSchedule.h).
//
// Modelled on ibPGDynamicListProperty: read-only, no in-place editor, the value is a click flag;
// clicking fires OnSetValue, which CallAfter-opens the schedule editor. A schedule is a structure
// with fourteen fields — it cannot be typed into a grid cell, so the row shows the SENTENCE the
// rules produce and the editing happens in a dialog.
//
// It holds the property's OWNER, where the desktop's held the property: the client's property is the server's row, replaced by every answer, so the dialog finds it again by its
// name and writes it back through its owner (ibPropertyGate::SetValue), as an edit in the grid does.
// -----------------------------------------------------------------------

class ibPGScheduleProperty : public wxPGProperty {
public:

	ibPGScheduleProperty(ibPropertyObject* property = nullptr, const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGScheduleProperty() override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	virtual void RefreshChildren() override;

protected:
	ibPropertyObject* m_ownerProperty = nullptr;
private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGScheduleProperty);
};

#endif // __ADVPROP_SCHEDULE_H__
