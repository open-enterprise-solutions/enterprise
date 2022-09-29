////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"

#include "metadata/metaObjects/attribute/metaAttributeObject.h"

CReferenceDataObject* CCatalogManager::FindByCode(const CValue& vCode)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {

			CMetaDefaultAttributeObject* catCode = m_metaObject->GetCatalogCode();
			wxASSERT(catCode);

			wxString sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catCode, "LIKE");
			PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery, tableName);
			if (statement == NULL)
				return CReferenceDataObject::Create(m_metaObject);

			CValue code = catCode->AdjustValue(vCode);
		
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
			wxString UUID = wxEmptyString;

			CMetaDefaultAttributeObject* catName = m_metaObject->GetCatalogCode();
			wxASSERT(catName);

			wxString sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(catName, "LIKE");
			PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery, tableName);

			if (statement == NULL)
				return CReferenceDataObject::Create(m_metaObject);

			CValue name = catName->AdjustValue(vName);

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