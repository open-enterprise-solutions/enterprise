#ifndef __ADVPROP_NUMBER_H__
#define __ADVPROP_NUMBER_H__

#include "deskgui/univ/propgrid/propgrid.h"
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/backend_core.h"

// -----------------------------------------------------------------------
// ibPGNumberProperty
// -----------------------------------------------------------------------

// THE DECIMAL CELL —the exact decimal the server
// stores (ibNumber, core), read from the property's node and edited as one, never through a double.
class ibPGNumberProperty : public ibNumericProperty {
public:
	ibPGNumberProperty(const wxString& label = ibPG_LABEL,
		const wxString& name = ibPG_LABEL,
		const ibVariant& value = ibVariant());
	virtual ~ibPGNumberProperty() override;

	virtual wxString ValueToString(ibVariant& value,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;

	virtual bool StringToValue(ibVariant& variant,
		const wxString& text,
		ibPGPropValFormatFlags flags = ibPGPropValFormatFlags::Null) const override;
	virtual bool DoSetAttribute(const wxString& name, ibVariant& value) override;

	virtual bool ValidateValue(ibVariant& value,
		ibPGValidationInfo& validationInfo) const override;

	static wxValidator* GetClassValidator();

	virtual wxValidator* DoGetValidator() const override;
	virtual ibVariant AddSpinStepValue(long stepScale) const override;

protected:

	// Common validation code to be called in ValidateValue() implementations.
	// Note that 'value' is reference on purpose, so we can write
	// back to it when mode is ibPGNumericValidationMode::Saturate or ibPGNumericValidationMode::Wrap.
	bool DoNumericValidation(ibNumber& value, ibPGValidationInfo* pValidationInfo, ibPGNumericValidationMode mode) const
	{
		if (value.IsNan()) {
			if (value < 0) {
				if (mode == ibPGNumericValidationMode::ErrorMessage) {
					wxString msg = ibString::Format(_("Value must be higher."));
					pValidationInfo->SetFailureMessage(msg);
				}
				value = 0;
				return false;
			}
			if (value > 0) {
				if (mode == ibPGNumericValidationMode::ErrorMessage) {
					wxString msg = ibString::Format(_("Value must be less."));
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
	IB_PG_DECLARE_PROPERTY_CLASS(ibPGNumberProperty);
};

#endif
