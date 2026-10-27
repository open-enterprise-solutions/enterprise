#include "widgets.h"

void ibValueCheckbox::OnPropertyCreated(ibProperty* property)
{
	//if (m_propertySource == property) {
	//	ibValueCheckbox::SaveToVariant(m_propertySource->GetValue(), GetMetaData());
	//}
}

bool ibValueCheckbox::OnPropertyChanging(ibProperty* property, const ibPropertyValue& newValue)
{
	//if (m_propertySource == property && !ibValueCheckbox::LoadFromVariant(newValue))
	//	return false;
	return ibValueControl::OnPropertyChanging(property, newValue);
}