#ifndef __ADVPROP_COLOUR_H__
#define __ADVPROP_COLOUR_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/props.h>
#include "core/graphics/colour.h"   // ibColour — what the cell holds, as its text

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGColourProperty
// -----------------------------------------------------------------------

// wxColourProperty, copied with the part of wxSystemColourProperty it runs on — over ibColour's text
// instead of a wxColour, which is what ibPropertyColour holds: the cell's value goes to the property
// as it is. No system colours: ibColour has none.
class ibPGColourProperty : public wxEnumProperty {
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGColourProperty)
public:

	ibPGColourProperty(const wxString& label = wxPG_LABEL, const wxString& name = wxPG_LABEL, const wxVariant& value = wxNullVariant);
	virtual ~ibPGColourProperty() = default;

	virtual void OnSetValue() override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	virtual bool StringToValue(wxVariant& variant, const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	virtual bool IntToValue(wxVariant& variant, int number,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	// The value is ibColour's text, not a choice's label: wxEnumProperty's check refuses every colour.
	virtual bool ValidateValue(wxVariant& value, wxPGValidationInfo& validationInfo) const override;

	virtual bool OnEvent(wxPropertyGrid* propgrid, wxWindow* primary, wxEvent& event) override;

	virtual wxSize OnMeasureImage(int item) const override;
	virtual void OnCustomPaint(wxDC& dc, const wxRect& rect, wxPGPaintData& paintdata) override;

protected:

	// The colour a value holds — the cell's own when none is given; not IsOk for no colour at all.
	ibColour GetVal(const wxVariant* pVariant = nullptr) const;

	// The colour of a choice, by its index.
	ibColour GetColour(int index) const;
	// The choice a colour is, or wxNOT_FOUND.
	int ColToInd(const ibColour& colour) const;
	int GetCustomColourIndex() const;

	bool QueryColourFromUser(wxVariant& variant) const;
};

#endif
