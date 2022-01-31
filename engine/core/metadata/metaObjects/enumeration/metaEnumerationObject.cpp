////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : meta-enumerations
////////////////////////////////////////////////////////////////////////////

#include "metaEnumerationObject.h"
#include "appData.h"
#include "metadata/objects/baseObject.h"
#include "metadata/metadata.h"
#include "databaseLayer/databaseLayer.h"

wxIMPLEMENT_DYNAMIC_CLASS(CMetaEnumerationObject, IMetaObject)

bool CMetaEnumerationObject::LoadData(CMemoryReader &reader)
{
	/*IMetaObjectValue *metaObject = wxStaticCast(GetParent(), IMetaObjectValue);

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

METADATA_REGISTER(CMetaEnumerationObject, "metaEnum", g_metaEnumCLSID);
