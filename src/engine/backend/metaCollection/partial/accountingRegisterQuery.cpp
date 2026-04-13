////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : accounting register - DDL and virtual table queries
////////////////////////////////////////////////////////////////////////////

#include "accountingRegister.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/appData.h"

////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectAccountingRegister::CreateAndUpdateRegisterTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB();

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		retCode = db_query->RunQuery("CREATE TABLE %s;", tableName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		// Add Period column
		retCode = ProcessAttribute(tableName,
			m_propertyAttributePeriod->GetMetaObject(), nullptr);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		// Add user-defined dimensions
		for (const auto object : GetDimentionArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName, object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		// Add user-defined resources
		for (const auto object : GetResourceArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName, object, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		ibValueMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;

			// dimensions from dst
			for (const auto object : dstValue->GetDimentionArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindDimensionObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			// dimensions current
			for (const auto object : GetDimentionArrayObject()) {
				retCode = ProcessDimension(tableName,
					object, dstValue->FindDimensionObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			// resources from dst
			for (const auto object : dstValue->GetResourceArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindResourceObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			// resources current
			for (const auto object : GetResourceArrayObject()) {
				retCode = ProcessResource(tableName,
					object, dstValue->FindResourceObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = db_query->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}
