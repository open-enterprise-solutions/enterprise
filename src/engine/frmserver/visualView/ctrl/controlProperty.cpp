#include "control.h"

bool ibValueControl::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	if (m_propertyName == property && FindControlByName(newValue.GetString()) != nullptr)
		return false;

	return true;
}

void ibValueControl::OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue)
{
	ibValueFrame::OnPropertyChanged(property, oldValue, newValue);
}