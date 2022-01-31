////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

CObjectDataProcessorValue::actionData_t CObjectDataProcessorValue::GetActions(form_identifier_t formType)
{
	return actionData_t(this);
}

void CObjectDataProcessorValue::ExecuteAction(action_identifier_t action, CValueForm *srcForm)
{
}