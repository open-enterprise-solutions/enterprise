////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

CRecordDataObjectReport::actionData_t CRecordDataObjectReport::GetActions(const form_identifier_t &formType)
{
	return actionData_t(this);
}

void CRecordDataObjectReport::ExecuteAction(const action_identifier_t &action, IBackendValueForm *srcForm)
{
}