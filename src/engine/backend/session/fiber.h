#ifndef __IB_FIBER_H__
#define __IB_FIBER_H__

// A stackful fiber pinned to the OS thread that created it.
//
// SwitchTo returns on this fiber when somebody switches back. Destroy
// only after the entry function has returned — the stack is still full
// of live C++ objects until then, and freeing it would skip their
// destructors. An exception that escapes the entry is stored on the
// fiber and does not cross the switch.

#include "fiberLocals.h"

#include <cstddef>
#include <exception>

#if !defined(_WIN32)
// The asm trampoline calls this. It has to reach RunEntry, which stays
// private so a caller cannot run a fiber's body out from under the switch.
extern "C" void ibFiberEnter();
#endif

class ibFiber {
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
	ibFiberLocals::Snapshot m_locals;

	void RunEntry();
#if !defined(_WIN32)
	friend void ibFiberEnter();
#endif
#if !defined(_WIN32)
	void InitPosixStack();
#endif
#if defined(_WIN32)
	static void CALLBACK WinMain(void* arg);
#endif
};

#endif
