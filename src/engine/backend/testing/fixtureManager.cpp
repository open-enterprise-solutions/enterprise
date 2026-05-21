/////////////////////////////////////////////////////////////////////////////
// fixtureManager — see header for the strategy rationale.
/////////////////////////////////////////////////////////////////////////////

#include "fixtureManager.hpp"

#include "backend/databaseLayer/databaseLayer.h"

namespace ibTesting {

ibFixtureManager::ibFixtureManager(ibDatabaseLayer* db)
	: m_db(db)
{
}

FixtureOutcome ibFixtureManager::Push(const char* /*tag*/)
{
	if (m_db == nullptr) {
		// No database — record the push so a matching Pop still
		// balances; outcome flagged as degraded so the runner can
		// surface it.
		m_stack.push_back(0);
		return FixtureOutcome::UnsupportedDriver;
	}

	const int preDepth = m_db->IsActiveTransaction() ? 1 : 0;
	try {
		m_db->BeginTransaction();
	}
	catch (...) {
		// Driver refused to open the transaction (busy, RO file, etc.).
		// Don't push — Pop wouldn't have anything to roll back.
		return FixtureOutcome::Error;
	}
	m_stack.push_back(preDepth);
	return FixtureOutcome::Ok;
}

FixtureOutcome ibFixtureManager::Pop()
{
	if (m_stack.empty()) {
		return FixtureOutcome::NoActiveTransaction;
	}
	m_stack.pop_back();

	if (m_db == nullptr) {
		return FixtureOutcome::UnsupportedDriver;
	}

	if (!m_db->IsActiveTransaction()) {
		// Inner code already committed/rolled the transaction. The
		// runner records this as a fixture-degraded outcome so the
		// operator can investigate; cleanup still proceeded.
		return FixtureOutcome::NoActiveTransaction;
	}

	try {
		m_db->RollBack();
	}
	catch (...) {
		return FixtureOutcome::Error;
	}
	return FixtureOutcome::Ok;
}

////////////////////////////////////////////////////////////////////////////
// ScopedFixture
////////////////////////////////////////////////////////////////////////////

ibFixtureManager::ScopedFixture::ScopedFixture(ibFixtureManager& mgr, const char* tag)
	: m_mgr(mgr), m_pushOutcome(mgr.Push(tag))
{
}

ibFixtureManager::ScopedFixture::~ScopedFixture()
{
	if (m_committed) return;
	if (m_pushOutcome == FixtureOutcome::Error) {
		// Push refused — Pop would underflow.
		return;
	}
	// Discard outcome — the runner is the rightful owner of fixture
	// diagnostics, and a destructor can't throw safely.
	(void)m_mgr.Pop();
}

} // namespace ibTesting
