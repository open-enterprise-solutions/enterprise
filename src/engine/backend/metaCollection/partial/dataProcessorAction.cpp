////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

CRecordDataObjectDataProcessor::actionData_t CRecordDataObjectDataProcessor::GetActions(const form_identifier_t &formType)
{
	return actionData_t(this);
}

void CRecordDataObjectDataProcessor::ExecuteAction(const action_identifier_t &action, IBackendValueForm *srcForm)
{
}