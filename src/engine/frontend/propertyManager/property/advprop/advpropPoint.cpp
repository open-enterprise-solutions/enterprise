#include "advpropPoint.h"

#include "backend/propertyManager/property/propertyPoint.h"
#include "frontend/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frontend/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGPointProperty
// -----------------------------------------------------------------------

#if wxCHECK_VERSION(3, 1, 0)
wxPG_IMPLEMENT_PROPERTY_CLASS(ibPGPointProperty, wxPGProperty, TextCtrl)
#else
WX_PG_IMPLEMENT_PROPERTY_CLASS(ibPGPointProperty, wxPGProperty, wxPoint, const wxPoint&, TextCtrl)
#endif

// register frontend property 
class ibPropertyPointerLoader
{
public:
	ibPropertyPointerLoader()
	{
		ibPropertyRegistry::Register([](ibPropertyPoint* prop) -> wxPGProperty* {
			return new ibPGPointProperty(prop->GetLabel(), prop->GetName(), prop->GetValueAsPoint());
		});
	}
}g_pointerLoader;

ibPGPointProperty::ibPGPointProperty(const wxString& label,
	const wxString& strName,
	const wxPoint& value) : wxPGProperty(label, strName)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(_("X"), wxT("x"), value.x));
	AddPrivateChild(new wxIntProperty(_("Y"), wxT("y"), value.y));
}

ibPGPointProperty::~ibPGPointProperty() {}

void ibPGPointProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const wxPoint& point = wxPointRefFromVariant(m_value);

	Item(0)->SetValue((long)point.x);
	Item(1)->SetValue((long)point.y);
}

wxVariant ibPGPointProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	wxPoint point;
	point << thisValue;
	int val = childValue.GetLong();
	switch (childIndex)
	{
		case 0:
			point.x = val;
			break;
		case 1:
			point.y = val;
			break;
	}
	wxVariant newVariant;
	newVariant << point;
	return newVariant;
}