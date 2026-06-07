// Lifecycle / leak-safety tests for the L2 query cursor
// (docs/query-language-arc.md §12 line 1, §13).
//
// Proves the core RAII guarantee WITHOUT the connection pool or appData: an
// ibQueryResult releases its statement + result set in its destructor — on the
// normal path, during exception unwind, and exactly once across a move. The
// full "pool returns to rest after Execute" invariant is integration scope
// (ibDatabaseQueryBuilder routes through the global ibApplicationData pool) and
// is exercised against a real SQLite database, not here.
//
// Counting mocks: the connection's CloseResultSet / CloseStatement forward to
// the object's Close(), which bumps a counter the test inspects. The mocks are
// stack-owned by the test; ibQueryResult only closes them, never deletes them.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "mock_database_layer.h"

namespace {

class CountingStatement : public ibPreparedStatement {
public:
	int m_closeCount = 0;

	void Close() override { ++m_closeCount; }

	void SetParamInt(int, int) override {}
	void SetParamDouble(int, double) override {}
	void SetParamNumber(int, const ibNumber&) override {}
	void SetParamString(int, const wxString&) override {}
	void SetParamNull(int) override {}
	void SetParamBlob(int, const void*, long) override {}
	void SetParamDate(int, const wxDateTime&) override {}
	void SetParamBool(int, bool) override {}
	int  GetParameterCount() override { return 0; }
	int  RunQuery() override { return 0; }
	ibDatabaseResultSet* RunQueryWithResults() override { return nullptr; }
};

class CountingResultSet : public ibDatabaseResultSet {
public:
	int m_closeCount = 0;

	bool Next() override { return false; }
	void Close() override { ++m_closeCount; }

	int        LookupField(const wxString&) override { return -1; }
	int        GetResultInt(int) override { return 0; }
	wxString   GetResultString(int) override { return wxString(); }
	long long  GetResultLong(int) override { return 0; }
	bool       GetResultBool(int) override { return false; }
	wxDateTime GetResultDate(int) override { return wxDateTime(); }
	void*      GetResultBlob(int, wxMemoryBuffer&) override { return nullptr; }
	double     GetResultDouble(int) override { return 0.0; }
	ibNumber   GetResultNumber(int) override { return ibNumber(); }
	bool       IsFieldNull(int) override { return true; }
	ibResultSetMetaData* GetMetaData() override { return nullptr; }
};

// Connection whose Close* forward to the object's Close() so the test can
// observe that ibQueryResult released them. Reuses MockDatabaseLayer for the
// rest of the (unused) driver surface.
class CountingConn : public MockDatabaseLayer {
public:
	bool CloseResultSet(ibDatabaseResultSet*& rs) override {
		if (rs) rs->Close();
		rs = nullptr;
		return true;
	}
	bool CloseStatement(ibPreparedStatement*& st) override {
		if (st) st->Close();
		st = nullptr;
		return true;
	}
};

} // namespace

TEST(QueryLifecycle, DestructorReleasesStatementAndResultSet) {
	CountingStatement stmt;
	CountingResultSet rs;
	auto conn = std::make_shared<CountingConn>();
	{
		ibQueryResult r(conn, &stmt, &rs);
	}
	EXPECT_EQ(rs.m_closeCount, 1);
	EXPECT_EQ(stmt.m_closeCount, 1);
}

TEST(QueryLifecycle, ReleasesDuringExceptionUnwind) {
	CountingStatement stmt;
	CountingResultSet rs;
	auto conn = std::make_shared<CountingConn>();
	try {
		ibQueryResult r(conn, &stmt, &rs);
		throw std::runtime_error("boom");   // unwind before the scope ends naturally
	} catch (const std::exception&) {
	}
	EXPECT_EQ(rs.m_closeCount, 1);
	EXPECT_EQ(stmt.m_closeCount, 1);
}

TEST(QueryLifecycle, MoveTransfersOwnershipNoDoubleClose) {
	CountingStatement stmt;
	CountingResultSet rs;
	auto conn = std::make_shared<CountingConn>();
	{
		ibQueryResult a(conn, &stmt, &rs);
		ibQueryResult b(std::move(a));   // a is now empty; only b owns the handles
	}
	EXPECT_EQ(rs.m_closeCount, 1);
	EXPECT_EQ(stmt.m_closeCount, 1);
}

TEST(QueryLifecycle, MoveAssignReleasesPriorHandles) {
	CountingStatement stmt1, stmt2;
	CountingResultSet rs1, rs2;
	auto conn = std::make_shared<CountingConn>();
	{
		ibQueryResult a(conn, &stmt1, &rs1);
		ibQueryResult b(conn, &stmt2, &rs2);
		b = std::move(a);   // b's original handles (stmt2/rs2) released here
		EXPECT_EQ(rs2.m_closeCount, 1);
		EXPECT_EQ(stmt2.m_closeCount, 1);
	}                       // b (now holding stmt1/rs1) releases on scope exit
	EXPECT_EQ(rs1.m_closeCount, 1);
	EXPECT_EQ(stmt1.m_closeCount, 1);
}
