////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-enumerations
////////////////////////////////////////////////////////////////////////////

#include "metaEnumerationObject.h"
#include "appData.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metadata.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

wxIMPLEMENT_DYNAMIC_CLASS(CMetaEnumerationObject, IMetaObject)

bool CMetaEnumerationObject::LoadData(CMemoryReader &reader)
{
	/*IMetaObjectRecordData *metaObject = wxStaticCast(GetParent(), IMetaObjectRecordData);

	if (metaObject)
	{
		if (databaseLayer->TableExists(metaObject->GetTableNameDB()))
		{
			auto result = databaseLayer->RunQueryWithResults("SELECT UUID FROM " + metaObject->GetTableNameDB() + " WHERE METAID = '" << GetMetaID() << "'");
			if (result->Next()) m_guid = result->GetResultString(guidName);
		}

		return false;
	}*/

	return true;
}

bool CMetaEnumerationObject::SaveData(CMemoryWriter &writer)
{
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

METADATA_REGISTER(CMetaEnumerationObject, "enum", g_metaEnumCLSID);
