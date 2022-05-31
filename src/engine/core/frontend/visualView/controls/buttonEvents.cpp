#include "widgets.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************
void CValueButton::OnButtonPressed(wxCommandEvent &event)
{
	event.Skip(CallEvent("onButtonPressed"));
}
