////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metagrid object
////////////////////////////////////////////////////////////////////////////

#include "metaGridObject.h"
#include "metadata/metadata.h"

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

bool CMetaGridObject::OnRunMetaObject(int flags)
{
	return IMetaObject::OnRunMetaObject(flags);
}

bool CMetaGridObject::OnCloseMetaObject()
{
	return IMetaObject::OnCloseMetaObject();
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaGridObject, "metaTemplate", g_metaTemplateCLSID);
METADATA_REGISTER(CMetaCommonGridObject, "metaCommonTemplate", g_metaCommonTemplateCLSID);
