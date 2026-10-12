// An unknown name used to return before any hashing, so it was hundreds of
// milliseconds faster than a known name with a wrong password. The login
// dialog also listed every name before a password was entered.

#include <gtest/gtest.h>

#include <chrono>

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/utils/passwordHash.hpp"
#include "core/guid.h"

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
};

} // namespace

TEST(LoginList, AServerDoesNotListUsersUnlessTheBaseAsks)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	EXPECT_TRUE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eFILE_MODE, wxEmptyString));
	EXPECT_FALSE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eSERVER_MODE, wxEmptyString));
	EXPECT_TRUE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eSANDBOX_MODE, wxEmptyString));

	const wxString dir = wxFileName::GetTempDir()
		+ wxFileName::GetPathSeparator() + wxT("oes-login-name-387e");
	ASSERT_TRUE(wxFileName::Mkdir(dir, 0700, wxPATH_MKDIR_FULL));
	const wxString path = dir + wxFileName::GetPathSeparator() + wxT("infobase.conf");

	{
		wxFFile file(path, wxT("w"));
		ASSERT_TRUE(file.IsOpened());
		ASSERT_TRUE(file.Write(wxT("ShowUserListAtLogin = 0\n")));
	}
	EXPECT_FALSE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eFILE_MODE, dir));
	EXPECT_FALSE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eSERVER_MODE, dir));

	{
		wxFFile file(path, wxT("w"));
		ASSERT_TRUE(file.IsOpened());
		ASSERT_TRUE(file.Write(wxT("ShowUserListAtLogin = 1\n")));
	}
	EXPECT_TRUE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eFILE_MODE, dir));
	EXPECT_TRUE(ibApplicationInstance::ShowUserListAtLogin(ibRunMode::eSERVER_MODE, dir));

	wxRemoveFile(path);
	wxFileName::Rmdir(dir);

	ASSERT_TRUE(ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE));
	EXPECT_TRUE(ibApplicationInstance::Get()->ShowUserListAtLogin());
	ibApplicationInstance::DestroyAppDataEnv();

	ASSERT_TRUE(ibApplicationInstance::CreateAppDataEnv(ibRunMode::eSERVER_MODE));
	EXPECT_FALSE(ibApplicationInstance::Get()->ShowUserListAtLogin());
	ibApplicationInstance::DestroyAppDataEnv();
}

TEST_F(LoginNameFix, AnUnknownNameCostsTheSameAsAWrongPassword)
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

	const wxString password = wxT("right-secret");
	ibUserInfo keeper;
	keeper.m_strUserGuid = wxNewUniqueGuid;
	keeper.m_strUserName = wxT("keeper");
	keeper.m_strUserFullName = wxT("Keeper");
	keeper.m_strUserPassword = ibPasswordHash::Hash(password);
	ASSERT_TRUE(keeper.m_strUserPassword.StartsWith(wxT("$pbkdf2-sha256$")));
	ASSERT_TRUE(ibUserInfo::Save(keeper));

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

	const long slower = unknownMs > wrongMs ? unknownMs : wrongMs;
	const long faster = unknownMs < wrongMs ? unknownMs : wrongMs;
	EXPECT_GT(faster, 40) << "unknown " << unknownMs << " ms, wrong " << wrongMs << " ms";
	EXPECT_LT(static_cast<double>(slower - faster) / static_cast<double>(slower), 0.10)
		<< "unknown " << unknownMs << " ms, wrong " << wrongMs << " ms";
}
