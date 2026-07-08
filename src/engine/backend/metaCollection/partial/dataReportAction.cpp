////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

ibValueRecordDataObjectReport::ibActionCollection ibValueRecordDataObjectReport::GetActionCollection(const ibFormID &formType)
{
	return ibActionCollection(this);
}

void ibValueRecordDataObjectReport::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
}