#include "advpropSize.h"

#include "backend/propertyManager/property/propertySize.h"
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
	const wxSize& value) : wxPGProperty(label, strName)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(_("Width"), wxT("width"), value.x));
	AddPrivateChild(new wxIntProperty(_("Height"), wxT("height"), value.y));
}

void ibPGSizeProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxSize& size = wxSizeRefFromVariant(m_value);

	Item(0)->SetValue((long)size.x);
	Item(1)->SetValue((long)size.y);
}

wxVariant ibPGSizeProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxSize size;
	size << thisValue;
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
	wxVariant newVariant;
	newVariant << size;
	return newVariant;
}