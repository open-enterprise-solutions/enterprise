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

> ⭐⭐ **THE DIRECTION: A SUBSYSTEM GETS ITS OWN EXCEPTION.** Stated as policy on 2026-08-14 — every
> subsystem is meant to grow its own variety over time, so that the set of exceptions is *diverse*
> and each one can be handled in its own way. That is the point of the whole arrangement: a caller
> should be able to say "this failure I know what to do with" by naming a TYPE, and let everything
> else travel untouched.
>
> The alternative — one exception class carrying a message — collapses every subsystem into a single
> undifferentiated failure, and the only way back out is matching on message text, which is one
> translation away from breaking. It also produces the handler nobody can write: `catch (...)` that
> must decide, from nothing, whether to retry, to report, or to abort.
>
> So when a subsystem starts needing a decision of its own — "wait for the other user", "the query
> text is wrong, point at the position", "the schema drifted" — that is the moment to give it a
> variety rather than a Kind on somebody else's. The session family below is the worked example: it
> was carved out of the query family the day a session refusal needed a different answer from a
> query refusal. New varieties are expected; a growing table here is the design working, not rot.

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
| `ibBackendSessionException` | **the session** — who may work in this base, and on whose connection | nothing malfunctioned: wait, ask the others to leave, open a session first (§3a) |

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

## 3a. The SESSION's own refusals — a family added 2026-08-14

`ibBackendSessionException` (`backend/session/sessionException.h`) answers the one question none of
the varieties above could hold: **who may work in this base right now, and on whose connection.**
When it is raised nothing has malfunctioned. Somebody else holds the base, or nobody holds it at
all — and the caller can usually act on that.

| Kind | The condition | What the caller does next |
|---|---|---|
| `ExclusiveHeld` | somebody holds the exclusive lock | wait, or ask them to release it |
| `OthersActive` | others are connected and the operation needs the base to itself (a restructuring that executes DDL) | ask them to leave. Nobody has TAKEN anything here — a different sentence to the user and a different remedy from `ExclusiveHeld` |
| `NoSession` | there is no session on this thread at all | a programming fault at a boundary: session-scoped state was asked for outside a session |
| `NoConnection` | the holder or the pool would not give the session a connection | the engine was never reached, so this is **not** a DBMS error and must stop arriving as one |

Why it is a family of its own rather than a Kind on an existing one — §3's test ("would any caller
want to catch exactly this and nothing else?"), answered three times over:

* **as a DATABASE exception it would inherit retry semantics**, and retrying "another user is
  connected" is a spin, not a recovery;
* **as a QUERY exception it would claim our machinery failed** — which is false, and sends whoever
  reads it looking in the wrong place;
* **as a bare `ibBackendException`** it carries no answer to "so what do I do", which is precisely
  what a session refusal is FOR: every Kind above maps to a different next step for the user.

### `NoConnection` moved here, and its consumer stopped asking about a Kind

"Could not obtain a connection from the holder" used to be `ibBackendQueryException::Kind::NoConnection`
— the SESSION's refusal filed under the query tier, because the query builder is where the absence
is NOTICED. It is now raised as `ibBackendSessionException::Kind::NoConnection` from all six sites
that notice it, in `databaseLayer/databaseQueryBuilder.cpp`: `ExecuteIR`, `Render`,
`ExecuteRendered`, both `Execute` overloads (DDL and DML) and `ExecuteReturning`.

Its one consumer is `ibJobManager::WriteSharedSettings` (`job/jobRegister.cpp`), and the move let it
say what it means **by the handler it writes**:

```cpp
catch (const ibBackendSessionException&) { return ibWriteOutcome::NoBase;  }  // not asked yet
catch (const ibBackendQueryException&)   { return ibWriteOutcome::Refused; }  // ours, and it failed
```

A job declared before a database is open — a unit test, the platform's own list at bring-up — has
not been refused anything; it has not been *asked* yet, which is why that outcome has three values
and not two. Written as a Kind test the same handler reads
`catch (const ibBackendQueryException& e) { if (e.GetKind() != …) throw; }`, and one forgotten
rethrow there swallows somebody else's failure — the argument `queryException.h` already makes for
`ibBackendQuerySourceException` being a subclass rather than a Kind.

### The other three Kinds got their throwers the same day

The vocabulary stopped being declaration-only a few hours later. Every Kind is now raised, and each
site had been spelling a session refusal as something else:

| Kind | Raised at | What it used to be |
|---|---|---|
| `ExclusiveHeld` | `ibSession::SetExclusive`, `ibExclusiveResult::HeldByOther` (`session/session.cpp:696`) | `ibBackendCoreException` — "something went wrong", which is the one thing it is not |
| `OthersActive` | `ibSession::SetExclusive`, `ibExclusiveResult::NotSole` (`session.cpp:699`) | the same `ibBackendCoreException`, indistinguishable from the line above except by its sentence |
| `NoSession` | `ibSession::DatabaseLayer()` when `ibSession::Current()` is null (`session.cpp:1334`) | `ibBackendCoreException` on the way to `ses_query` |
| `NoConnection` | the six sites in `databaseLayer/databaseQueryBuilder.cpp` | `ibBackendQueryException::Kind::NoConnection` |

`SetExclusive` is the clearest case for the split. **Both of its refusals mean "you cannot have the
base to yourself", and they need different sentences and different remedies:** `ExclusiveHeld` says
somebody has TAKEN it — wait, or ask them to release it; `OthersActive` says nobody has taken
anything, there are simply other people connected — ask them to leave. A restructuring that executes
DDL hits the second one, and telling a user "another session is in exclusive mode" when no session is
would send them looking for a lock that does not exist. `Pending` and a missing registry stay
`ibBackendCoreException`, correctly: those *are* malfunctions.

`DatabaseLayer()` is the boundary case rather than a condition of the base — session-scoped state
asked for on a thread with no session. It is typed so a caller that can cope tells it apart from "the
base refused" without reading the message; `jobRegister.cpp` (below) already does exactly that one
level up, which is what made the fault worth a Kind instead of a message.

### Honest remainder

`ibBackendQueryException::Kind::NoConnection` is **gone** from `query/queryException.h` — an
enumerator with no thrower is a second name for a refusal that already has one, and the next person
to need it would have picked whichever of the two the header showed them first. The place it stood
now carries a comment saying where it went (`queryException.h:46`), which costs one line and answers
the only question its absence raises.

Its one consumer is still `ibJobManager::WriteSharedSettings`, and it is still the only site anywhere
that catches `ibBackendSessionException` — the other three Kinds are raised and nobody yet catches
them by type. That is the expected order (a raise is useless to a caller that has no answer to give,
but a caller cannot be written against a type that does not exist), not a gap to close by inventing
handlers.

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

## 5a. A number means it went through; a refusal is an exception

The rule the database itself follows, and the one this engine follows for the same reason.

A call that completes returns a **count** — how much it touched. Zero is one of those counts and
carries no verdict: a DDL statement changes no rows, a cleanup can match none, a diff can find
nothing to do. More or fewer than expected is still a completed call; whether that number is the
one the caller wanted is the caller's question, not the callee's.

A call that **cannot proceed** raises. That is the only way a refusal travels.

The two must never share a channel, and both directions of the mistake have been paid for here:

- **A count read as a verdict.** The totals-maintenance installer tested its result against
  `DATABASE_LAYER_QUERY_RESULT_ERROR`, which is `0` — the very number `CREATE TRIGGER` legitimately
  returns. Every successful install read as a failure.
- **A refusal returned as a value.** The same installer later caught the driver's exception and
  turned it into `false`, which became the status `ibMaterializeApply::Failed`. Nothing read that
  status: its one reader asked whether the value was `Rebuilt`, to decide whether to write a line in
  the ledger. So a `CREATE TRIGGER` that could not compile ended exactly like one that succeeded —
  the apply walked on, saved the configuration over the old one, and ran the regeneration pass. The
  configuration then described a schema the database did not have, and no later apply could bring
  the two back together, because a diff of two descriptions cannot see that one of them is a lie.

Hence, for anything that changes the database:

1. return counts, and never encode failure in one;
2. raise on refusal, and let it reach the transaction boundary;
3. if a helper must answer with a `bool` (a callback, a barrier-parked write), **the caller reads
   it** and raises. An answer nobody reads is the same as no answer at all.

The payoff is that the transaction boundary needs no cooperation from anyone: an exception unwinds
to the apply's `catch`, which rolls back and rethrows. Half-applied is not a state the system can
be left in, and this is what keeps it that way.

Why that matters more here than elsewhere: the configuration diff is the ONLY description of the
physical schema, so a half-applied base is a base its own configuration no longer describes, and
nothing will ever notice — the next diff compares two descriptions and is right to stay silent when
they agree ([schema-authority.md](schema-authority.md)).

---

## 5b. A rule that COULD NOT RUN has not passed

A guard that reads the database to decide whether a change is allowed has **three** answers, not
two: *allowed*, *refused*, and *neither could be established*. Written with a catch, the third
silently becomes the first — and the first is the strongest permission the system can grant.

Both instances were live on 2026-08-14, in `m_beforeChange` rules attached to schema declarations
([accounting-register-arc.md § 5c](accounting-register-arc.md)):

- the chart of accounts' analytics ceiling wrapped its count in
  `catch (const ibBackendException&) { return true; }` — every failure, whatever it was, answered
  PASS;
- the hierarchy rule answered **0 rows** on any exception, which reads as *nobody is in the way*.
  What it guards is not cosmetic: zero there lets a `Parent` column be dropped out from under the
  rows that have one.

There is exactly one legitimate reason for such a rule to find nothing, and it is not an exception:
the table does not exist yet, on a base nothing has ever been applied to, where no row can be
stranded. So both now ask `ibDatabaseQueryBuilder::TableExists()` in front, plainly, and let every
other failure travel — a broken lowering, a lost connection, a dialect that cannot render the
aggregate. The apply stops instead of granting permission it never established.

The general form is § 5a's, said of a guard rather than of a write: **"allowed" must never be the
same value as "I do not know".** Where the return type cannot hold the difference, the not-knowing
raises.

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

A per-event companion (`ibDiagTrace`) lived beside it for the accounting-register arc and was removed
with it — what a temporary trace is for, and what it settled, is recorded in
[accounting-register-arc.md § 5i](accounting-register-arc.md). The pattern is worth repeating when a
sequence rather than a decision is in question; the file is not worth keeping, or it becomes a second
logger nobody maintains.

Losing the text is the same class of defect as losing the exception.
