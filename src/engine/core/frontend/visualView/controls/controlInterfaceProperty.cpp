#include "controlInterface.h"
#include "frontend/visualView/visualEditor.h"

bool IValueControl::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (m_propertyName == property && FindControlByName(newValue.GetString()) != NULL)
		return false;

	if (g_visualHostContext != NULL)
		g_visualHostContext->ModifyProperty(property, newValue);

	return true;
}

void IValueControl::OnPropertyChanged(Property* property)
{
}