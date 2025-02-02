#include "widgets.h"

void CValueCheckbox::OnPropertyCreated(Property* property)
{
	if (m_propertySource == property) {
		CValueCheckbox::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	}
}

bool CValueCheckbox::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertySource == property && !CValueCheckbox::LoadFromVariant(newValue))
		return false;
	return IValueControl::OnPropertyChanging(property, newValue);
}