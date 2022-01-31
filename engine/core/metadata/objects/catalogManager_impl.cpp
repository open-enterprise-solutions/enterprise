////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"

CValue CManagerCatalogValue::FindByCode(const CValue &vCode)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {
			wxString UUID = wxEmptyString;

			CMetaDefaultAttributeObject *catCode = m_metaObject->GetCatCode();
			wxASSERT(catCode);

			wxString sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE %s LIKE ?";
			PreparedStatement *statement = databaseLayer->PrepareStatement(sqlQuery, tableName, catCode->GetFieldNameDB());
			if (statement == NULL) {
				return new CValueReference(m_metaObject);
			}

			CValueTypeDescription *tdCode = new CValueTypeDescription(catCode->GetClassTypeObject(),
				catCode->GetNumberQualifier(), catCode->GetDateQualifier(), catCode->GetStringQualifier());
			CValue code = tdCode->AdjustValue(vCode);

			statement->SetParamString(1, code.GetString());
			wxDELETE(tdCode);

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

CValue CManagerCatalogValue::FindByName(const CValue &vName)
{
	if (appData->EnterpriseMode()) {
		wxString tableName = m_metaObject->GetTableNameDB();
		if (databaseLayer->TableExists(tableName)) {
			wxString UUID = wxEmptyString;

			CMetaDefaultAttributeObject *catName = m_metaObject->GetCatCode();
			wxASSERT(catName);

			wxString sqlQuery = "SELECT FIRST 1 UUID FROM %s WHERE %s LIKE ?";
			PreparedStatement *statement = databaseLayer->PrepareStatement(sqlQuery, tableName, catName->GetFieldNameDB());
			if (statement == NULL) {
				return new CValueReference(m_metaObject);
			}

			CValueTypeDescription *tdName = new CValueTypeDescription(catName->GetClassTypeObject(),
				catName->GetNumberQualifier(), catName->GetDateQualifier(), catName->GetStringQualifier());
			CValue name = tdName->AdjustValue(vName);

			statement->SetParamString(1, name.GetString());
			wxDELETE(tdName);

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