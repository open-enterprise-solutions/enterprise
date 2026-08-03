# Portability — the rules, and what breaking them cost

> **Why this file exists.** On 2026-08-02 the tree was compiled by GCC for the first time in a
> long while (CI, see [BUILD.md § Continuous integration](BUILD.md)). It took a day to get from
> "does not compile" to **the whole suite green on Linux, 914/914 — the same count as Windows**.
> Almost none of the fixes made the code *more Linux-ish*; each one made it **correct C++** that
> MSVC had been letting slide, and one of them (§1.6, the dangling `?:` reference) was live
> undefined behaviour in the shipping Windows build. This is the distilled set of rules, so the
> next thousand lines do not repeat it.
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

### 1.7 A GUI object may not be a file-scope static

A `static wxScreenDC` (or `wxBitmap`, `wxFont`, …) at namespace scope is constructed while the
library loads — **before `wxApp::OnInit`, before `gtk_init`**. Windows tolerates it because
`GetDC(nullptr)` works at any time; GTK does not. The compiler cannot see this: it is a
load-order fault, so it shows up as a crash on startup, not as an error.

Build the object where it is used. `ibDynamicStaticText` measured labels through a shared static
DC, which additionally forced a critical section around the measuring block — a shared DC cannot
serve two threads. Local, it costs a DC only on a cache miss and needs no lock at all: the
portability fix removed code rather than adding it.

### 1.8 An initialiser list that disagrees with the declaration order is a lie

Members are constructed in **declaration order**, always. The order written in the initialiser
list changes nothing — the compiler silently reorders. So a list that reads

```cpp
class A { int m_size; int m_count; public: A(int n) : m_count(n), m_size(m_count * 2) {} };
```

looks like it computes `m_count` and then derives `m_size`, while in fact `m_size` is built
first and reads an **uninitialised** `m_count`. No crash, no error — just a wrong number now
and then. Nothing in this tree does that today (every initialiser reads a *parameter*), which
is exactly why the lists were reordered rather than the warning silenced: the day someone adds
a member-to-member dependency to a list that is already out of order, the bug is silent.

### 1.9 Keep the warning log readable, or it stops being a tool

The first GCC build produced **~48,000 warnings**. Buried in them were three
`-Wreturn-local-addr` lines naming the exact cause of 33 crashing tests — found only because
someone went looking.

Ask both toolchains for the same thing, and mean it:

| | Flags | Note |
|---|---|---|
| GCC / Clang | `-Wall`, minus `-Wunused-parameter` / `-Wsign-compare` | `-Wunknown-pragmas` and `-Wattributes` are off — 97% of the original volume, none of the value (`#pragma region`; `BACKEND_API` on elaborated specifiers) |
| MSVC (`.sln`, Debug) | `/W4` | was `/W3`, i.e. blind to `-Wreorder`'s counterpart `C5038` |
| MSVC (CMake) | `/W4` | was `/W1` — CI saw **less** than the developer's own Visual Studio, so it could not act as the control |

`Common.props` and the CMake MSVC branch disable the same set (`4100` = unused parameter,
`4018/4389/4245` = sign compare, `4127` = constant condition in wx macros), so a warning visible
on one compiler is visible on the other.

Cleared out on 2026-08-02, from 3,513 GCC warnings down to a couple of hundred:

| Class | Was | Action |
|---|---|---|
| `-Wreorder` | 1,812 in 58 sites | lists reordered — see §1.8. One header (`compileContext.h`) accounted for 1,008 of them: it is included by every TU, so a single constructor was reported over and over. Count sites, not lines |
| `-Wwrite-strings` | 1,374 | XPM arrays are `const char*` now (31 files). Also a real fix: they feed a `const char**` field, and `char*[]` → `const char**` is not a conversion the standard allows. The 9 that remain are inside wx's own `wxT()` macro |
| `-Wparentheses` | 35 | `(BASE + level) | FLAG` in the fold-level code. Precedence already grouped it that way — the parentheses only say so |
| `C4706` (MSVC) | 3 | `if (hr = Call())` in `valueOLE.cpp`. The idiom was deliberate but is spelled exactly like a mistyped `==`, which is why the warning exists; now `(hr = Call()) != S_OK` |
| `-Wswitch` | 83 in 13 sites | left as is — each is a deliberately narrow switch with a fallback return after it |
| `C4244` (MSVC) | 305 | left as is — every one is in `3rdparty/` (libwebp, wx). Our own code is clean at `/W4` |

Two ways this sweep went wrong, both caught by the local build, both worth avoiding:

- **A regex must not touch an initialiser list containing a call.** It stops at the first
  closing paren, so `m_pRef(p ? p->Get() : nullptr)` and
  `m_objGuid(src.m_metaObject->CreateUniqueKeyPair())` came out mangled. Nested parens get
  reordered by hand.
- **Neighbouring classes are not evidence.** `objCtor.h` holds two nearly identical ctor
  classes whose members are declared in **mirrored** order, so a fix copied from one broke
  the other — which had been correct. Read the declaration order of the class you are editing.

### 1.10 Fix the root, then sweep — do not fix where the compiler stopped

A compiler reports the first place a class of mistake surfaces, never the class. Every time one
of these appeared, the same pattern was grepped across the tree, and it usually found more:
`<atomic>` was missing in **five** files, not the one that failed; `?:` with `wxEmptyString` was in
**eight** places; the socket headers in **three**. Two waves were the *same root* seen from two
sides (`ibNumber`'s constructors, then its `ToInt`) — a root fix retires the whole class,
including the instances the compiler had not reached yet.

---

## 2. Platform status

Measured from the CI job logs of 2026-08-03, not from recollection.

| Platform | Build | Suite | Notes |
|---|---|---|---|
| **Windows x64** (MSVC, Ninja) | green, **applications linked** | **919/919** (137 s) | Matches the local `.sln` build exactly. Job ~37 min |
| **Windows x86** (MSVC, `.sln`) | green | — | The shipping build |
| **Linux x64** (GCC 11, libstdc++, wxGTK) | green, **applications linked** | **919/919** (107 s) | Identical count to Windows — same tests, same result. Job ~33 min |
| **GUI (Linux, Xvfb)** | green | **26/26** (1.9 s) | `libfrontend.so` links; the runtime-form suite passes under Xvfb |
| **macOS 14 arm64** (Apple Clang, libc++, wxOSX) | backend + frontend green; designer in progress | pending | **Fastest of the three: ~20 min against Linux 33 and Windows 37** — three-core M1 at `"jobs": 3`, half the parallelism the Windows preset uses. Six defects found on its first runs, listed in § 3 |

The engine — backend, compiler, interpreter, query engine, all five drivers — is cross-platform
as of 2026-08-02, and as of 2026-08-03 so are the **applications**: `enterprise`, `designer`,
`daemon`, `launcher`, `codeRunner` and `simplePlugin` link on Linux and Windows in CI. Three
toolchains, three standard libraries, two 64-bit models, two CPU architectures, one result.

What that claim does **not** cover, so nobody reads more into the table than it says:

- ~~**The applications are never linked in CI.**~~ **Closed 2026-08-03**: every job now ends
  with a `Build the applications` step (the default target), so `enterprise`, `designer`,
  `daemon`, `launcher`, `codeRunner` and `simplePlugin` link on all three platforms. It was as
  cheap as predicted — backend and frontend are already built by the steps before it, leaving
  only the executables' own translation units.
- **Only SQLite actually executes.** Firebird is compiled but loads `fbclient` at run time, and
  the runner has none; PostgreSQL, MySQL and ODBC are compile-only. `FirebirdLeaseTest` exercises
  file-lock mechanics against a path that need not exist, not the driver.
- ~~**Only x86-64.**~~ **Closed 2026-08-03** by the macOS arm64 job. The earlier reasoning —
  that ARM buys little because `ibNumber` selects its carry intrinsics on `_MSC_VER` rather than
  on the architecture, so Clang and GCC already execute the portable fallback — holds for
  `ibNumber` specifically, and that part was right. What it undersold is everything outside it:
  on AArch64 `char` is **unsigned**, the memory model is weaker (a missing atomic ordering is no
  longer hidden by x86's), and alignment/padding differ where a type's natural alignment drove
  the layout. Those are properties of the CPU, not of one class's intrinsics.
- **Nobody has run the product on Linux.** Building is not running: the `wxScreenDC` in §1.7 was
  found by reading, and that class of fault — load order, resources, paths — only appears when
  something actually starts.

### What the crashes turned out to be

The first Linux run had **33 SEGFAULTs**, every one in a test that compiles or executes a script.
The cause was not a platform difference: three accessors in `ibLexem` returned `const wxString&`
from a conditional whose second arm was a temporary. A conditional with an lvalue and a prvalue
arm yields a **prvalue**, so the reference dangled on *every* call, on every platform. Windows
had been passing on allocator luck. Fixing it retired all 33 at once — and removed real
undefined behaviour from the shipping build.

The remaining two `FirebirdLeaseTest` failures were genuine POSIX semantics: `fcntl(F_SETLK)`
locks are owned by the **process**, not the file description, so two connections inside one
process could not see each other's lock and leader election never excluded anyone. `F_OFD_SETLK`
(Linux 3.15+) is tied to the open file description and matches what `LockFileEx` does per HANDLE
on Windows.

### GUI — what it took

All **372** frontend translation units compile under wxGTK, `libfrontend.so` links, and the
runtime-form suite passes 26/26 under Xvfb. (Sixteen more sources are excluded on purpose:
`web/` + `wfrontend.cpp` belong to `wfrontend.dll`, and six `uikit` units are unfinished.)

Not one failure was about drawing a widget. `uikit` is a wxUniversal fork that paints its own
controls, so it carries no native-toolkit assumptions — it went through untouched. Everything
that broke was ordinary C++ MSVC had accepted: default arguments binding a non-const reference
to a temporary, an anonymous aggregate holding members with constructors, `wxToolTip` used
without its header, `wxScrolledWindow::OnScroll` (gone since wx 2.8), and twice an include path
whose case only a case-sensitive filesystem checks — the second of which alone blocked nine
files, since the header is pulled in by the whole code editor.

A note on method, because it cost the most: with ninja stopping at the first error, a 20-minute
CI round returned exactly one line. Building the GUI job with `-k 0` returns every independent
failure at once — the round that produced the final thirteen files resolved to **four** causes.
Also worth knowing: a `fatal error` (missing header) stops that file at the `#include`, so its
body is not checked at all. Thirteen failures were ten unread files plus three real errors.

### Not yet proven anywhere

- ~~**ARM64.**~~ **Closed 2026-08-03** by the macOS arm64 job — see § 3. The portable
  `ibNumber` carry fallback now executes on a real AArch64 CPU, not only compiles.

---

## 3. What the third toolchain found on its first day (2026-08-03)

Apple Clang on AArch64 is the third compiler, the third standard library and the second CPU at
once. It paid for itself immediately, and — this is the part worth remembering — **none of what
it found was new**. Every one of these had been sitting in the tree, invisible for want of an
observer:

| Finding | Class |
|---|---|
| `getrandom()` called on macOS | a two-way `#if` where the platform needs three (§ 1.4) |
| ten `NULL` compared against non-pointers | Firebird handles are `unsigned int`; wx `Parse*` return `bool` |
| `CheckIndex` declared `inline`, defined in a `.cpp`, called from a header | a promise no TU could keep |
| three `SetPropVal` overrides missing the base's `const` | not overrides at all — they HID the virtual |
| `wxVariant(m_selected[idx])` over a `std::vector<bool>` | indexing yields a PROXY; libc++ finds the conversion ambiguous |
| two `wxCharts` include guards checking one macro and defining another | the header is not guarded at all |

**The noise mattered as much as the findings.** The first macOS run produced 67,318 warning
lines, of which 66,563 were `-Winconsistent-missing-override` — the property hierarchy marks
about half its overrides, and every unmarked one reprints in each TU that includes the header.
Silencing that one class (Clang only; GCC has no equivalent in `-Wall`) took the log to ~1,200
lines and made the six findings above visible. They had all been present in the first log too.

Two lessons about *silencing* itself, both learned the hard way here:

- `COMPILE_OPTIONS` on a **source file** cannot quiet a **header**. Vendored `datavgen.h` defines
  its classes inline, so it warned inside our TUs, where that flag does not apply — 429
  `-Wreorder` lines survived a setting meant to remove them. `#pragma GCC system_header` in the
  header is what works; it took the same class to 3.
- Silence the vendor, fix ours. Of the same batch, `objinspect.h` and `toolBar.h` were **our**
  headers, and one of them was re-reading `collection.GetID(i)` twice per loop iteration while
  leaving the first read unused.

Still open, in order of weight: **the web is absent from CMake entirely** (`wenterprise-server`
has no `CMakeLists.txt`, `wfrontend` is filtered out of the frontend glob) — so breaking it keeps
all four jobs green; `designer` compiles `mainFrameDesignerCmd.cpp`, a dead legacy main-frame the
`.vcxproj` excludes, which means the two build systems produce different binaries; and 936
`-Woverloaded-virtual` plus ~100 unmarked `override` declarations across 27 property headers.
