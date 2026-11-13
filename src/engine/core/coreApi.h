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
#  define IB_EXPORT_VTABLE
#else
// A class exported MEMBER BY MEMBER (fstring.h says why) that has a vtable is still seen WHOLE outside its module on
// ELF and Mach-O: its vtable and type info follow the class's own visibility there, and the modules are built hidden —
// what Boost calls BOOST_SYMBOL_VISIBLE. On Windows a mark on the class is the import table again, so there it is none.
#  define IB_EXPORT __attribute__((visibility("default")))
#  define IB_IMPORT __attribute__((visibility("default")))
#  define IB_EXPORT_VTABLE __attribute__((visibility("default")))
#endif

#if defined(CORE_EXPORTS)
#define CORE_API IB_EXPORT
#else
#define CORE_API IB_IMPORT
#endif
#define CORE_API_VTABLE IB_EXPORT_VTABLE

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

// A name no other in the translation unit has, from a prefix: ibMAKE_UNIQUE_NAME(s_reg_) -> s_reg_17. What wx calls
// wxMAKE_UNIQUE_NAME. The counter, not the line, on every compiler: unique within the unit even when two
// registrations come from one line of a macro, and MSVC's Edit and Continue (/ZI) breaks __LINE__ inside a macro.
#define ibCONCAT_HELPER(x1, x2) x1 ## x2
#define ibCONCAT(x1, x2) ibCONCAT_HELPER(x1, x2)
#define ibMAKE_UNIQUE_NAME(text) ibCONCAT(text, __COUNTER__)

// "Not found" for an index or a position: what wx calls wxNOT_FOUND, the same -1.
constexpr int ibNOT_FOUND = -1;

// AN ASSERTION - what wx calls wxASSERT / wxASSERT_MSG / wxFAIL_MSG: a condition the code holds true, checked in a
// Debug build. NDEBUG leaves it unread (sizeof: what it names is still used, so a variable only an assertion reads draws
// no warning), as wx's are not checked in a release build. A failure is said and the code goes on - once per place, by
// the handler a program sets (ibCrashGuard::SetAssertHandler), the journal and stderr when it set none.
class ibString;
CORE_API void ibAssertFailure(const char* file, int line, const char* func, const char* cond, const ibString& msg);

#ifdef NDEBUG
#  define ibASSERT_MSG(cond, msg) ((void)sizeof(!(cond)))
#  define ibFAIL_MSG(msg)         ((void)0)
#else
#  define ibASSERT_MSG(cond, msg) ((cond) ? (void)0 : ibAssertFailure(__FILE__, __LINE__, __func__, #cond, msg))
#  define ibFAIL_MSG(msg)         ibAssertFailure(__FILE__, __LINE__, __func__, "Assert failure", msg)
#endif
#define ibASSERT(cond) ibASSERT_MSG(cond, ibString())

#endif
