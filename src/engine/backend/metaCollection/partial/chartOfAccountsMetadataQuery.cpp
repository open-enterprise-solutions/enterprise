#include "chartOfAccounts.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

bool ibValueMetaObjectChartOfAccounts::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	if (!ibValueMetaObjectRecordDataHierarchyMutableRef::CreateAndUpdateTableDB(srcMetaData, srcMetaObject, flags))
		return false;

	const wxString& tableName = GetTableNameDB(); int retCode = 1;

	if ((flags & createMetaTable) != 0) {

		ibValueMetaObjectSubcontoKindsTable const* subcontoKindsTable = GetSubcontoKindsTable();
		retCode = ProcessTable(subcontoKindsTable->GetTableNameDB(),
			subcontoKindsTable, nullptr);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectChartOfAccounts* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			//tables current 
			ibValueMetaObjectSubcontoKindsTable const* subcontoKindsTable = GetSubcontoKindsTable();
			wxASSERT(subcontoKindsTable);
			retCode = ProcessTable(subcontoKindsTable->GetTableNameDB(),
				subcontoKindsTable, dstValue->GetSubcontoKindsTable());

			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
		}
	}
	else if ((flags & deleteMetaTable) != 0) {

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		ibValueMetaObjectSubcontoKindsTable const* subcontoKindsTable = GetSubcontoKindsTable();
		wxASSERT(subcontoKindsTable);

		const wxString& tabularName = subcontoKindsTable->GetTableNameDB();
		if (db_query->TableExists(tabularName))
			retCode = db_query->RunQuery("DROP TABLE %s", tabularName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;

}