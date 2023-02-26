#include "frameInterface.h"
#include "frontend/visualView/visualEditor.h"

bool IValueFrame::OnPropertyChanging(Property* property, const wxVariant& newValue)
{
	if (g_visualHostContext != NULL)
		g_visualHostContext->ModifyProperty(property, newValue);

	return true;
}

void IValueFrame::OnPropertyChanged(Property* property)
{
}

bool IValueFrame::OnEventChanging(Event* event, const wxString& newValue)
{
	if (g_visualHostContext != NULL)
		g_visualHostContext->ModifyEvent(event, newValue);

	return true;
}