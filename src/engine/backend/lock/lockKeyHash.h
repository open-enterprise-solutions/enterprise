/////////////////////////////////////////////////////////////////////////////
// Deterministic key hashing for sys_lock conflict detection.
//
// Acquire/Conflict-check looks up rows by (namespace, keyHash). The
// hash MUST be deterministic across processes / runs / locales so two
// Acquire calls with semantically-equal keys collide on the same
// keyHash regardless of who computes it. SHA-256 over a canonical
// byte serialization of the key field map.
//
// Field-iteration order MUST NOT affect the hash — callers may build
// the keyFields vector in any order; we sort by name internally
// before hashing. ibValue types supported as key values: STRING,
// NUMBER, BOOLEAN, DATE, REFFER (ibValueReferenceDataObject — the
// most common case for ref-keyed locks).
//
// Output: 64-char lowercase hex string. Fits the sys_lock.keyHash
// VARCHAR(64) column exactly.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_KEY_HASH_H_
#define _IB_LOCK_KEY_HASH_H_

#include "backend/lock/lockTypes.h"

namespace ibLockKey {

// Canonical 64-char hex SHA-256 over the sorted (name, value) byte
// stream. Identical keyFields (regardless of insertion order) yield
// the same hash on every process / driver / build.
//
// Throws ibBackendCoreException if a value type isn't supported as a
// key. (Add new types here as call sites need them.)
BACKEND_API wxString Hash(const std::vector<std::pair<wxString, ibValue>>& keyFields);

// Human-readable canonical text of the key — used as the keyData
// payload on sys_lock for conflict-message rendering. Same sort and
// type discipline as Hash, but emitted as "<name>=<value>;..." text
// rather than hashed bytes. Empty for an empty keyFields vector.
BACKEND_API wxString Canonicalize(const std::vector<std::pair<wxString, ibValue>>& keyFields);

} // namespace ibLockKey

#endif // _IB_LOCK_KEY_HASH_H_
