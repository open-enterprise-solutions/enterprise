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

	IValueTable::Clear();

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();

	while (resultSet->Next()) {
		modelArray_t valueRow; wxMemoryBuffer bufferData;
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
		
		IValueTable::Append(
			new wxValueTableListRow(valueRow, resultSet->GetResultString(guidName)), !CTranslateError::IsSimpleMode()
		);
	};

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::RefreshModel()
{
	std::vector<std::pair<Guid, wxValueTreeListNode*>> treeData;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName + " ORDER BY CAST(UUID AS VARCHAR(36));";

	IValueTree::Clear();

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();
	wxASSERT(metaReference);
	while (resultSet->Next()) {
		modelArray_t valueRow;
		if (m_listMode == CTreeDataObjectFolderRef::LIST_FOLDER) {
			bool isFolder = IMetaAttributeObject::GetValueAttribute(
				m_metaObject->GetDataIsFolder(), resultSet).GetBoolean();
			if (!isFolder)
				continue;
		};
		wxMemoryBuffer bufferData; CReferenceDataObject* pRefData = NULL;
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			pRefData = CReferenceDataObject::CreateFromPtr(
				m_metaObject->GetMetadata(), bufferData.GetData()
			);
			valueRow.insert_or_assign(metaReference->GetMetaID(), pRefData);
		}
		else {
			pRefData = CReferenceDataObject::Create(m_metaObject);
			wxASSERT(metaReference);
			valueRow.insert_or_assign(metaReference->GetMetaID(), pRefData);
		}
		wxASSERT(pRefData);
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			valueRow.insert_or_assign(attribute->GetMetaID(),
				IMetaAttributeObject::GetValueAttribute(attribute, resultSet)
			);
		}
		treeData.emplace_back(resultSet->GetResultString(guidName),
			new wxValueTreeListNode(
				NULL, valueRow, resultSet->GetResultString(guidName), this
			)
		);
	};

	/* wxDataViewModel:: */ BeforeReset();

	for (auto data : treeData) {
		wxValueTreeListNode* node = data.second; CValue cRefVal; 
		wxASSERT(node); CReferenceDataObject* pRefData = NULL; 
		node->GetValue(cRefVal, *m_metaObject->GetDataParent());
		if (cRefVal.ConvertToValue(pRefData)) {
			auto foundedIt = std::find_if(treeData.begin(), treeData.end(), [pRefData](std::pair<Guid, wxValueTreeListNode*>& pair) { return pair.first == pRefData->GetGuid(); });
			if (foundedIt != treeData.end()) {
				node->SetParent(foundedIt->second);
			}
			else {
				node->SetParent(GetRoot());
			}
		}
	}
	
	/* wxDataViewModel:: */ AfterReset();

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::RefreshModel()
{
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;

	IValueTable::Clear();

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	while (resultSet->Next()) {
		modelArray_t keyRow, valueRow;
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
		IValueTable::Append(
			new wxValueTableKeyRow(valueRow, keyRow), !CTranslateError::IsSimpleMode()
		);
	};
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}