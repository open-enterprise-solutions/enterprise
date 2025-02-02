#include "accumulationRegister.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/appData.h"

////////////////////////////////////////////////////////////////////////////////

bool CMetaObjectAccumulationRegister::CreateAndUpdateBalancesTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(eRegisterType::eBalances);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		retCode = db_query->RunQuery("CREATE TABLE %s;", tableName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_attributePeriod, nullptr);

		for (auto& obj : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				obj, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (auto& obj : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto& obj : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					obj, dstValue->FindDimensionByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto& obj : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto& obj : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					obj, dstValue->FindResourceByGuid(obj->GetDocPath())
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

bool CMetaObjectAccumulationRegister::CreateAndUpdateTurnoverTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(eRegisterType::eTurnovers);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		
		retCode = db_query->RunQuery("CREATE TABLE %s;", tableName);
	
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_attributePeriod, nullptr);

		for (auto& obj : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				obj, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (auto& obj : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto& obj : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					obj, dstValue->FindDimensionByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto& obj : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto& obj : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					obj, dstValue->FindResourceByGuid(obj->GetDocPath())
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

////////////////////////////////////////////////////////////////////////////////

bool CMetaObjectAccumulationRegister::CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	CMetaObjectAccumulationRegister* dstValue = nullptr; int tableBalancesFlags = flags, tableTurnoverFlags = flags;

	if (srcMetaObject != nullptr &&
		srcMetaObject->ConvertToValue(dstValue)) {
		eRegisterType dstRegType = dstValue->GetRegisterType();
		if (dstRegType != GetRegisterType()
			&& GetRegisterType() == eRegisterType::eBalances) {
			tableBalancesFlags = createMetaTable;
			tableTurnoverFlags = deleteMetaTable;
		}
		else if (dstRegType != GetRegisterType()
			&& GetRegisterType() == eRegisterType::eTurnovers) {
			tableBalancesFlags = deleteMetaTable;
			tableTurnoverFlags = createMetaTable;
		}
		else if (GetRegisterType() == eRegisterType::eTurnovers) {
			tableBalancesFlags = defaultFlag;
		}
		else if (GetRegisterType() == eRegisterType::eBalances) {
			tableTurnoverFlags = defaultFlag;
		}
	}
	else if (srcMetaObject == nullptr
		&& flags == createMetaTable) {
		if (GetRegisterType() == eRegisterType::eBalances) {
			tableBalancesFlags = createMetaTable;
			tableTurnoverFlags = defaultFlag;
		}
		else {
			tableBalancesFlags = defaultFlag;
			tableTurnoverFlags = createMetaTable;
		}
	}
	else if (srcMetaObject == nullptr
		&& flags == deleteMetaTable) {
		if (GetRegisterType() == eRegisterType::eBalances) {
			tableBalancesFlags = deleteMetaTable;
			tableTurnoverFlags = defaultFlag;
		}
		else {
			tableBalancesFlags = defaultFlag;
			tableTurnoverFlags = deleteMetaTable;
		}
	}

	//if (tableBalancesFlags != defaultFlag
	//	&& !CMetaObjectAccumulationRegister::CreateAndUpdateBalancesTableDB(srcMetaData, srcMetaObject, tableBalancesFlags))
	//	return false;

	//if (tableTurnoverFlags != defaultFlag
	//	&& !CMetaObjectAccumulationRegister::CreateAndUpdateTurnoverTableDB(srcMetaData, srcMetaObject, tableTurnoverFlags))
	//	return false;

	return IMetaObjectRegisterData::CreateAndUpdateTableDB(
		srcMetaData,
		srcMetaObject,
		flags
	);
}

////////////////////////////////////////////////////////////////////////////////

bool CRecordSetObjectAccumulationRegister::SaveVirtualTable()
{
	/*CMetaObjectAccumulationRegister* metaObject = nullptr;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	CMetaObjectAttributeDefault* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB(); bool firstUpdate = true;
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + "(" + IMetaObjectAttribute::GetSQLFieldName(attributePeriod);

	for (auto& obj : metaObject->GetObjectDimensions()) {
		queryText += "," + IMetaObjectAttribute::GetSQLFieldName(attribute);
	}

	queryText += ") VALUES ("; bool firstInsert = true;

	unsigned int fieldCount = IMetaObjectAttribute::GetSQLFieldCount(attributePeriod);
	for (unsigned int i = 0; i < fieldCount; i++) {
		queryText += (firstInsert ? "?" : ",?");
		if (firstInsert) {
			firstInsert = false;
		}
	}

	for (auto& obj : metaObject->GetObjectDimensions()) {
		for (unsigned int i = 0; i < IMetaObjectAttribute::GetSQLFieldCount(attribute); i++) {
			queryText += ",?";
		}
	}

	queryText += ") MATCHING ( " + IMetaObjectAttribute::GetSQLFieldName(attributePeriod);

	for (auto& obj : metaObject->GetObjectDimensions()) {
		queryText += "," + IMetaObjectAttribute::GetSQLFieldName(attribute);
	}

	queryText += ");";

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);

	if (statement == nullptr)
		return false;

	bool hasError = false;

	for (auto objectValue : m_objectValues) {

		if (hasError)
			break;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(
			attributePeriod,
			objectValue.at(attributePeriod->GetMetaID()),
			statement,
			position
		);

		for (auto& obj : metaObject->GetObjectDimensions()) {
			auto foundedKey = m_keyValues.find(attribute->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				IMetaObjectAttribute::SetValueAttribute(
					attribute,
					foundedKey->second,
					statement,
					position
				);
			}
			else {
				IMetaObjectAttribute::SetValueAttribute(
					attribute,
					objectValue.at(attribute->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->DoRunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	db_query->CloseStatement(statement);
	return !hasError;*/
	return true;
}

bool CRecordSetObjectAccumulationRegister::DeleteVirtualTable()
{
	/*CMetaObjectAccumulationRegister* metaObject = nullptr;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	CMetaObjectAttributeDefault* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;

	for (auto& obj : metaObject->GetObjectDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;

	if (statement == nullptr)
		return false;

	for (auto& obj : metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	statement->DoRunQuery();
	statement->Close();*/
	return true;
}