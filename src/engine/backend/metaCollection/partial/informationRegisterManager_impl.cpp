////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "backend/compiler/value/valueMap.h"
#include "backend/compiler/value/valueTable.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

CValue CInformationRegisterManager::Get(const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colCollection->AddColumn(obj->GetName(), obj->GetTypeDescription(), obj->GetSynonym());
		colInfo->SetColumnID(obj->GetMetaID());
	}

	CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			CValue vSelValue;
			if (valFilter->Property(obj->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					obj, vSelValue
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
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

	if (statement == nullptr)
		return retTable;

	int position = 1;
	for (auto filter : selFilter) {
		IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == nullptr)
		return retTable;

	while (resultSet->Next()) {
		CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (auto& obj : m_metaObject->GetGenericAttributes()) {
			CValue retValue;
			if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
				retLine->SetValueByMetaID(obj->GetMetaID(), retValue);
		}
		wxDELETE(retLine);
	}

	resultSet->Close();
	statement->Close();

	return retTable;
}

CValue CInformationRegisterManager::Get(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				obj->GetName(),
				obj->GetTypeDescription(),
				obj->GetSynonym()
			);
		colInfo->SetColumnID(obj->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		wxString queryText = "SELECT * FROM %s WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod());

		for (auto filter : selFilter) {
			queryText = queryText +
				" AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
		}

		IPreparedStatement* statement = db_query->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
					retLine->SetValueByMetaID(obj->GetMetaID(), retValue);
			}
			wxDELETE(retLine);
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::GetFirst(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueStructure* retTable = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		retTable->SetAt(obj->GetName(), CValue());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		if (resultSet->Next()) {
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				CValue retValue; 
				if(IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
					retTable->SetAt(obj->GetName(), retValue);
			}
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::GetLast(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueStructure* retTable = CValue::CreateAndConvertObjectValueRef<CValueStructure>();
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		retTable->SetAt(obj->GetName(), CValue());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		if (resultSet->Next()) {
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
					retTable->SetAt(obj->GetName(), retValue);
			}
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::SliceFirst(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				obj->GetName(),
				obj->GetTypeDescription(),
				obj->GetSynonym()
			);
		colInfo->SetColumnID(obj->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
					retLine->SetValueByMetaID(obj->GetMetaID(), retValue);
			}
			wxDELETE(retLine);
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::SliceLast(const CValue& cPeriod, const CValue& cFilter)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	CValueTable* retTable = CValue::CreateAndConvertObjectValueRef<CValueTable>();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				obj->GetName(),
				obj->GetTypeDescription(),
				obj->GetSynonym()
			);
		colInfo->SetColumnID(obj->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = nullptr; std::map<IMetaObjectAttribute*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto& obj : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(obj->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						obj, vSelValue
					);
				}
			}
		}

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaObjectAttribute::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : m_metaObject->GetObjectDimensions()) {
			IMetaObjectAttribute::sqlField_t sqlDim = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto& obj : m_metaObject->GetObjectResources()) {
			IMetaObjectAttribute::sqlField_t sqlRes = IMetaObjectAttribute::GetSQLFieldData(obj);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		IPreparedStatement* statement = db_query->PrepareStatement(sqlQuery);

		if (statement == nullptr)
			return retTable;

		int position = 1;

		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaObjectAttribute::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaObjectAttribute::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		IDatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == nullptr)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaObjectAttribute::GetValueAttribute(obj, retValue, resultSet))
					retTable->SetAt(obj->GetName(), retValue);
			}
			wxDELETE(retLine);
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}
