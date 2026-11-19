////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataTool
////////////////////////////////////////////////////////////////////////////

#include "dataTool.h"

ibValueRecordDataObjectDataTool::ibStandardCommandSet ibValueRecordDataObjectDataTool::GetStandardCommands(const ibFormID &formType)
{
	return ibStandardCommandSet(this);
}

void ibValueRecordDataObjectDataTool::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
}