// =============================================================================
// MockDatabaseLayer — minimal ibDatabaseLayer stub for pool/holder unit tests.
//
// Only implements the methods ibConnectionPool actually calls during
// Init / Checkout / IsBusy / Clone lifecycle. Query / SQL methods return
// nullptr or zero — these tests don't exercise the SQL layer.
// =============================================================================

#pragma once

#include "backend/databaseLayer/databaseLayer.h"

class MockDatabaseLayer : public ibDatabaseLayer {
public:
    MockDatabaseLayer() = default;
    ~MockDatabaseLayer() override = default;

    bool Open(const wxString&) override { m_open = true; return true; }
    bool Close() override                { m_open = false; return true; }
    bool IsOpen() override               { return m_open; }
    ibDatabaseLayer* Clone() override    { auto* c = new MockDatabaseLayer(); c->m_open = true; return c; }

    bool TableExists(const wxString&) override { return false; }
    bool ViewExists (const wxString&) override { return false; }
    wxArrayString GetTables() override                      { return {}; }
    wxArrayString GetViews() override                       { return {}; }
    wxArrayString GetColumns(const wxString&) override      { return {}; }
    int  GetDatabaseLayerType() const override              { return 0; }
    const ibDialectDictionary& GetDialect() const override {
        static const ibDialectDictionary s_dialect;   // default ctor = ANSI baseline
        return s_dialect;
    }

    int                  DoRunQuery(const wxString&, bool) override            { return 0; }
    ibDatabaseResultSet* DoRunQueryWithResults(const wxString&) override       { return nullptr; }
    ibPreparedStatement* DoPrepareStatement(const wxString&) override          { return nullptr; }
    void                 DoBeginTransaction(const ibTxOptions&) override       {}
    void                 DoCommit() override                                   {}
    void                 DoRollBack() override                                 {}

private:
    bool m_open = true;
};
