# Technology journal — what the engine is doing, while it does it

> **Scope:** the third diagnostic surface — `ibTechJournal` and the `ibJournal*` macros
> (`src/engine/backend/diagnostics/journal.{h,cpp}`): what it answers that the other two do not,
> the one-file rule for `wxLog*`, the line format, lifetime, cost, and how to write a line worth
> reading. Companions: [audit-log.md](audit-log.md) (`ibLogger` — the USER-facing activity log),
> [exceptions.md](exceptions.md) (how a failure travels to a person),
> [debugger-architecture.md](debugger-architecture.md) (the other way to watch a running process).
>
> **Status (2026-08-22):** the layer is in, the rule is stated, and the migration of existing
> callsites is in flight — see §2.

---

## 1. Three surfaces, one question each

They look alike from a distance (all three write text somewhere) and they are not
interchangeable. Each answers exactly one question, and reaching for the wrong one produces a
file nobody can use.

| Surface | Macro / entry | Answers | Who reads it | Where it lands |
|---|---|---|---|---|
| `ibLogger` | `ibLog` | **What the USER did** — login, a document written, a schema applied | administrators, through a viewer in the product | SQLite `.olg` sink, retention sweep ([audit-log.md](audit-log.md)) |
| `ibCrashGuard` | `ibCrashGuard::*` | **What happened at the END** — the fault and the last words | whoever collects the report | `crashdumps/*.dmp`, `*_startup.log`, `*_unhandled.log` |
| `ibTechJournal` | `ibJournal`, `ibJournalInfo`, `ibJournalWarning`, `ibJournalError` | **What the ENGINE is doing, continuously, while it works** | a developer, after the fact | `journal/<exe>_<stamp>_<pid>.log` |

Which to reach for:

- The event is a **business fact** someone may be asked about later — who logged in, who posted
  what, when a schema was applied. That is `ibLog`. It is a product surface: rows, retention, a
  viewer, an audience that is not you.
- The process is **about to stop**, or has stopped. That is `ibCrashGuard`. It runs once, after
  everything else has already lost the ability to speak.
- Everything else — which query ran, which road the fold took, what a source refused to read, how
  long a step took. That is `ibJournal`. Nobody is meant to read it in production; it is what a
  developer opens when the answer is wrong and the code looks right.

The dividing line is the audience. An audit row is written **for someone else**; a journal line is
written **for the next person debugging this**. Auditing engine internals fills the product's log
with noise no administrator can act on; journalling a login instead of auditing it means the fact
is gone in Release, where the journal does not exist at all (§6).

---

## 2. The rule: `wxLog*` is called in ONE file

**`wxLogDebug` / `wxLogMessage` / `wxLogWarning` / `wxLogError` are called in exactly one file —
`src/engine/backend/diagnostics/journal.cpp`, inside the layer.** Every other callsite in the
engine writes the journal.

The translation is one-to-one, and the arguments line up **positionally** — a source tag is added
in front and nothing else changes:

| Before | After |
|---|---|
| `wxLogDebug(fmt, ...)` | `ibJournal(source, fmt, ...)` |
| `wxLogMessage(fmt, ...)` | `ibJournalInfo(source, fmt, ...)` |
| `wxLogWarning(fmt, ...)` | `ibJournalWarning(source, fmt, ...)` |
| `wxLogError(fmt, ...)` | `ibJournalError(source, fmt, ...)` |

```cpp
// before
wxLogError(wxT("cannot open database %s: %s"), path, err.GetErrorDescription());
// after
ibJournalError(wxT("db"), wxT("cannot open database %s: %s"), path, err.GetErrorDescription());
```

So translating a file is **one mechanical replacement per verb**, plus choosing the tag once. No
reasoning about severity, no reordering, no `#include` (§5).

**Status.** As of 2026-08-22 there are 44 `ibJournal*` callsites across 18 files, and roughly 156
`wxLog*` calls still outside the layer. Those are covered meanwhile by a `wxLogChain` installed in
`ibTechJournal::Open` — a **safety net, not the plan**: it catches wx's own diagnostics and the
sites not translated yet, tagging them `wx.error`, `wx.warn`, `wx.debug` and so on, and appending
`[file:line]` when wx knows them. A line under a `wx.*` tag is a line whose callsite still needs
translating. To see what is left:

```cmd
grep -rn "wxLog\(Debug\|Message\|Warning\|Error\)(" --include=*.cpp --include=*.h src/engine
```

The chain **chains, it does not replace**: the previous target stays alive, so the debugger output
window shows everything it showed before. And the global log level is left exactly as it was —
raising it would make new lines appear in the debugger that never appeared before, and the promise
is the opposite one: the same output as always, plus a file.

---

## 3. Why the layer forwards at the SAME severity

`ibTechJournal::Write` writes the file **and** calls the matching wx verb. Not one or the other,
and not everything funnelled through `wxLogDebug`.

The reason is that wx's verbs are not interchangeable in a GUI application:

| wx verb | What the user sees |
|---|---|
| `wxLogError` | a dialog, in front of them, now |
| `wxLogWarning` | a warning surface |
| `wxLogMessage` | a message |
| `wxLogDebug` | nothing — debugger output only |

If the layer echoed everything as debug, then migrating a `wxLogError` callsite to
`ibJournalError` would have journalled the error and **silently taken it off the user's screen**.
The callsite would keep compiling, the file would look better, and the user would stop being told
that something failed. That is the worst class of change: invisible, mechanical, and a regression.

So the mark maps back to the verb it came from:

```cpp
switch (mark) {
case ibJournalMark::Error:   wxLogError  (wxT("%s"), message); break;
case ibJournalMark::Warning: wxLogWarning(wxT("%s"), message); break;
case ibJournalMark::Info:    wxLogMessage(wxT("%s"), message); break;
default:                     wxLogDebug  (wxT("%s"), line.Left(line.Len() - 1)); break;
}
```

**The user sees the message; the file gets the timestamped line.** A timestamp and a thread id
belong in a file, not in a dialog. The debug echo is the exception — it carries the full line,
because there the timestamp is the point.

Two mechanics worth knowing before touching this:

- **The echo happens outside the file lock.** wx takes locks of its own; holding two in one order
  here and the other order there is how a deadlock is built.
- **The loop is broken by a per-thread flag**, not by bypassing wx. The echoed line would
  otherwise come straight back through the chain into `Write`; instead the echo announces itself
  (`thread_local bool s_echoing`) and the chained target lets that line pass. Per-thread, because
  two threads may be writing at once and one must not silence the other.

---

## 4. The line

```
! 14:22:07.318  t9284   query.stitch    composed 63 rows, holds [...] carries out [...]
^ ^             ^       ^               ^
| time (ms)     thread  event source    detail
mark
```

The format is `"%c %s  t%-5lu  %-14s  %s\n"`: mark, `HH:MM:SS.mmm`, thread id, source, message.

**The mark is a LEVEL, not decoration.** A reader — and later a filter — can keep the marked lines
and drop the rest without parsing anything else:

| Mark | `ibJournalMark` | Written by |
|---|---|---|
| `!` | `Error` | `ibJournalError` |
| `?` | `Warning` | `ibJournalWarning` |
| `*` | `Info` | `ibJournalInfo` |
| (blank) | `Plain` | `ibJournal` |

**Blank matters.** The ordinary running commentary is most of the file, and it deliberately gets a
space rather than a third letter: `!` is findable in a wall of ten thousand lines only because the
column is empty everywhere else. A `.` or a `-` there would cost exactly the property the mark
exists for.

Time is to the millisecond because ordering two events and measuring the gap between them is most
of what this file is for. The thread is on every line because the same message from two threads is
two different stories. Every line is flushed as it is written: a crash must not cost the lines that
explain it.

The file opens with what is running, under the `start` tag, before anything can go wrong — binary,
product version, **build number**, configuration, architecture, compile date, OS, wxWidgets
version, pid, locale, user, host, working directory, the binary's path and the journal's own path.
A journal whose first line is already a symptom is half useless: the next question is always
"which build, on what, since when", and by then the person asking is not at the machine any more.

The build number is days since 2020-01-01, derived from `__DATE__` of `journal.cpp` — the day the
engine was compiled. Two binaries both calling themselves `1.0.1` are the same release and may be
different code; this is what tells them apart in a report, and it needs no build system to
maintain it.

---

## 5. Lifetime and location

**Opened by `ibCrashGuard::Install`** — the first act of every application in the tree, before any
handler is registered and long before a database is chosen or a session is made.

It sits **above `ibApplicationData` and is deliberately not owned by it**. Owning it there would
mean the journal starts after the part of the run most worth journalling: bring-up, which is where
"nothing happens and nothing is said" is most common. It ends with the process, also deliberately
— a line written during teardown, after wx has torn its own logging down, still lands.

| Property | Value |
|---|---|
| Instance | `ibTechJournal::Instance()`, a function-local static; all writes go through statics |
| Opened from | `ibCrashGuard::Install(exeName)`, guarded `#ifndef NDEBUG` |
| File | `<exeDir>/journal/<exe>_<stamp>_<pid>.log`, stamp `%Y%m%dT%H%M%S` |
| Beside | `<exeDir>/crashdumps/` |
| Closing | `ibTechJournal::Close()` — optional; the process may simply end |
| Query | `ibTechJournal::IsOpen()`, `ibTechJournal::Path()` |

**One file per run, keyed.** A single overwritten file loses exactly the run a person is reporting
the moment they restart to try again. The key is the same stamp-and-pid pair the dump files carry,
so a dump and the journal explaining it are found together in one directory — the dump says where
it stopped, the journal says what it was doing.

**Failure to open is silent by design.** A machine with no writable directory beside the binary
must still run: `IsOpen()` then answers false and every write is a no-op.

**No include is needed.** `journal.h` is included from `backend_core.h`, last, so `ibJournal(...)`
is available in every file of the engine the way `_()` is. A diagnostic that has to be arranged
for is a diagnostic nobody writes at the moment they need it.

---

## 6. Debug only, for now

In a Release build the four macros expand to `((void)0)` — **arguments included**, so nothing is
evaluated, nothing is formatted, and nothing is linked in.

```cpp
#ifdef NDEBUG
#	define ibJournal(source, ...)  ((void)0)
#else
#	define ibJournal(source, ...)  ibTechJournal::Print(ibJournalMark::Plain, (source), __VA_ARGS__)
#endif
```

A callsite compiles either way and needs no `#ifdef` of its own.

**Why Debug only:** a developer running a Debug binary has already consented to a file being
written beside the binary. A Release journal needs decisions that have not been made — how much
disk it may take and who cleans it up, what may appear in it given that lines carry user data and
identities, and who collects it when a customer reports something. Those are support and privacy
decisions, not a coding one, and the switch will be added when there is a Release story to attach
it to. The API exists in both builds so that nothing has to change at the callsites when it is.

---

## 7. What a line costs

**The gate is one atomic read, tested BEFORE formatting.** `IsOpen()` is a relaxed load of a
`std::atomic<bool>` — an atomic and not the file handle, because looking at the handle needs the
lock and the whole point is to decide without taking one. With the journal off, a callsite costs a
load, a branch and a return; the format string is never expanded, nothing is allocated, and
nothing is converted.

**Arguments are not evaluated when the journal is closed** in Release (the macro is gone). In
Debug they are evaluated at the callsite as ordinary function arguments — so a cheap argument is
free and an expensive one is not. When building the argument itself is expensive (concatenating a
column list, walking a container), guard the whole block:

```cpp
if (ibTechJournal::IsOpen()) {
	wxString have;
	for (const ibQueryRamColumn& c : TC.Columns())
		have += (have.IsEmpty() ? wxString() : wxT(", "))
		      + wxString::Format(wxT("%s#%u"), c.m_name, static_cast<unsigned>(c.m_id));
	ibJournal(wxT("query.stitch"), wxT("composed %ld rows, holds [%s]"), rows, have);
}
```

**The format string is checked against its arguments at compile time.** `Print` is declared with
wx's `WX_DEFINE_VARARG_FUNC`, the same machinery every raise in this codebase uses. Two things a
hand-rolled parameter pack does not give: that check, and the right implementation in both wx
builds (wchar and UTF-8) without the callsite knowing which one it is in.

Writes are serialised by a `wxCriticalSection`, which is what makes two threads' lines two lines
instead of one shredded one.

---

## 8. An error takes a dump with it

`ibJournalError` writes the line, echoes it at error severity, and then calls
`ibCrashGuard::WriteDumpNow(wxT("_error"))`.

The reason: by the time anyone reads the line, the state that produced it is gone — the stack has
unwound, the objects are destroyed, the thread has moved on. The dump is the only way to keep it,
and the machinery for writing one already exists for crashes. It is a **reduced** minidump on
purpose — threads, stacks, handles, unloaded modules, indirectly referenced memory; **not** full
memory. The crash path takes full memory because it is the last thing that will ever happen; this
one runs in a process that is still working and may run while a user waits, so it is worth having
often rather than worth having complete. Windows only for now: the POSIX side writes a backtrace
from its signal handler and has no equivalent "snapshot me" call.

Two limits, both deliberate:

- **Once per run.** A failure that happens once happens in a loop soon after, and a snapshot per
  iteration would fill a disk while the program is still trying to work. The first one is the one
  worth having: it is the failure before anything downstream reacted to it. A `std::atomic<bool>`
  exchange enforces it, and a follow-up `*` line records where the dump went.
- **It does not terminate the process.** A log verb that can kill the process is a trap — someone
  will put it inside a loop, or on a path that recovers perfectly well. Deliberate abort stays a
  deliberate call: `ibCrashGuard::TerminateProcessFast`, written where the decision is actually
  made.

For the same reason, a `wxLogError` arriving through the chain is recorded with the **`?` mark,
not `!`**: the chained target sees every `wxLogError` in the process, including ones a caller is
about to catch and handle, and a snapshot per handled error is exactly the loop the once-per-run
rule exists to avoid.

---

## 9. How to write a line worth reading

**Name the source in dotted form.** The tag is a subsystem, narrowing left to right: `query`,
`query.compose`, `query.stitch`, `query.walk`, `db.firebird`, `job.register`, `session`. The
column is 14 characters wide, so short segments stay aligned. Dotted form is what makes a file
greppable by area (`grep " query\." journal.log`) without a filter language.

**Record decisions and refusals, not just successes.** A refusal is invisible by construction: a
column no source claims is simply absent from the composed rows, and everything downstream reads
it as empty — a dimension that folds every row under one blank key, a figure with nothing under it
to explain itself. Nothing throws, nothing is logged, the report is merely wrong. The journal is
where a refusal becomes visible, which is most of its value.

**Record counts and identities.** A reader must be able to tell *"nothing happened"* from
*"everything was dropped"*, and those two look identical in a result. So write how many, and
which:

```cpp
ibJournal(wxT("query.compose"), wxT("source %s: claimed [%s]  REFUSED [%s]"), name, claimed, refused);
```

Identities beat names alone. `Value#1005` says which `Value` — the one with metaID 1005 — and two
columns that print the same name are then two different lines rather than one confusing one.

**Do not journal in a tight per-row loop.** The journal flushes every line to disk. A line per row
turns a 60 ms read into a 6 s one and buries the lines that matter under a hundred thousand that
do not. Journal the loop, not the iteration: what it was given, what it produced, what it skipped.

**Write what happened, in the words of the subsystem.** Not `"error in compose"` but
`"composed 63 rows"`. The first is a verdict, and a verdict is only useful if it is right; the
second is a fact, and a fact is useful even when the reader's theory is wrong.

---

## 10. A worked example — 2026-08-22

A query over **two** sources produced a report whose totals columns were empty. Two attempts to
find the cause by reading code produced two wrong answers: first the fold, then the report's
output binding. Both were plausible, both were wrong, and each cost a build and a run to disprove.

One journal line, from `query.stitch`, ended it:

```
composed 63 rows, holds [Value#1005, Posted#1026, Number#1024] carries out [Value#1005]
```

Sixty-three rows were composed, so the read worked. The stitched table **held** three columns and
**carried out** one. The totals columns were not empty; they were never projected out of the RAM
stitch. The defect was in the projection, in neither of the two places that had been read, and the
line named it in a single glance.

The point is not that the journal found a bug. It is that the journal answers **"what is actually
happening"**, while reading code answers **"what could be broken"** — and the first question is
usually the more useful one, because it is answered by the machine rather than by a theory. Two
silent filters sat between the leaves claiming their columns and the fold reading them: the join's
own column list, and this projection. A column dropped by either is indistinguishable downstream
from a column that was read and found empty. That is precisely the shape of defect that a running
commentary catches and a stack trace does not, because nothing failed.

---

## 11. Quick reference

```cpp
ibJournal       (wxT("query"),       wxT("run: %s"), name);                     // blank mark
ibJournalInfo   (wxT("session"),     wxT("opened %s"), user);                   // *
ibJournalWarning(wxT("db.firebird"), wxT("retrying after %d ms"), delay);       // ?
ibJournalError  (wxT("job.register"),wxT("job %s failed: %s"), job, reason);    // ! + one dump

ibTechJournal::IsOpen();     // gate an expensive argument block
ibTechJournal::Path();       // the run's file, absolute; empty if it never opened
ibTechJournal::Close();      // flush + close; optional
```

| Do | Do not |
|---|---|
| `ibJournal*` at every callsite | call `wxLog*` outside `journal.cpp` |
| dotted source tags (`query.compose`) | one tag for a whole DLL |
| log refusals and skips | log only the happy path |
| counts and identities (`Value#1005`) | `"done"`, `"ok"`, `"error"` |
| guard expensive argument building with `IsOpen()` | journal per row |
| `ibLog->Audit(...)` for business events | journal what an administrator must be able to read |
