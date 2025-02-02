////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metagrid object
////////////////////////////////////////////////////////////////////////////

#include "metaGridObject.h"
#include "backend/metaData.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectGrid, IMetaObject)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectCommonGrid, CMetaObjectGrid)

//***********************************************************************
//*                           Metagrid                                  *
//***********************************************************************

bool CMetaObjectGrid::LoadData(CMemoryReader &reader)
{
	return true;
}

bool CMetaObjectGrid::SaveData(CMemoryWriter &writer)
{
	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool CMetaObjectGrid::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaObjectGrid::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectGrid, "baseTemplate", g_metaTemplateCLSID);
METADATA_TYPE_REGISTER(CMetaObjectCommonGrid, "commonTemplate", g_metaCommonTemplateCLSID);
