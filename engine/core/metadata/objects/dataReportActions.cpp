////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

CObjectReportValue::actionData_t CObjectReportValue::GetActions(form_identifier_t formType)
{
	return actionData_t(this);
}

void CObjectReportValue::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
}