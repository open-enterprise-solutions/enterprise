# Portability — the rules, and what breaking them cost

> **Why this file exists.** On 2026-08-02 the tree was compiled by GCC for the first time in a
> long while (CI, see [BUILD.md § Continuous integration](BUILD.md)). It took **fourteen
> rounds** to get from "does not compile" to "builds and runs 95% of the suite". Almost none of
> the fixes made the code *more Linux-ish* — each one made it **correct C++** that MSVC had been
> letting slide. This is the distilled set of rules, so the next thousand lines do not repeat it.
>
> Read § 1 before writing code that touches types, includes or platform APIs. § 2 is the
> evidence.

---

## 1. The rules

### 1.1 One base for every 64-bit alias

`long`, `long long` and `int64_t` are **three different types** to the C++ overload resolver,
even where all three are 64 bits wide. MSVC hides this: on Windows `int64_t`, `wxLongLong_t` and
`__int64` are all the same type, so mixing them is invisible. On LP64 (Linux, macOS) `int64_t` is
`long` while `wxLongLong_t` is `long long`, and the two stop being interchangeable.

- Every 64-bit id in the tree is `uint64_t`: `ibClassID`, `ibPictureID`, and the `s64` / `u64`
  the reader/writer layer speaks. Do not introduce an alias over `wxLongLong_t`.
- A **value** can still convert, so this bites hardest on **references and pointers**: they bind
  to exactly one type. `ToInt(int64_t&)` simply cannot accept a `wxLongLong_t`, and
  `ToULongLong(unsigned long long*)` cannot accept the address of a `uint64_t`.
- When a type must accept every integer, declare its constructors over the **language** types
  (`int`, `long`, `long long` and unsigned twins), not over `<cstdint>` aliases. No two of those
  collide on any platform we target, so one set of declarations binds everywhere — this is what
  `ibNumber` does.
- Converting ms to `wxDateTime` goes through `wxLongLong`, never implicitly.

### 1.2 Include what you use — MSVC's transitive includes are not a contract

If a file declares a `std::atomic` member, it includes `<atomic>`. Same for `<algorithm>`,
`<cstring>`, `<limits>`. MSVC's headers happen to drag these in; libstdc++ and libc++ do not, and
neither does MSVC after an unrelated header is reordered. A file that compiles only because of
someone else's include is not portable — it is *lucky*.

### 1.3 Include a driver header under the same guard as the code using it

Backend driver classes are `BACKEND_API`, i.e. `__declspec(dllexport)` while `backend.dll` is
built, and **MSVC instantiates an exported class in full in every TU that sees its definition**.
So an unguarded `#include` makes that object file demand the class's whole vtable, constructor and
destructor even when every *use* is compiled out by the preprocessor. Guarding the code without
guarding the include achieves nothing.

The macro contract: `OES_USE_<DRIVER>` is defined **exactly when that driver's sources are
compiled**. CMake does it per option; the `.sln` does it through `OesDriverDefines` in
`ConfigurationDefs.props` (there all drivers are always built).

### 1.4 Platform APIs need their platform's headers on BOTH sides

`SOL_SOCKET`, `SO_*`, `IPPROTO_TCP`, `TCP_NODELAY` arrive with winsock through wx on Windows and
from `<sys/socket.h>` / `<netinet/in.h>` / `<netinet/tcp.h>` on POSIX. A `#if defined(_WIN32)`
block whose `#else` branch is thinner than its `#if` branch is a bug waiting for the first person
to build the other platform.

### 1.5 A nested class cannot carry a default argument built from its own initializers

A nested class's default member initializers are not complete until the **enclosing** class is,
so a method of that enclosing class may not default an argument to `Nested{}` or `Nested()`. No
spelling fixes it — the type has to move to namespace scope. `ibDbTxOptions` did, keeping an
in-class alias so `ibDatabaseLayer::ibTxOptions` still reads the same.

### 1.6 The small ones, each of which cost a round trip

| Construct | Why it fails elsewhere |
|---|---|
| anonymous `struct { … };` holding a member with a constructor | an MSVC extension; also, `T* a, b;` declares `b` **by value** |
| `T& x = T()` as a default argument | binds a non-const reference to a temporary |
| `cond ? wxString : wxEmptyString` | the arms must agree; `wxEmptyString` is a `const wxChar*` |
| `enum Foo` in a declaration with no `Foo` in scope | elaborated specifier, legal only if the type is declared |
| `ibValuePtr<T> p = new T()` | the pointer constructor is `explicit`; copy-init cannot use it |
| `ofstream::open(wstring)` | an MSVC extension — POSIX takes UTF-8 bytes |

### 1.7 Keep the warning log readable, or it stops being a tool

The first GCC build produced **~48,000 warnings**. Buried in them were three
`-Wreturn-local-addr` lines naming the exact cause of 33 crashing tests — found only because
someone went looking. Two classes accounted for 97% of the volume and none of the value, so
they are off in the CMake build: `-Wunknown-pragmas` (`#pragma region`, an MSVC editor hint)
and `-Wattributes` (`BACKEND_API` on elaborated type specifiers, ignored off-Windows).

What was deliberately left ON, having been swept and found clean — so that when the count
rises again, it means something:

| Class | Count | Sites | Real bugs |
|---|---|---|---|
| `-Wreorder` | 1,098 | 107 | none — every initialiser reads a *parameter*; the only two members initialised from another member declare it first |
| `-Wswitch` | 83 | 13 | none — each is a deliberately narrow switch with a fallback return after it |

### 1.8 Fix the root, then sweep — do not fix where the compiler stopped

A compiler reports the first place a class of mistake surfaces, never the class. Every time one
of these appeared, the same pattern was grepped across the tree, and it usually found more:
`<atomic>` was missing in **five** files, not the one that failed; `?:` with `wxEmptyString` was in
**eight** places; the socket headers in **three**. Two waves were the *same root* seen from two
sides (`ibNumber`'s constructors, then its `ToInt`) — a root fix retires the whole class,
including the instances the compiler had not reached yet.

---

## 2. Platform status

| Platform | Build | Suite | Notes |
|---|---|---|---|
| **Windows x64** (MSVC, Ninja) | green | **855/855** | Matches the local `.sln` build exactly |
| **Windows x86** (MSVC, `.sln`) | green | — | The shipping build |
| **Linux x64** (GCC 11, libstdc++, wxGTK) | green | 820/862 | See below |
| **GUI (Linux, Xvfb)** | — | — | Runs `oes_frontend_runtime_test`; 26/26 locally on wxMSW |
| **macOS** | not in CI | — | A colleague builds locally; ARM64, libc++, wxOSX — three differences at once |

### Linux — what is still red (2026-08-02)

- **33 SEGFAULTs, one suspected root.** Every failing test compiles or executes a script:
  `CompilerTest`, `ClosureCapture`, `RuntimeTest` in full, plus `CompilerAOT`. Nothing in values,
  queries, serialization or the DB layer fails. `RuntimeTest.EmptyBytecodeNoop` **passes**, so the
  machinery comes up and it is handling non-empty source that dies.

  Worth stating plainly: **the same tests pass on Windows**. A crash on one platform with green
  tests on another is as often latent undefined behaviour as it is a platform difference — the
  kind that "works" only because of an allocator or an initialisation order. Until the backtrace
  says otherwise, treat it as possibly affecting Windows too. CI takes a `gdb` backtrace on
  failure so the next round answers this rather than guessing.
- **2 `FirebirdLeaseTest` failures**, plain assertion failures rather than crashes — environment
  rather than language, not yet investigated.

### Not yet proven anywhere

- **ARM64.** `ibNumber` uses `_addcarry_u32` / `_subborrow_u32` under MSVC on x86/x64 and a
  portable fallback everywhere else. That fallback has **never been executed by the suite** — only
  compiled. An ARM job (`ubuntu-24.04-arm`, free for public repos) would run the exact-decimal
  tests over it, and does so without dragging in Clang / libc++ / wxOSX at the same time, which is
  what makes it a cleaner first step than macOS.
