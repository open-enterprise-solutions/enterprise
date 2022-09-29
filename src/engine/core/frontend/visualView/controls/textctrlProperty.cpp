#include "widgets.h"

void CValueTextCtrl::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueTextCtrl::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

bool CValueTextCtrl::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueTextCtrl::LoadFromVariant(newValue))
		return true;
	return IValueControl::OnPropertyChanging(property, newValue);
}