#include <gtest/gtest.h>

#include "backend/session/authThrottle.h"

namespace {

using Clock = ibAuthThrottle::Clock;

Clock::time_point At(int seconds)
{
	return Clock::time_point(std::chrono::seconds(seconds));
}

ibAuthThrottle::Decision Fail(ibAuthThrottle& gate, const wxString& user,
                              const wxString& source, Clock::time_point now)
{
	const ibAuthThrottle::Decision started = gate.Begin(user, source, now);
	if (started.m_allowed)
		gate.Finish(user, source, false, now);
	return started;
}

} // namespace

TEST(AuthThrottle, AFailedNameWaitsLongerEachTimeAndThenAMinute)
{
	ibAuthThrottle gate;
	const int want[] = { 1, 2, 4, 8, 16, 32, 60, 60 };
	int t = 0;
	for (const int delay : want) {
		const ibAuthThrottle::Decision started = Fail(gate, wxT("Ada"), wxT("local"), At(t));
		ASSERT_TRUE(started.m_allowed) << "t=" << t;
		const ibAuthThrottle::Decision again = gate.Begin(wxT("Ada"), wxT("local"), At(t));
		EXPECT_FALSE(again.m_allowed);
		EXPECT_EQ(again.m_retryAfterSec, delay);
		EXPECT_EQ(gate.SecondsUntil(wxT("Ada"), wxT("local"), At(t)), delay);
		t += delay;
	}
}

TEST(AuthThrottle, ASuccessStartsTheWaitAgain)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("local"), At(0)).m_allowed);
	EXPECT_FALSE(gate.Begin(wxT("Ada"), wxT("local"), At(0)).m_allowed);

	ASSERT_TRUE(gate.Begin(wxT("Ada"), wxT("local"), At(1)).m_allowed);
	gate.Finish(wxT("Ada"), wxT("local"), true, At(1));

	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("local"), At(1)).m_allowed);
	const ibAuthThrottle::Decision again = gate.Begin(wxT("Ada"), wxT("local"), At(1));
	EXPECT_FALSE(again.m_allowed);
	EXPECT_EQ(again.m_retryAfterSec, 1);
}

TEST(AuthThrottle, AQuietQuarterHourStartsTheWaitAgain)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("local"), At(0)).m_allowed);
	ASSERT_TRUE(gate.Begin(wxT("Ada"), wxT("local"), At(1)).m_allowed);
	gate.Finish(wxT("Ada"), wxT("local"), false, At(1));
	EXPECT_EQ(gate.Begin(wxT("Ada"), wxT("local"), At(1)).m_retryAfterSec, 2);

	const int later = 1 + ibAuthThrottle::kFailureWindowSec + 1;
	ASSERT_TRUE(Fail(gate, wxT("Ada"), wxT("local"), At(later)).m_allowed);
	EXPECT_EQ(gate.Begin(wxT("Ada"), wxT("local"), At(later)).m_retryAfterSec, 1);
}

TEST(AuthThrottle, TheNameIsTheSameInEitherCase)
{
	ibAuthThrottle gate;
	ASSERT_TRUE(Fail(gate, wxT("Admin"), wxT("10.0.0.1"), At(0)).m_allowed);
	const ibAuthThrottle::Decision again = gate.Begin(wxT(" admin "), wxT("10.0.0.2"), At(0));
	EXPECT_FALSE(again.m_allowed);
	EXPECT_EQ(again.m_retryAfterSec, 1);
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

	// Another address still may, and a success never spent a failure.
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

TEST(AuthThrottle, OnlyFourChecksRunAtOnce)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kMaxInFlightVerifies; ++i) {
		const ibAuthThrottle::Decision started = gate.Begin(wxString::Format(wxT("p%d"), i), wxT("local"), At(0));
		ASSERT_TRUE(started.m_allowed);
	}
	const ibAuthThrottle::Decision fifth = gate.Begin(wxT("p-extra"), wxT("local"), At(0));
	EXPECT_FALSE(fifth.m_allowed);
	EXPECT_EQ(fifth.m_retryAfterSec, 1);

	gate.ReleaseSlot();
	const ibAuthThrottle::Decision after = gate.Begin(wxT("p-extra"), wxT("local"), At(0));
	EXPECT_TRUE(after.m_allowed);
	gate.Finish(wxT("p-extra"), wxT("local"), true, At(0));
}

TEST(AuthThrottle, AnEmptySourceIsTheLocalOne)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kSourceBurst; ++i)
		ASSERT_TRUE(Fail(gate, wxString::Format(wxT("e%d"), i), wxEmptyString, At(0)).m_allowed);
	EXPECT_FALSE(gate.Begin(wxT("e-next"), wxT("local"), At(0)).m_allowed);
}

TEST(AuthThrottle, TheCallingThreadNamesItsSource)
{
	EXPECT_EQ(ibAuthThrottle::CurrentSource(), wxT("local"));
	{
		ibAuthSourceScope outer(wxT("10.2.0.1"));
		EXPECT_EQ(ibAuthThrottle::CurrentSource(), wxT("10.2.0.1"));
		{
			ibAuthSourceScope inner(wxT("10.2.0.2"));
			EXPECT_EQ(ibAuthThrottle::CurrentSource(), wxT("10.2.0.2"));
		}
		EXPECT_EQ(ibAuthThrottle::CurrentSource(), wxT("10.2.0.1"));
	}
	EXPECT_EQ(ibAuthThrottle::CurrentSource(), wxT("local"));

	ibAuthThrottle::NoteRefusal(0);
	EXPECT_EQ(ibAuthThrottle::TakeRefusal(), 0);
	ibAuthThrottle::NoteRefusal(12);
	EXPECT_EQ(ibAuthThrottle::TakeRefusal(), 12);
	EXPECT_EQ(ibAuthThrottle::TakeRefusal(), 0);
}
