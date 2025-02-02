#include "frame.h"

bool IValueFrame::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	return true;
}

void IValueFrame::OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (g_visualHostContext != nullptr)
		g_visualHostContext->ModifyProperty(property, oldValue, newValue);
}

bool IValueFrame::OnEventChanging(Event* event, const wxString& newValue)
{
	return true;
}

void IValueFrame::OnEventChanged(Event* event, const wxVariant& oldValue, const wxVariant& newValue)
{
	if (g_visualHostContext != nullptr) 
		g_visualHostContext->ModifyEvent(event, oldValue, newValue);
}
