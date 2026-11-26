/////////////////////////////////////////////////////////////////////////////
// Pessimistic-lock types — request descriptors and result tags for
// ibLockManager (the base's lock file, ibLockFile).
//
// The lock file holds locks on VALUES of a table's fields — an object's own
// lock (its Ref, asked by its Lock() and held until it is let go) and the
// query's LOCK (the keys its WHERE names, held until the business
// transaction ends — the handle is its ticket,
// ibDatabaseLayer::AddTicket).
// The row a write touches is still serialized by the DB row lock (the
// record-locks Phase 5-7 path, see docs/private/record-locks.md).
//
// The namespace is the table's identity — a metaobject's guid
// (GetDocPath) — or a subsystem's own word ("Job.<guid>"). New kinds
// add their own without touching the lock layer.
//
// Lifetime = held until the handle is released (ibLockHandle dtor /
// Release — an object let go, a transaction ending, a named LOCK's UNLOCK),
// or session-end cleanup (registry ProcessRemove), or zombie sweep
// (heartbeat cutoff in registry's JobSweepStale).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_TYPES_H_
#define _IB_LOCK_TYPES_H_

#include "backend/backend.h"
#include "backend/compiler/value.h"

#include <cstdint>
#include <vector>
#include <utility>

// Lock mode — Shared coexists with other Shared, Exclusive blocks all.
// A write and an object's own lock hold Exclusive; Shared is a read that
// keeps writers out until it is done — a LOCK SHARED, a record set's
// Read in a transaction.
enum class ibLockMode : std::int32_t {
	Shared    = 0,
	Exclusive = 1,
};

// ONE FIELD'S RANGE — every value from `from` to `to`, both in it. An empty bound is open, and so is one neither a date
// nor a number: the lock file knows no order of it. A LOCK's `>` / `<` is brought to it by the LOCK (queryLock.cpp).
struct BACKEND_API ibLockRange {
	ibString field;
	ibValue  from;
	ibValue  to;
};

// One lock request inside an Acquire batch. keyFields are the VALUES it
// holds — {"Ref", <reference>} for an object, the fields a LOCK's WHERE ties
// to a value for a table — and keyRanges the ranges of the fields its WHERE
// bounds (`Period BETWEEN &From AND &To`). A field it does not name is any value: two
// keys of one namespace meet when they agree on every field both name — a
// value in a range, two ranges that cross (ibLockKey::Overlap) — and a key
// naming nothing — the whole namespace — meets every key.
//
// Typed factories `ForRef` / `ForNamespace` cover the common shapes —
// call sites construct the item via them so the magic "Ref" field
// name + the SHA-256 key-hash derivation never live in user code.
// Hash and canonical-text payload are owned by the item via
// KeyHash() / KeyData(); lockManager reads them during
// Acquire's conflict-check + INSERT and no longer composes the hash
// itself. Adding a new resource kind = adding a new `For*` factory.
struct BACKEND_API ibLockItem {
	ibString                                  namespaceName;
	std::vector<std::pair<ibString, ibValue>> keyFields;
	std::vector<ibLockRange>                  keyRanges;   // a field each — `Period BETWEEN &A AND &B`, `Period >= &A`
	ibLockMode                                lockMode = ibLockMode::Exclusive;
	// ⭐ WHAT A REFUSAL NAMES - the locked thing as a person knows it (a metaobject's synonym), given by whoever asks
	// for the lock, which is the one that knows it. The namespace is an identity, a metaobject's guid: a conflict
	// said "3f2a…-… is locked by another session" (2026-11-19). Empty: the refusal names the namespace.
	ibString                                  objectName;

	ibLockItem() = default;

	// Ref-object lock (Catalog / Document / ChartOf*) keyed by the
	// object's REFERENCE under its metaobject's namespace. The field is
	// "Ref", as the query language names it, and the value the reference
	// itself — so an object's own lock and a `LOCK … WHERE Ref = &Item` write one
	// key and meet. (It was the guid as text, which hashes as a text.)
	static ibLockItem ForRef(const ibString& namespacePath,
	                          const ibValue&  ref,
	                          ibLockMode      mode = ibLockMode::Exclusive,
	                          const ibString& objectName = ibEmptyString) {
		ibLockItem it;
		it.namespaceName = namespacePath;
		it.keyFields.push_back({ ibT("Ref"), ref });
		it.lockMode = mode;
		it.objectName = objectName;
		return it;
	}

	// Whole-namespace lock — no per-key sub-identifier. Single-row
	// "global" resources (Constants — one row per metaobject, the
	// namespace IS the identifier) collide on the namespace alone.
	static ibLockItem ForNamespace(const ibString& namespacePath,
	                                ibLockMode      mode = ibLockMode::Exclusive,
	                                const ibString& objectName = ibEmptyString) {
		ibLockItem it;
		it.namespaceName = namespacePath;
		it.lockMode = mode;
		it.objectName = objectName;
		return it;
	}

	// Hash + canonical text over keyFields and keyRanges — made from them AS THEY ARE at the call. Implementations live
	// in lockKeyHash.cpp alongside Hash() / Canonicalize() so the bytes-layout discipline stays in one TU.
	// ⚠ NOT CACHED: keyFields is a public field, and a cache it does not reset is a key that lies. The provider brings
	// each value to its field's type (ibDbTableProvider::ExecuteLock) AFTER the LOCK facade had asked KeyData to drop
	// repeats, and the cached text — the value as written, 1.006 — went to the manager in place of the field's 1.01
	// (found live 2026-11-26).
	ibString KeyHash() const;
	ibString KeyData() const;
};

// Acquisition options. `wait` is the table header's: another acquire of
// the same table is waited for at the driver's lock timeout (FB: 30s via
// TPB); wait=false refuses at once. `timeoutMs` is the held lock's: 0 —
// a lock another session holds refuses at once (an object's Lock() over
// one somebody holds); > 0 — it is waited out for that long, asked
// again as the holder lets go, then refused (a LOCK in a transaction).
// allowUpgrade controls in-place S→X promotion on re-entrant acquire by
// the same session.
struct BACKEND_API ibLockOptions {
	bool         wait         = true;
	std::int32_t timeoutMs    = 0;
	bool         allowUpgrade = true;
};

// HOW LONG A QUERY'S LOCK WAITS FOR ANOTHER SESSION TO LET GO before it is refused — as long as the base waits for a
// row another transaction holds (the driver's lock timeout, FB 30 s), so a LOCK and the write behind it wait alike.
constexpr std::int32_t kLockWaitMs = 30000;

#endif // _IB_LOCK_TYPES_H_
