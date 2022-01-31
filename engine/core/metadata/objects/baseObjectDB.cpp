////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base db reader/saver
////////////////////////////////////////////////////////////////////////////

#include "baseObject.h"
#include "appData.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "utils/stringUtils.h"

//**********************************************************************************************************
//*                                          Common functions                                              *
//**********************************************************************************************************

wxString IMetaObjectRefValue::GetTableNameDB()
{
	wxString className = GetClassName();
	return wxString::Format("%s%i",
		className, GetMetaID());
}

int IMetaObjectRefValue::ProcessAttribute(const wxString &tableName, IMetaAttributeObject *srcAttr, IMetaAttributeObject *dstAttr) {

	int retCode = 1;
	//is null - create
	if (dstAttr == NULL) {
		wxString fieldName = srcAttr->GetFieldNameDB();
		retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s %s;", tableName, fieldName, srcAttr->GetSQLTypeObject());
	}
	// update 
	else if (srcAttr != NULL) {
		if (srcAttr->GetTypeDescription() != dstAttr->GetTypeDescription()) {
			wxString fieldName = srcAttr->GetFieldNameDB();
			if (srcAttr->GetTypeObject() == dstAttr->GetTypeObject()) {
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ALTER COLUMN %s TYPE %s;", tableName, fieldName, srcAttr->GetSQLTypeObject());
			}
			else {
				retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s;", tableName, fieldName);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return retCode;
				retCode = databaseLayer->RunQuery("ALTER TABLE %s ADD %s %s;", tableName, fieldName, srcAttr->GetSQLTypeObject());
			}
		}
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
	}
	//delete 
	else if (srcAttr == NULL) {
		wxString fieldName = dstAttr->GetFieldNameDB();
		retCode = databaseLayer->RunQuery("ALTER TABLE %s DROP %s;", tableName, fieldName);
	}

	return retCode;
}

int IMetaObjectRefValue::ProcessEnumeration(const wxString &tableName, meta_identifier_t id, CMetaEnumerationObject *srcEnum, CMetaEnumerationObject *dstEnum)
{
	int retCode = 1;
	//is null - create
	if (dstEnum == NULL) {	
		PreparedStatement *prepareStatement = 
			databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) MATCHING(UUID);", tableName);	
		if (!prepareStatement)
			return 0;
		reference_t *reference_impl = new reference_t{ id, srcEnum->GetGuid()};
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		prepareStatement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		retCode = prepareStatement->RunQuery();
		delete reference_impl;
	}
	// update 
	else if (srcEnum != NULL) {
		PreparedStatement *prepareStatement = 
			databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) MATCHING(UUID);", tableName);
		if (!prepareStatement)
			return 0;
		reference_t *reference_impl = new reference_t{ id, srcEnum->GetGuid() };
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		prepareStatement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		retCode = prepareStatement->RunQuery();
		delete reference_impl;
	}
	//delete 
	else if (srcEnum == NULL) {
		retCode = databaseLayer->RunQuery("DELETE FROM %s WHERE UUID = '%s' ;", tableName, dstEnum->GetGuid().str());
	}

	return retCode;
}

int IMetaObjectRefValue::ProcessTable(const wxString &tabularName, CMetaTableObject *srcTable, CMetaTableObject *dstTable)
{
	int retCode = 1;
	//is null - create
	if (dstTable == NULL) {
		retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL, UUIDREF BLOB);", tabularName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return retCode;
		CMetaDefaultAttributeObject *attrNumberLine = srcTable->GetNumberLine();
		wxASSERT(attrNumberLine);
		//default attributes
		for (auto attribute : srcTable->GetObjectAttributes()) {
			retCode = ProcessAttribute(tabularName,
				attribute, NULL);
		}
		retCode = databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s (UUID, %s);", tabularName, tabularName, attrNumberLine->GetFieldNameDB());
	}
	// update 
	else if (srcTable != NULL) {
		for (auto attribute : srcTable->GetObjectAttributes()) {
			retCode = ProcessAttribute(tabularName,
				attribute, dstTable->FindAttributeByName(attribute->GetDocPath())
			);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	//delete 
	else if (srcTable == NULL) {
		retCode = databaseLayer->RunQuery("DROP TABLE %s", tabularName);
	}

	return retCode;
}

bool IMetaObjectRefValue::CreateAndUpdateTableDB(IConfigMetadata *srcMetaData, IMetaObject *srcMetaObject, int flags)
{
	wxString tableName = GetTableNameDB();

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL PRIMARY KEY, UUIDREF BLOB);", tableName);
		for (auto attribute : GetObjectAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				attribute, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s (UUID);", tableName, tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto enumeration : GetObjectEnums()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessEnumeration(tableName, m_metaId,
				enumeration, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto table : GetObjectTables()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessTable(table->GetTableNameDB(),
				table, NULL);
		}
	}
	else if ((flags & updateMetaTable) != 0) {
		//if src is null then delete
		IMetaObjectRefValue *dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {
			//attributes from dst 
			for (auto attribute : dstValue->GetObjectAttributes()) {
				IMetaObject *foundedMeta =
					IMetaObjectRefValue::FindAttributeByName(attribute->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessAttribute(tableName, NULL, attribute);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//attributes current
			for (auto attribute : GetObjectAttributes()) {
				retCode = ProcessAttribute(tableName,
					attribute, dstValue->FindAttributeByName(attribute->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//enums from dst 
			for (auto enumeration : dstValue->GetObjectEnums()) {
				IMetaObject *foundedMeta =
					IMetaObjectRefValue::FindEnumByName(enumeration->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessEnumeration(tableName, m_metaId, NULL, enumeration);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//enums current
			for (auto enumeration : GetObjectEnums()) {
				retCode = ProcessEnumeration(tableName, m_metaId,
					enumeration, dstValue->FindEnumByName(enumeration->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//tables from dst 
			for (auto table : dstValue->GetObjectTables()) {
				IMetaObject *foundedMeta =
					IMetaObjectRefValue::FindTableByName(table->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessTable(table->GetTableNameDB(), NULL, table);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//tables current 
			for (auto table : GetObjectTables()) {
				retCode = ProcessTable(table->GetTableNameDB(),
					table, dstValue->FindTableByName(table->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP TABLE %s", tableName);
		for (auto table : GetObjectTables()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			wxString tabularName = table->GetTableNameDB();
			retCode = databaseLayer->RunQuery("DROP TABLE %s", tabularName);
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

//**********************************************************************************************************
//*                                          Query functions                                               *
//**********************************************************************************************************

bool IDataObjectRefValue::ReadInDB()
{
	if (!m_metaObject || m_bNewObject)
		return false;

	wxString tableName = m_metaObject->GetTableNameDB();

	if (databaseLayer->TableExists(tableName)) {
		wxString queryText = "SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';";
		DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults(queryText);
		if (!resultSet) {
			return false;
		}
		for (auto currTable : m_metaObject->GetObjectTables()) {
			CValueTabularRefSection *tabularSection =
				new CValueTabularRefSection(this, currTable);
			m_aObjectValues[currTable->GetMetaID()] = tabularSection;
			m_aObjectTables.push_back(tabularSection);
		}
		if (resultSet->Next()) {
			//load other attributes 
			for (auto attribute : m_metaObject->GetObjectAttributes()) {
				wxString nameAttribute = attribute->GetFieldNameDB();
				switch (attribute->GetTypeObject())
				{
				case eValueTypes::TYPE_BOOLEAN:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultBool(nameAttribute); break;
				case eValueTypes::TYPE_NUMBER:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultNumber(nameAttribute); break;
				case eValueTypes::TYPE_DATE:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultDate(nameAttribute); break;
				case eValueTypes::TYPE_STRING:
					m_aObjectValues[attribute->GetMetaID()] = resultSet->GetResultString(nameAttribute); break;
				default:
				{
					wxMemoryBuffer bufferData;
					resultSet->GetResultBlob(nameAttribute, bufferData);
					if (!bufferData.IsEmpty()) {
						m_aObjectValues[attribute->GetMetaID()] = CValueReference::CreateFromPtr(
							m_metaObject->GetMetadata(), bufferData.GetData()
						);
					}
					else {
						m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
							m_metaObject->GetMetadata(), attribute->GetTypeObject()
						);
					}
					break;
				}
				}
			}
			for (auto tabularSection : m_aObjectTables) {
				tabularSection->LoadDataFromDB();
			}
		}
		resultSet->Close();
		return true;
	}
	return false;
}

#include "compiler/systemObjects.h"

bool IDataObjectRefValue::SaveInDB()
{
	wxASSERT(m_metaObject);

	if (m_codeGenerator) {
		if (m_aObjectValues[m_codeGenerator->GetMetaID()].IsEmpty()) {
			m_aObjectValues[m_codeGenerator->GetMetaID()] = m_codeGenerator->GenerateCode();
		}
	}

	//check fill attributes 
	bool fillCheck = true;

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		if (attribute->FillCheck()) {
			if (m_aObjectValues[attribute->GetMetaID()].IsEmpty()) {
				wxString fillError =
					wxString::Format(_("""%s"" is a required field"), attribute->GetSynonym());
				CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
				fillCheck = false; 
			}
		}
	}

	if (!fillCheck) {
		return false; 
	}

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	queryText += "UUID, UUIDREF";
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		queryText = queryText + ", " + attribute->GetFieldNameDB();

	}
	queryText += ") VALUES (?, ?";
	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		queryText += ", ?";
	}
	queryText += ") MATCHING (UUID);";
	PreparedStatement *statement = databaseLayer->PrepareStatement(queryText);
	if (!statement) {
		return false;
	}

	m_objGuid = m_reference_impl->m_guid;

	statement->SetParamString(1, m_objGuid.str());
	statement->SetParamBlob(2, m_reference_impl, sizeof(reference_t));

	int position = 3;

	for (auto attribute : m_metaObject->GetObjectAttributes()) {
		m_aObjectValues[attribute->GetMetaID()].SetBinaryData(position++, statement);
	}

	bool hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;

	databaseLayer->CloseStatement(statement);

	//table parts
	if (!hasError) {
		for (auto table : m_aObjectTables) {
			if (!table->SaveDataInDB()) {
				hasError = true; break;
			}
		}
	}

	if (!hasError) {
		m_bNewObject = false;
	}

	return !hasError;
}

bool IDataObjectRefValue::DeleteInDB()
{
	if (m_bNewObject)
		return false;

	wxString tableName = m_metaObject->GetTableNameDB();
	bool hasError = databaseLayer->RunQuery("DELETE FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';") == DATABASE_LAYER_QUERY_RESULT_ERROR;

	//table parts
	for (auto table : m_aObjectTables) {
		if (!table->DeleteDataInDB()) {
			hasError = true; break;
		}
	}

	return !hasError;
}

#include "metadata/metadata.h"
#include "compiler/systemObjects.h"

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

CValue IDataObjectRefValue::CCodeGenerator::GenerateCode() const
{
	wxASSERT(m_metaAttribute);
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString fieldName = m_metaAttribute->GetFieldNameDB();

	DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults("SELECT %s FROM %s ORDER BY %s;", fieldName, tableName, fieldName);
	if (!resultSet) {
		wxString className = metadata->GetNameObjectFromID(
			m_metaAttribute->GetClassTypeObject()
		);
		return metadata->CreateObject(className);
	}

	number_t code = 1;
	if (m_metaAttribute->GetTypeObject() == eValueTypes::TYPE_NUMBER) {

		while (resultSet->Next()) {
			number_t fieldCode = resultSet->GetResultNumber(fieldName);
			if (fieldCode == code) {
				code++;
			}
		}
		resultSet->Close();
		return code;
	}
	else if (m_metaAttribute->GetTypeObject() == eValueTypes::TYPE_STRING) {

		ttmath::Conv conv;

		conv.precision = m_metaAttribute->GetLength();
		conv.leading_zero = true;
		wxString strCode = code.ToString(conv);

		while (resultSet->Next()) {
			wxString fieldCode = resultSet->GetResultString(fieldName);
			if (fieldCode == strCode) {
				code++;
				strCode = code.ToString(conv);
			}
		}
		resultSet->Close();
		return strCode;
	}

	wxASSERT_MSG(false, "m_metaAttribute->GetTypeObject() != eValueTypes::TYPE_NUMBER"
		"|| m_metaAttribute->GetTypeObject() != eValueTypes::TYPE_STRING");

	return CValue();
}