// =============================================================================
// OES Enterprise — a statement that fails in the driver's OWN transaction, against a live Firebird (#648)
//
// With no transaction open, the Firebird layer runs a statement in a one-statement transaction of its
// own (the "quickie") and rolls it back when the statement fails. Two things went wrong on that path:
//
//   - RunQuery rolled back BEFORE it threw, and DoRollBack begins with ResetErrorCodes: the code and
//     the message just read from Firebird were cleared, and the exception went out with an empty
//     description and a code of 0;
//   - RunQueryWithResults checked an error code after each statement but the last — and a failed
//     statement throws, it never answers with a code — so a failure before the last statement left the
//     quickie open, its depth counted, and every later statement ran inside a transaction nobody owned.
//
// The steps are the issue's: a table, a unique index, and the same row inserted twice, outside any
// transaction.
//
// SKIPS when no Firebird client can be loaded — the general CI jobs have none (docs/portability.md).
// CI's Firebird job runs `Firebird*` with the kit beside the binary, and fails on a skip.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseLayerException.h"
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <string>

namespace {

// isc_no_dup — "attempt to store duplicate value (visible to active transactions) in unique index".
constexpr int kFirebirdNoDup = 335544349;

ibString ScratchDbPath(const char* testName)
{
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return wxFileName(wxStandardPaths::Get().GetTempDir(),
		ibString::Format(ibT("oes_fb_quick_%s_%lld.fdb"), testName, (long long)now)).GetFullPath();
}

class FirebirdQuickTransaction : public ::testing::Test {
protected:
	// shared_ptr: a statement holds a weak_ptr back to its layer.
	std::shared_ptr<ibDatabaseLayerFirebird> layer;
	ibString dbPath;

	void SetUp() override {
		dbPath = ScratchDbPath(::testing::UnitTest::GetInstance()->current_test_info()->name());

		// The constructor loads fbclient and raises where there is none, so it is inside the guard —
		// and only the question "can this machine reach a Firebird" is. The table and the index are
		// made outside it: a failure there is a failure, not a skip.
		try {
			layer = std::make_shared<ibDatabaseLayerFirebird>();
			layer->SetUser(ibT("SYSDBA"));
			layer->SetPassword(ibT("masterkey"));
			if (!layer->Open(dbPath))
				layer.reset();
		}
		catch (...) { layer.reset(); }

		if (!layer)
			GTEST_SKIP() << "no Firebird client on this machine (" << dbPath.ToStdString() << ")";

		// Every statement here runs with no transaction open — each in the layer's own quickie.
		ASSERT_FALSE(layer->IsActiveTransaction());
		layer->RunQuery(ibT("CREATE TABLE T (A INTEGER NOT NULL)"));
		layer->RunQuery(ibT("CREATE UNIQUE INDEX T_UQ ON T (A)"));
		layer->RunQuery(ibT("INSERT INTO T (A) VALUES (1)"));
		ASSERT_FALSE(layer->IsActiveTransaction());
	}

	void TearDown() override {
		if (layer && layer->IsOpen()) layer->Close();
		layer.reset();
		if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
	}

	int CountRows() {
		ibDatabaseResultSet* rs = layer->RunQueryWithResults(ibT("SELECT COUNT(*) FROM T"));
		int count = -1;
		if (rs != nullptr) {
			if (rs->Next())
				count = rs->GetResultInt(1);
			layer->CloseResultSet(rs);
		}
		return count;
	}

	// What the caught exception says, held after the catch.
	struct Caught {
		bool thrown = false;
		std::string what;
		ibString description;
		int driverCode = 0;
		ibBackendDatabaseException::Kind kind = ibBackendDatabaseException::Kind::Unknown;
	};

	static void Hold(Caught& caught, const ibDatabaseLayerException& err) {
		caught.thrown = true;
		caught.what = err.what();
		caught.description = err.GetErrorDescription();
		caught.driverCode = err.GetDriverErrorCode();
		caught.kind = err.GetKind();
	}

	// The connection is usable and its books balance: nothing is left open, a statement of its own runs,
	// and a transaction opened and committed by hand opens and closes at depth one.
	void ExpectNoTransactionLeftOpen(int insertValue) {
		EXPECT_FALSE(layer->IsActiveTransaction()) << "the layer's own transaction was left open";
		EXPECT_NO_THROW(layer->RunQuery(ibString::Format(ibT("INSERT INTO T (A) VALUES (%d)"), insertValue)));
		EXPECT_FALSE(layer->IsActiveTransaction());

		layer->BeginTransaction();
		EXPECT_TRUE(layer->IsActiveTransaction());
		EXPECT_NO_THROW(layer->RunQuery(ibString::Format(ibT("INSERT INTO T (A) VALUES (%d)"), insertValue + 1)));
		EXPECT_NO_THROW(layer->Commit());
		EXPECT_FALSE(layer->IsActiveTransaction()) << "Begin / Commit did not balance";
	}
};

} // namespace

// The issue's steps: the second INSERT of the same value, outside a transaction, is refused — and the
// refusal says what Firebird said, with its code, rather than "".
TEST_F(FirebirdQuickTransaction, ADuplicateOutsideATransactionSaysWhatFirebirdSaid)
{
	Caught caught;
	try {
		layer->RunQuery(ibT("INSERT INTO T (A) VALUES (1)"));
	}
	catch (const ibDatabaseLayerException& err) { Hold(caught, err); }

	ASSERT_TRUE(caught.thrown) << "the duplicate was not refused";
	EXPECT_NE(caught.what.find("duplicate"), std::string::npos) << "what(): \"" << caught.what << "\"";
	EXPECT_NE(caught.what.find("T_UQ"), std::string::npos) << "what(): \"" << caught.what << "\"";
	EXPECT_FALSE(caught.description.IsEmpty());
	EXPECT_EQ(caught.driverCode, kFirebirdNoDup);
	EXPECT_EQ(caught.kind, ibBackendDatabaseException::Kind::Constraint);

	ExpectNoTransactionLeftOpen(10);
	EXPECT_EQ(CountRows(), 3);   // 1, then 10 and 11 — the refused row is not there
}

// Several statements in one RunQueryWithResults, with no transaction open: they run in one quickie,
// and the one that fails is NOT the last. The quickie is rolled back — the row inserted before the
// failure goes with it — the exception says what Firebird said, and nothing is left open.
TEST_F(FirebirdQuickTransaction, AFailureBeforeTheLastStatementOfABatchRollsTheBatchBack)
{
	Caught caught;
	try {
		ibDatabaseResultSet* rs = layer->RunQueryWithResults(
			ibT("INSERT INTO T (A) VALUES (5); INSERT INTO T (A) VALUES (1); SELECT COUNT(*) FROM T"));
		if (rs != nullptr)
			layer->CloseResultSet(rs);
	}
	catch (const ibDatabaseLayerException& err) { Hold(caught, err); }

	ASSERT_TRUE(caught.thrown) << "the duplicate was not refused";
	EXPECT_NE(caught.what.find("duplicate"), std::string::npos) << "what(): \"" << caught.what << "\"";
	EXPECT_EQ(caught.driverCode, kFirebirdNoDup);

	ExpectNoTransactionLeftOpen(20);
	EXPECT_EQ(CountRows(), 3);   // 1, then 20 and 21 — the batch's 5 was rolled back with it
}
