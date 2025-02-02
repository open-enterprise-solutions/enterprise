////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

void CListDataObjectEnumRef::RefreshModel(const int countPerPage) {
	
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	const wxString& tableName = GetMetaObject()->GetTableNameDB();
	wxString queryText = wxString::Format("SELECT * FROM %s", tableName);
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	wxString orderText; bool firstOrder = true;
	for (auto& sort : m_sortOrder.m_sorts) {
		if (sort.m_sortEnable) {
			const wxString& operation_sort = sort.m_sortAscending ? "ASC" : "DESC";
			if (m_metaObject->IsDataReference(sort.m_sortModel)) {
				if (firstOrder) orderText += " ORDER BY ";
				orderText += (firstOrder ? " " : ", ");
				orderText += "	CASE \n";
				for (auto& obj : m_metaObject->GetObjectEnums()) {
					orderText += "	WHEN _uuid = '" + obj->GetGuid().str() + "' THEN " + stringUtils::IntToStr(GetMetaObject()->FindEnumByGuid(obj->GetGuid().str())->GetParentPosition()) + '\n';
				}
				orderText += "	END " + operation_sort + " \n";
				if (firstOrder) firstOrder = false;
			}
		}
	};
	queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
	CMetaObjectAttributeDefault* metaOrder = m_metaObject->GetDataOrder();
	IValueTable::Clear();
	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	/////////////////////////////////////////////////////////
	IValueTable::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	/////////////////////////////////////////////////////////
	while (resultSet->Next()) {
		const Guid& enumRow = resultSet->GetResultString(guidName);
		wxValueTableEnumRow* rowData = new wxValueTableEnumRow(enumRow);
		rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
		rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumByGuid(enumRow.str())->GetParentPosition());
		IValueTable::Append(rowData, !CBackendException::IsEvalMode());
	};

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
}

void CListDataObjectEnumRef::RefreshItemModel(const wxDataViewItem& topItem, const wxDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		wxValueTableEnumRow* valueTableListRow = GetViewData<wxValueTableEnumRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
					if (firstWhere)
						firstWhere = false;
				}
				else {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
					if (firstWhere)
						firstWhere = false;
				}
			}
		}
		wxString orderText; bool firstOrder = true;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = !sort.m_sortAscending ? "ASC" : "DESC";
				if (m_metaObject->IsDataReference(sort.m_sortModel)) {
					int pos_in_parent = 1, current_pos = 0;
					if (firstOrder) orderText += " ORDER BY ";
					if (firstWhere) whereText += " WHERE ";
					orderText += " CASE \n";
					whereText += " CASE \n";
					for (auto& obj : m_metaObject->GetObjectEnums()) {
						const wxString& str_guid = obj->GetGuid().str();
						orderText += " WHEN _uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						whereText += " WHEN _uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						if (obj->GetGuid() == valueTableListRow->GetGuid()) current_pos = pos_in_parent;
						pos_in_parent++;
					}
					orderText += " END " + operation_sort + " \n";
					whereText += " END ";
					whereText += "  < " + stringUtils::IntToStr(current_pos) + " \n";
					if (firstOrder) firstOrder = false;
					if (firstWhere) firstWhere = false;
				}
			}
		};
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
		CMetaObjectAttributeDefault* metaOrder = m_metaObject->GetDataOrder();
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableEnumRow* rowData = new wxValueTableEnumRow(resultSet->GetResultString(guidName));
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumByGuid(rowData->GetGuid().str())->GetParentPosition());
			IValueTable::Insert(rowData, 0, !CBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(IValueTable::GetRowCount() - 1, IValueTable::GetRowCount(), !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		wxValueTableEnumRow* valueTableListRow = GetViewData<wxValueTableEnumRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
					if (firstWhere)
						firstWhere = false;
				}
				else {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
					if (firstWhere)
						firstWhere = false;
				}
			}
		}
		wxString orderText; bool firstOrder = true;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = sort.m_sortAscending ? "ASC" : "DESC";
				if (m_metaObject->IsDataReference(sort.m_sortModel)) {
					int pos_in_parent = 1, current_pos = 0;
					if (firstOrder) orderText += " ORDER BY ";
					if (firstWhere) whereText += " WHERE ";
					orderText += " CASE \n";
					whereText += " CASE \n";
					for (auto& obj : m_metaObject->GetObjectEnums()) {
						const wxString& str_guid = obj->GetGuid().str();
						orderText += " WHEN _uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						whereText += " WHEN _uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						if (obj->GetGuid() == valueTableListRow->GetGuid()) current_pos = pos_in_parent;
						pos_in_parent++;
					}
					orderText += " END " + operation_sort + " \n";
					whereText += " END ";
					whereText += "  > " + stringUtils::IntToStr(current_pos) + " \n";
					if (firstOrder) firstOrder = false;
					if (firstWhere) firstWhere = false;
				}
			}
		};
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
		CMetaObjectAttributeDefault* metaOrder = m_metaObject->GetDataOrder();
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableEnumRow* rowData = new wxValueTableEnumRow(resultSet->GetResultString(guidName));
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumByGuid(rowData->GetGuid().str())->GetParentPosition());
			IValueTable::Append(rowData, !CBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(0, 1, !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CListDataObjectRef::RefreshModel(const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
	wxString whereText; bool firstWhere = true;
	for (auto& filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	int compare_func = 0;
	wxString orderText; bool firstOrder = true;
	for (auto& sort : m_sortOrder.m_sorts) {
		if (sort.m_sortEnable) {
			const wxString& operation = sort.m_sortAscending ? "ASC" : "DESC";
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
				if (firstOrder)
					orderText += " ORDER BY ";
				for (auto& field : IMetaObjectAttribute::GetSQLFieldData(attribute)) {
					if (field.m_type == IMetaObjectAttribute::eFieldTypes_Reference) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation);
						if (firstOrder) firstOrder = false;
					}
					else {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation);
						if (firstOrder) firstOrder = false;
					}
				}
			}
			if (compare_func == 0) {
				compare_func = sort.m_sortAscending ? 1 : -1;
			}
		}
	};

	for (auto& sort : m_sortOrder.m_sorts) {
		if (sort.m_sortEnable) {
			const wxString& operation_sort = (compare_func >= 0 ? sort.m_sortAscending : !sort.m_sortAscending) ? "ASC" : "DESC";
			if (m_metaObject->IsDataReference(sort.m_sortModel)) {
				if (firstOrder)
					orderText += " ORDER BY ";
				orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", guidName, operation_sort);
				if (firstOrder)
					firstOrder = false;
			}
		}
	};

	const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes();
	queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
	/////////////////////////////////////////////////////////
	IValueTable::Clear();
	/////////////////////////////////////////////////////////
	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	/////////////////////////////////////////////////////////
	IValueTable::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {
		wxValueTableListRow* rowData = new wxValueTableListRow(resultSet->GetResultString(guidName));
		for (auto& attribute : vec_attr) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
		IValueTable::Append(rowData, !CBackendException::IsEvalMode());
	};

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
}

void CListDataObjectRef::RefreshItemModel(const wxDataViewItem& topItem, const wxDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		wxValueTableListRow* valueTableListRow = GetViewData<wxValueTableListRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
					if (firstWhere)
						firstWhere = false;
				}
				else {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
					if (firstWhere)
						firstWhere = false;
				}
			}
		}
		wxString orderText; bool firstOrder = true; wxString sortText;
		int compare_func = 0;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = !sort.m_sortAscending ? "ASC" : "DESC";
				const wxString& operation_compare = !sort.m_sortAscending ? ">=" : "<=";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder)
						orderText += " ORDER BY ";
					for (auto& field : IMetaObjectAttribute::GetSQLFieldData(attribute)) {
						if (field.m_type == IMetaObjectAttribute::eFieldTypes_Reference) {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
							if (firstOrder)
								firstOrder = false;

						}
						else {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
							if (firstOrder)
								firstOrder = false;
						}

						if (firstWhere) whereText += " WHERE ";
						whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation_compare);
						sortText += (sortText.IsEmpty() ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, '=');
						if (firstWhere) firstWhere = false;
					}
				}
				if (compare_func == 0) {
					compare_func = sort.m_sortAscending ? 1 : -1;
				}
			}
		};
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = (compare_func >= 0 ? !sort.m_sortAscending : sort.m_sortAscending) ? "ASC" : "DESC";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder)
						orderText += " ORDER BY ";
					orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", guidName, operation_sort);
					if (firstOrder)
						firstOrder = false;

					if (firstWhere)
						whereText += " WHERE ";

					whereText += (firstWhere ? " " : " AND ") + wxString::Format((compare_func >= 0 ? !sort.m_sortAscending : sort.m_sortAscending) ? "case when %s then %s > '%s' else true end" : "case when %s then %s < '%s' else true end", sortText, guidName, valueTableListRow->GetGuid().str());
					if (firstWhere)
						firstWhere = false;
				}
			}
		};
		const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes();
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableListRow* rowData = new wxValueTableListRow(resultSet->GetResultString(guidName));
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			IValueTable::Insert(rowData, 0, !CBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(IValueTable::GetRowCount() - 1, IValueTable::GetRowCount(), !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		wxValueTableListRow* valueTableListRow = GetViewData<wxValueTableListRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
					if (firstWhere)
						firstWhere = false;
				}
				else {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
					if (firstWhere)
						firstWhere = false;
				}
			}
		}
		wxString orderText; bool firstOrder = true; wxString sortText;
		int compare_func = 0;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = sort.m_sortAscending ? "ASC" : "DESC";
				const wxString& operation_compare = sort.m_sortAscending ? ">=" : "<=";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder) orderText += " ORDER BY ";
					for (auto& field : IMetaObjectAttribute::GetSQLFieldData(attribute)) {
						if (field.m_type == IMetaObjectAttribute::eFieldTypes_Reference) {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
							if (firstOrder) firstOrder = false;

						}
						else {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
							if (firstOrder) firstOrder = false;
						}
						if (firstWhere) whereText += " WHERE ";
						whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation_compare);
						sortText += (sortText.IsEmpty() ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, '=');
						if (firstWhere) firstWhere = false;
					}
				}
				if (compare_func == 0) {
					compare_func = sort.m_sortAscending ? 1 : -1;
				}
			}
		};
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = (compare_func >= 0 ? sort.m_sortAscending : !sort.m_sortAscending) ? "ASC" : "DESC";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder)
						orderText += " ORDER BY ";
					orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", guidName, operation_sort);
					if (firstOrder)
						firstOrder = false;

					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + wxString::Format((compare_func >= 0 ? sort.m_sortAscending : !sort.m_sortAscending) ? "case when %s then %s > '%s' else true end" : "case when %s then %s < '%s' else true end", sortText, guidName, valueTableListRow->GetGuid().str());
					if (firstWhere)
						firstWhere = false;
				}
			}
		};
		const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes();
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableListRow* rowData = new wxValueTableListRow(resultSet->GetResultString(guidName));
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			IValueTable::Append(rowData, !CBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(0, 1, !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CListRegisterObject::RefreshModel(const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			if (firstWhere)
				whereText += " WHERE ";
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>");
			if (firstWhere)
				firstWhere = false;
		}
	}
	const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes(), & vec_dim = m_metaObject->GetGenericDimensions();
	queryText = queryText + whereText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	IValueTable::Clear();
	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
		}
	}
	/////////////////////////////////////////////////////////
	IValueTable::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {
		wxValueTableKeyRow* rowData = new wxValueTableKeyRow;
		if (m_metaObject->HasRecorder()) {
			CMetaObjectAttributeDefault* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			IMetaObjectAttribute::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
			CMetaObjectAttributeDefault* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			IMetaObjectAttribute::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
		}
		else {
			for (auto& dimension : vec_dim) {
				IMetaObjectAttribute::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
			}
		}
		for (auto& attribute : vec_attr) {
			IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		IValueTable::Append(
			rowData, !CBackendException::IsEvalMode()
		);
	};

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
}

void CListRegisterObject::RefreshItemModel(const wxDataViewItem& topItem, const wxDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		wxValueTableKeyRow* valueTableListRow = GetViewData<wxValueTableKeyRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
		wxString orderText; bool firstOrder = true; wxString sortText;
		int compare_func = 0;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = !sort.m_sortAscending ? "ASC" : "DESC";
				const wxString& operation_compare = !sort.m_sortAscending ? ">=" : "<=";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (firstOrder)
					orderText += " ORDER BY ";
				for (auto& field : IMetaObjectAttribute::GetSQLFieldData(attribute)) {
					if (field.m_type == IMetaObjectAttribute::eFieldTypes_Reference) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
						if (firstOrder)
							firstOrder = false;

					}
					else {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
						if (firstOrder)
							firstOrder = false;
					}

					if (firstWhere) whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation_compare);
					sortText += (sortText.IsEmpty() ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, '=');
					if (firstWhere) firstWhere = false;
				}
				if (compare_func == 0) {
					compare_func = sort.m_sortAscending ? 1 : -1;
				}
			}
		};
		const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes(), & vec_dim = m_metaObject->GetGenericDimensions();
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableKeyRow* rowData = new wxValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				CMetaObjectAttributeDefault* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				IMetaObjectAttribute::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
				CMetaObjectAttributeDefault* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				IMetaObjectAttribute::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
			}
			else {
				for (auto& dimension : vec_dim) {
					IMetaObjectAttribute::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
				}
			}
			for (auto& attribute : vec_attr) {
				IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(IValueTable::GetRowCount() - 1, IValueTable::GetRowCount(), !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		wxValueTableKeyRow* valueTableListRow = GetViewData<wxValueTableKeyRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();
		wxString queryText = wxString::Format("SELECT * FROM %s ", tableName);
		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
		wxString orderText; bool firstOrder = true; wxString sortText;
		int compare_func = 0;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = sort.m_sortAscending ? "ASC" : "DESC";
				const wxString& operation_compare = sort.m_sortAscending ? ">=" : "<=";
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				if (firstOrder) orderText += " ORDER BY ";
				for (auto& field : IMetaObjectAttribute::GetSQLFieldData(attribute)) {
					if (field.m_type == IMetaObjectAttribute::eFieldTypes_Reference) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
						if (firstOrder) firstOrder = false;

					}
					else {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
						if (firstOrder) firstOrder = false;
					}
					if (firstWhere) whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation_compare);
					sortText += (sortText.IsEmpty() ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, '=');
					if (firstWhere) firstWhere = false;
				}

				if (compare_func == 0) {
					compare_func = sort.m_sortAscending ? 1 : -1;
				}
			}
		};
		const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes(), & vec_dim = m_metaObject->GetGenericDimensions();
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		/////////////////////////////////////////////////////////
		IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(sort.m_sortModel));
				wxASSERT(attribute);
				IMetaObjectAttribute::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			wxValueTableKeyRow* rowData = new wxValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				CMetaObjectAttributeDefault* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				IMetaObjectAttribute::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
				CMetaObjectAttributeDefault* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				IMetaObjectAttribute::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
			}
			else {
				for (auto& dimension : vec_dim) {
					IMetaObjectAttribute::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
				}
			}
			for (auto& attribute : vec_attr) {
				IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			IValueTable::Append(rowData, !CBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) IValueTable::ClearRange(0, 1, !CBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void CTreeDataObjectFolderRef::RefreshModel(const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValue isFolder, parent;
	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;
	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& operation = filter.m_filterComparison == eComparisonType::eComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
			else {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", guidName, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
	}
	const std::vector<IMetaObjectAttribute*>& vec_attr = m_metaObject->GetGenericAttributes();
	CMetaObjectAttributeDefault* metaReference = m_metaObject->GetDataReference();
	CMetaObjectAttributeDefault* metaParent = m_metaObject->GetDataParent();
	CMetaObjectAttributeDefault* metaIsFolder = m_metaObject->GetDataIsFolder();
	wxASSERT(metaReference);
	queryText = queryText + whereText;
	IValueTree::Clear();
	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			IMetaObjectAttribute* attribute = dynamic_cast<IMetaObjectAttribute*>(m_metaObject->FindMetaObjectByID(filter.m_filterModel));
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const CValue& filterValue = filter.m_filterValue; CReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				IMetaObjectAttribute::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	//////////////////////////////////////////////////
	std::vector<wxValueTreeListNode*> arrTree;
	//////////////////////////////////////////////////

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	while (resultSet->Next()) {

		if (!IMetaObjectAttribute::GetValueAttribute(metaIsFolder, isFolder, resultSet)) continue;
		if (m_listMode == CTreeDataObjectFolderRef::LIST_FOLDER && !isFolder.GetBoolean()) continue;
		if (!IMetaObjectAttribute::GetValueAttribute(metaParent, parent, resultSet)) continue;

		wxValueTreeListNode* rowData = new wxValueTreeListNode(nullptr, resultSet->GetResultString(guidName), this, isFolder.GetBoolean());
		for (auto& attribute : vec_attr) {
			if (m_metaObject->IsDataReference(attribute->GetMetaID()))
				continue;
			IMetaObjectAttribute::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		rowData->AppendTableValue(metaReference->GetMetaID(), CReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
		//////////////////////////////////////////////////
		arrTree.push_back(rowData);
		//////////////////////////////////////////////////
	};

	/* wxDataViewModel::*/ BeforeReset();
	static CValue cReference;
	for (auto& node : arrTree) {
		CReferenceDataObject* reference = NULL;
		if (node->GetValue(*m_metaObject->GetDataParent(), cReference)) {
			if (cReference.ConvertToValue(reference)) {
				auto it = std::find_if(arrTree.begin(), arrTree.end(), [reference](wxValueTreeListNode* node) {
					return reference->GetGuid() == node->GetGuid();}
				);
				if (it != arrTree.end()) node->SetParent(*it);
				else node->SetParent(GetRoot());
			}
		}
	}

	/* wxDataViewModel:: */ AfterReset();

	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////