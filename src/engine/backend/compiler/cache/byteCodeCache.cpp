////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.cpp
// Purpose     : ibByteCodeCache DAO implementation against sys_bytecode_cache.
////////////////////////////////////////////////////////////////////////////

#include "byteCodeCache.h"

#include "backend/appData.h"
#include "backend/compiler/byteCode.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: descriptor pilot
#include "backend/fileSystem/fs.h"
#include "backend/guid.h"

// Descriptor (ibRuntimeModuleDataObject) AOT-cache DAO, migrated onto the L2
// query door. The default-ctor ibDatabaseQueryBuilder resolves to
// ibConnectionPool::CurrentHolder() — the SAME pool entry the `db_query` macro
// routes through (connectionScope.h) — so this runs on the same connection it
// used to. The blob rides as an ibConstBlob (bound via SetParamBlob, never
// inlined); UPSERT stays DELETE-then-INSERT (uniform across drivers). A passive
// scope (pool not up) makes Execute throw NoConnection, which the surrounding
// try/catch turns into a cache miss → recompile — the same graceful degradation
// as the old `db_query == nullptr` guard. See docs/query-language-arc.md §17.

bool ibByteCodeCache::Save(const ibByteCode& bc)
{
	if (db_query == nullptr) return false;
	if (!db_query->TableExists(bytecode_cache_table)) return false;

	// Serialize first — if AOT writer rejects the bytecode (e.g. a
	// non-primitive constant pool entry like TYPE_REFFER), we don't
	// want to leave the previous row deleted.
	ibWriterMemory writer;
	if (!bc.SerializeAOT(writer)) return false;
	const wxMemoryBuffer blob = writer.buffer();

	const wxString descIdStr = bc.m_id;
	const wxString bcVerStr  = bc.m_version;

	// UPSERT via DELETE-then-INSERT (uniform across drivers; FB has no ON
	// CONFLICT). Both statements run on one builder = one CurrentHolder
	// connection, so there is no cross-connection isolation concern.
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(bytecode_cache_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")), ibConst(ibValue(descIdStr)))));
		q.Execute(ibInsert(bytecode_cache_table, {
			{ wxT("descriptor_id"),    ibConst(ibValue(descIdStr)) },
			{ wxT("bytecode_version"), ibConst(ibValue(bcVerStr)) },
			{ wxT("bc_blob"),          ibConstBlob(blob.GetData(),
			                                       static_cast<size_t>(blob.GetDataLen())) },
		}));
	}
	catch (...) { return false; }
	return true;
}

bool ibByteCodeCache::Load(ibByteCode& outBc, const ibGuid& descId)
{
	if (db_query == nullptr) return false;
	if (!db_query->TableExists(bytecode_cache_table)) return false;

	wxASSERT(descId.isValid());

	try {
		// SELECT bc_blob FROM <t> WHERE descriptor_id = <descId>
		ibDatabaseQueryBuilder q;
		ibQueryIR ir(
			ibProject(
				ibFilter(
					ibScan(bytecode_cache_table),
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")),
					        ibConst(ibValue(wxString(descId))))),
				{ { ibCol(wxT("bc_blob")), wxEmptyString } }));

		ibQueryResult res = q.ExecuteIR(ir);   // RAII: closes cursor + statement
		if (res.Next()) {
			if (ibDatabaseResultSet* rs = res.RawResultSet()) {
				wxMemoryBuffer blob;
				rs->GetResultBlob(1, blob);
				if (blob.GetDataLen() > 0) {
					ibReaderMemory reader(blob);
					return outBc.DeserializeAOT(reader);
				}
			}
		}
	}
	catch (...) { return false; }
	return false;
}

void ibByteCodeCache::Invalidate(const ibGuid& descId)
{
	if (db_query == nullptr) return;
	if (!db_query->TableExists(bytecode_cache_table)) return;

	wxASSERT(descId.isValid());

	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(bytecode_cache_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")),
			        ibConst(ibValue(wxString(descId))))));
	}
	catch (...) { /* best-effort — Invalidate is hygiene, not correctness */ }
}

void ibByteCodeCache::InvalidateAll()
{
	if (db_query == nullptr) return;
	if (!db_query->TableExists(bytecode_cache_table)) return;
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(bytecode_cache_table));   // no WHERE = all rows
	}
	catch (...) { /* best-effort */ }
}
