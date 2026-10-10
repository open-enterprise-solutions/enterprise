#ifndef __IB_FIBER_H__
#define __IB_FIBER_H__

// A stackful fiber pinned to the OS thread that created it.
//
// SwitchTo returns on this fiber when somebody switches back. Destroy
// only after the entry function has returned — the stack is still full
// of live C++ objects until then, and freeing it would skip their
// destructors. An exception that escapes the entry is stored on the
// fiber and does not cross the switch.

#include "core/core.h"
#include "core/fiber/fiberLocals.h"

#include <cstddef>
#include <exception>

#if !defined(_WIN32)
// The asm trampoline calls this. It has to reach RunEntry, which stays
// private so a caller cannot run a fiber's body out from under the switch.
extern "C" void ibFiberEnter();
#endif

class CORE_API ibFiber {
public:
	using Entry = void (*)(void* arg);

	// One megabyte, the same reserve a worker thread has on Windows
	// (the MSVC default). There is no interpreter recursion limit, so a
	// fiber has to offer what the thread it replaces offered. The pages
	// commit on touch; a parked question pays for what the script used,
	// not for the whole reserve.
	//
	// Win32 x86 (the shipping build) has a 2 GB user address space, 4 GB
	// under WOW64 only when the exe is linked /LARGEADDRESSAWARE — this
	// tree does not pass that flag. After the image, the heap and wx,
	// roughly a gigabyte is left, which is about a thousand fibers at
	// this reserve. x64 and arm64 are not address-space bound.
	static constexpr std::size_t kStackReserve = 1024u * 1024u;

	// The calling thread becomes its own scheduler fiber (the thread
	// stack). Idempotent on a thread that already converted.
	static void ConvertThread();
	static void ReleaseThread();

	static ibFiber* Current();
	static ibFiber* Scheduler();

	static ibFiber* Create(Entry entry, void* arg, std::size_t reserveBytes = kStackReserve);
	static void Destroy(ibFiber* fiber);

	bool Finished() const { return m_finished; }
	std::exception_ptr TakeException() { return std::move(m_exception); }
	bool IsScheduler() const { return m_scheduler; }

	// Catch handlers and mutexes held across script, counted on this
	// fiber. Both belong to the OS thread; Await refuses to park while
	// either count is open. Read by the pool, written by the scopes below.
	int HandlerDepth() const noexcept { return m_handlerDepth; }
	int LockDepth() const noexcept { return m_lockDepth; }

	// Save this fiber's locals, install the target's, switch stacks.
	void SwitchTo(ibFiber* target);

private:
	friend class ibFiberHandlerScope;
	friend class ibFiberLockScope;

	ibFiber() = default;

	void EnterHandler() noexcept { ++m_handlerDepth; }
	void LeaveHandler() noexcept { --m_handlerDepth; }
	void EnterLock() noexcept { ++m_lockDepth; }
	void LeaveLock() noexcept { --m_lockDepth; }

	Entry m_entry = nullptr;
	void* m_arg = nullptr;
	void* m_sp = nullptr;
	void* m_stackAlloc = nullptr;
	std::size_t m_stackAllocSize = 0;
	void* m_stackBottom = nullptr;
	std::size_t m_stackSize = 0;
#if defined(_WIN32)
	void* m_osFiber = nullptr;
#endif
	bool m_scheduler = false;
	bool m_finished = false;
	int m_handlerDepth = 0;
	int m_lockDepth = 0;
	std::exception_ptr m_exception;
	void* m_asanFake = nullptr;
	// TSan's context for this fiber. The scheduler holds the thread's own
	// context and must not destroy it; every other fiber owns the one
	// Create made. Null when the build is not instrumented.
	void* m_tsanFiber = nullptr;
	ibFiberLocals::Snapshot m_locals;

	void RunEntry();
#if !defined(_WIN32)
	friend void ibFiberEnter();
#endif
#if !defined(_WIN32)
	void InitPosixStack();
#endif
#if defined(_WIN32)
	// A Win32 fiber procedure (`VOID CALLBACK (LPVOID)`). CALLBACK is __stdcall; spelled out so this header needs
	// no <windows.h>.
	static void __stdcall FiberProc(void* arg);
#endif
};

// Open while a C++ catch handler runs on this fiber. The caught exception
// lives in the thread's exception state; parking would hand that state to
// the next fiber on the thread.
class CORE_API ibFiberHandlerScope {
public:
	ibFiberHandlerScope();
	~ibFiberHandlerScope();
	ibFiberHandlerScope(const ibFiberHandlerScope&) = delete;
	ibFiberHandlerScope& operator=(const ibFiberHandlerScope&) = delete;
private:
	ibFiber* m_fiber = nullptr;
};

// Open while a mutex is held across script on this fiber. A std::mutex
// held across a park deadlocks the thread (the parked fiber is the one
// that would unlock, and the owner is the thread). A recursive section
// lets the next fiber enter and the exclusion is gone.
class CORE_API ibFiberLockScope {
public:
	ibFiberLockScope();
	~ibFiberLockScope();
	ibFiberLockScope(const ibFiberLockScope&) = delete;
	ibFiberLockScope& operator=(const ibFiberLockScope&) = delete;
private:
	ibFiber* m_fiber = nullptr;
};

// Locks `mutex` and counts it on the fiber for the same span.
template<class Mutex>
class ibFiberMutexLock {
public:
	explicit ibFiberMutexLock(Mutex& mutex) : m_mutex(mutex)
	{
		m_mutex.lock();
		m_locked = true;
	}
	~ibFiberMutexLock()
	{
		if (m_locked)
			m_mutex.unlock();
	}
	ibFiberMutexLock(const ibFiberMutexLock&) = delete;
	ibFiberMutexLock& operator=(const ibFiberMutexLock&) = delete;
private:
	Mutex&           m_mutex;
	bool             m_locked = false;
	ibFiberLockScope m_account;
};

#endif
