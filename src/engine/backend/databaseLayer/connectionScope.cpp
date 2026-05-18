#include "connectionScope.h"

#include "backend/appData.h"
#include "backend/session/session.h"
#include "connectionHolder.h"
#include "connectionPool.h"

ibConnectionScope::ibConnectionScope()
	: m_ownsConn(false)
{
	Acquire(nullptr);
}

ibConnectionScope::ibConnectionScope(ibDatabaseConnectionHolder* customHolder)
	: m_ownsConn(false)
{
	Acquire(customHolder);
}

void ibConnectionScope::Acquire(ibDatabaseConnectionHolder* customHolder)
{
	auto* pool = ibApplicationData::GetConnectionPool();
	if (pool == nullptr) return;   // passive — db_query falls back to primary

	// Resolve the holder for this scope:
	//   - explicit `customHolder` wins (private-holder pattern, e.g. a
	//     thread_local singleton owned by the calling subsystem, or
	//     ibSession::Current()->Holder() for session-bound work);
	//   - else fall through to CurrentHolder() = ThreadHolder
	//     (db_query channel). Sessions are NOT routed implicitly any
	//     more — session work passes its holder explicitly via
	//     `ibSession::Current()->OpenConnectionScope()` (see session.h).
	m_holder = (customHolder != nullptr)
		? customHolder
		: ibConnectionPool::CurrentHolder();
	auto* holder = m_holder;

	if (holder != nullptr) {
		// 1. Nested scope — another scope is already bound for this
		//    holder. Inherit; don't take ownership; dtor leaves the pool
		//    untouched.
		if (auto existing = pool->GetScopeConn(holder)) {
			m_conn = existing;
			return;
		}
		// 2. Active TX is pinned for this holder — reuse the same conn
		//    so SQL inside the scope lands on the TX's connection. We
		//    DO mark scope binding so nested scopes find us via the
		//    same lookup path; ownership ensures the dtor unbinds.
		if (auto tx = pool->GetReservedTx(holder)) {
			m_conn = tx;
			pool->BindScopeHolder(holder, m_conn);
			m_ownsConn = true;
			return;
		}
	}

	// 3. Fresh checkout — pool hands a borrowed entry. Bind it to the
	//    holder so nested scopes / ses_query for the same holder route
	//    to this conn. No-op when holder == nullptr (non-holder
	//    threads using Checkout directly).
	m_conn = pool->Checkout();
	if (m_conn) {
		m_ownsConn = true;
		if (holder != nullptr)
			pool->BindScopeHolder(holder, m_conn);
	}
	// else: pool saturated / not initialised — passive scope. db_query
	// resolves via the pool's primary-conn fallback.
}

ibConnectionScope::~ibConnectionScope()
{
	// Safety-net: any unresolved Begin on this scope gets rolled back
	// so an exception between Begin and Commit can't leave a dangling
	// TX on the conn. Swallow driver exceptions here — propagating
	// from a dtor would std::terminate.
	if (m_activeTx && m_conn) {
		try { m_conn->RollBack(); } catch (...) {}
		m_activeTx = false;
	}

	if (m_ownsConn && m_holder) {
		if (auto* pool = ibApplicationData::GetConnectionPool())
			pool->UnbindScopeHolder(m_holder);
	}
	// Inherit / passive path: parent still holds the binding; nothing
	// to undo. Dropping our m_conn just decrements its refcount; the
	// parent's lease keeps the connection alive in the pool.
}

ibConnectionScope::ibConnectionScope(ibConnectionScope&& other) noexcept
	: m_conn(std::move(other.m_conn))
	, m_holder(other.m_holder)
	, m_ownsConn(other.m_ownsConn)
	, m_activeTx(other.m_activeTx)
{
	other.m_holder   = nullptr;
	other.m_ownsConn = false;
	other.m_activeTx = false;
}

ibConnectionScope& ibConnectionScope::operator=(ibConnectionScope&& other) noexcept
{
	if (this != &other) {
		// Roll back our own state first (any unresolved TX, and unbind
		// the holder if we owned it) before adopting the source's
		// state.
		if (m_activeTx && m_conn) {
			try { m_conn->RollBack(); } catch (...) {}
		}
		if (m_ownsConn && m_holder) {
			if (auto* pool = ibApplicationData::GetConnectionPool())
				pool->UnbindScopeHolder(m_holder);
		}

		m_conn     = std::move(other.m_conn);
		m_holder   = other.m_holder;
		m_ownsConn = other.m_ownsConn;
		m_activeTx = other.m_activeTx;
		other.m_holder   = nullptr;
		other.m_ownsConn = false;
		other.m_activeTx = false;
	}
	return *this;
}

void ibConnectionScope::SafeBeginTransaction(const ibTxOptions& opts)
{
	if (!m_activeTx && m_conn) {
		m_conn->BeginTransaction(opts);
		m_activeTx = true;
	}
}

void ibConnectionScope::SafeCommitTransaction()
{
	if (m_activeTx && m_conn) {
		m_conn->Commit();
		m_activeTx = false;
	}
}

void ibConnectionScope::SafeRollBackTransaction()
{
	if (m_activeTx && m_conn) {
		m_conn->RollBack();
		m_activeTx = false;
	}
}
