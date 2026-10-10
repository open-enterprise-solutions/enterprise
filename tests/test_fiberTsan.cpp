// A switch is a happens-before. The assembly is invisible to ThreadSanitizer,
// so the annotations in ibFiber::SwitchTo are what keep the write on one
// fiber and the read on the other from being reported as a race. Without a
// sanitizer the same assertions are the value that crossed the switch.

#include <gtest/gtest.h>

#include "core/fiber/fiber.h"

namespace {

struct Shared {
	int value = 0;
};

void Child(void* arg)
{
	Shared* const shared = static_cast<Shared*>(arg);
	if (shared->value != 1)
		shared->value = -1;
	else
		shared->value = 2;
}

struct Call {
	ibFiber::Entry entry = nullptr;
	void* arg = nullptr;
};

void Entry(void* p)
{
	Call* const call = static_cast<Call*>(p);
	call->entry(call->arg);
}

} // namespace

TEST(FiberTsan, ASwitchPublishesTheWriteBeforeTheRead)
{
	ibFiber::ConvertThread();
	struct Guard {
		ibFiber* fiber = nullptr;
		~Guard()
		{
			if (fiber != nullptr)
				ibFiber::Destroy(fiber);
			ibFiber::ReleaseThread();
		}
	} guard;

	Shared shared;
	shared.value = 1;
	Call call{&Child, &shared};
	guard.fiber = ibFiber::Create(&Entry, &call);
	ibFiber::Scheduler()->SwitchTo(guard.fiber);

	EXPECT_TRUE(guard.fiber->Finished());
	EXPECT_FALSE(static_cast<bool>(guard.fiber->TakeException()));
	EXPECT_EQ(shared.value, 2);
}
