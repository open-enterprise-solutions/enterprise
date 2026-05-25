////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo = colCollection->AddColumn(object->GetName(), object->GetTypeDesc(), object->GetSynonym());
		colInfo->SetColumnID(object->GetMetaID());
	}

	ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValue vSelValue;
			if (valFilter->Property(object->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					object, vSelValue
				);
			}
		}
	}

	wxString queryText = "SELECT * FROM %s "; bool firstWhere = true;

	for (auto filter : selFilter) {
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	ibPreparedStatement* statement = ses_query->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

	if (statement == nullptr)
		return retTable;

	int position = 1;
	for (auto filter : selFilter) {
		ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			ibValue retValue;
			if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
				retLine->SetValueByMetaID(object->GetMetaID(), retValue);
		}
		wxDELETE(retLine);
	}

	resultSet->Close();
	statement->Close();

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::Get(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		wxString queryText = "SELECT * FROM %s WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod());

		for (auto filter : selFilter) {
			queryText = queryText +
				" AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
		}

		ibPreparedStatement* statement = ses_query->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				ibValue retValue;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
					retLine->SetValueByMetaID(object->GetMetaID(), retValue);
			}
			wxDELETE(retLine);
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::GetFirst(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueStructure* retTable = ibValue::CreateAndPrepareValueRef<ibValueStructure>();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		retTable->SetAt(object->GetName(), ibValue());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		ibValueMetaObjectAttributeBase::ibSQLField sqlCol =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += ") AS lastTable"; bool firstWhere = true;

		for (auto filter : selFilter) {
			if (firstWhere) {
				sqlQuery = sqlQuery + " WHERE ";
			}
			sqlQuery = sqlQuery +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		if (resultSet->Next()) {
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				ibValue retValue; 
				if(ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
					retTable->SetAt(object->GetName(), retValue);
			}
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::GetLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueStructure* retTable = ibValue::CreateAndPrepareValueRef<ibValueStructure>();
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		retTable->SetAt(object->GetName(), ibValue());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		ibValueMetaObjectAttributeBase::ibSQLField sqlCol =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += ") AS lastTable"; bool firstWhere = true;

		for (auto filter : selFilter) {
			if (firstWhere) {
				sqlQuery = sqlQuery + " WHERE ";
			}
			sqlQuery = sqlQuery +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		if (resultSet->Next()) {
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				ibValue retValue;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
					retTable->SetAt(object->GetName(), retValue);
			}
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::SliceFirst(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		ibValueMetaObjectAttributeBase::ibSQLField sqlCol =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += ") AS lastTable"; bool firstWhere = true;

		for (auto filter : selFilter) {
			if (firstWhere) {
				sqlQuery = sqlQuery + " WHERE ";
			}
			sqlQuery = sqlQuery +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				ibValue retValue;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
					retLine->SetValueByMetaID(object->GetMetaID(), retValue);
			}
			wxDELETE(retLine);
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}

ibValue ibValueManagerDataObjectInformationRegister::SliceLast(const ibValue& cPeriod, const ibValue& cFilter)
{
	if (ses_query != nullptr && !ses_query->IsOpen())
		ibBackendCoreException::Error(_("Database is not open!"));
	else if (ses_query == nullptr)
		ibBackendCoreException::Error(_("Database is not open!"));

	ibValueModelTable* retTable = ibValue::CreateAndPrepareValueRef<ibValueModelTable>();
	ibValueModelTable::ibValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		ibValueModelTable::ibValueModelColumnCollection::ibValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				object->GetName(),
				object->GetTypeDesc(),
				object->GetSynonym()
			);
		colInfo->SetColumnID(object->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ibPeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == ibWriteRegisterMode::eSubordinateRecorder) {
		ibValueStructure* valFilter = nullptr; std::map<ibValueMetaObjectAttributeBase*, ibValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (const auto object : m_metaObject->GetDimensionArrayObject()) {
				ibValue vSelValue;
				if (valFilter->Property(object->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						object, vSelValue
					);
				}
			}
		}

		ibValueMetaObjectAttributeBase::ibSQLField sqlCol =
			ibValueMetaObjectAttributeBase::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += ibValueMetaObjectAttributeBase::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			sqlQuery += "," + ibValueMetaObjectAttributeBase::GetSQLFieldName(object);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (const auto object : m_metaObject->GetDimensionArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlDim = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (const auto object : m_metaObject->GetResourceArrayObject()) {
			ibValueMetaObjectAttributeBase::ibSQLField sqlRes = ibValueMetaObjectAttributeBase::GetSQLFieldData(object);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != ibValueMetaObjectAttributeBase::ibFieldTypes::ibFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += ") AS lastTable"; bool firstWhere = true;

		for (auto filter : selFilter) {
			if (firstWhere) {
				sqlQuery = sqlQuery + " WHERE ";
			}
			sqlQuery = sqlQuery +
				(firstWhere ? " " : " AND ") + ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		ibValueMetaObjectAttributeBase::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			ibValueMetaObjectAttributeBase::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			ibValueModelTable::ibValueModelTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
				ibValue retValue;
				if (ibValueMetaObjectAttributeBase::GetValueAttribute(object, retValue, resultSet))
					retTable->SetAt(object->GetName(), retValue);
			}
			wxDELETE(retLine);
		}

		ses_query->CloseResultSet(resultSet);
		ses_query->CloseStatement(statement);
	}

	return retTable;
}
