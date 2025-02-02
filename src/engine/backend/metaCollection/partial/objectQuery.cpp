////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : base db reader/saver
////////////////////////////////////////////////////////////////////////////

#include "object.h"
#include "backend/appData.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"


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

int IMetaObjectRecordDataRef::ProcessAttribute(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr)
{
	return IMetaObjectAttribute::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRecordDataRef::ProcessEnumeration(const wxString& tableName, CMetaObjectEnum* srcEnum, CMetaObjectEnum* dstEnum)
{
	int retCode = 1;
	//is null - create
	if (dstEnum == nullptr) {
		IPreparedStatement* prepareStatement = nullptr;

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			prepareStatement = db_query->PrepareStatement("INSERT INTO %s (_uuid) VALUES (?) ON CONFLICT(_uuid) DO UPDATE SET _uuid = excluded._uuid;", tableName);
		else
			prepareStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s (_uuid) VALUES (?) MATCHING(_uuid);", tableName);

		if (prepareStatement == nullptr)
			return 0;
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		retCode = prepareStatement->RunQuery();
		db_query->CloseStatement(prepareStatement);
	}
	// update 
	else if (srcEnum != nullptr) {
		IPreparedStatement* prepareStatement = nullptr;
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			prepareStatement = db_query->PrepareStatement("INSERT INTO %s (_uuid) VALUES (?) ON CONFLICT(_uuid) DO UPDATE SET _uuid = excluded._uuid;", tableName);
		else
			prepareStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s (_uuid) VALUES (?) MATCHING(_uuid);", tableName);

		if (prepareStatement == nullptr)
			return 0;
		prepareStatement->SetParamString(1, srcEnum->GetGuid().str());
		retCode = prepareStatement->RunQuery();
		db_query->CloseStatement(prepareStatement);
	}
	//delete 
	else if (srcEnum == nullptr) {
		retCode = db_query->RunQuery("DELETE FROM %s WHERE _uuid = '%s' ;", tableName, dstEnum->GetGuid().str());
	}

	return retCode;
}

int IMetaObjectRecordDataRef::ProcessTable(const wxString& tabularName, CMetaObjectTable* srcTable, CMetaObjectTable* dstTable)
{
	int retCode = 1;
	//is null - create
	if (dstTable == nullptr) {
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery("CREATE TABLE %s (_uuid uuid NOT NULL);", tabularName);
		else
			retCode = db_query->RunQuery("CREATE TABLE %s (_uuid VARCHAR(36) NOT NULL);", tabularName);
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return retCode;
		//default attributes
		for (auto& obj : srcTable->GetGenericAttributes()) {
			retCode = ProcessAttribute(tabularName,
				obj, nullptr);
		}
	}
	// update 
	else if (srcTable != nullptr) {
		for (auto& obj : srcTable->GetGenericAttributes()) {
			retCode = ProcessAttribute(tabularName,
				obj, dstTable->FindAttributeByGuid(obj->GetDocPath())
			);
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return retCode;
		}
	}
	//delete 
	else if (srcTable == nullptr) {
		retCode = db_query->RunQuery("DROP TABLE %s", tabularName);
	}

	return retCode;
}

#include <fstream>

bool IMetaObjectRecordDataRef::CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB(); int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery("CREATE TABLE %s (_uuid uuid NOT NULL PRIMARY KEY);", tableName);
		else
			retCode = db_query->RunQuery("CREATE TABLE %s (_uuid VARCHAR(36) NOT NULL PRIMARY KEY);", tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		retCode = db_query->RunQuery("CREATE INDEX %s_INDEX ON %s (_uuid);", tableName, tableName);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetDefaultAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			if (IMetaObjectRecordDataRef::IsDataReference(obj->GetMetaID()))
				continue;
			retCode = ProcessAttribute(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectEnums()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessEnumeration(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectTables()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessTable(obj->GetTableNameDB(),
				obj, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRecordDataRef* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			//attributes from dst 
			for (auto& obj : dstValue->GetDefaultAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindDefAttributeByGuid(obj->GetDocPath());
				if (dstValue->IsDataReference(obj->GetMetaID()))
					continue;
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//attributes current
			for (auto& obj : GetDefaultAttributes()) {
				if (IMetaObjectRecordDataRef::IsDataReference(obj->GetMetaID()))
					continue;
				retCode = ProcessAttribute(tableName,
					obj, dstValue->FindDefAttributeByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (auto& obj : dstValue->GetObjectAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindAttributeByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//attributes current
			for (auto& obj : GetObjectAttributes()) {
				retCode = ProcessAttribute(tableName,
					obj, dstValue->FindAttributeByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//enums from dst 
			for (auto& obj : dstValue->GetObjectEnums()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindEnumByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessEnumeration(tableName,
						nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//enums current
			for (auto& obj : GetObjectEnums()) {
				retCode = ProcessEnumeration(tableName,
					obj, dstValue->FindEnumByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//tables from dst 
			for (auto& obj : dstValue->GetObjectTables()) {
				IMetaObject* foundedMeta =
					IMetaObjectRecordDataRef::FindTableByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessTable(obj->GetTableNameDB(), nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}
			//tables current 
			for (auto& obj : GetObjectTables()) {
				retCode = ProcessTable(obj->GetTableNameDB(),
					obj, dstValue->FindTableByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		if (db_query->TableExists(tableName)) {
			retCode = db_query->RunQuery("DROP TABLE %s", tableName);
		}
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;
		for (auto& obj : GetObjectTables()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			const wxString& tabularName = obj->GetTableNameDB();
			if (db_query->TableExists(tabularName)) {
				retCode = db_query->RunQuery("DROP TABLE %s", tabularName);
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

int IMetaObjectRegisterData::ProcessDimension(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr)
{
	return IMetaObjectAttribute::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRegisterData::ProcessResource(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr)
{
	return IMetaObjectAttribute::ProcessAttribute(tableName, srcAttr, dstAttr);
}

int IMetaObjectRegisterData::ProcessAttribute(const wxString& tableName, IMetaObjectAttribute* srcAttr, IMetaObjectAttribute* dstAttr)
{
	return IMetaObjectAttribute::ProcessAttribute(tableName, srcAttr, dstAttr);
}

////////////////////////////////////////////////////////////////////////////////////////

bool IMetaObjectRegisterData::UpdateCurrentRecords(const wxString& tableName, IMetaObjectRegisterData* dst)
{
	if (HasRecorder()) {
		CMetaObjectAttributeDefault* metaRec = dst->GetRegisterRecorder();
		if (metaRec == nullptr)
			return false;
		for (auto& clsid : metaRec->GetClsids()) {
			if (!m_attributeRecorder->ContainType(clsid)) {
				int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR; wxString clsStr; clsStr << wxLongLong_t(clsid);
				retCode = db_query->RunQuery("DELETE FROM %s WHERE %s_RTRef = " + clsStr, tableName, metaRec->GetFieldNameDB());
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////

bool IMetaObjectRegisterData::CreateAndUpdateTableDB(IMetaDataConfiguration* srcMetaData, IMetaObject* srcMetaObject, int flags)
{
	const wxString& tableName = GetTableNameDB();

	int retCode = DATABASE_LAYER_QUERY_RESULT_ERROR;

	if ((flags & createMetaTable) != 0) {

		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			retCode = db_query->RunQuery("CREATE TABLE %s (rowData BYTEA);", tableName);
		else
			retCode = db_query->RunQuery("CREATE TABLE %s (rowData BLOB);", tableName);

		for (auto& obj : GetDefaultAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectDimensions()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessDimension(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectResources()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessResource(tableName,
				obj, nullptr);
		}

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
			return false;

		for (auto& obj : GetObjectAttributes()) {
			if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
				return false;
			retCode = ProcessAttribute(tableName,
				obj, nullptr);
		}
	}
	else if ((flags & updateMetaTable) != 0) {

		//if src is null then delete
		IMetaObjectRegisterData* dstValue = nullptr;
		if (srcMetaObject->ConvertToValue(dstValue)) {

			if (!UpdateCurrentRecords(tableName, dstValue))
				return false;

			//attributes from dst 
			for (auto& obj : dstValue->GetDefaultAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDefAttributeByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (auto& obj : GetDefaultAttributes()) {
				retCode = ProcessAttribute(tableName,
					obj, dstValue->FindDefAttributeByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//dimensions from dst 
			for (auto& obj : dstValue->GetObjectDimensions()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindDimensionByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessDimension(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//dimensions current
			for (auto& obj : GetObjectDimensions()) {
				retCode = ProcessDimension(tableName,
					obj, dstValue->FindDimensionByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//resources from dst 
			for (auto& obj : dstValue->GetObjectResources()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindResourceByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessResource(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//resources current
			for (auto& obj : GetObjectResources()) {
				retCode = ProcessResource(tableName,
					obj, dstValue->FindResourceByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}

			//attributes from dst 
			for (auto& obj : dstValue->GetObjectAttributes()) {
				IMetaObject* foundedMeta =
					IMetaObjectRegisterData::FindAttributeByGuid(obj->GetDocPath());
				if (foundedMeta == nullptr) {
					retCode = ProcessAttribute(tableName, nullptr, obj);
					if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
						return false;
				}
			}

			//attributes current
			for (auto& obj : GetObjectAttributes()) {
				retCode = ProcessAttribute(tableName,
					obj, dstValue->FindAttributeByGuid(obj->GetDocPath())
				);
				if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
					return false;
			}
		}
	}
	else if ((flags & deleteMetaTable) != 0) {
		retCode = db_query->RunQuery("DROP TABLE %s", tableName);
	}

	if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	return true;
}

//**********************************************************************************************************
//*                                          Query functions                                               *
//**********************************************************************************************************

#include "backend/systemManager/systemManager.h"

bool IRecordDataObjectRef::ReadData()
{
	return ReadData(m_objGuid);
}

bool IRecordDataObjectRef::ReadData(const Guid& srcGuid)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	if (m_newObject && !srcGuid.isValid())
		return false;
	wxASSERT(m_metaObject);
	wxString tableName = m_metaObject->GetTableNameDB();
	if (db_query->TableExists(tableName)) {

		IDatabaseResultSet* resultSet = nullptr;
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
			resultSet = db_query->RunQueryWithResults("SELECT * FROM " + tableName + " WHERE _uuid = '" + srcGuid.str() + "' LIMIT 1;");
		else
			resultSet = db_query->RunQueryWithResults("SELECT FIRST 1 * FROM " + tableName + " WHERE _uuid = '" + srcGuid.str() + "';");

		if (resultSet == nullptr)
			return false;
		bool succes = true;
		if (resultSet->Next()) {
			//load other attributes 
			for (auto& obj : m_metaObject->GetGenericAttributes()) {
				if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
					IMetaObjectAttribute::GetValueAttribute(obj, m_objectValues[obj->GetMetaID()], resultSet);
				}
			}
			for (auto& obj : m_metaObject->GetObjectTables()) {
				CTabularSectionDataObjectRef* tabularSection = new CTabularSectionDataObjectRef(this, obj);
				if (!tabularSection->LoadData(srcGuid))
					succes = false;
				m_objectValues.insert_or_assign(obj->GetMetaID(), tabularSection);
			}
		}
		resultSet->Close();
		return succes;
	}
	return false;
}

bool IRecordDataObjectRef::SaveData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	if (m_codeGenerator != nullptr) {
		if (m_objectValues[m_codeGenerator->GetMetaID()].IsEmpty()) {
			m_objectValues[m_codeGenerator->GetMetaID()] = m_codeGenerator->GenerateCode();
		}
	}

	//check fill attributes 
	bool fillCheck = true;
	wxASSERT(m_metaObject);
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (obj->FillCheck()) {
			if (m_objectValues[obj->GetMetaID()].IsEmpty()) {
				wxString fillError =
					wxString::Format(_("""%s"" is a required field"), obj->GetSynonym());
				CSystemFunction::Message(fillError, eStatusMessage::eStatusMessage_Information);
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
	queryText += "_uuid";
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(obj->GetMetaID()))
			continue;
		queryText = queryText + ", " + IMetaObjectAttribute::GetSQLFieldName(obj);
	}
	queryText += ") VALUES (?";
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(obj->GetMetaID()))
			continue;
		unsigned int fieldCount = IMetaObjectAttribute::GetSQLFieldCount(obj);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += ", ?";
		}
	}
	queryText += ");";

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	m_objGuid = m_reference_impl->m_guid;
	statement->SetParamString(1, m_objGuid.str());

	int position = 2;

	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		if (m_metaObject->IsDataReference(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_objectValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}

	bool hasError =
		statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;

	db_query->CloseStatement(statement);

	//table parts
	if (!hasError) {
		for (auto& obj : m_metaObject->GetObjectTables()) {
			ITabularSectionDataObject* tabularSection = nullptr;
			if (m_objectValues[obj->GetMetaID()].ConvertToValue(tabularSection)) {
				if (!tabularSection->SaveData()) {
					hasError = true;
					break;
				}
			}
			else if (m_objectValues[obj->GetMetaID()].GetType() != TYPE_NULL) {
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
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	if (m_newObject)
		return true;
	const wxString& tableName = m_metaObject->GetTableNameDB();
	//table parts
	for (auto& obj : m_metaObject->GetObjectTables()) {
		ITabularSectionDataObject* tabularSection = nullptr;
		if (m_objectValues[obj->GetMetaID()].ConvertToValue(tabularSection)) {
			if (!tabularSection->DeleteData())
				return false;
		}
		else if (m_objectValues[obj->GetMetaID()].GetType() != TYPE_NULL)
			return false;

	}
	db_query->RunQuery("DELETE FROM " + tableName + " WHERE _uuid = '" + m_objGuid.str() + "';");
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

	if (m_metaObject != nullptr) {
		CMetaObjectAttributeDefault* attributeDeletionMark = m_metaObject->GetDataDeletionMark();
		wxASSERT(attributeDeletionMark);
		IRecordDataObjectRef::SetValueByMetaID(*attributeDeletionMark, deletionMark);
	}

	SaveModify();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IRecordManagerObject::ExistData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	if (m_recordLine == nullptr)
		return false;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;
	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		CValue retValue; m_recordLine->GetValueByMetaID(obj->GetMetaID(), retValue);
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			retValue,
			statement,
			position
		);
	}
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false;
	if (resultSet->Next())
		founded = true;
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return founded;
}

bool IRecordManagerObject::ReadData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	if (m_recordSet->ReadData()) {
		if (m_recordLine == nullptr) {
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
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	if (m_recordSet->Selected()
		&& !DeleteData())
		return false;

	if (ExistData())
		return false;

	m_recordSet->m_keyValues.clear();
	wxASSERT(m_recordLine);
	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		CValue retValue; m_recordLine->GetValueByMetaID(obj->GetMetaID(), retValue);
		m_recordSet->m_keyValues.insert_or_assign(
			obj->GetMetaID(), retValue
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
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));
	return m_recordSet->DeleteRecordSet();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IRecordSetObject::ExistData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_keyValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false;
	if (resultSet->Next())
		founded = true;
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return founded;
}

bool IRecordSetObject::ExistData(number_t& lastNum)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB(); int position = 1;
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_keyValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}

	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	bool founded = false; lastNum = 1;
	while (resultSet->Next()) {
		CValue numLine; IMetaObjectAttribute::GetValueAttribute(m_metaObject->GetRegisterLineNumber(), numLine, resultSet);
		if (numLine > lastNum) {
			lastNum = numLine.GetNumber();
		}
		founded = true;
	}
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return founded;
}

bool IRecordSetObject::ReadData()
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	IValueTable::Clear(); int position = 1;

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "SELECT * FROM " + tableName; bool firstWhere = true;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
		if (firstWhere) {
			firstWhere = false;
		}
	}
	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;
	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_keyValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}
	IDatabaseResultSet* resultSet = statement->RunQueryWithResults();
	if (resultSet == nullptr)
		return false;
	while (resultSet->Next()) {
		wxValueTableRow* rowData = new wxValueTableRow();
		for (auto& obj : m_metaObject->GetGenericDimensions()) {
			IMetaObjectAttribute::GetValueAttribute(obj, rowData->AppendTableValue(obj->GetMetaID()), resultSet);
		}
		for (auto& obj : m_metaObject->GetGenericAttributes()) {
			IMetaObjectAttribute::GetValueAttribute(obj, rowData->AppendTableValue(obj->GetMetaID()), resultSet);
		}
		IValueTable::Append(rowData, !CBackendException::IsEvalMode());
		m_selected = true;
	}

	resultSet->Close();
	db_query->CloseResultSet(resultSet);
	db_query->CloseStatement(statement);
	return GetRowCount() > 0;
}

bool IRecordSetObject::SaveData(bool replace, bool clearTable)
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	//check fill attributes 
	bool fillCheck = true; long currLine = 1;
	for (long row = 0; row < GetRowCount(); row++) {
		for (auto& obj : m_metaObject->GetGenericAttributes()) {
			if (obj->FillCheck()) {
				wxValueTableRow* node = GetViewData<wxValueTableRow>(GetItem(row));
				wxASSERT(node);
				if (node->IsEmptyValue(obj->GetMetaID())) {
					wxString fillError =
						wxString::Format(_("The %s is required on line %i of the %s"), obj->GetSynonym(), currLine, m_metaObject->GetSynonym());
					CSystemFunction::Message(fillError, eStatusMessage::eStatusMessage_Information);
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

	wxString tableName = m_metaObject->GetTableNameDB(); wxString queryText; bool firstUpdate = true;
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		queryText = "INSERT INTO " + tableName + " (";
	}
	else {
		queryText = "UPDATE OR INSERT INTO " + tableName + " (";
	}
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		queryText += (firstUpdate ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
		if (firstUpdate) {
			firstUpdate = false;
		}
	}
	queryText += ") VALUES ("; bool firstInsert = true;
	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		unsigned int fieldCount = IMetaObjectAttribute::GetSQLFieldCount(obj);
		for (unsigned int i = 0; i < fieldCount; i++) {
			queryText += (firstInsert ? "?" : ",?");
			if (firstInsert) {
				firstInsert = false;
			}
		}
	}

	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
		queryText += ")";
	}
	else {
		queryText += ") MATCHING (";
		if (m_metaObject->HasRecorder()) {
			CMetaObjectAttributeDefault* attributeRecorder = m_metaObject->GetRegisterRecorder();
			wxASSERT(attributeRecorder);
			queryText += IMetaObjectAttribute::GetSQLFieldName(attributeRecorder);
			CMetaObjectAttributeDefault* attributeNumberLine = m_metaObject->GetRegisterLineNumber();
			wxASSERT(attributeNumberLine);
			queryText += "," + IMetaObjectAttribute::GetSQLFieldName(attributeNumberLine);
		}
		else
		{
			bool firstMatching = true;
			for (auto& obj : m_metaObject->GetGenericDimensions()) {
				queryText += (firstMatching ? "" : ",") + IMetaObjectAttribute::GetSQLFieldName(obj);
				if (firstMatching) {
					firstMatching = false;
				}
			}
		}
		queryText += ");";
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText);
	if (statement == nullptr)
		return false;

	bool hasError = false;

	for (long row = 0; row < GetRowCount(); row++) {
		if (hasError)
			break;
		int position = 1;
		for (auto& obj : m_metaObject->GetGenericAttributes()) {
			auto foundedKey = m_keyValues.find(obj->GetMetaID());
			if (foundedKey != m_keyValues.end()) {
				IMetaObjectAttribute::SetValueAttribute(
					obj,
					foundedKey->second,
					statement,
					position
				);
			}
			else if (m_metaObject->IsRegisterLineNumber(obj->GetMetaID())) {
				IMetaObjectAttribute::SetValueAttribute(
					obj,
					numberLine++,
					statement,
					position
				);
			}
			else {
				wxValueTableRow* node = GetViewData< wxValueTableRow>(GetItem(row));
				wxASSERT(node);
				IMetaObjectAttribute::SetValueAttribute(
					obj,
					node->GetTableValue(obj->GetMetaID()),
					statement,
					position
				);
			}
		}

		hasError = statement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR;
	}

	db_query->CloseStatement(statement);

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
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	wxString tableName = m_metaObject->GetTableNameDB();
	wxString queryText = "DELETE FROM " + tableName; bool firstWhere = true;
	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		if (firstWhere) {
			queryText = queryText + " WHERE ";
		}
		queryText = queryText +
			(firstWhere ? " " : " AND ") + IMetaObjectAttribute::GetCompositeSQLFieldName(obj);
		if (firstWhere) {
			firstWhere = false;
		}
	}

	IPreparedStatement* statement = db_query->PrepareStatement(queryText); int position = 1;

	if (statement == nullptr)
		return false;

	for (auto& obj : m_metaObject->GetGenericDimensions()) {
		if (!IRecordSetObject::FindKeyValue(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::SetValueAttribute(
			obj,
			m_keyValues.at(obj->GetMetaID()),
			statement,
			position
		);
	}

	statement->RunQuery();
	db_query->CloseStatement(statement);
	return DeleteVirtualTable();
}

//**********************************************************************************************************
//*                                          Code generator												   *
//**********************************************************************************************************

CValue IRecordDataObjectRef::CCodeGenerator::GenerateCode() const
{
	if (db_query != nullptr && !db_query->IsOpen())
		CBackendException::Error(_("database is not open!"));
	else if (db_query == nullptr)
		CBackendException::Error(_("database is not open!"));

	wxASSERT(m_metaAttribute);

	const wxString& tableName = m_metaObject->GetTableNameDB();
	const wxString& fieldName = m_metaAttribute->GetFieldNameDB();

	IMetaData* metaData = m_metaObject->GetMetaData();
	wxASSERT(metaData);

	//IDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT %s FROM %s ORDER BY %s FOR UPDATE;",
	IDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT %s FROM %s ORDER BY %s;",
		IMetaObjectAttribute::GetSQLFieldName(m_metaAttribute),
		tableName,
		IMetaObjectAttribute::GetSQLFieldName(m_metaAttribute)
	);

	if (resultSet == nullptr)
		return m_metaAttribute->CreateValue();

	number_t code = 1; CValue fieldCode;

	if (m_metaAttribute->ContainType(eValueTypes::TYPE_NUMBER)) {
		while (resultSet->Next()) {
			IMetaObjectAttribute::GetValueAttribute(m_metaAttribute, fieldCode, resultSet);
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
			IMetaObjectAttribute::GetValueAttribute(m_metaAttribute, fieldCode, resultSet);
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