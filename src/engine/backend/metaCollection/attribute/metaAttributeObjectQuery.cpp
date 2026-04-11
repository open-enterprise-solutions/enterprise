#include "metaAttributeObject.h"

#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/metaData.h"

#include "backend/objCtor.h"
#include "backend/metaCollection/partial/reference/reference.h"

wxString ibValueMetaObjectAttributeBase::GetSQLTypeObject(const ibClassID& clsid) const
{
	const ibTypeDescription& typeDesc = GetTypeDesc();

	switch (ibValue::GetVTByID(clsid))
	{
	case ibValueTypes::TYPE_BOOLEAN:
		return wxString::Format("SMALLINT");
	case ibValueTypes::TYPE_NUMBER:
		if (typeDesc.GetScale() > 0)
			return wxString::Format("NUMERIC(%i,%i)", typeDesc.GetPrecision(), typeDesc.GetScale());
		else
			return wxString::Format("NUMERIC(%i)", typeDesc.GetPrecision());
	case ibValueTypes::TYPE_DATE:
		if (typeDesc.GetDateFraction() == ibDateFractions::ibDateFractions_Date)
			return wxString::Format("DATE");
		else if (typeDesc.GetDateFraction() == ibDateFractions::ibDateFractions_DateTime)
			return wxString::Format("TIMESTAMP");
		else
			return wxString::Format("TIME");
	case ibValueTypes::TYPE_STRING:
		if (typeDesc.GetAllowedLength() == ibAllowedLength::ibAllowedLength_Variable)
			return wxString::Format("VARCHAR(%i)", typeDesc.GetLength());
		else
			return wxString::Format("CHAR(%i)", typeDesc.GetLength());
	case ibValueTypes::TYPE_ENUM:
		return wxString::Format("INTEGER");
	default:
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			return wxString::Format("BYTEA");
		return wxString::Format("BLOB");
	}

	return wxEmptyString;
}

unsigned short ibValueMetaObjectAttributeBase::GetSQLFieldCount(const ibValueMetaObjectAttributeBase* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); unsigned short sqlField = 1;

	if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_DATE)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
		sqlField += 1;
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM)) {
		sqlField += 1;
	}
	if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
		sqlField += 2;
	}

	return sqlField;
}

wxString ibValueMetaObjectAttributeBase::GetSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr, const wxString& aggr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_B";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		if (aggr.IsEmpty())
			sqlField += fieldName + "_N";
		else
			sqlField += aggr + "(" + fieldName + "_N" + ") AS " + fieldName + "_N";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		if (aggr.IsEmpty())
			sqlField += fieldName + "_D";
		else
			sqlField += aggr + "(" + fieldName + "_D" + ") AS " + fieldName + "_D";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_S";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_E";
	}
	if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
		if (!sqlField.IsEmpty())
			sqlField += ",";
		sqlField += fieldName + "_RTRef" + "," + fieldName + "_RRRef";
	}

	return fieldName + "_TYPE"
		+ (sqlField.Length() > 0 ? "," : "")
		+ sqlField;
}

wxString ibValueMetaObjectAttributeBase::GetCompositeSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr, const wxString& cmp)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_B " + cmp + " ? ";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_N " + cmp + " ? ";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_D " + cmp + " ? ";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_S " + cmp + " ? ";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_E " + cmp + " ? ";
	}
	if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
		if (!sqlField.IsEmpty())
			sqlField += " AND ";
		sqlField += fieldName + "_RTRef = ? AND " + fieldName + "_RRRef " + cmp + " ? ";
	}

	return fieldName + "_TYPE = ? "
		+ (sqlField.Length() > 0 ? " AND " : "")
		+ sqlField;
}

wxString ibValueMetaObjectAttributeBase::GetExcludeSQLFieldName(const ibValueMetaObjectAttributeBase* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); wxString sqlField = wxEmptyString;

	if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_B = excluded." + fieldName + "_B";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_N = excluded." + fieldName + "_N";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_DATE)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_D = excluded." + fieldName + "_D";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_S = excluded." + fieldName + "_S";
	}
	if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_E = excluded." + fieldName + "_E";
	}
	if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
		if (!sqlField.IsEmpty())
			sqlField += ", ";
		sqlField += fieldName + "_RTRef = excluded." + fieldName + "_RTRef, " + fieldName + "_RRRef = excluded." + fieldName + "_RRRef";
	}

	return fieldName + "_TYPE = excluded." + fieldName + "_TYPE"
		+ (sqlField.Length() > 0 ? ", " : "")
		+ sqlField;
}

ibValueMetaObjectAttributeBase::ibSQLField ibValueMetaObjectAttributeBase::GetSQLFieldData(const ibValueMetaObjectAttributeBase* metaAttr)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB(); ibSQLField sqlData(fieldName + "_TYPE");

	if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_Boolean,
			fieldName + "_B"
		);
	}

	if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_Number,
			fieldName + "_N"
		);
	}

	if (metaAttr->ContainType(ibValueTypes::TYPE_DATE)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_Date,
			fieldName + "_D"
		);
	}

	if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_String,
			fieldName + "_S"
		);
	}

	if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_Enum,
			fieldName + "_E"
		);
	}

	if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
		sqlData.AppendType(
			ibFieldTypes::ibFieldTypes_Reference,
			fieldName + "_RTRef",
			fieldName + "_RRRef"
		);
	}

	return sqlData;
}

#include "backend/valueInfo.h"

int ibValueMetaObjectAttributeBase::ProcessAttribute(const wxString& tableName,
	const ibValueMetaObjectAttributeBase* srcAttr, const ibValueMetaObjectAttributeBase* dstAttr)
{
	int retCode = 1;
	//is null - create
	if (dstAttr == nullptr) {
		const wxString& fieldName = srcAttr->GetFieldNameDB(); bool createReference = false; ibMetaData* metaData = srcAttr->GetMetaData();
		retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_TYPE %s DEFAULT 0 NOT NULL;", tableName, fieldName, "INTEGER");
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		const ibTypeDescription& typeDesc = srcAttr->GetTypeDesc();
		for (auto clsid : typeDesc.GetClsidList()) {
			ibValueTypes valType = ibValue::GetVTByID(clsid);
			switch (valType) {
			case ibValueTypes::TYPE_BOOLEAN:
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_B %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case ibValueTypes::TYPE_NUMBER:
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_N %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case ibValueTypes::TYPE_DATE:
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case ibValueTypes::TYPE_STRING:
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_S %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			case ibValueTypes::TYPE_ENUM:
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_E %s DEFAULT 0;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
				break;
			default:
				const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
				wxASSERT(typeCtor);
				if (typeCtor != nullptr && ibCtorObjectMetaType::ibCtorObjectMetaType_Reference == typeCtor->GetMetaTypeCtor()) {
					createReference = true;
				}
			}
		}
		if (createReference) {
			retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;

			retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL ? wxT("BYTEA") : wxString::Format(wxT("BINARY(%i)"), reference_size_t));
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	// update 
	else if (srcAttr != nullptr) {
		if (srcAttr->GetTypeDesc() != dstAttr->GetTypeDesc()) {
			const ibTypeDescription& srcTypeDesc = srcAttr->GetTypeDesc();
			const wxString& fieldName = srcAttr->GetFieldNameDB(); std::set<ibClassID> createdRef, currentRef, removedRef; ibMetaData* metaData = srcAttr->GetMetaData();
			for (auto clsid : srcTypeDesc.GetClsidList()) {
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				if (!dstAttr->ContainType(clsid)) {
					ibValueTypes valType = ibValue::GetVTByID(clsid);
					switch (valType) {
					case ibValueTypes::TYPE_BOOLEAN:
						retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_B %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_NUMBER:
						retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_N %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_DATE:
						retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_STRING:
						retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_S %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_ENUM:
						retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_E %s DEFAULT 0;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					default:
						const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
						//wxASSERT(typeCtor);
						if (typeCtor != nullptr && ibCtorObjectMetaType::ibCtorObjectMetaType_Reference == typeCtor->GetMetaTypeCtor()) {
							createdRef.insert(clsid);
						}
					}
				}
			}
			const ibTypeDescription& dstTypeDesc = dstAttr->GetTypeDesc();
			for (auto clsid : dstTypeDesc.GetClsidList()) {
				ibValueTypes valType = ibValue::GetVTByID(clsid);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				if (srcAttr->ContainType(clsid)) {
					switch (valType) {
					case ibValueTypes::TYPE_BOOLEAN:
						if (!srcAttr->EqualType(clsid, dstAttr->GetTypeDesc()))
							retCode = db_query->RunQuery("ALTER TABLE %s ALTER COLUMN %s_B TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_NUMBER:
						if (!srcAttr->EqualType(clsid, dstAttr->GetTypeDesc()))
							retCode = db_query->RunQuery("ALTER TABLE %s ALTER COLUMN %s_N TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_DATE:
						if (!srcAttr->EqualType(clsid, dstAttr->GetTypeDesc())) {
							if (srcTypeDesc.GetDateFraction() != ibDateFractions::ibDateFractions_Time && dstTypeDesc.GetDateFraction() == ibDateFractions::ibDateFractions_Time) {
								retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
								if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
									return retCode;
								retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_D %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
							}
							else {
								retCode = db_query->RunQuery("ALTER TABLE %s ALTER COLUMN %s_D TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
							}
						}
						break;
					case ibValueTypes::TYPE_STRING:
						if (!srcAttr->EqualType(clsid, dstAttr->GetTypeDesc()))
							retCode = db_query->RunQuery("ALTER TABLE %s ALTER COLUMN %s_S TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					case ibValueTypes::TYPE_ENUM:
						if (!srcAttr->EqualType(clsid, dstAttr->GetTypeDesc()))
							retCode = db_query->RunQuery("ALTER TABLE %s ALTER COLUMN %s_E TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject(clsid));
						break;
					default:
						const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
						//wxASSERT(typeCtor);
						if (typeCtor != nullptr && ibCtorObjectMetaType::ibCtorObjectMetaType_Reference == typeCtor->GetMetaTypeCtor()) {
							currentRef.insert(clsid);
						}
					}
				}
				else {
					switch (valType) {
					case ibValueTypes::TYPE_BOOLEAN:
						retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_B;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_Boolean);
						break;
					case ibValueTypes::TYPE_NUMBER:
						retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_N;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_Number);
						break;
					case ibValueTypes::TYPE_DATE:
						retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_Date);
						break;
					case ibValueTypes::TYPE_STRING:
						retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_S;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_String);
						break;
					case ibValueTypes::TYPE_ENUM:
						retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_E;", tableName, fieldName);
						if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
							return retCode;
						retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_Enum);
						break;
					default:
						const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
						//wxASSERT(typeCtor);
						if (typeCtor != nullptr && ibCtorObjectMetaType::ibCtorObjectMetaType_Reference == typeCtor->GetMetaTypeCtor()) {
							removedRef.insert(clsid);
						}
					}
				}
			}
			if (createdRef.size() > 0 && currentRef.size() == 0 && removedRef.size() == 0) {
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_RTRef %s;", tableName, fieldName, "BIGINT");
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = db_query->RunQuery("ALTER TABLE %s ADD %s_RRRef %s;", tableName, fieldName, db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL ? wxT("BYTEA") : wxString::Format(wxT("BINARY(%i)"), reference_size_t));
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
			}
			if (currentRef.size() > 0) {
				for (auto clsid : removedRef) {
					wxString clsStr; clsStr << clsid;
					retCode = db_query->RunQuery("DELETE FROM %s WHERE %s_RTRef = " + clsStr, tableName, fieldName);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return retCode;
				}
			}
			if (removedRef.size() > 0 && currentRef.size() == 0) {
				retCode = db_query->RunQuery("UPDATE %s SET %s_TYPE = 0 WHERE %s_TYPE = %i;", tableName, fieldName, fieldName, (int)ibFieldTypes::ibFieldTypes_Reference);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_RTRef;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_RRRef;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
			}
		}
	}
	//delete 
	else if (srcAttr == nullptr) {
		const wxString& fieldName = dstAttr->GetFieldNameDB(); bool removeReference = false; ibMetaData* metaData = dstAttr->GetMetaData();
		retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_TYPE;", tableName, fieldName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		const ibTypeDescription& dstTypeDesc = dstAttr->GetTypeDesc();
		for (auto clsid : dstTypeDesc.GetClsidList()) {
			ibValueTypes valType = ibValue::GetVTByID(clsid);
			switch (valType) {
			case ibValueTypes::TYPE_BOOLEAN:
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_B;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case ibValueTypes::TYPE_NUMBER:
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_N;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case ibValueTypes::TYPE_DATE:
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_D;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case ibValueTypes::TYPE_STRING:
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_S;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			case ibValueTypes::TYPE_ENUM:
				retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_E;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				break;
			default:
				const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
				//wxASSERT(typeCtor);
				if (typeCtor != nullptr && ibCtorObjectMetaType::ibCtorObjectMetaType_Reference == typeCtor->GetMetaTypeCtor()) {
					removeReference = true;
				}
				break;
			}
		}

		if (removeReference) {
			retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_RTRef;", tableName, fieldName);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
			retCode = db_query->RunQuery("ALTER TABLE %s DROP %s_RRRef;", tableName, fieldName);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}

	return retCode;
}

void ibValueMetaObjectAttributeBase::SetValueAttribute(const ibValueMetaObjectAttributeBase* metaAttr,
	const ibValue& cValue, ibPreparedStatement* statement, int& position)
{
	//write type & data
	if (cValue.GetType() == ibValueTypes::TYPE_EMPTY) {

		statement->SetParamInt(position++, ibFieldTypes_Empty); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_BOOLEAN) {

		statement->SetParamInt(position++, ibFieldTypes_Boolean); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, cValue.GetBoolean()); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_NUMBER) {

		statement->SetParamInt(position++, ibFieldTypes_Number); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_DATE) {

		statement->SetParamInt(position++, ibFieldTypes_Date); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, cValue.GetDate()); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_STRING) {

		statement->SetParamInt(position++, ibFieldTypes_String); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, cValue.GetString()); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_NULL) {

		statement->SetParamInt(position++, ibFieldTypes_Null); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (cValue.GetType() == ibValueTypes::TYPE_ENUM) {

		statement->SetParamInt(position++, ibFieldTypes_Enum); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, cValue.GetNumber()); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 	
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, cValue.GetInteger()); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else {

		statement->SetParamInt(position++, ibFieldTypes_Reference); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		const ibClassID& clsid = cValue.GetClassType();
		wxASSERT(clsid > 0);

		const ibMetaData* metaData = metaAttr->GetMetaData();
		wxASSERT(metaData);

		const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid);
		wxASSERT(typeCtor);

		if (typeCtor != nullptr && typeCtor->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference) {
			ibValueReferenceDataObject* refData = nullptr;
			if (cValue.ConvertToValue(refData)) {
				statement->SetParamNumber(position++, clsid); //TYPE REF
				statement->SetParamBlob(position++, refData->GetReferenceData(), sizeof(ibReference)); //DATA REF
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

void ibValueMetaObjectAttributeBase::SetValueAttribute(const ibValueMetaObjectAttributeBase* attribute, const ibValue& cValue, ibPreparedStatement* statement)
{
	int position = 1;
	SetValueAttribute(attribute, cValue, statement, position);
}

#include "backend/compiler/enumUnit.h"

bool ibValueMetaObjectAttributeBase::GetValueAttribute(const wxString& fieldName,
	const ibFieldTypes& fieldType, const ibValueMetaObjectAttributeBase* metaAttr, ibValue& retValue, ibDatabaseResultSet* resultSet, bool createData)
{
	switch (fieldType)
	{
	case ibFieldTypes_Boolean:
		retValue = resultSet->GetResultBool(fieldName);
		return true;
	case ibFieldTypes_Number:
		retValue = resultSet->GetResultNumber(fieldName);
		return true;
	case ibFieldTypes_Date:
		retValue = resultSet->GetResultDate(fieldName);
		return true;
	case ibFieldTypes_String:
		retValue = resultSet->GetResultString(fieldName);
		return true;
	case ibFieldTypes_Null:
		retValue = ibValueTypes::TYPE_NULL;
		return true;
	case ibFieldTypes_Enum:
	{
		const ibMetaData* metaData = metaAttr->GetMetaData();
		wxASSERT(metaData);

		const ibValue& defValue = metaAttr->CreateValue();
		const ibCtorAbstractType* so = metaData->GetAvailableCtor(defValue.GetClassType());

		if (so != nullptr) {

			ibValue enumVariant(resultSet->GetResultInt(fieldName));
			ibValue* ppParams[] = { &enumVariant };

			try {
				ibValuePtr<ibValueEnumerationWrapper> creator(
					metaData->CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName(), ppParams, 1));
				retValue = creator->GetEnumVariantValue();
			}
			catch (...) {
				retValue = defValue;
				return false;
			}

			return true;
		}

		retValue = defValue;
		return false;
	}
	case ibFieldTypes_Reference:
	{
		ibMetaData* metaData = metaAttr->GetMetaData();
		wxASSERT(metaData);
		const ibClassID& refType = resultSet->GetResultLong(fieldName + wxT("_RTRef"));

		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(fieldName + wxT("_RRRef"), bufferData);
		if (!bufferData.IsEmpty()) {

			if (createData) {

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::CreateFromPtr(metaData, bufferData.GetData()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			ibValuePtr<ibValueReferenceDataObject> created_reference(
				ibValueReferenceDataObject::Create(metaData, bufferData.GetData()));

			retValue = created_reference;
			return created_reference != nullptr;
		}
		else if (refType > 0) {

			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(refType);
			if (typeCtor != nullptr) {

				const ibValueMetaObject* metaObject = typeCtor->GetMetaObject();
				wxASSERT(metaObject);

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::Create(metaData, metaObject->GetMetaID()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			return false;
		}

		break;
	}
	}

	return false;
}

bool ibValueMetaObjectAttributeBase::GetValueAttribute(const wxString& fieldName,
	const ibValueMetaObjectAttributeBase* metaAttr, ibValue& retValue, ibDatabaseResultSet* resultSet, bool createData)
{
	ibFieldTypes fieldType =
		static_cast<ibFieldTypes>(resultSet->GetResultInt(fieldName + wxT("_TYPE")));

	switch (fieldType)
	{
	case ibFieldTypes_Boolean:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName + wxT("_B"), ibFieldTypes_Boolean, metaAttr, retValue, resultSet, createData);
	case ibFieldTypes_Number:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName + wxT("_N"), ibFieldTypes_Number, metaAttr, retValue, resultSet, createData);
	case ibFieldTypes_Date:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName + wxT("_D"), ibFieldTypes_Date, metaAttr, retValue, resultSet, createData);
	case ibFieldTypes_String:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName + wxT("_S"), ibFieldTypes_String, metaAttr, retValue, resultSet, createData);
	case ibFieldTypes_Null:
		retValue = ibValueTypes::TYPE_NULL;
		return true;
	case ibFieldTypes_Enum:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName + wxT("_E"), ibFieldTypes_Enum, metaAttr, retValue, resultSet, createData);
	case ibFieldTypes_Reference:
		return ibValueMetaObjectAttributeBase::GetValueAttribute(fieldName, ibFieldTypes_Reference, metaAttr, retValue, resultSet, createData);
	default:
		retValue = metaAttr->CreateValue(); // if attribute was updated after 
		return true;
	}

	return false;
}

bool ibValueMetaObjectAttributeBase::GetValueAttribute(const ibValueMetaObjectAttributeBase* metaAttr, ibValue& retValue, ibDatabaseResultSet* resultSet, bool createData)
{
	return ibValueMetaObjectAttributeBase::GetValueAttribute(
		metaAttr->GetFieldNameDB(),
		metaAttr, retValue, resultSet, createData
	);
}

///////////////////////////////////////////////////

void ibValueMetaObjectAttributeBase::SetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, const ibReaderMemory& reader, ibPreparedStatement* statement,
	int& position)
{
	const int fieldType = reader.r_s32();

	//write type & data
	if (fieldType == ibFieldTypes_Empty || fieldType == ibFieldTypes_Null) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (fieldType == ibFieldTypes_Boolean) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, reader.r_u8()); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (fieldType == ibFieldTypes_Number) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)) {

			ibNumber value;
			reader.r(&value.exponent, sizeof(value.exponent));
			reader.r(&value.mantissa, sizeof(value.mantissa));
			reader.r(&value.info, sizeof(value.info));

			statement->SetParamNumber(position++, value); //DATA number 
		}

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (fieldType == ibFieldTypes_Date) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, wxLongLong(reader.r_u64())); //DATA date 

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (fieldType == ibFieldTypes_String) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING)) {
			statement->SetParamString(position++, reader.r_stringZ()); //DATA string 
		}

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else if (fieldType == ibFieldTypes_Enum) {

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 

		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 

		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 	

		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, reader.r_s32()); //DATA enum 

		if (metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)) {
			statement->SetParamNumber(position++, 0); //TYPE REF
			statement->SetParamNull(position++); //DATA REF
		}
	}
	else {

		wxMemoryBuffer typeRRBuffer;

		statement->SetParamInt(position++, fieldType); //TYPE

		if (metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN))
			statement->SetParamBool(position++, false); //DATA binary 
		if (metaAttr->ContainType(ibValueTypes::TYPE_NUMBER))
			statement->SetParamNumber(position++, 0); //DATA number 
		if (metaAttr->ContainType(ibValueTypes::TYPE_DATE))
			statement->SetParamDate(position++, emptyDate); //DATA date 
		if (metaAttr->ContainType(ibValueTypes::TYPE_STRING))
			statement->SetParamString(position++, wxEmptyString); //DATA string 

		if (metaAttr->ContainType(ibValueTypes::TYPE_ENUM))
			statement->SetParamInt(position++, wxNOT_FOUND); //DATA enum 

		statement->SetParamNumber(position++, reader.r_u64()); //TYPE REF
		reader.r_chunk(rt_ref_chunk, typeRRBuffer);
		statement->SetParamBlob(position++, typeRRBuffer.GetData(), typeRRBuffer.GetDataLen()); //DATA REF
	}
}

void ibValueMetaObjectAttributeBase::SetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, const ibReaderMemory& reader, ibPreparedStatement* statement)
{
	int position = 1;
	SetBinaryData(metaAttr, reader, statement, position);
}

void ibValueMetaObjectAttributeBase::GetBinaryData(const ibValueMetaObjectAttributeBase* metaAttr, ibWriterMemory& writer, ibDatabaseResultSet* resultSet)
{
	const wxString& fieldName = metaAttr->GetFieldNameDB();
	const int fieldType = resultSet->GetResultInt(fieldName + wxT("_TYPE"));

	writer.w_s32(fieldType);

	//DATA boolean 
	if (fieldType == ibFieldTypes_Boolean
		&& metaAttr->ContainType(ibValueTypes::TYPE_BOOLEAN)
		&& resultSet != nullptr) {
		writer.w_u8(resultSet->GetResultBool(fieldName + wxT("_B")));
	}
	else if (fieldType == ibFieldTypes_Boolean) {
		writer.w_u8(false);
	}

	//DATA number 
	if (fieldType == ibFieldTypes_Number
		&& metaAttr->ContainType(ibValueTypes::TYPE_NUMBER)
		&& resultSet != nullptr) {
		const ibNumber& value = resultSet->GetResultNumber(fieldName + wxT("_N"));
		writer.w(&value.exponent, sizeof(value.exponent));
		writer.w(&value.mantissa, sizeof(value.mantissa));
		writer.w(&value.info, sizeof(value.info));
	}
	else if (fieldType == ibFieldTypes_Number) {
		const ibNumber& value = 0;
		writer.w(&value.exponent, sizeof(value.exponent));
		writer.w(&value.mantissa, sizeof(value.mantissa));
		writer.w(&value.info, sizeof(value.info));
	}

	//DATA date 
	if (fieldType == ibFieldTypes_Date
		&& metaAttr->ContainType(ibValueTypes::TYPE_DATE)
		&& resultSet != nullptr) {
		const wxDateTime& dt = resultSet->GetResultDate(fieldName + wxT("_D"));
		const wxLongLong& llData = dt.GetValue();
		writer.w_u64(llData.GetValue());
	}
	else if (fieldType == ibFieldTypes_Date) {
		writer.w_u64(emptyDate);
	}

	//DATA string 
	if (fieldType == ibFieldTypes_String
		&& metaAttr->ContainType(ibValueTypes::TYPE_STRING)
		&& resultSet != nullptr) {
		writer.w_stringZ(resultSet->GetResultString(fieldName + wxT("_S")));
	}
	else if (fieldType == ibFieldTypes_String) {
		writer.w_stringZ(wxEmptyString);
	}

	//DATA enum 
	if (fieldType == ibFieldTypes_Enum
		&& metaAttr->ContainType(ibValueTypes::TYPE_ENUM)
		&& resultSet != nullptr) {
		writer.w_s32(resultSet->GetResultInt(fieldName + wxT("_E")));
	}
	else if (fieldType == ibFieldTypes_Enum) {
		writer.w_s32(wxNOT_FOUND);
	}

	//DATA reference 
	if (fieldType == ibFieldTypes_Reference
		&& metaAttr->ContainMetaType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference)
		&& resultSet != nullptr) {
		wxMemoryBuffer bufferData;
		resultSet->GetResultBlob(fieldName + wxT("_RRRef"), bufferData);
		writer.w_u64(resultSet->GetResultLong(fieldName + wxT("_RTRef")));
		writer.w_chunk(rt_ref_chunk, bufferData);
	}
	else if (fieldType == ibFieldTypes_Reference) {
		writer.w_u64(0);
		writer.w_chunk(rt_ref_chunk, wxMemoryBuffer());
	}
}

///////////////////////////////////////////////////