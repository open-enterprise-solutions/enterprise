////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>

CReferenceDataObject * CDocumentManager::FindByNumber(const CValue &vNumber, const CValue &vPeriod)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {
			wxString UUID = wxEmptyString;

			CMetaDefaultAttributeObject* docNumber = m_metaObject->GetDocumentNumber();
			CMetaDefaultAttributeObject* docDate = m_metaObject->GetDocumentDate();
			wxASSERT(docNumber && docDate);

			wxString sqlQuery = "";
			if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
				sqlQuery = "SELECT UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(docNumber, "LIKE") + " LIMIT 1;";
				if (!vPeriod.IsEmpty()) {
					sqlQuery += IMetaAttributeObject::GetCompositeSQLFieldName(docNumber, "<=");
				}
			}
			else {
				sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(docNumber, "LIKE") + ";";
				if (!vPeriod.IsEmpty()) {
					sqlQuery += IMetaAttributeObject::GetCompositeSQLFieldName(docNumber, "<=");
				}
			}
		
			PreparedStatement *statement = NULL;
			if (!vPeriod.IsEmpty()) {
				statement = databaseLayer->PrepareStatement(sqlQuery, tableName);
				if (statement == NULL) {
					return CReferenceDataObject::Create(m_metaObject);
				}
				
				int position = 1;

				CValue number = docNumber->AdjustValue(vNumber);
				IMetaAttributeObject::SetValueAttribute(docNumber, number, statement, position);
				CValue period = docDate->AdjustValue(vPeriod);
				IMetaAttributeObject::SetValueAttribute(docDate, period, statement, position);
			}
			else {
				statement = databaseLayer->PrepareStatement(sqlQuery, tableName);
				if (statement == NULL) {
					return CReferenceDataObject::Create(m_metaObject);
				}

				int position = 1;

				CValue number = docNumber->AdjustValue(vNumber);
				IMetaAttributeObject::SetValueAttribute(docNumber, number, statement, position);
			}

			CReferenceDataObject *foundedReference = NULL;

			DatabaseResultSet *databaseResultSet = statement->RunQueryWithResults();
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
