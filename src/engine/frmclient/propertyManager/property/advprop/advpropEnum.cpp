
#include <wx/propgrid/props.h>   // wxEnumProperty / wxPGChoices — this file's own, not the backend's

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the chosen one's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The chosen one a value holds, as the server stores it (a Number node)…
long ReadId(const wxVariant& value)
{
	return static_cast<long>(ibVariantDataSerializable::GetNodeValue(value).AsInt());
}

// …and the value of another: the same property's, named by the cell (ValueToString).
wxVariant WriteId(const wxVariant& value, long id)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Int(id), wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGEnumProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's choice, read from the node and
// written back as one.
class ibPGEnumProperty : public wxEnumProperty {
public:

	ibPGEnumProperty(const wxString& label, const wxString& name, wxPGChoices& choices, const wxVariant& value)
		: wxEnumProperty(label, name, choices)
	{
		SetValue(value);
	}

	virtual void OnSetValue() override { SetIndex(GetIndexForValue(ReadId(m_value))); }

	virtual wxString ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadId(value));
		return wxEnumProperty::ValueToString(held, flags);
	}

	virtual bool StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadId(variant));
		if (!wxEnumProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteId(variant, held.GetLong());
		return true;
	}

	virtual bool IntToValue(wxVariant& variant, int number, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadId(variant));
		if (!wxEnumProperty::IntToValue(held, number, flags))
			return false;
		variant = WriteId(variant, held.GetLong());
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
		ibPropertyRegistry::Register(wxT("ibPropertyEnum"), [](ibBackendProperty* property) -> wxPGProperty* {
			wxPGChoices choices = ibPropertyRegistry::GetChoices(property);
			return new ibPGEnumProperty(property->GetLabel(), property->GetName(), choices, property->GetValue());
		});
	}
}g_enumLoader;