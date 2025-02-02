////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-enumerations
////////////////////////////////////////////////////////////////////////////

#include "metaEnumObject.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaObjectEnum, IMetaObject)

bool CMetaObjectEnum::LoadData(CMemoryReader &reader)
{
	return true;
}

bool CMetaObjectEnum::SaveData(CMemoryWriter &writer)
{
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(CMetaObjectEnum, "enum", g_metaEnumCLSID);
