#include "accumulationRegister.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/appData.h"
#include "backend/query/dbTableProvider.h"   // ibDbTableProvider::SetValueAttribute — DB write decomposition

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

		ibDbTableProvider::SetValueAttribute(
			attributePeriod,
			objectValue.at(attributePeriod->GetMetaID()),
			statement,
			position
		);

		for (const auto object : metaObject->GetDimensionArrayObject()) {
			auto foundedKey = m_keyValues.find(attribute->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				ibDbTableProvider::SetValueAttribute(
					attribute,
					foundedKey->second,
					statement,
					position
				);
			}
			else {
				ibDbTableProvider::SetValueAttribute(
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
		ibDbTableProvider::SetValueAttribute(
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