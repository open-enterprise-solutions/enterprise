# 20. Communication

## Communication channels

| Channel | What for |
|-------|----------|
| Project Telegram group | Daily communication, questions, discussions |
| Alerts Telegram channel | Automatic notifications: crash reports, update failures |
| GitHub PR comments | Code review, technical discussions |
| Jira comments | Task-specific discussions |
| Zoom / Google Meet | Planning, retro, demo, pair programming |
| Email | Formal communication, external contacts, licensing questions |

## Async vs Sync

### Async — default

Telegram, Jira, PR comments — don't expect an instant reply.

- **Response time:** by end of business day
- **Urgent:** within 1 hour (with @mention)

Use for:
- Task questions
- Code review
- Discussing decisions
- Task status

### Sync — when needed

Zoom / Meet — schedule ahead, agenda required.

Use for:
- Sprint planning
- Retrospective
- Demo (showing the desktop app's capabilities)
- Pair programming / debugging tough C++ problems
- Discussion of architectural decisions (ABI, plugins, cross-platform)
- P1/P2 incidents (app crash at customers)

Rule: if the question can be resolved in Telegram in 3-5 messages — Zoom is not needed.

## Standups

### Format

Three items:
1. What I did yesterday / since the last standup
2. What I'll do today
3. Blockers — what's stopping progress

### Async standup (team <= 3 people)

Write in the morning into the project Telegram group:

```
#standup
1. Finished implementing PDF export for the designer (PR #112)
2. Today: fix the crash in wxGrid on an empty DataSet, tests on Windows
3. Blocker: no Windows 10 x86 test machine — need help @qa
```

### Sync standup (team > 3 people)

- Zoom, 15 minutes max
- Every day at the same time
- Don't drift into discussion — details after the standup

## Telegram rules

### Channel structure

- One channel per project (don't proliferate channels)
- Separate channel for alerts: crash reports from users, auto-update errors (read-only, bots only)

### Tags

Use tags for quick search:

```
#bug        — bug report
#build      — build / distribution release info
#question   — question to the team
#review     — review request
#decision   — recorded decision
#incident   — incident (P1-P4)
#crash      — user crash report or minidump
```

### Messaging etiquette

- Don't drop code blocks longer than 10 lines — link the PR or file
- Urgent: @mention the specific person, don't just write "hi, got a question"
- Don't split one thought into 10 messages — write it in one
- Screenshots / video: when words don't carry the message (especially for wxWidgets UI bugs)

### Format for urgent messages

```
@developer_name URGENT: App crashes when opening a project with Firebird
Minidump: \\share\crashes\oes_20260410_143200.dmp
Stack: wxGrid::OnPaint → DataSource::Fetch → FBStatement::Execute
Need help diagnosing — reproduces for 3 customers
```

Not urgent:
```
Hi
Got a question
About a task
When are you free?
```

Right:
```
@developer_name Question on OES-456: how do we pass NULL values
from Firebird into wxGrid — via wxVariant or a separate flag?
Need it for the cell editor, not blocking.
```

## Meeting minutes

### Rule

Every meeting longer than 30 minutes = minutes. Use AI Protocolist for automatic minute taking.

### What to capture

- Participants
- Topics discussed
- Decisions made
- Action items (who, what, when)
- Open questions

### Principle

> "If a decision isn't written down, it doesn't exist."

Verbal agreements don't count. Any decision that affects the project must be captured in meeting minutes, a Jira ticket, or documentation.

## Communication in code review

### Principles

- **"Code, not the author"** — discuss the solution, not the person
- Don't make it personal
- Constructive criticism — propose an alternative, not just "bad"
- Praise good solutions — don't only critique

### Comment prefixes

```
issue: A raw pointer here without a nullptr check — possible crash.
Use wxASSERT or an early return.
→ Must fix. PR won't merge until fixed.

suggestion: Can replace the manual loop with std::transform, code
becomes shorter and more expressive.
→ Recommendation. Author's call.

question: Why wxString instead of std::string here? Is there
a reason to use a wxWidgets type in this layer?
→ Question for understanding. There may be a good reason.

nit: Typo in a method name: GetParrent → GetParent
→ Minor. Fix if easy.

praise: Great connection pool implementation for Firebird!
Clean RAII and clear object lifetime.
→ Positive feedback. Important for motivation.
```

### Examples of good and bad communication

```
# Bad
"This is wrong"
"Why do this?"
"Redo it"

# Good
"issue: This query doesn't use parameterized placeholders,
SQL injection through Firebird DSQL is possible. Use
ISC_STATUS array and isc_dsql_execute2 with parameters:
  stmt->SetParam(1, userId);
  stmt->Execute();"

# Bad
"I don't understand this code"

# Good
"question: Can you explain the logic in lines 78-95?
Not sure why the double mutex lock when updating
wxDataViewListCtrl — is a deadlock possible?"
```

## Decision documentation

### Where to store what

| Decision type | Where to store |
|-------------|-------------|
| Architectural decisions | `docs/` in the project repo or `CLAUDE.md` |
| Process decisions | This repository (`engineering-playbook`) |
| Task-level decisions | Jira ticket or PR description |
| Meeting decisions | Meeting minutes |

### Rules

1. **Don't make important decisions in DMs.** If a decision was made privately — bring it into the common channel or document it.

2. **Decision context.** Record not just WHAT was decided, but WHY:
   ```
   #decision Use wxString for all UI text fields, std::string for
   internal logic and network code.
   Rationale: wxString handles Unicode correctly on all platforms
   (Windows / Linux / macOS), std::string is the standard for business
   logic and DBMS interaction without wxWidgets dependencies.
   ```

3. **Architectural decisions (ADR).** For major decisions — an Architecture Decision Record:
   ```markdown
   # ADR-007: Implement plugin system via DLL/SO loading

   ## Status
   Accepted (2026-03-15)

   ## Context
   OES must support third-party extensions (DBMS connectors,
   report exporters, custom widgets) without recompiling the core.

   ## Decision
   Load plugins through wxDynamicLibrary. Every plugin exports an
   extern "C" factory function CreatePlugin() and an ABI version.

   ## Alternatives
   - Static linking — cannot update plugins independently
   - COM/DCOM (Windows only) — breaks cross-platform
   - Scripting engine (Lua/Python) — too slow for UI rendering

   ## Consequences
   - Need a stable C ABI (extern "C" interfaces)
   - Plugin versioning is mandatory
   - Plugins must link against the same CRT version
   ```

## Feedback

### Feedback format

- Specific, with examples
- Timely (not a month later)
- Balance of positive and constructive
- Private for critique, public for praise

### Retrospectives

Hold every 2 weeks (end of sprint):
- What went well
- What can be improved
- Action items (specific, with owners)

Format: 30-45 minutes, Zoom, the whole team.
