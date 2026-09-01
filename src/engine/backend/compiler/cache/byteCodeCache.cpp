////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.cpp
// Purpose     : ibByteCodeCache DAO implementation against sys_bytecode_cache.
////////////////////////////////////////////////////////////////////////////

#include "byteCodeCache.h"

#include <mutex>

#include "backend/appData.h"
#include "backend/compiler/byteCode.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: descriptor pilot
#include "backend/fileSystem/fs.h"
#include "backend/diagnostics/journal.h"   // ibJournal — an invalidation that did NOT happen must say so
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

bool ibByteCodeCache::Save(const ibByteCode& bc, const wxString& configMd5)
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
		// WEED THE PREVIOUS CONFIGURATIONS OUT, ONCE. Rows keyed on an older digest can never be found
		// again — which is the whole point — but "never found" is not "gone". The table cannot GROW
		// without bound (descriptor_id is its primary key, so one row per descriptor, and a recompile
		// replaces it), yet a descriptor the new configuration never compiles — a module that was
		// deleted, or simply one nobody has called yet — keeps a blob nothing can ever use. This is
		// the first moment the new digest is known to be real: a compile just succeeded under it. The
		// guard keeps it to one statement per digest per process, so the ordinary path stays a
		// DELETE-by-key plus an INSERT.
		// The guard is process-wide but reached from every session's compile path, so
		// the compare-and-set takes a lock: two threads assigning one wxString is a
		// torn string, not merely a duplicated DELETE. The statement itself runs
		// outside the lock — it is a database round-trip, and correctness only needs
		// "exactly one thread wins the digest".
		static std::mutex s_weedMutex;
		static wxString s_weededFor;

		bool bWeedNow = false;
		{
			std::lock_guard<std::mutex> lock(s_weedMutex);
			if (s_weededFor != configMd5) {
				s_weededFor = configMd5;
				bWeedNow = true;
			}
		}

		if (bWeedNow) {
			try {
				q.Execute(ibDelete(bytecode_cache_table,
					ibBinOp(ibQueryBinOp::Ne, ibCol(wxT("config_md5")), ibConst(ibValue(configMd5)))));
			} catch (...) { /* hygiene, not correctness — a stale row is unreachable either way */ }
		}
		q.Execute(ibDelete(bytecode_cache_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")), ibConst(ibValue(descIdStr)))));
		q.Execute(ibInsert(bytecode_cache_table, {
			{ wxT("descriptor_id"),    ibConst(ibValue(descIdStr)) },
			{ wxT("bytecode_version"), ibConst(ibValue(bcVerStr)) },
			{ wxT("config_md5"),       ibConst(ibValue(configMd5)) },
			{ wxT("bc_blob"),          ibConstBlob(blob.GetData(),
			                                       static_cast<size_t>(blob.GetDataLen())) },
		}));

		// THE OTHER HALF OF THE PAIR — see Load. Names the descriptor, the digest this row is written
		// under, and the bytecode's own version guid, so a row that is later TAKEN can be traced back
		// to the moment it was made. One line per save: a cache whose contents cannot be accounted for
		// is where "it ran the previous text" hides, and that costs a crash rather than a wrong answer.
		ibJournalInfo(wxT("bytecode.cache"), wxT("save %s md5=%s ver=%s bytes=%u"),
			descIdStr, configMd5, bcVerStr, (unsigned)blob.GetDataLen());
	}
	catch (...) { return false; }
	return true;
}

bool ibByteCodeCache::Load(ibByteCode& outBc, const ibGuid& descId, const wxString& configMd5)
{
	if (db_query == nullptr) return false;
	if (!db_query->TableExists(bytecode_cache_table)) return false;

	wxASSERT(descId.isValid());

	try {
		// SELECT bc_blob FROM <t> WHERE descriptor_id = <descId> AND config_md5 = <configMd5>
		//
		// THE FINGERPRINT IS PART OF THE KEY, not a field somebody compares afterwards. A row written
		// against an earlier configuration is not "found and rejected" — it is not found. That removes
		// the whole class of "somebody forgot the check" and makes a save self-invalidating: saving
		// recomputes the configuration's digest, so every previously cached row falls out of reach in
		// the same instant, without a single DELETE having to run first.
		ibDatabaseQueryBuilder q;
		ibQueryIR ir(
			ibProject(
				ibFilter(
					ibScan(bytecode_cache_table),
					ibBinOp(ibQueryBinOp::And,
					        ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")),
					                ibConst(ibValue(wxString(descId)))),
					        ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("config_md5")),
					                ibConst(ibValue(configMd5))))),
				{ { ibCol(wxT("bc_blob")), wxEmptyString } }));

		ibQueryResult res = q.ExecuteIR(ir);   // RAII: closes cursor + statement

		// WHY A ROW IS TAKEN. The key is meant to make a stale row unfindable: a save recomputes the
		// configuration's digest, and every row written under the previous one falls out of reach.
		// One got through anyway on 2026-09-01 — bytecode carrying a frame number this engine's
		// chain never had, and emptying the table cured it — so the question worth being able to ask
		// is not what the key GUARDS but why a lookup MATCHED.
		//
		// One line per lookup: the descriptor, the digest asked for, and whether it hit. Read beside
		// the Save line it separates the three readings — the digest never changed, the two doors
		// disagree about it, or a stale blob was written under a fresh one.
		const bool hit = res.Next();

		ibJournalInfo(wxT("bytecode.cache"), wxT("load %s md5=%s -> %s"),
			wxString(descId), configMd5, hit ? wxT("HIT") : wxT("miss"));

		if (hit) {
			// The blob is read through the RESULT's own typed accessor, BY NAME. It used to borrow the
			// raw driver cursor and read field 1 — an L2-1 leak by the header's own words, and the last
			// caller of it, so the hatch is gone with this line. By name rather than by position for
			// the ordinary reason: the projection above is what decides which column that is, and a
			// number here would go on compiling after somebody adds a second one.
			wxMemoryBuffer blob;
			res.GetResultBlob(wxT("bc_blob"), blob);
			if (blob.GetDataLen() > 0) {
				ibReaderMemory reader(blob);
				return outBc.DeserializeAOT(reader);
			}
		}
	}
	catch (...) { return false; }
	return false;
}

void ibByteCodeCache::Invalidate(const ibGuid& descId)
{
	// ⭐⭐ IT MAY DECLINE, BUT IT MAY NOT DO SO IN SILENCE.
	//
	// Three ways out of this function do nothing: no database handle, no table yet, and an exception
	// on the way. All three are legitimate — but each leaves a row holding bytecode compiled from
	// text that no longer exists, and a stale blob does not fail politely. It carries the previous
	// text's variable table, so a frame reference past the end of the chain reads the debug heap's
	// fence instead of a frame and the process dies (`Document.GoodsIssue.ObjectModule`, 2026-08-31
	// — found from a crash dump, because nothing anywhere had said a word).
	//
	// 🛑 THE WORD "HYGIENE" WAS DOING REAL DAMAGE HERE. It is true that correctness rests on the
	// cache KEY rather than on this call — and it is exactly the kind of true sentence that makes a
	// silent no-op look acceptable. Whether the key alone is enough is a question one has to be able
	// to ASK, and nobody could: this was the only place that knew, and it said nothing.
	if (db_query == nullptr) {
		ibJournalWarning(wxT("bytecode.cache"),
			wxT("invalidate skipped for '%s': no database connection"), wxString(descId));
		return;
	}

	if (!db_query->TableExists(bytecode_cache_table)) return;   // nothing cached yet — nothing stale

	wxASSERT(descId.isValid());

	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(bytecode_cache_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")),
			        ibConst(ibValue(wxString(descId))))));
	}
	catch (const ibBackendException& err) {
		ibJournalWarning(wxT("bytecode.cache"),
			wxT("invalidate failed for '%s': %s"), wxString(descId), err.GetErrorDescription());
	}
	catch (...) {
		ibJournalWarning(wxT("bytecode.cache"),
			wxT("invalidate failed for '%s'"), wxString(descId));
	}
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
