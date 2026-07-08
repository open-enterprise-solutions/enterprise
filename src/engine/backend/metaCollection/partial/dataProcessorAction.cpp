////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

ibValueRecordDataObjectDataProcessor::ibActionCollection ibValueRecordDataObjectDataProcessor::GetActionCollection(const ibFormID &formType)
{
	return ibActionCollection(this);
}

void ibValueRecordDataObjectDataProcessor::CallAsAction(const ibActionID &action, ibBackendValueForm *srcForm)
{
}