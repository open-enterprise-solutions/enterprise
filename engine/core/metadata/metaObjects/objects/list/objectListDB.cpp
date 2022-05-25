////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "utils/stringUtils.h"

void CListDataObjectRef::RefreshModel()
{
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName + " ORDER BY CAST(UUID AS VARCHAR(36));";

	m_aObjectValues.clear();

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();

	while (resultSet->Next()) {
		std::map<meta_identifier_t, CValue> valueRow;
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			wxASSERT(metaReference);
			valueRow.insert_or_assign(metaReference->GetMetaID(), CReferenceDataObject::CreateFromPtr(
				m_metaObject->GetMetadata(), bufferData.GetData())
			);
		}
		else {
			wxASSERT(metaReference);
			valueRow.insert_or_assign(metaReference->GetMetaID(),
				CReferenceDataObject::Create(m_metaObject)
			);
		}
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			valueRow.insert_or_assign(attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet)
			);
		}
		m_aObjectValues.insert_or_assign(
			resultSet->GetResultString(guidName), valueRow
		);
	};

	resultSet->Close();

	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Reset(m_aObjectValues.size());
	}

	databaseLayer->CloseStatement(statement);
}

void CListRegisterObject::RefreshModel()
{
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;

	m_aObjectValues.clear();

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	while (resultSet->Next()) {
		std::map<meta_identifier_t, CValue> keyRow;
		std::map<meta_identifier_t, CValue> valueRow;
		if (m_metaObject->HasRecorder()) {
			CMetaDefaultAttributeObject* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			keyRow.insert_or_assign(attributeRecorder->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attributeRecorder, resultSet));
			CMetaDefaultAttributeObject* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			keyRow.insert_or_assign(attributeNumberLine->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attributeNumberLine, resultSet));
		}
		else {
			for (auto dimension : m_metaObject->GetGenericDimensions()) {
				keyRow.insert_or_assign(dimension->GetMetaID(),
					IMetaAttributeObject::GetValueAttribute(dimension, resultSet));
			}
		}
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			valueRow.insert_or_assign(attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet)
			);
		}
		m_aObjectValues.insert_or_assign(
			keyRow, valueRow
		);
	};

	resultSet->Close();

	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Reset(m_aObjectValues.size());
	}

	databaseLayer->CloseStatement(statement);
}