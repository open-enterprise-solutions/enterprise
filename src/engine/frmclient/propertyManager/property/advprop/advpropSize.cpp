#include "advpropSize.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the size's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"
#include "frmclient/win/typeconv.h"   // the size as the server writes it

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------
#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGSizeProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(ibPGSizeProperty, wxPGProperty, wxSize, const wxSize&, TextCtrl)
#endif

namespace {

// The size a value holds, as the server writes it (its text)…
wxSize ReadSize(const wxVariant& value)
{
	return typeConv::StringToSize(ibVariantDataSerializable::GetNodeValue(value).AsString());
}

// …and the value of another: the same property's, read as that text.
wxVariant WriteSize(const wxVariant& value, const wxSize& size)
{
	const wxString text = typeConv::SizeToString(size);
	return wxVariant(new ibVariantDataSerializable(value.GetType(), ibDataValue::String(text), text));
}

} // namespace

// register frontend property
class ibPropertySizeLoader
{
public:
	ibPropertySizeLoader()
	{
		ibPropertyRegistry::Register(wxT("ibPropertySize"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGSizeProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_sizeLoader;

ibPGSizeProperty::ibPGSizeProperty(const wxString& label,
	const wxString& strName,
	const wxVariant& value) : wxPGProperty(label, strName)
{
	SetValue(value);
	const wxSize size = ReadSize(value);
	AddPrivateChild(new wxIntProperty(_("Width"), wxT("width"), size.x));
	AddPrivateChild(new wxIntProperty(_("Height"), wxT("height"), size.y));
}

void ibPGSizeProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxSize size = ReadSize(m_value);

	Item(0)->SetValue((long)size.x);
	Item(1)->SetValue((long)size.y);
}

wxVariant ibPGSizeProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxSize size = ReadSize(thisValue);
	const int val = childValue.GetLong();
	switch (childIndex)
	{
	case 0:
		size.x = val;
		break;
	case 1:
		size.y = val;
		break;
	}
	return WriteSize(thisValue, size);
}