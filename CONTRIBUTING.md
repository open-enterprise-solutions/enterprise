# Contributing

Contributions are welcome. This file says two things: what a change should look like, and
what sending one means legally. The second part is short, and it is not boilerplate — please
read it, because this project is not licensed the way most repositories on GitHub are.

---

## What sending a pull request means

OES is **source-available, not open source** ([LICENSE.md](LICENSE.md)): free for any
noncommercial use, and licensed separately to anyone who wants to earn from it. That second
half is how the work is paid for.

A licence describes what a **user** may do. It says nothing about what the project receives
from a **contributor** — and your change is your copyright, not ours. So without an explicit
grant from you, an accepted contribution would sit inside code we sell commercial licences
for while we hold no right to license it that way. That is not a theoretical tidiness
problem: it would make the commercial licence untrue for every customer.

So, by opening a pull request, you grant Maxim Kornienko and Yurii Bulakh a perpetual,
worldwide, irrevocable, royalty-free licence to use, reproduce, modify and distribute your
contribution, **and to sublicense it**, including under commercial terms and under future
versions of the project's licence. You keep your copyright; you are giving permission, not
giving it away, and you remain free to use your own contribution however you like elsewhere.

You also confirm that the contribution is yours to give: that you wrote it, or have the right
to submit it, and that it does not carry obligations from somebody else's licence.

If you cannot make that grant — because an employer holds rights to your work, or because the
code came from somewhere else — say so in the pull request instead of quietly leaving it out.
Code with unclear provenance is the one kind we cannot take, however good it is.

A CLA bot may ask you to confirm this on your first pull request. It records what this section
already says; there is nothing extra in it.

### What contributing here is, said plainly

It is worth being blunt about this, because the usual assumption on GitHub is the opposite
one and nobody should discover it afterwards.

This is not a commons. The platform stays owned by the people named above and is sold
commercially. A change you contribute goes into something that earns money for someone else,
and you are not paid for it. If that is not what you want, do not send it — fork the project
for your own noncommercial work instead, which the licence expressly allows and which owes us
nothing.

What we offer in return is not money, and we would rather say so than imply otherwise.

**The platform itself, free, for anything noncommercial.** Not a trial, not a cut-down
edition, not a licence that expires — the whole thing: the designer, the language, the query
engine, the reports, every driver. Build your own systems on it, run them, keep them. That
offer stands whether you ever contribute a line or not; contributing is not how you earn it.

**The sources, to read and to learn from.** For a platform of this kind that is rarer than it
sounds, and the debugging you did to find that bug is the proof of what it is worth: nobody
can do that to a product they cannot read.

**A place for the work to live.** Your change goes into something that is maintained — built
on three toolchains, tested, and still here next year — rather than into a fork that goes
quiet.

**Your name on it.** In `NOTICE.md` and in the release notes of the version that carries your
change. A contribution accepted here is a contribution to the platform, and it is recorded as
one.

If that trade is fair to you, we are glad of the help. If it is not, that is a reasonable
conclusion and no hard feelings.

### What this does not restrict

Nothing here limits what you may do with the project as a user. Read it, build it, change it,
run it, write configurations for it, teach from it, fork it publicly for your own
noncommercial work — none of that needs anyone's permission, and none of it obliges you to
contribute anything back.

---

## What a change should look like

**Branch from `develop`, pull-request into `develop`.** `master` carries release-tagged
commits only.

**Match the code around you before matching this file.** Naming, comment density, brace
placement and file layout vary a little by area, and the local convention wins. Broadly: `ib`
prefix for public classes, `m_` for members, `s_` for statics, `g_` for compile-time
constants; tabs for indentation.

**Say why in the commit message, not what.** The diff already says what changed. What it
cannot say is what was wrong before, what you measured, and what you decided against — and
that is the part somebody will need in a year. Long commit messages are normal here.

**Database access goes through `ibPreparedStatement`.** Never build SQL by concatenating
user-supplied values, whatever the surrounding legacy code does.

**Backend stays GUI-free.** No wxWidgets includes under `src/engine/backend` — that boundary
is what lets the engine run headless in the daemon, the web server and the tests.

**Build it on more than Windows if you can.** MSVC accepts things GCC and Clang refuse
(ambiguous conversions, name hiding, unused-value rules), so a change that builds locally can
still break the Linux and macOS jobs. CI runs all three; read its output before assuming.

**Tests where the behaviour is testable.** `tests/` uses Google Test:
`TEST(ClassName, Method_Condition_Expected)`. A bug fix without a test that would have caught
it is accepted, but it is worth saying in the PR why one was impractical.

Bug reports and feature requests are welcome as GitHub Issues. A report that includes what you
did, what you expected and what the technology journal said is worth several that do not.
