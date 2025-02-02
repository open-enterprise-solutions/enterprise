////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"

#include "backend/metaCollection/attribute/metaAttributeObject.h"

CReferenceDataObject* CCatalogManager::FindByCode(const CValue& vCode)
{
	if (!appData->DesignerMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (db_query->TableExists(tableName)) {

			CMetaObjectAttributeDefault* catCode = m_metaObject->GetDataCode();
			wxASSERT(catCode);

			wxString sqlQuery = "";
			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				sqlQuery = "SELECT _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(catCode, "LIKE") + " LIMIT 1";
			else
				sqlQuery = "SELECT FIRST 1 _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(catCode, "LIKE");

			IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery, tableName);
			if (statement == nullptr)
				return CReferenceDataObject::Create(m_metaObject);

			const CValue &code = catCode->AdjustValue(vCode);
		
			int position = 1;
			IMetaObjectAttribute::SetValueAttribute(catCode, code, statement, position);

			CReferenceDataObject* foundedReference = nullptr;
			IDatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
			wxASSERT(databaseResultSet);
			if (databaseResultSet->Next()) {
				Guid foundedGuid = databaseResultSet->GetResultString(guidName);
				if (foundedGuid.isValid()) {
					foundedReference = CReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
			}
			databaseResultSet->Close();

			if (foundedReference != nullptr) {
				return foundedReference;
			}
		}
	}
	return CReferenceDataObject::Create(m_metaObject);
}

CReferenceDataObject* CCatalogManager::FindByName(const CValue& vName)
{
	if (!appData->DesignerMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (db_query->TableExists(tableName)) {
		
			CMetaObjectAttributeDefault* catName = m_metaObject->GetDataCode();
			wxASSERT(catName);

			wxString sqlQuery = "";	
			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				sqlQuery = "SELECT _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(catName, "LIKE") + " LIMIT 1";
			else 
				sqlQuery = "SELECT FIRST 1 _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(catName, "LIKE");

			IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery, tableName);

			if (statement == nullptr)
				return CReferenceDataObject::Create(m_metaObject);

			const CValue &name = catName->AdjustValue(vName);

			int position = 1;
			IMetaObjectAttribute::SetValueAttribute(catName, name, statement, position);

			CReferenceDataObject* foundedReference = nullptr;
			IDatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
			wxASSERT(databaseResultSet);
			if (databaseResultSet->Next()) {
				Guid foundedGuid = databaseResultSet->GetResultString(guidName);
				if (foundedGuid.isValid()) {
					foundedReference = CReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
			}
			databaseResultSet->Close();

			if (foundedReference != nullptr) {
				return foundedReference;
			}
		}
	}
	return CReferenceDataObject::Create(m_metaObject);
}