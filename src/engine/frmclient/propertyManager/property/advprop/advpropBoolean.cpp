
#include "deskgui/univ/propgrid/advprops.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the flag's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The flag a value holds, as the server stores it (a Bool node)…
bool ReadBool(const ibVariant& value)
{
	return ibVariantSerializable::GetNodeValue(value).AsBool();
}

// …and the value of another: the same property's, named by the cell (ValueToString).
ibVariant WriteBool(bool flag)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::Bool(flag), wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGBooleanProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's flag, read from the node and
// written back as one.
class ibPGBooleanProperty : public ibBoolProperty {
public:

	ibPGBooleanProperty(const wxString& label, const wxString& name, const ibVariant& value)
		: ibBoolProperty(label, name)
	{
		SetValue(value);
	}

	virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadBool(value));
		return ibBoolProperty::ValueToString(held, flags);
	}

	virtual bool StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadBool(variant));
		if (!ibBoolProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteBool(held.GetBoolean());
		return true;
	}

	virtual bool IntToValue(ibVariant& variant, int number, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadBool(variant));
		if (!ibBoolProperty::IntToValue(held, number, flags))
			return false;
		variant = WriteBool(held.GetBoolean());
		return true;
	}

	virtual int GetChoiceSelection() const override { return ReadBool(m_value) ? 1 : 0; }
};

} // namespace

// register frontend property
class ibPropertyBooleanLoader
{
public:
	ibPropertyBooleanLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertyBoolean"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGBooleanProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_boolLoader;