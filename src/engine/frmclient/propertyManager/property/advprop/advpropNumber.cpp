#include "advpropNumber.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the decimal's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGNumberProperty
// -----------------------------------------------------------------------

wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGNumberProperty, wxNumericProperty, TextCtrl)

namespace {

// The decimal a value holds, as the server stores it (a Number node)…
ibNumber ReadNumber(const wxVariant& value)
{
	const ibDataValue node = ibVariantDataSerializable::GetNodeValue(value);
	return node.Kind() == ibDataKind::Number ? node.AsNumber() : ibNumber();
}

// …and the value of another: the same property's, read as the decimal's own words.
wxVariant WriteNumber(const wxVariant& value, const ibNumber& number)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Number(number), number.ToString()));
}

} // namespace

// register frontend property
class ibPropertyNumberLoader
{
public:
	ibPropertyNumberLoader()
	{
		// ibPropertyNumber — the decimal as the server stores it (a Number node), and back as one; an integer, signed or
		// not, is the same node (ibDataValue::Int, UInt), the server's property keeps it whole.
		const ibPropertyRegistry::ibPropertyMaker maker = [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGNumberProperty(property->GetLabel(), property->GetName(), property->GetValue());
		};
		ibPropertyRegistry::Register(wxT("ibPropertyNumber"), maker);
		ibPropertyRegistry::Register(wxT("ibPropertyInteger"), maker);
		ibPropertyRegistry::Register(wxT("ibPropertyUInteger"), maker);
	}
}g_numberLoader;

ibPGNumberProperty::ibPGNumberProperty(const wxString& label,
	const wxString& strName,
	const wxVariant& value)
	: wxNumericProperty(label, strName)
{
	m_precision = -1;
	SetValue(value);
}

ibPGNumberProperty::~ibPGNumberProperty() {}

wxString ibPGNumberProperty::ValueToString(wxVariant& variant,
	wxPGPropValFormatFlags flags) const
{
	return variant.IsNull() ? wxString() : ReadNumber(variant).ToString();
}

bool ibPGNumberProperty::StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const
{
	if (text.empty()) {
		variant = WriteNumber(variant, ibNumber());
		return true;
	}

	ibNumber value;
	bool res = value.FromString(text);
	if (res) {
		if (ReadNumber(variant) != value) {
			variant = WriteNumber(variant, value);
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
	ibNumber fpv = ReadNumber(variant);
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
	ibNumber v = ReadNumber(m_value);
	double step = m_spinStep.GetDouble();
	v += (step * stepScale);
	DoNumericValidation(v, nullptr, mode);
	return WriteNumber(m_value, v);
}
