#include "connectionPool.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"   // ibBackendCoreException on pool-exhaustion timeout
#include "connectionHolder.h"
#include "connectionScope.h"
#include "databaseLayer.h"

ibDatabaseConnectionHolder* ibConnectionPool::ThreadHolder()
{
	// Per-thread holder identity for non-session db_query / default
	// ibConnectionScope() calls. Each thread gets its own reservation key,
	// so concurrent db_query work from different threads acquires
	// independent pool connections rather than serialising on one singleton.
	// Block-local thread_local static guarantees zero-init on first use per
	// thread, no global ctor ordering.
	static thread_local ibSingleConnectionHolder ts_holder;
	return &ts_holder;
}

ibDatabaseConnectionHolder* ibConnectionPool::CurrentHolder()
{
	// db_query channel only — per-thread holder. Sessions have their
	// own channel via session->Holder() (called explicitly by ses_query
	// or by ibConnectionScope(session->Holder())). No implicit session
	// routing here; mixing the two channels would couple db_query's
	// ad-hoc per-statement semantics with the session's long-lived
	// holder.
	return ThreadHolder();
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::GetActiveTxConnection()
{
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return nullptr;
	auto* holder = CurrentHolder();
	if (holder == nullptr) return nullptr;
	return pool->GetReservedTx(holder);
}

void ibConnectionPool::SetActiveTxConnection(std::shared_ptr<ibDatabaseLayer> conn)
{
	if (!conn) return;
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return;
	// Resolve the holder for the TX pin, in priority:
	//   1. CurrentHolder() — session if bound, else db_query singleton.
	//   2. The conn's already-pinned holder (set by an earlier ReserveTx
	//      on a different thread).
	//   3. The conn's scope-binding — lets an ad-hoc holder that
	//      opened a scope still pin its TX correctly.
	ibDatabaseConnectionHolder* holder = CurrentHolder();
	if (holder == nullptr) {
		if (auto* h = conn->GetHolder()) holder = h;
		else                             holder = pool->FindBoundHolder(conn.get());
	}
	if (holder == nullptr) return;
	pool->ReserveTx(holder, std::move(conn));
}

void ibConnectionPool::ClearActiveTxConnection(ibDatabaseLayer* conn)
{
	if (conn == nullptr) return;
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return;
	// The holder that pinned this layer is the source of truth — set
	// by ReserveTx, cleared by ReleaseTx. Read it before calling
	// release (ReleaseTx will null it out).
	if (auto* h = conn->GetHolder())
		pool->ReleaseTx(h);
}

ibDatabaseConnectionHolder* ibConnectionPool::FindBoundHolder(ibDatabaseLayer* conn) const
{
	if (conn == nullptr) return nullptr;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& e : m_entries) {
		if (e.conn.get() == conn) {
			if (e.txHolder)    return e.txHolder;
			if (e.scopeHolder) return e.scopeHolder;
			return nullptr;
		}
	}
	return nullptr;
}

void ibConnectionPool::ReserveTx(ibDatabaseConnectionHolder* holder,
                                 std::shared_ptr<ibDatabaseLayer> conn)
{
	if (holder == nullptr || !conn) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto now = std::chrono::steady_clock::now();

	for (auto& e : m_entries) {
		if (e.conn.get() == conn.get()) {
			// Defensive: re-pin if the entry was already reserved by
			// another holder (paired Begin without Commit). Drop the
			// stale back-pointer first.
			if (e.txHolder && e.txHolder != holder && e.conn)
				e.conn->m_holder = nullptr;
			e.txHolder  = holder;
			e.startedAt = now;
			conn->m_holder = holder;
			return;
		}
	}
	// Conn isn't in our registry — register as a reserved entry.
	// Happens when a holder begins a TX on a layer the pool didn't hand
	// out (Designer single-conn mode, externally-supplied conn).
	conn->m_holder = holder;
	ibConnectionEntry e;
	e.conn      = std::move(conn);
	e.txHolder  = holder;
	e.startedAt = now;
	e.lastUsed  = now;
	m_entries.push_back(std::move(e));
}

void ibConnectionPool::ReleaseTx(ibDatabaseConnectionHolder* holder)
{
	if (holder == nullptr) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& e : m_entries) {
		if (e.txHolder == holder) {
			if (e.conn) e.conn->m_holder = nullptr;
			e.txHolder  = nullptr;
			e.startedAt = {};
			e.lastUsed  = std::chrono::steady_clock::now();
			// Available again iff nothing else still holds the entry.
			if (!e.inUse && e.scopeHolder == nullptr)
				m_cv.notify_one();
			return;
		}
	}
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::GetReservedTx(
	ibDatabaseConnectionHolder* holder) const
{
	if (holder == nullptr) return nullptr;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& e : m_entries) {
		if (e.txHolder == holder)
			return e.conn;
	}
	return nullptr;
}

void ibConnectionPool::ReleaseAll(ibDatabaseConnectionHolder* holder)
{
	if (holder == nullptr) return;

	// Capture every conn keyed on this holder before clearing the
	// reservations — we'll close statements/result sets after dropping
	// the pool lock so driver calls don't run under m_mutex.
	std::vector<std::shared_ptr<ibDatabaseLayer>> conns;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& e : m_entries) {
			if ((e.txHolder == holder || e.scopeHolder == holder) && e.conn)
				conns.push_back(e.conn);
		}
	}

	ReleaseTx(holder);
	UnbindScopeHolder(holder);

	// Close any result sets / prepared statements the holder left
	// behind on its conn. Layers are returned to the idle pool after
	// release; the next checkout must not inherit dangling cursors or
	// pending statements from the previous user.
	for (auto& c : conns) {
		if (!c) continue;
		try { c->CloseResultSets(); } catch (...) { /* swallowed: cleanup before reparking conn — driver error here would otherwise leak into the next checkout */ }
		try { c->CloseStatements(); } catch (...) { /* swallowed: same as above */ }
	}
}

ibSingleConnectionHolder::ibSingleConnectionHolder() = default;

ibSingleConnectionHolder::~ibSingleConnectionHolder()
{
	// Self-clean on dtor — covers static instance teardown at process
	// exit and stack-local scope unwind. ReleaseAll is idempotent:
	// no-op if this holder never reserved anything.
	if (auto* pool = ibApplicationData::GetConnectionPool())
		pool->ReleaseAll(this);
}

ibConnectionScope ibDatabaseConnectionHolder::OpenConnectionScope()
{
	return ibConnectionScope(this);
}

std::shared_ptr<ibDatabaseLayer> ibDatabaseConnectionHolder::AcquireFreeConnection() const
{
	// Plain Checkout — no holder binding. The wrapped shared_ptr's
	// deleter parks the entry back in the pool when the caller drops
	// the last reference. Side-channel for work that must NOT join
	// the holder's current TX (parallel side query, async refresh).
	auto* pool = ibApplicationData::GetConnectionPool();
	return pool != nullptr ? pool->Checkout() : nullptr;
}

std::shared_ptr<ibDatabaseLayer> ibDatabaseConnectionHolder::EnsureConnection()
{
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return nullptr;
	// 1. TX-pinned > 2. scope-bound > 3. fresh Checkout + bind as scope.
	// Single entry point — replaced the old read-only GetConnection.
	// Auto-bind ensures multi-statement runtime paths share one conn:
	// without it, every call would Checkout a new conn and live result
	// sets on a previously-Acquired conn would leak (entry returned to
	// pool while busy → next Checkout skips → grows pool → deadlocks).
	if (auto tx = pool->GetReservedTx(this)) return tx;
	if (auto scope = pool->GetScopeConn(this)) return scope;
	auto conn = pool->Checkout();
	if (conn) pool->BindScopeHolder(this, conn);
	return conn;
}

void ibConnectionPool::BindScopeHolder(ibDatabaseConnectionHolder* holder,
                                       std::shared_ptr<ibDatabaseLayer> conn)
{
	if (holder == nullptr || !conn) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& e : m_entries) {
		if (e.conn.get() == conn.get()) {
			// First-bind wins — nested ibConnectionScope ctors observe
			// scopeHolder!=nullptr and inherit instead of attempting to
			// rebind. The outermost scope's dtor calls Unbind.
			if (e.scopeHolder == nullptr)
				e.scopeHolder = holder;
			return;
		}
	}
}

void ibConnectionPool::UnbindScopeHolder(ibDatabaseConnectionHolder* holder)
{
	if (holder == nullptr) return;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& e : m_entries) {
		if (e.scopeHolder == holder) {
			e.scopeHolder = nullptr;
			if (!e.inUse && e.txHolder == nullptr)
				m_cv.notify_one();
			return;
		}
	}
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::GetScopeConn(
	ibDatabaseConnectionHolder* holder) const
{
	if (holder == nullptr) return nullptr;
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& e : m_entries) {
		if (e.scopeHolder == holder)
			return e.conn;
	}
	return nullptr;
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::GetPrimaryConnection()
{
	auto* pool = ibApplicationData::GetConnectionPool();
	return pool != nullptr ? pool->m_source : nullptr;
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::GetDatabaseLayer()
{
	// `db_query` macro target — always ThreadHolder. Resolution is fixed to
	// the calling thread's own reservation key:
	//   1. TX-pinned conn  (ThreadHolder's BeginTransaction)
	//   2. Scope-bound conn (ThreadHolder's outer ibConnectionScope)
	//   3. Primary fallback (m_source)
	// Session-aware work uses session->Holder() directly (ses_query) and
	// never goes through CurrentHolder — pool stays holder-agnostic.
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return nullptr;
	auto* holder = ThreadHolder();
	if (auto txConn = pool->GetReservedTx(holder))
		return txConn;
	if (auto scopeConn = pool->GetScopeConn(holder))
		return scopeConn;
	return pool->m_source;
}

ibConnectionScope ibConnectionPool::GetFreeConnection()
{
	// The full priority chain (TX > scope > Checkout) is implemented
	// inside ibConnectionScope's default ctor. Returning the scope
	// directly lets callers write the natural RAII form
	// `ibConnectionScope scope = GetFreeConnection()` without an
	// intermediate shared_ptr round-trip. Mandatory copy elision in
	// C++17 means no move actually happens for prvalue returns.
	return ibConnectionScope();
}


ibConnectionPool::ibConnectionPool(ib::AppDataCtorToken) {}

ibConnectionPool::~ibConnectionPool()
{
	// Call Shutdown defensively in case the owner forgot to. No-op
	// after an explicit Shutdown.
	Shutdown();
}

void ibConnectionPool::Init(std::shared_ptr<ibDatabaseLayer> primary, std::size_t maxSize, std::size_t minIdle)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (auto& e : m_entries) {
		if (e.conn) e.conn->m_holder = nullptr;
	}
	m_entries.clear();
	m_source   = primary;
	m_maxSize  = maxSize;
	m_minIdle  = minIdle;
	const auto now = std::chrono::steady_clock::now();
	if (primary) {
		// Master is the first entry and the seed for Clone(). Marked
		// idle (no holder, not in-use) so the earliest Checkout hands
		// it out directly rather than paying a Clone() cost.
		ibConnectionEntry e;
		e.conn     = primary;
		e.lastUsed = now;
		m_entries.push_back(std::move(e));
	}
	// Pre-warm — Clone() up to minIdle additional conns so light bursts
	// don't pay the Open cost. Server modes (wes, future oes-server)
	// pass minIdle high enough to absorb typical concurrency; GUI hosts
	// use minIdle=2 (registry write conn + script thread), so the loop
	// below clones exactly one extra connection at Init.
	while (m_entries.size() < m_minIdle && m_entries.size() < m_maxSize) {
		ibDatabaseLayer* raw = primary ? primary->Clone() : nullptr;
		if (raw == nullptr) break;
		ibConnectionEntry e;
		e.conn     = std::shared_ptr<ibDatabaseLayer>(raw);
		e.lastUsed = now;
		m_entries.push_back(std::move(e));
	}
	m_shutdown = false;
	m_cv.notify_all();
}

void ibConnectionPool::Shutdown()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_shutdown = true;

	// Explicitly Close() every conn before dropping our shared_ptrs —
	// drivers don't always Close in their dtor (some only release
	// native handles on explicit Close). Outstanding hand-outs are a
	// separate concern: their lambda-captured ref keeps the layer
	// alive until the caller drops it; the deleter then sees
	// m_shutdown and just lets the captured sp die with the lambda.
	for (auto& e : m_entries) {
		if (e.conn) {
			e.conn->m_holder = nullptr;
			try { if (e.conn->IsOpen()) e.conn->Close(); }
			catch (...) { /* swallowed: shutdown-time Close failures (e.g. Firebird isc_io_error on already-disconnected DB) must not propagate from this destructor-style path — see ~ibApplicationData → Shutdown chain; an unhandled throw here lands in std::terminate during process exit. */ }
		}
	}
	m_entries.clear();

	if (m_source && m_source->IsOpen()) {
		try { m_source->Close(); }
		catch (...) { /* swallowed: same rationale as the pool loop above — source close failure on shutdown is unrecoverable and never worth aborting the process for. */ }
	}
	m_source.reset();

	m_cv.notify_all();
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::Checkout()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	const auto deadline = std::chrono::steady_clock::now() + kCheckoutTimeout;
	while (true) {
		if (m_shutdown || !m_source)
			return nullptr;

		// Reap stale idle entries before serving — keeps the pool from
		// holding onto idle conns that haven't been touched in a while
		// once load drops back below minIdle.
		ReapStaleLocked();

		// Linear scan for an available entry — small N (m_maxSize is
		// typically low double-digits). Walk from back to front so the
		// hottest (most recently parked) entry is served first.
		// `conn->IsBusy()` is the safety net for callers that obtained
		// the layer via bare db_query (no scope, no reservation) and
		// then created a stmt / result set: while those live, no
		// other thread can take the conn — driver-side cursors would
		// race otherwise.
		for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
			if (it->txHolder == nullptr && it->scopeHolder == nullptr
			    && !it->inUse && it->conn && !it->conn->IsBusy()) {
				it->inUse    = true;
				it->lastUsed = std::chrono::steady_clock::now();
				return WrapHandout(it->conn);
			}
		}

		if (m_entries.size() < m_maxSize) {
			ibDatabaseLayer* raw = m_source->Clone();
			if (raw == nullptr)
				return nullptr;
			// FIRST shared_ptr wrapping `raw` — initialises the weak_ptr
			// inside std::enable_shared_from_this so future
			// shared_from_this() calls route through this control block.
			auto sp = std::shared_ptr<ibDatabaseLayer>(raw);
			ibConnectionEntry e;
			e.conn     = sp;
			e.inUse    = true;
			e.lastUsed = std::chrono::steady_clock::now();
			m_entries.push_back(std::move(e));
			return WrapHandout(std::move(sp));
		}
		// Saturated — every connection is busy and the pool is at m_maxSize.
		// Wait BOUNDED for a Return / ReleaseTx / Unbind / Shutdown. If the
		// deadline passes with the pool still full, fail loudly instead of
		// blocking the worker thread forever (the old wait() had no timeout —
		// a leaked or stuck borrower would hang the caller indefinitely).
		if (std::chrono::steady_clock::now() >= deadline) {
			ibBackendCoreException::Error(
				_("Database connection pool exhausted: all %d connections are busy and none became free within %d s. Try again later or raise the pool size."),
				(int)m_maxSize, (int)kCheckoutTimeout.count());
		}
		m_cv.wait_until(lock, deadline);
	}
}

void ibConnectionPool::ReapStaleLocked()
{
	const auto now = std::chrono::steady_clock::now();
	std::size_t idleCount = 0;
	for (const auto& e : m_entries) {
		if (e.txHolder == nullptr && e.scopeHolder == nullptr
		    && !e.inUse && e.conn && !e.conn->IsBusy())
			++idleCount;
	}
	if (idleCount <= m_minIdle) return;

	for (auto it = m_entries.begin(); it != m_entries.end() && idleCount > m_minIdle; ) {
		const bool reapable = (it->txHolder == nullptr)
			&& (it->scopeHolder == nullptr) && !it->inUse
			&& it->conn && it->conn != m_source && !it->conn->IsBusy()
			&& (now - it->lastUsed >= kIdleTimeout);
		if (!reapable) { ++it; continue; }
		if (it->conn->IsOpen()) it->conn->Close();
		it = m_entries.erase(it);
		--idleCount;
	}
}

void ibConnectionPool::Return(std::shared_ptr<ibDatabaseLayer> conn)
{
	// Explicit Return is a convenience — dropping the last shared_ptr
	// reference has the same effect via the hand-out's deleter. Reset
	// here so the caller doesn't have to assign nullptr separately.
	conn.reset();
}

std::shared_ptr<ibDatabaseLayer> ibConnectionPool::WrapHandout(
	std::shared_ptr<ibDatabaseLayer> sp)
{
	// Hand-out shared_ptr has its OWN control block (constructed here
	// via the (raw, deleter) ctor). On drop the deleter clears the
	// entry's inUse flag so it's available for the next Checkout. The
	// closure captures `sp` by value — that strong ref keeps the layer
	// alive even if Shutdown ran while the hand-out was outstanding.
	// The raw pointer is NEVER deleted by the hand-out itself; the
	// layer's destruction is driven by the entry's shared_ptr in
	// m_entries (or, post-Shutdown, the closure-captured sp).
	ibDatabaseLayer* raw = sp.get();
	return std::shared_ptr<ibDatabaseLayer>(
		raw,
		[this, sp](ibDatabaseLayer*) {
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_shutdown) return;   // sp drops with the lambda
			for (auto& e : m_entries) {
				if (e.conn.get() == sp.get()) {
					e.inUse    = false;
					e.lastUsed = std::chrono::steady_clock::now();
					// Wake one Checkout waiter only if the entry is now
					// fully available (no TX, no scope binding).
					if (e.txHolder == nullptr && e.scopeHolder == nullptr)
						m_cv.notify_one();
					return;
				}
			}
		}
	);
}

bool ibConnectionPool::IsInitialised() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return !m_shutdown && m_source != nullptr;
}

std::size_t ibConnectionPool::LiveSize() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_entries.size();
}

std::size_t ibConnectionPool::IdleSize() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::size_t idle = 0;
	for (const auto& e : m_entries) {
		if (e.txHolder == nullptr && e.scopeHolder == nullptr
		    && !e.inUse && e.conn && !e.conn->IsBusy())
			++idle;
	}
	return idle;
}

std::size_t ibConnectionPool::MaxSize() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_maxSize;
}

std::size_t ibConnectionPool::MinIdle() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_minIdle;
}
