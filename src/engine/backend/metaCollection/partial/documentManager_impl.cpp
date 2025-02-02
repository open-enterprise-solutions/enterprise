////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"

CReferenceDataObject * CDocumentManager::FindByNumber(const CValue &vNumber, const CValue &vPeriod)
{
	if (!appData->DesignerMode()) {
		
		if (db_query != nullptr && !db_query->IsOpen())
			CBackendException::Error(_("database is not open!"));
		else if (db_query == nullptr)
			CBackendException::Error(_("database is not open!"));

		wxString tableName = m_metaObject->GetTableNameDB();
		if (db_query->TableExists(tableName)) {

			CMetaObjectAttributeDefault* docNumber = m_metaObject->GetDocumentNumber();
			CMetaObjectAttributeDefault* docDate = m_metaObject->GetDocumentDate();
			wxASSERT(docNumber && docDate);

			wxString sqlQuery = "";
			if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
				sqlQuery = "SELECT _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(docNumber, "LIKE") + " LIMIT 1;";
				if (!vPeriod.IsEmpty()) {
					sqlQuery += IMetaObjectAttribute::GetCompositeSQLFieldName(docNumber, "<=");
				}
			}
			else {
				sqlQuery = "SELECT FIRST 1 _uuid FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(docNumber, "LIKE") + ";";
				if (!vPeriod.IsEmpty()) {
					sqlQuery += IMetaObjectAttribute::GetCompositeSQLFieldName(docNumber, "<=");
				}
			}
		
			IPreparedStatement *statement = nullptr;
			if (!vPeriod.IsEmpty()) {
				statement = db_query->PrepareStatement(sqlQuery, tableName);
				if (statement == nullptr) {
					return CReferenceDataObject::Create(m_metaObject);
				}
				
				int position = 1;

				CValue number = docNumber->AdjustValue(vNumber);
				IMetaObjectAttribute::SetValueAttribute(docNumber, number, statement, position);
				CValue period = docDate->AdjustValue(vPeriod);
				IMetaObjectAttribute::SetValueAttribute(docDate, period, statement, position);
			}
			else {
				statement = db_query->PrepareStatement(sqlQuery, tableName);
				if (statement == nullptr) {
					return CReferenceDataObject::Create(m_metaObject);
				}

				int position = 1;

				CValue number = docNumber->AdjustValue(vNumber);
				IMetaObjectAttribute::SetValueAttribute(docNumber, number, statement, position);
			}

			CReferenceDataObject *foundedReference = nullptr;

			IDatabaseResultSet *databaseResultSet = statement->RunQueryWithResults();
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
