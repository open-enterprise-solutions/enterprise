#ifndef _CORE_MEMORY_POOL_H__
#define _CORE_MEMORY_POOL_H__

/////////////////////////////////////////////////////////////////////////////
// ibMemoryPool — the per-thread, size-classed free list the engine's small blocks come from: a text's
// (ibString) and a buffer's bytes (ibMemoryBuffer). A block freed goes back to the pool of the thread
// that frees it, up to kCap per class; one larger than the biggest class goes straight to the CRT.
//
// It was ibString's alone (fstring.cpp) until the buffer took the string's shape (2026-10-23); the code is
// the string's, moved. AllocateClass and Deallocate are core's own — not exported, so on Linux, where core
// hides what it does not export, a call to them is direct; Drain is exported for the hosts' exit paths.
/////////////////////////////////////////////////////////////////////////////

#include "core/core.h"

#include <cstddef>
#if defined(_MSC_VER)
#include <intrin.h>      // _BitScanReverse — the pool's class, counted
#endif

namespace ibMemoryPool {
namespace detail {

	constexpr std::size_t kClasses[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
	constexpr int         kNum       = static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));
	constexpr std::size_t kCap       = 128;   // max cached blocks per class per thread

	static_assert(kClasses[0] == 16, "ClassOf counts classes as powers of two from 16");

	// The width of a number in bits — std::bit_width, which this C++17 tree does not have.
	inline int BitWidth(std::size_t x) noexcept {
		if (x == 0) return 0;
#if defined(_MSC_VER)
		unsigned long top;
#  if defined(_WIN64)
		_BitScanReverse64(&top, x);
#  else
		_BitScanReverse(&top, x);
#  endif
		return static_cast<int>(top) + 1;
#else
		return static_cast<int>(sizeof(unsigned long long) * 8) - __builtin_clzll(static_cast<unsigned long long>(x));
#endif
	}

	// The class a block of this many bytes comes from (16 << class holds it), counted rather than searched:
	// a block's making and its going both ask.
	inline int ClassOf(std::size_t bytes) noexcept {
		if (bytes <= kClasses[0]) return 0;
		if (bytes > kClasses[kNum - 1]) return -1;   // larger than the biggest class → straight to ::operator new
		return BitWidth(bytes - 1) - 4;               // 17..32 → 1, 33..64 → 2, … 2049..4096 → 8
	}

} // namespace detail

// A block of class `c`: a cached one, or a fresh one of the class's full size. The caller has counted the class
// already — the block's maker asks it once, and knows then how much room it gets.
void* AllocateClass(int c);

// A block of `bytes` back: to this thread's pool when its class has room there, to the CRT otherwise.
void Deallocate(void* p, std::size_t bytes) noexcept;

// Hands this thread's cached blocks back to the CRT — see the note at the definition.
CORE_API void Drain() noexcept;

} // namespace ibMemoryPool

#endif // !_CORE_MEMORY_POOL_H__
