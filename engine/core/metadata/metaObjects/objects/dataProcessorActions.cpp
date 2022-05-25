////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

CObjectDataProcessor::actionData_t CObjectDataProcessor::GetActions(const form_identifier_t &formType)
{
	return actionData_t(this);
}

void CObjectDataProcessor::ExecuteAction(const action_identifier_t &action, CValueForm *srcForm)
{
}