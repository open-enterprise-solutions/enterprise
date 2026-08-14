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

### 1.5a A ternary may not mix `wxString` with `const wxChar*`

```cpp
return projection.m_expr ? ibRenderQueryExpr(*projection.m_expr) : wxEmptyString;   // MSVC only
return Chance(2) ? SafeName() : s_reserved[i];                                      // MSVC only
```

MSVC picks a conversion and moves on. **GCC** refuses outright — *"operands to `?:` have different
types `wxString` and `const wxChar*`"* — and **Clang** refuses for the opposite reason, that it can
convert *either* way: *"conditional expression is ambiguous"*. The two toolchains disagree about
why, and agree that it does not compile.

It is easy to write without noticing because neither operand looks like a raw pointer: `wxEmptyString`
is one, and so is any element of a `static const wxChar* []` table. Make both branches `wxString` —
`wxString()` instead of `wxEmptyString`, `wxString(table[i])` instead of `table[i]`.

⚠ `wxT("a") : wxT("b")` is FINE — both branches are the same type. The hazard is only the MIXED
pair, which is why a grep for `: wxEmptyString` finds one shape of it and misses the other. Search
for the CLASS (a ternary whose branches are a wxString-valued call and a literal/table entry), not
for one spelling.

This one shipped four times in a single batch — three in one dialog, one in a new test — and every
one of them compiled clean on `Debug|x86`. It is invisible to the local build by construction.

### 1.5b Two string overloads and a literal — the ambiguity MSVC ranks away

```cpp
recorder.SetString(wxT("a document"));   // MSVC only
```

`wxT("…")` is a `const wchar_t[]`, and the candidates were `SetString(const wxString&)` and
`SetString(ibString&&)`. The literal reaches **either** through exactly one user-defined
conversion, so neither wins: **GCC** — *"call of overloaded `SetString(const wchar_t [11])` is
ambiguous"* — and **Clang** — *"call to member function `SetString` is ambiguous"*. MSVC ranks the
pair and compiles, so this is invisible to `Debug|x86` by construction, like § 1.5a.

Fix it at the DECLARATION, never at the callsite. Declaring the pointer forms makes a literal an
exact match and the ambiguity cannot recur for any future caller:

```cpp
bool SetString(const char* sParam)    { return SetString(wxString(sParam)); }
bool SetString(const wchar_t* sParam) { return SetString(wxString(sParam)); }
```

`ibValue`'s constructors and `operator=` already carried these forms — for the neighbouring trap,
where a pointer with no exact match converts to **bool** (a standard conversion beats the
user-defined one to `wxString`, and `ibValue v = wxEmptyString` became Boolean TRUE). The rule is
the same in both directions: **a type that accepts strings must accept character pointers
explicitly.** The hazard is the CLASS — any overload set holding two string-constructible
parameter types — not the one spelling `SetString`.

### 1.6 The small ones, each of which cost a round trip

| Construct | Why it fails elsewhere |
|---|---|
| anonymous `struct { … };` holding a member with a constructor | an MSVC extension; also, `T* a, b;` declares `b` **by value** |
| `T& x = T()` as a default argument | binds a non-const reference to a temporary |
| `cond ? wxString : wxEmptyString` | the arms must agree; `wxEmptyString` is a `const wxChar*` |
| `enum Foo` in a declaration with no `Foo` in scope | elaborated specifier, legal only if the type is declared |
| `ibValuePtr<T> p = new T()` | the pointer constructor is `explicit`; copy-init cannot use it |
| `wxLongLong_t(0)` — a functional cast | off MSVC the alias is **two words** (`long long`), and only a single-word type name can be spelled `T(x)`; use `static_cast<T>(x)` |
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
| **Windows x64** (MSVC, Ninja) | green, **applications linked** | **919/919** (132 s) | Matches the local `.sln` build exactly. Job ~37 min |
| **Windows x86** (MSVC, `.sln`) | green | — | The shipping build |
| **Linux x64** (GCC 11, libstdc++, wxGTK) | green, **applications linked** | **919/919** (96.6 s) | Identical count to Windows — same tests, same result. Job ~33 min |
| **GUI (Linux, Xvfb)** | green | **26/26** (1.9 s) | `libfrontend.so` links; the runtime-form suite passes under Xvfb |
| **macOS 14 arm64** (Apple Clang, libc++, wxOSX) | green, **applications linked** | **919/919** (163 s) | Same count as the other two — a third toolchain, a third standard library and a second CPU agreeing on every test. **Fastest job of the three: ~22 min against Linux 33 and Windows 37**, on a three-core M1 at `"jobs": 3` — half the parallelism the Windows preset uses. Six defects found on its first runs, listed in § 3 |

**The counts moved, and what they are for did not.** On 2026-08-07 the three general jobs ran
**1166** each and the GUI job **38**. The figure above is kept as the day the matrix first
agreed; the property being watched was never "919" but *the three platforms reporting the same
number*, which they still do. That run was red on all three at once — nine `JobManager` cases
against one engine-side cause, not a portability fault — and the GUI job passed all 38 and then
failed to exit, which is § 4.

**The GUI on macOS is known to work, by hand rather than by CI.** A colleague develops on a Mac:
designer and the enterprise runtime run there, forms render, the app bundles carry their icon.
That is why the port cost six small defects and not a rewrite — the ground had been walked before
CI ever saw it. What the macOS job adds is not discovery but **retention**: it turns "builds on
one machine, when someone remembers to try" into "builds on every commit, for everyone". The
still-missing half is a GUI job there (the harness needs a window server, and the headless story
on macOS is not Xvfb), so on that platform CI covers the build and the backend suite, and a human
covers the screen.

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

- ~~**ARM64.**~~ **Closed 2026-08-03, and this time by execution, not by a build.** The macOS
  arm64 job ran the whole suite: **919/919 in 163 s**, all applications linked. That is the half
  that mattered — everything specific to this CPU is invisible to a compiler and shows up only at
  run time: `char` is UNSIGNED, the memory model is weaker than x86's (a missing atomic ordering
  is no longer hidden by the hardware), and alignment differs where a type's natural alignment
  drove the layout. Ninety-five `ibNumber` tests executed there, `NumberLayout.SizeofIs` and
  `NumberLayout.AlignofIs` among them, so the portable carry fallback — the one MSVC never takes,
  since `_addcarry_u32` covers x86 — is now exercised on real AArch64 rather than merely compiled.

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
all four jobs green. That was not a discovery: [BUILD.md](BUILD.md) has said "the web runtime
targets build under the MSBuild solution only" all along. What is new is only the consequence,
now that CI builds the applications everywhere else; `designer` compiles `mainFrameDesignerCmd.cpp`, a dead legacy main-frame the
`.vcxproj` excludes, which means the two build systems produce different binaries; and 936
`-Woverloaded-virtual` plus ~100 unmarked `override` declarations across 27 property headers.

---

## 4. The GUI job — a hang that is not a portability fault, and a watchdog that watched the wrong process

**The hang.** `oes_frontend_runtime_test` passes every assertion in about three seconds, prints
`Global test environment tear-down`, and then does not exit. Linux/GTK only — the same binary
exits cleanly on Windows, where the whole local suite (1208 with nothing excluded) runs to the
end. The teardown is two lines, `ibWxGuiEnvironment::TearDown` in `tests/frontendFix.h`:
`wxTheApp->OnExit()` then `wxEntryCleanup()`. The second one, as the stack below finally showed.

**Why three runs produced no evidence.** The step bounds the run at 600 s and, on crossing it,
takes every thread's stack — the one thing a hang on a machine you do not have cannot otherwise
tell you. It found its victim like this:

```bash
stuck=$(pgrep -f '[o]es_frontend_runtime_test' | head -n1)
```

Both processes match that. `-f` reads the **whole command line**, and the wrapper's command line
is `xvfb-run -a ./bin/Debug/oes_frontend_runtime_test`; `head -n1` then takes the lower pid,
which is always the parent. So it attached to `xvfb-run` — a shell script — and printed one
frame, `wait4`, waiting for the very process the stack was supposed to describe. It then killed
the wrapper, leaving the real process alive for the runner to reap as an orphan two lines later.
Deterministic, not unlucky: the same thing would happen on the tenth run as on the first.

**The rules this leaves.**

- A watchdog must hold the pid of the process it is watching, not search for it by a pattern
  that its own launcher also matches. The wrapper is now gone — the step starts `Xvfb :99`
  itself, so `$!` **is** the process under test.
- Wait for the display **socket** (`/tmp/.X11-unix/X99`), not for a guess at how long Xvfb takes.
  The harness `GTEST_SKIP`s every test when the toolkit cannot come up, so starting early turns
  the job green while asserting nothing. The step now greps its own output for that skip and
  fails on it.
- When a hang appears after a batch, the cheapest instrument is the same binary with the new
  test sets removed. Reading the code first produced two confident theories that both died on
  inspection — the job manager's tick thread (`DestroyAppDataEnv` stops it, and it predates the
  batch) and queued window deletion (`wx/app.h`: with no event loop running, `Destroy()` deletes
  **directly**) — which is the argument for measuring instead of writing a third.

**The answer (2026-08-07).** With the watchdog attached to the right pid, one run named it:

```
ibWxGuiEnvironment::TearDown  ->  wxEntryCleanup  ->  wxApp::CleanUp
  ->  wxLog::SetActiveTarget(nullptr)  ->  wxLogGui::Flush   (nMsgCount = 21)
  ->  wxLogDialog::ShowModal  ->  gtk_main  ->  poll(timeout = -1)
```

A GUI `wxApp` installs **`wxLogGui`**, and that target does not print warnings — it buffers them
and pours the whole batch into a **modal** `wxLogDialog` when the log target is taken down, which
`wxApp::CleanUp` does with `delete wxLog::SetActiveTarget(nullptr)`. That happens *after* the last
test has passed, so the suite is green, the exit code never arrives, and no assertion is involved
anywhere. It is not a portability fault and not the new tests: any warning at all is enough, and
the batch merely loaded more icons. All 21 messages were the same one — libpng's
`iCCP: known incorrect sRGB profile`.

Three things came out of it, in the order they matter:

- **The harness suppresses the log target too**, not just asserts and CRT reports:
  `delete wxLog::SetActiveTarget(new wxLogStderr())` right after `wxEntryStart`. stderr has no OK
  button, and wx still deletes the target itself in `CleanUp`. The general rule the harness now
  follows: headless, *any deferred flush in a GUI build is a modal window*.
- **The step's shell is `bash -e`** (the Actions default; `set -uo pipefail` in the body does not
  undo it), so `wait "$runner"; status=$?` aborted the script at the `wait` — which is why the run
  that produced the stack printed no `gui.log`, ran no control run, and never reached the
  skipped-suite check. It is `wait "$runner" || status=$?` now. A diagnostic that only runs on
  failure has to survive the failure.
- **The warnings had a source worth removing**: six wxInclude-generated PNGs in
  `frontend/win/editor/codeEditor/res/bitmaps_res.h` carried a Photoshop `iCCP` chunk libpng
  rejects. The chunk is ancillary and carries its own CRC, so dropping it needs no re-encode —
  verified pixel-identical on all six, and the header went from 128 KB to 29 KB. A sweep over the
  other embedded images (71 base64 PNGs, 5 `.png`, 23 `.ico`) found no second carrier.
