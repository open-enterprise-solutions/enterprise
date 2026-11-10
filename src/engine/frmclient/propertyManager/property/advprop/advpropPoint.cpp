#include "advpropPoint.h"

#include "frmclient/backend/propertyManager/property/variant/variantSerializable.h"   // the value — the point's node
#include "frmclient/propertyManager/property/private/prop.h"             // ibPGPropertyFlags_*
#include "frmclient/propertyManager/property/private/propertyRegistry.h"

// -----------------------------------------------------------------------
// ibPGPointProperty
// -----------------------------------------------------------------------

ibPG_IMPLEMENT_PROPERTY_CLASS(ibPGPointProperty, ibPGProperty, TextCtrl)

// register frontend property
class ibPropertyPointerLoader
{
public:
	ibPropertyPointerLoader()
	{
		// ibPropertyPoint — the point as the server stores it: its text, `x,y` (ibPoint::ToString); none reads as the
		// default position, as the engine's property reads it.
		ibPropertyRegistry::Register(wxT("ibPropertyPoint"), [](ibBackendProperty* property) -> ibPGProperty* {
			return new ibPGPointProperty(property->GetLabel(), property->GetName(), property->GetValue());
		});
	}
}g_pointerLoader;

ibPGPointProperty::ibPGPointProperty(const wxString& label,
	const wxString& strName,
	const ibVariant& value) : ibPGProperty(label, strName)
{
	SetValue(value);
	const ibPoint point = GetVal();
	AddPrivateChild(new ibIntProperty(_("X"), wxT("x"), point.m_x));
	AddPrivateChild(new ibIntProperty(_("Y"), wxT("y"), point.m_y));
}

ibPGPointProperty::~ibPGPointProperty() {}

ibPoint ibPGPointProperty::GetVal(const ibVariant* pVariant) const
{
	if (!pVariant)
		pVariant = &m_value;

	ibPoint point = ibDefaultPosition;
	point.FromString(ibVariantSerializable::GetNodeValue(*pVariant).AsString());
	return point;
}

void ibPGPointProperty::RefreshChildren()
{
	if (GetChildCount() < 2) return;

	const ibPoint point = GetVal();

	Item(0)->SetValue((long)point.m_x);
	Item(1)->SetValue((long)point.m_y);
}

ibVariant ibPGPointProperty::ChildChanged(ibVariant& thisValue, const int childIndex,
	ibVariant& childValue) const {
	ibPoint point = GetVal(&thisValue);
	int val = childValue.GetInteger();
	switch (childIndex)
	{
		case 0:
			point.m_x = val;
			break;
		case 1:
			point.m_y = val;
			break;
	}
	return ibVariant(new ibVariantSerializable(ibDataValue::String(point.ToString()),
		point.ToString()));
}
