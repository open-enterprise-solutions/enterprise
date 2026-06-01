#include "metaResourceObject.h"
#include "backend/metadata.h"


ibSelectorDataType ibValueMetaObjectResource::GetFilterDataType() const
{
	ibValueMetaObjectGenericData* metaObject = dynamic_cast<ibValueMetaObjectGenericData*>(m_parent);
	if (metaObject->GetClassType() == g_metaInformationRegisterCLSID) 
		return metaObject->GetFilterDataType();
	return ibSelectorDataType::ibSelectorDataType_resource;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectResource, "Resource", g_metaResourceCLSID);