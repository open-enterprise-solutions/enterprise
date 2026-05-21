////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : catalog manager
////////////////////////////////////////////////////////////////////////////

#include "catalogManager.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

ibValueReferenceDataObject* ibValueManagerDataObjectCatalog::FindByCode(const ibValue& cParam) const 
{
	if (!appData->DesignerMode() || ibApplicationData::DesignerDataWriteEnabled()) {

		const auto db = ses_query;

		if (!cParam.IsEmpty()) {
			const wxString tableName = m_metaObject->GetTableNameDB();
			ibValueMetaObjectAttributePredefined* attributeCode = m_metaObject->GetDataCode();
			wxASSERT(attributeCode);
			const wxString sqlQuery = (db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD)
				? "SELECT FIRST 1 uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeCode, "LIKE")
				: "SELECT uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeCode, "LIKE") + " LIMIT 1";
			ibStatementGuard statement(db, db->PrepareStatement(sqlQuery, tableName));
			if (!statement)
				return ibValueReferenceDataObject::Create(m_metaObject);
			int position = 1;
			ibValueMetaObjectAttributeBase::SetValueAttribute(attributeCode, attributeCode->AdjustValue(cParam), statement.get(), position);
			ibValueReferenceDataObject* foundedReference = nullptr;
			if (ibDatabaseResultSet* rs = statement->RunQueryWithResults()) {
				if (rs->Next()) {
					const ibGuid& foundedGuid = rs->GetResultString(guidName);
					if (foundedGuid.isValid()) foundedReference = ibValueReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
				db->CloseResultSet(rs);
			}
			if (foundedReference != nullptr) return foundedReference;
		}
	}
	return ibValueReferenceDataObject::Create(m_metaObject);
}

ibValueReferenceDataObject* ibValueManagerDataObjectCatalog::FindByDescription(const ibValue& cParam) const
{
	if (!appData->DesignerMode() || ibApplicationData::DesignerDataWriteEnabled()) {

		const auto db = ses_query;

		if (!cParam.IsEmpty()) {
			const wxString tableName = m_metaObject->GetTableNameDB();
			ibValueMetaObjectAttributePredefined* attributeDescription = m_metaObject->GetDataDescription();
			wxASSERT(attributeDescription);
			const wxString sqlQuery = (db->GetDatabaseLayerType() == DATABASELAYER_FIREBIRD)
				? "SELECT FIRST 1 uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeDescription, "LIKE")
				: "SELECT uuid FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attributeDescription, "LIKE") + " LIMIT 1";
			ibStatementGuard statement(db, db->PrepareStatement(sqlQuery, tableName));
			if (!statement) return ibValueReferenceDataObject::Create(m_metaObject);
			int position = 1;
			ibValueMetaObjectAttributeBase::SetValueAttribute(attributeDescription, attributeDescription->AdjustValue(cParam), statement.get(), position);
			ibValueReferenceDataObject* foundedReference = nullptr;
			if (ibDatabaseResultSet* rs = statement->RunQueryWithResults()) {
				if (rs->Next()) {
					const ibGuid& foundedGuid = rs->GetResultString(guidName);
					if (foundedGuid.isValid()) foundedReference = ibValueReferenceDataObject::Create(m_metaObject, foundedGuid);
				}
				db->CloseResultSet(rs);
			}
			if (foundedReference != nullptr) return foundedReference;
		}
	}	
	return ibValueReferenceDataObject::Create(m_metaObject);
}
