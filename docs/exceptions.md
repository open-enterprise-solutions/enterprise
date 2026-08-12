# Exceptions — the boundaries, not the class list

> **Scope:** how a failure travels from where it is raised to where a person reads it, and what
> every boundary in between is obliged to do. Classes are named only where the rule needs them.
> Companions: [../CLAUDE.md](../CLAUDE.md) §5 (the throw/catch shape),
> [diagnostics.md](diagnostics.md) if present, [compiler-pipeline.md](compiler-pipeline.md)
> (compile vs runtime errors).

---

## 1. The mistake this document exists to prevent

For most of this engine's life there were **two exception hierarchies, and they never met**:
`std::exception` — what the language, the standard library and every third-party dependency throw
— and `ibBackendException`, what *this* code throws, deriving from nothing.

Generic code cannot know about the second one. What a boundary writes is
`catch (const std::exception&)`, and everything else falls into `catch (...)`, which by definition
has no object to ask. So a project exception carrying an exact sentence arrived at every generic
boundary as *nothing at all*.

The cost was paid on 2026-08-12: the session-registry thread caught a failure, called its
fail-stop, and killed the process reporting **"registry-thread unknown exception"** — while the
exception in its hands was holding `Object 'ConstantObject.Attribute3' is not exist`. The message
that named the defect was discarded at the exact moment the process decided to stop.

**Fixed at the root:** `ibBackendException` now derives from `std::exception` and implements
`what()`. Not because anything in the class needed the standard base, but because everything
*around* it assumes one.

Two consequences worth stating plainly:

- A `catch (const std::exception&)` anywhere — a thread body, an event handler, a library callback
  — now sees the description instead of losing it.
- **Ordering became load-bearing.** Any handler that lists both must put `ibBackendException`
  first, since it is now the derived one. Every site in the tree already did (audited at the time
  of the change); a new site that gets it backwards will compile and silently stop reaching its
  own handler.

---

## 2. The shape of a throw

Thrown **by value**, caught **by const reference** — plain C++, no pointers, no `new`:

```cpp
ibBackendCoreException::Error(_("Object '%s' is not exist"), className);   // formats, then throws
throw ibBackendInterruptException();

catch (const ibBackendAccessException& err) { … }   // derived first
catch (const ibBackendException& err)       { … }   // base second
catch (const std::exception& e)             { … }   // standard base LAST of the named
catch (...)                                 { … }   // only where §5 allows
```

`ibBackendException` has a virtual destructor, so catching by base keeps `dynamic_cast` and
re-throws well-defined. A re-throw uses bare `throw;` — it propagates *the same object*, preserving
`m_errorHandled` (which is how `ProcessError` avoids formatting one failure twice).

### The description is held once, as UTF-8

`what()` must return a `const char*` into storage that outlives the call, and must be `noexcept`.
A `wxString` holds UTF-16 here, so asking it for UTF-8 yields a temporary that dies on return.
Hence the class stores the sentence **as UTF-8 bytes**, built in the constructor:

- `what()` hands those bytes back and builds nothing — that is what makes the `noexcept` honest;
- `GetErrorDescription()` composes its `wxString` from them, and always returned by value anyway.

One storage, two doors. Keeping both representations as fields would mean one sentence kept in
step by hand, and an exception is never on a hot path.

---

## 3. The varieties — the type IS the question "who refused, and how"

The subclasses are not decoration and not a severity scale. **A variety says which subsystem
failed**, so a caller can catch exactly the failure it knows how to handle and let the rest travel.
A handler around a posting run wants the lock conflict and nothing else; a retry loop wants the
deadlock and nothing else. Written as one exception class plus a string, neither is expressible —
you would be matching on message text, which is a translation away from breaking.

| Class | Who refused | What the caller may do about it |
|---|---|---|
| `ibBackendCoreException` | anything without a subsystem of its own | report; the ordinary case |
| `ibBackendAccessException` | the rights model | a refusal, not a malfunction — names WHAT was refused ("writing register 'Sales'") and the caller may legitimately continue |
| `ibBackendInterruptException` | **the user** | not a failure at all — see below |
| `ibBackendLockException` | concurrency (row / object locks) | usually a *message*, not a bug: reload, or try again later |
| `ibBackendDatabaseException` | the database engine | depends entirely on the kind — retry, or surface as a bug |
| `ibBackendQueryException` | the query layer (L2 translation / connections) | a broken query or an exhausted pool — never a user error |

### Two levels: the class says WHO, the kind says WHAT

The database and lock varieties carry an inner `enum class Kind`, and each value states the
handling rule outright:

```cpp
ibBackendDatabaseException::Kind::Deadlock      // always safe to retry — the engine already rolled us back
ibBackendDatabaseException::Kind::ConnectionLost// reconnect; the pool's lazy clone usually hides it
ibBackendDatabaseException::Kind::Constraint    // business-level: "Already exists", "Required field empty"
ibBackendDatabaseException::Kind::Syntax        // not retryable — broken schema or wrong query: a bug
ibBackendLockException::Kind::VersionChanged    // "Object was changed by another user, please reload"
ibBackendLockException::Kind::RowLockTimeout    // "Object is being edited by another user"
```

This is the difference between a diagnosis and a complaint. "Database error" tells the caller to
give up; `Deadlock` tells it to retry, and `Constraint` tells it to show the user a field. **A
retry loop that cannot tell a deadlock from a syntax error is either an infinite loop or a lost
transaction.**

### Adding a variety

When a subsystem gains failures with their own handling rule — a spreadsheet document, an import
run, a plugin ABI call — it gets its own class, not a prefix in the message. The test is not "is
this area important" but: **would any caller want to catch exactly this and nothing else?** If
yes, the type is the only way to say it. If the answers differ *within* the new area, that is the
inner `Kind`, and each value documents what the caller should do — the enum is where the handling
rule lives, so the knowledge is not re-derived at every catch site.

### The interrupt is not an error

`ibBackendInterruptException` means the user (or the debugger) stopped execution. It is the one
that punishes a careless handler: a generic `catch (const ibBackendException&)` that logs "error"
turns a deliberate stop into a false alarm, and a `catch (...) {}` that eats it turns a stop
request into a hang. Handle it **first and separately** wherever a script can run
(`procUnit.cpp:1761` is the reference site).

---

## 4. Every boundary, and what it owes

A boundary is any place a failure would otherwise leave the program: the top of a thread, an event
handler, an application entry point, a callback handed to a library.

| Boundary | Obligation |
|---|---|
| **Thread body** (session registry, job manager, worker pool, logger) | catch all three shapes; a thread that dies silently takes its subsystem with it |
| **wx event handler** | catch and report — an exception through wx's dispatch is undefined behaviour in a GUI |
| **Application entry** (`mainApp.cpp`, `wes/main.cpp`) | catch, show, drain the error chain (§6) |
| **`crashGuard`** | last resort: record before the process dies |

Two rules for what a boundary does with what it caught:

1. **Report the description, never the fact alone.** "unknown exception" and "operation failed"
   are the same sentence: they say a thing happened and nothing about which.
2. **The message is DATA, never a format.** `wxLogError(err.what())` passes the description as the
   format string, so an object name carrying a `%` makes the logger read a vararg nobody passed.
   Always `wxLogError(wxT("%s"), …)`. And `what()` is UTF-8 bytes (§2) — hand it to
   `wxString::FromUTF8`, not to the narrow overload, which re-reads them in the current locale and
   mangles every Cyrillic message. This was live in two handlers in `visualHost.cpp`, reachable for
   the first time the day `ibBackendException` started deriving from `std::exception`.
3. **A fail-stop is for a broken INVARIANT, not for a failed operation.** The registry thread
   terminates the process deliberately — if the registry stops maintaining `sys_session`, every
   other node's view of who holds what becomes a lie, and continuing spreads it. That reasoning
   does not extend to an application-level failure that merely happened to be raised on that
   thread (a name that does not resolve, a query that failed). Same throw, different consequence:
   the invariant question is "can the rest of the system still trust our state?".

---

## 5. When an empty `catch (...)` is legitimate

Three places, and no others:

- a **destructor** — throwing out of one during unwinding terminates the process;
- **rollback of an already-failed transaction** — the rollback failing changes nothing;
- a **cleanup loop** that must reach every element (per-row deletes on shutdown).

Around thirty such sites exist in `backend.dll`, all of that shape. In business logic the rule is
the opposite: log or rethrow. An empty catch there is not error handling — it is the deletion of
evidence, and the bug it hides surfaces later somewhere that cannot explain it.

---

## 6. The per-thread error chain

Every `ibBackendException` constructor pushes its description onto the **calling thread's** chain
(`PushLastError`, bounded so a runaway loop cannot grow it without limit):

- `DrainLastErrors()` — read and clear; what a failed startup / login / metadata load shows;
- `PeekLastErrors()` — read without clearing, for a diagnostics view;
- `GetLastError()` — the legacy single-string form, kept for old callsites.

Why a chain rather than a last-error string: a failure is usually a sequence, and the visible one
is the last, not the cause. The single string lost every earlier reason the moment any handler
constructed a wrapped exception — so the report named the symptom and hid the origin.

---

## 7. Compile-time versus runtime, and the diagnostic record

`ProcessError` builds a **diagnostic record** (`diagnostic.h`) — file, module, line, position, the
offending source line, the code — publishes it to subscribers, and the human-facing text is
*assembled from that record*. The text is a projection; never build the record from the text.

The **kind** (compile / runtime) is stated by the caller and cannot be recovered from the message:
a compile error means the code never ran, a runtime one means it did, and both read alike.

Error codes (`ERROR_*` in `backend_exception.h`) exist for a second reason beyond consistency: the
runtime ones are raised inside `ibProcUnit::Execute`, where spelling `_("…")` inline put `wxString`
construction into the interpreter's hot loop (see [runtime-perf.md](runtime-perf.md) §5). A code is
a constant at the callsite; the text is looked up only when a failure actually happens.

---

## 8. Diagnostics have to reach a person

A message that exists only in the debugger does not exist. The applications are normally run
**without a debugger attached**, and in a GUI build `wxLog` queues output and shows it later — too
late to precede an assert or a `terminate`.

So anything a boundary records for a human goes **to a file**: `ibTraceToFile`
(`backend/utils/debugTrace.h`) appends a timestamped line to `oes-debug.log` next to the
executable. The fail-stop path writes its reason there before terminating; that is what turned an
unreproducible crash into one sentence naming the object.

Losing the text is the same class of defect as losing the exception.
