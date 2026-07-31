#include "metadataConfiguration.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: dialect-neutral DDL/DML + typed row reads (kills the PG/FB forks below)

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

	// load config: SELECT binary_data, file_guid FROM <config table>
	ibDatabaseQueryBuilder q;
	ibQueryIR ir(ibProject(ibScan(GetCommonConfigTable(GetConfigType())),
		{ { ibCol(wxT("binary_data")), wxEmptyString }, { ibCol(wxT("file_guid")), wxEmptyString } }));
	ibQueryResult result = q.ExecuteIR(ir);   // RAII: closes cursor + statement

	//load metadata from DB
	if (result.Next()) {

		//clear data
		if (!ClearDatabase()) {
			wxASSERT_MSG(false, "ClearDatabase() == false");
			return false;
		}

		wxMemoryBuffer binaryData;
		result.GetResultBlob(wxT("binary_data"), binaryData);
		ibReaderMemory metaReader(binaryData.GetData(), binaryData.GetBufSize());

		//check is file empty
		if (metaReader.eof())
			return false;

		//load metadata (header is read inside LoadCommonTree)
		if (!LoadCommonTree(g_metaCommonMetadataCLSID, metaReader))
			return false;

		m_configNew = false;

		m_metaGuid = result.GetResultString(wxT("file_guid"));
		m_md5Hash = ibMD5::ComputeMd5(wxBase64Encode(binaryData.GetData(), binaryData.GetDataLen()));
	}

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
		// (the saved baseline m_configMetadata + the edited config `this`) and hand them to the
		// metadata-agnostic builder; the builder computes and applies the delta. An object now only
		// DECLARES its tables + value rows — no per-metaobject DeleteMetaTable / CreateMetaTable walk.
		const ibSchemaSnapshot target = BuildSchemaSnapshot();
		const bool hasBaseline = (m_configMetadata != nullptr && m_configMetadata->GetCommonMetaObject() != nullptr);
		const ibSchemaSnapshot baseline = hasBaseline
			? m_configMetadata->BuildSchemaSnapshot()
			: ibSchemaSnapshot();

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

	// One UPSERT — the L2 door renders ON CONFLICT (PG/SQLite/MySQL) vs UPDATE OR INSERT … MATCHING (FB)
	// from the match key, so the per-driver fork is gone. The blob rides as ibConstBlob (bound, not inlined).
	// A real failure THROWS (caught below); the affected-row count is not inspected.
	ibDatabaseQueryBuilder q_save;
	q_save.Execute(ibUpsert(config_save_table, {
			{ wxT("file_name"),   ibConst(ibValue(config_name)) },
			{ wxT("binary_data"), ibConstBlob(writerData.pointer(), writerData.size()) },
			{ wxT("file_guid"),   ibConst(ibValue(m_metaGuid.str())) },
		}, { wxT("file_name") }));

	m_md5Hash = ibMD5::ComputeMd5(
		wxBase64Encode(writerData.pointer(), writerData.size())
	);

	if ((flags & saveConfigFlag) != 0) {

		// Copy the just-saved config_save → config (whole-table). Both go through L2 — DELETE, then
		// INSERT … SELECT (ibInsertSelect with an empty projection = SELECT *). Standard SQL, no fork.
		// Execute returns the affected-row COUNT and signals a real failure by THROWING (caught below) —
		// a 0 count is NOT an error (an empty config_table on the first save has nothing to delete).
		q_save.Execute(ibDelete(config_table));
		q_save.Execute(ibInsertSelect(config_table, {}, ibProject(ibScan(config_save_table))));
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
	const ibSchemaSnapshot target = BuildSchemaSnapshot();

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
	// Restore config_save from config (the inverse of OnSaveDatabase's copy) — both through L2:
	// DELETE, then INSERT … SELECT (ibInsertSelect, empty projection = SELECT *). Standard SQL, no fork.
	// A real failure THROWS; a 0-row DELETE (empty config_save) is not an error.
	ibDatabaseQueryBuilder q;
	q.Execute(ibDelete(config_save_table));
	q.Execute(ibInsertSelect(config_save_table, {}, ibProject(ibScan(config_table))));
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

	//db for enterprise
	if (!db_query->TableExists(config_table)) {
		// file_guid is a GUID column: native UUID on PostgreSQL, CHAR(36) elsewhere — the dialect TYPE-MAP
		// picks per driver, so the PG/non-PG fork is gone. binary_data: BYTEA on PG, BLOB elsewhere.
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(config_table, {
			{ wxT("file_name"),   ibTypeString(128), false, true,  wxEmptyString },
			{ wxT("file_guid"),   ibTypeGuid(),      true,  false, wxEmptyString },
			{ wxT("binary_data"), ibTypeBlob(),      true,  false, wxEmptyString },
		}));
		q.Execute(ibCreateIndex(config_table, wxT("s_config_index"), { wxT("file_name") }));
	}
}

void ibMetaDataConfigurationStorage::CreateConfigSaveTable() {

	//db for designer
	if (!db_query->TableExists(config_save_table)) {
		// Same schema as config_table — GUID + blob columns resolve per driver via the dialect TYPE-MAP.
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(config_save_table, {
			{ wxT("file_name"),   ibTypeString(128), false, true,  wxEmptyString },
			{ wxT("file_guid"),   ibTypeGuid(),      true,  false, wxEmptyString },
			{ wxT("binary_data"), ibTypeBlob(),      true,  false, wxEmptyString },
		}));
		q.Execute(ibCreateIndex(config_save_table, wxT("s_config_save_index"), { wxT("file_name") }));
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaDataConfigurationStorage::CreateConfigSequence()
{
	if (!db_query->TableExists(sequence_table)) {

		// (interval, meta_guid, prefix) is the sequence identity. The L2 CreateTable renderer carries only
		// inline single-column PRIMARY KEY, so the composite uniqueness rides as a UNIQUE index — which is
		// also the UPSERT match target in LoadSequenceFromBuffer (and replaces the old separate index).
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(sequence_table, {
			{ wxT("interval"),  ibTypeInteger(),  true, false, wxEmptyString },
			{ wxT("meta_guid"), ibTypeString(36), true, false, wxEmptyString },
			{ wxT("prefix"),    ibTypeString(24), true, false, wxEmptyString },
			{ wxT("number"),    ibTypeInteger(),  true, false, wxEmptyString },
		}));
		q.Execute(ibCreateIndex(sequence_table, wxT("sequence_index"),
			{ wxT("interval"), wxT("meta_guid"), wxT("prefix") }, /*unique*/true));
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

void ibMetaDataConfigurationStorage::ResetSequence()
{
	if (db_query->TableExists(sequence_table)) {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(sequence_table));
	}
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

		// One UPSERT, values bound (not string-concatenated as before) — the door renders the
		// ON CONFLICT / UPDATE OR INSERT … MATCHING fork from the match keys.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(sequence_table, {
			{ wxT("interval"),  ibConst(ibValue(interval)) },
			{ wxT("meta_guid"), ibConst(ibValue(strDocPath)) },
			{ wxT("prefix"),    ibConst(ibValue(strPrefix)) },
			{ wxT("number"),    ibConst(ibValue(number)) },
		}, { wxT("interval"), wxT("meta_guid"), wxT("prefix") }));

		seqReaderPrev = seqReader;
	};

	return true;
}

bool ibMetaDataConfigurationStorage::SaveSequenceToBuffer(ibWriterMemory& writer)
{
	if (!db_query->TableExists(sequence_table))
		return false;

	// save sequence: SELECT interval, meta_guid, prefix, number FROM sequence_table
	ibDatabaseQueryBuilder q;
	ibQueryIR ir(ibProject(ibScan(sequence_table), {
		{ ibCol(wxT("interval")),  wxEmptyString },
		{ ibCol(wxT("meta_guid")), wxEmptyString },
		{ ibCol(wxT("prefix")),    wxEmptyString },
		{ ibCol(wxT("number")),    wxEmptyString },
	}));
	ibQueryResult result = q.ExecuteIR(ir);   // RAII: closes cursor + statement

	int idx = 0;

	while (result.Next()) {
		ibWriterMemory seqWriter;
		seqWriter.w_u32(result.GetResultInt(wxT("interval")));
		seqWriter.w_stringZ(result.GetResultString(wxT("meta_guid")));
		seqWriter.w_stringZ(result.GetResultString(wxT("prefix")));
		seqWriter.w_u32(result.GetResultInt(wxT("number")));
		writer.w_chunk(idx++, seqWriter.buffer());
	}

	return true;
}