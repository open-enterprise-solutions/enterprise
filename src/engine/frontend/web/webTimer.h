#ifndef __WEB_TIMER_H__
#define __WEB_TIMER_H__

// Universal interval timer for the web runtime — API-compatible with
// wxTimer so ibValueForm's idle-handler machinery uses one code path
// on both builds (see ibFrontendTimer typedef in frontendTypes.h).
//
// Pure transport: ticks at the requested interval and posts a
// wxTimerEvent to ITS OWN wxEvtHandler queue. Subscribers Bind
// wxEVT_TIMER on the timer instance and receive the event exactly
// like they would from a native wxTimer; event.GetEventObject()
// returns the ibWebTimer* (not the internal wxTimer held for the
// wxTimerEvent ctor), so `map<procName, ibFrontendTimer*>` lookups
// match without any web-specific dispatcher layer.
//
// Tick source is std::thread, not wxTimer native: wfrontend.dll has
// no wxApp / message pump, so native wxTimer (which on MSW needs
// WM_TIMER) never fires. std::thread + sleep_for is portable and
// doesn't depend on wxApp. The thread only produces the
// wxTimerEvent; it never runs script code — ProcessPendingEvents
// happens on the session worker thread via app->PostWork.
//
// Firing flow:
//   tick thread sleeps interval → wakes →
//   QueueEvent(new wxTimerEvent{event_object=this}) on self.
//   app->PostWork([timer]{ timer->ProcessPendingEvents(); }).
//   ↓
//   worker thread drains: calls ProcessPendingEvents on the timer
//   handler, which dispatches to whatever bound wxEVT_TIMER on this
//   instance (typically ibValueForm::OnIdleHandler), then ends the
//   task as a dispatch does (ibWebApplication::SettleAfterScript), so
//   a change the handler made reaches the stream under a new sequence.

#include <atomic>
#include <chrono>
#include <thread>

#include <wx/event.h>
#include <wx/timer.h>   // wxTimerEvent dispatch type + internal wxTimer

class ibWebApplication;

class ibWebTimer : public wxEvtHandler {
public:
	// Default-ctor matches wxTimer's. App pointer resolved lazily from
	// the session's thread_local context on first Start — so the
	// ibValueForm::AttachIdleHandler body can `new ibWebTimer()` with
	// the same shape as `new wxTimer()` on desktop.
	ibWebTimer();
	virtual ~ibWebTimer() override;

	// Same signature as wxTimer::Start — milliseconds + oneShot.
	// interval in ms; oneShot == wxTIMER_ONE_SHOT (true) stops after
	// one fire, wxTIMER_CONTINUOUS (false) fires repeatedly.
	bool Start(int milliseconds, bool oneShot = false);
	void Stop();
	bool IsRunning() const { return !m_stop.load() && m_thread.joinable(); }

private:
	void TickLoop();

	ibWebApplication*         m_app = nullptr;  // resolved in Start

	std::chrono::milliseconds m_interval { 0 };
	bool                      m_oneShot = false;

	// Held only to satisfy wxTimerEvent's `(wxTimer&)` ctor — the
	// event's source is reassigned via SetEventObject(this) so
	// subscribers see the ibWebTimer* directly.
	wxTimer                   m_wxTimer;

	std::atomic<bool>         m_stop { false };
	std::thread               m_thread;
};

#endif
