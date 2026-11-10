#ifndef __ADVPROP_EVENT_H__
#define __ADVPROP_EVENT_H__

#include "deskgui/univ/propgrid/propgrid.h"

// -----------------------------------------------------------------------
// ibPGEventProperty
// -----------------------------------------------------------------------

class ibPGEventProperty : public ibStringProperty {
public:

	ibPGEventProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual ibPGEditorDialogAdapter* GetEditorDialog() const override;

	// The event's picture, answered when the grid draws (advpropValuePicture.h).
	virtual wxSize OnMeasureImage(int item = -1) const override;
	virtual void OnCustomPaint(wxDC& dc, const wxRect& rect, ibPGPaintData& paintdata) override;

private:
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGEventProperty);
};

#endif