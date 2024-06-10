////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base db reader/saver
////////////////////////////////////////////////////////////////////////////

#include "object.h"
#include "appData.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include <3rdparty/databaseLayer/databaseErrorCodes.h>
#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "utils/stringUtils.h"

//**********************************************************************************************************
//*                                          Common functions                                              *
//**********************************************************************************************************

wxString IMetaObjectRecordDataRef::GetTableNameDB() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format("%s%i",
		className, GetMetaID());
}

int IMetaObjectRecordDataRef::ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	return IMetaAttributeObject::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRecordDataRef::ProcessEnumeration(const wxString& tableName, const meta_identifier_t& id, CMetaEnumerationObject* srcEnum, CMetaEnumerationObject* dstEnum)
{
	int retCode = 1;
	//is null - create
	if (dstEnum == NULL) {
		PreparedStatement* prepareStatement = NULL;
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			prepareStatement = databaseLayer->PrepareStatement("INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) ON CONFLICT(UUID) DO UPDATE SET UUID = excluded.UUID, UUIDREF = excluded.UUIDREF;", tableName);
		else
			prepareStatement = databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) MATCHING(UUID);", tableName);
		if (prepareStatement == NULL)
			return 0;
		reference_t* reference_impl = new reference_t{ id, srcEnum->GetGuid() };
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		prepareStatement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		retCode = prepareStatement->RunQuery();
		databaseLayer->CloseStatement(prepareStatement);
		delete reference_impl;
	}
	// update 
	else if (srcEnum != NULL) {
		PreparedStatement* prepareStatement = NULL;
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			prepareStatement = databaseLayer->PrepareStatement("INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) ON CONFLICT(UUID) DO UPDATE SET UUID = excluded.UUID, UUIDREF = excluded.UUIDREF;", tableName);
		else
			prepareStatement = databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (UUID, UUIDREF) VALUES (?, ?) MATCHING(UUID);", tableName);
		if (prepareStatement == NULL)
			return 0;
		reference_t* reference_impl = new reference_t{ id, srcEnum->GetGuid() };
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		prepareStatement->SetParamBlob(2, reference_impl, sizeof(reference_t));
		retCode = prepareStatement->RunQuery();
		databaseLayer->CloseStatement(prepareStatement);
		delete reference_impl;
	}
	//delete 
	else if (srcEnum == NULL) {
		retCode = databaseLayer->RunQuery("DELETE FROM %s WHERE UUID = '%s' ;", tableName, dstEnum->GetGuid().str());
	}

	return retCode;
}

int IMetaObjectRecordDataRef::ProcessTable(const wxString& tabularName, CMetaTableObject* srcTable, CMetaTableObject* dstTable)
{
	int retCode = 1;
	//is null - create
	if (dstTable == NULL) {
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL, UUIDREF BYTEA);", tabularName);
		else
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL, UUIDREF BLOB);", tabularName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		//default attributes
		for (auto attribute : srcTable->GetGenericAttributes()) {
			retCode = ProcessAttribute(tabularName,
				attribute, NULL);
		}
	}
	// update 
	else if (srcTable != NULL) {
		for (auto attribute : srcTable->GetGenericAttributes()) {
			retCode = ProcessAttribute(tabularName,
				attribute, dstTable->FindAttributeByGuid(attribute->GetDocPath())
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

bool IMetaObjectRecordDataRef::CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB(); int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL PRIMARY KEY, UUIDREF BYTEA);", tableName);
		else
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (UUID VARCHAR(36) NOT NULL PRIMARY KEY, UUIDREF BLOB);", tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s (UUID);", tableName, tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto attribute : GetDefaultAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			if (IMetaObjectRecordDataRef::IsDataReference(attribute->GetMetaID()))
				continue;
			retCode = ProcessAttribute(tableName,
				attribute, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto attribute : GetObjectAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				attribute, NULL);
		}

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
		IMetaObjectRecordDataRef* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			//attributes from dst 
			for (auto attribute : dstValue->GetDefaultAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindDefAttributeByGuid(attribute->GetDocPath());
				if (dstValue->IsDataReference(attribute->GetMetaID()))
					continue;
				if (foundedMeta == NULL) {
					retCode = ProcessAttribute(tableName, NULL, attribute);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//attributes current
			for (auto attribute : GetDefaultAttributes()) {
				if (IMetaObjectRecordDataRef::IsDataReference(attribute->GetMetaID()))
					continue;
				retCode = ProcessAttribute(tableName,
					attribute, dstValue->FindDefAttributeByGuid(attribute->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (auto attribute : dstValue->GetObjectAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindAttributeByGuid(attribute->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessAttribute(tableName, NULL, attribute);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//attributes current
			for (auto attribute : GetObjectAttributes()) {
				retCode = ProcessAttribute(tableName,
					attribute, dstValue->FindAttributeByGuid(attribute->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//enums from dst 
			for (auto enumeration : dstValue->GetObjectEnums()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindEnumByGuid(enumeration->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessEnumeration(tableName, m_metaId, NULL, enumeration);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//enums current
			for (auto enumeration : GetObjectEnums()) {
				retCode = ProcessEnumeration(tableName, m_metaId,
					enumeration, dstValue->FindEnumByGuid(enumeration->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//tables from dst 
			for (auto table : dstValue->GetObjectTables()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindTableByGuid(table->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessTable(table->GetTableNameDB(), NULL, table);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//tables current 
			for (auto table : GetObjectTables()) {
				retCode = ProcessTable(table->GetTableNameDB(),
					table, dstValue->FindTableByGuid(table->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		if (databaseLayer->TableExists(tableName)) {
			retCode = databaseLayer->RunQuery("DROP TABLE %s", tableName);
		}
		for (auto table : GetObjectTables()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			wxString tabularName = table->GetTableNameDB();
			if (databaseLayer->TableExists(tabularName)) {
				retCode = databaseLayer->RunQuery("DROP TABLE %s", tabularName);
			}
		}
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

wxString IMetaObjectRegisterData::GetTableNameDB() const
{
	const wxString& className = GetClassName();
	wxASSERT(m_metaId != 0);
	return wxString::Format("%s%i",
		className, GetMetaID());
}

int IMetaObjectRegisterData::ProcessDimension(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	return IMetaAttributeObject::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRegisterData::ProcessResource(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	return IMetaAttributeObject::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRegisterData::ProcessAttribute(const wxString& tableName, IMetaAttributeObject* srcAttr, IMetaAttributeObject* dstAttr)
{
	return IMetaAttributeObject::ProcessAttribute(tableName, srcAttr, dstAttr);
}

////////////////////////////////////////////////////////////////////////////////////////

bool IMetaObjectRegisterData::UpdateCurrentRecords(const wxString& tableName, IMetaObjectRegisterData* dst)
{
	if (HasRecorder()) {
		CMetaDefaultAttributeObject* metaRec = dst->GetRegisterRecorder();
		if (metaRec == NULL)
			return false;
		for (auto clsid : metaRec->GetClsids()) {
			if (!m_attributeRecorder->ContainType(clsid)) {
				int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR; wxString clsStr; clsStr << wxLongLong_t(clsid);
				retCode = databaseLayer->RunQuery("DELETE FROM %s WHERE %s_RTRef = " + clsStr, tableName, metaRec->GetFieldNameDB());
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////

bool IMetaObjectRegisterData::CreateAndUpdateTableDB(IConfigMetadata* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB();

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (rowData BYTEA);", tableName);
		else
			retCode = databaseLayer->RunQuery("CREATE TABLE %s (rowData BLOB);", tableName);

		for (auto attribute : GetDefaultAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				attribute, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto dimension : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				dimension, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto resource : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				resource, NULL);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto attribute : GetObjectAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				attribute, NULL);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = NULL;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;

			//attributes from dst 
			for (auto attribute : dstValue->GetDefaultAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDefAttributeByGuid(attribute->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessAttribute(tableName, NULL, attribute);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (auto attribute : GetDefaultAttributes()) {
				retCode = ProcessAttribute(tableName,
					attribute, dstValue->FindDefAttributeByGuid(attribute->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//dimensions from dst 
			for (auto dimension : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByGuid(dimension->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessDimension(tableName, NULL, dimension);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto dimension : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					dimension, dstValue->FindDimensionByGuid(dimension->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto resource : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByGuid(resource->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessResource(tableName, NULL, resource);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto resource : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					resource, dstValue->FindResourceByGuid(resource->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (auto attribute : dstValue->GetObjectAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindAttributeByGuid(attribute->GetDocPath());
				if (foundedMeta == NULL) {
					retCode = ProcessAttribute(tableName, NULL, attribute);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (auto attribute : GetObjectAttributes()) {
				retCode = ProcessAttribute(tableName,
					attribute, dstValue->FindAttributeByGuid(attribute->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = databaseLayer->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

//**********************************************************************************************************
//*                                          Query functions                                               *
//**********************************************************************************************************

#include "core/compiler/systemObjects.h"

bool IRecordDataObjectRef::ReadData()
{
	return ReadData(m_objGuid);
}

bool IRecordDataObjectRef::ReadData(const Guid& srcGuid)
{
	if (m_newObject && !srcGuid.isValid())
		return false;
	wxASSERT(m_metaObject);
	wxString tableName = m_metaObject->GetTableNameDB();
	if (databaseLayer->TableExists(tableName)) {

		DatabaseResultSet* resultSet = NULL;
		if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM " + tableName + " WHERE UUID = '" + srcGuid.str() + "' LIMIT 1;");
		else
			resultSet = databaseLayer->RunQueryWithResults("SELECT FIRST 1 * FROM " + tableName + " WHERE UUID = '" + srcGuid.str() + "';");

		if (resultSet == NULL)
			return false;
		bool succes = true;
		if (resultSet->Next()) {
			//load other attributes 
			for (auto attribute : m_metaObject->GetGenericAttributes()) {
				IMetaAttributeObject::GetValueAttribute(attribute, m_objectValues[attribute->GetMetaID()], resultSet);
			}
			for (auto table : m_metaObject->GetObjectTables()) {
				CTabularSectionDataObjectRef* tabularSection = new CTabularSectionDataObjectRef(this, table);
				if (!tabularSection->LoadData(srcGuid))
					succes = false;
				m_objectValues.insert_or_assign(table->GetMetaID(), tabularSection);
			}
		}
		resultSet->Close();
		return succes;
	}
	return false;
}

#include "core/compiler/systemObjects.h"

bool IRecordDataObjectRef::SaveData()
{
	wxASSERT(m_metaObject);

	if (m_codeGenerator != NULL) {
		if (m_objectValues[m_codeGenerator->GetMetaID()].IsEmpty()) {
			m_objectValues[m_codeGenerator->GetMetaID()] = m_codeGenerator->GenerateCode();
		}
	}

	//check fill attributes 
	bool fillCheck = true;

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (attribute->FillCheck()) {
			if (m_objectValues[attribute->GetMetaID()].IsEmpty()) {
				wxString fillError =
					wxString::Format(_("""%s"" is a required field"), attribute->GetSynonym());
				CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
				fillCheck = false;
			}
		}
	}

	if (!fillCheck)
		return false;

	if (!IRecordDataObjectRef::DeleteData())
		return false;

	const wxString& tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "INSERT INTO " + tableName + " (";
	queryText += "UUID, UUIDREF";
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(attribute->GetMetaID()))
			continue;
		queryText = queryText + ", " + IMetaAttributeObject::GetSQLFieldName(attribute);
	}
	queryText += ") VALUES (?, ?";
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(attribute->GetMetaID()))
			continue;
		unsigned int fieldCount = IMetaAttributeObject::GetSQLFieldCount(attribute);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += ", ?";
		}
	}
	queryText += ");";

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;

	m_objGuid = m_reference_impl->m_guid;

	statement->SetParamString(1, m_objGuid.str());
	statement->SetParamBlob(2, m_reference_impl, sizeof(reference_t));

	int position = 3;

	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_objectValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	bool hasError =
		statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;

	databaseLayer->CloseStatement(statement);

	//table parts
	if (!hasError) {
		for (auto table : m_metaObject->GetObjectTables()) {
			ITabularSectionDataObject* tabularSection = NULL;
			if (m_objectValues[table->GetMetaID()].ConvertToValue(tabularSection)) {
				if (!tabularSection->SaveData()) {
					hasError = true;
					break;
				}
			}
			else if (m_objectValues[table->GetMetaID()].GetType() != TYPE_NULL) {
				hasError = true;
				break;
			}
		}
	}

	if (!hasError) {
		m_newObject = false;
	}

	return !hasError;
}

bool IRecordDataObjectRef::DeleteData()
{
	if (m_newObject)
		return true;
	const wxString& tableName = m_metaObject->GetTableNameDB();
	//table parts
	for (auto table : m_metaObject->GetObjectTables()) {
		ITabularSectionDataObject* tabularSection = NULL;
		if (m_objectValues[table->GetMetaID()].ConvertToValue(tabularSection)) {
			if (!tabularSection->DeleteData())
				return false;
		}
		else if (m_objectValues[table->GetMetaID()].GetType() != TYPE_NULL)
			return false;

	}
	databaseLayer->RunQuery("DELETE FROM " + tableName + " WHERE UUID = '" + m_objGuid.str() + "';");
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IRecordDataObjectFolderRef::ReadData()
{
	if (IRecordDataObjectRef::ReadData()) {
		IMetaObjectRecordDataFolderMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		CValue isFolder; IRecordDataObjectFolderRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = eObjectMode::OBJECT_FOLDER;
		else
			m_objMode = eObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

bool IRecordDataObjectFolderRef::ReadData(const Guid& srcGuid)
{
	if (IRecordDataObjectRef::ReadData(srcGuid)) {
		IMetaObjectRecordDataFolderMutableRef* metaFolder = GetMetaObject();
		wxASSERT(metaFolder);
		CValue isFolder; IRecordDataObjectFolderRef::GetValueByMetaID(*metaFolder->GetDataIsFolder(), isFolder);
		if (isFolder.GetBoolean())
			m_objMode = eObjectMode::OBJECT_FOLDER;
		else
			m_objMode = eObjectMode::OBJECT_ITEM;
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

void IRecordDataObjectRef::SetDeletionMark(bool deletionMark)
{
	if (m_newObject)
		return;

	if (m_metaObject != NULL) {
		CMetaDefaultAttributeObject* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
		wxASSERT(attributeDeletionMark);
		IRecordDataObjectRef::SetValueByMetaID(*attributeDeletionMark, deletionMark);
	}

	SaveModify();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IRecordManagerObject::ExistData()
{
	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	if (m_recordLine == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;
	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		CValue retValue; m_recordLine->GetValueByMetaID(attribute->GetMetaID(), retValue);
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			retValue,
			statement,
			position
		);
	}
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == NULL)
		return false;
	bool founded = false;
	if (resultSet->Next())
		founded = true;
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return founded;
}

bool IRecordManagerObject::ReadData()
{
	if (m_recordSet->ReadData()) {
		if (m_recordLine == NULL) {
			m_recordLine = m_recordSet->GetRowAt(
				m_recordSet->GetItem(0)
			);
		}
		return true;
	}

	return false;
}

bool IRecordManagerObject::SaveData(bool replace)
{
	if (m_recordSet->Selected()
		&& !DeleteData())
		return false;

	if (ExistData())
		return false;

	m_recordSet->m_keyValues.clear();
	wxASSERT(m_recordLine);
	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		CValue retValue; m_recordLine->GetValueByMetaID(attribute->GetMetaID(), retValue);
		m_recordSet->m_keyValues.insert_or_assign(
			attribute->GetMetaID(), retValue
		);
	}
	if (m_recordSet->WriteRecordSet(replace, false)) {
		m_objGuid.SetKeyPair(m_metaObject, m_recordSet->m_keyValues);
		return true;
	}
	return false;
}

bool IRecordManagerObject::DeleteData()
{
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::ExistData()
{
	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == NULL)
		return false;
	bool founded = false;
	if (resultSet->Next())
		founded = true;
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return founded;
}

bool IRecordSetObject::ExistData(number_t& lastNum)
{
	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == NULL)
		return false;
	bool founded = false; lastNum = 1;
	while (resultSet->Next()) {
		CValue numLine; IMetaAttributeObject::GetValueAttribute(m_metaObject->GetRegisterLineNumber(), numLine, resultSet);
		if (numLine > lastNum) {
			lastNum = numLine.GetNumber();
		}
		founded = true;
	}
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return founded;
}

bool IRecordSetObject::ReadData()
{
	IValueTable::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);
	if (statement == NULL)
		return false;
	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}
	DatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == NULL)
		return false;
	while (resultSet->Next()) {
		wxValueTableRow* rowData = new wxValueTableRow();
		for (auto attribute : m_metaObject->GetGenericDimensions()) {
			IMetaAttributeObject::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			IMetaAttributeObject::GetValueAttribute(attribute, rowData->AppendTableValue(attribute->GetMetaID()), resultSet);
		}
		IValueTable::Append(rowData, !CTranslateError::IsSimpleMode());
		m_selected = true;
	}

	resultSet->Close();
	databaseLayer->CloseResultSet(resultSet);
	databaseLayer->CloseStatement(statement);
	return GetRowCount() > 0;
}

bool IRecordSetObject::SaveData(bool replace, bool clearTable)
{
	bool hasError = false;

	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			if (attribute->FillCheck()) {
				wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(attribute->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s"), attribute->GetSynonym(), currLine, m_metaObject->GetSynonym());
					CSystemObjects::Message(fillError, eStatusMessage::eStatusMessage_Information);
					fillCheck = false;
				}
			}
		}
		currLine++;
	}

	if (!fillCheck)
		return false;

	number_t numberLine = 1, oldNumberLine = 1;

	if (m_metaObject->HasRecorder() &&
		IRecordSetObject::ExistData(oldNumberLine)) {
		if (replace && !IRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}
	else if (IRecordSetObject::ExistData()) {
		if (replace && !IRecordSetObject::DeleteData())
			return false;
		if (!replace) {
			numberLine = oldNumberLine;
		}
	}

	wxString tableName = m_metaObject->GetTableNameDB(); bool firstUpdate = true;
	wxString queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		queryText += (firstUpdate ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(attribute);
		if (firstUpdate) {
			firstUpdate = false;
		}
	}
	queryText += ") VALUES ("; bool firstInsert = true;
	for (auto attribute : m_metaObject->GetGenericAttributes()) {
		unsigned int fieldCount = IMetaAttributeObject::GetSQLFieldCount(attribute);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += (firstInsert ? "?" : ",?");
			if (firstInsert) {
				firstInsert = false;
			}
		}
	}

	queryText += ") MATCHING (";

	if (m_metaObject->HasRecorder()) {
		CMetaDefaultAttributeObject* attributeRecorder = m_metaObject->GetRegisterRecorder();
		wxASSERT(attributeRecorder);
		queryText += IMetaAttributeObject::GetSQLFieldName(attributeRecorder);
		CMetaDefaultAttributeObject* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
		wxASSERT(attributeNumberLine);
		queryText += "," + IMetaAttributeObject::GetSQLFieldName(attributeNumberLine);
	}
	else
	{
		bool firstMatching = true;
		for (auto attribute : m_metaObject->GetGenericDimensions()) {
			queryText += (firstMatching ? "" : ",") + IMetaAttributeObject::GetSQLFieldName(attribute);
			if (firstMatching) {
				firstMatching = false;
			}
		}
	}

	queryText += ");";

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText);

	if (statement == NULL)
		return false;

	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 1;
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			auto foundedKey = m_keyValues.find(attribute->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					foundedKey->second,
					statement,
					position
				);
			}
			else if (m_metaObject->IsRegisterLineNumber(attribute->GetMetaID())) {
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					numberLine++,
					statement,
					position
				);
			}
			else {
				wxValueTableRow* node = GetViewData< wxValueTableRow>(GetItem(row));
				wxASSERT(node);
				IMetaAttributeObject::SetValueAttribute(
					attribute,
					node->GetTableValue(attribute->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	databaseLayer->CloseStatement(statement);

	if (!hasError && !SaveVirtualTable())
		return false;

	if (!hasError && clearTable)
		IValueTable::Clear();
	else if (!clearTable)
		m_selected = true;

	return !hasError;
}

bool IRecordSetObject::DeleteData()
{
	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;
	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaAttributeObject::GetCompositeSQLFieldName(attribute);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	PreparedStatement* statement = databaseLayer->PrepareStatement(queryText); int position = 1;

	if (statement == NULL)
		return false;

	for (auto attribute : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(attribute->GetMetaID()))
			continue;
		IMetaAttributeObject::SetValueAttribute(
			attribute,
			m_keyValues.at(attribute->GetMetaID()),
			statement,
			position
		);
	}

	statement->RunQuery();
	databaseLayer->CloseStatement(statement);
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

#include "core/metadata/metadata.h"

CValue IRecordDataObjectRef::CCodeGenerator::GenerateCode() const
{
	wxASSERT(m_metaAttribute);

	const wxString& tableName = m_metaObject->GetTableNameDB();
	const wxString& fieldName = m_metaAttribute->GetFieldNameDB();

	IMetadata* metaData = m_metaObject->GetMetadata();
	wxASSERT(metaData);

	DatabaseResultSet* resultSet = databaseLayer->RunQueryWithResults("SELECT %s FROM %s ORDER BY %s FOR UPDATE;",
		IMetaAttributeObject::GetSQLFieldName(m_metaAttribute),
		tableName,
		IMetaAttributeObject::GetSQLFieldName(m_metaAttribute)
	);

	if (resultSet == NULL)
		return m_metaAttribute->CreateValue();

	number_t code = 1; CValue fieldCode;

	if (m_metaAttribute->ContainType(eValueTypes::TYPE_NUMBER)) {
		while (resultSet->Next()) {
			IMetaAttributeObject::GetValueAttribute(m_metaAttribute, fieldCode, resultSet);
			if (fieldCode.GetNumber() == code) {
				code++;
			}
		}
		resultSet->Close();
		return code;
	}
	else if (m_metaAttribute->ContainType(eValueTypes::TYPE_STRING)) {
		ttmath::Conv conv;
		conv.precision = m_metaAttribute->GetLength();
		conv.leading_zero = true;	
		wxString strCode = code.ToString(conv);
		while (resultSet->Next()) {
			IMetaAttributeObject::GetValueAttribute(m_metaAttribute, fieldCode, resultSet);
			if (fieldCode.GetString() == strCode) {
				code++;
				strCode = code.ToString(conv);
			}
		}
		resultSet->Close();
		return strCode;
	}

	wxASSERT_MSG(false, "m_metaAttribute->GetClsids() != eValueTypes::TYPE_NUMBER"
		"|| m_metaAttribute->GetClsids() != eValueTypes::TYPE_STRING");

	return code;
}