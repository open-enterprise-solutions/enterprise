#include "accumulationRegister.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "appData.h"

////////////////////////////////////////////////////////////////////////////////

bool CMetaObjectAccumulationRegister::CreateAndUpdateBalancesTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(eRegisterType::eBalances);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		retCode = databaseLayer->RunQuery("CREATE TABLE %s (rowData BLOB);", tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_attributePeriod, NULL);

		for (auto dimension : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				dimension, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto resource : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				resource, NULL);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (auto dimension : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByName(dimension->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessDimension(tableName, NULL, dimension);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto dimension : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					dimension, dstValue->FindDimensionByName(dimension->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto resource : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByName(resource->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessResource(tableName, NULL, resource);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto resource : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					resource, dstValue->FindResourceByName(resource->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

bool CMetaObjectAccumulationRegister::CreateAndUpdateTurnoverTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetRegisterTableNameDB(eRegisterType::eTurnovers);

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		retCode = databaseLayer->RunQuery("CREATE TABLE %s (rowData BLOB);", tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = ProcessAttribute(tableName,
			m_attributePeriod, NULL);

		for (auto dimension : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				dimension, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto resource : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				resource, NULL);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;
			//dimensions from dst 
			for (auto dimension : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByName(dimension->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessDimension(tableName, NULL, dimension);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto dimension : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					dimension, dstValue->FindDimensionByName(dimension->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto resource : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByName(resource->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessResource(tableName, NULL, resource);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto resource : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					resource, dstValue->FindResourceByName(resource->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool CMetaObjectAccumulationRegister::CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	CMetaObjectAccumulationRegister* dstValue = NULL; int tableBalancesFlags = flags, tableTurnoverFlags = flags;

	if (srcMetaObject != NULL &&
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
	else if (srcMetaObject == NULL
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
	else if (srcMetaObject == NULL
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

bool CRecordSetAccumulationRegister::SaveVirtualTable()
{
	/*CMetaObjectAccumulationRegister* metaObject = NULL;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	CMetaDefaultAttributeObject* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB(); bool firstUpdate = true;
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + "(" + IMetaAttributeObject::GetSQLFieldName(attributePeriod);

	for (auto attribute : metaObject->GetObjectDimensions()) {
		queryText += "," + IMetaAttributeObject::GetSQLFieldName(attribute);
	}

	queryText += ") VALUES ("; bool firstInsert = true;

	unsigned int fieldCount = IMetaAttributeObject::GetSQLFieldCount(attributePeriod);
	for (unsigned int i = 0; i < fieldCount; i++) {
		queryText += (firstInsert ? "?" : ",?");
		if (firstInsert) {
			firstInsert = false;
		}
	}

	for (auto attribute : metaObject->GetObjectDimensions()) {
		for (unsigned int i = 0; i < IMetaAttributeObject::GetSQLFieldCount(attribute); i++) {
			queryText += ",?";
		}
	}

	queryText += ") MATCHING ( " + IMetaAttributeObject::GetSQLFieldName(attributePeriod);

	for (auto attribute : metaObject->GetObjectDimensions()) {
		queryText += "," + IMetaAttributeObject::GetSQLFieldName(attribute);
	}

	queryText += ");";

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);

	if (statement == NULL)
		return false;

	bool hasError = false;

	for (auto objectValue : m_aObjectValues) {

		if (hasError)
			break;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(
			attributePeriod,
			objectValue.at(attributePeriod->GetMetaID()),
			statement,
			position
		);

		for (auto attribute : metaObject->GetObjectDimensions()) {
			auto foundedKey = m_aKeyValues.find(attribute->GetMetaID());
			if (foundedKey != m_aKeyValues.end()) {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					foundedKey->second,
					statement,
					position
				);
			}
			else {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					objectValue.at(attribute->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	databaseLayer->CloseStatement(statement);
	return !hasError;*/
	return true;
}

bool CRecordSetAccumulationRegister::DeleteVirtualTable()
{
	/*CMetaObjectAccumulationRegister* metaObject = NULL;

	if (!m_metaObject->ConvertToValue(metaObject))
		return false;

	CMetaDefaultAttributeObject* attributePeriod = metaObject->GetRegisterPeriod();
	wxASSERT(attributePeriod);

	wxString tableName = metaObject->GetRegisterTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;

	for (auto attribute : metaObject->GetObjectDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;

	if (statement == NULL)
		return false;

	for (auto attribute : metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_aKeyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	statement->RunQuery();
	statement->Close();*/
	return true;
}