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

#include <wx/filefn.h>     // wxRemoveFile
#include <wx/filename.h>   // wxFileName::GetTempDir
#include <wx/init.h>       // wxInitializer
#include <wx/utils.h>      // wxGetProcessId

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

// The stale sweep deletes on the registry thread, and Add waits behind that thread.
// A row another transaction still holds must come back at once (noWait), not after
// the driver's lock wait, or the caller hears "registry did not answer to Add".
TEST(SessionSweep, ALockedRowIsLeftForTheNextPass)
{
	wxInitializer wxInit;
	ASSERT_TRUE(wxInit.IsOk());

	const wxString path = wxFileName::GetTempDir()
		+ wxFileName::GetPathSeparator()
		+ wxString::Format(wxT("oes-session-sweep-%ld.sqlite"), wxGetProcessId());
	struct Gone {
		wxString path;
		std::shared_ptr<ibDatabaseLayerSQLite> holder;
		std::shared_ptr<ibDatabaseLayerSQLite> sweeper;
		static void Drop(const wxString& file) {
			if (wxFileExists(file))
				wxRemoveFile(file);
		}
		~Gone() {
			if (holder) holder->Close();
			if (sweeper) sweeper->Close();
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
	gone.sweeper = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(gone.holder->Open(path));
	ASSERT_TRUE(gone.sweeper->Open(path));

	gone.holder->RunStatement(wxT("CREATE TABLE sys_session (session TEXT PRIMARY KEY, n INTEGER)"));
	gone.holder->RunStatement(wxT("INSERT INTO sys_session (session, n) VALUES ('zombie', 1)"));

	gone.holder->BeginTransaction();
	gone.holder->RunStatement(wxT("UPDATE sys_session SET n = 2 WHERE session = 'zombie'"));

	const auto started = std::chrono::steady_clock::now();
	bool refused = false;
	try {
		ibDbTxOptions opts;
		opts.noWait = true;
		gone.sweeper->BeginTransaction(opts);
		gone.sweeper->RunStatement(wxT("DELETE FROM sys_session WHERE session = 'zombie'"));
		gone.sweeper->Commit();
	}
	catch (const ibBackendException&) {
		refused = true;
		if (gone.sweeper->IsActiveTransaction())
			gone.sweeper->RollBack();
	}
	const auto waited = std::chrono::steady_clock::now() - started;
	EXPECT_TRUE(refused) << "a locked row is skipped, not deleted out from under its owner";
	EXPECT_LT(waited, std::chrono::seconds(1)) << "the skip does not spend the driver's lock wait";

	gone.holder->RollBack();

	ibDbTxOptions again;
	again.noWait = true;
	gone.sweeper->BeginTransaction(again);
	const int removed = gone.sweeper->RunStatement(wxT("DELETE FROM sys_session WHERE session = 'zombie'"));
	gone.sweeper->Commit();
	EXPECT_EQ(removed, 1) << "once the lock is gone the same delete takes the row";
}
