#include "widgets.h"

//*******************************************************************
//*                             Events                              *
//*******************************************************************
void CValueButton::OnButtonPressed(wxCommandEvent &event)
{
	event.Skip(
		CallAsEvent(m_onButtonPressed, CValueButton::GetValue())
	);
}
