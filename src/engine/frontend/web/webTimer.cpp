#include "webTimer.h"

#include <algorithm>

#include "webApplication.h"

ibWebTimer::ibWebTimer() = default;

ibWebTimer::~ibWebTimer()
{
	Stop();
}

bool ibWebTimer::Start(int milliseconds, bool oneShot)
{
	m_app      = currentWebApp();  // pin session at Start; null if no session thread
	m_interval = std::chrono::milliseconds(milliseconds);
	m_oneShot  = oneShot;
	m_stop.store(false);
	m_thread   = std::thread(&ibWebTimer::TickLoop, this);
	return true;
}

void ibWebTimer::Stop()
{
	m_stop.store(true);
	if (m_thread.joinable())
		m_thread.join();
}

void ibWebTimer::TickLoop()
{
	// Split the interval into ~200ms chunks so Stop() returns within
	// one chunk even during a long sleep.
	const auto chunk = std::chrono::milliseconds(200);
	while (!m_stop.load()) {
		auto remaining = m_interval;
		while (remaining > std::chrono::milliseconds::zero()) {
			if (m_stop.load()) return;
			const auto step = std::min(remaining, chunk);
			std::this_thread::sleep_for(step);
			remaining -= step;
		}
		if (m_stop.load()) return;

		// Build a wxTimerEvent from the internal wxTimer (that's the
		// only public ctor), then re-point its event object to `this`
		// so subscribers see the ibWebTimer* — same shape as desktop
		// where wxTimer IS the event object.
		auto* event = new wxTimerEvent(m_wxTimer);
		event->SetEventObject(this);
		QueueEvent(event);  // post to OUR OWN handler queue

		// Wake the session worker and ask it to drain OUR pending
		// events. The worker is the sole script-runner (ibProcUnit is
		// thread-affine); posting+drain from any other thread would
		// corrupt procUnit state.
		// The handler ran script that may have changed the form, and
		// nothing else on this road bumps the sequence, so the task ends
		// the way a dispatch does. The drain in that tail may destroy the
		// form the timer belongs to, and the timer with it: nothing here
		// touches `self` after the handler returns.
		if (m_app != nullptr) {
			ibWebTimer* self = this;
			ibWebApplication* app = m_app;
			m_app->PostWork([self, app]{
				self->ProcessPendingEvents();
				app->SettleAfterScript();
			});
		}

		if (m_oneShot) return;
	}
}
