#include "control.h"

bool ibValueControl::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	if (m_propertyName == property && FindControlByName(newValue.GetString()) != nullptr)
		return false;

	return true;
}

void ibValueControl::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
}