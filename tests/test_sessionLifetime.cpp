#include "backend/session/sessionLifetime.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <wx/init.h>
#include <wx/utils.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "core/guid.h"

namespace {

struct WxReady {
	WxReady()
	{
		static wxInitializer init;
		(void)init;
	}
};

struct EnvGuard {
	explicit EnvGuard(const wxString& name)
		: m_name(name)
	{
		m_had = wxGetEnv(name, &m_previous);
		wxUnsetEnv(name);
	}
	~EnvGuard()
	{
		if (m_had)
			wxSetEnv(m_name, m_previous);
		else
			wxUnsetEnv(m_name);
	}
	wxString m_name;
	wxString m_previous;
	bool m_had = false;
};

constexpr long long kMinute = 60LL * 1000LL;
constexpr long long kHour = 60LL * kMinute;

} // namespace

TEST(SessionLifetime, DefaultsAreThirtyMinutesAndEightHours)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 30LL * kMinute);
	EXPECT_EQ(policy.m_absoluteMs, 8LL * kHour);
}

TEST(SessionLifetime, EnvironmentReplacesTheDefaults)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	wxSetEnv(wxT("OES_SESSION_IDLE_MINUTES"), wxT("5"));
	wxSetEnv(wxT("OES_SESSION_ABSOLUTE_HOURS"), wxT("2"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 5LL * kMinute);
	EXPECT_EQ(policy.m_absoluteMs, 2LL * kHour);
}

TEST(SessionLifetime, ABadOverrideLeavesTheDefault)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	wxSetEnv(wxT("OES_SESSION_IDLE_MINUTES"), wxT("nope"));
	wxSetEnv(wxT("OES_SESSION_ABSOLUTE_HOURS"), wxT("0"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 30LL * kMinute);
	EXPECT_EQ(policy.m_absoluteMs, 8LL * kHour);
}

TEST(SessionLifetime, AnActiveSessionInsideBothLimitsStays)
{
	const ibSessionLifetime policy;
	const long long auth = 1'000'000;
	const long long last = auth + 10 * kMinute;
	const long long now  = auth + 20 * kMinute;
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, last, policy), ibSessionEnd::Keep);
}

TEST(SessionLifetime, SilencePastTheIdleLimitEndsIt)
{
	const ibSessionLifetime policy;
	const long long auth = 1'000'000;
	const long long last = auth;
	const long long now  = auth + 31 * kMinute;
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, last, policy), ibSessionEnd::Idle);
}

TEST(SessionLifetime, ActivityDoesNotExtendTheAbsoluteLimit)
{
	const ibSessionLifetime policy;
	const long long auth = 1'000'000;
	const long long now  = auth + 8 * kHour + 1;
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, now, policy), ibSessionEnd::Absolute);
}

TEST(SessionLifetime, AnUnauthenticatedSessionCountsFromBirth)
{
	const ibSessionLifetime policy;
	const long long born = 1'000'000;
	const long long now  = born + 8 * kHour + 1;
	EXPECT_EQ(ibJudgeSessionLifetime(now, born, 0, now, policy), ibSessionEnd::Absolute);
}

TEST(SessionLifetime, ALoginBeforeThePasswordChangeIsOver)
{
	const ibDateTime changed = ibDateTime::Now();
	const long long at = ibWallUnixMs(changed);
	ASSERT_GT(at, 0);
	EXPECT_TRUE(ibSessionPasswordStillCurrent(at, changed));
	EXPECT_TRUE(ibSessionPasswordStillCurrent(at + 1000, changed));
	EXPECT_FALSE(ibSessionPasswordStillCurrent(at - 5000, changed));
	EXPECT_TRUE(ibSessionPasswordStillCurrent(at, ibDateTime()));
	EXPECT_FALSE(ibSessionPasswordStillCurrent(0, changed));
}

namespace {

struct UserStamp : ::testing::Test {
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

		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  /*notNull*/false, /*pk*/true,  wxEmptyString },
			{ wxT("name"),       ibTypeString(64),  /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("fullName"),   ibTypeString(128), /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("changed"),    ibTypeDate(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("dataSize"),   ibTypeInteger(),   /*notNull*/true,  /*pk*/false, wxEmptyString },
			{ wxT("binaryData"), ibTypeBlob(),      /*notNull*/true,  /*pk*/false, wxEmptyString },
		}));
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get(/*required*/ false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST_F(UserStamp, ARehashKeepsTheMomentThePasswordChanged)
{
	ibUserInfo keeper;
	keeper.m_strUserGuid = wxNewUniqueGuid;
	keeper.m_strUserName = wxT("keeper");
	keeper.m_strUserFullName = wxT("Keeper");
	keeper.m_strUserPassword = wxT("$pbkdf2-sha256$old");
	ASSERT_TRUE(ibUserInfo::Save(keeper));

	const ibUserInfo stored = ibUserInfo::Read(ibGuid(keeper.m_strUserGuid));
	ASSERT_TRUE(stored.IsOk());
	ASSERT_FALSE(stored.m_passwordChangedAt.IsEmpty());

	ibUserInfo rehashed = stored;
	rehashed.m_strUserPassword = wxT("$pbkdf2-sha256$rehashed");
	ASSERT_TRUE(ibUserInfo::Save(rehashed, /*passwordChanged=*/false));
	const ibUserInfo afterRehash = ibUserInfo::Read(ibGuid(keeper.m_strUserGuid));
	EXPECT_EQ(afterRehash.m_passwordChangedAt.GetValue(), stored.m_passwordChangedAt.GetValue());
	EXPECT_EQ(afterRehash.m_strUserPassword, rehashed.m_strUserPassword);

	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	ibUserInfo changed = afterRehash;
	changed.m_strUserPassword = wxT("$pbkdf2-sha256$new-password");
	ASSERT_TRUE(ibUserInfo::Save(changed));
	const ibUserInfo afterChange = ibUserInfo::Read(ibGuid(keeper.m_strUserGuid));
	EXPECT_GT(afterChange.m_passwordChangedAt.GetValue(), stored.m_passwordChangedAt.GetValue());
}
