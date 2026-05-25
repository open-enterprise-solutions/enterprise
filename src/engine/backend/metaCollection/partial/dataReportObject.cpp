////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report - object
////////////////////////////////////////////////////////////////////////////

#include "dataReport.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectReport::ibValueRecordDataObjectReport(const ibValueMetaObjectReport* metaObject) : ibValueRecordDataObjectExt(metaObject)
{
}

ibValueRecordDataObjectReport::ibValueRecordDataObjectReport(const ibValueRecordDataObjectReport& source) : ibValueRecordDataObjectExt(source)
{
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject.
// GetCurrentObjectFormID is inline in dataReport.h.