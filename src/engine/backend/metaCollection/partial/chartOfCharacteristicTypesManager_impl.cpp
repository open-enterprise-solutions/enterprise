////////////////////////////////////////////////////////////////////////////
//	Author		: Tetracode Dev
//	Description : chart of characteristic types manager
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypesManager.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfCharacteristicTypes::FindByCode(const ibValue& cParam) const
{
	if (!appData->DesignerMode()) {

		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!cParam.IsEmpty()) {
			const wxString& tableName = m_metaObject->GetTableNameDB();
			if (db_query->TableExists(tableName)) {
				ibValueMetaObjectAttributePredefined* attributeCode = m_metaObject->GetDataCode();
				wxASSERT(attributeCode);
				wxString sqlQuery = "";
				if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
					sqlQuery = "SELECT uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeCode, "LIKE") + " LIMIT 1";
				else
					sqlQuery = "SELECT FIRST 1 uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeCode, "LIKE");
				ibPreparedStatement* statement = db_query->PrepareStatement(sqlQuery, tableName);
				if (statement == nullptr)
					return ibValueReferenceDataObject::Create(m_metaObject);
				int position = 1;
				ibValueMetaObjectAttributeBase::SetValueAttribute(attributeCode, attributeCode->AdjustValue(cParam), statement, position);
				ibValueReferenceDataObject* foundedReference = nullptr;
				ibDatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
				wxASSERT(databaseResultSet);
				if (databaseResultSet->Next()) {
					const ibGuid& foundedGuid = databaseResultSet->GetResultString(guidName);
					if (foundedGuid.isValid()) foundedReference = ibValueReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
				db_query->CloseResultSet(databaseResultSet);
				db_query->CloseStatement(statement);
				if (foundedReference != nullptr) return foundedReference;
			}
		}
	}
	return ibValueReferenceDataObject::Create(m_metaObject);
}

ibValueReferenceDataObject* ibValueManagerDataObjectChartOfCharacteristicTypes::FindByDescription(const ibValue& cParam) const
{
	if (!appData->DesignerMode()) {

		if (db_query != nullptr && !db_query->IsOpen())
			ibBackendCoreException::Error(_("Database is not open!"));
		else if (db_query == nullptr)
			ibBackendCoreException::Error(_("Database is not open!"));

		if (!cParam.IsEmpty()) {
			const wxString tableName = m_metaObject->GetTableNameDB();
			if (db_query->TableExists(tableName)) {
				ibValueMetaObjectAttributePredefined* attributeDescription = m_metaObject->GetDataDescription();
				wxASSERT(attributeDescription);
				wxString sqlQuery = "";
				if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
					sqlQuery = "SELECT uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeDescription, "LIKE") + " LIMIT 1";
				else
					sqlQuery = "SELECT FIRST 1 uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeDescription, "LIKE");
				ibPreparedStatement* statement = db_query->PrepareStatement(sqlQuery, tableName);
				if (statement == nullptr) return ibValueReferenceDataObject::Create(m_metaObject);
				int position = 1;
				ibValueMetaObjectAttributeBase::SetValueAttribute(attributeDescription, attributeDescription->AdjustValue(cParam), statement, position);
				ibValueReferenceDataObject* foundedReference = nullptr;
				ibDatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
				wxASSERT(databaseResultSet);
				if (databaseResultSet->Next()) {
					const ibGuid& foundedGuid = databaseResultSet->GetResultString(guidName);
					if (foundedGuid.isValid()) foundedReference = ibValueReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
				db_query->CloseResultSet(databaseResultSet);
				db_query->CloseStatement(statement);
				if (foundedReference != nullptr) return foundedReference;
			}
		}
	}
	return ibValueReferenceDataObject::Create(m_metaObject);
}
