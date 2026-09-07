#ifndef __WEB_APPLICATION_H__
#define __WEB_APPLICATION_H__

// Per-session web "application" — analogue of wxTheApp for one web user.
//
// Owns everything that must be isolated between concurrent sessions:
// the session-scoped module manager (with its own compiled bytecode,
// ibProcUnit, common modules and context variables) and — in future
// steps — the logical main frame with open documents.
//
// Lifecycle mirrors wxApp: OnInit() stands the per-session runtime up
// (think BeforeRun + RunDatabase in desktop mode), OnExit() tears it
// down (Close + DestroyMainModule). One ibWebApplication lives inside
// each ibWebSession and is destroyed with it.
//
// Inherits wxEvtHandler so scripts, timers (AttachIdleHandler) and any
// future per-session dispatch can Bind/Unbind/ProcessEvent through the
// app object the same way wxTheApp is used on desktop — wfrontend.dll
// has no real wxApp loop, but ibWebSession::Tick (HTTP-tick from the
// handler thread) will route events into this handler.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>

#include <wx/event.h>

// The live-update signal, held by shared_ptr because a waiter outlives the
// thing that wakes it. An SSE subscriber blocks here for up to 25 seconds
// while its browser is idle, and the session behind it can be swept in that
// window -- destroying the application whose mutex and condition variable the
// waiter is parked on. Owning the signal separately means the waiter's own
// reference keeps it alive, and Close() tells it the reason it is being woken
// is that nothing more is coming.
class ibWebLiveSignal {
public:
	// A state change happened: wake everyone parked here.
	void Bump()
	{
		m_seq.fetch_add(1, std::memory_order_acq_rel);
		std::lock_guard<std::mutex> lk(m_mutex);
		m_cv.notify_all();
	}

	// The application is going away. Waiters return at once and every later
	// wait answers immediately, so a subscriber cannot park on a session that
	// no longer exists.
	void Close()
	{
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			m_closed = true;
		}
		m_cv.notify_all();
	}

	bool IsClosed() const
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_closed;
	}

	uint64_t Current() const { return m_seq.load(); }

	// Block until the sequence moves past lastSeen, the signal closes, or the
	// timeout runs out. A timeout of zero or less waits without one.
	uint64_t Wait(uint64_t lastSeen, int timeoutMs)
	{
		std::unique_lock<std::mutex> lk(m_mutex);
		const auto ready = [this, lastSeen] {
			return m_closed || m_seq.load() != lastSeen;
		};
		if (timeoutMs <= 0)
			m_cv.wait(lk, ready);
		else
			m_cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), ready);
		return m_seq.load();
	}

private:
	std::atomic<uint64_t>   m_seq { 1 };
	mutable std::mutex      m_mutex;
	std::condition_variable m_cv;
	bool                    m_closed = false;
};

// value_ptr.h only forward-declares ibValue (via backend_core.h); we need
// the complete ibValue base for ibValuePtr<T> instantiation below.
#include "backend/compiler/value.h"
#include "backend/value_ptr.h"
#include "backend/session/sessionHolder.h"

class ibValueModuleManagerRuntimeConfiguration;
class ibWebFrame;
class ibSession;
class ibWebClientSession;

class ibWebApplication : public wxEvtHandler {
public:
	ibWebApplication();
	virtual ~ibWebApplication();

	// Startup. Runs AFTER user authentication succeeded: build the web
	// frame first (scripts fired from CreateMainModule — OnStart
	// handlers, load-page code, early OpenForm calls — already need a
	// frame to attach to), then compile + execute the session's main
	// module.
	//
	// The holder passes straight through into the frame's constructor:
	// the web main window owns its session exactly as the desktop one
	// does. This object only borrows it.
	virtual bool OnInit(ibSessionHolder&& holder);
	virtual void OnExit();

	// Out-of-line: the ibValuePtr<T> → T* conversion static_casts via
	// ibValue*, which requires T complete. Keeping the body in
	// webApplication.cpp (with moduleManager.h already included there)
	// lets TUs that only need the class identity (formObject.cpp,
	// webTimer.cpp) stop at a forward decl.
	ibValueModuleManagerRuntimeConfiguration* GetManagerModule() const;
	ibWebFrame*                        GetFrame()         const { return m_frame; }

	// Session context for this application. Not stored — read out of the
	// frame, which is the thing that actually owns the session. Keeping a
	// second pointer here would be the same fact written twice, and the
	// two would have to be kept in step through teardown.
	ibSession* GetSessionContext() const;   // == m_frame->Session()

	// Active tab's visual host, or nullptr if no tab is open. The HTTP
	// layer uses this to serialise the current tree into the response
	// body after a Dispatch*.
	class ibVisualHostClient* GetActiveHost() const;

	// Live-update sequence counter. Every state-changing action
	// (Dispatch, timer tick, tab switch/close, CreateAndUpdateVisualHost
	// from script) calls MarkDirty() which bumps the live signal and wakes
	// every waiter on it. SSE subscribers call WaitForChange(lastSeen,
	// timeoutMs) — block until seq advances past lastSeen OR timeout,
	// return the current seq. Multiple subscribers on one session (e.g.
	// multiple browser tabs of the same user) each track their own
	// lastSeen, so notify_all wakes them all without any waiter "eating"
	// an update for the others. Sequence starts at 1 so first WaitForChange
	// with lastSeen=0 returns immediately — new SSE clients get current
	// state without waiting for the next event.
	void     MarkDirty();
	uint64_t CurrentSeq() const { return m_live->Current(); }
	uint64_t WaitForChange(uint64_t lastSeen, int timeoutMs);

	// The signal itself, for a caller that must wait outside the lock that
	// found this application -- see ibWebLiveSignal.
	std::shared_ptr<ibWebLiveSignal> LiveSignal() const { return m_live; }

	// Session dispatcher — single generic entry. HTTP handlers hand off
	// here: given a controlId + kind ("click" | "text" | "toggle" | …)
	// + value, the dispatcher resolves the ibValueFrame in the active
	// tab's form, fetches its paired ibWebWindow from the host's
	// (frame → wxObject) map, and calls HandleRequest(kind, value) on
	// it — polymorphic, no subclass-specific branches here. Each
	// ibWebXxx handles the kinds it understands. Drains on this
	// thread, rebuilds the visual tree. See docs/web/event-dispatcher.md.
	bool Dispatch(int controlId, const wxString& kind, const wxString& value);

	// Legacy kind-specific entry points. Thin shims over Dispatch —
	// kept so existing HTTP routes (/action, /change, /toggle) stay
	// wired without the wfrontend shims knowing the new unified
	// endpoint exists. Callers choose whichever fits their body shape.
	bool DispatchControlAction(int controlId)                      { return Dispatch(controlId, wxT("click"), wxString());                      }
	bool DispatchTextChange(int controlId, const wxString& value)  { return Dispatch(controlId, wxT("text"),  value);                           }
	bool DispatchToggle(int controlId, bool checked)               { return Dispatch(controlId, wxT("toggle"), checked ? wxT("1") : wxT("0")); }

	// One page of a tablebox's rows. Not a Dispatch kind: Dispatch
	// answers "did the control take it" and returns the whole form
	// again, and rows are neither — they are a page, asked for
	// repeatedly as the user scrolls, and they carry no side effect on
	// the form at all. Returns the page JSON as its own document.
	// `dir` is "first" | "next" | "prev".
	std::string FetchRows(int controlId, const wxString& dir, int count);

	// Run a command off the form's own command bar. Not a Dispatch kind:
	// the bar is chrome, not a control, so it has no entry in the
	// (frame -> wxObject) map and FindControlByID would never reach it.
	// The action id names the command; the form is asked for its bar.
	bool DispatchCommand(int actionId);

	// Session task dispatch — forwards to the process-wide ibWorkerPool
	// (appData->GetWorkerPool()), which preserves per-session FIFO +
	// lease semantics across all concurrent web sessions sharing the
	// pool's fixed worker thread count. Replaces the per-session worker
	// thread that used to live on this object.
	//
	//   PostWork          — fire-and-forget; used by timer ticks and
	//                       internal cleanup paths.
	//   RunOnWorker<T>(f) — submits and returns a future for the
	//                       result; HTTP handlers .get() it to
	//                       synchronously return the JSON response.
	//
	// Both eventually call ibWorkerPool::Submit(session, ...).
	// Reentrant submit on the same session runs inline (the pool worker
	// already holds this session's lease). Submission on a stopped pool
	// or with no session context is a no-op for PostWork; RunOnWorker
	// returns a future that fulfils with an exception in that case.
	void PostWork(std::function<void()> fn);

	template <class Fn>
	auto RunOnWorker(Fn&& fn) -> std::future<decltype(fn())> {
		using R = decltype(fn());
		auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
		auto fut  = task->get_future();
		PostWork([task]{ (*task)(); });
		return fut;
	}

private:
	// m_moduleManager owns its refcount via ibValuePtr — assigning a raw
	// pointer IncrRef's implicitly, assigning nullptr (or destruction)
	// DecrRef's. Keeps OnInit's failure branch and OnExit's teardown
	// leak-free without a matching IncrRef/DecrRef pair to track.
	ibWebFrame*                                   m_frame         = nullptr;
	// m_moduleManager field removed — moduleManager is shared process-
	// wide and lives on metadata; GetManagerModule() pulls from there.
	bool                                          m_initialized   = false;

	// Live-update state. See MarkDirty/WaitForChange docs above.
	std::shared_ptr<ibWebLiveSignal>              m_live
		= std::make_shared<ibWebLiveSignal>();
};

// Reach the current session's ibWebApplication from arbitrary web
// script context (ibValueForm, ibValueControl, …). Goes through the
// thread_local main-frame singleton that the backend already uses;
// returns null if we're not inside a session (e.g. module-loading
// before OnInit). Defined in webApplication.cpp.
ibWebApplication* currentWebApp();

#endif
