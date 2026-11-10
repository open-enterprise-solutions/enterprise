#ifndef __ADVPROP_COLOUR_H__
#define __ADVPROP_COLOUR_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/props.h"
#include "core/graphics/colour.h"   // ibColour — what the cell holds, as its text

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGColourProperty
// -----------------------------------------------------------------------

// ibColourProperty, copied with the part of ibSystemColourProperty it runs on — over ibColour's text
// instead of a wxColour, which is what ibPropertyColour holds: the cell's value goes to the property
// as it is. No system colours: ibColour has none.
class ibPGColourProperty : public ibEnumProperty {
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGColourProperty)
public:

	ibPGColourProperty(const wxString& label = ibPG_LABEL, const wxString& name = ibPG_LABEL, const ibVariant& value = ibVariant());
	virtual ~ibPGColourProperty() = default;

	virtual void OnSetValue() override;

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
	virtual bool StringToValue(ibVariant& variant, const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
	virtual bool IntToValue(ibVariant& variant, int number,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
	// The value is ibColour's text, not a choice's label: ibEnumProperty's check refuses every colour.
	virtual bool ValidateValue(ibVariant& value, ibPGValidationInfo& validationInfo) const override;

	virtual bool OnEvent(ibPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

	virtual wxSize OnMeasureImage(int item) const override;
	virtual void OnCustomPaint(wxDC& dc, const wxRect& rect, ibPGPaintData& paintdata) override;

protected:

	// The colour a value holds — the cell's own when none is given; not IsOk for no colour at all.
	ibColour GetVal(const ibVariant* pVariant = nullptr) const;

	// The colour of a choice, by its index.
	ibColour GetColour(int index) const;
	// The choice a colour is, or wxNOT_FOUND.
	int ColToInd(const ibColour& colour) const;
	int GetCustomColourIndex() const;

	bool QueryColourFromUser(ibVariant& variant) const;
};

#endif
