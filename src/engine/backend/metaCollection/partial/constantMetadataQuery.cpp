////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : constants - db
////////////////////////////////////////////////////////////////////////////

#include "constant.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/metaData.h"

#include "appData.h"

/////////////////////////////////////////////////////////////////////////////////////

bool CMetaObjectConstant::CreateConstantSQLTable()
{
	//create constats 	
	if (!db_query->TableExists(CMetaObjectConstant::GetTableNameDB())) {

		int retCode = db_query->RunQuery("CREATE TABLE %s (RECORD_KEY CHAR DEFAULT '6' PRIMARY KEY);", CMetaObjectConstant::GetTableNameDB());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
		retCode = db_query->RunQuery("INSERT INTO %s (RECORD_KEY) VALUES ('6');", CMetaObjectConstant::GetTableNameDB());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}

	return db_query->IsOpen();
}

bool CMetaObjectConstant::DeleteConstantSQLTable()
{
	//create constats 	
	if (db_query->TableExists(CMetaObjectConstant::GetTableNameDB())) {

		int retCode = db_query->RunQuery("DROP TABLE %s;", CMetaObjectConstant::GetTableNameDB());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
	}

	return db_query->IsOpen();
}

/////////////////////////////////////////////////////////////////////////////////////

int CMetaObjectConstant::ProcessAttribute(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr)
{
	return IMetaObjectAttribute::ProcessAttribute(tableName, srcAttr, dstAttr);
}

bool CMetaObjectConstant::CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB();
	const wxString& fieldName = GetFieldNameDB();

	int retCode = 1;

	if ((flags & createMetaTable) != 0) {
		retCode = ProcessAttribute(tableName, this, nullptr);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
			return false;
		}
	}
	else if ((flags & updateMetaTable) != 0) {
		//if src is null then delete
		CMetaObjectConstant* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			retCode = ProcessAttribute(tableName, this, dstValue);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		//if src is null then delete
		CMetaObjectConstant* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			retCode = ProcessAttribute(tableName, nullptr, dstValue);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				return false;
			}
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}