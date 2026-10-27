#ifndef _CORE_API_H__
#define _CORE_API_H__

// THE CORE'S LIBRARY — how its API is exported, and the character and the literal its text is made of. On its own, so
// the string (fstring.h) takes these without the rest of core.h, which names a class id by its text and so needs the
// string first.

// THE PLATFORM, as the engine names it — what wx calls __WXMSW__ / __WXOSX__ / __WXGTK__: Windows, macOS, and every
// other Unix (Linux first).
#if defined(_WIN32)
#  define __IBMSW__ 1
#elif defined(__APPLE__)
#  define __IBOSX__ 1
#else
#  define __IBGTK__ 1
#endif

// A module's export and import, for every module's own API macro (CORE_API below) — what wx calls WXEXPORT / WXIMPORT.
#if defined(_WIN32)
#  define IB_EXPORT __declspec(dllexport)
#  define IB_IMPORT __declspec(dllimport)
#else
#  define IB_EXPORT __attribute__((visibility("default")))
#  define IB_IMPORT __attribute__((visibility("default")))
#endif

#if defined(CORE_EXPORTS)
#define CORE_API IB_EXPORT
#else
#define CORE_API IB_IMPORT
#endif

// THE ENGINE'S CHARACTER — what an ibString (fstring.h) is made of, and what wx calls wxChar. Here, not in
// fstring.h, because headers under it (core.h, the number's separators, the boolean's words) name it too.
// UTF-16 on every OS: one length and one index for a text wherever it is read, and the wire's bytes as they lie.
#define ibChar char16_t
// …and a literal of it, as wxT is wx's: ibT("text"), ibT('c'). The literal itself, never a macro's name.
#define ibT(text) u ## text

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
