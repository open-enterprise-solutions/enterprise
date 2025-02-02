#include "metadataConfiguration.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/utils/wxmd5.hpp"
#include "backend/appData.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////
#include <wx/base64.h>
//////////////////////////////////////////////////////////////////////////////////////////////////////
#define config_table	  wxT("_config")
#define config_save_table wxT("_config_save")
//////////////////////////////////////////////////////////////////////////////////////////////////////
#define config_name       wxT("sys.database")
//////////////////////////////////////////////////////////////////////////////////////////////////////

inline wxString GetCommonConfigTable(eConfigType cfg_type) {

	switch (cfg_type)
	{
	case eConfigType::eConfigType_Load:
		return config_table;
	case eConfigType::eConfigType_Load_And_Save:
		return config_save_table;
	}

	return wxEmptyString;
}

//**************************************************************************************************
//*                                          ConfigMetadata                                        *
//**************************************************************************************************

bool CMetaDataConfiguration::LoadConfiguration(int flags)
{
	//close if opened
	if (CMetaDataConfiguration::IsConfigOpen()) {
		if (!CloseConfiguration(forceCloseFlag)) {
			return false;
		}
	}

	//clear data 
	if (!ClearConfiguration()) {
		wxASSERT_MSG(false, "ClearConfiguration() == false");
		return false;
	}

	// load config
	IDatabaseResultSet* result = db_query->RunQueryWithResults("SELECT binary_data, file_guid FROM %s; ", GetCommonConfigTable(GetConfigType()));
	if (result == nullptr)
		return false;

	//load metadata from DB 
	if (result->Next()) {

		wxMemoryBuffer binaryData;
		result->GetResultBlob(wxT("binary_data"), binaryData);
		CMemoryReader metaReader(binaryData.GetData(), binaryData.GetBufSize());

		//check is file empty
		if (metaReader.eof())
			return false;

		//check metadata 
		if (!LoadHeader(metaReader))
			return false;

		//load metadata 
		if (!LoadCommonMetadata(g_metaCommonMetadataCLSID, metaReader))
			return false;

		m_configNew = false;

		m_metaGuid = result->GetResultString(wxT("file_guid"));
		m_md5Hash = wxMD5::ComputeMd5(wxBase64Encode(binaryData.GetData(), binaryData.GetDataLen()));
	}
	
	result->Close();	
	return true;
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

#include "metaCollection/partial/constant.h"

bool CMetaDataConfigurationStorage::SaveConfiguration(int flags)
{
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1	
	//begin transaction 
	db_query->BeginTransaction();
#endif 

	//remove old tables (if need)
	if ((flags & saveConfigFlag) != 0) {

		//Delete common object
		if (!DeleteCommonMetadata(g_metaCommonMetadataCLSID)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else 
			return false;
#endif
		}

		IMetaObject* commonObject = m_configMetadata->GetCommonMetaObject();
		wxASSERT(commonObject);

		for (auto& obj : commonObject->GetObjects()) {
			IMetaObject* foundedMeta =
				m_commonObject->FindByName(obj->GetDocPath());
			if (foundedMeta == nullptr) {
				bool ret = obj->DeleteMetaTable(this);
				if (!ret) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
					db_query->RollBack(); return false;
#else 
					return false;
#endif
				}
			}
		}
		
		//delete tables sql
		if (m_configNew && !CMetaObjectConstant::DeleteConstantSQLTable()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else 
			return false;
#endif
		}

		if (m_configNew && !CMetaObject::ExecuteSystemSQLCommand()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else 
			return false;
#endif
		}

		//create & update tables sql
		if (m_configNew && !CMetaObjectConstant::CreateConstantSQLTable()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else 
			return false;
#endif
		}

		for (auto& obj : m_commonObject->GetObjects()) {
			IMetaObject* foundedMeta =
				commonObject->FindByName(obj->GetDocPath());
			wxASSERT(obj);
			bool ret = true;

			if (foundedMeta == nullptr) {
				ret = obj->CreateMetaTable(m_configMetadata);
			}
			else {
				ret = obj->UpdateMetaTable(m_configMetadata, foundedMeta);
			}

			if (!ret) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
				db_query->RollBack(); return false;
#else
				return false;
#endif
			}
		}
	}

	//common data
	CMemoryWriter writterData;

	//Save header info 
	if (!SaveHeader(writterData)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack(); return false;
#else 
		return false;
#endif
	}

	//Save common object
	if (!SaveCommonMetadata(g_metaCommonMetadataCLSID, writterData, flags)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack(); return false;
#else 
		return false;
#endif
	}

	IPreparedStatement* prepStatement = nullptr;
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		prepStatement = db_query->PrepareStatement("INSERT INTO %s (file_name, binary_data, file_guid) VALUES(?, ?, ?)"
			"ON CONFLICT (file_name) DO UPDATE SET file_name = EXCLUDED.file_name, binary_data = EXCLUDED.binary_data, file_guid = EXCLUDED.file_guid; ", config_save_table);
	else
		prepStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s (file_name, binary_data, file_guid) VALUES(?, ?, ?) MATCHING (file_name); ", config_save_table);

	if (!prepStatement) return false;

	prepStatement->SetParamString(1, config_name);
	prepStatement->SetParamBlob(2, writterData.pointer(), writterData.size());
	prepStatement->SetParamString(3, m_metaGuid.str());

	if (prepStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack();
#else 
		return false;
#endif
	}
	
	m_md5Hash = wxMD5::ComputeMd5(
		wxBase64Encode(writterData.pointer(), writterData.size())
	);

	m_configSave = (flags & saveConfigFlag) != 0;

#if _USE_SQL_SERVER_FOR_METADATA == 1
	if (!db_query->CloseStatement(prepStatement)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack();
#else 
		return false;
#endif
	}
#endif

	Modify(false);

	if ((flags & saveConfigFlag) != 0) {

		bool hasError =
			db_query->RunQuery("DELETE FROM %s;", config_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
		hasError = hasError ||
			db_query->RunQuery("INSERT INTO %s SELECT * FROM %s;", config_table, config_save_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
		if (hasError)
			return false;

		if (!m_configMetadata->LoadConfiguration(onlyLoadFlag)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
#else 
			return false;
#endif
		}

		if (!m_configMetadata->RunConfiguration()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
#else 
			return false;
#endif
		}

	}
	else {
		Modify(true);
	}

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
	db_query->Commit();
#endif 
	return true;
}

bool CMetaDataConfigurationStorage::RoolbackConfiguration()
{
	bool hasError = db_query->RunQuery("DELETE FROM %s;", config_save_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
	hasError = hasError || db_query->RunQuery("INSERT INTO %s SELECT * FROM %s;", config_save_table, config_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
	if (hasError) return false;
	//close data 
	if (CMetaDataConfiguration::IsConfigOpen()) {
		if (!CloseConfiguration(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseConfiguration() == false");
			return false;
		}
	}

	//clear data 
	if (!ClearConfiguration()) {
		wxASSERT_MSG(false, "ClearConfiguration() == false");
		return false;
	}

	if (LoadConfiguration())
		return RunConfiguration();
	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

bool CMetaDataConfigurationStorage::TableAlreadyCreated()
{
	return db_query->TableExists(config_table) &&
		db_query->TableExists(config_save_table);
}

void CMetaDataConfigurationStorage::CreateConfigTable() {

	//db for enterprise - TODO
	if (!db_query->TableExists(config_table)) {
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
			db_query->RunQuery("CREATE TABLE %s ("
				"file_name VARCHAR(128) NOT NULL PRIMARY KEY,"
				"file_guid uuid NOT NULL,"
				"binary_data BYTEA NOT NULL);", config_table);         	//size of binary medatadata
		}
		else
		{
			db_query->RunQuery("CREATE TABLE %s ("
				"fileName VARCHAR(128) NOT NULL PRIMARY KEY,"
				"file_guid uuid NOT NULL,"
				"binary_data BLOB NOT NULL);", config_table);         	//size of binary medatadata
		}
		db_query->RunQuery("CREATE INDEX config_index ON %s (file_name);", config_table);
	}
}

void CMetaDataConfigurationStorage::CreateConfigSaveTable() {

	//db for designer 
	if (!db_query->TableExists(config_save_table)) {
		if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL) {
			db_query->RunQuery("CREATE TABLE %s ("
				"file_name VARCHAR(128) NOT NULL PRIMARY KEY,"
				"file_guid uuid NOT NULL,"
				"binary_data BYTEA NOT NULL);", config_save_table);         	//size of binary medatadata
		}
		else {
			db_query->RunQuery("CREATE TABLE %s ("
				"file_name VARCHAR(128) NOT NULL PRIMARY KEY,"
				"file_guid uuid NOT NULL,"
				"binary_data BLOB NOT NULL);", config_save_table);         	//size of binary medatadata
		}
		db_query->RunQuery("CREATE INDEX config_save_index ON %s (file_name);", config_save_table);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////