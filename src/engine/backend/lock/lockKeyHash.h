/////////////////////////////////////////////////////////////////////////////
// A lock's key — its text and its hash.
//
// The conflict check reads the TEXT (Canonicalize, Overlap): two keys of
// one table meet when they agree on every field both name. Each field is
// written as its value's own hash (GetValueHash), so any value keys and
// nothing here asks a table what a reference is. The key's hash
// (ibLockItem::KeyHash) is its text's, and names one key in
// the lock file (ibLockFile::ibRow::m_keyHash).
//
// Field-iteration order MUST NOT affect either — callers may build the
// keyFields vector in any order; both sort by name.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_KEY_HASH_H_
#define _IB_LOCK_KEY_HASH_H_

#include "backend/lock/lockTypes.h"

namespace ibLockKey {

// Canonical 64-char hex SHA-256 over the sorted (name, value) byte
// stream, the values in their exact typed bytes (STRING, NUMBER,
// BOOLEAN, DATE). Identical keyFields (regardless of insertion order)
// yield the same hash on every process / driver / build. It is what
// saved settings are found by (ibSettingsStorage::HashKey), so it stays
// exact; for no fields it is the whole-table sentinel a lock header is
// kept under.
//
// Throws ibBackendCoreException if a value type isn't supported as a
// key. (Add new types here as call sites need them.)
BACKEND_API ibString Hash(const std::vector<std::pair<ibString, ibValue>>& keyFields);

// The key as text — a row's m_keyData, what a conflict is read from (Overlap): "<name>=<value hash>;…", sorted by name
// as Hash sorts. Two equal values write one hash — never a presentation, which two items may share. Empty for an
// empty keyFields vector.
BACKEND_API ibString Canonicalize(const std::vector<std::pair<ibString, ibValue>>& keyFields);

// ⭐⭐ DO TWO KEYS OF ONE TABLE MEET — the conflict question, asked of two Canonicalize texts. They meet when they agree
// on every field both name: a field only one of them names is any value to the other, so a key naming no field (the
// whole table) meets every key. `Product=A;Warehouse=W` meets `Product=A` and `Warehouse=W`, not `Product=B`.
BACKEND_API bool Overlap(const ibString& keyData, const ibString& otherKeyData);

// The names a Canonicalize text keys by, as one number (FNV-1a over them, their case aside) — never 0, so 0 stays "not
// known". Two keys of the same names meet exactly when their texts are one (Overlap's walk, without reading it): a
// table's writes all name the same fields, so the lock file compares them by two integers and a text.
BACKEND_API std::uint64_t FieldsHash(const ibString& keyData);

} // namespace ibLockKey

#endif // _IB_LOCK_KEY_HASH_H_
