// =============================================================================
// OES Enterprise — transaction-semantics tests (cross-driver contract)
//
// CRITICAL PATH. BeginTransaction / Commit / RollBack are NON-virtual wrappers
// on ibDatabaseLayer that manage a nesting depth counter + an "aborted" flag;
// drivers only implement DoBeginTransaction / DoCommit / DoRollBack. The
// documented contract (databaseLayer.h):
//   * IsActiveTransaction() == (m_txDepth > 0)
//   * the FIRST Begin drives DoBeginTransaction; nested Begins only ++depth
//   * Commit is "kept" or an exception and never rolls back on its own: after
//     any inner RollBack every Commit, at any depth, throws Kind::RolledBack and
//     leaves the transaction open ("an inner rollback poisons the whole
//     transaction"); the owner rolls it back in its catch
//   * RollBack with nothing open throws Kind::NoTransaction
//
// If this counter/flag regresses, an inner rollback would NOT poison the outer
// commit and a half-rolled-back transaction would persist — silent data
// corruption, and it would also break the holder-pinned read-your-writes the
// whole session model relies on. Run against the embedded SQLite driver so the
// test needs no appData / pool. Skips (not fails) if SQLite cannot come up.
// =============================================================================

#include <gtest/gtest.h>

#include <memory>
#include <wx/init.h>

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/databaseLayerException.h"

namespace {

struct DbTxFix : ::testing::Test {
    wxInitializer                          m_wxInit;   // wxBase before the driver
    std::shared_ptr<ibDatabaseLayerSQLite> db;
    bool ready = false;

    void SetUp() override {
        if (!m_wxInit.IsOk())
            GTEST_SKIP() << "wxBase init failed (no wxApp host)";
        db = std::make_shared<ibDatabaseLayerSQLite>();
        if (!db->Open(wxT(":memory:")))
            GTEST_SKIP() << "in-memory SQLite open failed";
        ready = true;
    }

    void Exec(const wxString& sql) { db->RunQuery(wxT("%s"), sql); }

    int CountRows() {
        ibDatabaseResultSet* rs =
            db->RunQueryWithResults(wxT("%s"), wxString(wxT("SELECT count(*) AS c FROM t")));
        const int n = (rs != nullptr && rs->Next()) ? rs->GetResultInt(wxT("c")) : -1;
        if (rs != nullptr) db->CloseResultSet(rs);
        return n;
    }

    // The Kind a Commit refuses with; Unknown if it did not throw.
    ibBackendDatabaseException::Kind KindOfCommit() {
        try { db->Commit(); }
        catch (const ibBackendDatabaseException& e) { return e.GetKind(); }
        return ibBackendDatabaseException::Kind::Unknown;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Depth counter drives IsActiveTransaction
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, IsActiveTracksDepth) {
    if (!ready) GTEST_SKIP();
    EXPECT_FALSE(db->IsActiveTransaction());
    db->BeginTransaction();
    EXPECT_TRUE(db->IsActiveTransaction());
    db->Commit();
    EXPECT_FALSE(db->IsActiveTransaction());
}

// ---------------------------------------------------------------------------
// Read-your-writes: an uncommitted INSERT is visible to the same connection.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, ReadYourWritesWithinTransaction) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));     // auto-committed (no TX active)

    db->BeginTransaction();
    Exec(wxT("INSERT INTO t (n) VALUES (1)"));
    EXPECT_EQ(CountRows(), 1) << "the writer's own connection must see its uncommitted row";
    db->Commit();
    EXPECT_EQ(CountRows(), 1) << "committed row persists";
}

// ---------------------------------------------------------------------------
// RollBack discards the transaction's changes.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, RollBackDiscardsChanges) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));

    db->BeginTransaction();
    Exec(wxT("INSERT INTO t (n) VALUES (1)"));
    EXPECT_EQ(CountRows(), 1);
    db->RollBack();
    EXPECT_FALSE(db->IsActiveTransaction());
    EXPECT_EQ(CountRows(), 0) << "rolled-back INSERT must not persist";
}

// ---------------------------------------------------------------------------
// Nested Begin/Commit: only the OUTERMOST commit really commits; the row
// persists and the depth returns to zero.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, NestedCommitOnlyOuterFinalises) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));

    db->BeginTransaction();                      // depth 1
    db->BeginTransaction();                      // depth 2
    Exec(wxT("INSERT INTO t (n) VALUES (1)"));
    EXPECT_TRUE(db->IsActiveTransaction());
    db->Commit();                                // inner: depth 2 -> 1, still active
    EXPECT_TRUE(db->IsActiveTransaction());
    db->Commit();                                // outer: depth 1 -> 0, real DoCommit
    EXPECT_FALSE(db->IsActiveTransaction());
    EXPECT_EQ(CountRows(), 1) << "outer commit persists the nested write";
}

// ---------------------------------------------------------------------------
// THE load-bearing one: an inner RollBack poisons the whole transaction, so
// the outer Commit must roll the data back rather than commit a partial TX.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, InnerRollBackPoisonsOuterCommit) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));

    db->BeginTransaction();                      // depth 1
    Exec(wxT("INSERT INTO t (n) VALUES (1)"));
    db->BeginTransaction();                      // depth 2
    db->RollBack();                              // aborted flag set, depth 2 -> 1
    EXPECT_TRUE(db->IsActiveTransaction());       // still inside the outer level
    // the outer commit refuses, says why, and touches nothing
    EXPECT_EQ(KindOfCommit(), ibBackendDatabaseException::Kind::RolledBack)
        << "a commit over an inner rollback must throw Kind::RolledBack";
    EXPECT_TRUE(db->IsActiveTransaction())
        << "a refused commit leaves the transaction open for its owner to roll back";
    db->RollBack();                              // the owner's own act: depth 1 -> 0, real DoRollBack
    EXPECT_FALSE(db->IsActiveTransaction());
    EXPECT_EQ(CountRows(), 0)
        << "an inner rollback must leave nothing of the transaction to keep";
}

// ---------------------------------------------------------------------------
// The poison refuses EVERY commit, an inner one too: the next write after a
// caught failure fails at once instead of at the very end.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, InnerCommitAfterRollBackRefuses) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));

    db->BeginTransaction();                      // depth 1 — the owner
    db->BeginTransaction();                      // depth 2 — a write that fails
    db->RollBack();                              // depth 1, poisoned
    db->BeginTransaction();                      // depth 2 — the next write
    EXPECT_EQ(KindOfCommit(), ibBackendDatabaseException::Kind::RolledBack);
    db->RollBack();                              // that write's scope: 2 -> 1
    db->RollBack();                              // the owner: 1 -> 0
    EXPECT_FALSE(db->IsActiveTransaction());
}

// ---------------------------------------------------------------------------
// A rollback of nothing is a mistake, and says so.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, RollBackWithNothingOpenThrows) {
    if (!ready) GTEST_SKIP();
    bool told = false;
    try { db->RollBack(); }
    catch (const ibBackendDatabaseException& e) {
        told = (e.GetKind() == ibBackendDatabaseException::Kind::NoTransaction);
    }
    EXPECT_TRUE(told) << "RollBack with no transaction open must throw Kind::NoTransaction";
}

// ---------------------------------------------------------------------------
// The poison does not outlive its transaction: the next one commits normally.
// ---------------------------------------------------------------------------

TEST_F(DbTxFix, PoisonEndsWithItsTransaction) {
    if (!ready) GTEST_SKIP();
    Exec(wxT("CREATE TABLE t (n INTEGER)"));

    db->BeginTransaction();
    db->BeginTransaction();
    db->RollBack();
    EXPECT_EQ(KindOfCommit(), ibBackendDatabaseException::Kind::RolledBack);
    db->RollBack();

    db->BeginTransaction();
    Exec(wxT("INSERT INTO t (n) VALUES (1)"));
    EXPECT_NO_THROW(db->Commit());
    EXPECT_EQ(CountRows(), 1) << "a clean transaction after a poisoned one must commit";
}
