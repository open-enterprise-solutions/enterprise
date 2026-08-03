# 25. Hunting Memory Leaks

The Debug build prints `Detected memory leaks!` and a block dump on exit. This chapter is the
procedure for turning that dump into a fix, the list of things in it that are **not** leaks, and
the traps that cost real time the first time round.

The dump comes from wxWidgets (`src/common/init.cpp` sets `_CRTDBG_LEAK_CHECK_DF`), so it fires
from the CRT's own exit path, after every `atexit` handler and every static destructor. It is
real, not an artefact of static teardown.

---

## The two instruments

| | tells you | when it prints |
|---|---|---|
| CRT dump | **what survived** — size, contents, allocation ordinal | last, after every module has been torn down |
| `leak-track` | **who allocated it** — full call stack | inside `OnExit`, *before* window teardown |

Neither is usable alone.

- The tracker **over-reports**, because wx windows and wx's own caches are still alive when it
  prints. Measured: tracker 4618 live, dump 2273 — the 2345 difference was entirely GUI objects
  and wx caches freed afterwards.
- The dump **under-explains**: no stacks, only sizes and ordinals.

**Cross-reference them.** A tracker group of N frames matching a count of N blocks of one size in
the dump is a real finding. A tracker group with no counterpart in the dump is a teardown-timing
artefact — ignore it.

---

## The tracker

Built into `designer/mainApp.cpp`, Debug + MSW only, dormant unless an environment variable turns
it on. Set them in **Project properties → Debugging → Environment**.

| variable | effect |
|---|---|
| `OES_TRACK_SIZE=116,32,64` | record a call stack for every allocation of these sizes |
| `OES_TRACK_ALL=1` | same for every size — complete, much slower |
| `OES_TRACK_ORDINAL=423758,424168` | print the stacks of **these exact blocks** and nothing else |
| `OES_TRACK_TAIL=1` | print every allocation made **after** the report — i.e. during teardown |
| `OES_BREAK_ALLOC=4429052` | break in the debugger on that allocation |

How it works: `_CrtSetAllocHook` captures a stack for each watched allocation into a hash keyed by
allocation ordinal; the free hook removes the entry; `ibLeakTrackReport()` prints whatever is left,
grouped by call site. Bookkeeping lives on a **private Win32 heap** — records must not appear in
the dump they exist to explain, nor re-enter the hook that writes them.

It lives in `backend/diagnostics/leakTracker.{h,cpp}`, so **every** binary that links backend has
it: enterprise, designer, codeRunner, launcher, daemon, wenterprise-server. Each one arms it with a
single line next to its entry point:

```cpp
#include "backend/diagnostics/leakTracker.h"

IB_LEAK_TRACKER_ARM();
```

Two details in that macro are load-bearing. It expands at **static-init** time, because both hooks
have to be armed before wxWidgets allocates anything — `OnInit` is already too late. And the
`std::atexit` call expands **in the executable**, not inside `ibLeakTrackArm`: MSVC gives every DLL
its own onexit table, so an `atexit` called from `backend.dll` would run at backend's
`DLL_PROCESS_DETACH` instead of at the exe's exit, which is not where the report was measured to
belong.

In Release the macro expands to nothing at all — no static, no hook, no `dbghelp`/`psapi`. The whole
module is `#if defined(DEBUG) && defined(__WXMSW__)`, and the dump it explains is a debug-CRT
feature that does not exist in a release build either.

### The object traces are switches too

`ibValue` and the type factory carry Debug-time traces of their own. They are **off by default**,
for the same reason the tracker is: they were left on once, and one designer run produced ~18000
lines of them — 72% of the log, enough that the lines worth reading could not be found.

| variable | effect |
|---|---|
| `OES_TRACE_VALUES=1` | every `ibValue` construction / destruction, with the live count |
| `OES_TRACE_TYPES=1` | every value-ctor registered into / removed from the factory |

The `ibValue` **counter** keeps running either way — it costs one atomic, and it is what makes
"how many are alive?" answerable without a rebuild. Only the printing is conditional.

`ibPropertyObject` used to carry a third one, with a live register behind a mutex. It answered its
question once (`none alive — clean teardown`) and was removed: a mutex and a set on every
construction, in every Debug run, to re-answer a settled question is the wrong trade — and an empty
exit dump answers it better, for every type at once.

### Read the second line first

```
=== leak-track: 4618 block(s) alive in 256 call site(s) (recorded 293177, released 288559) ===
    request offset -8; frees seen 4007049: no request 0, not ours 3718490, matched 288559
```

`released` must be comparable to `recorded`. If it is 0 or near it, free tracking is broken and
**the whole report is noise** — every allocation looks like a leak. The breakdown names the step
that is losing the frees.

### `PARTIAL` means the listing is incomplete

```
    PARTIAL: 6103 block(s) not listed - more than 256 distinct call sites;
    use OES_TRACK_ORDINAL=<n,n,...> to ask for specific blocks
```

The group table is capped. Rare blocks — which is exactly what survives to the dump — lose the
race for a slot to frequent ones. When this line appears, do not read the ranking; ask by ordinal.

### Ordinals drift between runs

An ordinal is a counter of allocations since process start, so it only lines up between runs that
allocate identically. Observed drift: **+4 on early blocks** between two runs of the same scenario,
much more on late ones if the scenario differed. Practical rule:

1. run, take the ordinals from **that run's** dump;
2. re-run with the same scenario **and the same `OES_TRACK_SIZE`** (changing the size list changes
   the allocation count and moves every ordinal), adding `OES_TRACK_ORDINAL`;
3. expect partial hits on the late ones; repeat for the misses.

---

## Procedure

### 1. Read the dump's shape, not its lines

```bash
grep -o '[0-9]* bytes long' log.txt | sort -n | uniq -c | sort -rn
```

A leak is a repeated size. One dominant size with a stable count per scenario is one object type.

### 2. Decide whether it grows

Run twice — once minimal (open, close), once heavy (several editors, forms, modules). If the count
for a size does not move, it is a one-shot cache and not worth chasing. If it tracks what you
opened, that is the leak.

### 3. Get the stack

`OES_TRACK_SIZE=<the size that grew>`. Then cross-reference with the dump.

### 4. Fix, rebuild, compare the histogram

Success is the size disappearing from the histogram — not the banner disappearing. See *Baseline*.

---

## What is in the dump and is NOT a leak

### The string pool free list

`ibFStringPool` (`backend/fstring.h`) caches freed blocks for reuse — up to 128 per size class
(16/32/64/128/…) per thread. `Deallocate` writes `next` into the first word and touches nothing
else, so a cached block sits in the dump **holding its old contents with the first four bytes
overwritten**. That is where dumps full of metadata-name fragments (`…rmObject`, `…rmList`,
Cyrillic mid-word) came from: freed strings, not live data.

Recognise it by:

- sizes exactly equal to the pool's classes;
- counts pinned near 128 per class;
- 16-byte blocks reading `<ptr> 00000000 CD CD CD CD CD CD CD CD` — `CD` is MSVC's never-written
  fill, so only the first word was ever touched. (A 16-byte block that is *all* `CD` after the
  first word is usually a `std::_Container_proxy`: 8 bytes rounded up to the 16 class.)

⚠ Size alone does not tell a cached block from a live one: a **live** `ibString` buffer is
allocated through the pool too, so it has exactly the same class size. A 16-byte block that is all
`CD` past the first word is usually a live `std::_Container_proxy` (8 bytes rounded up), not a free
node. Read the stack, not the size — this cost one wrong conclusion.

Two drains keep the pool out of the dump, split by which thread they can reach:

1. `ibFStringPool::Drain()` at the end of `ibApplicationData::DestroyAppDataEnv` — **the main
   thread**, and the only way to reach it. The thread that calls `exit()` detaches the *process*,
   never itself, so no thread-exit hook fires for it on any platform. Measured 291 → 30 blocks.
2. thread exit — **every other thread**, including those we neither own nor can join (Firebird's,
   wx's). The cache is per thread, so a thread that ends carries it off: the blocks stay allocated,
   unreachable, and are attributed in the dump to whoever first allocated them. Every surviving
   string block traced back to the session-registry thread, which builds the INSERT for a session
   row and then exits. Measured 30 → 14 blocks.

   The hook differs per platform, and that is deliberate — see *Other platforms* below.
   `DLL_THREAD_DETACH` in `backend.cpp`'s `DllMain` on MSW (safe under the loader lock: it returns
   blocks to the CRT heap and does nothing else); `~ThreadPool` elsewhere.

There used to be a **third**: an `atexit` handler registered under `#pragma init_seg(lib)`, on the
theory that statics hand buffers back after `DestroyAppDataEnv` has already drained. It measured 30
blocks before and 30 after — it never freed anything — and it is gone. The ordering recipe is sound
and still written up below; it simply had no work to do here. A cleanup that cleans nothing is a
claim that something needed cleaning.

⚠ The pool is `thread_local` **and per-module**: an `inline` variable is one instance per TU set,
so `backend.dll`, `frontend.dll` and `designer.exe` each carry their own. A drain only empties the
module it is compiled into.

### wxWidgets' language database

`wxUILocale::InitLanguagesDB`, reached from `ibApplicationData::InitLocale`, builds a table of every
known language — well over a thousand allocations. wx frees it in its own module cleanup, which
runs **after** the tracker report and **before** the CRT check: it appears in the tracker and never
in the dump. Nothing to do, and nothing to unload by hand — the table is wx's own.

### Per-thread state of a thread that outlives shutdown

A thread that is still running at process exit is **killed without TLS teardown**: its
`thread_local` objects are never destroyed, and whatever they hold stays in the dump. Six blocks of
one shape usually means six such threads, not six leaks.

Two costs hide here, and only one of them is the obvious one:

- the object's own heap (a `thread_local` vector's buffer);
- an **8-byte registration node per `thread_local` with a non-trivial destructor**. MSVC's
  `__dyn_tls_init` constructs those objects at `DLL_THREAD_ATTACH` — for **every** thread, used or
  not — and registers a destructor for each. The stack points at the variable's declaration, which
  reads like a call site and is not one.

The second dominates, and it scales with the **number of objects**, not with whether they touch the
heap. Measured 2026-07-30: replacing `thread_local std::vector<wxString>` with a fixed
`std::array<wxString, 64>` — an attempt to remove the buffer — turned 6 leaked nodes into 385, one
per array element per thread. The vector was the cheaper shape all along.

Two ways out, and the second is usually the right one:

- make the `thread_local` **trivially destructible**, which registers no node at all.
  `g_refReadStack` in `reference.cpp` became a plain array of `std::pair<ibMetaID, ibGuid>` for
  exactly that reason, and lost its allocation on the reference-read path as a bonus. A container of
  `wxString` cannot follow.
- **give the state an owner.** `tl_errorChain` in `backend_exception.cpp` is now one static
  `std::unordered_map<std::thread::id, std::vector<wxString>>` behind a mutex — the same shape as
  the current-session binding in `session.cpp`. Per-thread state has no owner, and nothing without
  an owner can be cleaned up; a static has one, and the CRT destroys it at exit. The lock is on the
  error path, which is by definition not hot.

A static that outlives its users needs a **trivially destructible `alive` flag declared before it**,
so a thread still running during teardown reads the flag, sees the owner is gone, and drops its
work instead of touching a destroyed container.

### Anything allocated during teardown — and wx's log target

Blocks allocated **after** the tracker's report are out of reach of every mechanism above: the table
is printed, and they cannot be asked for by ordinal because the ask needs a previous run to read the
number from, and setting the variable moves the number. `OES_TRACK_TAIL=1` prints each allocation as
it happens instead.

That mode carries a hard rule: **it records, it does not name.** Symbolising during teardown kills
the process — dbghelp runs against a CRT that `exit()` is dismantling and reaches `abort()`
(`c0000409`), and `__fastfail` bypasses SEH, so there is nothing to catch and no second chance to
print. Three runs died that way before the rule was written down. The tail therefore prints raw
frame addresses, the report prints the module table while printing is still safe, and the names are
recovered afterwards — load the crash dump (or any dump from the same boot, since ASLR bases hold)
in `cdb` and `ln <address>` each frame.

What that found: the last two blocks were a `wxLogOutputBest` (8 bytes: vptr + `m_formatter`) and
its `wxLogFormatter` (4 bytes: vptr). `wxEntryCleanup` deletes the active log target and then
deliberately leaves auto-vivification on — `init.cpp` says in so many words that leaking beats
losing a message logged from a static dtor. Every log call after cleanup then builds a target nobody
owns, and registrar teardown does log that late.

The trade is a false one, so we take the other side: with `wxLog::DontCreateOnDemand()` at the end
of `OnExit`, `wxLog::GetMainThreadActiveTarget` falls back to a **static** `wxLogOutputBest`
(`log.cpp`). Late messages still print; they just stop coming from the heap. This is not a wx bug —
it is a wx decision — so the override lives in the app, not in the fork.

### The object inspector

`ibObjectInspector::Create` calls `m_pg->Clear()` before every rebuild, and the grid owns the
properties. Inspector stacks in the tracker are windows still alive when it printed.

---

## Ordering: running a cleanup *last*

The problem this solves: an explicit call anywhere in application shutdown still happens before
static destructors, so anything a static hands back afterwards arrives too late to be cleaned up.

MSVC keeps static destructors and `atexit` handlers in **one LIFO list**. So *registering first
means running last*. To register first inside a DLL:

```cpp
#pragma warning(push)
#pragma warning(disable: 4073)   // init_seg(lib) is reserved for library code — this IS the library
#pragma init_seg(lib)            // construct before every ordinary static in this DLL
#pragma warning(pop)

namespace {
struct ibStringPoolDrainAtExit {
    ibStringPoolDrainAtExit() { std::atexit([] { ibFStringPool::Drain(); }); }
} s_stringPoolDrainAtExit;
}
```

`DllMain(DLL_PROCESS_ATTACH)` is **not** an option for this: the CRT constructs the DLL's statics
before calling it, so a registration there is already too late.

⚠ The recipe is written up because it is worth knowing, **not** because it is in use. It was added
for the string pool and measured to free nothing (30 blocks before, 30 after), and removed again.
Reach for it when a measurement shows something arriving after the last cleanup — not on the
suspicion that something might.

---

## Other platforms

The dump, the tracker and everything under `_CrtSetAllocHook` are MSVC's — all of it lives under
`#if defined(DEBUG) && defined(__WXMSW__)` and compiles out elsewhere. On Linux and macOS the same
four-step procedure holds; only the instrument changes:

| here | there |
|---|---|
| the CRT exit dump | LeakSanitizer (rides in with ASan) |
| `OES_TRACK_SIZE` + tracker report | ASan's own allocation stacks, or valgrind `--leak-check=full` |
| `OES_BREAK_ALLOC` | a conditional breakpoint on the allocator |

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DOES_SANITIZE=address
```

⚠ On MSVC that switch is **not** the same thing: `/fsanitize=address` finds overflows and
use-after-free and ships no leak detector at all. Windows leaks are the debug CRT's job. CMake says
so out loud rather than let the flag imply cover it does not give.

What must not differ is the **product** code, and one thing genuinely does. The per-thread string
pool is drained on thread exit through `DllMain`'s `DLL_THREAD_DETACH`, which exists only on
Windows. Without a counterpart, every thread that ends off Windows keeps its cache forever — and
`wenterprise-server` spawns threads per session, so it accumulates exactly where it costs most.

So off Windows `ThreadPool` has a destructor and drains itself on thread exit; on Windows it stays
trivially destructible and `DllMain` does the job. That asymmetry is not an oversight — a
`thread_local` with a destructor costs an 8-byte registration node per thread on MSVC, and that node
is lost when a thread is killed at process exit. Each platform gets the mechanism that is free on
it.

The rest carries over untouched: the static error registry, the factory's function-local static, and
`wxLog::DontCreateOnDemand()` are plain C++ and plain wx.

---

## Traps in the tools themselves

### Nothing the free hook is handed identifies the block

If you extend the tracker, know that on `_HOOK_FREE` the CRT passes:

- `lRequest` — **not** the number it passed on `_HOOK_ALLOC`;
- `blockType` — `_UNKNOWN_BLOCK`, not the type stored in the header;
- `nSize` — **always 0**.

The number lives in the block header, whose layout is undocumented and bitness-dependent. Do not
hard-code it: three allocations in a row carry three consecutive request numbers, so the offset is
the slot reading *n, n+1, n+2*. Derive it before installing the hook. (It resolves to −8.)

Each of those three cost a debugging round. The lesson is procedural: **when a mechanism swallows
everything silently, add a counter per failure mode instead of guessing again.** The breakdown line
named the culprit on the first run after it existed.

### `_CrtSetDumpClient` does not work here

It is only called for `_CLIENT_BLOCK`. Ours are `_NORMAL_BLOCK`.

### No silent caps

Any limit in a diagnostic — group table, ordinals per line, frames captured — must say when it
truncated. A report that looks complete and is not costs more than no report: the 256-group cap
hid every one of the 30 surviving blocks and made the listing look exhaustive.

### Where to print "who is still alive"

Only past `wxDELETE(s_instance)` in `ibApplicationData::DestroyAppDataEnv`. Not at the end of
`~ibApplicationData`'s body — members are destroyed after the closing brace, so a report there
counts objects the field sweep is about to destroy. Measured: 510 false positives.

### Stacks start with ten frames of boilerplate

`malloc` → `operator new` → the `xmemory` / `xstring` / `vector` internals. The tracker drops the
leading run of frames whose file lives under the MSVC headers or the CRT sources, so the stack
starts at the first line that is ours. Capture depth is 32 to leave real depth after the cut.

---

## Ownership rules that produced real leaks

- **`wxWindow::PopupMenu()` does not take ownership** and blocks until dismissed → put the menu on
  the stack. `new` + `delete` is a crutch over an allocation that should not exist. `new wxMenu` is
  correct in exactly two places: submenus passed to `AppendSubMenu`, and menus appended to a
  `wxMenuBar` — both transfer ownership.
- **A raw pointer handed to a constructor documented "lifetime is owned by the caller" needs a
  caller that owns it.** `ibCompileContext::PushFunction` created a context nobody owned and nobody
  read — one 116-byte leak per registered context method, per compile, and `SyntaxControl`
  recompiles on document **close**. The fix was to stop creating it.
- **A base class you `delete` through needs a virtual destructor — and an undo stack deletes through
  its base.** `ibVisualEditorCmd` had `virtual DoExecute()` and no virtual destructor, so
  `wxDELETE(m_undoStack.top())` ran the base destructor only. The command object itself is a few
  dozen bytes; what it *holds* is an `ibValuePtr<ibValueFrame>`, and a skipped `~ibValuePtr` is a
  control that never gets its `DecrRef`. One drag-and-drop of a Button leaked its sizer-item, its
  button and every property they own — 390 blocks, reproducible block-for-block. Two sibling
  commands also derived `wxEvtHandler`, so it was undefined behaviour as well as a leak. The same
  defect sat in `ibFormEditorCmd`, where MSVC had been saying so all along (**C5205** — "deleting an
  abstract class with a non-virtual destructor"): a warning naming a class you are actively
  debugging is not background noise.
- **A stack that owns its elements owns them on EVERY exit path, not just the destructor.** The same
  processor popped the redo branch bare in `Execute` and popped both stacks bare in `Reset` — two
  more leaks of exactly the same shape, each behind an ordinary user action (undo then do something
  else; reload the form). `Reset()` IS the destructor's body; writing it twice is how they drifted.
- **`wxRefCounter` starts at 1, so `new` already IS your reference.** An `IncRef()` right after it
  is a second reference nobody holds and nobody drops. `ibDialogPredefinedEditor` did exactly that:
  `new` (1) → `IncRef` (2) → `AssociateModel` (3) → `~wxDataViewCtrl` (2) → the dialog's `DecRef`
  (1), and the model outlived the dialog. `IncRef` belongs on a pointer you are *storing a second
  time*, never on one you just created.

---

## Baseline

Record what a clean exit looks like, so a regression is a number and not a feeling.

| date | dump | what changed |
|---|---|---|
| 2026-07-30, before | 2273 blocks | 198 of them 116 bytes and **growing with use** — compile contexts |
| after the context fix | 291 | growth gone; the rest was the string pool |
| after the in-app drain | 30 | main thread's pool cache returned |
| after the `atexit` drain | 30 | no change — nothing was left for it there |
| after `DLL_THREAD_DETACH` drain | 14 | every string block gone: they were dead threads' caches |
| after `g_refReadStack` → fixed array | 8 | its TLS destructor registration gone with it |
| after `tl_errorChain` → static registry | 2 | the other six registration nodes gone |
| after `wxLog::DontCreateOnDemand()` | **0** | no dump printed at all |
| 2026-08-03, designer + one drag-and-drop | 390 | the tripwire firing: undo commands deleted through a base with no virtual destructor |
| after the virtual destructors | **0** | dump empty again on the same scenario |

**The dump is empty, and that is the point.** Not because 12 bytes mattered, but because an empty
dump is a tripwire: from here, any block at all is a regression with a name, and the four steps
above start from a signal instead of from noise. Keep it empty.

It started paying immediately. The first run after the cleanup showed a 28-byte block with an early
ordinal — a mid-run allocation, not a teardown one. `OES_TRACK_SIZE=28` named it in a single run:
`ibDialogPredefinedEditor`'s data model, leaked once per open of the predefined-values editor
because of the `IncRef` above. It had been sitting under 2273 blocks of noise the whole time, and
went from invisible to a one-line question.

Note what the same run also showed and what it means: two `wxEventHashTable::InitHashTable` groups
appeared in the **tracker** and not in the **dump** — wx frees them in its own cleanup, which runs
after the report. Tracker-only is not a leak; the dump is the arbiter.

There is no "structural floor" — that was said twice during this hunt, at 14 blocks and again at 8,
and both times the next measurement disproved it. State what has been measured; do not promote it to
a limit.

**A baseline is per SCENARIO, and the July zero was measured on open-and-close.** The first run that
did real work in the form editor printed 390 blocks — not a regression against that zero, just the
first look at a path the zero never covered. The tripwire still did its job: 390 blocks, one
`OES_TRACK_SIZE` run, and the stack named `ibCommandDragItem::ApplyDrop`. Two scenarios now read
zero; every other one is unmeasured until someone runs it, so treat a fresh non-zero dump as
"untested path", not "broken yesterday" — and then fix it, because zero is reachable there too.
