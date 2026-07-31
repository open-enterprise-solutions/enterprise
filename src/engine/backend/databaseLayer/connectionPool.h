#ifndef __IB_CONNECTION_POOL_H__
#define __IB_CONNECTION_POOL_H__

// ibConnectionPool — bounded pool of ibDatabaseLayer connections.
//
// The pool owns the master connection (passed to Init as a shared_ptr)
// plus up to maxSize-1 clones created lazily on demand. Every check-out
// hands the caller a shared_ptr<ibDatabaseLayer> with a custom deleter
// that does NOT actually destroy the layer — instead it flips the
// owning entry's inUse flag back to false so the entry becomes
// available for the next Checkout. Because the layer object is always
// reachable through the entry's pool-held shared_ptr (or, post-
// Shutdown, the closure-captured `sp`), the first-shared_ptr-wins
// rule of std::enable_shared_from_this is satisfied once and stays
// satisfied: callers inside the layer can rely on
// `shared_from_this()` for the lifetime of the pool.
//
// All conns the pool knows about live in a single registry —
// m_entries. Each entry encodes its current state through the
// txHolder / scopeHolder / inUse triple; see the struct comment
// below for the IDLE / BORROWED / RESERVED states. Active-transaction
// reservation is a property of the entry, not a separate map:
// BeginTransaction sets entry.txHolder, Commit/RollBack clears it.
//
// Public surface — minimal:
//
//   Init / Shutdown            — lifecycle (driven by ibApplicationData).
//   IsInitialised()            — lifecycle probe.
//   GetFreeConnection()        — RAII scope factory; same as a
//                                default ibConnectionScope().
//   GetDatabaseLayer()         — backs the global `db_query` macro.
//
// Everything else (CurrentHolder, ThreadHolder, GetPrimaryConnection,
// Checkout, holder-keyed reservation primitives, scope-binding) is
// internal. End users go through the holder methods
// (GetConnection / AcquireFreeConnection) or ibConnectionScope, never
// through the pool directly.

#include "backend/backend.h"
#include "backend/appDataCtorToken.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

#include "connectionScope.h"

class ibDatabaseLayer;
class ibDatabaseConnectionHolder;
class ibSingleConnectionHolder;

class BACKEND_API ibConnectionPool {
public:
	~ibConnectionPool();

	ibConnectionPool(const ibConnectionPool&) = delete;
	ibConnectionPool& operator=(const ibConnectionPool&) = delete;

	// Construction restricted to ibApplicationData via the
	// ib::AppDataCtorToken gate — appData owns the pool for its
	// lifetime, same pattern as the other appData-owned subsystems.
	// Callers reach the pool through ibApplicationData::GetConnectionPool().
	explicit ibConnectionPool(ib::AppDataCtorToken);


	// Initialise. `primary` is the already-opened master connection —
	// the pool takes shared ownership and also uses it as the first
	// hand-out-able conn. `maxSize` caps total live conns; once
	// reached, Checkout blocks until a prior borrower releases.
	// `minIdle` is the floor for idle-shrink: idle clones beyond this
	// count get closed after `kIdleTimeout` of inactivity, keeping
	// at least minIdle conns warm for fast re-acquire.
	//
	// Re-initialising replaces m_source and resets the entry list.
	// Previously handed-out shared_ptrs stay valid via the closure-
	// captured master ref in their deleter.
	void Init(std::shared_ptr<ibDatabaseLayer> primary, std::size_t maxSize, std::size_t minIdle = 2);

	// Close and drop every connection the pool holds. Idle and
	// outstanding checkouts are both invalidated — callers must be
	// stopped before this is called. Idempotent.
	void Shutdown();

	// Acquire a connection scope for the calling holder's work. The
	// scope's ctor handles the priority chain internally:
	//   1. Inherit a scope already bound to the holder (nested
	//      scopes share one checkout).
	//   2. Else adopt the holder's active-TX conn if any.
	//   3. Else Checkout a fresh clone and bind it to the holder.
	//
	// Typical usage:
	//   ibConnectionScope scope = ibConnectionPool::GetFreeConnection();
	//
	// On dtor the scope-binding for the holder is cleared and the
	// entry's inUse flag flips back.
	static ibConnectionScope GetFreeConnection();

	// Lifecycle probe — true after Init has set up a master conn and
	// before Shutdown has dropped it. Used by appData's destroy path
	// to short-circuit teardown when the pool was never wired up
	// (e.g. CLI invocation that exits before connecting to a DB).
	bool IsInitialised() const;

	// Diagnostics — current pool sizing. LiveSize counts every entry
	// the pool knows about (idle + borrowed + tx-pinned + scope-bound);
	// IdleSize counts entries available for an immediate Checkout
	// (txHolder == nullptr && scopeHolder == nullptr && !inUse).
	// MaxSize / MinIdle return the configured caps. Used by /admin
	// diagnostics and load tests; cheap (single mutex acquire + linear
	// scan over a small vector).
	std::size_t LiveSize() const;
	std::size_t IdleSize() const;
	std::size_t MaxSize() const;
	std::size_t MinIdle() const;

	// The `db_query` entry point. Returns the conn the calling
	// holder should use, with priority:
	//   1. Active TX pin for the holder.
	//   2. Active scope binding for the holder.
	//   3. Primary / master conn (legacy fallback).
	// Returns nullptr only when the pool is not initialised.
	static std::shared_ptr<ibDatabaseLayer> GetDatabaseLayer();

	// The db_query channel's per-thread holder identity — the connection-lifetime anchor for
	// non-session (designer / CLI) db_query work. Each thread gets its own pool reservation key, so
	// concurrent non-session calls run on independent connections. Exposed so a subsystem that owns
	// per-save state keyed on the channel (the DDL/DML restructuring barrier in ibSchemaBuilder)
	// resolves the SAME holder across the save when no explicit holder was given.
	static ibDatabaseConnectionHolder* ThreadHolder();

public:
	// Default upper bound on how long Checkout() blocks when the pool is
	// saturated (all m_maxSize connections busy). On expiry it throws instead of
	// waiting forever, so a leaked / stuck borrower can't hang the worker thread.
	// Public because a caller that knows its work must not stall picks its own
	// bound against this one (see Checkout below).
	static constexpr std::chrono::seconds kCheckoutTimeout { 30 };

private:
	// --- Internal API -------------------------------------------------------

	// db_query channel only — returns ThreadHolder. Pool stays
	// holder-agnostic: it doesn't know about ibSession. Session-bound
	// work passes its own holder explicitly via
	// `ibConnectionScope(ibSession::Current()->Holder())` (or
	// `ibSession::Current()->OpenConnectionScope()`).
	static ibDatabaseConnectionHolder* CurrentHolder();

	// (ThreadHolder moved to the public section above — the barrier in ibSchemaBuilder resolves the
	//  db_query channel's holder through it; CurrentHolder still uses it internally.)

	// Master connection accessor — the conn that the pool Clone()s
	// from. Used as a fallback by GetDatabaseLayer.
	static std::shared_ptr<ibDatabaseLayer> GetPrimaryConnection();

	// Borrow a connection. Blocks if all clones are checked out and
	// the pool is at maxSize. Returns nullptr after Shutdown.
	// External use is funneled through ibDatabaseConnectionHolder::
	// AcquireFreeConnection (raw borrow) and ibConnectionScope (RAII
	// scope-bound borrow); both are friends.
	//
	// HOW LONG TO WAIT IS THE CALLER'S, not the pool's. The default is the
	// half-minute above — the right answer for work somebody started and is
	// waiting on, where failing early only means asking again. It is the wrong
	// answer for work that is merely SERVING somebody: a rented run reads one
	// portion for a form that already has rows on screen, so "the pool is full"
	// has to come back as an answer it can act on rather than as a stall. Same
	// bound, named by whoever knows what the wait costs.
	std::shared_ptr<ibDatabaseLayer> Checkout(
		std::chrono::milliseconds wait = kCheckoutTimeout);

	// Symmetric counterpart to Checkout — drops the caller's
	// shared_ptr (its deleter clears entry.inUse). Most callers just
	// let the shared_ptr drop on scope exit; kept for symmetry.
	void Return(std::shared_ptr<ibDatabaseLayer> conn);

	// Active-transaction state — driven by ibDatabaseLayer's
	// BeginTransaction / Commit / RollBack at depth 0↔1 transitions.
	// SetActiveTxConnection resolves the holder via CurrentHolder()
	// (or the conn's existing scope-binding for the ad-hoc holder
	// pattern); ClearActiveTxConnection reads conn->GetHolder() and
	// releases that holder's pin. Internal — exposed only to the
	// layer through static-method visibility.
	static std::shared_ptr<ibDatabaseLayer> GetActiveTxConnection();
	static void SetActiveTxConnection(std::shared_ptr<ibDatabaseLayer> conn);
	static void ClearActiveTxConnection(ibDatabaseLayer* conn);

	// Holder-keyed reservation primitives. Used by SetActive*/Clear*
	// above and by ibConnectionScope (via friend) to bind / unbind /
	// look up.
	void ReserveTx(ibDatabaseConnectionHolder* holder,
	               std::shared_ptr<ibDatabaseLayer> conn);
	void ReleaseTx(ibDatabaseConnectionHolder* holder);
	std::shared_ptr<ibDatabaseLayer> GetReservedTx(
	               ibDatabaseConnectionHolder* holder) const;

	void BindScopeHolder(ibDatabaseConnectionHolder* holder,
	                     std::shared_ptr<ibDatabaseLayer> conn);
	void UnbindScopeHolder(ibDatabaseConnectionHolder* holder);
	std::shared_ptr<ibDatabaseLayer> GetScopeConn(
	                     ibDatabaseConnectionHolder* holder) const;

	// Reverse lookup — given a conn, return the holder bound to it
	// (txHolder if pinned, otherwise scopeHolder). Used by
	// SetActiveTxConnection to resolve the right holder when the
	// calling thread has no Current() session.
	ibDatabaseConnectionHolder* FindBoundHolder(ibDatabaseLayer* conn) const;

	// Convenience: release every pool registration keyed on `holder`
	// (TX pin + scope binding) and close any leftover stmt/rs on
	// the conn. Idempotent. Used by ibSingleConnectionHolder's dtor
	// for self-cleanup.
	void ReleaseAll(ibDatabaseConnectionHolder* holder);

	// Wrap a pool-owned shared_ptr as a hand-out for a caller. The
	// returned shared_ptr has its own control block whose custom
	// deleter clears entry.inUse when the caller drops it. The lambda
	// captures `sp` by value so the layer stays alive for the
	// hand-out's whole life (covers post-Shutdown drops).
	std::shared_ptr<ibDatabaseLayer> WrapHandout(
		std::shared_ptr<ibDatabaseLayer> sp);

	// Drop idle entries older than kIdleTimeout, keeping at least
	// m_minIdle alive and never dropping the master. Caller must hold
	// m_mutex. Closes and erases the dropped entries.
	void ReapStaleLocked();

	// Friends — exposed so the implementation can call private
	// internals without going through static facades. ibSession is
	// already a holder by inheritance; named here only for
	// documentation.
	friend class ibConnectionScope;
	friend class ibDatabaseConnectionHolder;
	friend class ibSingleConnectionHolder;
	friend class ibDatabaseLayer;

	// --- Storage ------------------------------------------------------------

	mutable std::mutex              m_mutex;
	std::condition_variable         m_cv;

	// Master connection. Always kept alive by the pool so Clone() has
	// a live source even when every clone is currently checked out.
	std::shared_ptr<ibDatabaseLayer>              m_source;

	// Single connection registry — every conn the pool knows about
	// lives here. Three logical states are encoded by the entry's flags:
	//
	//   txHolder == nullptr && scopeHolder == nullptr && !inUse
	//                                  →  IDLE (available for Checkout)
	//   txHolder == nullptr && scopeHolder == nullptr &&  inUse
	//                                  →  BORROWED (handed out, no
	//                                      holder reservation yet)
	//   txHolder != nullptr            →  RESERVED for active TX;
	//                                      survives drop of the hand-
	//                                      out that started it
	//   scopeHolder != nullptr         →  scope-bound to the holder;
	//                                      nested scopes inherit
	//
	// Example: maxSize=6, 3 sessions with open TX, 3 conns idle —
	// m_entries has 6 rows; 3 with txHolder set + their startedAt, 3
	// with everything null. A watchdog walks m_entries looking for
	// txHolder!=null && now-startedAt > threshold to surface hung
	// transactions per holder.
	struct ibConnectionEntry {
		std::shared_ptr<ibDatabaseLayer>      conn;
		ibDatabaseConnectionHolder*           txHolder    = nullptr;
		ibDatabaseConnectionHolder*           scopeHolder = nullptr;
		std::chrono::steady_clock::time_point lastUsed;
		std::chrono::steady_clock::time_point startedAt;  // when txHolder was set
		bool                                  inUse   = false;
		bool                                  noWait  = false;
	};
	std::vector<ibConnectionEntry>  m_entries;
	static constexpr std::chrono::seconds kIdleTimeout { 60 };

	std::size_t                     m_maxSize   = 0;
	std::size_t                     m_minIdle   = 2;
	bool                            m_shutdown  = false;
};

#endif  // __IB_CONNECTION_POOL_H__
