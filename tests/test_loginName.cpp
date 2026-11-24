// The login by name. An unknown name used to return before any hashing, so it was
// hundreds of milliseconds faster than a known name with a wrong password. The
// thin client's login window asks the names first (users), as the desktop's lists them. And a name is one account,
// in whatever case it is typed (#376).

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <chrono>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/utils/passwordHash.hpp"
#include "core/exception.h"   // ibCoreException — a save refused for its name
#include "core/guid.h"
#include "frmserver/client/clientHost.h"

namespace {

struct LoginNameFix : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	void SetUp() override
	{
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get(/*required*/ false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// The base's one user, keeper, with the password given.
	void Keep(const ibString& password)
	{
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true },
			{ wxT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false },
			{ wxT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false },
			{ wxT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false },
			{ wxT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false },
			{ wxT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false },
		}));

		ibUserInfo keeper;
		keeper.m_strUserGuid = ibNewUniqueGuid;
		keeper.m_strUserName = wxT("keeper");
		keeper.m_strUserFullName = wxT("Keeper");
		keeper.m_strUserPassword = ibPasswordHash::Hash(password);
		ASSERT_TRUE(keeper.m_strUserPassword.StartsWith(wxT("$pbkdf2-sha256$")));
		ASSERT_TRUE(ibUserInfo::Save(keeper));
	}
};

} // namespace

// The thin client's login window lists the names as the desktop's does; it asks before any login, so no Client.
TEST_F(LoginNameFix, UsersAnswersTheNamesBeforeAnyLogin)
{
	Keep(wxT("right-secret"));

	// The host of the test's base, as a server makes one per base: `users` binds the caller's thread to it for the read.
	ibClientHost host(appData);
	const ibString answer = host.Call(wxT(R"({"jsonrpc":"2.0","id":1,"method":"users","params":{}})"));
	EXPECT_FALSE(answer.Contains(wxT("\"error\""))) << answer;
	EXPECT_TRUE(answer.Contains(wxT("\"Users\""))) << answer;
	EXPECT_TRUE(answer.Contains(wxT("\"keeper\""))) << answer;
}

TEST_F(LoginNameFix, AnUnknownNameCostsTheSameAsAWrongPassword)
{
	const ibString password = wxT("right-secret");
	Keep(password);

	ibUserInfo info;
	ASSERT_TRUE(appData->AuthenticateUser(wxT("keeper"), password, info));
	EXPECT_TRUE(info.IsOk());

	// Two of each, added, so a single slow sample does not decide the ratio.
	long unknownMs = 0;
	long wrongMs = 0;
	for (int i = 0; i < 2; ++i) {
		const auto u0 = std::chrono::steady_clock::now();
		EXPECT_FALSE(appData->AuthenticateUser(wxT("nobody"), password, info));
		unknownMs += std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - u0).count();

		const auto w0 = std::chrono::steady_clock::now();
		EXPECT_FALSE(appData->AuthenticateUser(wxT("keeper"), wxT("wrong-secret"), info));
		wrongMs += std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - w0).count();
	}

	// One side only: refused before the hashing, an unknown name took a few milliseconds against the
	// hundreds a wrong password takes. Half is far from both, so a slow machine does not decide it.
	EXPECT_GT(unknownMs * 2, wrongMs) << "unknown " << unknownMs << " ms, wrong " << wrongMs << " ms";
}

// A name is one account, however it is spelled (#376): typed in another case or with spaces around it, it signs the
// same account in, and the account comes back spelled as it was saved.
TEST_F(LoginNameFix, ANameSignsItsAccountInWhateverItsCase)
{
	const ibString password = wxT("right-secret");
	Keep(password);

	ibUserInfo info;
	ASSERT_TRUE(appData->AuthenticateUser(wxT(" KEEPER "), password, info));
	EXPECT_EQ(info.m_strUserName, ibString(wxT("keeper")));
	EXPECT_TRUE(ibUserInfo::Read(wxT("Keeper")).IsOk());
	EXPECT_FALSE(ibUserInfo::Read(wxT("   ")).IsOk());
}

// A second account of a name already held is refused, in any spelling - before #376 it was another row, and a login
// by that name took whichever the base read first. The table keeps its one account.
TEST_F(LoginNameFix, ASecondAccountOfANameIsRefused)
{
	Keep(wxT("right-secret"));

	ibUserInfo second;
	second.m_strUserGuid = ibNewUniqueGuid;
	second.m_strUserName = wxT("Keeper ");
	second.m_strUserFullName = wxT("Another keeper");
	EXPECT_THROW(ibUserInfo::Save(second), ibCoreException);
	EXPECT_EQ(ibUserInfo::ListAll().size(), 1u);
}

// An account may keep its name or spell it anew; a name of only spaces is no name and is refused.
TEST_F(LoginNameFix, AnAccountRespellsItsNameButCannotHaveNone)
{
	Keep(wxT("right-secret"));

	ibUserInfo keeper = ibUserInfo::Read(wxT("keeper"));
	ASSERT_TRUE(keeper.IsOk());
	keeper.m_strUserName = wxT("Keeper");
	EXPECT_TRUE(ibUserInfo::Save(keeper));
	EXPECT_EQ(ibUserInfo::Read(wxT("keeper")).m_strUserName, ibString(wxT("Keeper")));

	keeper.m_strUserName = wxT("  ");
	EXPECT_THROW(ibUserInfo::Save(keeper), ibCoreException);
	EXPECT_TRUE(ibUserInfo::Read(wxT("keeper")).IsOk());
}
