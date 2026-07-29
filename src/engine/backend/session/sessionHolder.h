#ifndef __IB_SESSION_HOLDER_H__
#define __IB_SESSION_HOLDER_H__

// ibSessionHolder — the single thread of life of an ibSession.
//
// The registry hands one out when a connection is granted; whoever takes
// it OWNS the session. When the holder dies the session dies with it —
// that is the whole mechanism. There is no second way to end a session
// and no way to forget to end one: the teardown lives in a destructor,
// not in a handler someone has to remember to call.
//
// The owner is always the thing the session exists FOR:
//
//   desktop     ibFrontendMainFrame   (via ibBackendDocFrame)
//   web         ibWebFrame            (same base)
//   compute srv the server-side frame projection driving a thin client
//   headless    ibApplicationData / the host process itself
//
// Because the holder lives on ibBackendDocFrame, all three UI-bearing
// cases get it from one place rather than from three parallel
// implementations.
//
// ---------------------------------------------------------------------
// Veto is NOT this class's business
// ---------------------------------------------------------------------
// "May I close?" is asked BEFORE the holder is dropped — on the frame's
// close gate, which polls the runtime (BeforeExit script, open
// documents) and then the frame's own constraints. A veto means the
// frame is never destroyed, so this destructor is never reached.
//
// Once it IS reached the point of no return is behind us: the holder
// tears the session down unconditionally. Asking "may I?" from a
// destructor is too late to act on the answer, so we do not ask.
//
// ---------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------
// The holder does not know — and does not need to know — which threads
// the session is bound to. Thread bindings live in weak_ptr form
// (s_currentByThread in session.cpp), so dropping the last strong
// reference expires every binding on every thread at once and
// ibSession::Current() starts answering nullptr on all of them. That
// works regardless of which thread the holder happened to die on.
//
// What the holder DOES owe the session is quiescence: a script thread
// holds its session by raw pointer on its own stack, and weak-pointer
// expiry cannot help there. Close() is responsible for cancelling
// in-flight work and waiting for the worker lease before the runtime is
// torn down.

#include "backend/backend.h"

#include <memory>
#include <utility>

class ibSession;

class BACKEND_API ibSessionHolder {
public:
	ibSessionHolder() = default;

	// Registry-facing ctor — takes the strong reference minted by Connect.
	explicit ibSessionHolder(std::shared_ptr<ibSession> session) noexcept
		: m_session(std::move(session)) {
	}

	// Move-only: exactly one owner, always. A copyable holder would mean
	// "the session dies when the LAST copy dies", and a copy forgotten in
	// a worker or a lambda capture would keep a session alive after its
	// form is long gone — the precise failure this class exists to make
	// impossible.
	ibSessionHolder(ibSessionHolder&& other) noexcept
		: m_session(std::move(other.m_session)) {
		other.m_session.reset();
	}

	ibSessionHolder& operator=(ibSessionHolder&& other) noexcept {
		if (this != &other) {
			Reset();
			m_session = std::move(other.m_session);
			other.m_session.reset();
		}
		return *this;
	}

	ibSessionHolder(const ibSessionHolder&)            = delete;
	ibSessionHolder& operator=(const ibSessionHolder&) = delete;

	~ibSessionHolder() { Reset(); }

	// Observers. Raw — everyone except the owner watches the session
	// without extending its life.
	ibSession* Get()        const { return m_session.get(); }

	ibSession* operator->() const { return m_session.get(); }
	explicit operator bool() const { return m_session != nullptr; }

	// Hand ownership on without tearing anything down. Used when the
	// session is created before its owner exists (authenticate first,
	// build the frame second) and the stack-local holder passes the
	// thread of life to the frame.
	std::shared_ptr<ibSession> Release() noexcept {
		return std::move(m_session);
	}

	// Explicit end-of-life, same as letting the holder die. Out of line
	// (sessionHolder.cpp) — ibSession must be complete to be closed, and
	// keeping this header free of session.h lets frame headers include it
	// cheaply.
	void Reset();

private:
	std::shared_ptr<ibSession> m_session;
};

// ---------------------------------------------------------------------
// ibSessionWatch — the other half of the pair: look, don't own.
//
// Everything that is not the owner watches: the registry's index, a
// session's own back-links, worker tasks, the debugger's parked queue,
// script objects that remember which session created them. None of them
// may keep a session alive — if they could, "the form died so the
// session died" would stop being true the moment anyone forgot a copy.
//
// Watching answers honestly after the owner is gone: Expired() goes
// true, Share() comes back empty. That is the point — a stale watch is a
// question with a clear answer, whereas a stale raw pointer is a crash.
// ---------------------------------------------------------------------
class BACKEND_API ibSessionWatch {
public:
	ibSessionWatch() = default;

	// Watch what somebody else owns.
	explicit ibSessionWatch(const ibSessionHolder& holder);
	explicit ibSessionWatch(std::shared_ptr<ibSession> session) noexcept
		: m_session(std::move(session)) {
	}

	// Watch a session reached as a raw pointer (the common case in code
	// that was handed an ibSession* and wants to remember it safely).
	// Out of line — needs shared_from_this on a complete ibSession.
	explicit ibSessionWatch(ibSession* session);

	bool Expired() const { return m_session.expired(); }
	void Reset()         { m_session.reset(); }

	// Hold the session still for as long as you need it. One step, and
	// the result is a plain shared_ptr — no bespoke lease type to learn:
	//
	//   auto s = watch.Share();
	//   if (!s) return;              // owner already let go
	//   s->SetActivity(...);
	//
	// It is a HOLD, not ownership: it cannot resurrect a session whose
	// owner is gone (comes back empty), and keeping one in a member field
	// would quietly make you a second owner. Stack scope only.
	std::shared_ptr<ibSession> Share() const { return m_session.lock(); }

	// Deliberately NO operator-> : it reads like a pointer but takes a
	// fresh hold on every use, so the natural-looking
	//
	//     if (watch && watch->Inserted())      // TWO separate holds
	//
	// has a window between the two where the owner can let go — and the
	// second hold then dereferences nothing. Share() once and work
	// through that, and the race cannot be written.
	//
	// "Is there still a session there?" — for tests that need no access.
	explicit operator bool() const { return !m_session.expired(); }

private:
	std::weak_ptr<ibSession> m_session;
};

#endif
