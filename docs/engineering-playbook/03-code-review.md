# 03. Code Review

## The main principle

**Every change reaches `develop` and `master` only through a Pull Request.** No exceptions. Even a one-line fix, even a CMakeLists.txt update, even a comment edit.

Direct push to `master` and `develop` is forbidden at the GitHub configuration level (branch protection rules).

---

## Review process

```
Author creates the PR
    ↓
Reviewer receives a notification
    ↓
Reviewer checks the code (checklist below)
    ↓
Reviewer leaves comments
    ↓
Author addresses the comments
    ↓
Reviewer verifies the fixes
    ↓
Approve → Squash Merge → Branch deletion
```

### Turnaround

- **Regular PRs** — reviewed within 1 working day
- **Hotfix PRs** — reviewed within 2 hours
- If the reviewer doesn't respond — ping them in chat, and bring in someone else if needed

---

## Reviewer checklist

Check every PR against these points:

### 1. Functionality
- Code compiles without errors or warnings (Debug + Release, x64)
- No runtime errors when testing the described scenario
- Edge cases handled (nullptr, empty strings, invalid user input)
- Works correctly when the DB connection is lost

### 2. Memory and resource management
- No raw owning pointers (`new` without immediate assignment to `unique_ptr`/`shared_ptr`)
- All resources managed via RAII (DB connections, file descriptors, GDI objects)
- No resource leaks on exceptions (destructors and RAII wrappers correct)
- `shared_ptr` used only when shared ownership is required, not by default
- No circular `shared_ptr` dependencies (use `weak_ptr`)

### 3. Security
- No hardcoded secrets (passwords, DB connection strings, API keys)
- Input data is validated before use
- SQL queries use parameterization — string concatenation in SQL is forbidden
- Fixed-size buffers protected from overflow (prefer `std::string`, `std::vector`)
- User input is escaped before display

### 4. Architecture
- Code is in the right place (core, designer, runtime, GUI)
- No duplicated logic (DRY)
- Dependencies are injected through the constructor or interfaces, not through global state
- New classes follow existing project conventions
- GUI code contains no business logic, business logic does not depend on wxWidgets

### 5. C++ specifics
- Functions that don't change object state are declared `const`
- Single-parameter constructors are declared `explicit` (unless conversion is intentional)
- Overloaded operators implemented correctly (rule of five when needed)
- No UB (uninitialized variables, signed overflow, out-of-bounds access)
- `std::string` for strings (not fixed-length `char[]` without a reason)
- Range-based for loops and STL algorithms where appropriate

### 6. Tests
- New functionality is covered by Google Test tests
- Tests pass (`ctest --output-on-failure`)
- Tests check behaviour (inputs → expected result), not implementation details
- Tests are independent (no shared state between test cases)

### 7. Documentation
- Public class methods are documented (Doxygen or a minimal comment)
- CLAUDE.md updated when architecture or module layout changes
- Complex business logic commented (why, not what)
- Workarounds are explained with the reason and/or a link to an issue

### 8. Database
- SQL queries use parameterization (prepared statements)
- DB schema changes are documented and have a migration script
- Transactions are used where atomicity is needed
- No N+1 queries in loops
- Indexes considered for new WHERE/JOIN conditions

### 9. Performance
- No redundant DB calls inside loops
- Large query results processed in pages or streams
- wxWidgets UI is updated from the main thread (not directly from background threads)
- Heavy operations moved to background threads (with proper synchronization)

### 10. Code quality
- Clear names for variables, functions, classes (no `tmp`, `x`, `data` style abbreviations)
- Functions are small, single responsibility (around 50 lines as a guideline)
- No commented-out code
- No leftover debug `wxLogDebug` / `printf` without a reason
- No TODO without a task link (`// TODO(#42): `)

### 11. Compatibility and portability
- No Windows-specific API where a cross-platform equivalent exists (wxWidgets, STL)
- No assumptions about absolute paths
- New dependencies are justified (don't add a library for one function)
- Changes that break the plugin API are documented

---

## AI-generated code: extra scrutiny

When a PR contains code written with the help of AI (Claude, ChatGPT), the reviewer should be **especially careful** about the following issues:

### Typical AI errors in C++ code

| Problem | Example | How to check |
|----------|--------|---------------|
| Non-existent APIs | `std::filesystem::read_all_text()` — does not exist | Check cppreference.com |
| Outdated patterns | `auto_ptr` instead of `unique_ptr`, `NULL` instead of `nullptr` | Cross-check against the C++17 standard |
| Wrong memory handling | `delete` instead of `delete[]` for arrays | Careful code review |
| wxWidgets API | Incorrect method signatures, non-existent classes | Check wxWidgets 3.x documentation |
| Thread-safety ignored | UI components accessed from a background thread | Find all wxWidgets calls outside the main thread |
| Non-existent OES classes | `OesSafeQuery`, `OesDatabaseManager`, `OesTransaction` — don't exist in the code | Verify the real names: ibPreparedStatement, ibDatabaseLayer, ibTransactionGuard |
| Wrong OES paths | `src/core/`, `src/gui/`, `src/designer/` — outdated paths | Real paths: `src/engine/backend/`, `src/engine/frontend/`, `src/engine/designer/` |
| SQL concatenation instead of parameters | `"SELECT ... WHERE ID = " + id` instead of `SetParamInt(1, id)` | Verify ibPreparedStatement + SetParamString/SetParamInt is used |
| Unsafe C++ | Use of `reinterpret_cast` without need, `const_cast` | Question every such cast |
| Hallucinated `#include` | Non-existent headers, wrong paths | Make sure the header actually exists in src/engine/ |

### Rule for reviewing AI code

> If you see an unfamiliar API call, class method, or header — **check the documentation** instead of trusting that "AI probably knows". AI confidently uses APIs that don't exist.

---

## Approval rules

| Change type | Approvals needed | Who reviews |
|---------------|-------------------|-------------|
| Regular feature / fix | 1 approve | Any team member |
| DB schema changes / migrations | 2 approve | Tech lead + developer |
| Public plugin API changes | 2 approve | Tech lead + developer |
| Build system changes (CMake/MSBuild) | 2 approve | Tech lead + DevOps |
| Destructive operations on data | 2 approve | Tech lead required |
| CI/CD changes (.github/workflows) | 2 approve | Tech lead + DevOps |

---

## Comment etiquette

So the PR author understands the priority and tone of each comment, use these prefixes:

### `issue:` — Blocking problem
Merge is impossible until fixed.

```
issue: There's a memory leak here — the FirebirdQuery object is created via new
but never deleted when Execute() throws.
Wrap it in unique_ptr or use an RAII guard.
```

### `suggestion:` — Improvement suggestion
Recommended to fix, but doesn't block the merge if the author explains why not.

```
suggestion: This SQL query can be cached as a prepared statement —
it is currently compiled on every call, which causes noticeable overhead
when called frequently against a large table.
```

### `nit:` — Minor
Style, formatting, naming. Doesn't block the merge.

```
nit: Rename `data` to `queryResult` — easier to read.
```

### `question:` — Question
Not a comment but a request for clarification.

```
question: Why is shared_ptr used here instead of unique_ptr?
Are there multiple owners of this object, or is it just out of habit?
```

### `praise:` — Praise
Don't forget to call out good code. It motivates people.

```
praise: Nice solution with the RAII wrapper for the transaction!
Rollback is now guaranteed even on an exception — much cleaner than before.
```

---

## How to be a good PR author

1. **Small PRs** — up to 400 lines of changes. Nobody reviews big PRs carefully
2. **Description** — explain WHAT and WHY, don't make the reviewer guess
3. **Self-review** — before requesting review, look over your own PR, verify it builds with no warnings
4. **Don't argue over nothing** — if the reviewer is right, fix it. If you disagree — make your case
5. **Reply to every comment** — even just "fixed" or "agreed, done"

## How to be a good reviewer

1. **Be constructive** — instead of "this is bad" write "it's better to do it like this: ..."
2. **Propose solutions** — don't just point out a problem, show how to fix it
3. **Don't block on trifles** — nit comments shouldn't delay the merge
4. **Quickly** — a review shouldn't sit longer than a day
5. **Praise good code** — it builds the right culture in the team
