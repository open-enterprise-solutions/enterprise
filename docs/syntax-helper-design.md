# Syntax helper — interactive language reference panel

> **Status:** LANDED (partial) 2026-05-26 — backend corpus + designer
> panel + editor look-up + pack-on-build. Deferred: editor margin
> `...` marker (Phase 1.3 nice-to-have), `helpEditor.exe` standalone
> utility, per-configuration corpus (Phase 5), web HTTP endpoints
> (Phase 6), help editor + review gate (Phase 7), skeleton generator
> + LLM fill + validation gate (Phase 2). Full breakdown below.

## Implementation status (2026-05-26)

First port from `feature/syntax-helper` (upstream branch) landed on
`feature/syntax-helper-port` with a stricter scope — backend +
designer panel + editor look-up + pack-on-build only. AI / MCP /
template-wizard / pluginWebPane work from the same upstream branch
stays out of this PR.

### Done

| Subphase | What | Verification |
|---|---|---|
| 1.1 | `backend/syntaxHelper/` — `ibHelpEntry / Corpus / Loader / Resolver / Category / LoadError` (9 files from upstream, ~1.5k lines) | clean Debug\|Win32 build |
| 1.1 | `ibHelpService` — owned subsystem on `ibApplicationData` (pattern: logger / lockManager); `CanonicaliseLocale` open-set first-2-chars, fallback "en"; graceful fallback when locale dir missing | smoke: launcher / designer start without crash |
| 1.1b | `HelpBucketSource` abstraction with `FileSystemSource` (dev) + `ZipSource` (prod, `wxConvUTF8` decode + `./` strip + empty-name skip) | `.hlk` round-trip parses cleanly |
| 1.1b | Pack-on-build via `[System.IO.Compression.ZipFile]::CreateFromDirectory` (MSBuild) + `cmake -E tar c --format=zip` (CMake); output `<exe>/help/<locale>.hlk` | `bin\Win32\Debug\help\{en,ru,uk}.hlk` produced on every build |
| 1.2 | `frontend/syntaxHelper/` — 14 view files (PaneView / TreeView / IndexView / SearchView / DetailView / ChooserDialog / DragSource) | panel renders in designer |
| 1.2 | AUI pane (`wxAUI_PANE_HELP = "syntaxHelperWindow"`); `EnsureHelpPane` / `ToggleHelpPane` lazy lifecycle on `ibFrontendDocMDIFrameDesigner` | Help → Syntax Helper toggles the pane |
| 1.3 | `ibCodeEditor::GetIdentifierUnderCursor` + `OpenHelpForCursor` resolver pipeline + chooser dialog wiring; RawCtrl+Alt+F1 (pane toggle), RawCtrl+F1 (cursor look-up) | live lookup via menu + shortcut |
| 1.3b | Editor right-click context menu (Cut/Copy/Paste/SelectAll + Look up in Syntax Helper) routing wxEVT_MENU upward through the parent chain | context menu item posts to host designer |
| 1.3c | Drag target — wxStyledTextCtrl built-in handles `wxTextDataObject` from `helpDragSource` | drag from panel into editor inserts identifier |
| - | UTF-8 BOM on 25 syntax-helper source files (MSVC without `/utf-8` flag was producing em-dash mojibake in `_()` literals) | no `вЂ—` in panel labels |
| - | `.hbk → .hlk` rename across loader / service / build / docs (avoids collision with 1С proprietary `.hbk` format) | `bin/help/*.hlk` artefacts |
| - | Menu items live in Help menu (was Tools, moved per user preference); macOS macro-Help-menu interception risk noted in comment | designer Help menu carries Syntax Helper + Look up + About |
| - | Compact appData footprint: +13 lines `.h` (forward decl + member + static getter) / +6 lines `.cpp` (include + lazy init in `InitLocale`); ownership mirrors `ibLogger` / `ibLockManager` | no `m_helpCorpus` / `RebuildHelpCorpus` directly on appData |
| - | `CMakePresets.json` (8 presets for VS-based CMake `Open Folder` workflow) | VS 2022 picks presets from dropdown |

### TODO (deferred)

| Subphase | What | Why deferred |
|---|---|---|
| 1.3 | Editor margin marker `DEF_HELP_HINT_ID=3` with `...` next to call expressions | Phase 4 nice-to-have; needs precompile-context plumbing to know "is the cursor inside a call expression"; ships well after basic look-up is in user hands |
| 1.4 | `helpEditor.exe` standalone utility for content editing | Scope questions outstanding (layout / editable fields / markdown preview / validation / reviewed-flip / locale switching) — start after user picks defaults |
| 5 | Per-configuration corpus tier (`appData->GetConfigCacheDir()/help/<locale>/`) for metadata-driven entries (Catalog/Document attributes & methods) | Needs configuration-save hook + per-config rebuild trigger; out of scope until first user complains the platform corpus misses their custom Catalog attribute |
| 6 | Web HTTP endpoints `/api/help/{tree,entry/<id>,resolve,search}` on wenterprise-server | wfrontend has no consumer yet (no React shell touching it); deferred until web client UI work picks up the corpus |
| 7 | Help editor: `reviewed: true` flip workflow + Designer-side review gate | Same blocker as 1.4 — needs the editor tool first |
| 2 | Skeleton generator + LLM fill + validation gate | Current corpus is hand-authored; tooling lands when content scales past manual editing. Original design proposed Python; user prefers C++ (e.g. `classChecker --dump-help` + gtest validation) — no Python in repo |
| - | Tests — no `test_help*.cpp` in the port (none in upstream either). Smoke is manual. | gtest fixtures pending; corpus-load unit test plus resolver fixtures would be ~150 lines |
| - | Content quality — `global_functions.json` is skeleton-only (`reviewed: false`, empty description / parameters / return / example); `keywords.json` + `primitive_types.json` are populated and `reviewed: true` | Phase 2 generator + LLM fill remains the cost driver; current state ships with usable keywords + types and stubs for functions |
| - | `wxLogMessage` debug noise in `wxZipInputStream` parsing path (left for now to ease zip-format diagnosis); strip after smoke validation stabilises | Diagnostic value during current shake-out |

### Files touched in this port

37 files changed, +4682 / -5 vs `origin/develop`.

- 11 new files in `src/engine/backend/syntaxHelper/` (9 upstream + 2 my `helpService.{h,cpp}`)
- 14 new files in `src/engine/frontend/syntaxHelper/` (all upstream, includes adapted to `syntaxHelper/` namespace)
- 15 JSON content files in `syntaxHelper/{en,ru,uk}/` (repo root, not under `src/`)
- 1 design doc `docs/syntax-helper-design.md` (this file)
- 1 `CMakePresets.json` (repo root, 8 build presets)
- Build wiring: `backend.vcxproj` + `backend.vcxproj.filters` + `backend/CMakeLists.txt` (+`StageSyntaxHelperContent` Target with `[IO.Compression.ZipFile]`); `frontend.vcxproj` + `frontend.vcxproj.filters`
- `appData.{h,cpp}` — forward decl + member + static getter + lazy init (compact, isolated)
- `mainFrameDesigner.{h,cpp,Menu.cpp,Parts.cpp}` — help-only hunks (without AI / template / plugin code from upstream)
- `mainFrame/mainFrame.h` — `wxAUI_PANE_HELP` + `wxID_FRONTEND_SYNTAX_HELPER` / `_LOOKUP` IDs
- `codeEditor.{h,cpp}` — `GetIdentifierUnderCursor` + `OnContextMenu` + bindings (no debug shortcut routing — that lands separately)

### Diff vs upstream `feature/syntax-helper` branch

The upstream branch carries ~250 additional files for AI / MCP / pluginWebPane / templateWizard / various designer panels — explicitly out of scope for this port. This port also:

- Renames the backend / frontend subsystem directory `help/ → syntaxHelper/` for naming consistency with the content root (was `data/help/` upstream → `syntaxHelper/` here)
- Replaces `data/help/` content path with `syntaxHelper/` (repo root, alongside `docs/`, `locale/`)
- Renames archive extension `.hbk → .hlk` (avoids overloading the 1С proprietary `.hbk` extension)
- Pulls `ibHelpService` out of `ibApplicationData` (upstream had `GetHelpCorpus` / `ReloadHelpCorpus` directly on appData — port follows the `ibLogger`/`ibLockManager` subsystem pattern)
- Drops the `bring-your-own-.hbk` external-platform integration path (was tied to the 1С ecosystem)
- Strips Cyrillic UI strings from comments + design doc (code stays ASCII-only; user-facing labels stay in `_()` macros for i18n)

---

**Status:** Design v5. Four rounds of triple-review. v1 closed
the original design holes (thread-safety, error model, pointer
lifetime, id grammar, categoryPath locale story, factual
misreferences). v2 added overlay rules, private cache directive,
C++17-safe atomic_shared_ptr publish pattern, content-hash
fingerprint, per-entry try/catch loader. v3 closed the v2 P0
(session vs appData ownership), unified the ambiguous-name
chooser as a single modal dialog with three buttons (Show /
Cancel / Help), pinned `Ctrl+F1` + "Look up in Syntax Helper"
context menu as the canonical trigger surfaces, locked the top-
level category layout to a low-code-platform-conventional set
(Language overview / Global context / Classic UI + Managed UI /
Common objects / Applied objects / System enums / Queries), and
added §9 "Reusable open-source components" with a license
compatibility matrix. Target branch
`feature/syntax-helper`. Replaces today's tooltip-only
IntelliSense help (`s_listHelpDescription` short strings) with a
categorized, searchable reference panel parallel to the editor.

**Goal:** every identifier the script-author can type — keyword,
built-in function, system enum, metadata class, attribute, method —
has a one-click path to a structured description (signature, semantics,
parameters, return value, availability tier, example) shown in a
dockable sidebar, with an inline `...` button next to procedure
parameters and an ambiguous-identifier chooser dialog.

The pure data layer (help corpus + lookup) must work headless so the
web frontend (wfrontend.dll) gets the same hover/click help as the
desktop designer.

## Table of contents

1. [Current state](#1-current-state)
2. [Help entry — data model](#2-help-entry--data-model)
3. [Backend — help corpus subsystem](#3-backend--help-corpus-subsystem)
4. [Help corpus generation pipeline](#4-help-corpus-generation-pipeline)
5. [Frontend — Desktop sidebar pane](#5-frontend--desktop-sidebar-pane)
6. [Frontend — Web (wfrontend.dll)](#6-frontend--web-wfrontenddll)
7. [Phasing](#7-phasing)
8. [Resolved decisions + remaining open questions](#8-resolved-decisions--remaining-open-questions)
9. [Reusable open-source components](#9-reusable-open-source-components)
10. [Non-goals (for this iteration)](#10-non-goals-for-this-iteration)

## 1. Current state

### What already exists

* `s_listKeyWord` (**58** entries in
  `src/engine/backend/compiler/translateCode.cpp:25-99` — 44 core
  language keywords + 15 LINQ keywords; ordering is load-bearing
  per the `KEY_FROM..KEY_INTO` comment at translateCode.cpp:82-83).
  Each row is `ibKeyWords{ wxString m_strKeyWord;
  wxString m_strShortDescription; }`
  (`translateCode.h:18-21`). Descriptions are mostly empty today.
* `s_listHelpDescription` — file-static
  `std::map<wxString, void*>` at `translateCode.cpp:16`, populated by
  `LoadKeyWords()` at `translateCode.cpp:244-257`. The `void*` value
  is a pointer to the originating `m_strShortDescription`. The map
  has no external accessor; we add one (`GetKeywordHelp`) or refactor
  to `const wxString&` returns during Phase 1.
* `ibValueSystemFunction::PrepareNames()`
  (`src/engine/backend/system/systemManager.cpp:113-217`) — registers
  ~85 built-in functions via `AppendFunc(name, argCount, signature)`
  / `AppendProc(...)` on an `ibMethods`-derived helper. The CLAUDE.md
  "88 functions" is approximate — the real number drifts as upstream
  adds new built-ins. Phase 1 generator pulls live counts.
* `ibValueMetaObject*` hierarchy — 11 business object types
  (Catalog / Document / Register / …). Each subclass declares its
  attribute / method surface via its own
  `virtual void PrepareNames() const;` — there is NO uniform
  `GetChildren()` on the base. The skeleton generator must dispatch
  per-subtype, or scrape the same meta-tree walker the configurator
  already uses for JSON export.
* `ibPrecompileContext::FindFunction / FindVariable` —
  `src/engine/frontend/win/editor/codeEditor/codeEditorInterpreter.h`
  (FRONTEND, not backend; the resolver-using sidebar therefore links
  against `frontend.dll` for editor-context resolution; headless
  callers — wenterprise-server — pass query-string fields instead of
  a live precompile context, §3).
* Upstream commit `9464237d` shipped an "All Functions" intellisense
  list — a flat enumeration of system functions. The categorized tree
  + html detail pane this doc proposes is the next step on top of it.
  The autocomplete dropdown stays; the syntax helper adds a
  "more info" affordance pointing into the help pane (§9).
* Designer AUI host is `ibFrameManager` (not `ibAuiManager` —
  earlier doc draft had the wrong name), declared at
  `src/engine/frontend/mainFrame/mainFrame.h:235,273` and consumed
  by `ibAuiDocDesignerMDIFrame` at
  `src/engine/designer/mainFrameDesigner.h:7`. Existing pane-creation
  call sites at `mainFrameDesignerCmd.cpp:39, 54, 128`.
* Scintilla margins in the editor at
  `src/engine/frontend/win/editor/codeEditor/codeEditor.cpp` —
  `DEF_LINENUMBER_ID=0`, `DEF_BREAKPOINT_ID=1`, `DEF_FOLDING_ID=2`.
  Adding a `DEF_HELP_HINT_ID=3` plus a bitmap and an extra branch in
  the existing `OnMarginClick` is the integration point for the
  `...` button.
* `wenterprise-server` HTTP routes are inline `svr.Get(...)` /
  `svr.Post(...)` lambdas in `wenterprise-server/main.cpp:441+` —
  there is NO pluggable route registry today. The new `/api/help/*`
  endpoints land as more inline lambdas alongside the existing
  ones. A future route-registration table is out of scope for this
  feature.

### What is missing

* Long-form descriptions (semantics, examples, availability tier).
* Category tree — today there is no hierarchical organization of the
  reference (the proposed top-level groups are Applied objects /
  Common objects / Queries / System enums / etc.).
* UI shell — no sidebar pane in either Designer or the web client.
* Inline `...` button on parameter lists in the editor.
* Ambiguous-name chooser dialog (e.g. `Date` resolves to a type, a
  Document property, a function, and a MomentTime attribute).
* Full-text search across descriptions.

## 2. Help entry — data model

```cpp
// Single help topic, single locale. The corpus holds one bilingual
// pair only at the name level (nameLocal + nameEn); rich text lives
// in one locale per file. JSON files are locale-scoped (§2.3), so
// description / example / parameters are implicitly in the file's
// locale — no per-locale fork inside the struct.
struct ibHelpEntry {
    wxString id;              // canonical key, see §2.2 — e.g. "fn.DateToStr"
    int      schemaVersion;   // loader-side; on-disk lives in bucket header
    wxString nameLocal;       // localised identifier (e.g. "Date" in en)
    wxString nameEn;          // English identifier — stable join key
    wxString signature;       // single-line call form
    wxString description;     // markdown subset → rendered to HTML at load
    wxString syntaxBlock;     // formatted declaration / call form
    wxString parameters;      // markdown table or `<dl>`-style key:value
    wxString returnDescr;     // return-value semantics
    wxString example;         // markdown code block(s)
    wxString availability;    // tier list — e.g. "Thick client, Web client, Server" (localised)

    enum Kind {
        kKeyword, kSystemFunction, kSystemConstant, kSystemEnum,
        kMetaObjectType, kMetaObjectAttribute, kMetaObjectMethod,
        kPrimitiveType, kCollection, kEvent, kOperator
    };
    Kind kind;

    // Locale-stable category keys, NOT display strings. Display name
    // comes from a per-locale category-dictionary lookup (§2.4).
    // e.g. {"applied_objects","documents","properties","date"}
    std::vector<wxString> categoryKeys;

    // Opaque ids of related entries — resolver walks them, loader
    // validates each at LoadAll time, dangling ids are demoted to P3
    // warnings (entry still loads).
    std::vector<wxString> seeAlso;

    // Author-set after Phase 7 (help editor / validation gate). LLM-
    // filled drafts ship with reviewed=false; UI may visually mark
    // them; CI gate refuses to ship the platform corpus with any
    // reviewed=false entries past the cutoff PR.
    bool reviewed = false;
};
```

### 2.1. Schema version + locator

* **`schemaVersion`** — top-level field in each bucket file, NOT per entry.
  Current = `1`. Loader rejects `schemaVersion > kCurrentSchema` (forward
  incompat). Reads `schemaVersion < kCurrentSchema` through an in-memory
  migration chain. New optional field = no bump; rename / removal /
  semantics change = bump + migration test against a golden fixture.

* **Locator** — corpus root is resolved as
  `appData->GetInstallDataDir() / "help" / <locale>` for the platform
  corpus, and `appData->GetConfigCacheDir() / "help" / <locale>` for
  the per-configuration corpus (§5 metadata-driven). Both directories
  are constructed by `appData` at startup so desktop and
  `wenterprise-server` agree without per-frontend path logic.

### 2.2. Canonical `id` grammar

Format: `<kind-prefix>.<englishName>[.<parent-englishName>]`

| Kind                  | Prefix     | Example                       |
|-----------------------|------------|-------------------------------|
| Keyword               | `kw`       | `kw.If`                       |
| System function       | `fn`       | `fn.Message`                  |
| System constant       | `const`    | `const.LineBreak`             |
| System enum value     | `enum`     | `enum.MessageStatus.Info`     |
| Primitive type        | `type`     | `type.Date`                   |
| Collection class      | `cls`      | `cls.ValueList`               |
| Metadata object type  | `mo`       | `mo.Catalog.Invoice`          |
| Metadata attribute    | `attr`     | `attr.Catalog.Invoice.Code`   |
| Metadata method       | `meth`     | `meth.Document.Invoice.Write` |

Metadata ids carry the **concrete object name** (`Invoice`,
`UserList`, etc.) as the second segment AND the `Kind` of metadata
object as the first qualifier. Two different documents with the same
attribute name (`Code`) get distinct ids
(`attr.Document.Invoice.Code` vs `attr.Document.Receipt.Code`).
Form events use `ev.Form.<EventName>` for form-shared events, and
`ev.Document.Invoice.<EventName>` for object-form-specific events.
| Form event            | `ev`       | `ev.Form.OnOpen`              |
| Operator              | `op`       | `op.Assign`                   |

Loader builds `std::unordered_map<wxString, EntryIndex>` from id to
slot. **Within a single corpus** (all buckets of one locale of one
source — platform corpus OR per-config corpus) duplicate ids are a
fatal load error. **Across the two corpora** (platform + per-config,
§3.4 reload swap merges them) the per-config id wins — see §3.6
overlay rule. English name segments are stored as authored
(PascalCase identity-preserved), NFC-normalized only; lookup is
case-sensitive — `fn.Message` and `fn.message` are different ids.
This keeps ids stable across slight rename drift while matching the
PascalCase convention used in every example throughout this
document.

### 2.3. JSON corpus schema

Stored in `data/help/<locale>/<bucket>.json`. One bucket per top-level
category. UTF-8, BOM tolerated (loader strips it). Bucket file shape:

```json
{
  "schema_version": 1,
  "locale": "ru-RU",
  "entries": [
    {
      "id": "type.Date",
      "name_local": "Date",
      "name_en": "Date",
      "kind": "primitive_type",
      "category_keys": ["common_lang","primitive_types","date"],
      "signature": "Date(<value>) / Date(<year>, <month>, <day>)",
      "description": "Values of this type hold a date with second precision...",
      "syntax_block": "Date(<value>)",
      "parameters": "<value> — string or numeric expression representing the date.",
      "return_descr": "A value of type Date.",
      "example": "Date('20170323104525') = '2017-03-23 10:45:25'",
      "availability": "Thin client, web client, mobile client, server",
      "see_also": ["fn.DateToStr","fn.StrToDate"],
      "reviewed": true
    }
  ]
}
```

Notes:
- JSON keys are `snake_case`; C++ fields are `camelCase`; the loader
  maps explicitly so the on-disk format stays idiomatic for any
  client (web JS / Python tooling) without C++ naming bleed.
- `return` is NOT used as a JSON key — `return_descr` instead — JS
  / Python parsers handle it without reserved-word friction.
- Loader robustness: each entry is parsed inside a per-entry
  `try { ... } catch (const nlohmann::json::exception&) { ... }`
  block — `nlohmann::json::value(key, default)` handles **missing**
  keys but still throws on **type-mismatched** present keys, so the
  try/catch is the only thing that actually upholds the non-throwing
  loader contract (§3.3). On any per-entry exception the loader
  records an `ibHelpLoadError{kEntrySkipped}` and continues with the
  next entry. Bucket-level errors (file not readable, top-level JSON
  malformed) record `kFatal` and abort that bucket only, NOT the
  whole corpus.

### 2.4. Category dictionary

Sibling to the buckets: `data/help/<locale>/_categories.json`:

```json
{
  "schema_version": 1,
  "locale": "ru-RU",
  "categories": {
    "applied_objects": "Applied objects",
    "documents":       "Documents",
    "properties":      "Properties",
    "common_lang":     "Language overview",
    "primitive_types": "Primitive types",
    "date":            "Date"
  }
}
```

Lookup at render time turns `categoryKeys: ["applied_objects",
"documents","properties","date"]` into the localized tree path. Adding
a locale = adding one dictionary + one buckets directory; no entry
schema change.

## 3. Backend — help corpus subsystem

New module: `src/engine/backend/help/`.

```
help/
├── helpEntry.h         // ibHelpEntry struct + Kind enum
├── helpCorpus.h/.cpp   // immutable loaded snapshot + indexes
├── helpCategory.h      // category tree node
├── helpLoader.cpp      // JSON → ibHelpEntry parse, error collection
├── helpResolver.h/.cpp // free function — corpus + name + hint → matches
└── helpLoadError.h     // structured loader-error record
```

Backend-only — no `#include <wx/...>` beyond what backend.dll already
permits (`wxString`, `wxLogMessage`). String I/O at the JSON boundary
uses `std::string` UTF-8; conversion to/from `wxString` happens inside
this module so consumers see only `wxString`. The headless HTTP path
on `wenterprise-server` round-trips UTF-8 without wx conversions on
the hot path — the server converts once at response serialization.

### 3.1. Ownership — no singleton, lifecycle on `appData`

`ibHelpCorpus` is owned by `ibApplicationData` (the existing
`appData` macro), not a Meyers singleton. Rationale: every other
large subsystem in OES — `activeMetaData`, the connection pool, the
session registry — hangs off `appData` so lifetime ordering is
deterministic (build at startup, drop on shutdown in reverse). A
second `Instance()` pattern would fragment that ownership.

```cpp
// ibApplicationData adds:
std::shared_ptr<const ibHelpCorpus> GetHelpCorpus() const;  // never nullptr
void                                ReloadHelpCorpus();      // §3.3
```

Access pattern (every caller, desktop + web):

```cpp
auto corpus = appData->GetHelpCorpus();   // shared_ptr snapshot
const ibHelpEntry* e = corpus->FindById(wxT("fn.Message"));
// 'corpus' shared_ptr keeps the snapshot alive past any reload.
```

### 3.2. Public API

```cpp
class BACKEND_API ibHelpCorpus {
public:
    // Frozen-after-construction. The shared_ptr returned by
    // appData->GetHelpCorpus() points to an immutable snapshot.
    // Reload (§3.3) builds a NEW corpus and atomically swaps; old
    // snapshots stay alive until their last reader releases.

    const ibHelpCategory* GetRoot() const;
    const ibHelpEntry*    FindById(const wxString& id) const;
    std::vector<const ibHelpEntry*> AllEntries() const;

    // Full-text + prefix search — built at construction.
    std::vector<const ibHelpEntry*> SearchPrefix(const wxString& prefix) const;
    std::vector<const ibHelpEntry*> SearchText(const wxString& query) const;

    // Diagnostics
    const std::vector<ibHelpLoadError>& LoadErrors() const;
    wxString Fingerprint() const;   // SHA-256 over bucket file content; ETag source — §6.1
    int      EntryCount() const;
    wxString Locale() const;
};

// Free function — pure: same inputs always produce same outputs.
// Lives in helpResolver.h, NOT in the corpus class — keeps the
// corpus storage-only and the resolver swappable.
BACKEND_API std::vector<const ibHelpEntry*>
ResolveByName(const ibHelpCorpus& corpus,
              const wxString& identifier,
              const ibHelpResolveHint& hint = {});
```

`ibHelpResolveHint` is pure data — strings + enums, NO live
`ibValue` / `ibPrecompileContext*`. The desktop editor builds it
from its precompile context (§5); the web client builds the same
struct from URL query params (`?parent=Documents.Invoice&role=member_access`).
Same struct, two producers.

```cpp
struct ibHelpResolveHint {
    enum class Role { kUnknown, kTypeName, kCallExpression,
                      kMemberAccess, kAssignmentTarget };

    wxString parentIdentifier;       // left of '.', empty if none
    wxString containingMetaObjectId; // "Catalog.Invoice" or empty
    Role     expectedRole = Role::kUnknown;
    bool     preferLocalName = true; // match name_local before name_en
};
```

### 3.3. Loader — non-throwing, partial-load semantics

```cpp
struct ibHelpLoadError {
    wxString bucketPath;
    int      line = 0;
    enum class Severity { kFatal, kEntrySkipped, kWarning };
    Severity severity;
    wxString message;
};

struct ibHelpLoadResult {
    std::shared_ptr<const ibHelpCorpus> corpus;  // never nullptr —
                                                 // empty corpus on
                                                 // total failure
    std::vector<ibHelpLoadError> errors;
    bool ok() const { /* no kFatal errors */ }
};

// Public — backend factory; called by ibApplicationData at startup
// and by ReloadHelpCorpus(). Never throws ibBackendException; bad
// JSON files surface as ibHelpLoadError entries. Caller (Designer
// or wes) decides whether to show a non-blocking notification or
// abort startup based on `result.ok()`.
BACKEND_API ibHelpLoadResult
LoadHelpCorpus(const wxString& localeCode,
               const wxString& platformDir,
               const wxString& configDir = wxEmptyString);
```

Throwing was rejected because:
- A corrupt JSON in production must never crash Designer startup.
- Web server context wants per-request graceful degradation, not
  exception unwinding across cpp-httplib handlers.
- Validation errors are intrinsically a collection (one bucket can
  fail while another loads); a single thrown exception loses that.

### 3.4. Thread-safety + reload

Corpus is **frozen after construction**. All read paths
(`FindById`, `ResolveByName`, `SearchPrefix`, `SearchText`,
`GetRoot`) are concurrent-safe by virtue of immutability — no locks
in the hot path. The **handle** to the current snapshot, however, is
a mutable `shared_ptr` member on `appData` and must be published /
read with explicit atomic ops.

Storage on `appData`:

```cpp
// C++17 path — explicit atomic_load / atomic_store on every read+write
// site. The non-atomic shared_ptr free-function overloads are deprecated
// in C++20 but still mandatory while the project remains C++17
// (CLAUDE.md: "Language C++17"). When the project moves to C++20,
// migrate this member to std::atomic<std::shared_ptr<const ibHelpCorpus>>
// and the explicit atomic_load / atomic_store calls collapse to
// member-function load() / store(); ABI of GetHelpCorpus() does not change.
std::shared_ptr<const ibHelpCorpus> m_helpCorpus;  // member on ibApplicationData

std::shared_ptr<const ibHelpCorpus>
ibApplicationData::GetHelpCorpus() const {
    return std::atomic_load_explicit(&m_helpCorpus, std::memory_order_acquire);
}

void ibApplicationData::SetHelpCorpus(
    std::shared_ptr<const ibHelpCorpus> next) {
    std::atomic_store_explicit(&m_helpCorpus, std::move(next),
                                std::memory_order_release);
}
```

`GetHelpCorpus()` on every read site, not "raw member access". The
returned snapshot is the caller's responsibility to keep through any
operation that uses pointers into the corpus (§3.5 lifetime contract).

Reload (Phase 5 — metadata-driven entries change when the
configuration is saved):

1. `ReloadHelpCorpus()` runs on a worker thread, builds a new
   `ibHelpCorpus`. **Serialization:** a single
   `std::mutex m_helpReloadMutex` on `appData` is acquired by the
   reload worker for the entire build phase. Concurrent reload
   requests (Phase 5 auto-reload on config save + a manual reload
   from a hot-key) serialize on this mutex — latest waiter wins;
   intermediate states are not published.
2. Worker publishes via `SetHelpCorpus(newCorpus)` — single atomic
   store, no torn handle.
3. Existing readers keep their snapshot through the rest of their
   operation — no UAF risk. Snapshot drops when the last reader
   releases.

The `BACKEND_API` `ibHelpCorpus` type is therefore `final` (no
subclassing — invariants survive vtable changes) and its
`shared_ptr<const ibHelpCorpus>` accessor returns by value.

### 3.5. Indexes

Built once in the constructor; each `O(N)` where N ≤ ~2000 today:

- `std::unordered_map<wxString, EntryIndex> m_byId` — id → slot
- `std::unordered_map<wxString, std::vector<EntryIndex>> m_byNameEn`
  (case-insensitive normalized) — used by resolver for ambiguous-name
  matches
- `std::unordered_map<wxString, std::vector<EntryIndex>> m_byNameLocal`
  — same for the locale alias
- `std::map<wxString, std::vector<EntryIndex>> m_prefixIndex` —
  `lower_bound` walk for prefix search. Value is a vector, not a
  single index, because multiple entries can share the same
  normalized prefix key (e.g. four `Date` entries: type / function /
  Document attribute / MomentTime attribute). Ordered std::map is
  O(log N + k) and zero extra code vs a trie. Phase 1 ships this;
  trie deferred unless profiling shows it.
- `std::unordered_map<wxString, std::vector<EntryIndex>> m_tokenIndex`
  — inverted index built from name + signature tokens only
  (NOT description text in Phase 1; description tokens land behind a
  feature flag in Phase 6 when HTTP search hits real load).

Search ranking: name-exact = 0, name-prefix = 1, signature-token = 2,
description-token = 3. BM25 only if Phase 6 telemetry shows it's
worth the complexity.

### 3.6. Platform / per-config overlay rule

Two corpora are loaded into the same snapshot — `ibHelpCorpus` is
itself the merger, not a coordinator over two corpus instances. The
loader runs `LoadHelpCorpus` once per source:

```cpp
ibHelpLoadResult platform = LoadHelpCorpus(locale, platformDir);
ibHelpLoadResult perConfig = LoadHelpCorpus(locale, configCacheDir);
// Each call produces an independent corpus instance; the corpus
// constructor (below) merges them into the final immutable snapshot.
auto merged = std::make_shared<const ibHelpCorpus>(
    std::move(platform.corpus), std::move(perConfig.corpus), locale);
```

Merge invariants (enforced in the merging constructor at LoadAll
time, not at every read):

1. Within each source corpus, duplicate ids are a fatal load error
   (`kFatal` in that source's `LoadErrors()`).
2. Across sources, a per-config id that **collides** with a platform
   id is an **overlay**, not a duplicate — the per-config entry
   replaces the platform entry in the merged snapshot. Overlay is
   recorded as `kWarning` in `LoadErrors()` so that operations have
   visibility into shadowing.
3. Per-config corpora may only emit ids whose `kind ∈ {attr, meth,
   mo, ev}` AND whose parent segment in the dotted id matches a
   user-defined metaobject. The validation gate (§4 step 3) rejects
   per-config buckets that try to define `fn.*` / `kw.*` /
   `type.*` / `const.*` ids — those namespaces are platform-only.
   This keeps the disjoint-id-space contract enforceable rather
   than aspirational.
4. `see_also` cross-references resolve against the merged snapshot.
   Dangling ids after merge demote to `kWarning` and the entry
   still loads.

`ibHelpEntry::fromConfiguration` is set on entries originating from
the per-config corpus so the UI can tag them visually.

## 4. Help corpus generation pipeline

Hand-writing ~85 functions + ~200 metadata properties + 58 keywords
+ enum/constant entries × three locales is the long pole. Four-stage
pipeline:

1. **Skeleton generator** — Python `tools/help-skeleton.py` consumes
   a JSON dump exported by a new in-binary `classChecker --dump-help`
   subcommand. The dump tool walks `s_listKeyWord`,
   `ibValueSystemFunction::PrepareNames()`, and each
   `ibValueMetaObject*` subtype's `PrepareNames()`. JSON-via-CLI keeps
   the script decoupled from C++ ABI churn — no direct source-parsing.
   Output: one bucket file skeleton per top-level category, with
   `id`, `name_en`, `name_local`, `signature`, `category_keys`,
   empty body fields, `reviewed: false`.

2. **LLM filler** — `tools/help-fill.py` feeds each skeleton entry +
   surrounding metadata context (for a Document attribute: the
   Document type, the attribute's type/qualifiers, plus precedent
   from the platform's own documentation conventions) to an LLM with
   a strict template prompt. Output goes back to the JSON file's
   `description` / `example` / `availability` / `parameters` fields.
   `reviewed: false` stays — the filler never marks reviewed.

3. **Validation gate** — `tools/help-validate.py` checks every
   filled entry: required fields non-empty, `see_also` ids resolve,
   `category_keys` exist in `_categories.json`, `signature` parses,
   `example` compiles via a headless `codeRunner` pass. Runs in CI on
   the corpus directory; fail-the-build on a regression.

4. **Human review (Phase 7)** — Designer-side "Help editor" panel
   for inspecting LLM-filled drafts, editing prose, flipping
   `reviewed: true`. CI then has a flag — once the platform corpus
   is past the cutoff PR, any `reviewed: false` entry blocks ship.

### 4.1. Localization — three first-class locales

The platform itself ships in ru-RU, uk-UA, and en-US (see
`backend.conf` Locale field, `i18n` POT/PO files). The syntax
helper's active locale **follows the platform locale** read at
session startup from `appData->GetLocale()`. No separate helper-only
toggle — switching the platform locale switches the helper. A
per-session override is documented in §5 but defaults to "inherit".

All three locales are **first-class** with full coverage parity:
- `data/help/ru-RU/*.json`
- `data/help/uk-UA/*.json`
- `data/help/en-US/*.json`
- one `_categories.json` per locale

The corpus must not ship with one locale ahead of the others past
the cutoff PR — CI checks that every entry id exists in all three
locales' buckets, and that every `reviewed: true` entry has prose
content (not just translated names) in all three.

### 4.2. Keyword name localization

OES supports English keywords (`Procedure / EndProcedure / If / Then`)
plus planned per-locale aliases via the translate layer. Each keyword
`ibHelpEntry` therefore carries `name_local` matching the locale alias
and `name_en` as the stable cross-locale join key.
`s_listKeyWord` ordering is load-bearing — `KEY_FROM..KEY_INTO`
block must not be reshuffled by anything in this pipeline (read-only
walk over the existing array, no append-in-middle).

## 5. Frontend — Desktop sidebar pane

New module: `src/engine/frontend/syntaxHelper/`.

```
syntaxHelper/
├── helpPaneView.h/.cpp     // wxAui pane container, splitter, 3 tabs
├── helpTreeView.cpp        // Tree tab — wxTreeCtrl, hierarchical categories
├── helpIndexView.cpp       // Index tab — filter textbox + filtered wxListBox
├── helpSearchView.cpp      // Search tab — full-text wxSearchCtrl + result list
├── helpDetailView.cpp      // bottom — wxHtmlWindow + toolbar
└── helpChooserDialog.cpp   // modal "Choose section" dialog (§5.3)
```

### 5.0. Top-level category layout

The tree's top-level nodes follow a conventional low-code-platform
structure (Language overview / Global context / UI / Common
objects / Applied objects / System enums / Queries) so users
moving from similar tools recognise the layout. Stored as stable
English `category_keys` (§2.2 / §2.4) with per-locale display
names resolved via `_categories.json` (each locale dictionary
maps the English key to its localised label — see the per-locale
JSON files under `syntaxHelper/`):

| `category_key`         | English display              |
|------------------------|------------------------------|
| `common_lang`          | Language overview            |
| `global_context`       | Global context               |
| `ui_regular`           | Classic UI                   |
| `ui_managed`           | Managed UI                   |
| `common_objects`       | Common objects               |
| `applied_objects`      | Applied objects              |
| `system_value_sets`    | System value sets            |
| `system_enums`         | System enums                 |
| `queries`              | Queries                      |

Sub-trees branch from these — e.g. `applied_objects/documents/<DocName>/properties/<PropName>`.

### Designer integration

`ibDesignerMainFrame` already owns the AUI manager that hosts the
metaTree on the left and the docView/editor on the centre. Add a
third docked pane on the right with the user-visible caption
"Syntax helper" (localised per the active locale):

```cpp
m_auiManager.AddPane(
    new ibHelpPaneView(this),
    wxAuiPaneInfo()
        .Name("syntaxHelper")
        .Caption(_("Syntax helper"))
        .Right()
        .Layer(1)
        .MinSize(300, 600)
        .CloseButton(true)
        .Show(false)             // hidden by default until first opened
);
```

Toggle in the View menu + toolbar button. Layout persists in the
existing AUI perspective string saved to the user-config file.

### Detail rendering

`wxHtmlWindow` accepts a subset of HTML. The corpus renderer transforms
each `ibHelpEntry` into a small HTML doc with:

* `<h2>` name (local + English in parentheses) for cross-locale clarity
* `<b>` section labels — Description / Syntax / Parameters / Example /
  Availability (localised at render time)
* `<code>` blocks for signatures and examples
* Links to `see_also` ids — clicking re-targets the pane to that entry

### Editor — gutter `...` button

Scintilla margin marker. When the cursor is inside a procedure /
function call expression the precompile context already knows the
target's id (or candidate set). The editor sets a margin marker on
that line; the marker click handler:

1. `ibHelpCorpus::ResolveByName(name, hint)` →
2. 1 result → activate help pane on that entry.
3. >1 results → open `ibHelpChooserDialog` (§5.3).
4. 0 results → margin marker is not drawn at all.

Implementation lives in `codeEditor.cpp` near `ShowMethods()` / the
existing autocomplete window — both already integrate with
`m_rootContext`.

### Ambiguous-name chooser dialog

`ibHelpChooserDialog` — single modal `wxDialog` used for ALL chooser
triggers (editor, tree, web round-trip). Standard "Choose section"
flow so users moving from other low-code platforms see a familiar
interaction.

Layout (labels shown here in English; UI strings are localised
via `_()` so each locale gets its own translation):

```
+-----------------------------------------------+
| Choose section                           [×]  |
|-----------------------------------------------|
| Select a section from the list:               |
| ┌───────────────────────────────────────────┐ |
| │ Language overview / Primitive types / Date │|
| │ Applied objects / Documents / ... / Date  │ |
| │ Applied objects / Journals / ...          │ |
| │ Universal objects / MomentTime / Date     │ |
| │ ...                                       │ |
| └───────────────────────────────────────────┘ |
|                       [Show] [Cancel] [Help]  |
+-----------------------------------------------+
```

Buttons (UI strings localised, English shown as default):
- **Show** — opens the syntax-helper pane on the selected entry.
  Default action (Enter).
- **Cancel** — closes without touching the pane state. Esc.
- **Help** — opens the helper pane on the "About the syntax helper"
  guide entry, not on the selected list item.

The dialog is **always modal** — no popup-at-caret variant. The
editor temporarily loses focus to the dialog; the dialog returns
focus to the same editor position on dismiss (Cancel / Show /
Esc). This avoids the focus-juggling complexity of a non-modal
popup over Scintilla.

Trigger paths converge on the same dialog:
- Ctrl+F1 in editor → resolver → 1 match opens pane; >1 matches
  show dialog
- Right-click → "Look up in Syntax Helper" → same resolver path
- Ctrl+F1 in metadata tree → resolver on node name → same dialog
- Web client: resolve endpoint returns N matches; React shell
  renders an equivalent overlay (same JSON, web-native chrome —
  not literally a wxDialog)

## 6. Frontend — Web (wfrontend.dll)

Web mode reuses `appData->GetHelpCorpus()` headless. Same locale
inheritance as desktop — the corpus reflects the platform locale of
the active web session, not a client-side preference. The web client
renders the help panel as an HTML sidebar (same JSON, different
chrome). Routes live in `wenterprise-server/main.cpp` as inline
`svr.Get` lambdas alongside the existing handlers:

```
GET /api/help/tree
GET /api/help/entry/<id>
GET /api/help/resolve?name=<X>[&parent=<Y>&role=<R>]
GET /api/help/search?q=<X>[&limit=N&offset=M]
```

Hint via query string mirrors `ibHelpResolveHint` (§3.2) — same
struct, two producers.

### 6.1. HTTP hygiene

* **Auth** — every `/api/help/*` route requires the same session
  cookie the rest of the web API expects. Unauthenticated requests
  get `401`. No anonymous corpus access — the platform's metadata
  walk is configuration-private.
* **Caching** — entry / tree / resolve responses carry
  `ETag: "<corpus.Fingerprint()>"` and
  `Cache-Control: private, max-age=3600, must-revalidate` plus
  `Vary: Cookie`. **`private`, not `public`** — once Phase 5 lands
  per-config entries, the corpus is configuration-scoped and a
  shared proxy must NOT serve one tenant's response to another.
  `Vary: Cookie` guards browser caches against session swap on the
  same machine. Honour `If-None-Match` → `304`. Reload (§3.4)
  changes the fingerprint, invalidating browser caches.
* **Fingerprint stability** — `corpus.Fingerprint()` is computed
  over **file content hashes** (SHA-256 of each bucket file body
  PLUS each `_categories.json` body PLUS any other loaded JSON file
  in the corpus directory), not mtime+size. Mtime is not stable
  across CI restores, container image rebuilds, or rsync deploys;
  same content can ship with different mtimes. Fingerprint shape:
  `SHA-256(locale | schema_version | sorted_all_corpus_file_hashes)`
  truncated to 16 hex chars for ETag readability. The "all corpus
  files" rule means changing only `_categories.json` (e.g. an i18n
  fix for a category label) busts ETags correctly; bucket-only
  hashing would 304 stale tree/detail labels indefinitely.
* **Search** — `Cache-Control: no-store`. `limit` defaults to 50,
  hard cap 200. Response shape: `{ "total": N, "items": [...] }`.
* **Tenancy / per-config** — corpus is **single per process**, owned
  by `appData` (§3.1). There is NO per-session corpus accessor: a
  single OES installation runs ONE configuration at a time (the one
  the server was started with), so the platform+per-config merged
  snapshot (§3.6) is correct for every session of that process.
  Multiple isolated configurations = multiple `wenterprise-server`
  processes, each with its own `appData->GetHelpCorpus()`. Session
  identity flows into the HTTP layer for auth + cache-key purposes
  only (see Caching below), not into corpus ownership. If a future
  multi-config-in-one-process design lands, it adds a per-config
  registry keyed by configuration id — but that re-architects more
  than the help corpus and is out of scope here.
* **Rate limit** — share the global request-limit middleware
  wenterprise-server already applies to its other JSON routes; no
  per-endpoint quota in Phase 6.

### 6.2. Web UI direction

The web shell's syntax helper is **NOT a 1:1 port** of the desktop
tabs. The web pattern is a command-palette-style search overlay
(Ctrl/Cmd-K) leading with full-text + recent + categories;
hierarchical tree is the secondary "Browse" view. Same JSON, web-native
chrome. Rationale: browser users expect palette search; nested
desktop tabs feel foreign in the same DOM as the rest of the React
shell upstream commit `9464237d` is building.

## 7. Phasing

Ship in independently-mergeable slices. Dependency chain explicit:

1. **Phase 1 — Data layer + loader.** `ibHelpEntry`, `ibHelpCorpus`
   (immutable + shared_ptr swap), `ibHelpResolver`, JSON loader with
   `ibHelpLoadError` collection. `appData->GetHelpCorpus()` wired.
   No UI. Resolver signature finalized with `ibHelpResolveHint`
   (the editor-context fields locked in now even though only unit
   tests consume them — Phase 4 must not force a redesign). Unit
   tests over a tiny hand-written corpus subset (Date, Message, If,
   Procedure, three keywords). **Precondition for all other phases.**

2. **Phase 2 — Skeleton generator + validation gate.** Python
   tooling. `classChecker --dump-help` subcommand emits the registry
   JSON. `tools/help-skeleton.py` builds bucket skeletons.
   `tools/help-validate.py` runs in CI on every PR touching `data/help/`.
   Validation gate ships BEFORE LLM-filled content so no
   un-validated prose can reach `data/help/` accidentally.
   **Depends on Phase 1** (id grammar, JSON schema).

3. **Phase 3 — Desktop sidebar + LLM-filled corpus seed.** Tree +
   Index + Search + detail panes, docked into `ibFrameManager`
   right side; View-menu toggle; persisted in AUI perspective. Empty
   state, load-failure state. `Ctrl+F1` (open syntax helper at the
   identifier under cursor) wired as a global accelerator in the
   designer mainframe; right-click → "Look up in Syntax Helper"
   context menu item (label localised per active locale); pane-open from
   View menu / toolbar. Detachable detail pane (`.Float()` allowed)
   for long examples. First LLM-filled corpus pass lands here
   covering all 85 system functions + 58 keywords + primitive types
   (`reviewed: false` on every entry — bar visually marks them
   "draft"). **Depends on Phase 1 + Phase 2.**

4. **Phase 4 — Editor integration.** Scintilla margin marker
   (`DEF_HELP_HINT_ID=3`) for `...` next to call expressions;
   click delegates to the same `Ctrl+F1` resolver path so all
   trigger surfaces converge on one code path. Ambiguous-name
   resolution opens `ibHelpChooserDialog` (modal, §5.3) —
   standard "Choose section" flow. Navigation history (← →) in
   the help pane via `Alt+←` / `Alt+→`. Tooltips swap from
   `s_listHelpDescription` short strings to corpus entries.
   **Depends on Phase 3.**

5. **Phase 5 — Per-configuration entries.** Help corpus extension
   for metadata-driven entries: every concrete Catalog / Document /
   Register attribute and method. Built by the corpus builder when
   the configuration is saved; cached in
   `appData->GetConfigCacheDir() / "help" / <locale>`. Reload swaps
   on configuration save via §3.4 shared_ptr swap.
   `ibHelpEntry` gains `bool fromConfiguration` flag for the UI to
   tag config-derived entries. **Depends on Phase 1 + Phase 3.**

6. **Phase 6 — Web endpoints + React panel.** HTTP routes on
   wenterprise-server (auth, ETag, pagination per §6.1). Web UI as
   command-palette overlay + secondary Browse view (§6.2). BM25
   ranking only if telemetry warrants. **Depends on Phase 5** (for
   per-tenant corpus correctness — without it web users see only
   platform entries).

7. **Phase 7 — Help editor.** Designer-side UI for inspecting
   `reviewed: false` drafts, editing prose in place, flipping
   `reviewed: true`. CI cutoff PR turns on the
   "no `reviewed: false` in shipped corpus" gate.
   **Depends on Phase 3.**

Each phase ships behind a dedicated PR. Phase 1 is the only
precondition for ALL others; Phases 2-7 each declare their explicit
prerequisites above.

## 8. Resolved decisions + remaining open questions

### Resolved (promoted out of "open" during the triple-review pass)

* **Markdown vs HTML in `description`.** Markdown source on disk
  (author-friendly). Loader renders to HTML in memory at LoadAll;
  no on-disk render cache — corpus is small enough that a re-render
  per startup is below the noise floor.

* **wxHtmlWindow render scope.** Supported Markdown subset:
  `# / ## / ###` headings, paragraphs, `**bold**`, `*italic*`,
  inline `` `code` ``, fenced ```` ``` ```` code blocks (mono font,
  no syntax highlighting in Phase 3 — defer to Phase 6 where the
  web shell can ship Prism). NO tables — `parameters` field renders
  as a `<dl>` (description list). NO inline HTML pass-through.

* **Per-configuration entries.** Two corpora are loaded
  independently — platform corpus from
  `appData->GetInstallDataDir()`, per-config corpus from
  `appData->GetConfigCacheDir()` — and **merged at construction
  time into a single immutable snapshot** by `ibHelpCorpus`'s
  merging constructor (§3.6). The merge replaces platform entries
  with their per-config namesakes (overlay); only ONE entry per id
  exists in the published snapshot. Entries from the per-config
  source carry `fromConfiguration: true`. Resolver lookups see a
  single merged corpus — there is no "resolver prefers" logic at
  query time. (Earlier draft used "merged at resolver lookup" /
  "resolver prefers" language; replaced because the two-model
  view was internally inconsistent — see §3.6 for the binding
  contract.)

* **Schema versioning.** `schema_version` is per-bucket top-level
  (§2.1). Loader migrates older versions in memory via an explicit
  chain; newer-than-known versions are rejected with a fatal load
  error. Migrations have a golden-fixture test each.

* **i18n of category path.** `category_keys` are locale-stable
  English ids; display strings come from `_categories.json` per
  locale (§2.4). Adding a locale = one dictionary + one buckets
  directory.

* **Editor integration with multi-line signatures.** The `...`
  button is cursor-aware, NOT line-aware. The editor's existing
  precompile-context expression tracker
  (`ibPrecompileContext::GetCurrentCallExpression()` — to be added
  in Phase 4 alongside the margin marker) walks the parenthesis
  stack regardless of newlines. Margin marker is drawn on the line
  containing the call's opening paren but the click target activates
  the help on the *call expression*, not the line.

* **Resolver lifetime.** `ResolveByName` returns
  `std::vector<const ibHelpEntry*>`. Pointers are valid for the
  lifetime of the `shared_ptr<const ibHelpCorpus>` snapshot the
  caller is holding (§3.4). Callers MUST keep their `shared_ptr`
  through any operation that uses the returned pointers — same
  contract as iterating a container while holding its handle.

### Remaining open

* **Help-editor (Phase 7) editing format.** Markdown-in-textarea
  vs a small WYSIWYG (the wxHtmlWindow renderer doesn't edit-in-
  place). Default: Markdown textarea + live preview in a split
  view. Revisit during Phase 7 design.

* **BM25 vs simple ranking on `/api/help/search`.** Phase 6 ships
  the simple ranker (§3.5). Telemetry from real usage decides
  whether BM25 is worth the index complexity.

* **Locale switching mid-session.** Today the corpus is loaded once
  for the platform locale at startup. Switching the platform locale
  in Designer requires a restart (other subsystems too). A "reload
  corpus on locale change" hook is feasible via §3.3 but not yet
  worth the work — locales rarely change mid-session.

## 9. Reusable open-source components

> **⚠ Legal-review TODO before Phase 2 starts pulling third-party
> content into `syntaxHelper/`:**
> 1. Confirm OES's own license variant — `COPYING` says
>    "LGPL 2.1 only" or "LGPL 2.1 or later"? This decides whether
>    LGPL-3 sources can be consumed (the latter allows forward
>    re-licensing).
> 2. MPL 2.0 derivative-work obligations on extracted comments:
>    confirm whether comment-extraction creates a covered work
>    (file-level copyleft applies) or a separate work (does not).
> 3. EU sui-generis database right caveat — facts / structure of
>    a database can be protected in EU jurisdictions even without
>    expression copyright.
>
> Phase 1 (data layer code) does NOT depend on §9 — it can ship in
> parallel with the legal audit.


The corpus content (~85 functions + 58 keywords + ~200 metadata
properties × 3 locales) is the biggest cost. Before writing
everything from scratch, audit existing OSS sources. OES is
**LGPL 2.1** — compatibility constraints below are mandatory, not
preferences.

### 9.1. License compatibility matrix

| License of source        | Can we link / bundle code? | Can we bundle text content? |
|--------------------------|----------------------------|------------------------------|
| MIT / BSD / Apache 2.0   | ✅ yes, with attribution   | ✅ yes                       |
| MPL 2.0 (file-level)     | ✅ yes if files unmodified | ✅ yes                       |
| LGPL 2.1                 | ✅ (same license)          | ✅                            |
| LGPL 3.0                 | ⚠️ version mismatch        | ⚠️ avoid                     |
| GPL 2 / GPL 3            | ❌ viral — would force OES → GPL | ❌ would force corpus → GPL |
| AGPL 3.0                 | ❌ same                    | ❌ same                      |
| CC-BY 4.0                | N/A (not for code)         | ✅ with attribution          |

For **text content** in the help corpus the key question is whether
the corpus JSON files are considered part of OES's redistributable
package. They are (`data/help/<locale>/*.json` ships with OES), so
any verbatim text we copy into them inherits the source's license.
Facts and structural decisions are not copyrightable; **expression**
(actual prose) is. Safe pattern: study OSS sources for structure +
write fresh prose.

### 9.2. Locale coverage map

| Locale  | Source strategy                                              |
|---------|--------------------------------------------------------------|
| en-US   | Primary authoring locale — native English prose              |
| ru-RU   | Manual translation + LLM-assisted from en-US; native review  |
| uk-UA   | Same as ru-RU; native review                                 |

Phase 7 review gate (§7) applies per locale — `reviewed: true`
lands only after a native speaker passes the entry.

## 10. Non-goals (for this iteration)

* Live editing of help inside Designer (Help editor is Phase 7, opt-in).
* Voice / chat-style help.
* "Smart" semantic search (vector embeddings). Plain prefix +
  substring + token-set search is enough for the first cut.
* Replacing the existing autocomplete dropdown — that stays. The
  syntax helper is parallel, not a replacement.
