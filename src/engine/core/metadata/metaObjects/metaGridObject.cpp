////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metagrid object
////////////////////////////////////////////////////////////////////////////

#include "metaGridObject.h"
#include "core/metadata/metadata.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaGridObject, IMetaObject)
wxIMPLEMENT_DYNAMIC_CLASS(CMetaCommonGridObject, CMetaGridObject)

//***********************************************************************
//*                           Metagrid                                  *
//***********************************************************************

bool CMetaGridObject::LoadData(CMemoryReader &reader)
{
	return true;
}

bool CMetaGridObject::SaveData(CMemoryWriter &writer)
{
	return true;
}

//***********************************************************************
//*                           read & save events                        *
//***********************************************************************

bool CMetaGridObject::OnBeforeRunMetaObject(int flags)
{
	return IMetaObject::OnBeforeRunMetaObject(flags);
}

bool CMetaGridObject::OnAfterCloseMetaObject()
{
	return IMetaObject::OnAfterCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaGridObject, "baseTemplate", g_metaTemplateCLSID);
METADATA_REGISTER(CMetaCommonGridObject, "commonTemplate", g_metaCommonTemplateCLSID);
