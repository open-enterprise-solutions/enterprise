#include "backend/session/sessionLifetime.h"

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/utils.h>

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

ibDateTime At(int year, unsigned month, unsigned day, unsigned hour, unsigned minute)
{
	return ibDateTime(year, month, day, hour, minute, 0, 0);
}

} // namespace

TEST(SessionLifetime, DefaultsAreThirtyMinutesAndEightHours)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 30LL * 60LL * 1000LL);
	EXPECT_EQ(policy.m_absoluteMs, 8LL * 60LL * 60LL * 1000LL);
}

TEST(SessionLifetime, EnvironmentReplacesTheDefaults)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	wxSetEnv(wxT("OES_SESSION_IDLE_MINUTES"), wxT("5"));
	wxSetEnv(wxT("OES_SESSION_ABSOLUTE_HOURS"), wxT("2"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 5LL * 60LL * 1000LL);
	EXPECT_EQ(policy.m_absoluteMs, 2LL * 60LL * 60LL * 1000LL);
}

TEST(SessionLifetime, ABadOverrideLeavesTheDefault)
{
	WxReady wx;
	EnvGuard idle(wxT("OES_SESSION_IDLE_MINUTES"));
	EnvGuard absolute(wxT("OES_SESSION_ABSOLUTE_HOURS"));
	wxSetEnv(wxT("OES_SESSION_IDLE_MINUTES"), wxT("nope"));
	wxSetEnv(wxT("OES_SESSION_ABSOLUTE_HOURS"), wxT("0"));
	const ibSessionLifetime policy = ibSessionLifetimePolicy();
	EXPECT_EQ(policy.m_idleMs, 30LL * 60LL * 1000LL);
	EXPECT_EQ(policy.m_absoluteMs, 8LL * 60LL * 60LL * 1000LL);
}

TEST(SessionLifetime, AnActiveSessionInsideBothLimitsStays)
{
	const ibSessionLifetime policy;
	const ibDateTime auth = At(2026, 6, 1, 12, 0);
	const ibDateTime last = At(2026, 6, 1, 12, 10);
	const ibDateTime now  = At(2026, 6, 1, 12, 20);
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, last, policy), ibSessionEnd::Keep);
}

TEST(SessionLifetime, SilencePastTheIdleLimitEndsIt)
{
	const ibSessionLifetime policy;
	const ibDateTime auth = At(2026, 6, 1, 12, 0);
	const ibDateTime last = At(2026, 6, 1, 12, 0);
	const ibDateTime now  = At(2026, 6, 1, 12, 31);
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, last, policy), ibSessionEnd::Idle);
}

TEST(SessionLifetime, ActivityDoesNotExtendTheAbsoluteLimit)
{
	const ibSessionLifetime policy;
	const ibDateTime auth = At(2026, 6, 1, 12, 0);
	const ibDateTime last = At(2026, 6, 1, 20, 59);
	const ibDateTime now  = At(2026, 6, 1, 21, 0);
	EXPECT_EQ(ibJudgeSessionLifetime(now, auth, auth, last, policy), ibSessionEnd::Absolute);
}

TEST(SessionLifetime, PasswordChangedAfterLoginIsNoLongerThatLogin)
{
	const ibDateTime auth    = At(2026, 6, 1, 12, 0);
	const ibDateTime changed = At(2026, 6, 1, 13, 0);
	EXPECT_FALSE(ibSessionPasswordStillCurrent(auth, changed));
	EXPECT_TRUE(ibSessionPasswordStillCurrent(changed, auth));
	EXPECT_TRUE(ibSessionPasswordStillCurrent(auth, ibDateTime()));
	EXPECT_TRUE(ibSessionPasswordStillCurrent(changed, changed));
}
