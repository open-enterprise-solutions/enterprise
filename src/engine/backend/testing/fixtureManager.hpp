/////////////////////////////////////////////////////////////////////////////
// fixtureManager — DB-state snapshot/restore for the functional test runner.
//
// Strategy: lean on ibDatabaseLayer's already-nested-safe transaction
// machinery (Begin / Commit / RollBack form a depth counter + aborted
// flag — see databaseLayer.h ~line 84). Each fixture push opens a fresh
// transaction; restore aborts and rolls it back. The "abort poisons outer
// commit" semantics already enforced by the layer guarantee that nested
// runtime BeginTransaction() calls inside the test under test commit
// against the fixture's transaction, not the driver, so a final fixture
// RollBack reverts every write the test performed.
//
// This deliberately does NOT introduce a new SAVEPOINT API on the driver
// interface — five drivers (Firebird, Postgres, SQLite, MySQL, ODBC)
// would all need updates, and the per-test isolation requirement is met
// by the existing nested-transaction layer.
//
// Two-DLL boundary: lives in backend, no wx GUI deps.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_TESTING_FIXTURE_MANAGER_HPP_
#define _IB_TESTING_FIXTURE_MANAGER_HPP_

#include "backend/backend.h"

#include <vector>

class ibDatabaseLayer;

namespace ibTesting {

// Outcome of a single fixture pop. Surfaced through the runner's report
// so a fixture failure is visible separately from an assertion failure
// (mirror of TestStatus on the test side).
enum class FixtureOutcome {
	Ok = 0,
	UnsupportedDriver,   // db driver doesn't support nested-safe TX
	NoActiveTransaction, // pop called without a matching push
	Error,               // underlying driver threw on Begin/RollBack
};

// RAII-friendly fixture manager. Holds a thin shared stack of in-flight
// transaction depths so the test runner can wrap each procedure in a
// ScopedFixture that auto-rolls back on dtor.
//
// Single-thread-of-test model — the runner invokes tests serially, no
// cross-thread sharing of an instance.
class BACKEND_API ibFixtureManager {
public:
	// `db` may be nullptr (e.g. tests with no configuration loaded);
	// every operation becomes a no-op returning UnsupportedDriver so the
	// caller can detect the degraded mode and still produce a report.
	explicit ibFixtureManager(ibDatabaseLayer* db);

	// Push a new fixture frame — begins a transaction, records the
	// pre-push depth so a paired Pop is detectable.
	FixtureOutcome Push(const char* tag);

	// Pop the most recent fixture frame, rolling back the wrapped
	// transaction. Always pops the stack entry even on driver error
	// (mismatched stack would otherwise compound across tests).
	FixtureOutcome Pop();

	// Diagnostic: how many fixture frames are currently outstanding.
	std::size_t Depth() const { return m_stack.size(); }

	// True when the underlying layer is nullptr — fixture isolation is
	// disabled and tests run with shared state. Runner surfaces this as
	// a warning in the report so the operator can decide.
	bool IsDegraded() const { return m_db == nullptr; }

	// RAII guard. Snapshots on ctor, restores on dtor unless Commit()
	// was called. Commit() lets a test intentionally keep its writes
	// (rare — used by test-data fixtures that PERSIST setup state).
	class BACKEND_API ScopedFixture {
	public:
		ScopedFixture(ibFixtureManager& mgr, const char* tag);
		~ScopedFixture();

		// Skip the auto-restore in the dtor. Use sparingly.
		void Commit() { m_committed = true; }

		// Last push outcome — caller can branch on UnsupportedDriver
		// to skip tests that require fixture isolation.
		FixtureOutcome PushOutcome() const { return m_pushOutcome; }

	private:
		ScopedFixture(const ScopedFixture&)            = delete;
		ScopedFixture& operator=(const ScopedFixture&) = delete;

		ibFixtureManager& m_mgr;
		FixtureOutcome    m_pushOutcome;
		bool              m_committed = false;
	};

private:
	ibDatabaseLayer*       m_db;
	std::vector<int>       m_stack; // pre-push tx depths — debug-only
};

} // namespace ibTesting

#endif // _IB_TESTING_FIXTURE_MANAGER_HPP_
