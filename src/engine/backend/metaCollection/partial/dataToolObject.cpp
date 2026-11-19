////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : dataTool - object
////////////////////////////////////////////////////////////////////////////

#include "dataTool.h"

//*********************************************************************************************
//*                                  ObjectCatalogValue                                       *
//*********************************************************************************************

ibValueRecordDataObjectDataTool::ibValueRecordDataObjectDataTool(const ibValueMetaObjectDataTool* metaObject) :
	ibValueRecordDataObjectExt(metaObject)
{
}

ibValueRecordDataObjectDataTool::ibValueRecordDataObjectDataTool(const ibValueRecordDataObjectDataTool& source) :
	ibValueRecordDataObjectExt(source)
{
}

// ShowFormValue / GetFormValue inherited from ibValueRecordDataObject.
// GetCurrentObjectFormID is inline in dataTool.h.