#include "advpropSize.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the size's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "deskgui/typeconv.h"   // the size as the server writes it

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------
ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGSizeProperty, ibPGProperty, TextCtrl)

namespace {

// The size a value holds, as the server writes it (its text)…
wxSize ReadSize(const ibVariant& value)
{
	return typeConv::StringToSize(ibVariantSerializable::GetNodeValue(value).AsString());
}

// …and the value of another: the same property's, read as that text.
ibVariant WriteSize(const wxSize& size)
{
	const wxString text = typeConv::SizeToString(size);
	return ibVariant(new ibVariantSerializable(ibDataValue::String(text), text));
}

} // namespace

// register frontend property
class ibPropertySizeLoader
{
public:
	ibPropertySizeLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySize"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGSizeProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_sizeLoader;

ibPGSizeProperty::ibPGSizeProperty(const wxString& label,
	const wxString& strName,
	const ibVariant& value) : ibPGProperty(label, strName)
{
	SetValue(value);
	const wxSize size = ReadSize(value);
	AddPrivateChild(new ibIntProperty(_("Width"), wxT("width"), size.x));
	AddPrivateChild(new ibIntProperty(_("Height"), wxT("height"), size.y));
}

void ibPGSizeProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxSize size = ReadSize(m_value);

	Item(0)->SetValue((long)size.x);
	Item(1)->SetValue((long)size.y);
}

ibVariant ibPGSizeProperty::ChildChanged(ibVariant& thisValue, const int childIndex,
	ibVariant& childValue) const {
	wxSize size = ReadSize(thisValue);
	const int val = childValue.GetInteger();
	switch (childIndex)
	{
	case 0:
		size.x = val;
		break;
	case 1:
		size.y = val;
		break;
	}
	return WriteSize(size);
}