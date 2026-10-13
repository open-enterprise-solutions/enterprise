// A bound blob outlives the buffer the caller handed in. SQLITE_STATIC kept that
// pointer, so a result set still stepping read freed memory.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include <wx/init.h>

#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"

TEST(SqliteBlob, BoundBytesSurviveTheCallerBuffer)
{
	wxInitializer wx;
	ASSERT_TRUE(wx.IsOk());

	ibDatabaseLayerSQLite db;
	ASSERT_TRUE(db.Open(wxT(":memory:")));
	ASSERT_GE(db.RunQuery(wxT("CREATE TABLE b (payload BLOB)")), 0);

	std::vector<unsigned char> bytes(16, 0xAB);
	bytes[0] = 0x00;
	bytes[15] = 0xFF;

	ibStatementGuard stmt(&db, db.PrepareStatement(wxT("INSERT INTO b (payload) VALUES (?)")));
	ASSERT_TRUE(static_cast<bool>(stmt));
	stmt->SetParamBlob(1, bytes.data(), static_cast<long>(bytes.size()));
	std::fill(bytes.begin(), bytes.end(), 0x00);
	stmt->RunQuery();

	ibResultSetGuard rows(&db, db.RunQueryWithResults(wxT("SELECT payload FROM b")));
	ASSERT_TRUE(static_cast<bool>(rows));
	ASSERT_TRUE(rows->Next());

	wxMemoryBuffer readBack;
	rows->GetResultBlob(1, readBack);
	ASSERT_EQ(16u, readBack.GetDataLen());
	const unsigned char* got = static_cast<const unsigned char*>(readBack.GetData());
	EXPECT_EQ(0x00, got[0]);
	EXPECT_EQ(0xAB, got[1]);
	EXPECT_EQ(0xFF, got[15]);
}
