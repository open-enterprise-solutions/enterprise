#include "informationRegister.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "appData.h"

bool CMetaObjectInformationRegister::CreateAndUpdateSliceFirstTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString viewName = GetTableNameDB() + wxT("SF");

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaAttributeObject::GetSQLFieldName(m_attributePeriod);
		for (auto dimension : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}

		wxString sqlQuery = "CREATE VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_attributePeriod, "MIN");
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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

		retCode = databaseLayer->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & updateMetaTable) != 0) {

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaAttributeObject::GetSQLFieldName(m_attributePeriod);
		for (auto dimension : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}

		wxString sqlQuery = "CREATE OR ALTER VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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

		retCode = databaseLayer->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP VIEW %s", viewName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

bool CMetaObjectInformationRegister::CreateAndUpdateSliceLastTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();
	wxString viewName = GetTableNameDB() + wxT("SL");

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaAttributeObject::GetSQLFieldName(m_attributePeriod);
		for (auto dimension : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}

		wxString sqlQuery = "CREATE VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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

		retCode = databaseLayer->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & updateMetaTable) != 0) {

		IMetaAttributeObject::sqlField_t sqlCol =
			IMetaAttributeObject::GetSQLFieldData(m_attributePeriod);

		wxString sqlViewColumn =
			IMetaAttributeObject::GetSQLFieldName(m_attributePeriod);
		for (auto dimension : GetObjectDimensions()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlViewColumn += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}

		wxString sqlQuery = "CREATE OR ALTER VIEW %s (" + sqlViewColumn + ") AS";
		sqlQuery += " SELECT T2." + sqlCol.m_fieldTypeName;

		for (auto dataType : sqlCol.m_types) {
			if (dataType.m_type != IMetaAttributeObject::eFieldTypes::eFieldTypes_Reference) {
				sqlQuery += ", T2." + dataType.m_field.m_fieldName;
			}
			else {
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefType;
				sqlQuery += ", T2." + dataType.m_field.m_fieldRefName.m_fieldRefName;
			}
		}

		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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
		sqlQuery += IMetaAttributeObject::GetSQLFieldName(m_attributePeriod, "MAX");
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += " FROM " + tableName;
		sqlQuery += " GROUP BY ";
		sqlQuery += sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(dimension);
		}
		for (auto resource : GetObjectResources()) {
			sqlQuery += "," + IMetaAttributeObject::GetSQLFieldName(resource);
		}
		sqlQuery += ") AS T1 "
			"INNER JOIN " + tableName + " AS T2 "
			" ON ";
		sqlQuery += " T1." + sqlCol.m_fieldTypeName + " = T2." + sqlCol.m_fieldTypeName;
		for (auto dimension : GetObjectDimensions()) {
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

		for (auto resource : GetObjectResources()) {
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

		retCode = databaseLayer->RunQuery(sqlQuery, viewName, tableName);
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP VIEW %s", viewName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

bool CMetaObjectInformationRegister::CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{

	if (!IMetaObjectRegisterData::CreateAndUpdateTableDB(srcMetaData, srcMetaObject, flags))
		return false;

	//if (!CMetaObjectInformationRegister::CreateAndUpdateSliceFirstTableDB(srcMetaData, srcMetaObject, flags))
	//	return false;

	//if (!CMetaObjectInformationRegister::CreateAndUpdateSliceLastTableDB(srcMetaData, srcMetaObject, flags))
	//	return false;

	return true;
}