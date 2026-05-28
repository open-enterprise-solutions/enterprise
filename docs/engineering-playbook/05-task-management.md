# 05. Task Management

## Task tracker

Every project uses **one** task tracker. The choice depends on the project:

| Tracker | When to use |
|--------|--------------------|
| **GitHub Issues** | Primary tracker for OES. Technical tasks next to the code, integrated with PRs |
| **Jira** | For projects with full Scrum, sprints, and story points |
| **Linear** | A Jira alternative for teams that prefer minimalism |

### GitHub Issues — primary tracker

**Task structure in GitHub Issues:**
- **Milestones:** major releases / work streams (v1.1.0, CMake Migration, Google Test Integration)
- **Labels:** task type, priority, component
- **Assignees:** responsible developer

**Linking Issues → GitHub:**
- Branch name contains the task number: `feature/42-pdf-export`
- PR title contains a link: `feat: add PDF export (#42)`
- In the PR description: `Closes #42` — GitHub automatically closes the issue on merge

**Rule:** one project = one tracker. Don't spread tasks across multiple tools. Every team member should know where to look for tasks.

---

## Task lifecycle

```
Backlog → To Do → In Progress → Review → Done
```

### Statuses

| Status | Meaning | Who moves it |
|--------|----------|---------------|
| **Backlog** | Task created but not planned | Task author |
| **To Do** | Planned for the current week/sprint | Tech lead |
| **In Progress** | Developer is working on the task | Developer (picks up the task) |
| **Review** | PR created, waiting for review | Developer (creates the PR) |
| **Done** | PR merged, task complete | Reviewer (after merge) |

### Transition rules

- Developer takes a task from **To Do** → moves it to **In Progress**
- No more than **2 tasks In Progress** per developer at the same time
- If a task is blocked — mark it with the `blocked` label and explain why
- A task is considered **Done** only when the PR is merged (not when the code is written)

---

## Task format

### Title

Format: **verb + object**

Good:
```
Add undo/redo support in form designer
Fix null pointer crash when opening report with empty dataset
Implement PDF export for reports
Refactor database layer to support connection pooling
Migrate core module build system to CMake
```

Bad:
```
Registration          — no verb, unclear what to do
Bug fix               — doesn't describe the problem
New feature           — doesn't describe the feature
Task #42              — meaningless title
Crash                 — no context
```

### Body

```markdown
## Description
When opening a report whose dataset has no records, the application
crashes with an Access Violation in `ReportRenderer::RenderRow()`. Happens
with any DBMS (Firebird, PostgreSQL).

## Expected behavior
The report should open and show an empty page with a header,
but no data rows.

## Steps to reproduce
1. Open any report
2. Make sure the dataset is empty (no records)
3. Click "Preview report"
4. Observed: Access Violation crash

## Environment
- OS: Windows 10 x64
- Build: Debug x64, commit abc1234
- Database: Firebird 4.0.1

## Acceptance criteria
- [ ] Report opens without crashing on an empty dataset
- [ ] Correct empty page is displayed
- [ ] Unit test added in tests/unit/runtime/ for this scenario
- [ ] No regressions in other tests

## Technical notes (optional)
Likely cause: `ReportRenderer::RenderRow()` accesses `m_dataset[0]`
without checking `m_dataset.empty()`. Check `ReportRenderer.cpp` around line 234.
```

---

## Task → branch → PR linking

Every task must be linked to a branch and PR:

```
Task #42: "Add PDF export for reports"
    ↓
Branch: feature/42-pdf-export
    ↓
PR: "feat: add PDF export for reports (#42)" → Closes #42
```

### How to link

In the PR description use the keywords:
```
Closes #42
Fixes #42
Resolves #42
```

GitHub automatically closes the issue when the PR is merged.

Include the task number in the branch name:
```
feature/42-add-pdf-export
fix/108-null-pointer-empty-dataset
chore/55-migrate-core-to-cmake
```

---

## Priorities

| Priority | Meaning | SLA (response time) | Example |
|-----------|----------|---------------------|--------|
| **Critical** | Customer crash, data loss, work blocked | Immediately | Access violation on startup, document loss |
| **High** | Important functionality broken, no workaround | Within a day | Cannot save a form, report won't generate |
| **Medium** | Functionality works with limitations | Within the current sprint | Slow loading of large reports |
| **Low** | Cosmetics, improvements, tech debt | When there's time | Icon misaligned on HiDPI |

### Prioritization rules

1. **Critical** and **High** — picked up without waiting for the next sprint
2. **Medium** — scheduled for the current or next week
3. **Low** — done when there are no higher-priority tasks
4. The tech lead sets priorities, a developer may propose a change with reasoning

---

## Labels / tags

Use labels for quick filtering:

| Label | Color | Meaning |
|-------|------|----------|
| `bug` | Red | Bug in existing functionality |
| `feature` | Green | New functionality |
| `enhancement` | Light blue | Improvement to existing functionality |
| `refactor` | Yellow | Refactoring without behaviour change |
| `docs` | Gray | Documentation |
| `security` | Dark red | Security issue |
| `blocked` | Orange | Task is blocked |
| `good first issue` | Purple | Suitable for a new contributor |
| `build` | Light gray | Build system (CMake, MSBuild, CI) |
| `database` | Dark blue | Working with DBMS or schema |
| `performance` | Light orange | Performance issues |

---

## Milestones — release planning

GitHub milestones correspond to product versions:

```
Milestone: v1.2.0
Due date: 2026-06-01
Description: Google Test integration, CMake migration for core module, PostgreSQL improvements

Issues:
  #55 Migrate core module to CMake
  #56 Add Google Test to build system
  #57 Write unit tests for ibDatabaseLayer (ibDatabaseLayerFirebird)
  #58 Fix PostgreSQL connection pool leak in ibDatabaseLayerPostgres
  #59 Add connection retry logic
```

Every new issue must be attached to a milestone or marked as Backlog.

---

## AI and tasks

### AI can create tasks

Claude Code and other AI assistants can create tasks (through the GitHub Issues API or MCP). Useful when the AI spots a bug or sees an improvement during work.

**However:** an AI-created task is a **draft**. A human must:
1. Check the task is relevant
2. Refine the description and acceptance criteria
3. Assign a priority
4. Attach it to a milestone or sprint

### AI as a decomposition assistant

AI is good at breaking down a large task into subtasks:

```
Prompt: "Break down the task 'Integrate Google Test into OES build system' into subtasks.
Project: C++17, MSBuild + CMake transition, currently no tests."

AI suggests:
1. Add Google Test as git submodule or vcpkg dependency
2. Create CMakeLists.txt for tests/ directory
3. Set up test runner configuration (CTest)
4. Write first smoke test to validate setup
5. Add unit tests for ibDatabaseLayer (connection, ibPreparedStatement execution)
6. Add unit tests for ibDatabaseResultSet (data access, type conversion)
7. Configure GitHub Actions to run tests on PR
8. Document how to run tests locally in README
```

The human verifies, corrects, and creates the tasks.

### AI as a helper writing acceptance criteria

```
Prompt: "Write acceptance criteria for the task:
'Fix crash when opening report with empty dataset in ReportRenderer'"

AI suggests:
- [ ] No crash (Access Violation) when dataset has zero rows
- [ ] Empty report displays header/footer but no data rows
- [ ] No crash with NULL dataset pointer
- [ ] Handles case where dataset exists but all rows are filtered out
- [ ] Unit test added: ReportRendererTest.EmptyDataset
- [ ] No regression in existing report tests
- [ ] Verified on Firebird and PostgreSQL backends
```

---

## Task retrospective

Every 2 weeks (or at the end of a sprint/milestone):
1. Review what was completed (Done)
2. What got stuck and why (Blocked, sitting in Review for a long time)
3. What didn't fit (left in To Do / In Progress)
4. What can be improved in the process

The goal is not to assign blame, but to improve the process. If tasks regularly slip — we're planning too much or tasks are too large.

---

## Handling technical debt

Technical debt is a separate task category. Rules:

1. **Record it** — when you spot something, create an issue with the `refactor` or `enhancement` label
2. **Don't ignore it** — tech debt accumulates and slows down development
3. **Plan for it** — allocate 10-20% of each sprint to tech debt
4. **Don't refactor silently** — refactoring goes through a PR and review like any other code

Examples of OES tech debt tasks:
```
refactor: Replace raw owning pointers in ibDatabaseLayer subclasses with unique_ptr
chore: Migrate designer module from MSBuild to CMake
test: Add unit tests for ibDatabaseResultSet class (currently 0% coverage)
refactor: Extract SQL query strings from business logic into constants
build: Enable /W4 warnings and fix all warning-as-errors
```
