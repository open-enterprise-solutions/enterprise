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

	// 64-bit keeps the thread reserve (Common.props, StackReserveSize, 8 MB).
	// MAX_REC_COUNT is 200 and ibProcUnit::Execute keeps about 9.4 KB per
	// interpreted level, so that reserve is what lets the count be the
	// limit a script sees. The pages commit on touch: a parked question
	// pays for the stack the script used, not for the reserve.
	//
	// A 32-bit process has about a gigabyte of user address space, and
	// every lease takes a fiber, so the reserve there is the 2 MB the
	// count needs. StackLow is what makes the smaller reserve safe: a
	// frame that would not fit is refused before the guard page. Placing
	// a fiber that does not fit throws bad_alloc; the pool refuses the
	// task instead of claiming the same queue again.
	static constexpr std::size_t kStackReserve =
		sizeof(void*) >= 8 ? (8u * 1024u * 1024u) : (2u * 1024u * 1024u);

	// Left below the pointer so the refusal itself still fits. The depth
	// counters stay the limit a script sees; this fires only when the
	// native stack would run out first.
	static constexpr std::size_t kRecursionSlack = 128u * 1024u;

	// StackRemaining answers this when it cannot see the bounds. Not empty.
	static constexpr std::size_t kUnknownStack = static_cast<std::size_t>(-1);

	// The calling thread becomes its own scheduler fiber (the thread
	// stack). Idempotent on a thread that already converted.
	static void ConvertThread();
	static void ReleaseThread();

	static ibFiber* Current();
	static ibFiber* Scheduler();

	// Bytes still unused below the stack pointer: this fiber's reserve
	// when one is current, otherwise the OS thread's stack.
	static std::size_t StackRemaining() noexcept;
	static bool StackLow() noexcept
	{
		const std::size_t left = StackRemaining();
		return left != kUnknownStack && left < kRecursionSlack;
	}

	static ibFiber* Create(Entry entry, void* arg, std::size_t reserveBytes = kStackReserve);

	// The next Create throws std::bad_alloc and clears the request.
	// A test uses it to prove a lease that cannot be placed refuses
	// the queued task instead of trying the same queue again.
	static void FailNextCreate();
	static void Destroy(ibFiber* fiber);

	bool Finished() const { return m_finished; }
	std::exception_ptr TakeException() { return std::move(m_exception); }
	bool IsScheduler() const { return m_scheduler; }

	// Save this fiber's locals, install the target's, switch stacks.
	void SwitchTo(ibFiber* target);

private:
	ibFiber() = default;

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

#endif
