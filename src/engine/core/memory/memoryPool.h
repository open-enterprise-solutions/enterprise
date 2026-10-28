#ifndef _CORE_MEMORY_POOL_H__
#define _CORE_MEMORY_POOL_H__

/////////////////////////////////////////////////////////////////////////////
// ibMemoryPool — the per-thread, size-classed free list the engine's small blocks come from: a text's
// (ibString) and a buffer's bytes (ibMemoryBuffer). A block freed goes back to the pool of the thread
// that frees it while that thread's cache has room (kCap, memoryPool.cpp); one larger than the biggest class
// goes straight to the CRT.
//
// It was ibString's alone (fstring.cpp) until the buffer took the string's shape (2026-10-23); the code is
// the string's, moved. AllocateClass and Deallocate are core's own — not exported, so on Linux, where core
// hides what it does not export, a call to them is direct; Drain is exported for the hosts' exit paths.
/////////////////////////////////////////////////////////////////////////////

#include "core/core.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ibMemoryPool {
namespace detail {

	// Sixteen bytes a step up to 64, then four classes a doubling: past 64 a block is at most a quarter more than
	// asked. Powers of two (until 2026-10-28) gave a 54-character text, 134 bytes, a block of 256; it takes 160.
	constexpr std::size_t kClasses[] = {
		  16,   32,   48,   64,
		  80,   96,  112,  128,
		 160,  192,  224,  256,
		 320,  384,  448,  512,
		 640,  768,  896, 1024,
		1280, 1536, 1792, 2048,
		2560, 3072, 3584, 4096 };
	constexpr int         kNum       = static_cast<int>(sizeof(kClasses) / sizeof(kClasses[0]));

	// The class of each sixteen bytes' step, made from the classes above at compile time — every class is whole
	// steps, so a step never straddles two (memoryPool.cpp checks every size).
	inline constexpr auto kClassOfStep = [] {
		std::array<std::uint8_t, kClasses[kNum - 1] / 16> classOf{};
		int c = 0;
		for (std::size_t step = 0; step < classOf.size(); ++step) {
			while (kClasses[c] < (step + 1) * 16) ++c;
			classOf[step] = static_cast<std::uint8_t>(c);
		}
		return classOf;
	}();

	// The class a block of this many bytes comes from — the smallest that holds it — looked up, not counted: a
	// block's making and its going both ask, and the bit count with its shifts cost a text 1.5–2 ns more than one
	// load (IbStringBench, 2026-10-28).
	constexpr int ClassOf(std::size_t bytes) noexcept {
		if (bytes - 1 >= kClasses[kNum - 1]) return -1;   // none, or larger than the biggest class → ::operator new
		return kClassOfStep[(bytes - 1) / 16];
	}

} // namespace detail

// A block of class `c`: a cached one, or a fresh one of the class's full size. The caller has counted the class
// already — the block's maker asks it once, and knows then how much room it gets.
void* AllocateClass(int c);

// A block of `bytes` back: to this thread's pool while its cache has room, to the CRT otherwise.
void Deallocate(void* p, std::size_t bytes) noexcept;

// Hands this thread's cached blocks back to the CRT — see the note at the definition.
CORE_API void Drain() noexcept;

} // namespace ibMemoryPool

#endif // !_CORE_MEMORY_POOL_H__
