#ifndef __ADVPROP_SCHEDULE_H__
#define __ADVPROP_SCHEDULE_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/backend_core.h"

class FRMCLIENT_API ibPropertyObject;

// -----------------------------------------------------------------------
// ibPGScheduleProperty — the frontend half of the schedule property, as the old desktop's.
//
// Modelled on ibPGDynamicListProperty: read-only, no in-place editor; the click reaches its
// OnEvent, which CallAfter-opens the schedule editor. A schedule is a structure
// with fourteen fields — it cannot be typed into a grid cell, so the row shows the SENTENCE the
// rules produce and the editing happens in a dialog.
//
// It holds the property's OWNER, where the desktop's held the property: the client's property is the server's row, replaced by every answer, so the dialog finds it again by its
// name and writes it back through its owner (ibPropertyGate::SetValue), as an edit in the grid does.
// -----------------------------------------------------------------------

class ibPGScheduleProperty : public ibPGProperty {
public:

	ibPGScheduleProperty(ibPropertyObject* property = nullptr, const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());
	virtual ~ibPGScheduleProperty() override;

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool OnEvent(ibPropertyGrid* propgrid, wxWindow* wnd_primary, wxEvent& event) override;

	virtual void RefreshChildren() override;

protected:
	ibPropertyObject* m_ownerProperty = nullptr;
private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGScheduleProperty);
};

#endif // __ADVPROP_SCHEDULE_H__
