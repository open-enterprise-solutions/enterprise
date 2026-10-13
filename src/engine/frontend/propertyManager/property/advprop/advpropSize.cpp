#include "advpropSize.h"

#include "backend/propertyManager/property/propertySize.h"
#include "backend/propertyManager/property/variant/variantSize.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGSizeProperty
// -----------------------------------------------------------------------
#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGSizeProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(ibPGSizeProperty, wxPGProperty, wxSize, const wxSize&, TextCtrl)
#endif

// register frontend property
class ibPropertySizeLoader
{
public:
	ibPropertySizeLoader()
	{
		ibPropertyRegistry::Register([](ibPropertySize* prop) -> wxPGProperty* {
			return new ibPGSizeProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsSize());
		});
	}
}g_sizeLoader;

ibPGSizeProperty::ibPGSizeProperty(const wxString& label,
	const wxString& strName,
	const ibSize& value) : wxPGProperty(label, strName)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(_("Width"), wxT("width"), value.m_width));
	AddPrivateChild(new wxIntProperty(_("Height"), wxT("height"), value.m_height));
}

void ibPGSizeProperty::DoSetValue(const ibSize& value)
{
	m_value = wxVariant(new ibVariantDataSize(value));
}

ibSize ibPGSizeProperty::GetVal(const wxVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibVariantDataSize* data = dynamic_cast<ibVariantDataSize*>(pVariant->GetData());
	return data != nullptr ? data->GetSize() : ibDefaultSize;
}

void ibPGSizeProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const ibSize size = GetVal();

	Item(0)->SetValue((long)size.m_width);
	Item(1)->SetValue((long)size.m_height);
}

wxVariant ibPGSizeProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	ibSize size = GetVal(&thisValue);
	const int val = childValue.GetLong();
	switch (childIndex)
	{
	case 0:
		size.m_width = val;
		break;
	case 1:
		size.m_height = val;
		break;
	}
	return wxVariant(new ibVariantDataSize(size));
}
