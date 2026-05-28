# 06. AI Agents

## Philosophy

AI (Claude Code, Claude Desktop, ChatGPT) is a **development partner**, not an autonomous developer. AI writes a draft, a human takes it to production quality. AI speeds up work but doesn't replace engineering judgement.

We use AI heavily in day-to-day work and consider it a competitive advantage. But this approach comes with clear rules.

---

## 10 rules for working with AI

### 1. A human always reviews AI code

No AI code reaches `develop` or `master` without human review. AI hallucinates — it confidently uses non-existent APIs, produces logic errors, ignores the project context. The human is the last line of defence before production.

**Practice:** Before committing AI code, read every line. Not just "looks OK" — actually verify: do those headers exist? Is the memory-management logic right? Any UB?

### 2. Don't trust blindly — verify APIs, logic, memory management

Typical AI mistakes in C++ code to catch:

```cpp
// AI may invoke non-existent APIs:
std::string::trim();                    // Does not exist in the standard
std::filesystem::read_all_text(path);   // Does not exist

// AI may write unsafe code:
std::string query = "SELECT * FROM users WHERE id = " + userId; // SQL injection!
char buf[256]; strcpy(buf, userInput.c_str());                  // Buffer overflow!

// AI may use outdated patterns:
std::auto_ptr<MyClass> ptr(new MyClass());  // auto_ptr removed in C++17
if (ptr == NULL) ...                         // Use nullptr

// AI may get the wxWidgets API wrong:
wxGrid::SetCellValue(row, col, value);      // Wrong signature
wxString::Format("%s", str);                // Error: %s expects const char*, not wxString
                                            // Correct: wxString::Format("%s", str.c_str())
                                            // or use wxString::Format("%ls", str.wc_str())
```

**Practice:** If you see an unfamiliar method or class — open cppreference.com or the wxWidgets docs and verify. AI lies very confidently.

### 3. AI does not commit directly — a human creates the commit

AI tools (Claude Code) can create commits. We **do not use** that. A human always:
1. Reviews the changes (`git diff`)
2. Decides what to include in the commit
3. Writes a meaningful message
4. Creates the commit themselves

Exception: AI may suggest a commit message, but a human verifies it and creates the commit.

### 4. No Co-Authored-By in commits

We **do not list AI as a co-author** in commits. No lines like:
```
Co-Authored-By: Claude <noreply@anthropic.com>
Co-Authored-By: ChatGPT <noreply@openai.com>
```

Reasons:
- Responsibility for the code lies with the human, not the AI
- It clutters git history
- It carries no useful information

### 5. CLAUDE.md — the main context file

CLAUDE.md is the most important file for working with AI. Update it when:
- Adding a new module or subsystem
- Project structure changes
- New business rules appear
- Build commands change
- New dependencies or configuration files

A good CLAUDE.md saves 15-30 minutes per AI session. A bad one (or its absence) forces you to re-explain context every time.

CLAUDE.md structure is described in [01-project-structure.md](./01-project-structure.md).

### 6. .claude.json — MCP connections

The `.claude.json` file lets Claude Code connect to external services through MCP (Model Context Protocol):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_your_token"
      }
    }
  }
}
```

This gives AI access to:
- The project file system (for code navigation)
- The GitHub API (for working with issues, PRs)

**Important:** If `.claude.json` contains real credentials — add it to `.gitignore`. You can commit a `.claude.json.example` without real tokens.

### 7. Specialized agents

In CLAUDE.md you can define "roles" for AI — specialized prompts for specific tasks:

```markdown
## AI Agents

### Security Scanner
Check code for vulnerabilities: SQL injection (string concatenation in SQL),
buffer overflows (unsafe strcpy/sprintf), hardcoded credentials,
unsafe file access.

### Code Reviewer
Check the PR against C++17 standards:
- No raw owning pointers (new without smart pointer)
- All resources managed via RAII
- const-correctness (methods with no side effects declared const)
- explicit for single-parameter constructors
- Use the checklist from 03-code-review.md

### Test Writer (Google Test)
Write tests for the given class/method.
Use Google Test (gtest/gtest.h).
Naming: TEST(ClassName, MethodName_Condition_ExpectedResult).
Test the public interface, isolate dependencies via mock objects.
For DB code: MockDatabaseLayer extends ibDatabaseLayer, mock PrepareStatement.
Parameterized queries: SetParamString/SetParamInt — NOT string concatenation.

### CMake Writer
Generate a CMakeLists.txt for the given module.
Requirements: C++17, target-based approach (target_sources, target_include_directories),
compatible with Visual Studio 2017+ and GCC 9+.
```

### 8. AI talks to the team in Russian, code is in English

| Context | Language |
|----------|------|
| Discussing a task with AI | Russian |
| Code written by AI | English |
| Code comments by AI | English |
| Commits | English |
| Documentation (README, CLAUDE.md) | English |

AI prompts can be written in Russian — AI understands and replies in the same language. But all code and technical documentation — English only.

### 9. AI prompts must be specific

Bad prompt:
```
Add PDF export
```

Good prompt:
```
Create an ibValueReportExporterPdf class for exporting reports to PDF in OES Enterprise:
- File: src/engine/backend/report/ibValueReportExporterPdf.h + .cpp
- Dependency: libharu (hpdf.h) — already linked in the project
- Method: bool Export(ibDatabaseLayer* db, int reportMetaId, const wxString& filePath)
- RAII: unique_ptr for internal resources
- Parameterized queries: ibPreparedStatement + SetParamString/SetParamInt
- Transactions: ibTransactionGuard (RAII rollback)
- Error handling: throw ibBackendCoreException on libharu or DB error
- No C++ namespace (historical project style, ib prefix)

Current layout: src/engine/backend/metaCollection/partial/commonObjectQuery.cpp
contains CreateAndUpdateTableDB(). C++17, MSVC 2017+ compiler.
```

**Rule:** the more context in the prompt, the better the result. Don't skimp on description.

### 10. AI output is a draft, not the final version

AI code is a starting point. After getting a result from the AI:

1. **Read** the entire code, carefully, not skimming
2. **Verify** header files (do they actually exist?) and API calls
3. **Build** — make sure it compiles without warnings
4. **Run** — make sure it works on real data
5. **Refine** — adapt it to the specifics of the project
6. **Trim** — AI often adds unnecessary comments and redundant code

---

## Patterns for using AI

### Scaffolding: AI creates the skeleton, a human finishes it

The best way to use AI is to create initial structure:

```
Prompt: "Create a skeleton for a new OES meta-object type:
- src/engine/backend/metaCollection/ — new class ibValueMetaObjectReport
- Base class: ibValueMetaObject (similar to ibValueMetaObjectCatalog)
- Methods: CreateAndUpdateTableDB() — create/update the DB table
- Parameterized queries via ibPreparedStatement + SetParamString/SetParamInt
- Transactions via ibTransactionGuard (RAII rollback)
- Exceptions: ibBackendCoreException on DB errors"
```

AI quickly produces the structure. The human then:
- Verifies compatibility with the ibDatabaseLayer API (real method signatures)
- Verifies that ibPreparedStatement::SetParamString/SetParamInt exist
- Adds the real metadata logic
- Writes unit tests with MockDatabaseLayer
- Verifies that ibTransactionGuard is invoked correctly

### Code Review: AI as the first reviewer

Before sending the PR to a human reviewer — ask the AI to check:

```
Prompt: "Review this C++ code for:
1. Memory and resource leaks (RAII, smart pointers)
2. Potential UB (uninitialized variables, out-of-bounds, null deref)
3. SQL injection and other vulnerabilities
4. const-correctness violations
5. C++17 compliance (no outdated constructs)"
```

AI finds obvious problems well and saves the human reviewer time.

### Debugging: AI analyzes the error and suggests fixes

```
Prompt: "Access Violation in ReportRenderer::RenderRow() with an empty dataset.
Stack trace: [stack trace from Visual Studio].
Here's the method code: [code].
Here's the ReportDocument class (dataset): [code].
What's wrong and how to fix?"
```

AI analyzes errors well, especially when given enough context (stack trace, code, DBMS type).

### Writing tests: AI generates, a human verifies

```
Prompt: "Write Google Test cases for ibDatabaseLayerFirebird:
- A test for a successful SELECT via ibPreparedStatement + SetParamString/SetParamInt
- A test for parameterized query (protection against SQL injection)
- A test for an error on invalid SQL (expect ibBackendCoreException)
- A test for behaviour on connection loss
- Transactions via ibTransactionGuard (verify rollback when Commit is not called)
Use MockDatabaseLayer (mock ibDatabaseLayer) for unit tests.
For integration tests — a real ibDatabaseLayerFirebird with a test DB."
```

### Refactoring: AI suggests improvements, a human approves

```
Prompt: "commonObjectQuery.cpp grew to 800 lines. Propose how to split it
into several classes while preserving the current interface (ibDatabaseLayer + ibPreparedStatement).
Show a step-by-step refactoring plan with names of the new classes.
Note: transactions via ibTransactionGuard, queries via SetParamString/SetParamInt."
```

AI proposes an architecture, a human evaluates and decides.

### CMake migration: AI helps with the syntax

```
Prompt: "Convert this .vcxproj section into CMakeLists.txt:
[ItemGroup content from .vcxproj]
Requirements: C++17, MSVC 2017+, target-based CMake, compatible with vcpkg."
```

---

## Data security when working with AI

### Strictly forbidden to send to AI:

| Data | Example | What to do |
|--------|--------|------------|
| DB connection strings | `SYSDBA:masterkey@server/database.fdb` | Replace with `<DB_CONNECTION_STRING>` |
| API keys and tokens | GitHub token, license keys | Replace with `<REDACTED>` |
| Personal data from the DB | Real user names, emails, phone numbers | Replace with fake data |
| Contents of config.ini | Real passwords and hosts | Use config.ini.example |
| SSH keys | Contents of ~/.ssh/ | Never |

### Data handling rules

1. **Mask sensitive data before sending to AI:**
```
# Bad:
"Connection error: host=192.168.1.100 user=admin password=secret123"

# Good:
"Firebird connection error, config: host=<HOST> user=<USER>"
```

2. **Use fake data in examples:**
```
# Bad:
"Here's config.ini: Password=MyRealPassword123"

# Good:
"Here's the config.ini structure (values replaced): Password=<your-password>"
```

3. **Don't use production DBs through MCP:**
`.claude.json` should point only to a **local** development DB, never to production.

---

## Setting up the AI working environment

### Claude Code CLI

```bash
# Prerequisite: Node.js 18+
# Download from https://nodejs.org/ (LTS recommended)
# Verify installation:
node --version   # must be v18.0.0 or newer
npm --version

# Install
npm install -g @anthropic-ai/claude-code

# Run in the project
cd /path/to/enterprise
claude

# Claude automatically reads CLAUDE.md and .claude.json
```

### Claude Desktop

Add MCP servers to `~/Library/Application Support/Claude/claude_desktop_config.json` (macOS) or `%APPDATA%\Claude\claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "C:/Projects/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_your_token"
      }
    }
  }
}
```

### General rules for AI tools

1. Start the session with context (CLAUDE.md is read automatically in Claude Code)
2. One prompt — one task (don't ask the AI to do 10 things at once)
3. Verify the result before the next prompt
4. If the AI goes off-track — stop and rephrase the task
5. Save good prompts in `docs/ai-prompts/` for reuse
6. When working with an unfamiliar API (wxWidgets, Firebird IBPP) — always tell the AI to check the current documentation

### Running builds — only on command

The AI MUST NOT run a build (`msbuild`, `cmake --build`, any compile command) on its own to verify edits. This includes:

- "Just one more rebuild" after a failed build — fix the code and stop.
- "Quick compile-check" after refactoring — leave the code, summarise the change, wait.
- Pre-emptive builds before showing results — finish edits, summarise, hand the user a one-line build command, stop.

Only run a build after an explicit user command — `build`, `запускай`, `собирай`, `пересобери`, or equivalent.

**Why:**
- A running `enterprise.exe` / `wenterprise-server.exe` / `designer.exe` holds `backend.dll` open → `LNK1168` and the rebuild fails at the link step. The AI cannot tell from outside whether the user has the binaries free.
- Each unprompted build burns 30–60 seconds of the user's attention (kills processes, watches output, replies to errors). Chained "fix → unprompted rebuild → fix → unprompted rebuild" loops drain the session faster than the actual code changes are worth.
- The user pipelines edits in the IDE while the AI works; an unprompted build interrupts that flow.

**How a "stop after edits" turn looks:** finish the change → one or two sentences describing what changed → optionally a single command line the user can paste → end the turn.
