#include <gtest/gtest.h>

#include "backend/session/authThrottle.h"

namespace {

using Clock = ibAuthThrottle::Clock;

Clock::time_point At(int seconds)
{
	return Clock::time_point(std::chrono::seconds(seconds));
}

ibAuthThrottle::Decision Fail(ibAuthThrottle& gate, const wxString& user,
                              const wxString& address, Clock::time_point now)
{
	const ibAuthThrottle::Decision started = gate.Begin(user, address, now);
	if (started.m_allowed)
		gate.Finish(user, address, false, now);
	return started;
}

} // namespace

TEST(AuthThrottle, AFailedNameWaitsLongerEachTimeAndThenAMinute)
{
	ibAuthThrottle gate;
	const int want[] = { 1, 2, 4, 8, 16, 32, 60 };
	int t = 0;
	for (int i = 0; i < 7; ++i) {
		const ibAuthThrottle::Decision started = Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(t));
		ASSERT_TRUE(started.m_allowed) << "t=" << t;
		const ibAuthThrottle::Decision again = gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(t));
		EXPECT_FALSE(again.m_allowed);
		EXPECT_EQ(again.m_retryAfterSec, want[i]);
		EXPECT_EQ(again.m_reason, ibAuthThrottle::Reason::Account);
		if (i + 1 < 7)
			t += want[i];
	}

	// One second of that minute is still left. The answer is the time
	// remaining, which is what a client waits.
	const ibAuthThrottle::Decision held = gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(t + 59));
	EXPECT_FALSE(held.m_allowed);
	EXPECT_EQ(held.m_retryAfterSec, 1);
	EXPECT_EQ(held.m_reason, ibAuthThrottle::Reason::Account);
}

TEST(AuthThrottle, AWrongPasswordOnceAMinuteStaysAtOneSecond)
{
	ibAuthThrottle gate;
	int t = 0;
	for (int i = 0; i < 5; ++i) {
		ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(t)).m_allowed);
		const ibAuthThrottle::Decision again = gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(t));
		EXPECT_FALSE(again.m_allowed);
		EXPECT_EQ(again.m_retryAfterSec, 1) << "attempt " << i;
		t += 60;
	}
}

TEST(AuthThrottle, ASuccessStartsTheWaitAgain)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(0)).m_allowed);
	EXPECT_FALSE(gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(0)).m_allowed);

	ASSERT_TRUE(gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(1)).m_allowed);
	gate.Finish(wxT("Ada"), wxT("10.0.0.1"), true, At(1));

	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(1)).m_allowed);
	const ibAuthThrottle::Decision again = gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(1));
	EXPECT_FALSE(again.m_allowed);
	EXPECT_EQ(again.m_retryAfterSec, 1);
}

TEST(AuthThrottle, AQuietMinuteStartsTheWaitAgain)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(0)).m_allowed);
	ASSERT_TRUE(gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(1)).m_allowed);
	gate.Finish(wxT("Ada"), wxT("10.0.0.1"), false, At(1));
	EXPECT_EQ(gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(1)).m_retryAfterSec, 2);

	const int later = 1 + ibAuthThrottle::kFailureWindowSec;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("10.0.0.1"), At(later)).m_allowed);
	EXPECT_EQ(gate.Begin(wxT("Ada"), wxT("10.0.0.1"), At(later)).m_retryAfterSec, 1);
}

TEST(AuthThrottle, TheNameIsTheSameInEitherCase)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Admin"), wxT("10.0.0.1"), At(0)).m_allowed);
	const ibAuthThrottle::Decision again = gate.Begin(wxT(" admin "), wxT("10.0.0.2"), At(0));
	EXPECT_FALSE(again.m_allowed);
	EXPECT_EQ(again.m_retryAfterSec, 1);
	EXPECT_EQ(again.m_reason, ibAuthThrottle::Reason::Account);
}

TEST(AuthThrottle, OneAddressCannotGuessEveryName)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kSourceBurst; ++i) {
		const wxString name = wxString::Format(wxT("u%d"), i);
		ASSERT_TRUE(Fail(gate, name, wxT("10.0.0.1"), At(0)).m_allowed) << name;
	}
	const ibAuthThrottle::Decision blocked = gate.Begin(wxT("u-next"), wxT("10.0.0.1"), At(0));
	EXPECT_FALSE(blocked.m_allowed);
	EXPECT_GE(blocked.m_retryAfterSec, 1);
	EXPECT_EQ(blocked.m_reason, ibAuthThrottle::Reason::Address);

	ibAuthThrottle fresh;
	for (int i = 0; i < ibAuthThrottle::kSourceBurst; ++i) {
		const wxString name = wxString::Format(wxT("ok%d"), i);
		ASSERT_TRUE(fresh.Begin(name, wxT("10.0.0.1"), At(0)).m_allowed);
		fresh.Finish(name, wxT("10.0.0.1"), true, At(0));
	}
	EXPECT_TRUE(fresh.Begin(wxT("ok-next"), wxT("10.0.0.1"), At(0)).m_allowed);
	fresh.Finish(wxT("ok-next"), wxT("10.0.0.1"), true, At(0));

	EXPECT_TRUE(gate.Begin(wxT("u-next"), wxT("10.0.0.9"), At(0)).m_allowed);
	gate.Finish(wxT("u-next"), wxT("10.0.0.9"), false, At(0));
}

TEST(AuthThrottle, ASourceRegainsOneFailureASecond)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kSourceBurst; ++i)
		ASSERT_TRUE(Fail(gate, wxString::Format(wxT("n%d"), i), wxT("10.1.0.1"), At(0)).m_allowed);
	EXPECT_FALSE(gate.Begin(wxT("later"), wxT("10.1.0.1"), At(0)).m_allowed);

	ASSERT_TRUE(Fail(gate, wxT("later"), wxT("10.1.0.1"), At(1)).m_allowed);
	EXPECT_FALSE(gate.Begin(wxT("after"), wxT("10.1.0.1"), At(1)).m_allowed);
}

TEST(AuthThrottle, AnEmptyAddressDoesNotLockAnyoneElse)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kSourceBurst + 2; ++i)
		ASSERT_TRUE(Fail(gate, wxString::Format(wxT("e%d"), i), wxEmptyString, At(0)).m_allowed);
	EXPECT_TRUE(gate.Begin(wxT("e-next"), wxEmptyString, At(0)).m_allowed);
	EXPECT_TRUE(gate.Begin(wxT("desk"), wxT("127.0.0.1:9"), At(0)).m_allowed);
	EXPECT_TRUE(gate.Begin(wxT("v6"), wxT("::1"), At(0)).m_allowed);
	EXPECT_TRUE(gate.Begin(wxT("local-name"), wxT("localhost"), At(0)).m_allowed);
}

TEST(AuthThrottle, TwoGatesDoNotShareAFailure)
{
	ibAuthThrottle one;
	ibAuthThrottle other;
	ASSERT_TRUE(Fail(one, wxT("Ada"), wxT("10.0.0.1"), At(0)).m_allowed);
	const ibAuthThrottle::Decision onTheOther = other.Begin(wxT("Ada"), wxT("10.0.0.1"), At(0));
	EXPECT_TRUE(onTheOther.m_allowed);
	EXPECT_EQ(onTheOther.m_reason, ibAuthThrottle::Reason::None);
}

TEST(AuthThrottle, TheMapsDoNotGrowWithoutABound)
{
	ibAuthThrottle gate;
	const int past = static_cast<int>(ibAuthThrottle::kMaxTracked) + 50;
	for (int i = 0; i < past; ++i) {
		const wxString name = wxString::Format(wxT("n%d"), i);
		const wxString address = wxString::Format(wxT("10.9.%d.%d"), (i / 250) % 250, i % 250);
		ASSERT_TRUE(Fail(gate, name, address, At(i)).m_allowed);
	}
	// A name from the start has been evicted: its wait is not still standing.
	EXPECT_TRUE(gate.Begin(wxT("n0"), wxT("10.8.0.1"), At(past)).m_allowed);
}
