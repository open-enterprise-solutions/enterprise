// How often a login may try a password (backend/session/sessionThrottle.h): the gate alone, on a clock the test holds.
// The door itself is in test_sessionFailover.cpp.

#include <gtest/gtest.h>

#include "backend/session/sessionThrottle.h"

namespace {

using Clock = ibAuthThrottle::Clock;

const ibString kFar = ibT("203.0.113.5");
const ibString kOther = ibT("203.0.113.6");

Clock::time_point At(int seconds)
{
	return Clock::time_point(std::chrono::seconds(seconds));
}

void Failed(ibAuthThrottle& gate, const ibString& name, const ibString& address, Clock::time_point now)
{
	ASSERT_TRUE(gate.Begin(name, address, now).m_allowed);
	gate.Fail(name, address, now);
}

} // namespace

TEST(AuthThrottle, AFailedNameWaitsLongerEachTimeAndThenAMinute)
{
	ibAuthThrottle gate;
	int now = 1000;
	for (const int wait : { 1, 2, 4, 8, 16, 32, 60, 60 }) {
		Failed(gate, ibT("Ada"), kFar, At(now));
		const ibAuthThrottle::Decision held = gate.Begin(ibT("Ada"), kFar, At(now));
		EXPECT_FALSE(held.m_allowed);
		EXPECT_EQ(held.m_retryAfterSec, wait);
		EXPECT_EQ(held.m_reason, ibAuthThrottle::Reason::Account);
		now += wait;
	}
}

// ⭐ A stranger who keeps getting a password wrong holds up nobody else.
TEST(AuthThrottle, AFailureFromOneAddressHoldsUpNoOtherAddress)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("admin"), kFar, At(1000));
	EXPECT_FALSE(gate.Begin(ibT("admin"), kFar, At(1000)).m_allowed);
	EXPECT_TRUE(gate.Begin(ibT("admin"), kOther, At(1000)).m_allowed);
}

// ⭐ Spent as it begins: a salvo does not get through before its first guess has failed.
TEST(AuthThrottle, GuessesSentTogetherSpendTheAddressBeforeAnyHasFailed)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts; ++i)
		EXPECT_TRUE(gate.Begin(ibString::Format(ibT("name-%d"), i), kFar, At(1000)).m_allowed);
	const ibAuthThrottle::Decision held = gate.Begin(ibT("one-more"), kFar, At(1000));
	EXPECT_FALSE(held.m_allowed);
	EXPECT_EQ(held.m_reason, ibAuthThrottle::Reason::Address);
	EXPECT_TRUE(gate.Begin(ibT("one-more"), kOther, At(1000)).m_allowed);
}

TEST(AuthThrottle, AnAddressRegainsAnAttemptASecond)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts; ++i)
		Failed(gate, ibString::Format(ibT("name-%d"), i), kFar, At(1000));
	EXPECT_FALSE(gate.Begin(ibT("next"), kFar, At(1000)).m_allowed);
	EXPECT_TRUE(gate.Begin(ibT("next"), kFar, At(1001)).m_allowed);
	EXPECT_FALSE(gate.Begin(ibT("after"), kFar, At(1001)).m_allowed);
}

// A morning when everyone arrives costs nothing: a success gives its attempt back.
TEST(AuthThrottle, ASuccessSpendsNothing)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts * 3; ++i) {
		const ibString name = ibString::Format(ibT("name-%d"), i);
		ASSERT_TRUE(gate.Begin(name, kFar, At(1000)).m_allowed);
		gate.Succeed(name, kFar);
	}
}

// Correct passwords of one shared name, side by side, are not refused: a client does not retry.
TEST(AuthThrottle, BeforeAnyFailureOneNameChecksSideBySide)
{
	ibAuthThrottle gate;
	for (int i = 0; i < 10; ++i)
		EXPECT_TRUE(gate.Begin(ibT("shared"), kFar, At(1000)).m_allowed);
}

TEST(AuthThrottle, AfterAFailureOneCheckAtATime)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), kFar, At(1000));
	ASSERT_TRUE(gate.Begin(ibT("Ada"), kFar, At(1001)).m_allowed);
	const ibAuthThrottle::Decision second = gate.Begin(ibT("Ada"), kFar, At(1001));
	EXPECT_FALSE(second.m_allowed);
	EXPECT_EQ(second.m_retryAfterSec, 1);
	gate.Fail(ibT("Ada"), kFar, At(1001));
	EXPECT_EQ(gate.Begin(ibT("Ada"), kFar, At(1001)).m_retryAfterSec, 2);
}

TEST(AuthThrottle, ASuccessClearsTheName)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), kFar, At(1000));
	ASSERT_TRUE(gate.Begin(ibT("Ada"), kFar, At(1001)).m_allowed);
	gate.Succeed(ibT("Ada"), kFar);
	Failed(gate, ibT("Ada"), kFar, At(1001));
	EXPECT_EQ(gate.Begin(ibT("Ada"), kFar, At(1001)).m_retryAfterSec, 1);
}

TEST(AuthThrottle, AQuietQuarterOfAnHourClearsTheName)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), kFar, At(1000));
	Failed(gate, ibT("Ada"), kFar, At(1001));
	const int later = 1001 + ibAuthThrottle::kFailureWindowSec;
	Failed(gate, ibT("Ada"), kFar, At(later));
	EXPECT_EQ(gate.Begin(ibT("Ada"), kFar, At(later)).m_retryAfterSec, 1);
}

// Nothing was learnt of the password: no failure counted, the attempt given back, the next check let through.
TEST(AuthThrottle, ACancelledCheckCountsForNothing)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), kFar, At(1000));
	ASSERT_TRUE(gate.Begin(ibT("Ada"), kFar, At(1001)).m_allowed);
	gate.Cancel(ibT("Ada"), kFar);
	ASSERT_TRUE(gate.Begin(ibT("Ada"), kFar, At(1001)).m_allowed);
	gate.Fail(ibT("Ada"), kFar, At(1001));
	EXPECT_EQ(gate.Begin(ibT("Ada"), kFar, At(1001)).m_retryAfterSec, 2);

	ibAuthThrottle spent;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts * 3; ++i) {
		ASSERT_TRUE(spent.Begin(ibT("Bob"), kFar, At(1000)).m_allowed);
		spent.Cancel(ibT("Bob"), kFar);
	}
}

// A desktop, or whatever a proxy brings in, is no bucket: its failures cost no other name anything.
TEST(AuthThrottle, ThisMachineIsNoBucket)
{
	for (const ibString& address : { ibString(), ibString(ibT("127.0.0.1")), ibString(ibT("::1")) }) {
		ibAuthThrottle gate;
		for (int i = 0; i < ibAuthThrottle::kAddressAttempts * 2; ++i)
			Failed(gate, ibString::Format(ibT("name-%d"), i), address, At(1000));
		EXPECT_TRUE(gate.Begin(ibT("next"), address, At(1000)).m_allowed) << address;
	}
}

// …and the addresses of this machine are one caller to a name.
TEST(AuthThrottle, ThisMachineIsToldApartByTheNameAlone)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), ibT("127.0.0.1"), At(1000));
	EXPECT_FALSE(gate.Begin(ibT("Ada"), ibT("::1"), At(1000)).m_allowed);
	EXPECT_FALSE(gate.Begin(ibT("Ada"), ibString(), At(1000)).m_allowed);
}

// Names are case-sensitive: folding them here would merge two accounts.
TEST(AuthThrottle, TwoSpellingsAreTwoNames)
{
	ibAuthThrottle gate;
	Failed(gate, ibT("Ada"), kFar, At(1000));
	EXPECT_TRUE(gate.Begin(ibT("ada"), kFar, At(1000)).m_allowed);
}

// A client's start says no name, and many start together: none of them waits for another.
TEST(AuthThrottle, NoNameIsNoGuess)
{
	ibAuthThrottle gate;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts * 3; ++i)
		Failed(gate, ibString(), kFar, At(1000));
}

// All within the second a first failure waits: one still in the table is refused, one that made room is let through.
TEST(AuthThrottle, NamesAndAddressesWithoutEndDoNotGrowWithoutEnd)
{
	const int tracked = static_cast<int>(ibAuthThrottle::kMostTracked);
	const auto step = [](int i) { return At(1000) + std::chrono::microseconds(i); };
	const auto addressOf = [](int i) { return ibString::Format(ibT("10.0.%d.%d"), i / 256, i % 256); };

	ibAuthThrottle gate;
	for (int i = 0; i < tracked + 10; ++i)
		Failed(gate, ibString::Format(ibT("name-%d"), i), addressOf(i), step(i));
	const auto now = step(tracked + 10);
	EXPECT_FALSE(gate.Begin(ibString::Format(ibT("name-%d"), tracked + 9), addressOf(tracked + 9), now).m_allowed);
	EXPECT_FALSE(gate.Begin(ibT("name-10"), addressOf(10), now).m_allowed);
	EXPECT_TRUE(gate.Begin(ibT("name-9"), addressOf(9), now).m_allowed);

	ibAuthThrottle spent;
	for (int i = 0; i < ibAuthThrottle::kAddressAttempts; ++i)
		ASSERT_TRUE(spent.Begin(ibString::Format(ibT("name-%d"), i), kFar, At(1000)).m_allowed);
	EXPECT_FALSE(spent.Begin(ibT("next"), kFar, At(1000)).m_allowed);
	for (int i = 0; i < tracked; ++i)
		ASSERT_TRUE(spent.Begin(ibT("Ada"), addressOf(i), step(i + 1)).m_allowed);
	EXPECT_TRUE(spent.Begin(ibT("next"), kFar, now).m_allowed);
}
