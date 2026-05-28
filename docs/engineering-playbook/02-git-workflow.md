# 02. Git Workflow

## Branching model

We use a simplified Git Flow with two main branches and short-lived feature branches.

```
master (production / stable release)
 │
 ├── develop (staging / integration)
 │    │
 │    ├── feature/form-designer-undo-redo
 │    ├── feature/postgresql-support
 │    ├── fix/firebird-connection-leak
 │    └── hotfix/critical-crash-on-open
 │
 └── hotfix/security-patch (directly from master, in critical cases)
```

### Main branches

| Branch | Purpose | Deploy | Protection |
|-------|-----------|--------|--------|
| `master` | Stable code, release tags | Production build | Protected: no direct push, only merge from develop |
| `develop` | Integration branch | CI build + tests | Protected: no direct push, only merge from feature/* |

### Working branches

| Type | Format | Branched from | Merged into | Example |
|-----|--------|-------------|-------------|--------|
| `feature/*` | feature/description | develop | develop | `feature/add-grid-sorting` |
| `fix/*` | fix/description | develop | develop | `fix/firebird-null-pointer` |
| `hotfix/*` | hotfix/description | master | master + develop | `hotfix/crash-on-report-open` |
| `refactor/*` | refactor/description | develop | develop | `refactor/database-layer-cleanup` |
| `chore/*` | chore/description | develop | develop | `chore/migrate-to-cmake` |

### Branch naming

Rules:
- **Lowercase only**
- **Kebab-case** (words separated by dashes)
- **Descriptive** — the name makes the branch's purpose obvious
- **English**

Good:
```
feature/form-designer-grid-snap
fix/firebird-transaction-rollback
hotfix/wx-assert-on-startup
refactor/extract-query-builder
chore/add-gtest-framework
```

Bad:
```
feature/task-123          — unclear what it does
fix_something             — underscore instead of dash
Feature/NewForm           — not lowercase
my-branch                 — no type, unclear
```

---

## Commit format

### Convention

```
type: short description in English
```

### Commit types

| Type | When to use | Example |
|-----|--------------------|--------|
| `feat` | New functionality | `feat: add undo/redo support in form designer` |
| `fix` | Bug fix | `fix: resolve null pointer in firebird connection pool` |
| `refactor` | Refactoring (no behaviour change) | `refactor: extract query builder into separate class` |
| `docs` | Documentation | `docs: update build instructions for CMake` |
| `style` | Formatting (no logic change) | `style: fix indentation in databaseLayer.cpp` |
| `test` | Tests | `test: add unit tests for ibDatabaseLayerFirebird` |
| `chore` | Routine (dependencies, configs, build) | `chore: add CMakeLists.txt for core module` |
| `perf` | Performance optimization | `perf: cache prepared statements in query executor` |
| `build` | Build system changes | `build: migrate database module to CMake` |

### Rules

1. **Commit language: English** — always, no exceptions
2. **First letter after the type is lowercase**: `feat: add ...`, not `feat: Add ...`
3. **No trailing period**: `feat: add grid control`, not `feat: add grid control.`
4. **Imperative mood**: `add`, `fix`, `update`, not `added`, `fixed`, `updated`
5. **Concise**: up to 72 characters in the first line
6. **NO Co-Authored-By lines from AI** — we don't list AI as co-authors

### Commit body (optional)

If you need an explanation — add a body after a blank line:

```
fix: resolve memory leak in Firebird connection pool

Connections were not returned to the pool when a query threw
an exception. Wrapped connection acquisition in RAII guard
(ConnectionGuard) to ensure release in all code paths.

Closes #87
```

### What is NOT allowed

```bash
# Bad commits:
"fix"                                    — unclear what
"WIP"                                    — don't commit work in progress
"fixed stuff"                            — uninformative
"feat: add feature"                      — tautology
"update"                                 — what was updated?
"Co-Authored-By: Claude <...>"           — forbidden
"fix: Исправлен краш при открытии"      — Russian language
```

---

## Pull Requests

### Creating a PR

1. **Always from a working branch into `develop`** (except hotfix → master)
2. **Description is mandatory** — what was done, why, how to verify
3. **One PR = one task/feature** — don't combine unrelated changes

### PR template

```markdown
## What
Brief description of changes (1-2 sentences).

## Why
Why these changes are needed (link to task/issue).

## How to test
1. Build the project in Debug (x64) configuration
2. Open a form with [description]
3. Perform [action]
4. Expected result: [description]

## Screenshots (if UI changes)
Before/after screenshots if the UI changed.

## Checklist
- [ ] Code compiles without errors or warnings (Debug + Release, x64)
- [ ] Tests pass (ctest)
- [ ] No memory leaks (checked with Valgrind/Dr.Memory or ASAN)
- [ ] No raw owning pointers (new without smart pointer)
- [ ] Configs with credentials did not end up in the commit
- [ ] CLAUDE.md updated if architecture changed
```

### Merge process

1. Author creates the PR
2. Reviewer reviews (see [03-code-review.md](./03-code-review.md))
3. Author addresses comments
4. Reviewer approves (Approve)
5. **Squash merge** — all commits from the branch are squashed into one
6. **Delete the branch** after merge — automatically or manually

### Why squash merge

- Clean history in `develop` and `master`
- Each commit on main branches = one finished feature/fix
- Easy to roll back with a single `git revert`

GitHub configuration: Settings → General → Pull Requests → Allow squash merging (only)

---

## Release process

### Develop → Master (release)

```
1. Make sure develop is stable (CI green, tests pass)
2. Verify the Release build manually on a clean machine
3. Create PR: develop → master
4. Description: list of changes since the previous release (CHANGELOG)
5. Review and approve
6. Merge (regular merge, not squash — preserve history)
7. Create the tag: vX.Y.Z
8. Build the installer and upload to GitHub Releases
```

### Versioning (Semantic Versioning)

```
vMAJOR.MINOR.PATCH

v1.0.0 — first stable release
v1.1.0 — new features (backwards compatible)
v1.1.1 — bug fixes
v2.0.0 — breaking changes (public plugin API change)
```

### Creating a tag

```bash
git checkout master
git pull origin master
git tag -a v1.2.0 -m "Release v1.2.0: add PostgreSQL support, fix Firebird connection leak"
git push origin v1.2.0
```

---

## Hotfix process

For critical production bugs that can't wait:

```
1. Create a hotfix/description branch from master
2. Fix the bug
3. PR: hotfix → master
4. After merging into master — create PR: master → develop (so the fix lands in develop)
5. Build the hotfix release
6. Create the tag vX.Y.Z+1
```

---

## Forbidden actions

### Strictly forbidden:

| Action | Why |
|----------|--------|
| `git push --force` to master or develop | Breaks history for the whole team |
| Committing files with credentials (DB passwords, API keys) | Leaks secrets |
| Committing build binaries (`.obj`, `.exe`, `.dll`) | Huge size, not needed in git |
| Committing `build/` and `Debug/`/`Release/` directories | Build artifacts don't belong in git |
| Direct push to master or develop | Bypasses code review |
| Committing the `.vs/` Visual Studio directory | Local IDE settings |

### How to protect branches in GitHub

Settings → Branches → Branch protection rules:

For `master`:
- Require pull request reviews (1 approval)
- Require status checks to pass (CI build + tests)
- Do not allow force pushes
- Do not allow deletions

For `develop`:
- Require pull request reviews (1 approval)
- Do not allow force pushes

---

## A typical working day

```bash
# 1. Start: update develop
git checkout develop
git pull origin develop

# 2. Create a branch for the task
git checkout -b feature/add-report-export-pdf

# 3. Work, commit (in small logical steps)
git add src/engine/backend/report/ibValueReportExporterPdf.h
git add src/engine/backend/report/ibValueReportExporterPdf.cpp
git commit -m "feat: add PDF report exporter class skeleton"

git add src/engine/backend/metaCollection/partial/commonObjectQuery.cpp
git commit -m "feat: integrate PDF exporter into report metadata query"

# 4. Push the branch
git push -u origin feature/add-report-export-pdf

# 5. Create a PR on GitHub: feature/add-report-export-pdf → develop

# 6. After review and approve — squash merge into develop

# 7. Update develop locally
git checkout develop
git pull origin develop

# 8. Delete the local branch
git branch -d feature/add-report-export-pdf
```

---

## Resolving conflicts

If your branch conflicts with `develop`:

```bash
# Option 1: Rebase (preferred for small branches)
git checkout feature/my-branch
git fetch origin
git rebase origin/develop
# Resolve conflicts in .h/.cpp files
git add .
git rebase --continue
git push --force-with-lease  # force-with-lease, not --force!

# Option 2: Merge develop into the branch (for large branches)
git checkout feature/my-branch
git fetch origin
git merge origin/develop
# Resolve conflicts
git add .
git commit -m "chore: merge develop into feature branch"
git push
```

**Important:** `--force-with-lease` is safer than `--force` — it checks that nobody else pushed to the branch after you.

### Conflicts in .vcxproj / CMakeLists.txt

Conflicts in Visual Studio project files (`.vcxproj`) happen often when working in parallel. Rule:

1. Open the conflicting `.vcxproj` in a text editor
2. In the `<ItemGroup>` section accept **both** sets of files (take changes from both sides)
3. Sort entries alphabetically to minimize future conflicts
4. For CMakeLists.txt — same approach: add all files from both sides
