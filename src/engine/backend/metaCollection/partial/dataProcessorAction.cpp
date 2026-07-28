////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

ibValueRecordDataObjectDataProcessor::ibStandardCommandSet ibValueRecordDataObjectDataProcessor::GetStandardCommands(const ibFormID &formType)
{
	return ibStandardCommandSet(this);
}

void ibValueRecordDataObjectDataProcessor::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
}