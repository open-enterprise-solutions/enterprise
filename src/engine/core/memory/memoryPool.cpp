// =============================================================================
// ibMemoryPool — the pool behind the engine's small blocks (memoryPool.h): one free list per size class per
// thread, and the drains that hand it back to the CRT.
// =============================================================================

#include "core/memory/memoryPool.h"

#include <cstdlib>       // std::atexit — the last drain
#include <new>

namespace ibMemoryPool {
namespace detail {

	// Every size to the smallest class that holds it — all of them, at compile time, once. A block's going looks its
	// class up again from the room it was made with, so a size given a wrong class is a block in the wrong list.
	constexpr bool EverySizeCountsToTheSmallestClassThatHoldsIt() {
		for (std::size_t bytes = 1; bytes <= kClasses[kNum - 1] + 1; ++bytes) {
			int smallest = 0;
			while (smallest < kNum && kClasses[smallest] < bytes) ++smallest;
			if (ClassOf(bytes) != (smallest < kNum ? smallest : -1)) return false;
		}
		return true;
	}
	static_assert(EverySizeCountsToTheSmallestClassThatHoldsIt(), "ClassOf and kClasses disagree");

	struct Node { Node* next; };

	// The most a thread's cache holds of one class — in bytes. It was 128 blocks a class until 2026-10-28, and then
	// a list's strings past the 128th were the CRT's both ways: 54 characters, 1000 alive at once, 127 ns a string
	// made and let go where the pool's blocks take 15 (IbStringBench, `heap, N alive`). Each class its own: one sum
	// for all of them, filled by 100000 strings of one size, left the others none — a short string 11 → 36 ns. And
	// a cap still: a thread that frees what another one makes would otherwise keep all of it — at most 56 MB a
	// thread, every class full.
	constexpr std::size_t kCap = 2 * 1024 * 1024;

	struct ThreadPool {
		// ⚠ NO INITIALIZERS: a thread_local of a trivial type is zeroed STATICALLY. `= {}` here gave the pool a
		// constructor, and MSVC then asked a lazy-init guard at EVERY access — four of them in one free, seen in
		// the disassembly (`cmp byte ptr [tls+0A0h],0 … call __dyn_tls_on_demand_init`, 2026-09-28).
		Node*         head[kNum];
		std::size_t   bytes[kNum];   // what each list holds, against kCap

		// Hand every cached block back to the CRT. Shared by Drain() and, off Windows, by the
		// destructor below.
		void Release() noexcept {
			for (int c = 0; c < kNum; ++c) {
				Node* node = head[c];
				while (node != nullptr) {
					Node* const next = node->next;
					::operator delete(node);
					node = next;
				}
				head[c] = nullptr;
				bytes[c] = 0;
			}
		}

#ifndef __IBMSW__
		// POSIX has no DLL_THREAD_DETACH, so the drain that covers worker threads on Windows has
		// no place to live here — and without it every thread that ends keeps its cache forever,
		// which on a long-running server (its workers come and go with the sessions) accumulates
		// thread after thread. So off Windows the pool destroys itself on thread exit, which is
		// exactly what thread_local destructors are for.
		//
		// NOT done on Windows, deliberately: there a thread_local with a destructor is built by
		// __dyn_tls_init for EVERY thread and costs an 8-byte registration node that is lost when
		// a thread is killed at process exit — the trade is measured in
		// docs/private/engineering-playbook/25-memory-leaks.md. DllMain covers those threads for free, so
		// the pool stays trivially destructible there.
		~ThreadPool() noexcept { Release(); }
#endif
	};

	// One per thread — and, the blocks living in this module only, one pool for every text and buffer.
	//
	// ⚠ ON LINUX, AT A FIXED OFFSET (initial-exec). The pool is read at every block's making and going, and from
	// inside a shared library the default model reaches a thread_local through a __tls_get_addr call each time —
	// the likeliest part of making a string going from 20 to 104 ns on the CI's Linux when the pool moved into
	// fstring.cpp (IbStringBench, 2026-09-27), while it costs 46 on Windows, whose DLL thread storage is a fixed
	// slot anyway. The library is loaded with the process, never opened later, which is what the model asks.
#if defined(__linux__)
	thread_local ThreadPool t_pool __attribute__((tls_model("initial-exec")));
#else
	thread_local ThreadPool t_pool;
#endif

} // namespace detail

void* AllocateClass(int c) {
	detail::ThreadPool& pool = detail::t_pool;       // the thread's pool reached once
	if (detail::Node* n = pool.head[c]) {            // reuse a cached block
		pool.head[c] = n->next;
		pool.bytes[c] -= detail::kClasses[c];
		return n;
	}
	return ::operator new(detail::kClasses[c]);       // fresh block, full class size
}

void Deallocate(void* p, std::size_t bytes) noexcept {
	if (p == nullptr) return;
	const int c = detail::ClassOf(bytes);
	if (c < 0) { ::operator delete(p); return; }      // no class, or oversized → return to the OS
	detail::ThreadPool& pool = detail::t_pool;
	if (pool.bytes[c] >= detail::kCap) { ::operator delete(p); return; }   // cache full
	detail::Node* n = static_cast<detail::Node*>(p);   // cache for reuse
	n->next = pool.head[c];
	pool.head[c] = n;
	pool.bytes[c] += detail::kClasses[c];
}

// Hands this thread's cached blocks back to the CRT. The cache exists to make string and buffer
// churn cheap, not to outlive the process: at exit the free list is indistinguishable from a leak
// in the CRT dump — the block still holds its old contents with only the first word overwritten by
// `next`, which is why the dump used to show fragments of metadata names. 279 blocks of that
// hide the next real leak.
//
// An explicit call at a chosen point, because the one thread that needs it most cannot be reached
// any other way: the thread that calls exit() detaches the PROCESS, never itself, so no
// thread-exit hook fires for it on any platform. Worker threads are covered without this — by
// DLL_THREAD_DETACH on Windows, by ~ThreadPool elsewhere (see the note there).
void Drain() noexcept { detail::t_pool.Release(); }

} // namespace ibMemoryPool

// ⚠ AND ONCE MORE, LAST OF ALL. A store of strings as long-lived as the process — the pictures a server sends, made
// once and kept in statics (ibPicture) — hands its blocks back during static destruction, after every Drain
// above; there they would sit in the pool past the end and read as leaks. So a drain is registered before every
// ordinary static of this module and runs after all of them: the runtime keeps static destructors and atexit
// handlers in one LIFO list (docs/private/engineering-playbook/25-memory-leaks.md, "Ordering: running a cleanup last").
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4073)   // init_seg(lib) is reserved for library code — this IS the library
#pragma init_seg(lib)            // construct before every ordinary static in this DLL
#pragma warning(pop)
#endif

namespace {
struct ibMemoryPoolDrainAtExit {
	ibMemoryPoolDrainAtExit() { std::atexit([] { ibMemoryPool::Drain(); }); }
};
#if defined(__GNUC__)
ibMemoryPoolDrainAtExit s_memoryPoolDrainAtExit __attribute__((init_priority(101)));
#else
ibMemoryPoolDrainAtExit s_memoryPoolDrainAtExit;
#endif
} // namespace
