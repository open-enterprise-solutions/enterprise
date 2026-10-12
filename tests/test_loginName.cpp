// The login by name. An unknown name used to return before any hashing, so it was
// hundreds of milliseconds faster than a known name with a wrong password. The
// thin client's login window asks the names first (users), as the desktop's lists them.

#include <gtest/gtest.h>

#include <chrono>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/utils/passwordHash.hpp"
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
	void Keep(const wxString& password)
	{
		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true,  wxEmptyString },
			{ wxT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
		}));

		ibUserInfo keeper;
		keeper.m_strUserGuid = wxNewUniqueGuid;
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

	ibClientHost host(nullptr);
	const wxString answer = host.Call(wxT(R"({"jsonrpc":"2.0","id":1,"method":"users","params":{}})"));
	EXPECT_FALSE(answer.Contains(wxT("\"error\""))) << answer;
	EXPECT_TRUE(answer.Contains(wxT("\"Users\""))) << answer;
	EXPECT_TRUE(answer.Contains(wxT("\"keeper\""))) << answer;
}

TEST_F(LoginNameFix, AnUnknownNameCostsTheSameAsAWrongPassword)
{
	const wxString password = wxT("right-secret");
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
