#ifndef __ADVPROP_NUMBER_H__
#define __ADVPROP_NUMBER_H__

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGNumberProperty
// -----------------------------------------------------------------------

// THE DESKTOP'S DECIMAL CELL (frontend/propertyManager/property/advprop/advpropNumber.h) — the exact decimal the server
// stores (ibNumber, core), read from the property's node and edited as one, never through a double.
class ibPGNumberProperty : public wxNumericProperty {
public:
	ibPGNumberProperty(const wxString& label = wxPG_LABEL,
		const wxString& name = wxPG_LABEL,
		const wxVariant& value = wxNullVariant);
	virtual ~ibPGNumberProperty() override;

	virtual wxString ValueToString(wxVariant& value,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(wxVariant& variant,
		const wxString& text,
		wxPGPropValFormatFlags flags = wxPGPropValFormatFlags::Null) const override;
	virtual bool DoSetAttribute(const wxString& name, wxVariant& value) override;

	virtual bool ValidateValue(wxVariant& value,
		wxPGValidationInfo& validationInfo) const override;

	static wxValidator* GetClassValidator();

	virtual wxValidator* DoGetValidator() const override;
	virtual wxVariant AddSpinStepValue(long stepScale) const override;

protected:

	// Common validation code to be called in ValidateValue() implementations.
	// Note that 'value' is reference on purpose, so we can write
	// back to it when mode is wxPG_PROPERTY_VALIDATION_SATURATE or wxPG_PROPERTY_VALIDATION_WRAP.
	bool DoNumericValidation(ibNumber& value, wxPGValidationInfo* pValidationInfo, int mode) const
	{
		if (value.IsNan()) {
			if (value < 0) {
				if (mode == wxPG_PROPERTY_VALIDATION_ERROR_MESSAGE) {
					wxString msg = wxString::Format(_("Value must be higher."));
					pValidationInfo->SetFailureMessage(msg);
				}
				value = 0;
				return false;
			}
			if (value > 0) {
				if (mode == wxPG_PROPERTY_VALIDATION_ERROR_MESSAGE) {
					wxString msg = wxString::Format(_("Value must be less."));
					pValidationInfo->SetFailureMessage(msg);
				}
				value = 0;
				return false;
			}
		}
		return true;
	}

	int m_precision;

private:
	WX_PG_DECLARE_PROPERTY_CLASS(ibPGNumberProperty);
};

#endif
