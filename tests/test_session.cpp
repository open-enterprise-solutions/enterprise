// =============================================================================
// OES Enterprise — ibSession composition + holder façade tests
//
// Focus: ibSession owns ibDatabaseConnectionHolder by composition (not
// inheritance) and exposes Holder() / EnsureConnection / OpenConnectionScope
// / DatabaseLayer as façade. Pool/connection-driven behaviour (auto-bind,
// scope lifecycle) is integration-test scope — needs full appData wiring +
// a live pool — and is not covered here.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/session/session.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

#include <chrono>
#include <type_traits>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/utils.h>

// ---------------------------------------------------------------------------
// Composition: ibSession owns a holder, does NOT inherit one
// ---------------------------------------------------------------------------

TEST(SessionHolder, IsNotADatabaseConnectionHolder) {
    // Composition over inheritance — ibSession should NOT be derived from
    // ibDatabaseConnectionHolder. The holder is a member, accessed via
    // Holder().
    EXPECT_FALSE((std::is_base_of<ibDatabaseConnectionHolder, ibSession>::value));
}

TEST(SessionHolder, HolderAccessorReturnsNonNull) {
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    EXPECT_NE(sess.Holder(), nullptr);
}

TEST(SessionHolder, HolderAccessorIsStable) {
    // Holder() must return the same pointer across calls — it's the
    // pool's identity key for reservations. A different pointer per call
    // would scatter TX/scope bindings across phantom holders.
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    EXPECT_EQ(sess.Holder(), sess.Holder());
}

TEST(SessionHolder, ConstHolderAccessorMatchesNonConst) {
    ibSession sess(wxT("test-id"), ibSessionKind::Designer);
    const ibSession& csess = sess;
    EXPECT_EQ(sess.Holder(), csess.Holder());
}

TEST(SessionHolder, DistinctSessionsHaveDistinctHolders) {
    // Each session must have its own holder — pool keys reservations by
    // address, two sessions sharing one holder would conflict on TX pin.
    ibSession a(wxT("session-a"), ibSessionKind::Designer);
    ibSession b(wxT("session-b"), ibSessionKind::Designer);
    EXPECT_NE(a.Holder(), b.Holder());
}

// ---------------------------------------------------------------------------
// DatabaseLayer static — backs the ses_query macro
// ---------------------------------------------------------------------------

TEST(SessionDbLayer, ThrowsWhenNoCurrentSession) {
    // No SessionScope active on this thread → ibSession::Current() is
    // null → DatabaseLayer() throws an explicit error rather than
    // silently returning the wrong conn.
    EXPECT_THROW(
        ibSession::DatabaseLayer(),
        ibBackendException);
}

// The registry thread heartbeats and sweeps sys_session, and Add waits behind that thread.
// A row another transaction still holds — an apply's open transaction, a peer's write —
// must come back at once (noWait), not after the driver's lock wait, or the caller hears
// "registry did not answer to Add". The heartbeat is an UPDATE; the sweep is a DELETE.
TEST(SessionRegistry, ALockedHeartbeatAndSweepComeBackAtOnce)
{
	wxInitializer wxInit;
	ASSERT_TRUE(wxInit.IsOk());

	const wxString path = wxFileName::GetTempDir()
		+ wxFileName::GetPathSeparator()
		+ wxString::Format(wxT("oes-session-nowait-%ld.sqlite"), wxGetProcessId());
	struct Gone {
		wxString path;
		std::shared_ptr<ibDatabaseLayerSQLite> holder;
		std::shared_ptr<ibDatabaseLayerSQLite> other;
		static void Drop(const wxString& file) {
			if (wxFileExists(file))
				wxRemoveFile(file);
		}
		~Gone() {
			if (holder) holder->Close();
			if (other) other->Close();
			Drop(path);
			Drop(path + wxT("-wal"));
			Drop(path + wxT("-shm"));
		}
	} gone;
	gone.path = path;
	Gone::Drop(path);
	Gone::Drop(path + wxT("-wal"));
	Gone::Drop(path + wxT("-shm"));

	gone.holder = std::make_shared<ibDatabaseLayerSQLite>();
	gone.other = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(gone.holder->Open(path));
	ASSERT_TRUE(gone.other->Open(path));

	gone.holder->RunStatement(wxT("CREATE TABLE sys_session (session TEXT PRIMARY KEY, n INTEGER)"));
	gone.holder->RunStatement(wxT("INSERT INTO sys_session (session, n) VALUES ('live', 1)"));

	gone.holder->BeginTransaction();
	gone.holder->RunStatement(wxT("UPDATE sys_session SET n = 2 WHERE session = 'live'"));

	const auto refuse = [&](const wxString& sql) {
		const auto started = std::chrono::steady_clock::now();
		bool refused = false;
		try {
			ibDbTxOptions opts;
			opts.noWait = true;
			gone.other->BeginTransaction(opts);
			gone.other->RunStatement(sql);
			gone.other->Commit();
		}
		catch (const ibBackendException&) {
			refused = true;
			if (gone.other->IsActiveTransaction())
				gone.other->RollBack();
		}
		const auto waited = std::chrono::steady_clock::now() - started;
		EXPECT_TRUE(refused) << "a locked row is left, not written out from under its owner: "
		                     << sql.utf8_str();
		EXPECT_LT(waited, std::chrono::seconds(1))
			<< "the refusal does not spend the driver's lock wait: " << sql.utf8_str();
	};
	refuse(wxT("UPDATE sys_session SET n = 3 WHERE session = 'live'"));
	refuse(wxT("DELETE FROM sys_session WHERE session = 'live'"));

	gone.holder->RollBack();

	ibDbTxOptions again;
	again.noWait = true;
	gone.other->BeginTransaction(again);
	const int updated = gone.other->RunStatement(wxT("UPDATE sys_session SET n = 3 WHERE session = 'live'"));
	gone.other->Commit();
	EXPECT_EQ(updated, 1) << "once the lock is gone the heartbeat update takes the row";

	gone.other->BeginTransaction(again);
	const int removed = gone.other->RunStatement(wxT("DELETE FROM sys_session WHERE session = 'live'"));
	gone.other->Commit();
	EXPECT_EQ(removed, 1) << "once the lock is gone the sweep delete takes the row";
}
