#include "workerPoolGUI.h"

#include "backend/session/session.h"

#include <wx/app.h>
#include <wx/thread.h>   // wxIsMainThread — the inline-vs-hop decision

#include <stdexcept>

std::future<void> ibWorkerPoolGUI::Submit(ibSession* /*session*/, Task task)
{
	auto promise = std::make_shared<std::promise<void>>();
	auto future  = promise->get_future();

	if (m_stop.load(std::memory_order_acquire)) {
		promise->set_exception(std::make_exception_ptr(
			std::runtime_error("worker pool is stopped")));
		return future;
	}

	// No wx loop yet (host startup, or headless run mode misconfigured) —
	// run the task inline so the future fulfils immediately. Avoids
	// dropping work on the floor in transitional states.
	if (wxTheApp == nullptr) {
		try {
			task();
			promise->set_value();
		}
		catch (...) {
			promise->set_exception(std::current_exception());
		}
		return future;
	}

	// ALREADY on the thread this pool drains onto — run it here and now.
	//
	// This is what makes the pool installable on an interactive session without
	// changing a single thing about how the desktop behaves today: every script
	// path, every form handler, every Submit that exists right now runs on the wx
	// main thread, so every one of them takes this branch and stays inline,
	// exactly as it did when GetWorkerPool() answered nullptr. The CallAfter
	// below then serves ONLY the case that has no equivalent today — a result
	// arriving from a background thread — which is the whole reason to install it.
	//
	// It is also a correctness rule, not just a shortcut: a task submitted from
	// the main thread and deferred would fulfil its future only on the next idle
	// pump, so a caller that waits on that future (RunOnSession, the teardown
	// drain) would block the very loop that has to run it — a self-deadlock.
	// Reentrant-inline is the same contract the headless pool keeps for a caller
	// that already holds the session's lease.
	if (wxIsMainThread()) {
		try {
			task();
			promise->set_value();
		}
		catch (...) {
			promise->set_exception(std::current_exception());
		}
		return future;
	}

	// Off-thread caller — hop. CallAfter posts a wx event that the next idle
	// pump executes; safe to call from any thread.
	wxTheApp->CallAfter([t = std::move(task), p = promise]() mutable {
		try {
			t();
			p->set_value();
		}
		catch (...) {
			p->set_exception(std::current_exception());
		}
	});

	return future;
}

void ibWorkerPoolGUI::DropSession(ibSession* /*session*/)
{
	// No per-session bookkeeping — tasks share the wx event queue,
	// which has no per-session partition. Nothing to drop.
}

void ibWorkerPoolGUI::CancelSession(ibSession* session)
{
	// Same cooperative-cancel contract as the headless pool: set the
	// flag, the interpreter sees it on the next opcode and unwinds.
	if (session != nullptr)
		session->RequestCancel();
}

void ibWorkerPoolGUI::Stop()
{
	// Flag-only stop. wx handles pending CallAfter events through its
	// own shutdown path — no separate join because there are no OS
	// threads owned by this pool.
	m_stop.store(true, std::memory_order_release);
}
