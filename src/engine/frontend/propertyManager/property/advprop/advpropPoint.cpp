#include "advpropPoint.h"

#include "backend/propertyManager/property/propertyPoint.h"
#include "backend/propertyManager/property/variant/variantPoint.h"
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
	const ibPoint& value) : wxPGProperty(label, strName)
{
	DoSetValue(value);
	AddPrivateChild(new wxIntProperty(_("X"), wxT("x"), value.m_x));
	AddPrivateChild(new wxIntProperty(_("Y"), wxT("y"), value.m_y));
}

ibPGPointProperty::~ibPGPointProperty() {}

void ibPGPointProperty::DoSetValue(const ibPoint& value)
{
	m_value = wxVariant(new ibVariantDataPoint(value));
}

ibPoint ibPGPointProperty::GetVal(const wxVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibVariantDataPoint* data = dynamic_cast<ibVariantDataPoint*>(pVariant->GetData());
	return data != nullptr ? data->GetPoint() : ibDefaultPosition;
}

void ibPGPointProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const ibPoint point = GetVal();

	Item(0)->SetValue((long)point.m_x);
	Item(1)->SetValue((long)point.m_y);
}

wxVariant ibPGPointProperty::ChildChanged(wxVariant& thisValue, const int childIndex,
	wxVariant& childValue) const {
	ibPoint point = GetVal(&thisValue);
	int val = childValue.GetLong();
	switch (childIndex)
	{
		case 0:
			point.m_x = val;
			break;
		case 1:
			point.m_y = val;
			break;
	}
	return wxVariant(new ibVariantDataPoint(point));
}
