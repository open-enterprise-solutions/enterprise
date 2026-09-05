#include "metadataConfiguration.h"

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: dialect-neutral DDL/DML + typed row reads (kills the PG/FB forks below)

#include "backend/utils/md5.hpp"
#include "backend/appData.h"
#include "backend/logger/logger.h"
#include "backend/diagnostics/journal.h"   // ibJournal — the apply writes WHAT changed, not only that it did
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
	case ibConfigType::ibConfigType_File:
		break;      // a file configuration has no common table in the database
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

bool ibMetaDataConfigurationStorage::IsDynamicUpdateAvailable() const
{
	// The SAME pair OnSaveDatabase hands the differ, compared instead of applied. Cheap: both snapshots
	// are built from metadata already in memory, no database is touched, and the comparison holds no
	// connection — asking must never be able to change anything.
	const ibSchemaSnapshot target = BuildSchemaSnapshot();
	const bool hasBaseline = (m_configMetadata != nullptr && m_configMetadata->GetCommonMetaObject() != nullptr);
	if (!hasBaseline)
		return false;   // fresh database — everything is about to be created, so nothing is "the same"
	const ibSchemaSnapshot baseline = m_configMetadata->BuildSchemaSnapshot();
	return SameStructure(&baseline, target);
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

		// ⚠ NOT A FAILURE TEST. `structRet` is an affected-row count, and 0 is what a pure-DDL delta
		// returns — a restructuring that only added a column changes no ROWS and is entirely
		// successful. A real failure arrives as an exception, which the catch at the end of this
		// function turns into a rollback + rethrow. Testing == 0 here rolled back good work and
		// reported it as broken, on exactly the applies that touched structure and nothing else.
		(void)structRet;
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

	// One UPSERT — the L2 door renders ON CONFLICT (PG/SQLite) vs UPDATE OR INSERT … MATCHING (FB)
	// from the match key, so the per-driver fork is gone. The blob rides as ibConstBlob (bound, not inlined).
	// A real failure THROWS (caught below); the affected-row count is not inspected.
	ibDatabaseQueryBuilder q_save;
	q_save.Execute(ibUpsert(config_save_table, {
			{ wxT("file_name"),   ibConst(ibValue(config_name)) },
			{ wxT("binary_data"), ibConstBlob(writerData.pointer(), writerData.size()) },
			{ wxT("file_guid"),   ibConst(ibValue(m_metaGuid.str())) },
		}, { wxT("file_name") }));

	// ⭐⭐ RECOMPUTED ON EVERY SAVE, AND THAT IS THE AOT CACHE'S WHOLE CORRECTNESS STORY. A cached
	// row is looked up by (descriptor, configuration digest), so bytecode compiled against an
	// earlier configuration is not found-and-rejected — it is NOT FOUND, and the caller takes the
	// ordinary compile path. Moving the digest here retires every previous row in the same instant,
	// with no DELETE to run and nobody to remember running it (byteCodeCache.h, which records that
	// the rule it replaced cost half a day twice).
	//
	// 🛑 SO IT MAY NOT WAIT FOR THE RESTRUCTURE. Deferring it to the publication was tried on
	// 2026-09-05 and taken straight back out: a plain save changes the module TEXT without moving
	// the key, which puts the previous row back in reach — the exact stale-bytecode failure the key
	// exists to make impossible.
	//
	// ⚠ AND IT IS NOT AN "APPLIED" MARK, which is what made the mistake tempting. Both readers ask
	// about TEXT, not about the database: Modify() compares the edited configuration against the
	// saved baseline, and the debugger verifies a connection by the configuration GUID and merely
	// carries this along. What "the base has not got yet" is answered by database_diff.
	m_md5Hash = ibMD5::ComputeMd5(
		wxBase64Encode(writerData.pointer(), writerData.size())
	);

	// ⭐⭐ THE ACTIVE CONFIGURATION IS *NOT* PUBLISHED HERE ANY MORE — see PublishConfiguration(),
	// called after the schema is finished.
	//
	// config_save holds the edited configuration and is written above, in this transaction, with the
	// DDL: that pair is atomic and must stay so. `config` is a different thing — it is the ACTIVE
	// configuration, the one a re-read produces the BASELINE from, and publishing it here declared
	// the schema finished while one phase of it had not run yet.
	//
	// On Firebird the maintenance (triggers, views) and the seeds cannot be built in the same
	// transaction as the tables they address, so they are deferred past this commit. If that deferred
	// phase then failed, the active configuration was already durable and ahead of the physical
	// schema — the differ compared the new configuration against itself, emitted nothing, and the
	// missing objects could never be built again. That is "the diff drifted", and it is not
	// recoverable by diffing harder: a differ over two configurations is right to stay silent when
	// they agree.
	//
	// So publication is the LAST step, and it happens only when everything before it worked.

	return db_query->IsActiveTransaction();

	}
	catch (...) {
		// ⭐ A FAILED APPLY LEAVES A RECORD — and until now it was the ONLY outcome that did not. The
		// audit entry is written by OnAfterSaveDatabase, which this path skips BY DEFINITION, so a base
		// whose restructuring blew up showed "applied", "saved", "saved" in its journal and then simply
		// nothing where the failure was. Reading the journal afterwards, the failure had never happened.
		//
		// The reason travels with it. The exception is re-thrown unchanged (the caller still surfaces the
		// chain); this only makes sure the base itself remembers what refused and when.
		if (ibLog != nullptr && ibLog->IsEnabled(ibLogLevel::Audit)) {
			wxString reason;
			try { throw; }
			catch (const ibBackendException& err) { reason = err.GetErrorDescription(); }
			catch (const std::exception& err)     { reason = wxString::FromUTF8(err.what()); }
			catch (...)                           { reason = _("unknown exception"); }
			ibLog->Audit(wxT("metadata"), wxT("apply_failed"),
			             wxString::Format(wxT("flags=0x%X: %s"), flags, reason));
		}

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

	// ⭐⭐ …AND WHAT ACTUALLY CHANGED, not only that something did.
	//
	// The audit above records the EVENT and the flags, which answers "was there an apply" and
	// nothing else. The account of the change — every CREATE / ALTER / DROP, every column added or
	// removed, every warning raised on the way — is sitting right here in the restructure ledger,
	// and until now it was read by the apply dialog and by `config_apply`'s result and then thrown
	// away. So an apply made from the designer's own button left NO trace of its content anywhere:
	// the technology journal showed the fact and not the substance (Max, 2026-08-31: "I updated the
	// database, it shows nothing").
	//
	// Written HERE rather than at either caller because both roads pass through it — the button and
	// the tool — and a record that exists only on one of them is the kind of gap that is discovered
	// from a crash dump later.
	if (const ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
		ledger.Count() > 0) {

		ibJournalInfo(wxT("metadata.apply"), wxT("%s: %u change(s)%s%s"),
			roolback ? wxT("rolled back") : wxT("applied"),
			(unsigned)ledger.Count(),
			ledger.HasWarnings() ? wxT(", with warnings") : wxT(""),
			ledger.HasErrors()   ? wxT(", with errors")   : wxT(""));

		// 🛑 AN ERROR HERE IS NOT LOGGED WITH A DIALOG — IT IS RAISED. ibJournalError echoes through
		// wxLogError, and in a GUI process that is a MODAL: the person in front of the designer was
		// shown "! Doesn't have any recorder" for work they never started (a `config_save` over MCP,
		// 2026-09-03), while the caller sat waiting on a window it could not see.
		//
		// ⭐ WHO SHOWS IT IS DECIDED BY WHO ASKED, and the way to let them decide is an exception:
		// the designer catches it at its button and shows a window; a tool catches it and puts the
		// words in its answer. Restructuring itself has no business knowing which of the two is
		// there — it only has to say what went wrong, once, and let go.
		// ⚠ AND THE LEVEL IS A FACT ABOUT THE RESTRUCTURING, NOT ABOUT THE PROCESS. "Doesn't have any
		// recorder" is an error OF THIS STAGE - the reason it will not go through, and the material
		// of the apply report - and not a failure of the application that anybody must be interrupted
		// about. Written through the error verb it became one: ibJournalError echoes at wx's ERROR
		// level, and in a GUI process that is a modal dialog in front of whoever happens to be there.
		//
		// So the whole ledger goes to the FILE as it stands, each line carrying its own level as a
		// word; nothing here opens a window.
		wxString raised;

		for (const ibRestructureInfo::Entry& entry : ledger) {

			const wxChar* level =
				  entry.type == ibRestructure::error   ? wxT("error")
				: entry.type == ibRestructure::warning ? wxT("warning")
				                                       : wxT("info");

			ibJournalInfo(wxT("metadata.apply"), wxT("%s: %s"), level, entry.descr);

			if (entry.type == ibRestructure::error)
				raised << (raised.IsEmpty() ? wxT("") : wxT("\n")) << entry.descr;
		}

		// 🛑 AND IT DOES NOT RAISE FROM HERE. Two reasons, and the second is the one that bites: a
		// caller needs the LIST - which objects, what is wrong with each - and an exception hands it
		// one string and unwinds; and this point is BEFORE the builder closes the restructuring
		// transaction below, so leaving through it would abandon the commit-or-rollback that has to
		// happen either way.
		//
		// The ledger IS the answer, and it stays where both roads already read it: the apply dialog
		// greys its button out of it, and a tool reads it into its result. Nothing is lost by not
		// throwing - what would have been thrown is sitting in `ledger` for whoever asked.
		(void)raised;
	}

	// The L3-2 builder OWNS the close of the restructuring transaction: rollback if asked, else commit the
	// DDL + the blob written in OnSaveDatabase, and on Firebird flush the deferred seed rows in their own
	// TX (the just-created tables are now durable). Metadata no longer touches Commit / RollBack here.
	const int afterRet = m_structureBuilder.OnAfterSave(roolback);

	// ⚠⚠ ZERO IS NOT AN ERROR. `DATABASE_LAYER_QUERY_RESULT_ERROR` is 0, and 0 is what every DDL
	// statement and every cleanup that matched no rows legitimately returns — it means "the database
	// changed nothing", not "the database failed". Failure travels as an EXCEPTION now; this test is
	// a leftover from the era of return codes, and it reads success as catastrophe.
	//
	// Where that leftover bit: it sits AFTER the commit. The restructuring transaction is already
	// durable by the time this line runs, so answering "failed" here skips the baseline reload below
	// — and from that moment the saved configuration this engine diffs against no longer describes
	// the database it is diffing FOR. Every later apply computes its delta from a snapshot the base
	// left behind: removing a common attribute emits nothing (the baseline never had the column), so
	// the column stays; adding it back emits ADD, and Firebird answers with a unique-key violation
	// on RDB$RELATION_FIELDS. Permanently — the two only drift further apart with each attempt.
	//
	// So: the ONLY question that decides the baseline is whether the transaction COMMITTED.
	if (roolback)
		return true;   // rolled back — nothing committed, nothing to re-read, and no failure to report

	// Metadata-side post-commit: reload the just-committed config in a FRESH transaction (the builder
	// already committed the restructuring one).
	//
	// ⚠ REACHED WHATEVER `afterRet` SAID. It is not a verdict on the commit — the commit happened
	// before it was produced, and what it reports on is the Firebird seed flush that follows. A
	// failure THERE is a data problem in tables that already exist; it must not leave this engine
	// holding a baseline the database has moved on from.
	(void)afterRet;   // read for its meaning below, never as a reason to skip the re-read
	if ((flags & saveConfigFlag) != 0) {

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		db_query->BeginTransaction();

		// ⭐⭐ PUBLISH FIRST, IN THE SAME TRANSACTION AS THE RE-READ — and only now, because only now
		// is the schema complete. Everything the apply had to build (structure in the commit above,
		// maintenance and seeds in the deferred phase after it) has run; a failure in any of them
		// raised and never reached this line, leaving `config` at the PREVIOUS configuration.
		//
		// That is what makes a failed apply repeatable: the active configuration still describes the
		// database as it actually is, so the next diff sees real work to do instead of comparing the
		// new configuration against itself and emitting nothing.
		//
		// It shares the re-read's transaction deliberately: publication and the baseline it produces
		// are one fact. Both land or neither does.
		ibDatabaseQueryBuilder q_publish;
		q_publish.Execute(ibDelete(config_table));
		q_publish.Execute(ibInsertSelect(config_table, {}, ibProject(ibScan(config_save_table))));

		// ⭐ THE WHOLE RE-READ IS INSIDE ONE TRANSACTION, SO IT NEEDS ONE ABORT PATH. Loading and running
		// a configuration compiles modules and reads blobs; any of it can raise, and an exception that
		// walks out of here leaves this transaction ACTIVE. It then holds locks on the config tables
		// until the connection dies, and the next apply meets that as a deadlock naming sys_config.
		// Same class as the commit guard in ibStructureBuilder::OnAfterSave.
		struct TxGuard {
			bool m_done = false;
			~TxGuard() {
				if (!m_done && db_query->IsActiveTransaction())
					db_query->RollBack();
			}
		} txGuard;
#endif
		if (!m_configMetadata->LoadDatabase(onlyLoadFlag)) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
			txGuard.m_done = true;
#else
			return false;
#endif
		}

		if (!m_configNew && !m_configMetadata->RunDatabase()) {
#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
			db_query->RollBack();
			txGuard.m_done = true;
#else
			return false;
#endif
		}

#if _USE_SAVE_METADATA_IN_TRANSACTION == 1
		if (db_query->IsActiveTransaction())
			db_query->Commit();
		txGuard.m_done = true;
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
	// Same reading as the incremental path: 0 is an affected-row count, not a verdict. Recreate rolls
	// back and rethrows on a real failure, so reaching this line at all means it worked.
	(void)recreateRet;

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