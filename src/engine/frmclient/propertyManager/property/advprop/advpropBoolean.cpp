
#include <wx/propgrid/advprops.h>

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the flag's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

namespace {

// The flag a value holds, as the server stores it (a Bool node)…
bool ReadBool(const wxVariant& value)
{
	return ibVariantDataSerializable::GetNodeValue(value).AsBool();
}

// …and the value of another: the same property's, named by the cell (ValueToString).
wxVariant WriteBool(const wxVariant& value, bool flag)
{
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::Bool(flag), wxEmptyString));
}

// -----------------------------------------------------------------------
// ibPGBooleanProperty
// -----------------------------------------------------------------------

// The grid's own cell over the property's value: what it shows and takes is the grid's flag, read from the node and
// written back as one.
class ibPGBooleanProperty : public wxBoolProperty {
public:

	ibPGBooleanProperty(const wxString& label, const wxString& name, const wxVariant& value)
		: wxBoolProperty(label, name)
	{
		SetValue(value);
	}

	virtual wxString ValueToString(wxVariant& value, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadBool(value));
		return wxBoolProperty::ValueToString(held, flags);
	}

	virtual bool StringToValue(wxVariant& variant, const wxString& text, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadBool(variant));
		if (!wxBoolProperty::StringToValue(held, text, flags))
			return false;
		variant = WriteBool(variant, held.GetBool());
		return true;
	}

	virtual bool IntToValue(wxVariant& variant, int number, wxPGPropValFormatFlags flags) const override
	{
		wxVariant held(ReadBool(variant));
		if (!wxBoolProperty::IntToValue(held, number, flags))
			return false;
		variant = WriteBool(variant, held.GetBool());
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
		ibPropertyRegistry::Register(wxT("ibPropertyBoolean"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGBooleanProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_boolLoader;