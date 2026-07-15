#include "advpropNumber.h"
#include "backend/propertyManager/property/propertyNumber.h"
#include "backend/propertyManager/property/variant/variantNumber.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGDataSourceProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGNumberProperty, wxNumericProperty, TextCtrl)

// register frontend property
class ibPropertyNumberLoader
{
public:
	ibPropertyNumberLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyNumber* prop) -> wxPGProperty* {
			return new ibPGNumberProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsNumber());
		});
		ibPropertyRegistry::Register([](ibPropertyInteger* prop) -> wxPGProperty* {
			return new wxIntProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsInteger());
		});
		ibPropertyRegistry::Register([](ibPropertyUInteger* prop) -> wxPGProperty* {
			return new wxUIntProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsUInteger());
		});
	}
}g_numberLoader;

ibPGNumberProperty::ibPGNumberProperty(const wxString& label,
	const wxString& strName,
	const ibNumber& value)
	: wxNumericProperty(label, strName)
{
	m_precision = -1;
	SetValue(
		new ibVariantDataNumber(value)
	);
}

ibPGNumberProperty::~ibPGNumberProperty() {}

wxString ibPGNumberProperty::ValueToString(wxVariant& variant,
	wxPGPropValFormatFlags flags) const
{
	wxString text;
	if (!variant.IsNull()) {
		ibVariantDataNumber* numberVariant =
			dynamic_cast<ibVariantDataNumber*>(variant.GetData());
		const ibNumber& value = numberVariant->GetNumber();
		text = value.ToString();
	}
	return text;
}

bool ibPGNumberProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	ibNumber value;
	ibVariantDataNumber* numberVariant =
		dynamic_cast<ibVariantDataNumber*>(variant.GetData());
	wxASSERT(numberVariant);
	if (text.empty()) {
		numberVariant->SetNumber(0.0f);
		return true;
	}

	bool res = value.FromString(text);
	if (res) {
		if (numberVariant->GetNumber() != value) {
			numberVariant->SetNumber(value);
			return true;
		}
	}
	else if (!!(flags & wxPGPropValFormatFlags::ReportError)) {
	}
	return false;
}

bool
ibPGNumberProperty::ValidateValue(wxVariant& variant,
	wxPGValidationInfo& validationInfo) const
{
	ibNumber fpv = 0.0f;
	ibVariantDataNumber* numberVariant = dynamic_cast<ibVariantDataNumber*>(variant.GetData());
	if (numberVariant != nullptr) {
		fpv = numberVariant->GetNumber();
	}
	return DoNumericValidation(fpv, &validationInfo, wxPG_PROPERTY_VALIDATION_ERROR_MESSAGE);
}

bool ibPGNumberProperty::DoSetAttribute(const wxString& strName, wxVariant& variant)
{
	if (strName == wxPG_FLOAT_PRECISION) {
		m_precision = variant.GetLong();
		return true;
	}
	return wxNumericProperty::DoSetAttribute(strName, variant);
}

wxValidator*
ibPGNumberProperty::GetClassValidator()
{
#if wxUSE_VALIDATORS
	WX_PG_DOGETVALIDATOR_ENTRY()

		wxValidator* validator = new wxNumericPropertyValidator(
			wxNumericPropertyValidator::Float);

	WX_PG_DOGETVALIDATOR_EXIT(validator)
#else
	return nullptr;
#endif
}

wxValidator* ibPGNumberProperty::DoGetValidator() const
{
	return GetClassValidator();
}

wxVariant ibPGNumberProperty::AddSpinStepValue(long stepScale) const
{
	int mode = m_spinWrap ? wxPG_PROPERTY_VALIDATION_WRAP
		: wxPG_PROPERTY_VALIDATION_SATURATE;
	wxVariant value = GetValue();
	ibNumber v = 0.0f;
	ibVariantDataNumber* numberVariant = dynamic_cast<ibVariantDataNumber*>(value.GetData());
	if (numberVariant != nullptr) {
		v = numberVariant->GetNumber();
	}
	double step = m_spinStep.GetDouble();
	v += (step * stepScale);
	DoNumericValidation(v, nullptr, mode);
	numberVariant->SetNumber(v);
	return value;
}