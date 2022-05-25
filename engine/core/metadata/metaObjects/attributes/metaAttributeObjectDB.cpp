#include "metaAttributeObject.h"

#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"

#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

unsigned int IMetaAttributeObject::GetSQLFieldCount(IMetaAttributeObject* metaAttr)
{
	wxString fieldName = metaAttr->GetFieldNameDB(); unsigned int sqlField = 1;

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
	wxString fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

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
	wxString fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

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

IMetaAttributeObject::sqlField_t IMetaAttributeObject::GetSQLFieldData(IMetaAttributeObject* metaAttr)
{
	wxString fieldName = metaAttr->GetFieldNameDB(); sqlField_t sqlData(fieldName + "_TYPE");

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

int IMetaAttributeObject::ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	int retCode = 1;
	//is null - create
	if (dstAttr == NULL) {
		wxString fieldName = srcAttr->GetFieldNameDB(); bool createReference = false; IMetadata* metaData = srcAttr->GetMetadata();
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
				if (eMetaObjectType::enReference == singleObject->GetMetaType()) {
					createReference = true;
				}
			}
		}
		if (createReference) {
			retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
			retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, "BLOB");
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	// update 
	else if (srcAttr != NULL) {
		if (srcAttr->GetTypeDescription() != dstAttr->GetTypeDescription()) {
			wxString fieldName = srcAttr->GetFieldNameDB(); std::set<CLASS_ID> createdRef, currentRef, removedRef; IMetadata* metaData = srcAttr->GetMetadata();
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
						if (eMetaObjectType::enReference == singleObject->GetMetaType()) {
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
						if (!srcAttr->EqualsType(clsid, dstAttr->GetDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_B TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_NUMBER:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_N TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_DATE:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetDescription())) {
							if (srcAttr->GetDateTime() != eDateFractions::eDateFractions_Time && dstAttr->GetDateTime() == eDateFractions::eDateFractions_Time) {
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
						if (!srcAttr->EqualsType(clsid, dstAttr->GetDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_S TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case eValueTypes::TYPE_ENUM:
						if (!srcAttr->EqualsType(clsid, dstAttr->GetDescription()))
							retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s_E TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					default:
						IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(clsid);
						wxASSERT(singleObject);
						if (eMetaObjectType::enReference == singleObject->GetMetaType()) {
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
						if (singleObject && eMetaObjectType::enReference == singleObject->GetMetaType()) {
							removedRef.insert(clsid);
						}
					}
				}
			}
			if (createdRef.size() > 0 && currentRef.size() == 0) {
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, "BLOB");
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
		wxString fieldName = dstAttr->GetFieldNameDB(); bool removeReference = false; IMetadata* metaData = dstAttr->GetMetadata();
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
				if (singleObject && eMetaObjectType::enReference == singleObject->GetMetaType()) {
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

#include "metadata/singleMetaTypes.h"

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
			statement->SetParamInt(position++, cValue.ToInt()); //DATA enum 

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

		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);

		CLASS_ID clsid = cValue.GetClassType();
		wxASSERT(clsid > 0);

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

#include "compiler/enum.h"

CValue IMetaAttributeObject::GetValueAttribute(const wxString& fieldName, const eFieldTypes& fieldType, IMetaAttributeObject* metaAttr, DatabaseResultSet* resultSet, bool createData)
{
	switch (fieldType)
	{
	case eFieldTypes_Boolean:
		return resultSet->GetResultBool(fieldName);
	case eFieldTypes_Number:
		return resultSet->GetResultNumber(fieldName);
	case eFieldTypes_Date:
		return resultSet->GetResultDate(fieldName);
	case eFieldTypes_String:
		return resultSet->GetResultString(fieldName);
	case eFieldTypes_Enum:
	{
		CValue defValue = metaAttr->CreateValue();

		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);
		IObjectValueAbstract* so =
			metaData->GetAvailableObject(defValue.GetClassType());
		wxASSERT(so);

		IEnumerationWrapper* enumVal =
			metaData->CreateAndConvertObjectRef<IEnumerationWrapper>(so->GetClassName());

		CValue enumVariant = resultSet->GetResultInt(fieldName);
		CValue* ppParams[] = { &enumVariant };

		if (enumVal != NULL &&
			enumVal->Init(ppParams)) {
			return enumVal->GetEnumVariantValue();
		}

		return defValue;
	}
	case eFieldTypes_Reference:
	{
		IMetadata* metaData = metaAttr->GetMetadata();
		wxASSERT(metaData);
		CLASS_ID refType = resultSet->GetResultLong(fieldName + "_RTRef");
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(fieldName + "_RRRef", bufferData);
		if (!bufferData.IsEmpty()) {
			if (createData) {
				return CReferenceDataObject::CreateFromPtr(
					metaData, bufferData.GetData()
				);
			}
			return CReferenceDataObject::Create(
				metaData, bufferData.GetData()
			);
		}
		else if (refType > 0) {
			IMetaTypeObjectValueSingle* singleObject = metaData->GetTypeObject(refType);
			wxASSERT(singleObject);
			IMetaObject* metaObject = singleObject->GetMetaObject();
			wxASSERT(metaObject);
			return CReferenceDataObject::Create(
				metaData, metaObject->GetMetaID()
			);
		}
		break;
	}
	}

	return CValue();
}

CValue IMetaAttributeObject::GetValueAttribute(const wxString& fieldName, IMetaAttributeObject* metaAttr, DatabaseResultSet* resultSet, bool createData)
{
	eFieldTypes fieldType =
		static_cast<eFieldTypes>(resultSet->GetResultInt(fieldName + "_TYPE"));

	switch (fieldType)
	{
	case eFieldTypes_Boolean:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_B", eFieldTypes_Boolean, metaAttr, resultSet, createData);
	case eFieldTypes_Number:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_N", eFieldTypes_Number, metaAttr, resultSet, createData);
	case eFieldTypes_Date:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_D", eFieldTypes_Date, metaAttr, resultSet, createData);
	case eFieldTypes_String:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_S", eFieldTypes_String, metaAttr, resultSet, createData);
	case eFieldTypes_Enum:
		return IMetaAttributeObject::GetValueAttribute(fieldName + "_E", eFieldTypes_Enum, metaAttr, resultSet, createData);
	case eFieldTypes_Reference:
		return IMetaAttributeObject::GetValueAttribute(fieldName, eFieldTypes_Reference, metaAttr, resultSet, createData);
	}

	return CValue();
}

CValue IMetaAttributeObject::GetValueAttribute(IMetaAttributeObject* metaAttr, DatabaseResultSet* resultSet, bool createData)
{
	return IMetaAttributeObject::GetValueAttribute(
		metaAttr->GetFieldNameDB(),
		metaAttr, resultSet, createData
	);
}
