/////////////////////////////////////////////////////////////////////////////
// Pessimistic-lock types — request descriptors and result tags for
// ibLockManager (sys_lock app-table coordination).
//
// sys_lock is for **long-held** locks — those that need to live across
// transaction boundaries (form-open hold until form-close, multi-step
// business workflows). Short-lived per-TX serialization is already
// covered by DB-level row locks (SELECT ... FOR UPDATE / WITH LOCK)
// in the record-locks Phase 5-7 path, see docs/record-locks.md.
//
// Lock granularity is per-key inside an open-ended namespace string.
// Conventions today:
//   "Catalog.<Name>"
//   "Document.<Name>"
//   "AccumulationRegister.<Name>.RecordSet"
//   "InformationRegister.<Name>.RecordSet"
//   ...
// New metaobject kinds add their own prefix without touching the lock
// layer.
//
// All locks are session-scoped: lifetime = held until explicit Unlock
// (via ibLockHandle dtor / Release), or session-end cleanup (registry
// ProcessRemove), or zombie sweep (heartbeat cutoff in registry's
// JobSweepStale).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_TYPES_H_
#define _IB_LOCK_TYPES_H_

#include "backend/backend.h"
#include "backend/compiler/value.h"
#include "backend/guid.h"

#include <cstdint>
#include <vector>
#include <utility>

// Lock mode — Shared coexists with other Shared, Exclusive blocks all.
// Auto-acquire paths (form-open) always use Exclusive; Shared is
// reserved for explicit script-side "block writers while I read
// consistently" semantics (rare).
enum class ibLockMode : std::int32_t {
	Shared    = 0,
	Exclusive = 1,
};

// One lock request inside an Acquire batch. Namespace strings follow
// the metaobject-path convention; keyFields are the identifying
// values (typically {"Ref", ibValue(refGuid)}; for non-recorder
// InformationRegister: dimension values).
//
// Empty keyFields means "the whole namespace" — coarse-grained lock
// used for periodic close / Apply only, never the default.
//
// Typed factories `ForRef` / `ForNamespace` cover the common shapes —
// call sites construct the item via them so the magic "Ref" field
// name + the SHA-256 key-hash derivation never live in user code.
// Hash and canonical-text payload are owned by the item via
// KeyHash() / KeyData() (lazy-cached); lockManager reads them during
// Acquire's conflict-check + INSERT and no longer composes the hash
// itself. Adding a new resource kind = adding a new `For*` factory.
struct BACKEND_API ibLockItem {
	wxString                                  namespaceName;
	std::vector<std::pair<wxString, ibValue>> keyFields;
	ibLockMode                                lockMode = ibLockMode::Exclusive;

	ibLockItem() = default;

	// Ref-object lock (Catalog / Document / ChartOf*) keyed by the
	// row's Ref GUID under the metaobject's "<Kind>.<Name>" namespace.
	// The field name "Ref" matches the row-identifier convention used
	// across mutable-ref tables; encapsulated here so call sites stay
	// free of the magic string.
	static ibLockItem ForRef(const wxString& namespacePath,
	                          const ibGuid&   refGuid,
	                          ibLockMode      mode = ibLockMode::Exclusive) {
		ibLockItem it;
		it.namespaceName = namespacePath;
		it.keyFields.push_back({ wxT("Ref"), ibValue(refGuid.str()) });
		it.lockMode = mode;
		return it;
	}

	// Whole-namespace lock — no per-key sub-identifier. Single-row
	// "global" resources (Constants — one row per metaobject, the
	// namespace IS the identifier) collide on the namespace alone.
	static ibLockItem ForNamespace(const wxString& namespacePath,
	                                ibLockMode      mode = ibLockMode::Exclusive) {
		ibLockItem it;
		it.namespaceName = namespacePath;
		it.lockMode = mode;
		return it;
	}

	// Hash + canonical text over keyFields — lazy, cached after first
	// compute. SHA-256 hex is never empty (Hash returns a zero-sentinel
	// for empty fields), so IsEmpty() reliably means "not cached yet"
	// for the hash. KeyData uses an explicit valid-bit because empty
	// keyFields legitimately give an empty canonical string.
	// Implementations live in lockKeyHash.cpp alongside Hash() /
	// Canonicalize() so the bytes-layout discipline stays in one TU.
	const wxString& KeyHash() const;
	const wxString& KeyData() const;

private:
	mutable wxString m_keyHashCached;
	mutable wxString m_keyDataCached;
	mutable bool     m_keyDataValid = false;
};

// Acquisition options. Default = wait at driver's normal lock-timeout
// (FB: 30s via TPB). Caller flips wait=false for instant fail-fast
// probe semantics. allowUpgrade controls in-place S→X promotion on
// re-entrant acquire by the same session.
struct BACKEND_API ibLockOptions {
	bool         wait         = true;
	std::int32_t timeoutMs    = 0;       // 0 = driver default
	bool         allowUpgrade = true;
};

#endif // _IB_LOCK_TYPES_H_
