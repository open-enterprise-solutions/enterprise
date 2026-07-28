////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

ibValueRecordDataObjectReport::ibStandardCommandSet ibValueRecordDataObjectReport::GetStandardCommands(const ibFormID &formType)
{
	return ibStandardCommandSet(this);
}

void ibValueRecordDataObjectReport::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
}