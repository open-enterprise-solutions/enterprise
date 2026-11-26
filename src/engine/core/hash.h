#ifndef _CORE_HASH_H__
#define _CORE_HASH_H__

#include <cstdint>

// ---------------------------------------------------------------------------
// THE MIXER, once. Every value hash in the engine is spelled through these and
// nowhere else — five hand-written copies of FNV-1a is five chances for one of them
// to be typed slightly differently, and a hash that differs by site is not a hash.
// The core's values hash themselves through it (GetHash of a flag, a date, a number,
// a text); the engine's value hands each its own (ibValue::GetValueHash).
//
// ⚠ ACCUMULATE IN uint64_t, NARROW ONCE AT THE END. size_t is 32 bits on the x86
// build while these constants are 64, so a size_t accumulator silently TRUNCATES
// them and that build runs a different — and much worse — hash than the x64 one.
// MSVC does say so (C4305/C4309), but only when the SOLUTION is built: the CMake
// test tree is x64, so the whole family of this defect is invisible there.
constexpr std::uint64_t kIbHashBasis = 14695981039346656037ULL;   // FNV-1a offset basis
constexpr std::uint64_t ibHashCombine(std::uint64_t h, std::uint64_t v)
{
	return (h ^ v) * 1099511628211ULL;                            // FNV-1a prime
}

// A whole word, fed through the mixer OCTET BY OCTET — which is what FNV-1a
// actually is, and what a raw 64-bit payload needs: one ibHashCombine of a word
// leaves the hash's low bits reading only the word's low bits, so two dates 2^32 ms
// apart would part only above the 32 bits a lock key keeps. ibHashCombine folds
// what is already mixed (another hash, a character); this folds a date or an
// integer straight off the field.
constexpr std::uint64_t ibHashStep(std::uint64_t h, std::uint64_t v)
{
	for (int i = 0; i < 8; ++i, v >>= 8)
		h = ibHashCombine(h, v & 0xFF);
	return h;
}

#endif
