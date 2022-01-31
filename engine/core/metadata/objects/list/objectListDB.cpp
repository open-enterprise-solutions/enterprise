////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "utils/stringUtils.h"

void IDataObjectList::UpdateModel()
{
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName + " ORDER BY CAST(UUID AS VARCHAR(36)); ";

	m_aObjectValues.clear();

	PreparedStatement *statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet *resultSet = statement->RunQueryWithResults();

	while (resultSet->Next()) {
		std::map<meta_identifier_t, CValue> valueRow;
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(guidRef, bufferData);
		
		if (!bufferData.IsEmpty()) {
			valueRow.insert_or_assign(m_metaObject->GetMetaID(), CValueReference::CreateFromPtr(
				m_metaObject->GetMetadata(), bufferData.GetData())
			);
		}
		
		for (auto attribute : m_metaObject->GetObjectAttributes()) {
			wxString fieldName = attribute->GetName();
			switch (attribute->GetTypeObject())
			{
			case eValueTypes::TYPE_BOOLEAN:
				valueRow.insert_or_assign(attribute->GetMetaID(), resultSet->GetResultBool(attribute->GetFieldNameDB()));
				break;
			case eValueTypes::TYPE_NUMBER:
				valueRow.insert_or_assign(attribute->GetMetaID(), resultSet->GetResultDouble(attribute->GetFieldNameDB()));
				break;
			case eValueTypes::TYPE_DATE:
				valueRow.insert_or_assign(attribute->GetMetaID(), resultSet->GetResultDate(attribute->GetFieldNameDB()));
				break;
			case eValueTypes::TYPE_STRING:
				valueRow.insert_or_assign(attribute->GetMetaID(), resultSet->GetResultString(attribute->GetFieldNameDB()));
				break;
			default:
				wxMemoryBuffer bufferData;
				resultSet->GetResultBlob(attribute->GetFieldNameDB(), bufferData);
				if (!bufferData.IsEmpty()) {
					valueRow.insert_or_assign(attribute->GetMetaID(), CValueReference::CreateFromPtr(
						m_metaObject->GetMetadata(), bufferData.GetData())
					);
				}
				break;
			}
		}
		m_aObjectValues.insert_or_assign(resultSet->GetResultString(guidName), valueRow);
	};

	resultSet->Close();

	if (!CTranslateError::IsSimpleMode()) {
		IValueTable::Reset(m_aObjectValues.size());
	}

	databaseLayer->CloseStatement(statement);
}