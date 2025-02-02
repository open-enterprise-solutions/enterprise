#include "control.h"

bool IValueControl::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyName == property && FindControlByName(newValue.GetString()) != nullptr)
		return false;

	return true;
}

void IValueControl::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (g_visualHostContext != nullptr)
		g_visualHostContext->ModifyProperty(property, oldValue, newValue);
}