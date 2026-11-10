#include "advpropNumber.h"
#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the decimal's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGNumberProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGNumberProperty, ibNumericProperty, TextCtrl)

namespace {

// The decimal a value holds, as the server stores it (a Number node)…
ibNumber ReadNumber(const ibVariant& value)
{
	const ibDataValue node = ibVariantSerializable::GetNodeValue(value);
	return node.Kind() == ibDataKind::Number ? node.AsNumber() : ibNumber();
}

// …and the value of another: the same property's, read as the decimal's own words.
ibVariant WriteNumber(const ibNumber& number)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::Number(number), number.ToString()));
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
		const ibPropertyRegistry::ibPropertyMaker maker = [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGNumberProperty(property->GetLabel(), property->GetName(), property->GetValue());
		};
		ibPropertyRegistry::Register(wxT("ibPropertyNumber"), maker);
		ibPropertyRegistry::Register(wxT("ibPropertyInteger"), maker);
		ibPropertyRegistry::Register(wxT("ibPropertyUInteger"), maker);
	}
}g_numberLoader;

ibPGNumberProperty::ibPGNumberProperty(const wxString& label,
	const wxString& strName,
	const ibVariant& value)
	: ibNumericProperty(label, strName)
{
	m_precision = -1;
	SetValue(value);
}

ibPGNumberProperty::~ibPGNumberProperty() {}

wxString ibPGNumberProperty::ValueToString(ibVariant& variant,
	ibPGPropValFormatFlags flags) const
{
	return variant.GetType() == ibValueTypes::TYPE_EMPTY ? ibString() : ReadNumber(variant).ToString();
}

bool ibPGNumberProperty::StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const
{
	if (text.empty()) {
		variant = WriteNumber(ibNumber());
		return true;
	}

	ibNumber value;
	bool res = value.FromString(text);
	if (res) {
		if (ReadNumber(variant) != value) {
			variant = WriteNumber(value);
			return true;
		}
	}
	else if (!!(flags & ibPGPropValFormatFlags::ReportError)) {
	}
	return false;
}

bool
ibPGNumberProperty::ValidateValue(ibVariant& variant,
	ibPGValidationInfo& validationInfo) const
{
	ibNumber fpv = ReadNumber(variant);
	return DoNumericValidation(fpv, &validationInfo, ibPGNumericValidationMode::ErrorMessage);
}

bool ibPGNumberProperty::DoSetAttribute(const wxString& strName, ibVariant& variant)
{
	if (strName == ibPG_FLOAT_PRECISION) {
		m_precision = variant.GetInteger();
		return true;
	}
	return ibNumericProperty::DoSetAttribute(strName, variant);
}

wxValidator*
ibPGNumberProperty::GetClassValidator()
{
#if wxUSE_VALIDATORS
	IB_PG_DOGETVALIDATOR_ENTRY()

		wxValidator* validator = new ibNumericPropertyValidator(
			ibNumericPropertyValidator::Float);

	IB_PG_DOGETVALIDATOR_EXIT(validator)
#else
	return nullptr;
#endif
}

wxValidator* ibPGNumberProperty::DoGetValidator() const
{
	return GetClassValidator();
}

ibVariant ibPGNumberProperty::AddSpinStepValue(long stepScale) const
{
	const ibPGNumericValidationMode mode = m_spinWrap ? ibPGNumericValidationMode::Wrap
		: ibPGNumericValidationMode::Saturate;
	ibNumber v = ReadNumber(m_value);
	double step = m_spinStep.GetDouble();
	v += (step * stepScale);
	DoNumericValidation(v, nullptr, mode);
	return WriteNumber(v);
}
