////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : informationRegister manager
////////////////////////////////////////////////////////////////////////////

#include "informationRegister.h"
#include "informationRegisterManager.h"

#include "core/compiler/valueMap.h"
#include "core/compiler/valueTable.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "appData.h"

CValue CInformationRegisterManager::Get(const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo = colCollection->AddColumn(attribute->GetName(), attribute->GetTypeDescription(), attribute->GetSynonym());
		colInfo->SetColumnID(attribute->GetMetaID());
	}

	CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
	if (cFilter.ConvertToValue(valFilter)) {
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			CValue vSelValue;
			if (valFilter->Property(dimension->GetName(), vSelValue)) {
				selFilter.insert_or_assign(
					dimension, vSelValue
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
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

	if (statement == NULL)
		return retTable;

	int position = 1;
	for (auto filter : selFilter) {
		IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();

	if (resultSet == NULL)
		return retTable;

	while (resultSet->Next()) {
		CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
		wxASSERT(retLine);
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			CValue retValue;
			if (IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
				retLine->SetValueByMetaID(attribute->GetMetaID(), retValue);
		}
		delete retLine;
	}

	resultSet->Close();
	statement->Close();

	return retTable;
}

CValue CInformationRegisterManager::Get(const CValue& cPeriod, const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				attribute->GetName(),
				attribute->GetTypeDescription(),
				attribute->GetSynonym()
			);
		colInfo->SetColumnID(attribute->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		wxString queryText = "SELECT * FROM %s WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod());

		for (auto filter : selFilter) {
			queryText = queryText +
				" AND " + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
		}

		PreparedStatement* statement = databaseLayer->PrepareStatement(queryText, m_metaObject->GetTableNameDB());

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
					retLine->SetValueByMetaID(attribute->GetMetaID(), retValue);
			}
			delete retLine;
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::GetFirst(const CValue& cPeriod, const CValue& cFilter)
{
	CValueStructure* retTable = new CValueStructure();
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		retTable->SetAt(attribute->GetName(), CValue());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		if (resultSet->Next()) {
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				CValue retValue; 
				if(IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
					retTable->SetAt(attribute->GetName(), retValue);
			}
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::GetLast(const CValue& cPeriod, const CValue& cFilter)
{
	CValueStructure* retTable = new CValueStructure();
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		retTable->SetAt(attribute->GetName(), CValue());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		if (resultSet->Next()) {
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
					retTable->SetAt(attribute->GetName(), retValue);
			}
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::SliceFirst(const CValue& cPeriod, const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				attribute->GetName(),
				attribute->GetTypeDescription(),
				attribute->GetSynonym()
			);
		colInfo->SetColumnID(attribute->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MIN");
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), ">=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
					retLine->SetValueByMetaID(attribute->GetMetaID(), retValue);
			}
			delete retLine;
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}

CValue CInformationRegisterManager::SliceLast(const CValue& cPeriod, const CValue& cFilter)
{
	CValueTable* retTable = new CValueTable();
	CValueTable::IValueModelColumnCollection* colCollection = retTable->GetColumnCollection();
	wxASSERT(colCollection);
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		CValueTable::IValueModelColumnCollection::IValueModelColumnInfo* colInfo =
			colCollection->AddColumn(
				attribute->GetName(),
				attribute->GetTypeDescription(),
				attribute->GetSynonym()
			);
		colInfo->SetColumnID(attribute->GetMetaID());
	}

	if (m_metaObject->GetPeriodicity() != ePeriodicity::eNonPeriodic ||
		m_metaObject->GetWriteRegisterMode() == eWriteRegisterMode::eSubordinateRecorder) {
		CValueStructure* valFilter = NULL; std::map<IMetaAttributeObject*, CValue> selFilter;
		if (cFilter.ConvertToValue(valFilter)) {
			for (auto dimension : m_metaObject->GetObjectDimensions()) {
				CValue vSelValue;
				if (valFilter->Property(dimension->GetName(), vSelValue)) {
					selFilter.insert_or_assign(
						dimension, vSelValue
					);
				}
			}
		}

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_metaObject->GetRegisterPeriod());

		wxString sqlQuery = " SELECT * FROM ( SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += ", T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " , T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += ", T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		sqlQuery += " FROM (SELECT ";
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_metaObject->GetRegisterPeriod(), "MAX");
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + m_metaObject->GetTableNameDB();
		sqlQuery += " WHERE " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterActive());
		sqlQuery += " AND " + IMetaAttributeObject::GetCompositeSQLFieldName(m_metaObject->GetRegisterPeriod(), "<=");
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : m_metaObject->GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + m_metaObject->GetTableNameDB() + " AS T2 "
			" ON ";

		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : m_metaObject->GetObjectDimensions()) {
			IMetaAttributeObject::sqlField_t sqlDim = IMetaAttributeObject::GetSQLFieldData(dimension);
			sqlQuery += " AND T1." + sqlDim.m_fieldTypeName + " = T2." + sqlDim.m_fieldTypeName;
			for (auto dataType : sqlDim.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldName + " = T2." + dataType.m_field.m_fieldName;
				}
				else {
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefType + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
					sqlQuery += " AND T1." + dataType.m_field.m_fieldRefName.m_fieldRefName + " = T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
				}
			}
		}

		for (auto resource : m_metaObject->GetObjectResources()) {
			IMetaAttributeObject::sqlField_t sqlRes = IMetaAttributeObject::GetSQLFieldData(resource);
			sqlQuery += " AND T1." + sqlRes.m_fieldTypeName + " = T2." + sqlRes.m_fieldTypeName;
			for (auto dataType : sqlRes.m_types) {
				if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
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
				(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(filter.first);
			if (firstWhere) {
				firstWhere = false;
			}
		}

		PreparedStatement* statement = databaseLayer->PrepareStatement(sqlQuery);

		if (statement == NULL)
			return retTable;

		int position = 1;

		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterActive(), true, statement, position);
		IMetaAttributeObject::SetValueAttribute(m_metaObject->GetRegisterPeriod(), cPeriod.GetDate(), statement, position);

		for (auto filter : selFilter) {
			IMetaAttributeObject::SetValueAttribute(filter.first, filter.second, statement, position);
		}

		DatabaseResultSet* resultSet = statement->RunQueryWithResults();

		if (resultSet == NULL)
			return retTable;

		while (resultSet->Next()) {
			CValueTable::CValueTableReturnLine* retLine = retTable->GetRowAt(retTable->AppendRow());
			wxASSERT(retLine);
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				CValue retValue;
				if (IMetaAttributeObject::GetValueAttribute(attribute, retValue, resultSet))
					retTable->SetAt(attribute->GetName(), retValue);
			}
			delete retLine;
		}

		resultSet->Close();
		statement->Close();
	}

	return retTable;
}
