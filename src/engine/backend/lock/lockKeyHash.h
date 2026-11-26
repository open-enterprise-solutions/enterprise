/////////////////////////////////////////////////////////////////////////////
// A lock's key — its text and its hash.
//
// The conflict check reads the TEXT (Canonicalize, Overlap): two keys of
// one table meet when they agree on every field both name. A date and a
// number are written as themselves, in an order the text keeps, so a range
// can be read against them; any other value as its own hash (GetValueHash),
// so any value keys and nothing here asks a table what a reference is. The key's hash
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

// The key as text — a row's m_keyData, what a conflict is read from (Overlap): "<name>=<value>;…", sorted by name
// as Hash sorts. A date writes `@2026-01-31T23:59:59.999`, a number `#12.5`, any other value its hash — never a
// presentation, which two items may share; a range `[<from>,<to>]`, an open bound empty, and one open both ways is no
// field. Two equal values write one text. Empty for a key of no field.
BACKEND_API ibString Canonicalize(const std::vector<std::pair<ibString, ibValue>>& keyFields,
                                  const std::vector<ibLockRange>& keyRanges = {});

// ⭐⭐ DO TWO KEYS OF ONE TABLE MEET — the conflict question, asked of two Canonicalize texts. They meet when they agree
// on every field both name: a field only one of them names is any value to the other, so a key naming no field (the
// whole table) meets every key. `Product=A;Warehouse=W` meets `Product=A` and `Warehouse=W`, not `Product=B`. A value
// agrees with a range it lies in, two ranges when they cross; two of no order between them (a hash against a range of
// dates) agree, a lock wider than asked and never narrower.
BACKEND_API bool Overlap(const ibString& keyData, const ibString& otherKeyData);

// The names a Canonicalize text keys by, as one number (FNV-1a over them, their case aside, and over a range's text
// whole) — never 0, so 0 stays "not known". Two keys of the same number meet exactly when their texts are one
// (Overlap's walk, without reading it): a table's writes all name the same fields, so the lock file compares them by
// two integers and a text; a range crosses another of other bounds, and its number is its own.
BACKEND_API std::uint64_t FieldsHash(const ibString& keyData);

} // namespace ibLockKey

#endif // _IB_LOCK_KEY_HASH_H_
