// The floating-point control word travels with the fiber. A fiber that
// changes rounding parks, another fiber on the same thread still sees the
// thread's rounding, and the first fiber still has its own when it resumes.
// MXCSR and the x87 control word on x64, FPCR on arm64, and the OS fiber
// context on Windows.

#include <gtest/gtest.h>

#include "core/fiber/fiber.h"

#include <cfenv>

namespace {

struct Probe {
	int resumed = -1;
	int other = -1;
	bool setFailed = false;
};

void FiberA(void* raw)
{
	Probe* const probe = static_cast<Probe*>(raw);
	if (std::fesetround(FE_UPWARD) != 0) {
		probe->setFailed = true;
		return;
	}
	ibFiber::Current()->SwitchTo(ibFiber::Scheduler());
	probe->resumed = std::fegetround();
}

void FiberB(void* raw)
{
	Probe* const probe = static_cast<Probe*>(raw);
	probe->other = std::fegetround();
}

} // namespace

TEST(FiberFp, RoundingFollowsTheFiberAcrossAPark)
{
	ibFiber::ConvertThread();
	struct Guard {
		ibFiber* a = nullptr;
		ibFiber* b = nullptr;
		~Guard()
		{
			if (a != nullptr)
				ibFiber::Destroy(a);
			if (b != nullptr)
				ibFiber::Destroy(b);
			std::fesetround(FE_TONEAREST);
			ibFiber::ReleaseThread();
		}
	} guard;

	ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);

	Probe probe;
	guard.a = ibFiber::Create(&FiberA, &probe);
	guard.b = ibFiber::Create(&FiberB, &probe);
	ASSERT_NE(guard.a, nullptr);
	ASSERT_NE(guard.b, nullptr);

	ibFiber::Scheduler()->SwitchTo(guard.a);
	EXPECT_FALSE(probe.setFailed);
	EXPECT_FALSE(guard.a->Finished());
	EXPECT_EQ(std::fegetround(), FE_TONEAREST);

	ibFiber::Scheduler()->SwitchTo(guard.b);
	EXPECT_TRUE(guard.b->Finished());
	EXPECT_FALSE(static_cast<bool>(guard.b->TakeException()));
	EXPECT_EQ(probe.other, FE_TONEAREST);

	ibFiber::Scheduler()->SwitchTo(guard.a);
	EXPECT_TRUE(guard.a->Finished());
	EXPECT_FALSE(static_cast<bool>(guard.a->TakeException()));
	EXPECT_EQ(probe.resumed, FE_UPWARD);
	EXPECT_EQ(std::fegetround(), FE_TONEAREST);
}
