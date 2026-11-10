
#include "deskgui/univ/propgrid/props.h"   // ibEnumProperty / ibPGChoices — this file's own, not the backend's

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the chosen one's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The chosen one a value holds, as the server stores it (a Number node)…
long ReadId(const ibVariant& value)
{
	return static_cast<long>(ibVariantSerializable::GetNodeValue(value).AsInt());
}

// …and the value of another: the same property's, named by the cell (ValueToString).
ibVariant WriteId(long id)
{
	return ibVariant(new ibVariantSerializable(ibDataValue::Int(id), wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGEnumProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's choice, read from the node and
// written back as one.
class ibPGEnumProperty : public ibEnumProperty {
public:

	ibPGEnumProperty(const wxString& label, const wxString& name, ibPGChoices& choices, const ibVariant& value)
		: ibEnumProperty(label, name, choices)
	{
		SetValue(value);
	}

	virtual void OnSetValue() override { SetIndex(GetIndexForValue(ReadId(m_value))); }

	virtual wxString ValueToString(ibVariant& value, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadId(value));
		return ibEnumProperty::ValueToString(held, flags);
	}

	virtual bool StringToValue(ibVariant& variant, const wxString& text, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadId(variant));
		if (!ibEnumProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteId(held.GetInteger());
		return true;
	}

	virtual bool IntToValue(ibVariant& variant, int number, ibPGPropValFormatFlags flags) const override
	{
		ibVariant held(ReadId(variant));
		if (!ibEnumProperty::IntToValue(held, number, flags))
			return false;
		variant = WriteId(held.GetInteger());
		return true;
	}
};

} // namespace

// register frontend property
class ibPropertyEnumLoader
{
public:
	ibPropertyEnumLoader()
	{
		// ibPropertyEnum, whatever it enumerates: the server names a template without its arguments. The choices are the
		// row's (the family's verb, GetValueList, asked on the server), so this editor does not know it is an enumeration.
		ibPropertyRegistry::Register(wxT("ibPropertyEnum"), [](ibBackendProperty* property) -> ibPGProperty* {
			ibPGChoices choices = ibPropertyRegistry::GetChoices(property);
			return new ibPGEnumProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		});
	}
}g_enumLoader;