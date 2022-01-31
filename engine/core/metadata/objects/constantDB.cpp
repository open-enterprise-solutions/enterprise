////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "metadata/metadata.h"
#include "utils/stringUtils.h"
#include "appData.h"

bool CMetaConstantObject::CreateAndUpdateTableDB(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString fieldName = GetFieldNameDB();

	int retCode = 1;

	if ((flags & createMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s %s;", tableName, fieldName, GetSQLTypeObject());
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		CMetaConstantObject *dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (CMetaConstantObject::GetTypeDescription() != dstValue->GetTypeDescription()) {
				if (CMetaConstantObject::GetTypeObject() == dstValue->GetTypeObject()) {
					retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s TYPE %s;", tableName, fieldName, GetSQLTypeObject());
				}
				else {
					retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s;", tableName, fieldName);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
						return false;
					}
					retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s %s;", tableName, fieldName, GetSQLTypeObject());
				} 
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s;", tableName, fieldName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}