# OES AI Roadmap — autonomous execution queue

**Branch:** feature/syntax-helper
**Started:** 2026-05-21
**Process:** /loop picks next `pending`, implements via Agent, commits, marks `done`, advances.

---

## Item status legend
- `pending` — not started
- `in_progress` — agent running
- `done` — committed
- `blocked:<reason>` — needs human gate
- `skipped:<reason>` — deferred

## Loop instructions
On each tick:
1. Read this file
2. Find FIRST `pending` item in priority order (Tier 0 → 1 → 2 → 3)
3. If `blocked`/`skipped` — emit status report and STOP loop until human review
4. Else mark `in_progress`, spawn Agent with `subagent_type: Backend Architect` (or Frontend Developer for UI, MCP Builder for MCP tools)
5. Agent implements + builds + tests + commits to feature/syntax-helper
6. On success: mark `done` with commit SHA, advance to next tick
7. On failure: mark `blocked:<reason>`, STOP loop, report to user
8. Build rule: `cmake --build build --parallel 3` (NEVER all cores)
9. Test rule: do NOT regress baseline (33 fail / 273 total)
10. Commit rule: no Co-Authored-By Claude, no AI attribution
11. Use `oes-platform` skill rules for C++ work

---

## Tier 0 — MCP polish (quick wins, days)

- [x] **done** mcp-001 (44d0aa8c): Tool annotations on all 12 oes-mcp tools. Build PASS, smoke PASS.
- [x] **done** mcp-002 (89469c2e): `compile_check` un-defer via `CompileCheckCapture::DoSetError` override on `ibCompileCode`. Real errors + line/column. Build PASS, smoke PASS. Requires loaded config; rejects `--no-config` mode.
- [x] **done** mcp-003 (69e97c39): `sigma_check` HTTP proxy to Pugi MCP via cpp-httplib + OpenSSL. Config cached, offline fallback structured. `openWorldHint=true`. **Pugi-side blocker:** endpoint returns 503 "Phase-0 scaffold, set OES_MCP_MOCK_MODE=true" — proxy works correctly, real validation pending Pugi team.
- [x] **done** mcp-004 (a74e54a3): `outputSchema` + `structuredContent` on `meta_query`, `list_objects`, `read_module`. JSON Schema Draft 2020-12. Smoke assertions added. Build PASS.

## Tier 0 — MCP polish (quick wins, days)

- [x] **done** mcp-005 (2fcfbb88): 4 Pugi template proxy tools live (`oes_templates_list/get/customize`, `oes_demo_data_get`). `PugiHttpInvoke` helper extracted, reusable for future proxy tools. `oes_templates_list` has outputSchema. Smoke + manual verified against real Pugi — 4 templates returned.

- [x] **done** skill-001 (inline, no commit — skill lives in ~/.claude/skills/): `oes-config` upgraded with Section 1.5 "Template-first workflow" — 4 templates table, 3 modes (Wizard/Sigma/Hybrid), demo data semantics, quotas, endpoint, known caveats. Section 13 reference updated with full Pugi + oes-mcp tool catalog.
- [x] **done** skill-002 (inline): `oes-platform` upgraded with Section 12.5 "MCP tool inventory" — 12-tool annotation matrix, error envelope contract, Pugi proxy HTTP shape, headless boot rules, tool design rubric.

- [x] **done** skill-003 (inline): `oes-templates` skill created at `~/.claude/skills/oes-templates/SKILL.md` — 11 sections, starter-only specialist. 4 templates table, 3 modes (Wizard/Sigma/Hybrid), demo data semantics, workflow templates, routing matrix vs oes-config/oes-platform, quotas, known caveats, anti-patterns.

## Tier 1 — Killer features (1-2 months)

- [x] **done** t1-001 (9f20fa56): 3-layer concurrency — `configLock.{hpp,cpp}` POSIX flock + Windows LockFileEx, dead-pid sweep, `<config>/sys/.oes.lock` manifest. oes-mcp Layer 1 probe + Layer 2 `RequireLockStillHeld` + Layer 3 `.oes-mcp-mutation` broadcast. Designer `externalMutationNotifier` wxTimer полит + toast. 6 gtests pass.
- [x] **done** t1-wizard (9cc344c5): Designer template wizard live. 10 new files (templateWizard / Card / Preview / Customize / Applier), 2144 LOC. 3 pages: gallery с async fetch + thumbnails, preview wxNotebook (Структура/Demo/Модули), customize 3-mode (Без изменений / Ручная / Tweak with AI). Apply через metaBridge::HostMeta* с undo. Policy grant/restore (AllowAlways → Deny). File menu wired. Build PASS, baseline preserved.
- [~] **blocked:GUI-DEP** t1-002 (2026-05-21): Form layout authoring through MCP shipped as deferred stub. `form_layout_read` + `form_layout_set` registered with full annotations + outputSchema, DTO + validator backend types in place (`src/engine/backend/metaCollection/formLayoutBlob.{hpp,cpp}`), 12 gtests + smoke checks pin the deferral contract. Calls return `isError` with `structuredContent.errorCode = OES_E_FORM_BLOB_GUI_DEPENDENCY` / `OES_E_NOT_A_FORM` / `OES_E_NOT_FOUND` / `OES_E_NO_CONFIG`. **Architectural blocker:** the form data blob held by `ibValueMetaObjectFormBase::GetFormData()` is a binary chunk format whose outer envelope is parseable from backend (uses `ibReaderMemory::open_chunk*`) BUT each control's payload is read positionally by frontend `ibValueFrame` subclasses (`src/engine/frontend/visualView/ctrl/*.cpp` — button.cpp/textctrl.cpp/checkbox.cpp/etc). Backend has no neutral schema for which property fields each of ~25 control classes writes in which order. Resolution requires one of: (a) mirror all ~25 control-class property layouts as backend-side declarative schemas (large, two-place updates per new control), (b) split `ibValueFrame` into a data half (backend) + a visual half (frontend) — cross-cutting refactor across all controls, or (c) introduce a parallel XML form-DSL that Designer writes alongside the binary blob and MCP reads/writes the DSL. Recommendation: option (c) — least invasive, keeps existing binary blob path untouched, agents get a stable surface. Estimate after architectural decision: ~2 weeks. Stub commit: TBD.
- [x] **done** t1-003 (282f4aff): Functional test runner — 8 assertion builtins (AssertEquals/NotEquals/NotNull/True/False/Greater/Less/Throws) + `ibFixtureManager` SAVEPOINT-based RAII rollback (reuses existing nested-safe BeginTransaction/RollBack, no driver changes) + `run_tests` MCP tool with json/junit/text formats + 18 new gtests + smoke updated. Baseline preserved (33/298).
- [x] **done** t1-004 (9362e1d1): BAS / 1С → OES migration tool — `import_bas_xml` REAL path with mapping into mutations[], `import_bas_cf` registered as deferred binary-archive route that tells callers to export XML first. Smoke coverage pins both tool contracts.

- [x] **done** t1-feedback (inline): `docs/pugi-template-issues.md` created — tracker doc with issue lifecycle (OPEN/ACKNOWLEDGED/FIXED/WONTFIX), template format, known caveats table from Pugi handoff (5 watch-items), reference links. Ongoing fill-in as issues surface.
- [x] **done** t1-005 (5d79fbfb): `oes-rag-local` scaffold — Ollama HTTP client (Ping/Embed/Chat), JSON-on-disk index, lexical retrieval, cpp-httplib server (port 11700, /query /llm /health), oes-mcp `PugiHttpInvoke` degrade chain (transport+5xx → local fallback, env-gated, allow-list 6 tools). 11 files added. 6/6 smoke tests pass. v2 deferred: real FAISS, Ollama embedding round-trip, per-object chunking, streaming, multi-corpus, ICU Unicode.

## Tier 2 — Productivity (3-6 months)

- [x] **done** t2-001 (14a82c61): 8 new oes-mcp tools — `role_list` (REAL), `role_acl_read/set` (STUB, no permission matrix), `journal_query` (PARTIAL — Document mode REAL, Journal kind missing), `register_query` (PARTIAL — records REAL, balance/turnover stub), `register_write` (STUB — needs Posting tx context), `predefined_values_list` (REAL), `predefined_values_set` (STUB — undo lambda routing missing). Tool count 21→29. Architectural gaps logged.
- [x] **done** t2-002 (0fd34485): 5 MCP resources + subscribe/notify capability. 2 concrete (config/current REAL, sigma-rules STUB v1 hardcoded 4 rules), 3 templates (catalog/{name}, module/{owner}/{kind}, docs/syntax/{lang} — all REAL via existing tool proxy). In-memory subs, fire-and-forget notify. `notifications/resources/updated` wired into meta_create/edit/delete + write_module + save_config.
- [x] **done** t2-003 (92a6e39d): 7 MCP prompts + `prompts:{listChanged:true}` capability. `oes:new-catalog` / `oes:new-document` / `oes:migrate-1c-xml` / `oes:explain-object` / `oes:audit-security` / `oes:write-report` / `oes:write-form`. Each renders Sigma-driven workflow template. Validation: missing required arg → -32602, unknown name → -32602. **MCP triad complete: Tools 29 + Resources 5 + Prompts 7.**
- [x] **done** t2-004 (6de08efe): 5 refactoring tools — `find_references` (REAL — attr types + module regex), `rename_with_refs` (REAL — dryRun default, regex_replace + metaBridge edit + undo), `metadata_diff` (PARTIAL — inline mode REAL, file mode stub), `dependency_graph` (REAL — BFS in/out/both), `extract_module_to_common` (PARTIAL — dry-run shows mutations, apply deferred lexer-aware extractor). Tool count 29→34.
- [x] **done** t2-005 (9475a082): Auto-snapshot system — `ibSnapshotManager` backend, 4 mutation handlers wired (meta_create/edit/delete + write_module), 2 new MCP tools (snapshots_list REAL, snapshot_rollback dryRun-mode REAL/apply STUB). Env `OES_MCP_AUTO_SNAPSHOT` (true/false/hash-only). 9/9 gtests pass. Tool count 34→36.
- [x] **done** t2-006 (1c4d1b35): 3 transactional staging tools (begin/commit/rollback). Single-active-tx guard, ibGuid UUIDs, metaBridge::UndoStackSize() new API, epoch-safe rollback. Tool count 40→43.
- [x] **done** t2-007 (40ed5703): 4-mode permission selector in Designer chat top — read-only / confirm-all / confirm-writes / auto. Modes are wired to plugin mutation policy; approve temporarily elevates write ops and restores after apply/error/reject.
- [x] **done** t2-008 (db48d970): Custom slash commands UI in Designer Plugin Manager. Stored in `plugins.json5` as `slashCommands`; native chat popup merges user commands with built-ins and dispatches custom rows through `chat.send`.
- [x] **done** t2-009 (b4fac2a3): Doc-comment skill — F1 in the code editor sends `editor.skill op="doc"` for the selection or nearest procedure/function signature, opening the assistant pane and reusing the existing documentation-comment flow.
- [x] **done** hotfix-pugi-chat (ef6247d9): Designer chat transport hardening — Pugi headers use `X-Tenant-Id` + `User-Agent`, Pugi env compatibility (`PUGI_BASE_URL`, `PUGI_OES_API_KEY`, `PUGI_TENANT_ID`, `PUGI_OES_LOCALE`), locale normalization (`uk`→`uk-UA`), chat path uses `ai_chat_query`, HTTP transport exceptions are caught, and cpp-httplib clients are closed explicitly. Build + targeted aiBridge tests + live Pugi API smoke PASS.
- [x] **done** t2-010 (ff2fd13a): Neutral assistant action audit trail — aiBridge writes structured JSONL events to `~/.oes/ai-audit/aiBridge-YYYY-MM-DD.jsonl` for chat/review/agent/apply/commit-message actions. Records requestId/tool/status/counts/sizes, never tokens or full prompt/code, and never inserts attribution comments into user modules or commit messages.
- [x] **done** t2-011 (6eaca142): Smoke validation post-apply — `headless_smoke_run` MCP tool reports load/compile status and can run functional tests through the existing rollback fixture. Smoke test covers the tool in `--no-config` protocol mode.
- [x] **done** t2-012 (a5a45390): Sigma reject diagnostic playbook — `sigma_check` now appends a natural-language diagnostic from Pugi `llm_query` when the Pugi verdict is `isError`, `ok:false`, or a Pugi-side scaffold 5xx. `oes-mcp` also accepts Pugi env aliases (`PUGI_BASE_URL`, `PUGI_OES_API_KEY`, optional tenant, normalized locale). Live proxy smoke reaches Pugi; current Pugi `sigma_check` still returns scaffold 503, tracked in Tier 4.

## Tier 2.5 — mcp-1c parity (inspired by feenlace/mcp-1c MIT)

Conkurent analysis (mcp-1c free tier 9 tools, Go binary, BM25 + BSL synonyms, auto-install, --ci mode). Their MIT code studied 2026-05-21. We borrow patterns + tool name shapes for cross-tool compatibility. Their paid tier (linter/optimizer/semantic-search) = our Pugi Pro roadmap.

- [x] **done** t2-013 (d57d9e2a): `execute_query` + `validate_query` MCP tools — read-only SQL guard accepts SELECT/WITH only, rejects DDL/DML/control tokens before prepare, binds params through `ibPreparedStatement`, caps results to `maxRows`, and returns structured columns/rows. Smoke covers allowed SELECT and rejected DROP.
- [x] **done** t2-014 (2c53bf71): `get_event_log` MCP tool — read-only event journal facade. Returns `available:false` with an empty list when `sys_event` is absent, and reads/filter rows by discovered date/level/user columns when a config has the table. Smoke covers tool registration.
- [x] **done** t2-015 (0cd77658): `oes_syntax_help` MCP tool — local lookup for OES keywords, built-in functions, opcode families, and short CES/VES examples from the product reference. Works without a loaded config; smoke covers a Date/builtin query.
- [x] **done** t2-016 (d8631832): `search_text` / `search_code` ranking pass — OES RU/UA/EN synonym expansion, term scoring, score-sorted hits, `maxResults`, and structuredContent. Disk-cache/non-blocking cold-start indexing is covered by t2-018.
- [x] **done** t2-017 (f136893d): `--install` / `--install-dry-run` command in `oes-mcp` — generates and optionally runs `claude mcp add <name> --transport=stdio --command=<this-binary> --args=<configPath>`. Help updated; dry-run verified. Designer wizard wrapper remains t3-004.
- [x] **done** t2-018: Non-blocking config load — normal stdio mode starts JSON-RPC immediately and loads the configuration in a background thread; `initialize`/`tools/list` work before metadata is ready, config-bound tools return structured `OES_E_CONFIG_LOADING`, and `config_info`/`oes://config/current` expose `loading`. CI mode remains blocking/fail-fast. Build + smoke PASS.
- [x] **done** t2-019 (c7bd0c9a): CI/CD mode `--ci --json` — loads the config, runs `headless_smoke_run` as the quality gate, emits JSON/text report, and exits 0/1 for pipelines. `--no-config` returns JSON failure with exit 1.
- [x] **done** t2-020 (dec7f315): Tool name aliases — `get_metadata_tree` → `list_objects`, `get_object_structure` → `meta_query`, `get_configuration_info` → `config_info`, `search_code` → `search_text`. Cross-tool compatibility. Tool count 39→43.
- [x] **done** t2-021: Stderr discipline check — pipe-mode stderr redirects to `~/.cache/oes-mcp/stderr.log` by default, `OES_MCP_STDERR=inherit|log` override documented, wx asserts disabled in redirected stdio mode. Build + smoke PASS.

## Tier 3 — Polish (6-12 months)

- [x] **done** t3-001: Web client invalidation broadcast — `wfrontend` sweep now watches the MCP mutation marker (`sys/.oes-mcp-mutation`) written by oes-mcp, bumps `metaGeneration`, evicts web sessions, and reloads metadata through the same Close/Load/Run path used by the existing `sys_config.file_guid` watcher. Build `wfrontend` + `wenterprise-server` PASS.
- [x] **done** t3-002: Multi-LLM profile switcher UI in chat header — native pane adds a per-request profile selector (`По умолчанию`, `Pugi`, `OpenAI fast`, `OpenAI quality`), chat envelopes carry `profile`, and aiBridge resolves it to Pugi MCP or OpenAI-compatible endpoint/model (`MODEL_FAST`, `MODEL_QUALITY`) without rewriting plugin env.
- [x] **done** t3-003: Voice input in chat pane — native wx pane now exposes a `Голос` button that focuses the chat input and hands off to OS-level dictation (macOS/Windows/Linux status text). Full Web Speech API path is not applicable after the wxWebView removal.
- [x] **done** t3-004: Onboarding wizard — Designer Tools → `Подключить AI…` opens a native setup dialog with the generated `claude mcp add oes ...` command, aiBridge env-file path, required Pugi keys, smoke prompt, demo `/agent` prompt, copy buttons, and a direct Plugins dialog handoff.
- [x] **done** t3-005: Telemetry loop — `oes-mcp` writes one structured JSONL row per `tools/call` to `.oes/mcp-audit/mcp-YYYY-MM-DD.jsonl` (tool, ok/error, duration, arg/result sizes; no prompt/code payload). Weekly aggregation remains a downstream reporting task.
- [x] **done** t3-006: Resumable chat from past message — user-message rows now expose `Редактировать и повторить`; clicking it moves that prompt back into the input, truncates later transcript entries, saves the shortened history, and lets the user edit/rerun forward.
- [x] **done** t3-007: Smart Snap — chat header now lets users pin `@Kind.Name` metadata tokens from the input; pinned objects are shown in the header and automatically resolved into the hidden context block for every later message until cleared or a new chat starts.
- [x] **done** t3-008: Score-gated reliability — aiBridge now accepts Pugi result fields `confidence` / `selfConfidence` / `suitabilityScore` / `reliability.score`, normalizes them to `chat.end.meta.confidence`, and Designer flags `<70%` as a visible suitability warning before the user applies output.
- [x] **done** t3-009: Tech debt scoring 0-100 — new read-only MCP tool `tech_debt_score` scores all modules or one module using LOC, branch-keyword complexity, TODO/FIXME markers, duplicated non-comment lines, and unreferenced procedure/function declarations; returns structured hot spots for reporting.
- [ ] **pending** t3-010: Security audit MCP tool — port mcp-1c's 11 SEC rules to OES context (SQL injection scan, eval misuse, unsafe deserialize). Pro tier. ~1 week.

## Tier 4 — Infrastructure / business

- [ ] **pending** t4-001: Pugi tier billing infra — free / pro / enterprise / air-gapped. Usage tracking, quota enforcement, upgrade flow. PUGI-SIDE, not OES. Blocked on Pugi team.
- [ ] **pending** t4-002: Schema/RAG re-ingestion pipeline — БП обновляется → cron Pugi re-index. PUGI-SIDE. Blocked.
- [ ] **pending** t4-003: Pugi API contract cleanup — document/keep stable `ai_chat_query`, `llm_query`, `oes_agent`, `oes_agent_resolve`, template tools; accept both `X-Tenant-Id` and legacy `X-Pugi-Tenant` for bridge compatibility; publish `/api/oes-mcp/tools` schema for OES smoke tests. PUGI-SIDE.
- [ ] **pending** t4-004: Pugi locale compatibility — accept legacy short locales (`uk`, `ru`, `en`) or expose a clear migration warning. OES now normalizes, but external clients still hit the strict zod enum. PUGI-SIDE.

## Tier 5 — VM performance (long-term, after product-market fit)

- [ ] **pending** t5-001: Threaded dispatch (computed goto on GCC/Clang) in ibProcUnit::Execute. ~2 days.
- [ ] **pending** t5-002: Inline caches на OPER_CALL_METHOD + ContextProp dispatch. ~2 weeks.
- [ ] **pending** t5-003: TYPE_DELTA → adaptive specialization (PEP 659 механизм). ~2 weeks.
- [ ] **pending** t5-004: AOT bytecode cache hash-keyed invalidation, file format, version-tolerance. ~2 weeks.
- [ ] **pending** t5-005: Closure capture (Lua upvalue model) — `ibUpvalue` heap cell, `OPER_CLOSE_UPVALS` opcode. ~3 weeks.

---

## Architecture markers (per item)

Each item lives in ONE of:
- **[platform]** — backend.dll / frontend.dll / designer / oes-mcp standalone binary. Ships as product core.
- **[plugin:<name>]** — separate .dll loaded via ABI v4. Pre-bundled in Designer install.
- **[binary:<name>]** — standalone helper binary in PATH after install.

Mapping:
- mcp-* → [platform] oes-mcp
- t1-001 Designer↔MCP concurrency → [platform] backend+oes-mcp
- t1-002 Form layout MCP → [platform] backend + oes-mcp
- t1-003 Functional test runner → [platform] backend builtins + [plugin:oes-test-runner] UI
- t1-004 1С migration → [plugin:oes-importer-bas-1c]
- t1-005 Local Ollama RAG → [binary:oes-rag-local]
- t2-* MCP tools/resources → [platform] oes-mcp
- t2-004 Refactoring primitives → [platform] backend + oes-mcp
- t2-007 4-mode permission → [platform] designer + [plugin:aiBridge]
- t3-001 Web client invalidation → [platform] backend + wfrontend
- t3-004 Onboarding wizard → [platform] designer
- t3-005 Telemetry → [platform] oes-mcp
- t5-* VM perf → [platform] backend

## Out-of-the-box distribution

Designer installer bundles:
- `plugins/aiBridge.dll` (pre-installed)
- `plugins/oes-importer-bas-1c.dll` (after t1-004 lands)
- `plugins/oes-test-runner.dll` (after t1-003 lands)
- `binaries/oes-mcp` (in PATH)
- `binaries/oes-rag-local` (optional, after t1-005 lands)
- `skills/oes-config/SKILL.md` (copied to ~/.claude/skills/ by wizard)

Fresh install → wizard → Pugi key → claude mcp add → ready in 3 clicks.

## Skipped / deferred

- (none yet)

## Blocked items

- **Pugi sigma_check production endpoint** (blocks mcp-003 real validation) — Pugi returns 503 Phase-0 scaffold. Action: ping Pugi team to ship real sigma_check beyond mock mode. Our proxy is ready.
- **Pugi confidence fields for chat/agent** — OES now consumes confidence metadata, but Pugi must emit one of `confidence`, `selfConfidence`, `suitabilityScore`, or `reliability.score` from `ai_chat_query`, `oes_agent`, and `triple_review` result envelopes for the gate to activate on real traffic.
- **t1-002 retry** — Anthropic API 529 Overloaded at 2026-05-21. Transient. Retry agent in 10-30 min.

## Done

- (built before this roadmap: child metadata, AI Property tooltip, Subtask/TODO/Markers, oes-mcp base, oes-platform skill, oes-config skill, deep reference docs)

---

## Auto-update protocol

Loop must update this file via Edit tool after each item:
- Change `pending` → `in_progress` → `done`
- Append commit SHA when done
- If blocked: change to `blocked:<reason>` and stop

Loop must NOT skip items based on personal judgement — order is sacred. Only `blocked` is stop signal.
