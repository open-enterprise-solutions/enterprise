////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.h
// Purpose     : Persistent storage for compiled ibByteCode blobs, keyed by
//               descriptor GUID. Backed by sys_bytecode_cache (DDL via
//               ibApplicationInstance::MigrateTableBytecodeCache).
//
// Cache is transparent — when Load returns true the descriptor skips the
// compile path; when it returns false the descriptor falls back to
// ibCompileCode::Compile() and re-populates the row through Save. All
// methods are no-ops if the cache table doesn't exist (defensive against
// fresh DBs where Migrate hasn't run yet).
//
// VALIDITY IS A KEY, NOT A CHECK. A row is looked up by (descriptor_id,
// config_md5) — the configuration's own digest, recomputed on every load
// and every save. Bytecode compiled against an earlier configuration is
// therefore not "found and rejected"; it is not found, and the caller
// takes the ordinary compile path. Saving the configuration produces a new
// digest and every previously cached row falls out of reach in that same
// instant, with no DELETE needing to run and nobody needing to remember to
// run it. Invalidate() stays as hygiene (reclaiming space), no longer as
// correctness.
//
// This replaced a validity rule of "the row exists", propped up by
// Designer-side Invalidate hooks. The header of byteCodeAOT.cpp had long
// DESCRIBED a fingerprint of source_hash + metadata_version +
// compiler_version + kind_bindings_hash and the code checked none of them,
// so anything that changed the meaning of the bytecode without touching
// the module text — a renamed attribute, a changed type, a base opened by
// a second tool — kept serving the stale blob. It cost half a day twice.
// One digest of the whole configuration covers every one of those causes,
// because the module text and the metadata shape are both inside it.
//
// The engine half of the key is kAOTFormatVersion (compiler/byteCodeFormat.h)
// and the hash of compiler/ and system/ generated when the backend is built
// (compiler/engineFingerprint.cmake). DeserializeAOT still rejects a blob
// whose header carries another number.
////////////////////////////////////////////////////////////////////////////

#ifndef __IB_BYTECODE_CACHE_H__
#define __IB_BYTECODE_CACHE_H__

#include "backend/backend.h"

#include <memory>

struct ibByteCode;   // defined as struct in compiler/byteCode.h
class ibGuid;

class BACKEND_API ibByteCodeCache {
public:
	// ⭐ A MODULE NOBODY HAS COMPILED YET IS COMPILED ONCE, WHOEVER ELSE ASKS MEANWHILE. Ten sessions
	// entering a base whose cache is empty all miss, and without this all ten compile the same module.
	//
	// The first to miss holds the module's row: inserted and not committed, on a connection of its own,
	// so it holds nothing of its session's. The others' inserts stand behind that row in the database
	// (a primary key waits for the transaction that holds it). Saved, the row is committed, their insert
	// conflicts, Acquire answers false and they read what was saved. Not saved — the compile failed, the
	// session died — the row goes back, and the next one in the queue holds it and compiles.
	//
	// Nobody reads an empty row: until it is written it is seen by nobody but its holder.
	//
	// ⚠ HELD ACROSS THE COMPILE ONLY, in the scope of that compile. Its transaction takes the thread's
	// db_query channel when that is free (ibConnectionPool::SetActiveTxConnection), so what this thread
	// reads meanwhile goes through it. A compile writes nothing, which is what makes that harmless.
	class ibRowLock {
	public:
		ibRowLock();
		~ibRowLock();
		// true — the row is held, compile. false — it was not taken: somebody finished while this waited,
		// or the database could not be asked. Either way read the cache again; a miss then compiles unheld.
		bool Acquire(const ibGuid& descId, const ibString& configMd5);
		// Writes the row and lets it go. false — nothing is held, or the write failed.
		bool Save(const ibByteCode& bc);
	private:
		struct ibHeld;
		std::unique_ptr<ibHeld> m_held;
	};

	// Persist a compiled bytecode. Identity (descriptor_id, bytecode
	// version) is taken from `bc.m_id` / `bc.m_version`; the blob is
	// produced by SerializeAOT. UPSERT semantics — re-saves overwrite
	// the existing row.
	//
	// Returns false on serialization failure or DB error. Caller is
	// expected to log via the surrounding compile path.
	// `configMd5` — ibMetaData::GetConfigMD5() of the configuration this bytecode was compiled
	// against. Stored with the row and required to find it again.
	//
	// ⚠ WHAT IS STORED IS NOT ONLY THAT DIGEST. Bytecode belongs to the ENGINE that produced it as
	// much as to the configuration, so the row is keyed by BOTH — see CacheKey, and the day it
	// cost to find out (2026-09-02). Callers pass the configuration's digest and nothing else:
	// which engine this is, the cache knows for itself.
	static bool Save(const ibByteCode& bc, const ibString& configMd5);

	// The row key, digested to the 32 characters `config_md5` holds:
	// kAOTFormatVersion, the build-time engine hash, then the configuration
	// digest. Two engines that disagree on either half do not find each
	// other's rows.
	static ibString CacheKey(const ibString& configDigest);

	// The engine hash this build wrote into the key. A test compares it with
	// the script, including after a built-in source has been substituted.
	static ibString EngineFingerprint();

	// Try to populate `outBc` from cache. Returns:
	//   true  — row found, blob deserialized successfully. outBc is
	//           fully populated except for live pointers (m_parent,
	//           m_dependencies) which the caller wires up.
	//   false — no row, or DeserializeAOT rejected the blob (magic /
	//           format-version mismatch). Caller treats as miss and
	//           recompiles from source.
	static bool Load(ibByteCode& outBc, const ibGuid& descId, const ibString& configMd5);

	// DELETE the cache row for one descriptor. Used by Designer's
	// OnSaveMetaObject / OnDeleteMetaObject hooks to keep the cache
	// consistent when the source side moves under the bytecode.
	// Best-effort — silent on failure.
	static void Invalidate(const ibGuid& descId);

	// TRUNCATE — drops every row. Reserved for metadata reload paths
	// and admin force-recompile commands.
	static void InvalidateAll();
};

#endif // __IB_BYTECODE_CACHE_H__
