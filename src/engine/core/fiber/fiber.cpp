#include "core/fiber/fiber.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
// CreateFiberEx / ConvertThreadToFiberEx are Vista. The core project
// does not set _WIN32_WINNT itself; wx does, but only after its own headers.
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0600
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#endif

#if defined(__SANITIZE_ADDRESS__)
#  define IB_FIBER_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define IB_FIBER_ASAN 1
#  endif
#endif

#if defined(IB_FIBER_ASAN)
extern "C" {
void __sanitizer_start_switch_fiber(void** fake_stack_save, const void* bottom, std::size_t size);
void __sanitizer_finish_switch_fiber(void* fake_stack_save, const void** bottom_old, std::size_t* size_old);
}
#endif

#if defined(__SANITIZE_THREAD__)
#  define IB_FIBER_TSAN 1
#elif defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define IB_FIBER_TSAN 1
#  endif
#endif

#if defined(IB_FIBER_TSAN)
// The assembly switch is invisible to TSan. These are what tell it a
// fiber exists and which one is running. <sanitizer/tsan_interface.h>
#  include <sanitizer/tsan_interface.h>
#endif

#if !defined(_WIN32)
extern "C" void ibFiberSwitch(void** fromSp, void** toSp);
extern "C" void ibFiberTrampoline();
#endif

namespace {

thread_local ibFiber* tl_currentFiber = nullptr;
thread_local ibFiber* tl_schedulerFiber = nullptr;

} // namespace

#if !defined(_WIN32)

// Reachable from the asm trampoline. `used` keeps it alive if this
// translation unit is linked with LTO — the reference lives in the .S.
extern "C" __attribute__((used)) void ibFiberEnter()
{
#  if defined(IB_FIBER_ASAN)
	__sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr);
#  endif
	ibFiber* const self = tl_currentFiber;
	if (self == nullptr)
		std::abort();
	self->RunEntry();
	self->SwitchTo(tl_schedulerFiber);
	std::abort();
}

#endif

void ibFiber::RunEntry()
{
	try {
		if (m_entry != nullptr)
			m_entry(m_arg);
	}
	catch (...) {
		m_exception = std::current_exception();
	}
	m_finished = true;
}

#if !defined(_WIN32)

void ibFiber::InitPosixStack()
{
	const std::uintptr_t top =
		(reinterpret_cast<std::uintptr_t>(m_stackBottom) + m_stackSize) & ~(std::uintptr_t)15;
#if defined(__x86_64__)
	// Matches ibFiberSwitch: mxcsr+fcw, r15, r14, r13, r12, rbx, rbp,
	// then the return address. sp % 16 == 0, and the ret in the switcher
	// lands on the trampoline with the alignment a call requires.
	unsigned char* sp = reinterpret_cast<unsigned char*>(top - 64);
	std::memset(sp, 0, 64);
	*reinterpret_cast<std::uint32_t*>(sp) = 0x1F80;       // default MXCSR
	*reinterpret_cast<std::uint16_t*>(sp + 4) = 0x037F;   // default x87 CW
	*reinterpret_cast<std::uint64_t*>(sp + 56) =
		reinterpret_cast<std::uint64_t>(&ibFiberTrampoline);
	m_sp = sp;
#elif defined(__aarch64__)
	// Ten 16-byte pairs: d8-d15, then x19-x30. x30 (the link register)
	// is the trampoline. sp stays 16-byte aligned, which is what a call
	// on the trampoline requires.
	unsigned char* sp = reinterpret_cast<unsigned char*>(top - 160);
	std::memset(sp, 0, 160);
	*reinterpret_cast<std::uint64_t*>(sp + 152) =
		reinterpret_cast<std::uint64_t>(&ibFiberTrampoline);
	m_sp = sp;
#else
#  error ibFiber has no stack image for this architecture
#endif
}

#endif

void ibFiber::ConvertThread()
{
	if (tl_schedulerFiber != nullptr)
		return;
	ibFiber* self = new ibFiber();
	self->m_scheduler = true;
	self->m_locals = ibFiberLocals::ForScheduler();
#if defined(_WIN32)
	void* handle = ConvertThreadToFiberEx(self, FIBER_FLAG_FLOAT_SWITCH);
	if (handle == nullptr) {
		if (GetLastError() == ERROR_ALREADY_FIBER)
			handle = GetCurrentFiber();
		else {
			delete self;
			throw std::runtime_error("ConvertThreadToFiberEx failed");
		}
	}
	self->m_osFiber = handle;
#endif
#if defined(IB_FIBER_TSAN)
	// The thread is already a fiber to TSan. The scheduler keeps that
	// context, so the first switch leaves it.
	self->m_tsanFiber = __tsan_get_current_fiber();
	__tsan_set_fiber_name(self->m_tsanFiber, "scheduler");
#endif
	tl_schedulerFiber = self;
	tl_currentFiber = self;
}

void ibFiber::ReleaseThread()
{
	ibFiber* self = tl_schedulerFiber;
	if (self == nullptr)
		return;
	tl_schedulerFiber = nullptr;
	tl_currentFiber = nullptr;
#if defined(_WIN32)
	ConvertFiberToThread();
	self->m_osFiber = nullptr;
#endif
	// The scheduler's TSan context belongs to the thread. Destroying it
	// here would retire the thread out from under the sanitizer.
	delete self;
}

ibFiber* ibFiber::Current() { return tl_currentFiber; }
ibFiber* ibFiber::Scheduler() { return tl_schedulerFiber; }

ibFiber* ibFiber::Create(Entry entry, void* arg, std::size_t reserveBytes)
{
	if (reserveBytes == 0)
		reserveBytes = kStackReserve;
	ibFiber* fiber = new ibFiber();
	fiber->m_entry = entry;
	fiber->m_arg = arg;
	try {
		fiber->m_locals = ibFiberLocals::ForFiber();
#if defined(_WIN32)
		fiber->m_stackSize = reserveBytes;
		void* handle = CreateFiberEx(
			64u * 1024u, reserveBytes, FIBER_FLAG_FLOAT_SWITCH, &ibFiber::FiberProc, fiber);
		if (handle == nullptr)
			throw std::bad_alloc();
		fiber->m_osFiber = handle;
#else
		const long page = ::sysconf(_SC_PAGESIZE);
		const std::size_t pageSize = page > 0 ? static_cast<std::size_t>(page) : 4096u;
		const std::size_t usable = (reserveBytes + pageSize - 1) & ~(pageSize - 1);
		const std::size_t total = usable + pageSize;
		int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#  ifdef MAP_STACK
		flags |= MAP_STACK;
#  endif
		void* mem = ::mmap(nullptr, total, PROT_NONE, flags, -1, 0);
		if (mem == MAP_FAILED)
			throw std::bad_alloc();
		// Owned by the fiber before the protect call, so the catch below
		// unmaps it if that fails.
		fiber->m_stackAlloc = mem;
		fiber->m_stackAllocSize = total;
		void* bottom = static_cast<unsigned char*>(mem) + pageSize;
		if (::mprotect(bottom, usable, PROT_READ | PROT_WRITE) != 0)
			throw std::bad_alloc();
		fiber->m_stackBottom = bottom;
		fiber->m_stackSize = usable;
		fiber->InitPosixStack();
#endif
	}
	catch (...) {
#if defined(_WIN32)
		if (fiber->m_osFiber != nullptr)
			DeleteFiber(fiber->m_osFiber);
#else
		if (fiber->m_stackAlloc != nullptr)
			::munmap(fiber->m_stackAlloc, fiber->m_stackAllocSize);
#endif
		delete fiber;
		throw;
	}
#if defined(IB_FIBER_TSAN)
	fiber->m_tsanFiber = __tsan_create_fiber(0);
	__tsan_set_fiber_name(fiber->m_tsanFiber, "fiber");
#endif
	return fiber;
}

void ibFiber::Destroy(ibFiber* fiber)
{
	if (fiber == nullptr || fiber->m_scheduler)
		return;
	if (!fiber->m_finished) {
		// A stack that has not unwound still owns every C++ object on it.
		// Freeing it here is the use-after-free this rule exists to prevent.
		std::fputs("ibFiber::Destroy called on a fiber that has not unwound\n", stderr);
		std::abort();
	}
#if defined(IB_FIBER_TSAN)
	// From the fiber that resumed, never from this one: it has already
	// switched back. The context is ours; the scheduler's is not, and
	// Destroy refuses a scheduler above.
	if (fiber->m_tsanFiber != nullptr) {
		__tsan_destroy_fiber(fiber->m_tsanFiber);
		fiber->m_tsanFiber = nullptr;
	}
#endif
#if defined(_WIN32)
	if (fiber->m_osFiber != nullptr)
		DeleteFiber(fiber->m_osFiber);
	fiber->m_osFiber = nullptr;
#else
	if (fiber->m_stackAlloc != nullptr)
		::munmap(fiber->m_stackAlloc, fiber->m_stackAllocSize);
	fiber->m_stackAlloc = nullptr;
#endif
	delete fiber;
}

void ibFiber::SwitchTo(ibFiber* target)
{
	if (target == nullptr || target == this)
		return;
	m_locals.Capture();
	target->m_locals.Install();
	tl_currentFiber = target;

#if defined(IB_FIBER_ASAN)
	const void* bottom = nullptr;
	std::size_t size = 0;
	if (!target->m_scheduler) {
		bottom = target->m_stackBottom;
		size = target->m_stackSize;
	}
	__sanitizer_start_switch_fiber(&m_asanFake, bottom, size);
#endif

#if defined(IB_FIBER_TSAN)
	// Immediately before the switch, and not no_sync: the writes before
	// this call happen-before the reads after it on the target. The
	// assembly (and SwitchToFiber) cannot tell TSan that themselves.
	// A missing context would leave TSan on the fiber we just left.
	if (target->m_tsanFiber == nullptr)
		std::abort();
	__tsan_switch_to_fiber(target->m_tsanFiber, 0);
#endif

#if defined(_WIN32)
	SwitchToFiber(target->m_osFiber);
#else
	ibFiberSwitch(&m_sp, &target->m_sp);
#endif

#if defined(IB_FIBER_ASAN)
	__sanitizer_finish_switch_fiber(m_asanFake, nullptr, nullptr);
#endif
}

#if defined(_WIN32)

void __stdcall ibFiber::FiberProc(void* arg)
{
	ibFiber* const self = static_cast<ibFiber*>(arg);
#  if defined(IB_FIBER_ASAN)
	__sanitizer_finish_switch_fiber(nullptr, nullptr, nullptr);
#  endif
	MEMORY_BASIC_INFORMATION info;
	if (::VirtualQuery(&info, &info, sizeof(info)) != 0)
		self->m_stackBottom = info.AllocationBase;
	self->RunEntry();
	self->SwitchTo(tl_schedulerFiber);
	std::abort();
}

#endif
