////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.cpp
// Purpose     : ibByteCodeCache DAO implementation against sys_bytecode_cache.
////////////////////////////////////////////////////////////////////////////

#include "byteCodeCache.h"

#include <mutex>
#include <thread>       // TEMPORARY — thread id in the cache-write probe
#include <functional>

#include "backend/appData.h"
#include "backend/compiler/byteCode.h"
#include "backend/databaseLayer/connectionHolder.h"   // ibSingleConnectionHolder — the save's own channel
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: descriptor pilot
#include "core/fileSystem/fs.h"
#include "backend/utils/md5.hpp"   // the key is digested to the width its column declares
#include "backend/compiler/byteCodeFormat.h"   // kAOTFormatVersion — one half of the engine key
#include "engineFingerprintBuild.h"            // kEngineFingerprint — the other, generated at build

// Descriptor (ibRuntimeModuleDataObject) AOT-cache DAO, migrated onto the L2
// query door. The default-ctor ibDatabaseQueryBuilder resolves to
// ibConnectionPool::CurrentHolder() — the SAME pool entry the `db_query` macro
// routes through (connectionScope.h) — so this runs on the same connection it
// used to. The blob rides as an ibConstBlob (bound via SetParamBlob, never
// inlined); UPSERT stays DELETE-then-INSERT (uniform across drivers). A passive
// scope (pool not up) makes Execute throw NoConnection, which the surrounding
// try/catch turns into a cache miss → recompile — the same graceful degradation
// as the old `db_query == nullptr` guard. See docs/private/query-language-arc.md §17.
// The SAVE is the exception: it writes on a holder of its own, never inside the
// caller's business transaction (see Save).

// 🛑⭐⭐ THE KEY IS THE ENGINE AND THE CONFIGURATION, NOT THE CONFIGURATION ALONE.
//
// Bytecode is compiled by an engine. A key of the configuration alone kept serving a blob after
// the engine that compiled it was gone (2026-09-02): a global function added at noon was invisible
// for an hour, and touching one module — which changed the configuration digest — made every row
// unreachable and the function appeared. The symptom is code that quietly stays as it was.
//
// The first engine half was GetBuildStamp(), the __DATE__/__TIME__ of core/build.cpp. core is its
// own library and is not rebuilt when the compiler or the interpreter changes, so the stamp stayed
// put and a stale blob kept running. A hand-bumped version in its place has the same hole from the
// other side: a built-in added at noon, or a codegen change that keeps the opcodes, is invisible
// until somebody remembers the bump, and a release that forgets it serves the previous engine's
// bytecode. A miss costs one recompile. A hit of the wrong blob does not announce itself.
//
// So the engine half is two things. kAOTFormatVersion is the number a person bumps when an opcode's
// meaning changes, and the opcode test refuses a list that moved without it. The other is
// kEngineFingerprint, a hash of compiler/** and system/** made by compiler/engineFingerprint.cmake
// when the backend is built — the compiler, the interpreter, and the built-in registry. Nobody
// writes that hash down. A change in those sources is a different key on the next build.
//
// Kept as one KEY VALUE rather than a second column: a row from another engine is then NOT FOUND,
// instead of found and rejected by a check somebody has to remember to write.
//
// ⭐ HASHED DOWN TO 32, because that is what the COLUMN is (`config_md5`, ibTypeString(32)).
// Returning the raw spelling made the key longer than 32 and every statement touching it died
// with a string truncation; the misses looked like a cold cache (found 2026-09-04). A wider
// column would be a schema change for a table whose contents are disposable.
wxString ibByteCodeCache::EngineFingerprint()
{
	return wxString::FromUTF8(kEngineFingerprint);
}

wxString ibByteCodeCache::CacheKey(const wxString& configDigest)
{
	return ibMD5::ComputeMd5(wxString::Format(wxT("%u.%s.%s"),
		(unsigned)kAOTFormatVersion, EngineFingerprint(), configDigest));
}

bool ibByteCodeCache::Save(const ibByteCode& bc, const wxString& configDigest)
{
	if (db_query == nullptr) return false;

	// Serialize first — if AOT writer rejects the bytecode (e.g. a
	// non-primitive constant pool entry like TYPE_REFFER), we don't
	// want to leave the previous row deleted.
	ibWriterMemory writer;
	if (!bc.SerializeAOT(writer)) return false;
	const wxMemoryBuffer blob = writer.buffer();

	const wxString descIdStr = bc.m_id;
	const wxString bcVerStr  = bc.m_version;

	const wxString configMd5 = CacheKey(configDigest);

	// UPSERT via DELETE-then-INSERT (uniform across drivers; FB has no ON
	// CONFLICT). Both statements run on one builder = one connection.
	//
	// ⭐⭐ ON A CONNECTION OF ITS OWN, IN A TRANSACTION OF ITS OWN, AND NEVER WAITING — the lock
	// manager's rule (lockManager.h, m_lockHolder), for the same reason. The cache is disposable: a
	// save that does not happen costs one recompile. It was written through the SESSION's connection,
	// which is to say inside whatever business transaction happened to be open when a module was first
	// compiled — and that transaction then held the cache row until it committed. Every other session
	// compiling the same module met the row in its DELETE and waited, with no timeout at all.
	//
	// Measured 2026-09-10: a script filling the payroll demo ran for twenty minutes in one transaction;
	// pressing "Add" on a list of absences in the same application froze the window — a new document
	// builds its register records, the record set's module was compiled, and its save stood behind the
	// script's uncommitted row (stack under cdb: ibByteCodeCache::Save → RunQuery → the Firebird lock
	// wait). In production the same shape is one person posting a large document while another
	// cannot open a form. So the row commits the moment it is written, and a row somebody else holds
	// is simply not written this time (noWait: the conflict comes back at once and is swallowed below).
	try {
		ibSingleConnectionHolder own;
		ibDatabaseQueryBuilder q(&own);
		if (!q.IsOpen() || !q.TableExists(bytecode_cache_table))
			return false;   // the same connection asks  see Load

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

		ibDatabaseLayer::ibTxOptions txOpts;
		txOpts.noWait = true;   // a row another session is writing right now is its save, not ours
		q.BeginTransaction(txOpts);
		try {
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
			q.Commit();
		}
		catch (...) {
			if (q.IsActiveTransaction())
				q.RollBack();
			throw;   // → the catch below: not saved this time, recompiled next time
		}

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

bool ibByteCodeCache::Load(ibByteCode& outBc, const ibGuid& descId, const wxString& configDigest)
{
	if (db_query == nullptr) return false;

	wxASSERT(descId.isValid());

	// The same key the row was written under — the platform AND the configuration (see CacheKey).
	const wxString configMd5 = CacheKey(configDigest);

	try {
		// SELECT bc_blob FROM <t> WHERE descriptor_id = <descId> AND config_md5 = <configMd5>
		//
		// THE FINGERPRINT IS PART OF THE KEY, not a field somebody compares afterwards. A row written
		// against an earlier configuration is not "found and rejected" — it is not found. That removes
		// the whole class of "somebody forgot the check" and makes a save self-invalidating: saving
		// recomputes the configuration's digest, so every previously cached row falls out of reach in
		// the same instant, without a single DELETE having to run first.
		// ⭐⭐ THE TABLE CHECK RIDES THIS BUILDER, NOT `db_query`. Both ask the same question, but of
		// DIFFERENT CONNECTIONS: the builder opens a scope and gets one of its own, while `db_query`
		// with no transaction and no scope falls back to the pool's PRIMARY connection — the one
		// every other thread without a scope also lands on (connectionPool.cpp, GetDatabaseLayer).
		// Two threads then drive one connection: this one preparing `SELECT COUNT(*) FROM
		// RDB$RELATIONS` while the lock sweeper finishes its own statement on the same handle.
		//
		// Measured twice on 2026-09-08, at the same instant in the journal both times — the sweeper
		// deleting a dead session's `sys_lock` rows one millisecond before the failure:
		//     [error] db.firebird  Error retrieving Next record
		//     [warning] module     Common module init failed … invalid request handle
		// and earlier, the same collision arriving as `invalid transaction handle`. The startup is
		// exactly when it happens: modules are being compiled while the sweeper cleans up after a
		// runtime that was killed rather than closed.
		ibDatabaseQueryBuilder q;
		if (!q.TableExists(bytecode_cache_table))
			return false;

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


	wxASSERT(descId.isValid());

	try {
		ibDatabaseQueryBuilder q;
		if (!q.TableExists(bytecode_cache_table))
			return;   // nothing cached yet, nothing stale; asked on THIS connection (see Load)

		q.Execute(ibDelete(bytecode_cache_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("descriptor_id")),
			        ibConst(ibValue(wxString(descId))))));
	}
	catch (const ibCoreException& err) {
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
	try {
		ibDatabaseQueryBuilder q;
		if (!q.TableExists(bytecode_cache_table))
			return;   // see Load  the check belongs on the builder's own connection

		q.Execute(ibDelete(bytecode_cache_table));   // no WHERE = all rows
	}
	catch (...) { /* best-effort */ }
}
