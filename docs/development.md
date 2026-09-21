# How we work

This is how the platform is actually built, written down so that nobody has to guess. It is the
practice, not an aspiration: every rule below is here because breaking it cost a day somewhere.
Where this file and the code disagree, the code is right and this file is stale; say so in a pull
request.

Two neighbours hold what is not repeated here:
[CONTRIBUTING.md](../CONTRIBUTING.md) (what sending a pull request means legally, and the four
comment markers) and [CLAUDE.md](../CLAUDE.md) (layout, naming table, key decisions, what not to
do). Portability has its own file, [portability.md](portability.md), and so does the build,
[BUILD.md](BUILD.md).

---

## 1. Branches, commits, pull requests

**`develop` is where work lands; `master` carries release tags only.** Maintainers commit to
`develop` directly. Everyone else sends a pull request into `develop`.

**A pull request is taken whole, with a merge commit.** We do not squash and we do not
cherry-pick: your commits stay yours, and GitHub shows the request as merged. When we want
something changed, we usually add a commit to your branch ourselves (the "allow edits by
maintainers" box) rather than send you a list of changes. The commit message says what changed and why,
and the pull request is then merged as `Merge pull request #N from <you>: <title>`. Requests are
taken one at a time, in the order they build on each other.

**When we decline, we say why.** Most often: the change removes a check instead of fixing what
the check caught (§4), or it builds a second road to something the engine already answers (§3).
A declined request is not a verdict on the person; the reply names what would make it acceptable.

**Your first pull request waits for CI.** GitHub holds workflows from first-time contributors
until a maintainer approves the run. A pending check on a first request says nothing about the
change.

**A commit message says what is now true, then why.** The title is a sentence about the result:
*"The accounting register sees ibMetaData whole where it hands one to AdjustValue"*, not
*"fix build"*. The body says what was wrong, what you measured, and what you decided against,
because the diff already shows what changed and nobody can recover the reasons later. Long bodies
are normal. English only. No generated footers or tool attribution lines.

**One commit, one concern.** A mechanical rename or move goes in a commit of its own, so the
commit that changes behaviour stays readable.

**No customer or partner names in the tree.** Code, tests, docs and commit messages describe a
database by what it is: *"a 40 000-employee copy"*, not whose it is.

---

## 2. Code

**Match the code around you.** Naming, file split and comment density are set by the
neighbourhood; the table in [CLAUDE.md](../CLAUDE.md#naming-conventions) is the summary.

**English everywhere in the source**: identifiers, comments, commit messages, documentation. The
user interface is translated; a `_()` message id stays ASCII ([portability.md §1.9a](portability.md)).

**Our own vocabulary.** Name a thing by what it is in this platform. Terms borrowed from other
products do not go into identifiers; a comparison, if a design needs one at all, is a footnote
in its document.

**Header guards are `#ifndef __NAME_H__` / `#define __NAME_H__`.** Almost every header uses
them; the few with `#pragma once` are exceptions to fix, not examples to follow.

**The backend stays GUI-free and never shows a modal window.** Its callers include the daemon,
the web server, background jobs and an assistant over MCP, and none of them can click a box.
Say it with `ibValueSystemFunction::Message`, or hand the reason back to the caller
(`bool Verb(…, wxString* refusal)`).

**Database access goes through `ibPreparedStatement`.** Never concatenate a value into SQL.

---

## 3. Before you add anything: where does it already live?

Most rejected changes were correct in what they did and wrong in where they put it. Before a new
file, class, method, field or flag, go through these questions.

**Does a neighbour already hold the place?** State of the interpreter already has a home; a
cell already has a description; a column already has an accessor. Put the new thing there, in
that neighbour's form. A new file beside an existing one that does the same job is the most
common way a feature fails to fit the next one.

**Is the new name really an overload?** If it does what an existing function does, on a
different input, it takes that function's name and its shape (`GetColumn(prefix, col)` beside
`GetColumn(col)`, `bool` result if the neighbour answers with `bool`). Keep public names when you
rewrite a body. Build new names from the file's own vocabulary, not from general engineering
words (`Frame`, `Slots`, `Lease`, `Pool`).

**One door per class.** Whatever helps the door lives inside it, a local `struct` in the function
body, not as private members and statics in the header. Count the names you added to a header
before you build; more than two is almost always too many.

**Who includes this header?** The cost of an edit is the number of translation units that
include the file, and it is paid again by the revert. `value.h`, `metaObject.h`,
`commonObject.h` and `metaData.h` reach almost the whole tree: changes there are agreed first,
never made in passing. A class needed by one `.cpp` lives in that `.cpp`. An optional new
`virtual` goes at the end of the class.

**Who calls this seam?** Before changing an interface, grep its callers and its overriders
separately. Overriders are many; callers are usually one or two, and they hold the answer.

**Can the existing parts already say it?** Ask whether the need is expressible with what exists
before adding a field, a verb or a step. The number you need often already exists (a meta id, a
row position, a key). The handle you need is often already held by someone; hold it too instead
of building a lifetime around it.

---

## 4. Design rules we keep coming back to

**One state, one road.** When something "works here but not there", do not look for the
condition that separates the cases; look for the second road that reads the same state. Put the
rule where there is only one of it, in the engine, not at each caller. Converging two roads
usually deletes code; if your fix adds a rule, you are probably building a third road. A shared
function with two callers is still two roads if one of them could read through the other.

**A door either does its job or throws.** `Commit` succeeds or throws; `RollBack` rolls back or
throws. A door does not decide on the owner's behalf. Retry, rollback and fallback are the
owner's choice, made in its own `try`/`catch`. A door that quietly acts for the owner leaves the
owner blind.

**One signal, one verb, at the owner.** Stopping work is `ibSession::Cancel()`. The session
passes the same word to its thread, its connection and its tenants, and whoever started the work
catches the interrupt. Two names for one act mean one of them only forwards to the other; delete it.
Put the trigger where every kind passes (`Close`), not in a hook a subclass may override. Before
adding a sender, check the journal: the signal is usually sent already and nobody is listening.

**Each subsystem refuses with its own exception type.** The type says who refused, `Kind` says
what. A caller catches the type it can handle and lets everything else pass; matching on message
text breaks with the first translation.

**Meaning is a type, not bits, branches or lists.** A new category ("any reference") is a
registered type with its own rule, not a pattern in unused bits of an id or a special case in
every consumer. A hand-maintained list of what the system already knows is a defect. Two flags
that are always set together are one state: make it an `enum`.

**Never reach for the active configuration.** Several configurations are open in one process:
the working base, one being compared, one opened from a file, external reports. Whoever reads a
value passes the configuration it belongs to. Taking the active one gives a silently wrong
answer, not an error. It is legitimate only in the running application and in the main frame.

**A reference is identified by its metadata id, never by its name**, and a value is not done
until it survives serialisation and a restart.

**A description holds no live runtime objects.** Descriptions (types, compositions, sources) are
data: copyable, comparable, storable as versions. A live reference inside one comes back as
`Unknown value type`.

**A new value is born owned.** Whatever makes a value answers with its holder, never a bare
pointer: the ctor registry and `ibValue::CreateObject` with an `ibValue`, a creator whose type the
caller needs with an `ibValuePtr<T>`, a source with an `ibSourcePtr<T>`. A bare pointer starts at
reference count zero, and a new object runs code of its own while it is being made (a data
object's module, its `Filling`): anything that takes `ThisObject` and lets it go frees the object
halfway through (#154). Hold it before initialising it. Keep the answer in a holder, never in a
`T*` — the holder converts to one silently, and the pointer dangles once the temporary goes. Where
no type is needed the holder is plain `ibValue`; `ibValuePtr<ibValue>` adds nothing.

**The schema diff is the only authority on DDL.** Nothing reads the database to decide what DDL
to run. A schema change that exists only in code, and not in the declaration, is invisible to
the diff, so the declaration must carry the difference.

**Derived state is a reading of the movements.** Totals, balances and schedules are computed
from what was recorded. When a reading is slow, find what is rare and read less; do not store a
second history.

**A removed check is not a fix.** Classify a change by its text: does it add a check, or remove
a `return false`? A removal has to show what the refusal protected and why that protection is no
longer needed. "It didn't work, now it does" looks the same for a fix and for a bypass.

**Finish the reading side.** A value that is written is not done until something reads it back,
including `WriteData`/`ReadData`, the database, and the copy. The test is a restart.

**A new kind starts from its nearest neighbour.** Before building a new metatype or value kind, list
what its closest neighbour answers (storage, call as function, set key, read surface, save and
load). The defects of a new kind are the answers nobody asked for: no error is raised, the value
is just wrong.

**Ask the one who holds it; do not cast to find out.** The value knows its type, the list knows
what it accepts, the property knows its flags.

**If it cannot be done with the mouse, the design is wrong.** Every feature has to be usable in
the designer, not only from code or an assistant.

---

## 5. Tests and CI

**The solution does not build the tests; CMake does.** A green `enterprise.sln` says nothing
about `tests/`. CI builds and runs them on Windows, Linux and macOS, and its verdict is the one
that counts. On `develop` a new push cancels the previous run, so read the latest one.

**Changing a signature means searching `tests/` for it.** Mocks and tests that read sources as
text break silently in a local build and loudly in CI.

**Green on MSVC says nothing about Clang and GCC.** Go through the list in
[portability.md §1](portability.md) before pushing a change to shared headers.

**Name tests `TEST(ClassName, Method_Condition_Expected)`**, and a bug fix brings the test that
would have caught it, or says in the pull request why one is impractical.

**Check the engine against a truth computed outside it.** A test that builds its expected answer
with the machinery under test confirms itself. The reference answer comes from raw records (the
postings, the movements), and synthetic cases are backed by runs over real configurations.

---

## 6. Speed

**Debug is the examiner.** Speed is accepted in the Debug build: what a checked build makes
expensive is a real cost it magnifies. Details and the worked example are in
[CLAUDE.md](../CLAUDE.md#what-not-to-do).

**Time each query before optimising.** One journal line per pass shows the culprit; a single
total ("46 s") only invites guesses.

**Measure the user's road.** A benchmark that takes a convenient shortcut (another driver, no
output to a document) measures something nobody waits for.

**Ask one question once.** When many readers ask the same thing with the same key, fold them
into one question before the join, not after.

---

## 7. Finding a defect

**Find who prints the line before changing anything.** If a rerun prints exactly the same
output, the fix went to the wrong place.

**Use the technology journal in a Debug build**, with a line at every step of the road in
question. Release does not write it.

**"It used to work" is a question for `git log`**, not for reasoning.

**Proving absence takes a second method.** "I didn't find it" is not "it isn't there": search
both spellings, both layers (backend and frontend), and the last week of history.

---

## 8. Documentation

**`docs/` is public**: these rules, the build, portability, architecture, the context file for
assistants, release notes. **`docs/private/` is a private submodule** with design documents,
arcs and plans; it opens for members of the organisation and is empty for everyone else. Links
into it from code and from public documents will not open for you. That is expected, and nothing
in the build depends on it.

**Documents are written in English**, and a document that describes code changes in the same
push as the code.

**A design document states the need, not our status.** "The engine cannot do X yet" goes stale
the day X lands; "a report needs X, because …" stays true.

---

## 9. Working with an assistant

Changes written with an AI assistant follow the same rules and the same review. Its context is
[CLAUDE.md](../CLAUDE.md), and [ai-context.md](ai-context.md) if it generates metadata or
scripts. The MCP server inside the designer lets it build and run a configuration itself; see
[CLAUDE.md](../CLAUDE.md#running-it--and-the-door-an-assistant-comes-in-through). Check what it
claims by running it, and keep its attribution lines out of commits.
