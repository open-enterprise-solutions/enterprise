#include "accumulationRegister.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/appData.h"

////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectAccumulationRegister::CreateAndUpdateBalancesTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(ibRegisterType::eBalances);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		retCode = db_query->RunQuery("CREATE TABLE %s;", tableName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_propertyAttributePeriod->GetMetaObject(), nullptr);

		for (const auto object : GetDimensionArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetResourceArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				object, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (const auto object : dstValue->GetDimensionArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindDimensionObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (const auto object : GetDimensionArrayObject()) {
				retCode = ProcessDimension(tableName,
					object, dstValue->FindDimensionObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (const auto object : dstValue->GetResourceArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindResourceObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
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

bool ibValueMetaObjectAccumulationRegister::CreateAndUpdateTurnoverTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(ibRegisterType::eTurnovers);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		
		retCode = db_query->RunQuery("CREATE TABLE %s;", tableName);
	
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_propertyAttributePeriod->GetMetaObject(), nullptr);

		for (const auto object : GetDimensionArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				object, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (const auto object : GetResourceArrayObject()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				object, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		ibValueMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (const auto object : dstValue->GetDimensionArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindDimensionObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (const auto object : GetDimensionArrayObject()) {
				retCode = ProcessDimension(tableName,
					object, dstValue->FindDimensionObjectByFilter(object->GetGuid())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (const auto object : dstValue->GetResourceArrayObject()) {
				ibValueMetaObject* foundedMeta =
					ibValueMetaObjectRegisterData::FindResourceObjectByFilter(object->GetGuid());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, object);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
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

////////////////////////////////////////////////////////////////////////////////

bool ibValueMetaObjectAccumulationRegister::CreateAndUpdateTableDB(ibMetaDataConfiguration* srcMetaData, ibValueMetaObject* srcMetaObject, int flags)
{
	ibValueMetaObjectAccumulationRegister* dstValue = nullptr; int tableBalancesFlags = flags, tableTurnoverFlags = flags;

	if (srcMetaObject != nullptr &&
		srcMetaObject->ConvertToValue(dstValue)) {
		ibRegisterType dstRegType = dstValue->GetRegisterType();
		if (dstRegType != GetRegisterType()
			&& GetRegisterType() == ibRegisterType::eBalances) {
			tableBalancesFlags = createMetaTable;
			tableTurnoverFlags = deleteMetaTable;
		}
		else if (dstRegType != GetRegisterType()
			&& GetRegisterType() == ibRegisterType::eTurnovers) {
			tableBalancesFlags = deleteMetaTable;
			tableTurnoverFlags = createMetaTable;
		}
		else if (GetRegisterType() == ibRegisterType::eTurnovers) {
			tableBalancesFlags = defaultFlag;
		}
		else if (GetRegisterType() == ibRegisterType::eBalances) {
			tableTurnoverFlags = defaultFlag;
		}
	}
	else if (srcMetaObject == nullptr
		&& flags == createMetaTable) {
		if (GetRegisterType() == ibRegisterType::eBalances) {
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
		if (GetRegisterType() == ibRegisterType::eBalances) {
			tableBalancesFlags = deleteMetaTable;
			tableTurnoverFlags = defaultFlag;
		}
		else {
			tableBalancesFlags = defaultFlag;
			tableTurnoverFlags = deleteMetaTable;
		}
	}

	//if (tableBalancesFlags != defaultFlag
	//	&& !ibValueMetaObjectAccumulationRegister::CreateAndUpdateBalancesTableDB(srcMetaData, srcMetaObject, tableBalancesFlags))
	//	return false;

	//if (tableTurnoverFlags != defaultFlag
	//	&& !ibValueMetaObjectAccumulationRegister::CreateAndUpdateTurnoverTableDB(srcMetaData, srcMetaObject, tableTurnoverFlags))
	//	return false;

	return ibValueMetaObjectRegisterData::CreateAndUpdateTableDB(
		srcMetaData,
		srcMetaObject,
		flags
	);
}

////////////////////////////////////////////////////////////////////////////////

bool ibValueRecordSetObjectAccumulationRegister::SaveVirtualTable()
{
	/*ibValueMetaObjectAccumulationRegister* metaObject = nullptr;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	ibValueMetaObjectAttributePredefined* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB(); bool firstUpdate = true;
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + "(" + ibValueMetaObjectAttributeBase::GetSQLFieldName(attributePeriod);

	for (const auto object : metaObject->GetDimensionArrayObject()) {
		queryText += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(attribute);
	}

	queryText += ") VALUES ("; bool firstInsert = true;

	unsigned int fieldCount = ibValueMetaObjectAttributeBase::GetSQLFieldCount(attributePeriod);
	for (unsigned int i = 0; i < fieldCount; i++) {
		queryText += (firstInsert ? "?" : ",?");
		if (firstInsert) {
			firstInsert = false;
		}
	}

	for (const auto object : metaObject->GetDimensionArrayObject()) {
		for (unsigned int i = 0; i < ibValueMetaObjectAttributeBase::GetSQLFieldCount(attribute); i++) {
			queryText += ",?";
		}
	}

	queryText += ") MATCHING ( " + ibValueMetaObjectAttributeBase::GetSQLFieldName(attributePeriod);

	for (const auto object : metaObject->GetDimensionArrayObject()) {
		queryText += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(attribute);
	}

	queryText += ");";

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);

	if (statement == nullptr)
		return false;

	bool hasError = false;

	for (auto objectValue : m_listObjectValue) {

		if (hasError)
			break;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(
			attributePeriod,
			objectValue.at(attributePeriod->GetMetaID()),
			statement,
			position
		);

		for (const auto object : metaObject->GetDimensionArrayObject()) {
			auto foundedKey = m_keyValues.find(attribute->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
					attribute,
					foundedKey->second,
					statement,
					position
				);
			}
			else {
				ibValueMetaObjectAttributeBase::SetValueAttribute(
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

bool ibValueRecordSetObjectAccumulationRegister::DeleteVirtualTable()
{
	/*ibValueMetaObjectAccumulationRegister* metaObject = nullptr;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	ibValueMetaObjectAttributePredefined* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;

	for (const auto object : metaObject->GetDimensionArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;

	if (statement == nullptr)
		return false;

	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (!ibValueRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::SetValueAttribute(
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