////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataProcessor - object
////////////////////////////////////////////////////////////////////////////

#include "dataProcessor.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectDataProcessor::ibValueRecordDataObjectDataProcessor(const ibValueMetaObjectDataProcessor* metaObject) :
	ibValueRecordDataObjectExt(metaObject)
{
}

ibValueRecordDataObjectDataProcessor::ibValueRecordDataObjectDataProcessor(const ibValueRecordDataObjectDataProcessor& source) :
	ibValueRecordDataObjectExt(source)
{
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject.
// GetCurrentObjectFormID is inline in dataProcessor.h.