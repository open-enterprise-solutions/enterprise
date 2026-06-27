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
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h" // ibDatabaseLayerSQLite::GetDialect

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
