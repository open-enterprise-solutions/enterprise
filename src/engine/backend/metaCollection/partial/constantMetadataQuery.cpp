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

bool ibValueMetaObjectConstant::CreateConstantSQLTable()
{
	s_restructureInfo.AppendWarning(_("Create constant table"));

	//create constats 	
	if (!db_query->TableExists(ibValueMetaObjectConstant::GetTableNameDB())) {

		int retCode = db_query->RunQuery("CREATE TABLE %s (RECORD_KEY CHAR DEFAULT '6' PRIMARY KEY);", ibValueMetaObjectConstant::GetTableNameDB());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
		//retCode = db_query->RunQuery("INSERT INTO %s (RECORD_KEY) VALUES ('6');", ibValueMetaObjectConstant::GetTableNameDB());
		//if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		//	return false;
		//}
	}

	return db_query->IsOpen();
}

bool ibValueMetaObjectConstant::DeleteConstantSQLTable()
{
	s_restructureInfo.AppendWarning("Create new database");

	//create constats 	
	if (db_query->TableExists(ibValueMetaObjectConstant::GetTableNameDB())) {

		int retCode = db_query->RunQuery("DROP TABLE %s;", ibValueMetaObjectConstant::GetTableNameDB());
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
	}

	return db_query->IsOpen();
}

/////////////////////////////////////////////////////////////////////////////////////

int ibValueMetaObjectConstant::ProcessAttribute(const wxString& tableName, ibValueMetaObjectAttributeBase* srcAttr, ibValueMetaObjectAttributeBase* dstAttr)
{
	//is null - create
	if (dstAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Create constant ") + srcAttr->GetFullName());
	}
	// update 
	else if (srcAttr != nullptr) {
		if (!srcAttr->CompareObject(dstAttr))
			s_restructureInfo.AppendInfo(_("Changing constant ") + srcAttr->GetFullName());
	}
	//delete 
	else if (srcAttr == nullptr) {
		s_restructureInfo.AppendInfo(_("Removed constant ") + dstAttr->GetFullName());
	}

	return ibValueMetaObjectAttributeBase::ProcessAttribute(tableName, srcAttr, dstAttr);
}

bool ibValueMetaObjectConstant::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB();
	const wxString& fieldName = GetFieldNameDB();

	int retCode = 1;

	if ((flags & createMetaTable) != 0 || (flags & repairMetaTable) != 0) {

		if ((flags & createMetaTable) != 0) {
			if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
				retCode = ProcessAttribute(tableName, this, nullptr);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
					return false;
				}
			}
		}
		else if ((flags & repairMetaTable) != 0) {

			if (db_query->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD) {
				retCode = ProcessAttribute(tableName, this, nullptr);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
					return false;
				}
			}
		}
	}
	else if ((flags & updateMetaTable) != 0) {
		//if src is null then delete
		ibValueMetaObjectConstant* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			retCode = ProcessAttribute(tableName, this, dstValue);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) {
				return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		//if src is null then delete
		ibValueMetaObjectConstant* dstValue = nullptr;
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

//////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectConstant::LoadTableData(const ibReaderMemory& reader)
{
	wxString sqlText = "";

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
		sqlText = "INSERT INTO %s (%s, RECORD_KEY) VALUES(";
		for (unsigned int idx = 0; idx < ibValueMetaObjectAttributeBase::GetSQLFieldCount(this); idx++) {
			sqlText += "?,";
		}
		sqlText += "'6')";
		sqlText += " ON CONFLICT (RECORD_KEY) ";
		sqlText += " DO UPDATE SET " + ibValueMetaObjectAttributeBase::GetExcludeSQLFieldName(this) + ";";
	}
	else {
		sqlText = "UPDATE OR INSERT INTO %s (%s, RECORD_KEY) VALUES(";
		for (unsigned int idx = 0; idx < ibValueMetaObjectAttributeBase::GetSQLFieldCount(this); idx++) {
			sqlText += "?,";
		}
		sqlText += "'6') MATCHING(RECORD_KEY);";
	}

	ibPreparedStatement* dbPreparedStatement =
		db_query->PrepareStatement(sqlText, GetTableNameDB(), ibValueMetaObjectAttributeBase::GetSQLFieldName(this));

	if (dbPreparedStatement == nullptr)
		return false;

	if (reader.r_u8())
		ibValueMetaObjectAttributeBase::SetBinaryData(this, reader, dbPreparedStatement);
	
	dbPreparedStatement->RunQuery();
	dbPreparedStatement->Close();
	return true;
}

#include "backend/objCtor.h"

bool ibValueMetaObjectConstant::SaveTableData(ibWriterMemory& writer) const
{
	const wxString& fieldName = GetFieldNameDB();
	ibDatabaseResultSet* dbResultSet =
		db_query->RunQueryWithResults(wxT("SELECT * FROM %s"), ibValueMetaObjectConstant::GetTableNameDB());

	if (dbResultSet == nullptr)
		return false;

	if (dbResultSet->Next()) {
		writer.w_u8(true);
		ibValueMetaObjectAttributeBase::GetBinaryData(this, writer, dbResultSet);
	}
	else {
		writer.w_u8(false);
	}
	
	db_query->CloseResultSet(dbResultSet);
	return true;
}

/////////////////////////////////////////////////////////////////////////////////////