////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

#include "core/metadata/metaObjects/attribute/metaAttributeObject.h"

CReferenceDataObject* CCatalogManager::FindByCode(const CValue& vCode)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {

			CMetaDefaultAttributeObject* catCode = m_metaObject->GetDataCode();
			wxASSERT(catCode);

			wxString sqlQuery = "";
			if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				sqlQuery = "SELECT UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catCode, "LIKE") + " LIMIT 1";
			else
				sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catCode, "LIKE");

			PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery, tableName);
			if (statement == NULL)
				return CReferenceDataObject::Create(m_metaObject);

			const CValue &code = catCode->AdjustValue(vCode);
		
			int position = 1;
			IMetaAttributeObject::SetValueAttribute(catCode, code, statement, position);

			CReferenceDataObject* foundedReference = NULL;
			DatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
			wxASSERT(databaseResultSet);
			if (databaseResultSet->Next()) {
				Guid foundedGuid = databaseResultSet->GetResultString(guidName);
				if (foundedGuid.isValid()) {
					foundedReference = CReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
			}
			databaseResultSet->Close();

			if (foundedReference != NULL) {
				return foundedReference;
			}
		}
	}
	return CReferenceDataObject::Create(m_metaObject);
}

CReferenceDataObject* CCatalogManager::FindByName(const CValue& vName)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {
		
			CMetaDefaultAttributeObject* catName = m_metaObject->GetDataCode();
			wxASSERT(catName);

			wxString sqlQuery = "";	
			if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
				sqlQuery = "SELECT UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catName, "LIKE") + " LIMIT 1";
			else 
				sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catName, "LIKE");

			PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery, tableName);

			if (statement == NULL)
				return CReferenceDataObject::Create(m_metaObject);

			const CValue &name = catName->AdjustValue(vName);

			int position = 1;
			IMetaAttributeObject::SetValueAttribute(catName, name, statement, position);

			CReferenceDataObject* foundedReference = NULL;
			DatabaseResultSet* databaseResultSet = statement->RunQueryWithResults();
			wxASSERT(databaseResultSet);
			if (databaseResultSet->Next()) {
				Guid foundedGuid = databaseResultSet->GetResultString(guidName);
				if (foundedGuid.isValid()) {
					foundedReference = CReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
			}
			databaseResultSet->Close();

			if (foundedReference != NULL) {
				return foundedReference;
			}
		}
	}
	return CReferenceDataObject::Create(m_metaObject);
}