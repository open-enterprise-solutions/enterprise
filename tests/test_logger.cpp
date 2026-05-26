// =============================================================================
// OES Enterprise — ibLogger smoke tests
//
// Phase 1 coverage: single-thread Push → drain → row count, plus a small
// concurrent stress (8 threads × 1k entries) verifying no dropped rows.
// Sink is the production ibLoggerSinkSqlite — these tests therefore also
// exercise the .olg directory creation, DDL idempotency, and the WAL
// pragmas. Cross-instance / PG paths and the viewer form are deferred.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/logger/logger.h"
#include "backend/logger/loggerEntry.h"
#include "backend/logger/loggerReader.h"
#include "backend/logger/loggerSweep.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/file.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

// Per-test temp directory. Built under wxStandardPaths::GetTempDir so we
// don't pollute the repo and we get auto-cleanup on most platforms.
wxString MakeTempLogDir(const wxString& suffix) {
    wxString base = wxStandardPaths::Get().GetTempDir();
    if (base.IsEmpty()) base = wxT(".");
    const wxString dir = base + wxFileName::GetPathSeparator()
        + wxString::Format(wxT("oes_logtest_%lld_%s"),
                           wxDateTime::UNow().GetValue().GetValue(),
                           suffix);
    if (wxFileName::DirExists(dir))
        wxFileName::Rmdir(dir, wxPATH_RMDIR_RECURSIVE);
    return dir;
}

// Open every .olg in `dir` read-only and sum log_entry counts. Logger
// must already be destroyed (so the writer has flushed + the WAL has
// merged) before calling this.
int CountRows(const wxString& dir) {
    if (!wxFileName::DirExists(dir)) return 0;
    int total = 0;
    wxDir d(dir);
    if (!d.IsOpened()) return 0;
    wxString fname;
    bool more = d.GetFirst(&fname, wxT("*.olg"), wxDIR_FILES);
    while (more) {
        ibDatabaseLayerSQLite db;
        if (db.Open(dir + wxFileName::GetPathSeparator() + fname)) {
            ibDatabaseResultSet* rs = db.RunQueryWithResults(
                wxT("SELECT COUNT(*) FROM log_entry"));
            if (rs != nullptr && rs->Next()) {
                total += rs->GetResultInt(1);
            }
            if (rs) db.CloseResultSet(rs);
            db.Close();
        }
        more = d.GetNext(&fname);
    }
    return total;
}

void Cleanup(const wxString& dir) {
    if (wxFileName::DirExists(dir))
        wxFileName::Rmdir(dir, wxPATH_RMDIR_RECURSIVE);
}

}   // namespace

// ---------------------------------------------------------------------------
// Single producer → drain → row count
// ---------------------------------------------------------------------------

TEST(Logger, SinglePush_LandsAsRow) {
    const wxString dir = MakeTempLogDir(wxT("single"));
    {
        ibLogger lg(dir);
        lg.Info(wxT("test"), wxT("smoke"), wxT("hello"));
    }   // dtor → writer Stop + final drain
    EXPECT_EQ(CountRows(dir), 1);
    Cleanup(dir);
}

TEST(Logger, AuditLevelPersists) {
    const wxString dir = MakeTempLogDir(wxT("audit"));
    {
        ibLogger lg(dir);
        lg.Audit(wxT("auth"), wxT("login"), wxT("Иванов"));
        lg.Audit(wxT("document"), wxT("saved"), wxT("Doc1"));
    }
    EXPECT_EQ(CountRows(dir), 2);
    Cleanup(dir);
}

// ---------------------------------------------------------------------------
// Concurrent producers — no drops at moderate load
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Reader — round-trip Audit-with-ref through ibLoggerReader::Query
// ---------------------------------------------------------------------------

TEST(LoggerReader, AuditWithRef_RoundTrip) {
    const wxString dir = MakeTempLogDir(wxT("ref"));
    const wxString guidA = wxT("11111111-1111-1111-1111-111111111111");
    const wxString guidB = wxT("22222222-2222-2222-2222-222222222222");
    {
        ibLogger lg(dir);
        lg.Audit(wxT("record"), wxT("saved"),    wxT("Catalog: ItemA"), guidA, 100);
        lg.Audit(wxT("record"), wxT("saved"),    wxT("Catalog: ItemB"), guidB, 100);
        lg.Audit(wxT("record"), wxT("deleted"),  wxT("Catalog: ItemA"), guidA, 100);
        lg.Audit(wxT("auth"),   wxT("login"),    wxT("user=admin"));  // no ref
    }
    ibLoggerReader rd(dir);

    ibLogFilter all;
    EXPECT_EQ(rd.Query(all).size(), 4u);
    EXPECT_EQ(rd.Count(all), 4u);

    ibLogFilter byRef;
    byRef.ref_guid = guidA;
    auto rows = rd.Query(byRef);
    EXPECT_EQ(rows.size(), 2u);
    for (const auto& r : rows) {
        EXPECT_EQ(r.ref_guid, guidA);
        EXPECT_EQ(r.ref_meta_id, 100);
    }

    ibLogFilter bySource;
    bySource.source = wxT("auth");
    EXPECT_EQ(rd.Query(bySource).size(), 1u);

    Cleanup(dir);
}

// ---------------------------------------------------------------------------
// Sweep — old files removed, recent ones survive
// ---------------------------------------------------------------------------

TEST(LoggerSweep, RemovesOldFilesOnly) {
    const wxString dir = MakeTempLogDir(wxT("sweep"));
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    const wxString sep = wxFileName::GetPathSeparator();
    // Pretend-history files spanning 6 months back.
    const wxString fresh = dir + sep + wxString::Format(
        wxT("oes_%04d_%02d.olg"),
        wxDateTime::Now().GetYear(),
        static_cast<int>(wxDateTime::Now().GetMonth()) + 1);
    const wxString stale = dir + sep + wxT("oes_2020_01.olg");
    wxFile().Create(fresh);
    wxFile().Create(stale);

    const int removed = ibLoggerSweep::RunOnce(dir, /*retentionDays=*/30);
    EXPECT_EQ(removed, 1);
    EXPECT_TRUE(wxFileExists(fresh));
    EXPECT_FALSE(wxFileExists(stale));

    Cleanup(dir);
}

TEST(Logger, MultiThread_NoDrops_AtModerateLoad) {
    const wxString dir = MakeTempLogDir(wxT("multi"));
    constexpr int kThreads      = 8;
    constexpr int kPerThread    = 1000;
    constexpr int kTotal        = kThreads * kPerThread;
    {
        ibLogger lg(dir, /*maxQueue=*/200000);
        std::atomic<int> started{0};
        std::vector<std::thread> ts;
        ts.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&, t] {
                started.fetch_add(1);
                while (started.load() < kThreads)
                    std::this_thread::yield();
                for (int i = 0; i < kPerThread; ++i) {
                    lg.Info(wxT("stress"),
                            wxT("tick"),
                            wxString::Format(wxT("t=%d i=%d"), t, i));
                }
            });
        }
        for (auto& th : ts) th.join();
    }   // dtor flushes
    EXPECT_EQ(CountRows(dir), kTotal);
    Cleanup(dir);
}
