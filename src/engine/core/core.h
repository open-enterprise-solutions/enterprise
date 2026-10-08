#ifndef _CORE_CORE_H__
#define _CORE_CORE_H__

#include <wx/defs.h>   // WXEXPORT / WXIMPORT — and nothing else: every header of the core includes this one

// THE SHARED MEANINGS — what the engine (backend) and its clients (frmclient, the protocol's sides) mean by the same
// words: the class ids and the ids of the configuration, the kinds of a value, a text, a date, a guid. Under both, and
// knowing neither: nothing here reads a base, runs a script or draws a window (docs/private/client-protocol-arc.md).
#if defined(CORE_EXPORTS)
#define CORE_API WXEXPORT
#else
#define CORE_API WXIMPORT
#endif

// `inline` IS A HINT AND THE INLINER DECLINES IT under a size budget — which it
// does exactly where it hurts, inside the big dispatch functions. procUnit.cpp
// found this first (its note: "the disassembly says so" — a two-instruction body
// still emitted a call from a 27 KB function) and defined these locally; they
// live here now because the second caller found the same wall, in
// ibNumber::Compare on the value comparator's hot arm.
//
// Reach for IB_FORCEINLINE only with a disassembly in hand. A forced body is
// pasted into EVERY caller, so the win has to be real and the body small.
#if defined(_MSC_VER)
#  define IB_FORCEINLINE __forceinline
#  define IB_NOINLINE    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#  define IB_FORCEINLINE inline __attribute__((always_inline))
#  define IB_NOINLINE    __attribute__((noinline))
#else
#  define IB_FORCEINLINE inline
#  define IB_NOINLINE
#endif

#endif
