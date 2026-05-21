////////////////////////////////////////////////////////////////////////////
// Name        : byteCodeCache.h
// Purpose     : Persistent storage for compiled ibByteCode blobs, keyed by
//               descriptor GUID. Backed by sys_bytecode_cache (DDL via
//               ibApplicationData::MigrateTableBytecodeCache).
//
// Cache is transparent — when Load returns true the descriptor skips the
// compile path; when it returns false the descriptor falls back to
// ibCompileCode::Compile() and re-populates the row through Save. All
// methods are no-ops if the cache table doesn't exist (defensive against
// fresh DBs where Migrate hasn't run yet).
//
// Validity: a row is considered valid as long as it exists. The contract
// for keeping cache honest sits on the Designer side — OnSaveMetaObject /
// OnDeleteMetaObject hook Invalidate(descId) on every change. Bytecode
// format drift is caught by ibByteCode::DeserializeAOT's magic / format-
// version check (returns false ⇒ Load reports miss ⇒ recompile).
////////////////////////////////////////////////////////////////////////////

#ifndef __IB_BYTECODE_CACHE_H__
#define __IB_BYTECODE_CACHE_H__

#include "backend/backend.h"

struct ibByteCode;   // defined as struct in compiler/byteCode.h
class ibGuid;

class BACKEND_API ibByteCodeCache {
public:
	// Persist a compiled bytecode. Identity (descriptor_id, bytecode
	// version) is taken from `bc.m_id` / `bc.m_version`; the blob is
	// produced by SerializeAOT. UPSERT semantics — re-saves overwrite
	// the existing row.
	//
	// Returns false on serialization failure or DB error. Caller is
	// expected to log via the surrounding compile path.
	static bool Save(const ibByteCode& bc);

	// Try to populate `outBc` from cache. Returns:
	//   true  — row found, blob deserialized successfully. outBc is
	//           fully populated except for live pointers (m_parent,
	//           m_dependencies) which the caller wires up.
	//   false — no row, or DeserializeAOT rejected the blob (magic /
	//           format-version mismatch). Caller treats as miss and
	//           recompiles from source.
	static bool Load(ibByteCode& outBc, const ibGuid& descId, const ibGuid* expectedVersion = nullptr);

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
