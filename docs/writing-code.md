# How we write code

The remarks review keeps making, written down once so that nobody has to make them again — to
you or to your assistant. [development.md](development.md) says how a change travels and holds
the design rules it is reviewed against (§3 *where does it already live*, §4 *design rules*);
they are not repeated here. This file is the rest: how a name, a class, a holder, a refusal and a
diff look in this engine.

If you work with an AI assistant, point it at this file and at development.md before its first
edit. Every rule below came up in review more than once. Where this file and the code disagree,
the code is right and this file is stale; say so in a pull request. A rule that needs no
judgement belongs in `.github/lint.sh`, not here.

---

## 1. Before the first line

**Read what you change and the road it sits on, not the whole tree.** Who sets the value, who
reads it, where it is serialised, which designer and runtime paths pass through it. A change that
compiles but does not reach the other end of that road is not done.

**Find the nearest neighbour and repeat it literally.** Open it whole and copy its shape and its
hooks (an empty virtual is where the owner reacts). Add nothing of your own: if the neighbour has
no such mechanism, you do not need one. A column group follows `ibValueNotebook::AddNotebookPage`,
not an interface with four virtuals and a factory.

**Ask a question before you build a mechanism.** In this order: is something remembered here that
should be asked of its holder; what do the reference and the neighbours call it; only then a
field, a verb or a step. `m_embedWindow` became `GetChildDocumentWindow(child)`.

**Count the doors before you add one.** A class that already receives (`Receive`) does not get a
`Confirm` or a `Receive(row)` beside it. Say which existing door the act goes through, or why none
fits.

**Write the smallest true version first and run it.** One fact, expressed in place. Types, enums
and helpers come after the first successful run, and only if the fork survives it.

**Open the code before calling anything out of scope.** An estimate rests on the place where the
work would land. "Five projection sites" once turned out to be one field and one line.

**Before removing something that looks unused, find what it carries:** a test that pins it, a
comment that declares it, a planned road, a parity placeholder for the thin client, a safety net.
Often only its type is surplus, not its meaning.

**Unify only what one mechanism serves well.** *One state, one road* (development.md §4) does not
mean one mechanism for cases it serves badly; then split, or give a case its own path.

**A new public name is shown before it is built on.** A new function, type or field on a class
goes into the pull-request description with the neighbour it mirrors, and is agreed before the
code around it starts to depend on it.

---

## 2. Names

**Where a neighbour holds the place, use its name or its overload.** `TranslateErrorCode(status)`
beside `TranslateErrorCode(int)`, not `ErrorCodeOf`; `GetColumn(prefix, col)` beside
`GetColumn(col)`. A name with no neighbour is the smell: it usually duplicates a function that
already answers.

**Copy the principle a family is named by, not only its suffix.** `ibFileRelay`, `ibServerRelay`
and `ibHostRelay` are named by destination, so the relay of commands is `ibCommandRelay`, not
`ibDesignerRelay`. `kLoginWithin` sits beside `kOpenWithin`. When porting, the original's names
are the neighbours.

**Name a thing by what it holds or answers,** not by when or for whom it is called: a parameter
`kind`, not `firstLogin`; `CanOpen` beside `CanAdd`, not `ForNewInstance`. A name promises exactly
what the body does, no less and no more.

**No `own`, `Own` or `owner` decoration.** Wanting it means a second copy of a fact, or a split the
reference does not make. `SetRowOwnSize` and `m_ownRowSizes` were removed: the box that applies a
band sets the rows it names.

**No plurals.** A collection is `<singular>Array` — `m_updatedControlArray` beside `m_pageArray`,
not `m_pendingRedraws`. The verb is `UpdateControl`, not `DrainPendingRedraws`.

**No `bool` that changes what a call means.** Two meanings are two names: `TakeWorkersLocked(bool)`
is two functions.

**One name per meaning.** Merge names into one verb only where every reader asks the same
question. Six "hidden" predicates became `IsAvailable`, while "the source is missing" stayed
`IsUnbound`, and the call site asks for both.

**Do not lengthen a working name.** `RefuseQueued` is right beside its neighbours;
`RefuseQueuedTasks` adds a word and no meaning. Rename someone else's name only when it lies,
disagrees with its neighbour, or encodes a flag.

**`using Base::Method;` means a name is wrong: rename instead.** `AdjustValue(in, out)` on both
`ibValue` and the type factory clashed in classes that are both; the runtime's became
`AdjustOutValue`. Before borrowing a neighbour's word, check that two bearers of it will not meet
in one class.

**When a base name hides the name its heirs want, rename the base.** `ibValue::Clone` became
`CloneValue`, and heirs keep `Clone()`.

**Never shadow a base field.** Grep the bases before naming a derived member; two concepts get two
names (`m_caretPos`, not a second `m_currentPos`).

**Rename across the wire.** A name on one side of the client/server protocol usually has a twin
with the same root on the other: grep `frmclient`, `frmserver` and `designer-thin`, and rename both.

**The thin client speaks in protocol verbs: `Send`, `Receive`, `Confirm`.** It declares no
properties, so `ReadProperty` on a property object is a tautology. The act happens at one door
(the inspector's `Create` calls `Receive`), never inside a constructor.

**No `MDI` in new names.** The web has tabs; the name promises a structure the code does not have.

---

## 3. Classes and files

**Each of `public`, `protected` and `private` appears once, in that order.**

**Inside each, one group per kind:** types and aliases, statics, virtual functions, plain
functions, data members last. A plain function never stands among virtuals. Event handlers (`On…`)
form a group of their own.

**Inside a group, order by meaning.** What works together stands together: a door and what it
calls, a read and its write, a question and its answer. A new function goes beside its relatives,
not at the end of the class. Definitions in the `.cpp` follow the order of the header, and a moved
body moves to its group in both files.

**Every override is `virtual R F(...) override`.** Both words, always: `override` says it
overrides, `virtual` says it may be overridden again, and a three-level metatype hierarchy is read
without opening the base.

**What is reached only through a door is `private`;** what nobody calls any more is deleted.

**A type one function needs lives in that function** as a local `struct`, or appears as an
elaborated type in the signature (`const class ibDataNode& node`), not in a block of forward
declarations. File-local helpers are `static` or in an anonymous namespace in the `.cpp`. An
`enum class` cannot be declared in a signature; it gets one opaque line, `enum class X : int;`,
with a comment.

**The runtime register block (`VALUE_TYPE_REGISTER`, `SYSTEM_TYPE_REGISTER`) stays last in its
`.cpp`.** New code goes above it.

**Count the callers before adding a virtual, or a member to a base.** With one caller it is an
expression at the call site.

**Extract a helper at the third copy,** as a `static` or inline function in the same `.cpp`, never
a new header for three lines.

**One path to `ibValue`.** No diamonds: capability mixins (`ibTabularObject`,
`ibActionDataObject`) never inherit `ibValue`, and a class swaps its base slot rather than adding a
second base.

**A constructor that builds a request or a description from a name is `explicit`.** An implicit
`ibFormRequest(const wxString&)` once turned "pass the request" into "pass the name" in twelve
places and dropped the selection condition silently.

**A function called across a DLL boundary is exported before you build** (`BACKEND_API`, as its
neighbours do); the miss shows up as LNK2019 at the end of a wide build. A new source file is
registered in its `.vcxproj` and `.filters`: CMake's glob forgives the miss, MSBuild does not.

**Keep wide headers quiet.** Explanations go at the definition in the `.cpp`, and a file-static
there beats a new member. A header costs its transitive closure, not its direct includers: `form.h`
has 23 direct includers and reaches 603 translation units. Touch a wide header last, once per
batch, and never for a comment alone. After splitting a header, grep its includers: each includes
what it uses.

**To change a wx control deeply, fork it into an `ib*` class** (`ibDataView`) whose header names
the wx version it came from and lists the changes; the wx submodule stays pristine. A small change
through an override or an event may inherit.

---

## 4. Casts

**Before a cast, ask three questions.** Is the declared type wrong? Is the conversion already
implicit? Am I casting to learn something the creator could simply hold? The one cast that stays
is a number read from the wire into an `enum`, once, where it is read.

**`const_cast` means a signature lies.** Fix the signature, or route through whoever owns the
non-const original. Hiding it in a member (`m_self = this`) is the same mistake.

**Probe an `ibValue` with `ConvertToValue(T*& out)`** where a mismatch is a normal path: it never
throws and unwraps references. Use `ConvertToType<T>()` or `CastValue<T>` only where a mismatch is
an error, and never `dynamic_cast<T*>(v->GetRef())`. A walk, a render, serialisation or an audit
over mixed objects uses the non-throwing sibling; the asserting one looks fine until it runs in
another process.

**Identify a type by its class id, never by its registered name:** `GetClassType() == g_<x>CLSID`,
not `GetClassName() == wxT("SizerItem")`. A global id is a `constexpr` in a header, not a map
filled at static initialisation.

**Do not widen a base with a virtual to avoid one cast.** One registry lookup and a `dynamic_cast`
of a pointer you already hold is fine, and so is a cast to build a value of the class you need. A
cast to learn something about a value is not: ask the value. No `dynamic_cast` on a hot path.

---

## 5. Holders and lifetime

development.md §4, *a new value is born owned*, is the base rule. In addition:

**A creator returns `ibValuePtr<T>`, and its empty answer is `wxEmptyValue`.** A getter of
something that already exists returns `const ibValue&`. Bridge holders (`ibSourcePtr<T>`,
`ibFormPtr<T>`) are for pure interfaces; anything that is an `ibValue` is held by `ibValuePtr<T>`.
After changing a creator, grep the calls whose result does not land in a holder.

**An enumeration value is made through the registry** (`ibValue::CreateEnumObject<T>(code)`),
never with `new`: a bare one has an empty name table.

**A reference-counted object never lives on the stack,** not even in a test fixture. The first
holder that takes it and lets it go deletes stack memory.

**Never keep a pointer read from a temporary `ibValue`**
(`ibValue::FromNode(node).ConvertToValue(p)`): the temporary dies at the end of the expression.
Hold the value in a variable first.

**No `.get()` at a call site.** Inside the class, call its accessor; where the callee always gets
an object, take a reference and pass `*holder`; keep a returned `shared_ptr` as it is. `.get()`
lives only inside the one getter.

**Owned plain C++ objects are `std::shared_ptr<T>`,** in collections too, and `unique_ptr` only
where single ownership is a real invariant. Take a sink parameter by value and `std::move` it into
the member; observe a `shared_ptr` by `const&`; return a local by value, without `std::move`.

**Whoever works with a counted object holds a reference for as long as the work lasts.** When
something becomes counted, list every road that hands it out (the main one, the fallback, "nobody
asked yet", "already gone"). Each road holds (`ibSession::AcquireMetaData`) or says in a comment
why it need not. No unlocked `.get()` on a pointer that is swapped under a lock.

**To show or keep what another object holds, hold a reference to it; do not clone it**
(`weak_from_this().lock()`).

**A reference into a container does not survive a mutation of that container.** When a structure
starts handing out its objects, or lives longer than before, ask who owns what it points to, and
snapshot what the consumer needs.

**Shutdown order lives in one owner, normally its destructor** (`~ibApplicationData`). A second
place that stops things is a second order.

---

## 6. One copy of a fact

**A cache is a second copy.** Wanting `mutable`, a lock or an invalidation counter means the data
has no owner, or something is rebuilt on every call: find who already computes it and read through
that. Never forward-declare a domain type into a generic interface to make room for a cache, and
never optimise without a measurement of your own.

**Load once; a read only reads.** State shared by the sessions of a base is computed once, at load,
in sequence. After that a getter never writes: refill-on-read is a race that shows only when two
sessions read at once. A mutex covers only what still races. No shared `static` wx GDI objects
(their reference counts are plain `int`): `ibDefaultSpreadsheetFont()`, not a `static const wxFont`.

**Settings are read once at start and passed on.** A second reader asks the holder
(`GetTlsFiles()`) and never re-reads the file.

**What a base needs comes up in `Open`, not at the first login.** An "is it up yet?" guard
(`EnsureStarted…`, `if (!m_started)`) is a check-then-act race in a host with many users.

**Everything about a property lives in its value (`ibVariant…`).** The value is an `ibVariant`, the
system's one value type — a primitive in place, or a counted reference to a LIVE value that keeps itself
current (a type refreshes against its configuration), so every holder sees it change. Derived data is
generated from the value, and the owning object keeps no parallel member: `m_sourceDescriptor` became
`ibVariantSource`.

**Ask the object whether it is empty** (`IsEmptyProperty()`); never work it out from a projection
outside it. If the predicate is missing, declare it on that entity.

**Before adding a counter, a generator or a version, ask what number the thing already has:** its
id, its position, `GetFactoryCountChanges()`. A counter that outlives its scope makes ids depend on
history. When two sides generate the same thing, keep one generator; do not add a field to keep
two in step.

**Publish the classification with the answer,** not just a name: a layer that knows `ibColumnRole`
hands it out. `EndsWith("_RRRef")` outside the layer that names columns is a defect.

**Wherever code says "default", say who decides.** A default left to the DBMS drifts from the one
another road decides itself: for an unstated window frame the server adds rows up to the current
one, while the in-memory twin folds the whole partition. State it in one place.

**Ask what an empty set means before adding to it.** If empty means "all", keep the additions apart
and apply them where "all" is expanded.

**A getter that answers a default for "unset" never feeds a copy loop.** Add `HasX` and copy only
what is set, or the default becomes a value of its own.

**A trait you are about to compute is one the owner should declare.** The session kind is declared
at `CreateSession(kind)` (`ibSessionKind`), not derived from the run mode and an empty user.

**Once it works, collapse the second currency:** a string beside an id, an encoding beside a type,
`bool` constants that should be one set of flags. Collapsing shrinks the code.

**A value describes itself:** `property->GetDataValue(v)` for the human value,
`ibValue::Serialize(node)` for a schema. No hand loop over typed getters, no switch on the number.

**An optional script argument travels as `const ibValue&`.** An empty `ibValue()` means "not
passed", the default is decided once, in the body, and the test is `GetType() == TYPE_EMPTY` (an
explicit `False` is "empty" too).

**A method does not parse a string of fields, directions or conditions.** Ordering, filtering and
grouping are said in the query language; if it lacks something, the query language gets it.

**A path has one decomposition and one walker.** Never split a dotted path by hand, and never make
values only to walk a structure.

**An untranslated engine word is a failed catalog lookup.** Fix the lookup; never add a second
translator.

**Two builds, one method.** The build difference goes inside it (`#ifdef OES_USE_WEB`); no stub
headers, no parallel class trees.

**When the model and the database disagree, find how they diverged.** Never reconcile with the
physical state (probing columns, tables or indexes); guard the invariant with refusal and rollback.

---

## 7. Where code lives

**Decide the scope of a fact before placing it.** A process fact (a handler registration, library
initialisation) goes in the one process-start place, an entity fact on the entity, and only a site
fact is swept across call sites. A harmless duplicate you feel like adding means the scope is wider
than the site.

**A property belongs to the entity it describes,** not to whatever type a binding returns: a format
is the attribute's or the control's, not the query column's. A constant lives with its owner,
beside its other constants (`ibSessionRegistry::kActivityWidth` beside `kConnectTimeout`).

**The parameters of an act travel in the request that already carries such parameters**
(`ibCreateRequest`), not as a new parameter in sixteen signatures or a cast to the source object.

**Fix a defect at the node where it happens,** by extending the symmetric mate of an existing edge,
not with a hook, a flag or a guard where the symptom shows. To forbid something in a state, close
the one door everybody gets the object through (`CurrentFrame()` gives no form during a forced
exit) instead of checking at each caller.

**Gate designer against runtime at the creator that pairs a widget with its data:** it hands the
widget a null model. The widget gets no mode flag, and the models get no per-class checks.

**Formatting is applied by the edge that shows the value.** Data answers "what value" and "what
format does the column have"; it never carries display text.

**A low-level factory only allocates.** Building, showing and fallbacks live above it, at the
command entry, and the factory never calls back into them.

**Navigation lives with the data it walks, and a conversion from a type is that type's member**
(`ibQueryRamTable::ToValueTable()`). Callbacks such as `next()` and `read(i)` mean a middle
structure is missing.

**Core holds structure and data only.** Every schema (an `ib…Description` with its
`ib…DescriptionMemory`) lives in `core/description/`, even with one copy, and reading and writing
it is pure. Core has no metadata, no factories and no interfaces to them. Checking a description
against a configuration is a separate step on the side, and the runtime over a schema lives in
`backend/backend_<x>.h`.

**Layers know only downward:** schema, then composer, then runtime. A description holds no runtime
object and no query mechanics.

**A contract shared by several drivers speaks its own structure** (`ibCompositionAttr`), never one
consumer's render type; conversion to a widget type happens when the widget asks.

**Designer-only and diagnostic code stays out of runtime classes and runtime routes.**

**New SQL is built from IR nodes, never glued as text in the renderer.** Glued text bypasses
aliases, quoting, parameter types and binding order. A missing node becomes a general mechanism; a
semantic node is only for an operation the dialects spell differently.

**Query work goes to the database first.** The in-memory road stays as the correctness road beside
a gated fast path, not the other way round. A special case that existing language constructs can
express is lowered by the compiler, not added to the runtime.

**A sentinel belongs to its domain.** "None" is 0 in the query domain and -1 in the model; fix a
collision where an id crosses domains, never by migrating a domain.

**Generated code obeys the hand-written rules.** A query the engine writes qualifies its fields
exactly as a person must.

**A guid is unique only inside its configuration.** A lookup across documents or caches resolves
the owning configuration first (`FindMetaDataDocument(metaObject->GetMetaData())`).

**What the platform knows goes into the platform:** a refusal in the tool, a paragraph in its
`GetDescription()`, an entry in `mcpToolPatterns.cpp`, a construct in `syntaxHelper/`. Every
assistant reads those; nobody reads private notes.

**No transaction stays open across user interaction.** Lock a record with its data version and a
short `FOR UPDATE` at write, or with the application lock table. An exact match is `=`; `LIKE` is
for a prefix only (a parameterised `LIKE ?` uses no index on Firebird).

**A password is stored only as its hash** (`ibPasswordHash`). The raw one lives in memory and is
cleared when the session closes.

---

## 8. Events and updates

development.md §4, *one signal, one verb, at the owner*, is the base rule. In addition:

**A change starts its own update.** A value rebuilds in its own `OnChildChanged` and then calls the
base, so the change reaches the holder; a child whose property changed tells its owner up the
attach chain (`m_attachOwner`). No `Ensure…` lazy build, no patch in the viewer.

**An event is raised after the rights check, once its subject is ready and held, and it passes the
subject.** A handler is foreign code: it must not see what the user may not see.

**A handler decides; a plain function does.** A verb that needs another verb's work calls that
function, never the other handler through a synthesised event: verb to verb through handlers
closes into recursion.

**A generic primitive only emits its event** (`ibWebTimer` queues a timer event); the subscribers
own the domain logic.

**Open and close through the lifecycle verb that fires its own events** (`CloseForm`). Never fire
them by hand, and never tear down views inside a control's handler.

**A component draws itself on its own show or activation** (`EVT_SHOW`); a container does not push
state into it.

**The inspector sets a property and fires the events.** The owner may veto in `OnPropertyChanging`
and is told in `OnPropertyChanged`; it never sets or rolls back the value itself. After an edit,
use the existing chain (the object reports `MetaObjectStage(Edited)`, the observer redraws) rather
than reworking someone else's mechanism.

**A "settle once" flag is a symptom.** Find what undoes the state every time and fix that.

**No timer or polling carries state between processes.** Use a named moment: start, the write
door, an explicit signal on its own channel. Over a wire, every tick is a server call.

**When "nothing arrives", read the sender from its first line** before declaring the channel dead
or building a collector; an early return on a mode flag is the first suspect.

**A mutator pushes the new value to its live view itself;** it never relies on a later re-walk.

---

## 9. Refusals and errors

development.md §4 is the base: a door does its job or throws, each subsystem refuses with its own
type, a removed check is not a fix. In addition:

**Throw by value, catch by `const&`,** derived types first. Rethrow with a bare `throw;`, build
with the `X::Error(...)` helpers, derive from `ibBackendException`, never `throw new`. Never
`catch (...)` to swallow: let it rise, or record it with its text.

**Turning a crash into `false` is half a fix.** Follow the `false` up to a person, and prefer the
door throwing with words. A configuration that failed to load once opened silently empty because a
caller returned on `false`.

**One value never carries two meanings.** `nullptr` cannot mean both "no such source" and "opaque
source": ask a second question (`SourceIsOpaque`), or record "answered" apart from the answer. A
row count carries no verdict; failure arrives as an exception, and a `bool` from a helper is read
by its caller and turned into a throw.

**A guard stands where the refusal is born,** inside the function that prints it, not at one of its
callers. An element that cannot be checked is named and skipped; it does not silence the whole
check.

**An assert, log line or dialog that fires on a working path is a messenger.** Do not remove it,
narrow it or reword it; find who asks the wrong object the wrong question.

**Key a guard on the question the rule asks.** An evaluation guard asks the evaluation
(`ibBackendException::IsEvalComplete()`), not the process mode; "not here" is asked of the holder
(`IsDesignerHost()`), not of the whole process. A refusal by process in a shared door starves every
other road through it, so when a shared door refuses, check that each caller still wants it.

**No optional "pass it if you have it" parameter.** A default is a branch hidden from the callers:
name the branch instead. When a check moves behind an optional parameter whose default means "no
check", every old caller, `tests/` included, silently stops checking.

**One failure, one report.** An outer handler asks whether the error was already reported
(`IsErrorHandled()`); reporting twice is the mirror of swallowing.

**A soft failure comes back through the existing return value** (`bool`, a typed empty value). The
computing tier gets no notification channel and no global state.

**"Could not ask" is not "absent".** What we cannot see by design stays quiet; an unknown qualified
name is reported. A verb checks that the request is its job, not only that the value is valid.

**A refusal names its subject** (our statement, the entity, the id), and a value we do not
recognise is reported as its raw number, not as silence.

---

## 10. Threads and sessions

**A subsystem receives its context as a reference from the owner that created it,** in its
constructor, along the chain (session, registry, application data, pool). No thread-scoped binding
and no "the only base in the process" fallback; with neither an owner nor a thread that declares
whom it works for, refuse rather than guess.

**Interpreter state is a session member, not `thread_local`:** sessions move between workers.

**Whatever reads a session's interface tree runs on that session's worker** (`RunOnWorker`), even
read-only serialisation. Work on shared configuration state runs on the session's pool, never on a
server I/O thread.

**A `static` scratch buffer under a lock that only serialises threads becomes `thread_local`,**
with no lock, and per-call state is reset on entry. A lock that guards shared state stays, as
`std::mutex`.

**In the backend, threads, mutexes and atomics are `std`.** New code starts on `std`. `wxString`,
`wxDateTime`, `_()` and the serialisation streams stay as they are: changing them changes the
format on disk and on the wire.

**Before removing a global, list every thread that reaches it** (`std::thread`, `wxThread`, pools,
the HTTP server, timers) and decide for each who binds its session.

**An ending session never waits on a window.** Keep the reason and say it after the application
data is unloaded.

---

## 11. Client and server

**The server describes; the client draws.** The thin client has one generic metaobject class and
one generic property, filled from the server's rows, and each property holds one serialisable
variant (`ibVariantDataSerializable`). No client-side property lists, no per-kind subclasses.

**The server sends exactly what should be seen,** and each answer replaces the client object's
properties wholesale. The server composes what the client shows (a title with its "modified"
mark); a wire flag is added only when the client must act on it.

**Data needed in one place travels in that place's packet,** trimmed on the server, not through a
new door, schema or request per holder. A diff that shrinks is the sign of the right answer.

**Before sending something, ask whose it is.** Configuration and session data come from the
server. Constants compiled into the engine (its pictures) are in core, and the wire carries only
their ids.

**A kind on the wire is an `enum class` with explicit values that are never renumbered,** not a
string. Every literal that goes over the wire, and every `_()` id, is ASCII.

**The client view is the server's control tree rebuilt bare,** by the host's one recursive walk
(`GenerateControl`). A control never builds its own children and gets no flag to skip a subtree.

**When something does not survive the wire, first check whether the value survives the format** (a
64-bit id written as a JSON number comes back rounded), and only then the way it is identified.

**A client never keeps a server-side position across a change;** it resolves the index when it
acts.

**A port copies the original whole:** files, classes, members, order, handlers, menus. Where the
original touched the backend, a client class of the same name hides the protocol call. Port from
the desktop original (since 2026-10-27 only on the branch `oes/thick_client`), never from an already
thin sibling, and the result is judged by running it beside the original. A protocol door is the
original's door, traced before it is written.

**A child application is started by the client, locally,** from the database parameters it holds:
the server may be on another machine.

---

## 12. Speed

development.md §6 is the base: Debug is the examiner, time each query, measure the user's road,
`ibString` on hot paths. In addition:

**A mechanism costs nothing when its feature is unused.** No allocation per row or per cell; a null
`unique_ptr`, not an empty `std::map` (MSVC allocates its head).

**In an object built per call, count what each member costs to make and to destroy.** A map that is
searched linearly is a vector.

**What is the same for every instance of a type is shared per type,** not built per instance.

**An expensive argument is computed only in the branch that needs it.**

**Keep hot functions small.** Cold error text goes out of line (`IB_NOINLINE`); a hot
`thread_local` is a trivial type with no member initialisers.

**No search-then-append (`FindOrAdd…`) on the single-pass compile path.** Each reference resolves
once; its identity goes in the operand.

**A limit is a measured fact of the engine, kept in the dialect** (`m_maxParameters`), never a
threshold we pick.

**A change to a hot path brings its bench row.** Time each pass with `ibJournalStopwatch` and leave
the line in. "Regression or not" is an A/B under identical conditions through the touched code;
±5% on rows you did not touch is code layout. The technology journal does not exist in Release:
subtract it from a Debug profile, never optimise it.

---

## 13. Tests

development.md §5 is the base. In addition:

**Compare the artefact, not its presence.** A structured result (a tree, a plan, bytecode) gets a
canonical text; "both produced a tree" is not a check.

**A test that replaces a production mechanism with a double tests the double.** Add one on the
real path.

**Run a fix's test without the fix** before accepting both, and check that the test does not
switch off what it tests (triggers, the write door).

**A test pins confirmed behaviour,** not a belief about it.

**A headless test builds no toolkit objects** (`wxFont` without `wxApp` crashes); see how the
neighbouring tests stub them.

**Changing behaviour, not only a signature, means grepping `tests/`** for the function's
expectations. When a door starts using a member or an argument it used to ignore, grep `tests/` for
that owner built with `nullptr`. After a mass rename, grep `tests/` and `docs/` too.

**Before pushing tests,** check that the quotes of string literals pair up, that every name they
reference exists in `src/`, and that their messages are ASCII. The solution never compiles them.

**A symptom in the interface is pinned by a backend test first,** step by step along the data
chain; the interface is touched once the chain is green.

**A milestone is proved on real configuration data along the user's road,** counting "matched what
was expected", not "something came back".

**After a run, read the technology journal:** exceptions, asserts and refusals, counted before and
after. Each line is a deliberate refusal or a defect, and an exception the engine catches in order
to fall back is a finding.

**Every `m_property…` member of a metatype is serialised.** Compare
`grep -o "m_property[A-Za-z]*"` over its header with its `ReadData` and `WriteData`.

**Red CI is fixed from its log.** Decide whether the test or the engine is wrong.

---

## 14. Finding a defect

development.md §7 is the base. In addition:

**Cut a layer; do not confirm a guess.** When an observation fits several causes, the next step
tells them apart. Never judge a result with two things changed at once, and after two unconfirmed
hypotheses, stop reasoning and read the data.

**"Works in A, not in B": list every difference** and eliminate the cheapest first. A vendored fork
is a full suspect.

**Read the declaration before editing `X->Y`:** `X` may be a local that shadows a member.

**A fix that failed shows that it is not enough, not that it is irrelevant.** A crash after a
correct change is usually a second invariant that rested on what you changed.

**A comment about a foreign system** (a DBMS, a driver) is verified by measurement before an error
path is built on it.

**"Verified" names the road the check excludes.**

**An empty search proves the pattern wrong, not the thing absent.** Search for the function, or for
the place it must live; sweep a bug class by its class, not by one spelling; say "not found", not
"absent".

**A bug is one instance of a class.** After the fix, state the pattern in one line, grep the tree
for it (vendored code aside), and fix or list every hit.

**Enabling a path that never ran is a change of its own, with its own tests.** Until then, note the
diagnosis in the code and keep the old behaviour. An unread value was either never wired or
deliberately disabled: find who stopped reading it before you wire it.

---

## 15. The diff

**Read your diff as a reviewer before you hand it in.** No moved, re-spaced or duplicated lines.
Insert at statement boundaries, never between someone's comment and its code. A method sits among
its class's methods. No duplicate `#include`, no cast the design was meant to remove; next to a dead
variable of the same kind, replace it rather than add a twin.

**A comment states intent or an observation, never status.** Check a comment's claim before you
rely on it. Change the prose in the same pass as the behaviour, and grep every mention of a
function whose responsibility moves. A comment states the condition, not one of its causes.

**Fix a trap; do not describe it.** A defect found on the way goes into the same batch, in a commit
of its own, not into a "later" list.

**Finish a feature across the cases its users need.** Grep for an existing mechanism before writing
"not supported"; a disabled control explained by "it is always so" is a prime suspect.

**After writing a value, find its reader** and check the key; after widening a function, check
that no caller's condition narrows it back. A fact declared in one place is swept through every
walker of its kind: count the mentions of a working neighbouring fact, and the new one needs as
many.

**Do not add what nobody asked for,** and finish the current stage before starting the next. Do not
start a wide rework inside a batch that has not landed.

**Before pushing, look for** debug edits, commented-out code, temporary files, TODOs without an
owner and weakened guards.

**A change to a wide header is finished whole and built once.** After changing the size or layout
of a class embedded by value or used as a base, rebuild every target that includes it: stale
objects show up as garbage pointers.

**Revert only the hunks you wrote.** Never reset a whole file in a tree with other changes, and
check `git diff --numstat` afterwards.

**A mechanical migration goes from the other end:** script the replacement, build, group the errors
by their root (an include, a door, an overload) and fix the root. Then check by hand what the
compiler cannot see: rewritten comments, silent non-overrides (C4263/C4264), lifetimes of
temporaries. A mass replacement is never wider than asked, file renames and entity renames are
separate operations, and a scripted edit reads and writes UTF-8 explicitly.

**Claim no more than you measured.**
