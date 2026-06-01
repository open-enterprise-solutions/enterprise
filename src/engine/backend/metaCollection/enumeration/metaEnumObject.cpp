////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-enumerations
////////////////////////////////////////////////////////////////////////////

#include "metaEnumObject.h"


bool ibValueMetaObjectEnum::LoadData(ibReaderMemory &reader)
{
	return true;
}

bool ibValueMetaObjectEnum::SaveData(ibWriterMemory &writer)
{
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_TYPE_REGISTER(ibValueMetaObjectEnum, "Enum", g_metaEnumCLSID);
