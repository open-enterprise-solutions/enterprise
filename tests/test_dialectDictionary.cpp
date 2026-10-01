// =============================================================================
// OES Enterprise — ibDialectDictionary tests
//
// The dialect dictionary (backend/databaseLayer/databaseLayer.h) is the data
// that closes every per-driver SQL fork: column-type strings, boolean spelling,
// pagination style. L2 renders against it and never branches on the driver. A
// silent drift here is exactly the class of bug that flipped Firebird's guid
// column from VARCHAR(36) to CHAR(36) (and broke guid round-trip via CHAR
// space-padding). These pins guard the ANSI baseline + the SQLite override.
// =============================================================================

#include <gtest/gtest.h>
#include "backend/databaseLayer/databaseLayer.h"               // ibDialectDictionary
#include "backend/databaseLayer/databaseQueryBuilder.h"        // the capability accessors + the DDL builders
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h" // ibDatabaseLayerSQLite::GetDialect
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"  // the other side of the DDL-barrier answer

// ---------------------------------------------------------------------------
// ANSI baseline (default-constructed dictionary)
// ---------------------------------------------------------------------------

TEST(DialectDictionary, BaselineGuidIsVarchar36) {
    ibDialectDictionary d;
    // VARCHAR — NOT CHAR — so a guid reads back without a charset-padded tail.
    EXPECT_EQ(d.m_typeGuid, wxT("VARCHAR(36)"));
}

TEST(DialectDictionary, BaselineScalarTypes) {
    ibDialectDictionary d;
    EXPECT_EQ(d.m_typeBoolean,       wxT("BOOLEAN"));
    EXPECT_EQ(d.m_typeDate,          wxT("TIMESTAMP"));
    EXPECT_EQ(d.m_typeDateOnly,      wxT("DATE"));
    EXPECT_EQ(d.m_typeStringPattern, wxT("VARCHAR(%d)"));
    EXPECT_EQ(d.m_typeNumberPattern, wxT("DECIMAL(%d,%d)"));
}

// ---------------------------------------------------------------------------
// SQLite override (driver self-describes via GetDialect()). SQLite stores a
// guid as TEXT — matches the DDL renderer test (Ref TEXT PRIMARY KEY).
// ---------------------------------------------------------------------------

TEST(DialectDictionary, SqliteOverridesGuidToText) {
    ibDatabaseLayerSQLite db;
    EXPECT_EQ(db.GetDialect().m_typeGuid, wxT("TEXT"));
}

// ---------------------------------------------------------------------------
// More baseline type-map entries + declarative flags / enums
// ---------------------------------------------------------------------------

TEST(DialectDictionary, BaselineMoreTypes) {
    ibDialectDictionary d;
    EXPECT_EQ(d.m_typeInteger,       wxT("INTEGER"));
    EXPECT_EQ(d.m_typeBigInt,        wxT("BIGINT"));     // reference _RTRef = clsid
    EXPECT_EQ(d.m_typeBlob,          wxT("BLOB"));
    EXPECT_EQ(d.m_typeCharPattern,   wxT("CHAR(%d)"));
    EXPECT_EQ(d.m_typeBinaryPattern, wxT("BINARY(%d)")); // reference _RRRef = guid+metaID
}

TEST(DialectDictionary, BaselineFlags) {
    ibDialectDictionary d;
    EXPECT_FALSE(d.m_dropIndexNeedsTable);
    EXPECT_FALSE(d.m_ddlCommitBeforeData);   // Firebird overrides this to true (DDL barrier)
}

TEST(DialectDictionary, BaselineEnums) {
    ibDialectDictionary d;
    EXPECT_TRUE(d.m_paramStyle == ibParamStyle::QuestionMark);
    EXPECT_TRUE(d.m_pagination == ibPagination::LimitOffset);
    EXPECT_TRUE(d.m_boolForm   == ibBoolForm::TrueFalse);
}

// =============================================================================
// Capability accessors — the QUESTION is asked of L2, never read off the dictionary
// =============================================================================
//
// Every one of these used to be spelled at a callsite in query/** as
// `layer->GetDialect().<field>`. Read that way, a field that changes shape leaves the compiler
// pointing at the DEFINITION and not at the readers. These pin the answers, and two of them pin
// the reason the accessor exists at all rather than the value it happens to return.

TEST(DbCapabilities, NoConnectionIsAnAnswerNotACrash) {
    // A passive / saturated pool hands out no connection, and that is an ordinary state. Every
    // capability answers NO and the DDL runs nothing and says so with 0 rows — none of it is a
    // dereference.
    EXPECT_FALSE(ibCanPushRollup(nullptr));
    EXPECT_FALSE(ibDdlCommitsBeforeData(nullptr));
    EXPECT_FALSE(ibAlterTableMultiClause(nullptr));
    EXPECT_EQ(0, ibExecuteDdl(nullptr, ibDropTable(wxT("nothing"), /*ifExists*/ true)));
}

TEST(DbCapabilities, SqliteAnswersItsOwnDeclarations) {
    ibDatabaseLayerSQLite db;
    // No GROUP BY ROLLUP — so the totals tree folds in RAM here, and the provider must not push it.
    EXPECT_FALSE(ibCanPushRollup(&db));
    // No DDL barrier: a statement is durable the moment it returns, which is exactly why the whole
    // deferral class is invisible to this suite (see the accounting arc).
    EXPECT_FALSE(ibDdlCommitsBeforeData(&db));
    // One ADD/DROP per ALTER — the structure builder splits its batches for this.
    EXPECT_FALSE(ibAlterTableMultiClause(&db));
}

TEST(DbCapabilities, TheBarrierAnswerDiffersByDriver) {
    // The one place the answer is TRUE among the drivers this suite can construct — and the reason
    // a barrier exists in the code at all. Pinned so "SQLite says false" is never mistaken for
    // "nobody says true".
    EXPECT_FALSE(ibDatabaseLayerSQLite::Dialect().m_ddlCommitBeforeData);
    EXPECT_TRUE(ibDatabaseLayerFirebird::Dialect().m_ddlCommitBeforeData);
}

TEST(DbCapabilities, NothingIsMissingOnSqlite) {
    // The vendor-routine hook defaults to "nothing to create", which is what lets the caller stop
    // branching on the engine: only PostgreSQL overrides it (MAX/MIN over uuid).
    ibDatabaseLayerSQLite db;
    EXPECT_TRUE(db.CreateMissingRoutines());
}

// -----------------------------------------------------------------------------
// Live SQLite — the two accessors that DO something rather than answer something
// -----------------------------------------------------------------------------

TEST(DbCapabilities, ExecuteDdlRunsTheStatementAsGiven) {
    ibDatabaseLayerSQLite db;
    if (!db.Open(wxT(":memory:")))
        GTEST_SKIP() << "in-memory SQLite unavailable";

    // ⭐ THE '%' IS THE POINT OF THIS TEST. ibExecuteDdl runs through RunStatement, not RunQuery —
    // and RunQuery is the printf-formatting, ';'-splitting door. A DEFAULT carrying a percent sign
    // is text DESCRIBED by nothing and final as it stands; sent through a format door it is eaten
    // before the server sees it. Going back to RunQuery makes this fail.
    ibDdlColumn tag;
    tag.m_name    = wxT("tag");
    tag.m_type    = ibTypeString(16);
    tag.m_default = wxT("'100%'");

    ibDdlColumn id;
    id.m_name = wxT("id");
    id.m_type = ibTypeInteger();

    ibExecuteDdl(&db, ibCreateTable(wxT("pct"), { id, tag }));
    EXPECT_TRUE(db.TableExists(wxT("pct"))) << "the CREATE did not reach the driver intact";

    db.Close();
}

