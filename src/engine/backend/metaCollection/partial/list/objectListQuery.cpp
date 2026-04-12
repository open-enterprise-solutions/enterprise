////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : list db 
////////////////////////////////////////////////////////////////////////////

#include "objectList.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"

void ibValueListDataObjectEnumRef::RefreshModel(const ibDataViewItem& topItem, const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = GetMetaObject()->GetTableNameDB();

	wxString queryText = wxT("");

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = wxString::Format("SELECT * FROM %s", tableName);
	else
		queryText = wxString::Format("SELECT FIRST " + stringUtils::IntToStr(countPerPage + 1) + " * FROM %s", tableName);

	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
				for (const auto object : m_metaObject->GetEnumObjectArray()) {
					orderText += "	WHEN uuid = '" + object->GetGuid().str() + "' THEN " + stringUtils::IntToStr(GetMetaObject()->FindEnumObjectByFilter(object->GetGuid())->GetParentPosition()) + '\n';
				}
				orderText += "	END " + operation_sort + " \n";
				if (firstOrder) firstOrder = false;
			}
		}
	};

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	else
		queryText = queryText + whereText + orderText;

	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	ibValueMetaObjectAttributePredefined* metaOrder = m_metaObject->GetDataOrder();
	ibValueModelTableBase::Clear();
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return;
	int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	/////////////////////////////////////////////////////////
	ibValueModelTableBase::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	/////////////////////////////////////////////////////////
	if (resultSet != nullptr) {
		while (resultSet->Next()) {
			ibGuid enumRow = resultSet->GetResultString(guidName);
			ibValueTableEnumRow* rowData = new ibValueTableEnumRow(enumRow);
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumObjectByFilter(enumRow)->GetParentPosition());
			ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		};
		db_query->CloseResultSet(resultSet);
	}
	db_query->CloseStatement(statement);
}

void ibValueListDataObjectEnumRef::RefreshItemModel(const ibDataViewItem& topItem, const ibDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		ibValueTableEnumRow* valueTableListRow = GetViewData<ibValueTableEnumRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
					for (const auto object : m_metaObject->GetEnumObjectArray()) {
						const wxString& str_guid = object->GetGuid().str();
						orderText += " WHEN uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						whereText += " WHEN uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						if (object->GetGuid() == valueTableListRow->GetGuid()) current_pos = pos_in_parent;
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

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + orderText;

		ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
		ibValueMetaObjectAttributePredefined* metaOrder = m_metaObject->GetDataOrder();
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableEnumRow* rowData = new ibValueTableEnumRow(resultSet->GetResultString(guidName));
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumObjectByFilter(rowData->GetGuid())->GetParentPosition());
			ibValueModelTableBase::Insert(rowData, 0, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(ibValueModelTableBase::GetRowCount() - 1, ibValueModelTableBase::GetRowCount(), !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		ibValueTableEnumRow* valueTableListRow = GetViewData<ibValueTableEnumRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
					for (const auto object : m_metaObject->GetEnumObjectArray()) {
						const wxString& str_guid = object->GetGuid().str();
						orderText += " WHEN uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						whereText += " WHEN uuid = '" + str_guid + "' THEN " + stringUtils::IntToStr(pos_in_parent) + '\n';
						if (object->GetGuid() == valueTableListRow->GetGuid()) current_pos = pos_in_parent;
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

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + orderText;

		ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
		ibValueMetaObjectAttributePredefined* metaOrder = m_metaObject->GetDataOrder();
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableEnumRow* rowData = new ibValueTableEnumRow(resultSet->GetResultString(guidName));
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			rowData->AppendTableValue(metaOrder->GetMetaID(), GetMetaObject()->FindEnumObjectByFilter(rowData->GetGuid())->GetParentPosition());
			ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(0, 1, !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListDataObjectRef::RefreshModel(const ibDataViewItem& topItem, const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();

	wxString queryText;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = wxString::Format("SELECT * FROM %s ", tableName);
	else
		queryText = wxString::Format("SELECT FIRST " + stringUtils::IntToStr(countPerPage + 1) + " * FROM %s ", tableName);

	wxString whereText; bool firstWhere = true;
	for (auto& filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
				if (firstOrder)
					orderText += " ORDER BY ";
				for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
					if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
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

	const std::vector<ibValueMetaObjectAttributeBase*>& vec_attr = m_metaObject->GetGenericAttributeArrayObject();

	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	else
		queryText = queryText + whereText + orderText;

	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	/////////////////////////////////////////////////////////
	ibValueModelTableBase::Clear();
	/////////////////////////////////////////////////////////
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return;
	int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	/////////////////////////////////////////////////////////
	ibValueModelTableBase::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet != nullptr) {
		while (resultSet->Next()) {
			ibValueTableListRow* rowData = new ibValueTableListRow(resultSet->GetResultString(guidName));
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
		};
		db_query->CloseResultSet(resultSet);
	}
	db_query->CloseStatement(statement);
}

void ibValueListDataObjectRef::RefreshItemModel(const ibDataViewItem& topItem, const ibDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		ibValueTableListRow* valueTableListRow = GetViewData<ibValueTableListRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder)
						orderText += " ORDER BY ";
					for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
						if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
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
						whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation_compare);
						sortText += (sortText.IsEmpty() ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, '=');
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
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
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
		const std::vector<ibValueMetaObjectAttributeBase*>& vec_attr = m_metaObject->GetGenericAttributeArrayObject();

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + orderText;

		ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableListRow* rowData = new ibValueTableListRow(resultSet->GetResultString(guidName));
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			ibValueModelTableBase::Insert(rowData, 0, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(ibValueModelTableBase::GetRowCount() - 1, ibValueModelTableBase::GetRowCount(), !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		ibValueTableListRow* valueTableListRow = GetViewData<ibValueTableListRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
					if (firstWhere)
						whereText += " WHERE ";
					whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				if (!m_metaObject->IsDataReference(sort.m_sortModel)) {
					if (firstOrder) orderText += " ORDER BY ";
					for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
						if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
							if (firstOrder) firstOrder = false;
						}
						else {
							orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
							if (firstOrder) firstOrder = false;
						}
						if (firstWhere) whereText += " WHERE ";
						whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation_compare);
						sortText += (sortText.IsEmpty() ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, '=');
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
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
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
		const std::vector<ibValueMetaObjectAttributeBase*>& vec_attr = m_metaObject->GetGenericAttributeArrayObject();

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + orderText;

		ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (m_metaObject->IsDataReference(filter.m_filterModel)) {
					const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
					if (filterValue.ConvertToValue(refData))
						statement->SetParamString(position++, refData->GetGuid());
					else
						statement->SetParamString(position++, wxNullUniqueKey);
				}
				else {
					ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
				}
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable && !m_metaObject->IsDataReference(sort.m_sortModel)) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableListRow* rowData = new ibValueTableListRow(resultSet->GetResultString(guidName));
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));
			ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(0, 1, !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueListRegisterObject::RefreshModel(const ibDataViewItem& topItem, const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	const wxString& tableName = m_metaObject->GetTableNameDB();

	wxString queryText;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = "SELECT * FROM " + tableName;
	else
		queryText = "SELECT FIRST " + stringUtils::IntToStr(countPerPage + 1) + " * FROM " + tableName;

	wxString whereText; bool firstWhere = true;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			if (firstWhere)
				whereText += " WHERE ";
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>");
			if (firstWhere)
				firstWhere = false;
		}
	}
	const std::vector<ibValueMetaObjectAttributeBase*>& vec_attr = m_metaObject->GetGenericAttributeArrayObject(), & vec_dim = m_metaObject->GetGenericDimentionArrayObject();
	/////////////////////////////////////////////////////////
	wxString orderText; bool firstOrder = true;
	/////////////////////////////////////////////////////////
	for (auto& sort : m_sortOrder.m_sorts) {
		if (sort.m_sortEnable) {
			const wxString& operation = sort.m_sortAscending ? "ASC" : "DESC";
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
			wxASSERT(attribute);
			const ibTypeDescription& typeDesc = attribute->GetTypeDesc();
			if (firstOrder)
				orderText += " ORDER BY ";
			for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
				if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
					orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation);
					if (firstOrder) firstOrder = false;
				}
				else if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_String) {
					orderText += (firstOrder ? " " : ", ") + wxString::Format("LPAD(%s, %i, ' ') %s", field.m_field.m_fieldName, typeDesc.GetStringQualifier().m_length, operation);
					if (firstOrder) firstOrder = false;
				}
				else {
					orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation);
					if (firstOrder) firstOrder = false;
				}
			}
		}
	};
	/////////////////////////////////////////////////////////
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		queryText = queryText + whereText + orderText + " LIMIT " + stringUtils::IntToStr(countPerPage + 1);
	else
		queryText = queryText + whereText + orderText;

	ibValueModelTableBase::Clear();
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return;
	int position = 1;
	for (auto& filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
		}
	}
	/////////////////////////////////////////////////////////
	ibValueModelTableBase::Reserve(countPerPage + 1);
	/////////////////////////////////////////////////////////
	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet != nullptr) {
		while (resultSet->Next()) {
			ibValueTableKeyRow* rowData = new ibValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
				ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
			}
			else {
				for (auto& dimension : vec_dim) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
				}
			}
			for (auto& attribute : vec_attr) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			ibValueModelTableBase::Append(
				rowData, !ibBackendException::IsEvalMode()
			);
		};
		db_query->CloseResultSet(resultSet);
	}
	db_query->CloseStatement(statement);
}

void ibValueListRegisterObject::RefreshItemModel(const ibDataViewItem& topItem, const ibDataViewItem& currentItem, const int countPerPage, const short scroll)
{
	const long row_top = GetRow(topItem);
	const long row_current = GetRow(currentItem);

	if (row_top == 0 && countPerPage < GetRowCount() && scroll > 0) {

		ibValueTableKeyRow* valueTableListRow = GetViewData<ibValueTableKeyRow>(GetItem(0));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
		wxString orderText; bool firstOrder = true; bool firstOr = true; wxString dimText;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = (!sort.m_sortAscending) ? "ASC" : "DESC";
				const wxString& operation_compare = (!sort.m_sortAscending) ? ">=" : "<=";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				const ibTypeDescription& typeDesc = attribute->GetTypeDesc();
				if (firstOrder)
					orderText += " ORDER BY ";
				for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
					if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
						if (firstOrder)
							firstOrder = false;
					}
					else if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_String) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("LPAD(%s, %i, ' ') %s", field.m_field.m_fieldName, typeDesc.GetStringQualifier().m_length, operation_sort);
						if (firstOrder) firstOrder = false;
					}
					else {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
						if (firstOrder) firstOrder = false;
					}
				}
				dimText += firstOr ? " (" + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, "<>") + ") " : " OR (" + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, "<>") + ") ";
				if (firstWhere) whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + attribute->GetFieldNameDB() + "_TYPE = ? ";
				if (firstWhere) firstWhere = false;
				for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
					if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldRefName.m_fieldRefType, operation_compare);
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldRefName.m_fieldRefName, operation_compare);
					}
					else if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_String) {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("LPAD(%s, %i, ' ') %s LPAD(cast(? as varchar(1024)), %i, ' ')", field.m_field.m_fieldName, typeDesc.GetStringQualifier().m_length, operation_compare, typeDesc.GetStringQualifier().m_length);
					}
					else {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldName, operation_compare);
					}
				}
				firstOr = false;
			}
		};

		const std::vector<ibValueMetaObjectAttributeBase*> vec_attr = m_metaObject->GetGenericAttributeArrayObject(),
			vec_dim = m_metaObject->GetGenericDimentionArrayObject();

		/////////////////////////////////////////////////////////
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + " AND (" + dimText + ") " + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + " AND (" + dimText + ") " + orderText;
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableKeyRow* rowData = new ibValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
				ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
			}
			else {
				for (auto& dimension : vec_dim) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
				}
			}
			for (auto& attribute : vec_attr) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			ibValueModelTableBase::Insert(rowData, 0, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(ibValueModelTableBase::GetRowCount() - 1, ibValueModelTableBase::GetRowCount(), !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
	else if (row_top + countPerPage == GetRowCount() && scroll < 0) {

		ibValueTableKeyRow* valueTableListRow = GetViewData<ibValueTableKeyRow>(GetItem(GetRowCount() - 1));
		const wxString& tableName = m_metaObject->GetTableNameDB();

		wxString queryText;
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = wxString::Format("SELECT * FROM %s ", tableName);
		else
			queryText = wxString::Format("SELECT FIRST 1 * FROM %s ", tableName);

		wxString whereText; bool firstWhere = true;
		for (auto& filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
				if (firstWhere)
					firstWhere = false;
			}
		}
		wxString orderText; bool firstOrder = true; bool firstOr = true; wxString dimText;
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const wxString& operation_sort = (sort.m_sortAscending) ? "ASC" : "DESC";
				const wxString& operation_compare = (sort.m_sortAscending) ? ">=" : "<=";
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				const ibTypeDescription& typeDesc = attribute->GetTypeDesc();
				if (firstOrder)
					orderText += " ORDER BY ";
				for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
					if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldRefName.m_fieldRefName, operation_sort);
						if (firstOrder)
							firstOrder = false;
					}
					else if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_String) {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("LPAD(%s, %i, ' ') %s", field.m_field.m_fieldName, typeDesc.GetStringQualifier().m_length, operation_sort);
						if (firstOrder) firstOrder = false;
					}
					else {
						orderText += (firstOrder ? " " : ", ") + wxString::Format("%s %s", field.m_field.m_fieldName, operation_sort);
						if (firstOrder) firstOrder = false;
					}
				}
				dimText += firstOr ? " (" + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, "<>") + ") " : " OR (" + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, "<>") + ") ";
				if (firstWhere) whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + attribute->GetFieldNameDB() + "_TYPE = ? ";
				if (firstWhere) firstWhere = false;
				for (auto& field : ibValueMetaObjectAttributeBase::GetSQLFieldData(attribute)) {
					if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_Reference) {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldRefName.m_fieldRefType, operation_compare);
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldRefName.m_fieldRefName, operation_compare);
					}
					else if (field.m_type == ibValueMetaObjectAttributeBase::ibFieldTypes_String) {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("LPAD(%s, %i, ' ') %s LPAD(cast(? as varchar(1024)), %i, ' ')", field.m_field.m_fieldName, typeDesc.GetStringQualifier().m_length, operation_compare, typeDesc.GetStringQualifier().m_length);
					}
					else {
						whereText += (firstWhere ? " " : " AND ") + wxString::Format("%s %s ?", field.m_field.m_fieldName, operation_compare);
					}
				}
				firstOr = false;
			}
		};

		const std::vector<ibValueMetaObjectAttributeBase*> vec_attr = m_metaObject->GetGenericAttributeArrayObject(),
			vec_dim = m_metaObject->GetGenericDimentionArrayObject();

		/////////////////////////////////////////////////////////
		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
			queryText = queryText + whereText + " AND (" + dimText + ") " + orderText + " LIMIT " + stringUtils::IntToStr(1);
		else
			queryText = queryText + whereText + " AND (" + dimText + ") " + orderText;
		/////////////////////////////////////////////////////////
		ibPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;
		for (auto filter : m_filterRow.m_filters) {
			if (filter.m_filterUse) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		for (auto& sort : m_sortOrder.m_sorts) {
			if (sort.m_sortEnable) {
				const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(sort.m_sortModel);
				wxASSERT(attribute);
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, valueTableListRow->GetTableValue(sort.m_sortModel), statement, position);
			}
		}
		/////////////////////////////////////////////////////////
		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
		/////////////////////////////////////////////////////////
		bool insertedValue = false;
		/////////////////////////////////////////////////////////
		while (resultSet->Next()) {
			ibValueTableKeyRow* rowData = new ibValueTableKeyRow;
			if (m_metaObject->HasRecorder()) {
				ibValueMetaObjectAttributePredefined* attributeRecorder = m_metaObject->GetRegisterRecorder();
				wxASSERT(attributeRecorder);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeRecorder, rowData->AppendNodeValue(attributeRecorder->GetMetaID()), resultSet);
				ibValueMetaObjectAttributePredefined* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
				wxASSERT(attributeNumberLine);
				ibValueMetaObjectAttributeBase::GetValueAttribute(attributeNumberLine, rowData->AppendNodeValue(attributeNumberLine->GetMetaID()), resultSet);
			}
			else {
				for (auto& dimension : vec_dim) {
					ibValueMetaObjectAttributeBase::GetValueAttribute(dimension, rowData->AppendNodeValue(dimension->GetMetaID()), resultSet);
				}
			}
			for (auto& attribute : vec_attr) {
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			ibValueModelTableBase::Append(rowData, !ibBackendException::IsEvalMode());
			/////////////////////////////////////////////////////////
			insertedValue = true;
			/////////////////////////////////////////////////////////
		};
		/////////////////////////////////////////////////////////
		if (insertedValue) ibValueModelTableBase::ClearRange(0, 1, !ibBackendException::IsEvalMode());
		/////////////////////////////////////////////////////////
		db_query->CloseResultSet(resultSet);
		db_query->CloseStatement(statement);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////

void ibValueModelTreeDataObjectFolderRef::RefreshModel(const ibDataViewItem& topItem, const int countPerPage)
{
	if (db_query != nullptr && !db_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (db_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValue isFolder, parent;
	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName;
	wxString whereText; bool firstWhere = true;
	for (auto& filter : m_filterRow.m_filters) {
		const wxString& operation = filter.m_filterComparison == ibComparisonType::ibComparisonType_Equal ? "=" : "<>";
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (!m_metaObject->IsDataReference(filter.m_filterModel)) {
				if (firstWhere)
					whereText += " WHERE ";
				whereText += (firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(attribute, operation);
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
	const std::vector<ibValueMetaObjectAttributeBase*>& vec_attr = m_metaObject->GetGenericAttributeArrayObject();
	ibValueMetaObjectAttributePredefined* metaReference = m_metaObject->GetDataReference();
	ibValueMetaObjectAttributePredefined* metaParent = m_metaObject->GetDataParent();
	ibValueMetaObjectAttributePredefined* metaIsFolder = m_metaObject->GetDataIsFolder();
	wxASSERT(metaReference);
	queryText = queryText + whereText;
	ibValueModelTreeBase::Clear();
	ibPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return;
	int position = 1;
	for (auto filter : m_filterRow.m_filters) {
		if (filter.m_filterUse) {
			const ibValueMetaObjectAttributeBase* attribute = m_metaObject->FindAnyAttributeObjectByFilter(filter.m_filterModel);
			wxASSERT(attribute);
			if (m_metaObject->IsDataReference(filter.m_filterModel)) {
				const ibValue& filterValue = filter.m_filterValue; ibValueReferenceDataObject* refData = nullptr;
				if (filterValue.ConvertToValue(refData))
					statement->SetParamString(position++, refData->GetGuid());
				else
					statement->SetParamString(position++, wxNullUniqueKey);
			}
			else {
				ibValueMetaObjectAttributeBase::SetValueAttribute(attribute, filter.m_filterValue, statement, position);
			}
		}
	}
	//////////////////////////////////////////////////
	std::vector<ibValueTreeListNode*> arrTree;
	//////////////////////////////////////////////////

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet != nullptr) {
		while (resultSet->Next()) {

			if (!ibValueMetaObjectAttributeBase::GetValueAttribute(metaIsFolder, isFolder, resultSet)) continue;
			if (m_listMode == ibValueModelTreeDataObjectFolderRef::LIST_FOLDER && !isFolder.GetBoolean()) continue;
			if (!ibValueMetaObjectAttributeBase::GetValueAttribute(metaParent, parent, resultSet)) continue;

			ibValueTreeListNode* rowData = new ibValueTreeListNode(nullptr, resultSet->GetResultString(guidName), this, isFolder.GetBoolean());
			for (auto& attribute : vec_attr) {
				if (m_metaObject->IsDataReference(attribute->GetMetaID()))
					continue;
				ibValueMetaObjectAttributeBase::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
			}
			rowData->AppendTableValue(metaReference->GetMetaID(), ibValueReferenceDataObject::CreateFromResultSet(resultSet, m_metaObject, rowData->GetGuid()));

			//////////////////////////////////////////////////
			arrTree.push_back(rowData);
			//////////////////////////////////////////////////
		};
		db_query->CloseResultSet(resultSet);
	}

	/* wxDataViewModel::*/ m_modelProvider->BeforeReset();

	static ibValue cReference;
	for (const auto node : arrTree) {

		ibValueReferenceDataObject* reference = NULL;
		if (node->GetValue(*m_metaObject->GetDataParent(), cReference)) {
			if (cReference.ConvertToValue(reference)) {

				auto iterator = std::find_if(arrTree.begin(), arrTree.end(),
					[reference](ibValueTreeListNode* node) { return reference->GetGuid() == node->GetGuid(); });

				if (iterator != arrTree.end())
					node->SetParent(*iterator);
				else
					node->SetParent(GetRoot());
			}
		}
	}

	/* wxDataViewModel:: */ m_modelProvider->AfterReset();

	db_query->CloseStatement(statement);
}

/////////////////////////////////////////////////////////////////////////////////////////////////