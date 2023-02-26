#include "metaAttributeObject.h"

#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include <3rdparty/databaseLayer/databaseErrorCodes.h>

#include "core/metadata/metadata.h"
#include "core/metadata/singleClass.h"

unsigned short IMetaAttributeObject::GetSQLFieldCount(IMetaAttributeObject* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); unsigned short sqlField = 1;

	if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_DATE)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_STRING)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_ENUM)) {
		sqlField += 1;
	}
	if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
		sqlField += 2;
	}

	return sqlField;
}

wxString IMetaAttributeObject::GetSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& aggr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_B";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		if (aggr.IsEmpty())
			sqlField += fieldName + "_N";
		else
			sqlField += aggr + "(" + fieldName + "_N" + ") AS " + fieldName + "_N";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		if (aggr.IsEmpty())
			sqlField += fieldName + "_D";
		else
			sqlField += aggr + "(" + fieldName + "_D" + ") AS " + fieldName + "_D";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_S";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_E";
	}
	if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_RTRef" + "," + fieldName + "_RRRef";
	}

	return fieldName + "_TYPE"
		+ (sqlField.Length() > 0 ? "," : "")
		+ sqlField;
}

wxString IMetaAttributeObject::GetCompositeSQLFieldName(IMetaAttributeObject* metaAttr, const wxString& oper)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_B " + oper + " ? ";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_N " + oper + " ? ";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_D " + oper + " ? ";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_S " + oper + " ? ";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_E " + oper + " ? ";
	}
	if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_RTRef = ? AND " + fieldName + "_RRRef " + oper + " ? ";
	}

	return fieldName + "_TYPE = ? "
		+ (sqlField.Length() > 0 ? " AND " : "")
		+ sqlField;
}

wxString IMetaAttributeObject::GetExcluteSQLFieldName(IMetaAttributeObject* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_B = excluded." + fieldName + "_B";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_N = excluded." + fieldName + "_N";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_D = excluded." + fieldName + "_D";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_S = excluded." + fieldName + "_S";
	}
	if (metaAttr->ContainType(eValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_E = excluded." + fieldName + "_E";
	}
	if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_RTRef = excluded." + fieldName + "_RTRef, " + fieldName + "_RRRef = excluded." + fieldName + "_RRRef";
	}

	return fieldName + "_TYPE = excluded." + fieldName + "_TYPE"
		+ (sqlField.Length() > 0 ? ", " : "")
		+ sqlField;
}

IMetaAttributeObject::sqlField_t IMetaAttributeObject::GetSQLFieldData(IMetaAttributeObject* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); sqlField_t sqlData(fieldName + "_TYPE");

	if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_Boolean,
			fieldName + "_B"
		);
	}

	if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_Number,
			fieldName + "_N"
		);
	}

	if (metaAttr->ContainType(eValueTypes::TYPE_DATE)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_Date,
			fieldName + "_D"
		);
	}

	if (metaAttr->ContainType(eValueTypes::TYPE_STRING)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_String,
			fieldName + "_S"
		);
	}

	if (metaAttr->ContainType(eValueTypes::TYPE_ENUM)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_Enum,
			fieldName + "_E"
		);
	}

	if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
		sqlData.AppendType(
			eFieldTypes::eFieldTypes_Reference,
			fieldName + "_RTRef",
			fieldName + "_RRRef"
		);
	}

	return sqlData;
}

int IMetaAttributeObject::ProcessAttribute(const wxString& tableName,
	IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	int retCode = 1;
	//is null - create
	if (dstAttr == NULL) {
		const wxString& fieldName = srcAttr->GetFieldNameDB(); bool createReference = false; IMetadata* metaData = srcAttr->GetMetadata();
		retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_TYPE %s DEFAULT 0 NOT NULL;", tableName, fieldName, "INTEGER");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		for (auto clsid : srcAttr->GetClsids()) {
			eValueTypes valType = CValue::GetVTByID(clsid);
			switch (valType) {
			case eValueTypes::TYPE_BOOLEAN:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_B %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case eValueTypes::TYPE_NUMBER:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_N %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case eValueTypes::TYPE_DATE:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case eValueTypes::TYPE_STRING:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_S %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case eValueTypes::TYPE_ENUM:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_E %s DEFAULT 0;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			default:
				IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
				wxASSERT(singleObject);
				if (singleObject != NULL && eMetaObjectType::enReference == singleObject->GetMetaType()) {
					createReference = true;
				}
			}
		}
		if (createReference) {
			retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
			retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL ? "BYTEA" : "BLOB");
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	// update 
	else if (srcAttr != NULL) {
		if (srcAttr->GetTypeDescription() != dstAttr->GetTypeDescription()) {
			const wxString& fieldName = srcAttr->GetFieldNameDB(); std::set<CLASS_ID> createdRef, currentRef, removedRef; IMetadata* metaData = srcAttr->GetMetadata();
			for (auto clsid : srcAttr->GetClsids()) {
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				if (!dstAttr->ContainType(clsid)) {
					eValueTypes valType = CValue::GetVTByID(clsid);
					switch (valType) {
					case eValueTypes::TYPE_BOOLEAN:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_B %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_NUMBER:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_N %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_DATE:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_STRING:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_S %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_ENUM:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_E %s DEFAULT 0;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					default:
						IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
						wxASSERT(singleObject);
						if (singleObject != NULL && eMetaObjectType::enReference == singleObject->GetMetaType()) {
							createdRef.insert(clsid);
						}
					}
				}
			}
			for (auto clsid : dstAttr->GetClsids()) {
				eValueTypes valType = CValue::GetVTByID(clsid);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				if (srcAttr->ContainType(clsid)) {
					switch (valType) {
					case eValueTypes::TYPE_BOOLEAN:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetTypeDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_B TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_NUMBER:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetTypeDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_N TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_DATE:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetTypeDescription())) {
							if (srcAttr->GetDateFraction() != eDateFractions::eDateFractions_Time && dstAttr->GetDateFraction() == eDateFractions::eDateFractions_Time) {
								retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
								if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
									return retCode;
								retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
							}
							else {
								retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_D TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
							}
						}
						break;
					case eValueTypes::TYPE_STRING:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetTypeDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_S TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_ENUM:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetTypeDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_E TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					default:
						IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
						wxASSERT(singleObject);
						if (singleObject != NULL && eMetaObjectType::enReference == singleObject->GetMetaType()) {
							currentRef.insert(clsid);
						}
					}
				}
				else {
					switch (valType) {
					case eValueTypes::TYPE_BOOLEAN:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_B;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_Boolean);
						break;
					case eValueTypes::TYPE_NUMBER:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_N;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_Number);
						break;
					case eValueTypes::TYPE_DATE:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_Date);
						break;
					case eValueTypes::TYPE_STRING:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_S;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_String);
						break;
					case eValueTypes::TYPE_ENUM:
						retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_E;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_Enum);
						break;
					default:
						IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
						wxASSERT(singleObject);
						if (singleObject != NULL && eMetaObjectType::enReference == singleObject->GetMetaType()) {
							removedRef.insert(clsid);
						}
					}
				}
			}
			if (createdRef.size() > 0 && currentRef.size() == 0) {
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL ? "BYTEA" : "BLOB");
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
			}
			if (currentRef.size() > 0) {
				for (auto clsid : removedRef) {
					wxString clsStr; clsStr << clsid;
					retCode = databaseLayer->RunQuery("DELETE FROM %s WHERE %s_RTRef = " + clsStr, tableName, fieldName);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return retCode;
				}
			}
			if (removedRef.size() > 0 && currentRef.size() == 0) {
				retCode = databaseLayer->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)eFieldTypes::eFieldTypes_Reference);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_RTRef;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_RRRef;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
			}
		}
	}
	//delete 
	else if (srcAttr == NULL) {
		const wxString& fieldName = dstAttr->GetFieldNameDB(); bool removeReference = false; IMetadata* metaData = dstAttr->GetMetadata();
		retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_TYPE;", tableName, fieldName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		for (auto clsid : dstAttr->GetClsids()) {
			eValueTypes valType = CValue::GetVTByID(clsid);
			switch (valType) {
			case eValueTypes::TYPE_BOOLEAN:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_B;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case eValueTypes::TYPE_NUMBER:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_N;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case eValueTypes::TYPE_DATE:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case eValueTypes::TYPE_STRING:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_S;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case eValueTypes::TYPE_ENUM:
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_E;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			default:
				IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
				wxASSERT(singleObject);
				if (singleObject != NULL && eMetaObjectType::enReference == singleObject->GetMetaType()) {
					removeReference = true;
				}
				break;
			}
		}
		if (removeReference) {
			retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_RTRef;", tableName, fieldName);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
			retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s_RRRef;", tableName, fieldName);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}

	return retCode;
}

#include "core/metadata/singleClass.h"

void IMetaAttributeObject::SetValueAttribute(IMetaAttributeObject* metaAttr,
	const CValue& cValue, PreparedStatement* statement, int& position)
{
	//write type & data
	if (cValue.GetType() == eValueTypes::TYPE_EMPTY) {

		statement->SetParamInt(position++, eFieldTypes_Empty); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_BOOLEAN) {

		statement->SetParamInt(position++, eFieldTypes_Boolean); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, cValue.GetBoolean()); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_NUMBER) {

		statement->SetParamInt(position++, eFieldTypes_Number); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_DATE) {

		statement->SetParamInt(position++, eFieldTypes_Date); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, cValue.GetDate()); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_STRING) {

		statement->SetParamInt(position++, eFieldTypes_String); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, cValue.GetString()); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_NULL) {

		statement->SetParamInt(position++, eFieldTypes_Null); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == eValueTypes::TYPE_ENUM) {

		statement->SetParamInt(position++, eFieldTypes_Enum); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 	
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, cValue.GetInteger()); //DATA enum 

		if (metaAttr->ContainMetaType(eMetaObjectType::enReference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else {

		statement->SetParamInt(position++, eFieldTypes_Reference); //TYPE

		if (metaAttr->ContainType(eValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(eValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(eValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(eValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(eValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		const CLASS_ID& clsid = cValue.GetTypeClass();
		wxASSERT(clsid > 0);

		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);

		IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
		wxASSERT(singleObject);

		if (singleObject != NULL && singleObject->GetMetaType() == eMetaObjectType::enReference) {
			CReferenceDataObject* refData = NULL;
			if (cValue.ConvertToValue(refData)) {
				statement->SetParamNumber(position++, clsid); //TYPE REF
				statement->SetParamBlob(position++, refData->GetReferenceData(), sizeof(reference_t)); //DATA REF
			}
			else {
				statement->SetParamNumber(position++, 0); //TYPE REF
				statement->SetParamNull(position++); //DATA REF
			}
		}
		else {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
}

#include "core/compiler/enum.h"

bool IMetaAttributeObject::GetValueAttribute(const wxString& fieldName,
	const eFieldTypes& fieldType, IMetaAttributeObject* metaAttr, CValue& retValue, DatabaseResultSet* resultSet, bool createData)
{
	switch (fieldType)
	{
	case eFieldTypes_Boolean:
		retValue = resultSet->GetResultBool(fieldName);
		return true;
	case eFieldTypes_Number:
		retValue = resultSet->GetResultNumber(fieldName);
		return true;
	case eFieldTypes_Date:
		retValue = resultSet->GetResultDate(fieldName);
		return true;
	case eFieldTypes_String:
		retValue = resultSet->GetResultString(fieldName);
		return true;
	case eFieldTypes_Null:
		retValue = eValueTypes::TYPE_NULL;
		return true;
	case eFieldTypes_Enum:
	{
		const CValue& defValue = metaAttr->CreateValue();
		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);
		IObjectValueAbstract* so =
			metaData->GetAvailableObject(defValue.GetTypeClass());
		wxASSERT(so);
		IEnumerationWrapper* enumVal =
			metaData->CreateAndConvertObjectRef<IEnumerationWrapper>(so->GetClassName());
		CValue enumVariant = resultSet->GetResultInt(fieldName);
		CValue* ppParams[] = { &enumVariant };

		if (enumVal != NULL &&
			enumVal->Init(ppParams, 1)) {
			const CValue& objValue = enumVal->GetEnumVariantValue();
			wxDELETE(enumVal);
			retValue = objValue;
			return true;
		}
		wxDELETE(enumVal);
		retValue = defValue;
		return true;
	}
	case eFieldTypes_Reference:
	{
		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);
		const CLASS_ID& refType = resultSet->GetResultLong(fieldName + "_RTRef");
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(fieldName + "_RRRef", bufferData);
		if (!bufferData.IsEmpty()) {
			if (createData) {
				retValue = CReferenceDataObject::CreateFromPtr(
					metaData, bufferData.GetData()
				);
				return true;
			}
			retValue = CReferenceDataObject::Create(
				metaData, bufferData.GetData()
			);
			return true;
		}
		else if (refType > 0) {
			IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(refType);
			wxASSERT(singleObject);
			IMetaObject* metaObject = singleObject->GetMetaObject();
			wxASSERT(metaObject);
			retValue = CReferenceDataObject::Create(
				metaData, metaObject->GetMetaID()
			);
			return true;
		}
		break;
	}
	}

	return false;
}

bool IMetaAttributeObject::GetValueAttribute(const wxString& fieldName,
	IMetaAttributeObject* metaAttr, CValue& retValue, DatabaseResultSet* resultSet, bool createData)
{
	eFieldTypes fieldType =
		static_cast<eFieldTypes>(resultSet->GetResultInt(fieldName + "_TYPE"));

	switch (fieldType)
	{
	case eFieldTypes_Boolean:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_B", eFieldTypes_Boolean, metaAttr, retValue, resultSet, createData);
	case eFieldTypes_Number:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_N", eFieldTypes_Number, metaAttr, retValue, resultSet, createData);
	case eFieldTypes_Date:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_D", eFieldTypes_Date, metaAttr, retValue, resultSet, createData);
	case eFieldTypes_String:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_S", eFieldTypes_String, metaAttr, retValue, resultSet, createData);
	case eFieldTypes_Null:
		retValue = eValueTypes::TYPE_NULL;
		return true;
	case eFieldTypes_Enum:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_E", eFieldTypes_Enum, metaAttr, retValue, resultSet, createData);
	case eFieldTypes_Reference:
		return IMetaAttributeObject::GetValueAttribute(fieldName, eFieldTypes_Reference, metaAttr, retValue, resultSet, createData);
	default:
		retValue = metaAttr->CreateValue(); // if attribute was updated after 
		return true;
	}

	return false;
}

bool IMetaAttributeObject::GetValueAttribute(IMetaAttributeObject* metaAttr, CValue& retValue, DatabaseResultSet* resultSet, bool createData)
{
	return IMetaAttributeObject::GetValueAttribute(
		metaAttr->GetFieldNameDB(),
		metaAttr, retValue, resultSet, createData
	);
}
