#include "metadataConfiguration.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/utils/md5.hpp"
#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/backend_exception.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////
#include <wx/base64.h>
//////////////////////////////////////////////////////////////////////////////////////////////////////

// The restructure ledger lives on the CONFIGURATION; this static pulls the ACTIVE config's instance, so
// this-less callers (static scaffold methods, the apply dialog) reach it without a metadata handle.
ibRestructureInfo& ibMetaDataConfigurationBase::GetRestructureInfo()
{
	return ibApplicationData::GetActiveMetaData()->m_restructureInfo;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
#define config_table	  wxT("sys_config")
#define config_save_table wxT("sys_config_save")
//////////////////////////////////////////////////////////////////////////////////////////////////////
#define config_name       wxT("sys.database")
//////////////////////////////////////////////////////////////////////////////////////////////////////

inline wxString GetCommonConfigTable(ibConfigType cfg_type) {

	switch (cfg_type)
	{
	case ibConfigType::ibConfigType_Load:
		return config_table;
	case ibConfigType::ibConfigType_Load_And_Save:
		return config_save_table;
	}

	return wxEmptyString;
}

//**************************************************************************************************
//*                                          ConfigMetadata                                        *
//**************************************************************************************************

bool ibMetaDataConfiguration::LoadDatabase(int flags)
{
	//close if opened
	if (ibMetaDataConfiguration::IsConfigOpen()) {
		if (!CloseDatabase(forceCloseFlag)) {
			return false;
		}
	}

	// load config
	ibDatabaseResultSet* resultSet = db_query->RunQueryWithResults("SELECT binary_data, file_guid FROM %s; ", GetCommonConfigTable(GetConfigType()));
	if (resultSet == nullptr)
		return false;

	//load metadata from DB 
	if (resultSet->Next()) {

		//clear data 
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
			return false;
		}

		wxMemoryBuffer binaryData;
		resultSet->GetResultBlob(wxT("binary_data"), binaryData);
		ibReaderMemory metaReader(binaryData.GetData(), binaryData.GetBufSize());

		//check is file empty
		if (metaReader.eof())
			return false;

		//load metadata (header is read inside LoadCommonTree)
		if (!LoadCommonTree(g_metaCommonMetadataCLSID, metaReader))
			return false;

		m_configNew = false;

		m_metaGuid = resultSet->GetResultString(wxT("file_guid"));
		m_md5Hash = ibMD5::ComputeMd5(wxBase64Encode(binaryData.GetData(), binaryData.GetDataLen()));
	}

	db_query->CloseResultSet(resultSet);
	return true;
}

//**************************************************************************************************
//*                                          ConfigSaveMetadata                                    *
//**************************************************************************************************

#include "metaCollection/partial/constant.h"
#include "backend/query/schemaSnapshot.h"   // ibSchemaSnapshot — the Storage builds it (ContributeTables) and hands it to the builder

////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::OnBeforeSaveDatabase(int flags)
{
	GetRestructureInfo().Clear();

	// The L3-2 builder OWNS the whole restructuring start: it acquires DB-wide exclusive (DDL monopoly),
	// discards any leftover unfinished build (rolls back a straggling transaction), resets the per-save
	// barrier, and OPENS the transaction on its connection. A failure (exclusive or begin) is recorded in
	// the builder's change log, which we surface to the apply UI. Metadata no longer touches exclusive /
	// BeginTransaction / the barrier here.
	if (!m_structureBuilder.OnBeforeSave()) {
		GetRestructureInfo().Absorb(m_structureBuilder.GetChanges());
		return false;
	}
	return true;
}

bool ibMetaDataConfigurationStorage::OnSaveDatabase(int flags)
{
	if (!db_query->IsActiveTransaction())
		return false;


	// Self-safeguard: a DDL exception (e.g. the FB driver throwing on a failed
	// ALTER) below would otherwise skip OnAfterSaveDatabase, leaking the open
	// transaction + the auto-acquired exclusive mode — the next apply then bails
	// silently at the "transaction already active" guard ("metadata returned
	// false", no text). Roll back + release here and rethrow so any caller
	// (designer / codeRunner / daemon) still reports the error and the DB stays clean.
	try {

	//remove old tables (if need)
	if ((flags & saveConfigFlag) != 0) {

		//Delete common object
		if (!DeleteCommonTree(g_metaCommonMetadataCLSID)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else 
			return false;
#endif
		}

		// System scaffold on a fresh database — the constant's SHARED sys_const table + the system tables.
		// These are NOT metaobject tables, so they stay outside the snapshot differ.
		if (m_configNew) {

			GetRestructureInfo().AppendInfo(_("Create new database"));

			if (!ibValueMetaObjectConstant::DeleteConstantSQLTable()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
				db_query->RollBack(); return false;
#else
				return false;
#endif
			}

			if (!ibValueMetaObjectConfiguration::ExecuteSystemSQLCommand()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
				db_query->RollBack(); return false;
#else
				return false;
#endif
			}

			if (!ibValueMetaObjectConstant::CreateConstantSQLTable()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
				db_query->RollBack(); return false;
#else
				return false;
#endif
			}
		}

		// Structure + seed via the declarative differ. The Storage's only job is to BUILD the two snapshots
		// (ContributeTables on the saved baseline m_configMetadata + the edited config `this`) and hand them
		// to the metadata-agnostic builder; the builder computes and applies the delta. An object now only
		// DECLARES its tables + value rows — no per-metaobject DeleteMetaTable / CreateMetaTable walk.
		ibSchemaSnapshot target;
		if (ibValueMetaObject* common = GetCommonMetaObject())
			common->ContributeTables(target);
		ibSchemaSnapshot baseline;
		const bool hasBaseline = (m_configMetadata != nullptr && m_configMetadata->GetCommonMetaObject() != nullptr);
		if (hasBaseline)
			m_configMetadata->GetCommonMetaObject()->ContributeTables(baseline);

		const int structRet = m_structureBuilder.OnSave(hasBaseline ? &baseline : nullptr, target);

		// Extract what the builder changed into this metadata's ledger (so the apply-change dialog shows it
		// alongside validation warnings/errors). The builder owns the delta; metadata just reads it out.
		GetRestructureInfo().Absorb(m_structureBuilder.GetChanges());

		if (structRet == DATABASE_LAYER_QUERY_RESULT_ERROR) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack(); return false;
#else
			return false;
#endif
		}
	}

	//common data
	ibWriterMemory writerData;

	//Save common object (header is written inside SaveCommonTree)
	if (!SaveCommonTree(g_metaCommonMetadataCLSID, writerData, flags)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack(); return false;
#else 
		return false;
#endif
	}

	ibPreparedStatement* prepStatement = nullptr;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		prepStatement = db_query->PrepareStatement("INSERT INTO %s (file_name, binary_data, file_guid) VALUES(?, ?, ?)"
			"ON CONFLICT (file_name) DO UPDATE SET file_name = EXCLUDED.file_name, binary_data = EXCLUDED.binary_data, file_guid = EXCLUDED.file_guid; ", config_save_table);
	else
		prepStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s (file_name, binary_data, file_guid) VALUES(?, ?, ?) MATCHING (file_name); ", config_save_table);

	if (!prepStatement) return false;

	prepStatement->SetParamString(1, config_name);
	prepStatement->SetParamBlob(2, writerData.pointer(), writerData.size());
	prepStatement->SetParamString(3, m_metaGuid.str());

	if (prepStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack(); return false;
#else
		return false;
#endif
	}

	if (!db_query->CloseStatement(prepStatement)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->RollBack(); return false;
#else 
		return false;
#endif
	}

	m_md5Hash = ibMD5::ComputeMd5(
		wxBase64Encode(writerData.pointer(), writerData.size())
	);

	if ((flags & saveConfigFlag) != 0) {

		bool hasError =
			db_query->RunQuery("DELETE FROM %s;", config_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
		hasError = hasError ||
			db_query->RunQuery("INSERT INTO %s SELECT * FROM %s;", config_table, config_save_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
		if (hasError)
			return false;
	}

	return db_query->IsActiveTransaction();

	}
	catch (...) {
		// The exception skips OnAfterSaveDatabase, so close the build here through the builder — it rolls
		// back its transaction AND releases the exclusive it took (its OnAfterSave's AutoRelease). Then
		// rethrow so the caller surfaces the backend error chain.
		m_structureBuilder.OnAfterSave(/*rollback*/ true);
		throw;
	}
}

bool ibMetaDataConfigurationStorage::OnAfterSaveDatabase(bool roolback, int flags)
{
	// (Exclusive release moved into the builder's OnAfterSave — it pairs the acquire in its OnBeforeSave.)

	// DDL apply audit. saveConfigFlag = full apply (DDL + seed); without
	// it the call is a metadata-only save (no schema change). Two events
	// distinguish admin-relevant DDL from background metadata-only writes.
	if (ibLog && ibLog->IsEnabled(ibLogLevel::Audit)) {
		const bool fullApply = (flags & saveConfigFlag) != 0;
		const wxString evt = roolback
			? wxT("apply_failed")
			: (fullApply ? wxT("applied") : wxT("saved"));
		ibLog->Audit(wxT("metadata"), evt,
		             wxString::Format(wxT("flags=0x%X"), flags));
	}

	// The L3-2 builder OWNS the close of the restructuring transaction: rollback if asked, else commit the
	// DDL + the blob written in OnSaveDatabase, and on Firebird flush the deferred seed rows in their own
	// TX (the just-created tables are now durable). Metadata no longer touches Commit / RollBack here.
	const int afterRet = m_structureBuilder.OnAfterSave(roolback);

	if (roolback)
		return afterRet != DATABASE_LAYER_QUERY_RESULT_ERROR;

	if (afterRet == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	// Metadata-side post-commit: reload the just-committed config in a FRESH transaction (the builder
	// already committed the restructuring one).
	if ((flags & saveConfigFlag) != 0) {

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->BeginTransaction();
#endif
		if (!m_configMetadata->LoadDatabase(onlyLoadFlag)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
#else
			return false;
#endif
		}

		if (!m_configNew && !m_configMetadata->RunDatabase()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
#else
			return false;
#endif
		}

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->Commit();
#endif
		Modify(false);

		// One-time flag: the very first apply on a fresh DB runs the "create new database" path (DROP +
		// recreate sys_const). Every later apply must be incremental. Clearing it here — after the config
		// blob is committed — flips the next apply to configNew=0; otherwise every Update re-DROPs sys_const
		// and a constant read mid-cycle hits its just-dropped column (FB -206 "column unknown FLDnnnn_TYPE").
		m_configNew = false;
	}
	else {
		Modify(false);
		Modify(true);
	}

	return !db_query->IsActiveTransaction();
}

////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::ReCreateDatabase()
{
	if (!m_configMetadata)
		return false;

	// Full rebuild: the Storage builds the target snapshot and hands it to the builder, which OWNS the
	// whole transaction — drop all of the config's tables, recreate + seed them all, commit (+ FB deferred-
	// seed flush). Metadata only declares the schema and reads the change log out.
	ibSchemaSnapshot target;
	if (ibValueMetaObject* common = GetCommonMetaObject())
		common->ContributeTables(target);

	const int recreateRet = m_structureBuilder.Recreate(target);
	GetRestructureInfo().Absorb(m_structureBuilder.GetChanges());
	if (recreateRet == DATABASE_LAYER_QUERY_RESULT_ERROR)
		return false;

	ResetSequence();
	return true;
}

////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::SaveDatabase(int flags)
{
	if (OnBeforeSaveDatabase(flags)) {
		bool succes = OnSaveDatabase(flags);
		OnAfterSaveDatabase(!succes, flags);
		return succes;
	}

	return false;
}

bool ibMetaDataConfigurationStorage::RollbackDatabase()
{
	bool hasError = db_query->RunQuery("DELETE FROM %s;", config_save_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
	hasError = hasError || db_query->RunQuery("INSERT INTO %s SELECT * FROM %s;", config_save_table, config_table) == DATABASE_LAYER_QUERY_RESULT_ERROR;
	if (hasError) return false;
	//close data 
	if (ibMetaDataConfiguration::IsConfigOpen()) {
		if (!CloseDatabase(forceCloseFlag)) {
			wxASSERT_MSG(false, "CloseDatabase() == false");
			return false;
		}
	}

	//clear data 
	if (!ClearDatabase()) {
		wxASSERT_MSG(false, "ClearDatabase() == false");
		return false;
	}

	if (LoadDatabase())
		return RunDatabase();
	return false;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

bool ibMetaDataConfigurationStorage::TableAlreadyCreated()
{
	return db_query->TableExists(config_table) &&
		db_query->TableExists(config_save_table) &&
		db_query->TableExists(sequence_table);
}

void ibMetaDataConfigurationStorage::CreateConfigTable() {

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
				"file_name VARCHAR(128) NOT NULL PRIMARY KEY,"
				"file_guid VARCHAR(36) NOT NULL,"
				"binary_data BLOB NOT NULL);", config_table);         	//size of binary medatadata
		}

		db_query->RunQuery("CREATE INDEX s_config_index ON %s (file_name);", config_table);
	}
}

void ibMetaDataConfigurationStorage::CreateConfigSaveTable() {

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
				"file_guid VARCHAR(36) NOT NULL,"
				"binary_data BLOB NOT NULL);", config_save_table);         	//size of binary medatadata
		}
		db_query->RunQuery("CREATE INDEX s_config_save_index ON %s (file_name);", config_save_table);
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaDataConfigurationStorage::CreateConfigSequence()
{
	if (!db_query->TableExists(sequence_table)) {

		db_query->RunQuery(wxT("create table %s ("
			"interval		   INTEGER       NOT NULL,"
			"meta_guid         VARCHAR(36)   NOT NULL,"
			"prefix			   VARCHAR(24)   NOT NULL,"
			"number			   INTEGER       NOT NULL,"
			"primary key (interval, meta_guid, prefix));"),

			sequence_table);

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {
			db_query->RunQuery(
				wxT("create index if not exists sequence_index on %s (interval, meta_guid, prefix);"),
				sequence_table
			);
		}
		else {
			db_query->RunQuery(
				wxT("create index sequence_index on %s (interval, meta_guid, prefix);"),
				sequence_table
			);
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaDataConfigurationStorage::ResetSequence()
{
	if (db_query->TableExists(sequence_table))
		db_query->RunQuery(wxT("delete from %s;"), sequence_table);
}

bool ibMetaDataConfigurationStorage::LoadSequenceFromBuffer(const ibReaderMemory& reader)
{
	if (!db_query->TableExists(sequence_table))
		return false;

	u64 seq_idx = 0;
	ibReaderMemory* seqReaderPrev = nullptr;

	while (true) {

		ibReaderMemory* seqReader = reader.open_chunk_iterator(seq_idx, seqReaderPrev);

		if (seqReader == nullptr)
			break;

		const int interval = seqReader->r_u32();
		const wxString& strDocPath = seqReader->r_stringZ();
		const wxString& strPrefix = seqReader->r_stringZ();
		const int number = seqReader->r_u32();

		if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD) {

			db_query->RunQuery(
				wxT("INSERT INTO %s (interval, meta_guid, prefix, number) VALUES (%s, '%s', '%s', %s) ON CONFLICT(interval, meta_guid, prefix) DO UPDATE SET interval = excluded.interval, meta_guid = excluded.meta_guid, prefix = excluded.prefix, number = excluded.number;"),
				sequence_table,
				stringUtils::IntToStr(interval),
				strDocPath,
				strPrefix, //prefix
				stringUtils::IntToStr(number)
			);
		}
		else {

			db_query->RunQuery(
				wxT("UPDATE OR INSERT INTO %s (interval, meta_guid, prefix, number) VALUES (%s, '%s', '%s', %s) MATCHING (interval, meta_guid, prefix);"),
				sequence_table,
				stringUtils::IntToStr(interval),
				strDocPath,
				strPrefix, //prefix
				stringUtils::IntToStr(number)
			);
		}

		seqReaderPrev = seqReader;
	};

	return true;
}

bool ibMetaDataConfigurationStorage::SaveSequenceToBuffer(ibWriterMemory& writer)
{
	if (!db_query->TableExists(sequence_table))
		return false;

	// save sequence 
	ibDatabaseResultSet* resultSet = db_query->RunQueryWithResults(wxT("select * FROM %s;"), sequence_table);

	if (resultSet == nullptr)
		return false;

	int idx = 0;

	while (resultSet->Next()) {

		ibWriterMemory seqWriter;
		seqWriter.w_u32(resultSet->GetResultInt(wxT("interval")));
		seqWriter.w_stringZ(resultSet->GetResultString(wxT("meta_guid")));
		seqWriter.w_stringZ(resultSet->GetResultString(wxT("prefix")));
		seqWriter.w_u32(resultSet->GetResultInt(wxT("number")));
		writer.w_chunk(idx++, seqWriter.buffer());
	}

	resultSet->Close();
	return true;
}