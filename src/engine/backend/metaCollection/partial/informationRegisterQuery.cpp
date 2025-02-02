#include "informationRegister.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/appData.h"

bool CMetaObjectInformationRegister::CreateAndUpdateSliceFirstTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString viewName = GetTableNameDB() + wxT("SF");

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod);
		for (auto& obj : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}

		wxString sqlQuery = "CREATE VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod, "MIN");
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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

		retCode = db_query->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & updateMetaTable) != 0) {

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod);
		for (auto& obj : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}

		wxString sqlQuery = "CREATE OR ALTER VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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

		retCode = db_query->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = db_query->RunQuery("DROP VIEW %s", viewName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

bool CMetaObjectInformationRegister::CreateAndUpdateSliceLastTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString viewName = GetTableNameDB() + wxT("SL");

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod);
		for (auto& obj : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}

		wxString sqlQuery = "CREATE VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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

		retCode = db_query->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & updateMetaTable) != 0) {

		IMetaObjectAttribute::sqlField_t sqlCol =
			IMetaObjectAttribute::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod);
		for (auto& obj : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlViewColumn += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}

		wxString sqlQuery = "CREATE OR ALTER VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaObjectAttribute::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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
		sqlQuery += IMetaObjectAttribute::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		for (auto& obj : GetObjectResources()) {
			sqlQuery += "," + IMetaObjectAttribute::GetSQLFieldName(obj);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto& obj : GetObjectDimensions()) {
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

		for (auto& obj : GetObjectResources()) {
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

		retCode = db_query->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = db_query->RunQuery("DROP VIEW %s", viewName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

bool CMetaObjectInformationRegister::CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	//if (!CMetaObjectInformationRegister::CreateAndUpdateSliceFirstTableDB(srcMetaData, srcMetaObject, flags))
	//	return false;

	//if (!CMetaObjectInformationRegister::CreateAndUpdateSliceLastTableDB(srcMetaData, srcMetaObject, flags))
	//	return false;

	return IMetaObjectRegisterData::CreateAndUpdateTableDB(
		srcMetaData,
		srcMetaObject,
		flags
	);
}