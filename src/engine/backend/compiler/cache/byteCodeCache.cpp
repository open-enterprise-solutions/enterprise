////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.cpp
// Purpose     : ibByteCodeCache DAO implementation against sys_bytecode_cache.
////////////////////////////////////////////////////////////////////////////

#include "byteCodeCache.h"

#include "backend/appData.h"
#include "backend/compiler/byteCode.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/fileSystem/fs.h"
#include "backend/guid.h"

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

	// UPSERT via DELETE-then-INSERT — uniform across drivers (FB has
	// no ON CONFLICT; SQLite/PG do but emulating per-driver dialect
	// here would be more code than the round-trip saving). Both
	// statements run on the same db_query connection, so no isolation
	// concern beyond the calling thread's own work.
	{
		ibStatementGuard del(db_query,
			db_query->PrepareStatement(
				wxT("DELETE FROM %s WHERE descriptor_id = ?;"),
				bytecode_cache_table));
		if (!del) return false;
		del->SetParamString(1, descIdStr);
		try { del->RunQuery(); }
		catch (...) { return false; }
	}
	{
		ibStatementGuard ins(db_query,
			db_query->PrepareStatement(
				wxT("INSERT INTO %s (descriptor_id, bytecode_version, bc_blob) ")
				wxT("VALUES (?, ?, ?);"),
				bytecode_cache_table));
		if (!ins) return false;
		ins->SetParamString(1, descIdStr);
		ins->SetParamString(2, bcVerStr);
		// SetParamBlob(int, const wxMemoryBuffer&) is a no-op default
		// in ibPreparedStatement; the (void*, long) overload is the
		// one drivers actually implement.
		ins->SetParamBlob  (3, blob.GetData(),
		                    static_cast<long>(blob.GetDataLen()));
		try { ins->RunQuery(); }
		catch (...) { return false; }
	}
	return true;
}

bool ibByteCodeCache::Load(ibByteCode& outBc, const ibGuid& descId, const ibGuid* expectedVersion)
{
	if (db_query == nullptr) return false;
	if (!db_query->TableExists(bytecode_cache_table)) return false;

	wxASSERT(descId.isValid());

	ibStatementGuard sel(db_query,
		db_query->PrepareStatement(
			wxT("SELECT bytecode_version, bc_blob FROM %s WHERE descriptor_id = ?;"),
			bytecode_cache_table));
	if (!sel) return false;
	sel->SetParamString(1, wxString(descId));

	ibDatabaseResultSet* rs = nullptr;
	try { rs = sel->RunQueryWithResults(); }
	catch (...) { return false; }
	if (rs == nullptr) return false;

	bool ok = false;
	if (rs->Next()) {
		const wxString rowVersion = rs->GetResultString(1);
		if (expectedVersion != nullptr &&
		    ibGuid(rowVersion) != *expectedVersion) {
			sel->CloseResultSet(rs);
			return false;
		}
		wxMemoryBuffer blob;
		rs->GetResultBlob(2, blob);
		if (blob.GetDataLen() > 0) {
			ibReaderMemory reader(blob);
			ok = outBc.DeserializeAOT(reader);
			if (ok && expectedVersion != nullptr &&
			    outBc.m_version != *expectedVersion) {
				ok = false;
			}
		}
	}
	sel->CloseResultSet(rs);
	return ok;
}

void ibByteCodeCache::Invalidate(const ibGuid& descId)
{
	if (db_query == nullptr) return;
	if (!db_query->TableExists(bytecode_cache_table)) return;

	wxASSERT(descId.isValid());

	ibStatementGuard del(db_query,
		db_query->PrepareStatement(
			wxT("DELETE FROM %s WHERE descriptor_id = ?;"),
			bytecode_cache_table));
	if (!del) return;
	del->SetParamString(1, wxString(descId));
	try { del->RunQuery(); }
	catch (...) { /* best-effort — Invalidate is hygiene, not correctness */ }
}

void ibByteCodeCache::InvalidateAll()
{
	if (db_query == nullptr) return;
	if (!db_query->TableExists(bytecode_cache_table)) return;
	try { db_query->RunQuery(wxT("DELETE FROM %s;"), bytecode_cache_table); }
	catch (...) { /* best-effort */ }
}
