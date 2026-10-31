#include "advpropPoint.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the point's node
#include "frmclient/propertyManager/property/private/prop.h"             // wxPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

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
		// ibPropertyPoint — the point as the server stores it: its text, `x,y` (ibPoint::ToString); none reads as the
		// default position, as the engine's property reads it.
		ibPropertyRegistry::Register(wxT("ibPropertyPoint"), [](ibBackendProperty* property) -> wxPGProperty* {
			return new ibPGPointProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_pointerLoader;

ibPGPointProperty::ibPGPointProperty(const wxString& label,
	const wxString& strName,
	const wxVariant& value) : wxPGProperty(label, strName)
{
	SetValue(value);
	const ibPoint point = GetVal();
	AddPrivateChild(new wxIntProperty(_("X"), wxT("x"), point.m_x));
	AddPrivateChild(new wxIntProperty(_("Y"), wxT("y"), point.m_y));
}

ibPGPointProperty::~ibPGPointProperty() {}

ibPoint ibPGPointProperty::GetVal(const wxVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibPoint point = ibDefaultPosition;
	point.FromString(ibVariantDataSerializable::GetNodeValue(*pVariant).AsString());
	return point;
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
	return wxVariant(new ibVariantDataSerializable(thisValue.GetType(), ibDataValue::String(point.ToString()),
		point.ToString()));
}
