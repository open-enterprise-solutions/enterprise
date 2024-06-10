////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "utils/stringUtils.h"

void CListDataObjectEnumRef::RefreshModel(const wxDataViewItem& topItem, int countPerPage) {
	const wxString& tableName = GetMetaObject()->GetTableNameDB();
	wxString queryText = wxString::Format("SELECT * FROM %s", tableName);
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& oper = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute, oper);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, oper);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	queryText = queryText + whereText;
	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();
	CMetaDefaultAttributeObject* metaOrder = m_metaObject->GetDataOrder();
	IValueTable::Clear();
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = NULL;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaAttributeObject::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {
		const Guid& enumRow = resultSet->GetResultString(guidName);
		wxMemoryBuffer bufferData; wxValueTableEnumRow* rowData = new wxValueTableEnumRow(enumRow);
		rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumByGuid(enumRow.str())->GetParentPosition());
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			wxASSERT(metaReference);
			CReferenceDataObject* createdReference = CReferenceDataObject::Create(metaReference->GetMetadata(), bufferData.GetData());
			if (createdReference != NULL)
				createdReference->FillFromModel(rowData->GetTableValues());
			rowData->AppendTableValue(metaReference->GetMetaID(), createdReference);
		}
		else {
			wxASSERT(metaReference);
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::Create(GetMetaObject()));
		}
		IValueTable::Append(rowData, !CTranslateError::IsSimpleMode());
	};

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CListDataObjectRef::RefreshModel(const wxDataViewItem& topItem, int countPerPage)
{
	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = wxString::Format("SELECT * FROM %s", tableName);
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& oper = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute, oper);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, oper);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	const std::vector<IMetaAttributeObject*>& vec_attr = m_metaObject->GetGenericAttributes();
	queryText = queryText + whereText;
	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();
	IValueTable::Clear();
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = NULL;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaAttributeObject::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {
		wxMemoryBuffer bufferData; const Guid& rowGuid = resultSet->GetResultString(guidName);
		wxValueTableListRow *rowData = new wxValueTableListRow(rowGuid);
		for (auto &attribute : vec_attr) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			IMetaAttributeObject::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			wxASSERT(metaReference);
			CReferenceDataObject* createdReference = CReferenceDataObject::Create(metaReference->GetMetadata(), bufferData.GetData());
			if (createdReference != NULL)
				createdReference->FillFromModel(rowData->GetTableValues());
			rowData->AppendTableValue(metaReference->GetMetaID(), createdReference);
		}
		else {
			wxASSERT(metaReference);
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::Create(m_metaObject));
		}

		IValueTable::Append(rowData, !CTranslateError::IsSimpleMode());
	};

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::RefreshModel(const wxDataViewItem& topItem, int countPerPage)
{
	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& oper = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute, oper);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, oper);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	const std::vector<IMetaAttributeObject*>& vec_attr = m_metaObject->GetGenericAttributes();
	queryText = queryText + whereText;
	CMetaDefaultAttributeObject* metaReference = m_metaObject->GetDataReference();
	IValueTree::Clear();
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = NULL;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaAttributeObject::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	
	std::vector<std::pair<Guid, wxValueTreeListNode*>> treeData; CValue cRefVal;
	
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	wxASSERT(metaReference);
	while (resultSet->Next()) {

		CValue isFolder = false;
		if (!IMetaAttributeObject::GetValueAttribute(m_metaObject->GetDataIsFolder(), isFolder, resultSet))
			continue;

		if (m_listMode == CTreeDataObjectFolderRef::LIST_FOLDER && !isFolder.GetBoolean())
			continue;

		wxValueTreeListNode* rowData = new wxValueTreeListNode(NULL, resultSet->GetResultString(guidName), this, isFolder.GetBoolean());
		for (auto &attribute : vec_attr) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			IMetaAttributeObject::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		wxMemoryBuffer bufferData; resultSet->GetResultBlob(guidRef, bufferData);
		if (!bufferData.IsEmpty()) {
			CReferenceDataObject* createdReference = CReferenceDataObject::Create(m_metaObject->GetMetadata(), bufferData.GetData());
			if (createdReference != NULL) {
				createdReference->FillFromModel(rowData->GetTableValues());
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), createdReference);
		}
		else {
			wxASSERT(metaReference);
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::Create(m_metaObject));
		}

		treeData.emplace_back(rowData->GetGuid(), rowData);
	};

	/* wxDataViewModel:: */ BeforeReset();

	for (auto data : treeData) {
		wxValueTreeListNode* node = data.second;
		wxASSERT(node); CReferenceDataObject* pRefData = NULL;
		node->GetValue(*m_metaObject->GetDataParent(), cRefVal);
		if (cRefVal.ConvertToValue(pRefData)) {
			auto foundedIt = std::find_if(treeData.begin(), treeData.end(),
				[pRefData](std::pair<Guid, wxValueTreeListNode*>& pair) {
					return pair.first == pRefData->GetGuid();
				}
			);
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

void CListRegisterObject::RefreshModel(const wxDataViewItem& topItem, int countPerPage)
{
	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName + " ORDER BY 1";
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			if (firstWhere)
				whereText += " WHERE ";
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			whereText += (firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute, filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>");
			if (firstWhere)
				firstWhere = false;
		}
	}
	const std::vector<IMetaAttributeObject*>& vec_attr = m_metaObject->GetGenericAttributes(), &vec_dim = m_metaObject->GetGenericDimensions();
	queryText = queryText + whereText;
	IValueTable::Clear();
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaAttributeObject* attribute = dynamic_cast<IMetaAttributeObject*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			IMetaAttributeObject::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
		}
	}
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {
		wxValueTableKeyRow* rowData = new wxValueTableKeyRow();
		if (m_metaObject->HasRecorder()) {
			CMetaDefaultAttributeObject* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			IMetaAttributeObject::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
			CMetaDefaultAttributeObject* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			IMetaAttributeObject::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
		}
		else {
			for (auto &dimension : vec_dim) {
				IMetaAttributeObject::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
			}
		}
		for (auto &attribute : vec_attr) {
			IMetaAttributeObject::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		IValueTable::Append(
			rowData, !CTranslateError::IsSimpleMode()
		);
	};

	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
}