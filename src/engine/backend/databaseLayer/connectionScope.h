#ifndef __IB_CONNECTION_SCOPE_H__
#define __IB_CONNECTION_SCOPE_H__

// ibConnectionScope — per-function / per-block RAII that ACQUIRES a
// connection from ibConnectionPool and pins it to a holder for the
// duration of the scope. The scope's `operator->` exposes the
// underlying ibDatabaseLayer, so a temporary like
//   ibConnectionScope()->RunQuery(...)
// is the canonical replacement for the legacy `db_query->RunQuery(...)`
// on any non-session call site. While the scope is alive, db_query
// access from the same holder resolves to this scope's connection
// regardless of which thread the call lands on (worker dispatch,
// debug eval); other holders see their own scope or a fresh
// checkout.
//
// Holder selection priority inside the ctor:
//   1. Explicit `customHolder` argument — for subsystems that need a
//      dedicated non-default channel.
//   2. ibConnectionPool::CurrentHolder() — session if bound, else the
//      process-wide db_query singleton. Same identity that the
//      `db_query` macro routes through, so the global and the scope
//      always land on the same pool entry on non-session threads.
//
//   bool ibValueRecordDataObjectCatalog::DeleteObject() {
//       ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
//       scope.SafeBeginTransaction();
//       scope->RunQuery(...);                 // or db_query->RunQuery — same conn
//       scope.SafeCommitTransaction();
//   }                                         // ~scope → pool repark
//
// Nested scopes for the same holder share the parent's connection.
// Only the outer-most scope actually performs a pool Checkout; inner
// scopes look up the holder's bound entry, reuse the same connection,
// and do NOT touch the pool at their own ctor / dtor. Nested
// Safe*Transaction calls collapse onto one real driver transaction
// through the base-class counter layer (see databaseLayer.h).
//
// Transaction API is merged into the scope via Safe{Begin,Commit,
// RollBack}Transaction. The dtor rolls back any unresolved Begin so
// an exception between Safe*Begin and Safe*Commit cleans up
// automatically.
//
// Fallback:
//   - If the pool is not initialised (early startup / unit-test
//     harness) or is saturated, the scope is "passive" — it has no
//     connection of its own and does not touch the TL slot. The
//     `db_query` macro then continues to resolve via the process-
//     wide ibApplicationData::m_db, preserving legacy behaviour
//     rather than failing.

#include "backend/backend.h"

#include <memory>

#include "databaseLayer.h"

class ibDatabaseConnectionHolder;

class BACKEND_API ibConnectionScope
{
public:
	// Acquire a connection. Holder-aware: if a scope is already active
	// for ibSession::Current(), inherit its connection (nested scopes
	// share one pool checkout per holder); otherwise reuse the holder's
	// TX-pinned conn if any, or fall through to a fresh pool Checkout
	// bound to the holder.
	//
	// Usage — direct or via the factory on ibConnectionPool:
	//   ibConnectionScope scope;
	//   ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
	//
	// The factory form relies on mandatory RVO / move: the scope is
	// constructed once at the caller's variable location. No
	// intermediate shared_ptr round-trip.
	ibConnectionScope();

	// Acquire a connection bound to an explicit private holder instead
	// of the calling thread's session. Useful for subsystems whose DB
	// work cannot live inside a session (admin commands, daemon job
	// runners, infrastructure save paths — see ibUserInfo::Save). The
	// holder is typically a thread_local singleton owned by the
	// subsystem; the scope reserves a conn against it and releases on
	// dtor. Nested ibConnectionScope instances using the same custom
	// holder inherit (passive) the same conn.
	explicit ibConnectionScope(ibDatabaseConnectionHolder* customHolder);

	~ibConnectionScope();

	ibConnectionScope(const ibConnectionScope&)            = delete;
	ibConnectionScope& operator=(const ibConnectionScope&) = delete;

	// Move-enabled so the factory `GetFreeConnection() -> scope` form
	// compiles on implementations that don't elide the return. On
	// move the destination inherits the owns-conn flag; the source
	// is left passive (its dtor becomes a no-op).
	ibConnectionScope(ibConnectionScope&& other) noexcept;
	ibConnectionScope& operator=(ibConnectionScope&& other) noexcept;

	// Accessors — scope->Foo() and db_query->Foo() both reach the
	// same driver while the scope is bound to the holder.
	ibDatabaseLayer* operator->() const { return m_conn.get(); }
	ibDatabaseLayer* get()        const { return m_conn.get(); }
	explicit operator bool()      const { return m_conn != nullptr; }

	// Expose the underlying shared_ptr — needed when a consumer
	// takes shared ownership for its own lifetime (code that outlives
	// the scope). Copying the shared_ptr bumps the refcount so the
	// conn stays alive. Prefer `operator->` for simple in-scope
	// driver calls.
	const std::shared_ptr<ibDatabaseLayer>& shared() const { return m_conn; }

	// Transaction API — merged into the scope. The scope tracks
	// whether it has an unresolved Begin; the dtor rolls back any
	// unmatched Begin so an exception between Begin and Commit cleans
	// up automatically. "Safe" prefix distinguishes these from the
	// driver-direct `scope->BeginTransaction()` calls (which bypass
	// the scope's tracking).
	//
	// The actual nested-safe counter lives on ibDatabaseLayer (see
	// databaseLayer.h). Multiple scopes on the same thread all call
	// Safe* on the same shared conn; the counter collapses them onto
	// one real driver-level TX.
	void SafeBeginTransaction(const ibTxOptions& opts = {});
	void SafeCommitTransaction();
	void SafeRollBackTransaction();

	// Is this scope holding an unmatched SafeBeginTransaction? Informational.
	bool HasActiveTransaction() const { return m_activeTx; }

	// True if this scope actually owns a pool checkout (i.e. is the
	// outer-most scope for its holder). Inner scopes that inherit
	// their parent's connection return false. Informational — most
	// callers don't need to distinguish.
	bool Owns() const { return m_ownsConn; }

private:
	// Acquire from pool, optionally bound to an explicit holder; if
	// `customHolder` is null, the holder is resolved from the current
	// session (or nothing for non-session threads). Shared by both
	// public ctors.
	void Acquire(ibDatabaseConnectionHolder* customHolder);

	std::shared_ptr<ibDatabaseLayer> m_conn;      // checked-out (owner) OR inherited (nested)
	ibDatabaseConnectionHolder*      m_holder = nullptr;  // bound holder (for owner dtor unbind)
	bool                             m_ownsConn;  // true = pool checkout / scope-bind, false = inherited
	bool                             m_activeTx = false;  // unmatched BeginTransaction
};

#endif  // __IB_CONNECTION_SCOPE_H__
