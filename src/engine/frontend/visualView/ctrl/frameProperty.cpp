#include "frame.h"

bool ibValueFrame::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
#ifndef OES_USE_WEB
	// g_visualHostContext = designer's editor notebook; web has no
	// designer context, so property changes just update the backing
	// ibProperty model without notifying a live editor.
	if (g_visualHostContext != nullptr)
		g_visualHostContext->ModifyProperty(property, oldValue, newValue);
#else
	(void)property; (void)oldValue; (void)newValue;
#endif
}

bool ibValueFrame::OnEventChanging(ibEvent* event, const wxVariant& newValue)
{
	return true;
}

void ibValueFrame::OnEventChanged(ibEvent* event, const wxVariant& oldValue, const wxVariant& newValue)
{
#ifndef OES_USE_WEB
	if (g_visualHostContext != nullptr)
		g_visualHostContext->ModifyEvent(event, oldValue, newValue);
#else
	(void)event; (void)oldValue; (void)newValue;
#endif
}
