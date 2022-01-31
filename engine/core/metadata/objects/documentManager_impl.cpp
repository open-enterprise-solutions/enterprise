////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : document manager
////////////////////////////////////////////////////////////////////////////

#include "documentManager.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/valueTypeDescription.h"

CValue CManagerDocumentValue::FindByNumber(const CValue &vNumber, const CValue &vPeriod)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {
			wxString UUID = wxEmptyString;

			CMetaDefaultAttributeObject *docNumber = m_metaObject->GetDocNumber();
			CMetaDefaultAttributeObject *docDate = m_metaObject->GetDocDate();
			wxASSERT(docNumber && docDate);

			wxString sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE %s LIKE ?";

			if (!vPeriod.IsEmpty()) {
				sqlQuery += "%s <= ?";
			}

			PreparedStatement *statement = NULL;
			if (!vPeriod.IsEmpty()) {
				statement = databaseLayer->PrepareStatement(sqlQuery, tableName, docNumber->GetFieldNameDB(), docDate->GetFieldNameDB());
				if (statement == NULL) {
					return new CValueReference(m_metaObject);
				}
				
				CValueTypeDescription *tdNumber = new CValueTypeDescription(docNumber->GetClassTypeObject(),
					docNumber->GetNumberQualifier(), docNumber->GetDateQualifier(), docNumber->GetStringQualifier());		
				CValue number = tdNumber->AdjustValue(vNumber);

				CValueTypeDescription *tdDate = new CValueTypeDescription(docDate->GetClassTypeObject(),
					docDate->GetNumberQualifier(), docDate->GetDateQualifier(), docDate->GetStringQualifier());
				CValue period = tdDate->AdjustValue(vPeriod);

				statement->SetParamString(1, number.GetString());
				statement->SetParamDate(2, period.GetDate());

				wxDELETE(tdNumber); wxDELETE(tdDate);
			}
			else {
				statement = databaseLayer->PrepareStatement(sqlQuery, tableName, docNumber->GetFieldNameDB());
				if (statement == NULL) {
					return new CValueReference(m_metaObject);
				}

				CValueTypeDescription *tdNumber = new CValueTypeDescription(docNumber->GetClassTypeObject(),
					docNumber->GetNumberQualifier(), docNumber->GetDateQualifier(), docNumber->GetStringQualifier());
				CValue number = tdNumber->AdjustValue(vNumber);

				statement->SetParamString(1, number.GetString()); 

				wxDELETE(tdNumber);
			}

			CValueReference *foundedReference = NULL;

			DatabaseResultSet *databaseResultSet = statement->RunQueryWithResults();
			wxASSERT(databaseResultSet);
			if (databaseResultSet->Next()) {
				Guid foundedGuid = databaseResultSet->GetResultString(guidName);
				if (foundedGuid.isValid()) {
					foundedReference = new CValueReference(m_metaObject, foundedGuid);
				}
			}
			databaseResultSet->Close();

			if (foundedReference != NULL) {
				return foundedReference;
			}
		}
	}
	return new CValueReference(m_metaObject);
}
