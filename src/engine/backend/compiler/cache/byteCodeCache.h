////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.h
// Purpose     : Persistent storage for compiled ibByteCode blobs, keyed by
//               descriptor GUID. Backed by sys_bytecode_cache (DDL via
//               ibApplicationInstance::CreateTableBytecodeCache).
//
// Cache is transparent — when Load returns true the descriptor skips the
// compile path; when it returns false the descriptor falls back to
// ibCompileCode::Compile() and re-populates the row through Save. All
// methods are no-ops if the cache table doesn't exist.
//
// VALIDITY IS A KEY, NOT A CHECK. A row is looked up by (descriptor_id,
// application_md5) — the application's own digest, recomputed on every load
// and every save. Bytecode compiled against an earlier application is
// therefore not "found and rejected"; it is not found, and the caller
// takes the ordinary compile path. Saving the application produces a new
// digest and every previously cached row falls out of reach in that same
// instant, with no DELETE needing to run and nobody needing to remember to
// run it. Invalidate() stays as hygiene (reclaiming space), no longer as
// correctness.
//
// This replaced a validity rule of "the row exists", propped up by
// Studio-side Invalidate hooks. The header of byteCodeAOT.cpp had long
// DESCRIBED a fingerprint of source_hash + metadata_version +
// compiler_version + kind_bindings_hash and the code checked none of them,
// so anything that changed the meaning of the bytecode without touching
// the module text — a renamed attribute, a changed type, a base opened by
// a second tool — kept serving the stale blob. It cost half a day twice.
// One digest of the whole application covers every one of those causes,
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
#include "core/guid.h"                  // LoadAll's rows are keyed by descriptor…
#include "core/memory/memoryBuffer.h"   // …and are the blobs as they were saved

#include <functional>
#include <map>

struct ibByteCode;   // defined as struct in compiler/byteCode.h

class BACKEND_API ibByteCodeCache {
public:
	// Persist a compiled bytecode. Identity (descriptor_id, bytecode
	// version) is taken from `bc.m_id` / `bc.m_version`; the blob is
	// produced by SerializeAOT. UPSERT semantics — re-saves overwrite
	// the existing row.
	//
	// Returns false on serialization failure or DB error. Caller is
	// expected to log via the surrounding compile path.
	// `configMd5` — ibMetaData::GetApplicationMD5() of the application this bytecode was compiled
	// against. Stored with the row and required to find it again.
	//
	// ⚠ WHAT IS STORED IS NOT ONLY THAT DIGEST. Bytecode belongs to the ENGINE that produced it as
	// much as to the application, so the row is keyed by BOTH — see CacheKey, and the day it
	// cost to find out (2026-09-02). Callers pass the application's digest and nothing else:
	// which engine this is, the cache knows for itself.
	static bool Save(const ibByteCode& bc, const ibString& configMd5);

	// The row key, digested to the 32 characters `application_md5` holds:
	// kAOTFormatVersion, the build-time engine hash, then the application
	// digest. Two engines that disagree on either half do not find each
	// other's rows.
	static ibString CacheKey(const ibString& configDigest);

	// The engine hash this build wrote into the key. A test compares it with
	// the script, including after a built-in source has been substituted.
	static ibString EngineFingerprint();

	// Try to populate `outBc` from cache. Returns:
	//   true  — row found, blob deserialized, its dependencies resolved
	//           (ResolveAndVerifyDependencies). outBc is fully populated
	//           except for m_parent, which the caller wires up.
	//   false — no row, DeserializeAOT rejected the blob (magic /
	//           format-version mismatch), or a dependency is missing or
	//           moved — that row is invalidated. Caller treats as miss and
	//           recompiles from source.
	static bool Load(ibByteCode& outBc, const ibGuid& descId, const ibString& configMd5);

	// ⭐ A MODULE NOBODY HAS COMPILED YET IS COMPILED ONCE, WHOEVER ELSE ASKS MEANWHILE. Ten sessions entering a base
	// whose cache is empty all miss, and without this all ten compile the same module. A miss holds the module's key
	// (the row lock, byteCodeCache.cpp), reads again, and only then has `compile` make the bytecode, saved under that
	// key. false — `compile` failed.
	static bool Load(ibByteCode& outBc, const ibGuid& descId, const ibString& configMd5,
		const std::function<bool(ibByteCode&)>& compile);

	// Every row of an application, in ONE statement: the blobs as they were saved, by descriptor. What the
	// application's ibByteCodeStorage (metaData.h) reads as the application opens, so that a session coming up
	// does not read its modules' rows one by one. Empty when there are none, or when the base could not be asked.
	static std::map<ibGuid, ibMemoryBuffer> LoadAll(const ibString& configMd5);

	// DELETE the cache row for one descriptor. Used by Studio's
	// OnSaveMetaObject / OnDeleteMetaObject hooks to keep the cache
	// consistent when the source side moves under the bytecode.
	// Best-effort — silent on failure.
	static void Invalidate(const ibGuid& descId);

	// TRUNCATE — drops every row. Reserved for metadata reload paths
	// and admin force-recompile commands.
	static void InvalidateAll();
};

#endif // __IB_BYTECODE_CACHE_H__
