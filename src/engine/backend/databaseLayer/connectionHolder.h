#ifndef __IB_DATABASE_CONNECTION_HOLDER_H__
#define __IB_DATABASE_CONNECTION_HOLDER_H__

// ibDatabaseConnectionHolder — identity tag for "something that can
// reserve a database connection across thread boundaries".
//
// The runtime holder today is ibSession; the pool keys its
// active-transaction reservation map on this base pointer so the
// pool / databaseLayer don't pull in session.h, and so future non-
// session holders (compute-server batch runner, daemon job scope)
// can plug in without growing ibSession.
//
// Reservation storage lives in ibConnectionPool — see
// Reserve/Release/GetReserved on the pool. The layer caches a
// weak_ptr to the holder that owns its current TX so a layer
// returned via shared_from_this() can be cross-checked against
// the pool's map.

#include "backend/backend.h"

#include <memory>
#include <set>
#include <vector>
#include <functional>

class ibDatabaseLayer;

class BACKEND_API ibDatabaseConnectionHolder {
public:
	virtual ~ibDatabaseConnectionHolder() = default;

	// Single entry point: returns a usable conn for this holder.
	// Resolution chain:
	//   1. TX-pinned conn (BeginTransaction not yet committed/rollbacked)
	//   2. Scope-bound conn (live ibConnectionScope on this holder)
	//   3. Fresh Checkout from pool, bound as scope to this holder so
	//      subsequent EnsureConnection calls return the same conn while
	//      the holder is alive. Released when the holder drops via
	//      ReleaseAll (holder dtor).
	//
	//   if (auto conn = ibSession::Current()->EnsureConnection()) {
	//       conn->RunQuery(...);
	//   }
	//
	// Defined out-of-line in connectionPool.cpp where the pool's
	// header is in scope. Returns nullptr only if the pool is not
	// initialised.
	std::shared_ptr<ibDatabaseLayer> EnsureConnection();

	// Fresh conn from the pool — wrapped Checkout, NOT bound to this
	// holder. The returned shared_ptr's deleter releases the entry
	// back to the pool when the caller drops the last reference. Use
	// for work that must run on a separate conn from this holder's
	// current TX/scope (e.g. session running parallel side queries
	// while its main TX stays open):
	//
	//   auto fresh = ibSession::Current()->AcquireFreeConnection();
	//   fresh->RunQuery(...);                  // independent of session's TX
	//   // fresh drops at scope end → pool repark
	//
	// Returns nullptr if the pool is saturated / not initialised.
	std::shared_ptr<ibDatabaseLayer> AcquireFreeConnection() const;

	// Convenience: return an ibConnectionScope bound to this holder.
	// Equivalent to `ibConnectionScope(this)`. Lets call sites stay
	// short — `auto scope = ibSession::Current()->OpenConnectionScope();`
	// (via session façade).
	class ibConnectionScope OpenConnectionScope();

	// --- DDL/DML barrier state (the current restructuring save) -----------------------------------
	// Tables CREATEd this save on a barrier dialect (Firebird), and the data writes DEFERRED past the
	// DDL commit (FB can't populate a freshly-created table in the same transaction). The state lives
	// here, not in process-wide statics, because the barrier is tied to THIS holder's connection /
	// transaction — so the SEVERAL ibSchemaBuilder instances of one save (Reset / per-table Execute /
	// Flush) share one home through the holder they run on. ibSchemaBuilder owns the logic; the holder
	// only stores. (query/schemaBuilder.h)
	std::set<wxString>&                 DdlCreatedTables()  { return m_ddlCreated; }
	std::vector<std::function<bool()>>& DdlDeferredWrites() { return m_ddlDeferred; }

private:
	std::set<wxString>                 m_ddlCreated;
	std::vector<std::function<bool()>> m_ddlDeferred;
};

// ibSingleConnectionHolder — generic empty holder. The OES runtime
// uses one per-thread instance as the db_query channel
// (ibConnectionPool::ThreadHolder); subsystems that need an
// alternate non-session channel (parallel isolation, dedicated
// quota) instantiate their own static instance and pass its address
// to ibConnectionScope(&customHolder).
//
// Self-cleaning: the dtor releases any pool registrations (TX pin
// and scope binding) keyed on this holder. Process-shutdown teardown
// of the singleton runs after the pool is gone, so the dtor's
// release is a no-op then.
class BACKEND_API ibSingleConnectionHolder : public ibDatabaseConnectionHolder {
public:
	ibSingleConnectionHolder();
	~ibSingleConnectionHolder() override;
};

#endif
