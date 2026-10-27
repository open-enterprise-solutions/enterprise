#include "control.h"

bool ibValueControl::OnPropertyChanging(ibProperty* property, const ibPropertyValue& newValue)
{
	if (m_propertyName == property && FindControlByName(newValue.GetString()) != nullptr)
		return false;

	return true;
}

void ibValueControl::OnPropertyChanged(ibProperty* property, const ibPropertyValue& oldValue, const ibPropertyValue& newValue)
{
	ibValueFrame::OnPropertyChanged(property, oldValue, newValue);
}