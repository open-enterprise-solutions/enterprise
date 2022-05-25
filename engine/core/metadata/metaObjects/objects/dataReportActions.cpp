////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

CObjectReport::actionData_t CObjectReport::GetActions(const form_identifier_t &formType)
{
	return actionData_t(this);
}

void CObjectReport::ExecuteAction(const action_identifier_t &action, CValueForm *srcForm)
{
}