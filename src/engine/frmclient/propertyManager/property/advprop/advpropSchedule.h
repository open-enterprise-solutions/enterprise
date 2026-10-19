#ifndef __ADVPROP_SCHEDULE_H__
#define __ADVPROP_SCHEDULE_H__

#include <wx/propgrid/propgrid.h>

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGScheduleProperty — the desktop's schedule cell (frontend advpropSchedule.h) over the server's row: the value is the
// row's node (ibJobScheduleDescriptionMemory), a click opens the job schedule window over a copy, and OK hands the
// edited node back as the pending value; the inspector sends it through the row's Set (the registered writer).
// -----------------------------------------------------------------------

class ibPGScheduleProperty : public wxPGProperty {
public:

	ibPGScheduleProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxNullVariant);
	virtual ~ibPGScheduleProperty();

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual void OnSetValue() override;

	virtual void RefreshChildren();

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGScheduleProperty);
};

#endif
