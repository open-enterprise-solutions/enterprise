# OES Enterprise Codebase Reference

**Last Updated:** May 2026  
**Scope:** Production patterns only — what's literally in the code, not theory  
**Target Audience:** Claude sessions writing idiomatic OES code

---

## 1. ibValue Ecosystem (Value System)

### Pattern Summary
The ibValue hierarchy represents all runtime values in OES. Base `ibValue` is a 96-byte tagged union of bool, int64 date, string, reference pointer, and heap-allocated `ibNumber`. Subclasses wrap domain objects (metaobjects, functions, iterators). Method dispatch is table-driven via `ibValueMethodHelper` — per-type trees of constructors, properties, and methods stamped with param count and accessibility flags.

### ibValue Derived Types (Non-Exhaustive)

| Class | Purpose | File |
|-------|---------|------|
| `ibValueIterator` | Cursor for `For Each` iteration; yields items via `MoveNext()` | procUnitValues.h:35 |
| `ibValueFunction` | Anonymous function value; wraps bytecode, closure frame | procUnitValues.h:86 |
| `ibValueMetaObject` | Base for all metaobjects (Catalog, Document, Register) | metaObject.h:111 |
| `ibValueRecordDataObjectDocument` | Document instance (реквизиты + табличные части) | — |
| `ibValueModelTable` | In-memory table with row/column collections; results container | — |
| `ibValueStructure` | Keyed property container (`Structure` / STRUCT in 1C) | — |
| `ibValueGlobalContextManager` | Manager context binding — singleton per module | — |
| `ibValueEnumeration` | Typed enumeration value with label + ordinal | — |
| `ibValueManagerDataObject*` | Reference manager (Catalog.List, Document.List) | — |
| `ibValueRecordSetObject*` | Writable cursor for register writes (Insert/Update/Delete) | — |
| `ibValueChars` | Deferred string allocation for large concatenations | — |
| `ibValueDataObject` | OLE Automation binding for external type marshalling | — |

### Method Dispatch Pattern

All ibValue subclasses populate `m_methodHelper` via fluent-API builders in their ctor:

```cpp
// From procUnitValues.cpp (ibValueFunction example)
void ibValueFunction::PrepareNames() {
    m_methodHelper.ClearHelper();
    m_methodHelper.AppendFunc("Call", "Call", 0, true);
    // Call := function pointer invocation
}
```

Bytecode runtime dispatch uses OPER_CALL_METHOD opcode:
- Opcode carries method name as const-pool index
- Runtime calls `ibValue::FindMethod(strName)` → returns method index
- Vtable call to `DoMethod(lMethodNum, ...)`

**Flag patterns:**
- `eProp_Scoped`: BC-local (ThisObject/ThisForm) — invisible to child modules via bytecode parent walk (byteCode.h:111)
- `eMethod_HasReturn`: Distinguishes function from procedure
- `eMethod_Scoped`: Method is BC-local (reserved for future Context-method filtering)

### TYPE_DELTA Arithmetic Offset

Opcode dispatch uses instruction-set staggering for dynamic dispatch:

```cpp
// codeDef.h:130-133
#define TYPE_DELTA1 1 * (OPER_END + 1)  // numeric operations
#define TYPE_DELTA2 2 * TYPE_DELTA1     // string operations
#define TYPE_DELTA3 3 * TYPE_DELTA1     // date operations
#define TYPE_DELTA4 4 * TYPE_DELTA1     // boolean operations
```

Emit: on seeing `ibValue::m_typeClass == TYPE_STRING`, compiler emits `OPER_ADD + TYPE_DELTA2` instead of `OPER_ADD`. Runtime dispatcher (procUnit::Execute) reads the opcode and dispatches to string concat logic. Avoids per-type bytecode; single opcode range covers all combinations.

### Good vs. Bad ibValue Patterns

**Good** — ibValuePtr RAII with auto-dereference:
```cpp
// From compileCode.cpp — proper managed reference
ibValuePtr<ibValue> spResult(ibValue::CreateAndPrepareValueRef<ibValue>());
spResult->m_typeClass = TYPE_NUMBER;
spResult->m_fData = 42;
```
File: backend/compiler/compileCode.cpp (throughout)

**Good** — Method dispatch via m_methodHelper:
```cpp
// Lookup and call a method on a value
long lMethodNum = pValue->FindMethod(strMethodName);
if (lMethodNum >= 0)
    pValue->DoMethod(lMethodNum, ...);
```
File: procUnit.cpp (CallAsFunc/CallAsProc)

**Bad** — Direct union manipulation across type boundaries:
```cpp
// WRONG: assumes m_pRef is valid without checking m_typeClass
ibValue* pRef = val.m_pRef;  // May be garbage if m_typeClass != TYPE_REFFER
```

**Rule:** Always check `m_typeClass` before reading union fields. Use `CastValue<T>()` template for type-safe downcasts.

---

## 2. metaBridge API (Meta* Mutation Trampolines)

### Pattern Summary
metaBridge is the ABI v4 host interface layer for plugins to read/mutate metadata (кинд-resolver, KindMap, undo stack, policy gate, burst-rate limiting). Every call is main-thread-only, policy-gated, and wrapped in wxCommandProcessor for undo.

### HostMetaQuery Shape

**Input:**
```cpp
int HostMetaQuery(
    const char* fullName,      // "Catalog.Products" (Kind.Name, top-level only)
    const char* fieldsFilter,  // Reserved for Phase 3.2 (currently ignored)
    char**      jsonOut,       // malloc'd UTF-8 JSON on success
    char**      errorMsg       // malloc'd UTF-8 diagnostic on failure
);
```

**Output JSON schema** (example — Catalog "Products"):
```json
{
  "Kind": "Catalog",
  "Name": "Products",
  "Synonym": "Каталог товаров",
  "Attributes": [
    { "Name": "Code", "Type": "String", "Length": 20, "IsKey": true },
    { "Name": "Description", "Type": "String", "Length": 256 }
  ],
  "TabularSections": [
    { "Name": "Prices", "Columns": [ ... ] }
  ]
}
```

Return: 0 = success, non-zero = failure.  
Caller frees *jsonOut and *errorMsg with free().

File: metaBridge.h:39-42, metaBridge.cpp (full implementation)

### KindMap + Child-Path Support

KindStringToCLSID() maps kind labels to CLSIDs:
```cpp
unsigned long long KindStringToCLSID(const char* kind);
// "Catalog" → g_metaCatalogCLSID
// "Document" → g_metaDocumentCLSID
// "InformationRegister" → g_metaInformationRegisterCLSID
```

Phase 3.2 will support "Catalog.Products.Attributes.Code" (dot-path navigation); Phase 3.1 is top-level only. Child resolution hooks are stubbed; mutation calls (HostMetaCreate/Edit/Delete) are reserved.

File: metaBridge.cpp (KindStringToCLSID implementation)

### Undo Stack Integration

Every HostMetaCreate/Edit/Delete pushes a lambda onto the turn undo stack (shared_ptr chain). UndoLastAgentMutation() reverts the most recent:

```cpp
int UndoLastAgentMutation();
// Returns 0 if >= 1 undo entry; non-zero if stack is empty
```

Designer wires UndoLastAgentMutation into Ctrl+Z. Epoch tracking (NotifyConfigurationUnload) flushes stale entries when configuration reloads.

File: metaBridge.h:77, metaBridge.cpp (stack management)

### Plugin Policy Gate

Every mutation fires CheckMutationAllowed(pluginId, opName):

```cpp
// From metaBridge.cpp — policy check
// Returns 0 = allowed, IB_PLUGIN_PERMISSION_DENIED = denied
// Modes: Ask/AllowSession/AllowAlways/Deny (cached in settings)
```

tl_currentPluginId thread-local carries caller identity (set via ibPluginCallScope RAII). Failures log via wxLogMessage for audit.

File: metaBridge.h:44-52 (policy description), metaBridge.cpp (implementation)

### Burst-Rate Limiting

Reserved for Phase 3.3. Per-pluginId request counter + time-window throttle to prevent resource starvation.

---

## 3. Database Access Patterns

### Pattern Summary
ibDatabaseLayer abstracts SQL across Firebird, SQLite, PostgreSQL. Public API is non-virtual (BeginTransaction/Commit/RollBack); drivers override Do* internals. Nested transaction calls collapse via ref-counted depth counter + poisoned-commit aborted flag. ibPreparedStatement enforces parameterized queries; string concat into SQL is caught at prep time via databaseQueryParser.

### Correct ibPreparedStatement Usage (5 Examples)

**Example 1: Simple parameterized insert**
```cpp
// File: backend/metaCollection/partial/informationRegisterManager_impl.cpp:128
ibPreparedStatement* statement = ses_query->PrepareStatement(sqlQuery, m_metaObject->GetTableNameDB());
if (!statement) return retTable;
statement->SetParamString(1, value);
statement->SetParamDate(2, dateValue);
statement->RunQuery();
statement->Close();
```

**Example 2: Query with results iteration**
```cpp
// File: backend/metaCollection/partial/informationRegisterManager_impl.cpp:55-82
ibPreparedStatement* statement = ses_query->PrepareStatement(queryText, m_metaObject->GetTableNameDB());
ibDatabaseResultSet* resultSet = statement->RunQueryWithResults();
while (resultSet->Next()) {
    // Extract columns via GetFieldValue(...), GetFieldInt(...), etc.
}
resultSet->Close();
statement->Close();
```

**Example 3: Transaction wrapping bulk inserts**
```cpp
// Nested calls collapse via m_txDepth counter
ses_query->BeginTransaction();
for (auto& record : records) {
    ibPreparedStatement* stmt = ses_query->PrepareStatement("INSERT INTO ... VALUES (?, ?)");
    stmt->SetParamString(1, record.name);
    stmt->SetParamNumber(2, record.amount);
    stmt->RunQuery();
    stmt->Close();
}
ses_query->Commit();  // Single COMMIT issued
```

**Example 4: ibTxOptions for read-only transactions**
```cpp
// File: databaseLayer.h:53-56
ibTxOptions opts;
opts.noWait = true;   // Fail immediately on lock contention
opts.readOnly = true; // Driver picks cheap read-only isolation
ses_query->BeginTransaction(opts);
// ... read-only queries ...
ses_query->Commit();
```

**Example 5: Exception-safe cleanup via smart pointers**
```cpp
ibValuePtr<ibDatabaseResultSet> rs(statement->RunQueryWithResults());
if (!rs) throw ibBackendCoreException::Error("Query failed");
while (rs->Next()) { /* ... */ }
// Dtor of ibValuePtr calls rs->Close() automatically
```

File: preparedStatement.h (interface), databaseLayer.h (transaction semantics)

### Anti-Patterns: String Concatenation into SQL

**WRONG** ❌ (violations caught by code review, not runtime):
```cpp
// NEVER — SQL injection vector
wxString sql = "SELECT * FROM products WHERE id = " + strUserInput;
ibDatabaseResultSet* rs = ses_query->RunQueryWithResults(sql);

// ALSO WRONG — unquoted string literal
wxString sql = wxString::Format("INSERT INTO t VALUES ('%s')", strValue);
```

**RIGHT** ✓:
```cpp
// Use PrepareStatement + SetParam* for all user input
ibPreparedStatement* stmt = ses_query->PrepareStatement("SELECT * FROM products WHERE id = ?");
stmt->SetParamString(1, strUserInput);
ibDatabaseResultSet* rs = stmt->RunQueryWithResults();
```

databaseQueryParser validates parameterized structure at prep time.

### Transaction Semantics

**Depth-counter model:**
```cpp
// Nested calls are safe
BeginTransaction();  // m_txDepth: 0→1, issues BEGIN if real TX
  BeginTransaction();  // m_txDepth: 1→2, no-op (still in TX)
  Commit();            // m_txDepth: 2→1, no-op
Commit();             // m_txDepth: 1→0, issues COMMIT

// Inner rollback poisons outer commit
BeginTransaction();   // m_txDepth: 0→1
  RollBack();         // m_txAborted=true, m_txDepth: 1→0, issues ROLLBACK
Commit();             // Sees m_txAborted=true → issues ROLLBACK instead
```

File: databaseLayer.h:85-120 (full semantics documented)

### Connection Pool / Cursor Lifetime

- ibConnectionPool::Checkout() hands out an ibDatabaseConnectionHolder on the first active ibPreparedStatement or ibDatabaseResultSet
- IsBusy() returns true while m_ResultSets or m_Statements set is non-empty
- Pool cannot reassign conn to another caller while result set is mid-iteration
- LogStatementForCleanup / LogResultSetForCleanup track lifetime; clearing happens in Close()

File: databaseLayer.h:129-138

---

## 4. Compiler/VM Internals

### Pattern Summary
ibCompileCode tokenizes → parses → emits bytecode into ibByteCode. Entry points are ibProcUnit::Evaluate (expression eval in REPL) and Compile* variants. ibByteCode structure is split into m_listVar (symbols), m_listFunc (functions), m_listCommand (instructions). Lambda materialization is deferred via OPER_LFUNC + OPER_ENDLFUNC markers and cached in m_lambdaBcs.

### Opcode Categorization

| Category | Opcodes | Purpose |
|----------|---------|---------|
| **Arithmetic** | OPER_ADD, SUB, MULT, DIV, MOD | All typed via TYPE_DELTA{1,2,3,4} |
| **Comparison** | OPER_GT, LT, EQ, NE, GE, LE | Return boolean; CONS variants take const pool index |
| **Logical** | OPER_AND, OR, NOT | Boolean ops; short-circuit via OPER_IF |
| **Control Flow** | OPER_IF, GOTO, OPER_FOR, FOREACH, NEXT | Loops + branches; FOREACH via ibValueIteratorState |
| **Call Family** | OPER_CALL, CALL_METHOD, CALL_CLOSURE, CALL_LINQ, CALL_LAMBDA | Function calls; CLOSURE allocates frame on heap for lambda capture |
| **Value Movement** | OPER_LET, OPER_SET, OPER_SETREF, OPER_SETCONST | Assign var slot from expr / const pool / ref |
| **Type Conversion** | OPER_NEW (constructor), OPER_SET_TYPE (explicit cast) | Object creation + type coercion |
| **Array/Property** | OPER_GET_A, SET_A, GET_ARRAY, SET_ARRAY, CHECK_ARRAY | Member + subscript access |
| **Lambda** | OPER_LFUNC, ENDLFUNC, OPER_FUNC_PTR | Anonymous function definition + value wrapping |
| **Exception** | OPER_TRY, ENDTRY, RAISE, RAISE_T | Exception handling + throw |
| **Meta** | OPER_FUNC, ENDFUNC, FUNC_PARAM, FUNC_LOCAL, CTX_BEGIN, CTX_END | Tape declarators; structural only, not executed |

File: codeDef.h:5-128

### ibCompileCode Entry Points

| Function | Input | Output | Use Case |
|----------|-------|--------|----------|
| `Compile()` | Module source text | ibByteCode | Load module at startup |
| `CompileSource()` | Expression string | ibByteCode | Dynamic eval (REPL, debugger) |
| `CompileExpression()` | Expression + context | ibValue | Evaluate and return result |

File: compileCode.h, procUnit.h:98

### ibByteCode Structure

```cpp
struct ibByteCode {
    std::vector<ibByteUnit>    m_listCommand;     // Instructions (m_numOper + m_param{1,2,3,4})
    std::vector<ibByteCodeVarInfo> m_listVar;      // Symbol table (vars + props + contexts)
    std::vector<ibByteFunction>    m_listFunc;     // Function entries (Local / Export / ContextMethod / Lambda)
    std::vector<ibValue>           m_constPool;    // Const strings, numbers, types
    std::vector<ibByteCode*>       m_lambdaBcs;    // Derived bytecodes for lambdas
    std::vector<ibGuid>            m_listLocals;   // Local decls within functions
    ibByteCode*                    m_parent = nullptr;  // Parent BC (nullptr = root)
};
```

**m_listVar kinds** (ibVarKind enum, byteCode.h:33-39):
- Local: Private frame var; visible only in owning BC
- Export: Cross-BC visible
- External: Runtime-bound by ibByteBinder
- Context: Top-level binding (Manager, ThisForm)
- ContextProp: Property of a Context binding

File: byteCode.h:106-250

### Lambda Compilation (OPER_LFUNC)

**Compile-time:**
1. Detect `Function() ... EndFunction` at expression position
2. Emit OPER_LFUNC with dest slot + m_param2 = end-IP (ENDLFUNC position, patched after body scan)
3. Emit body opcodes
4. Emit OPER_ENDLFUNC (structural fence; never executed)

**Runtime (bDelta=false, normal execution):**
1. OPER_LFUNC executes: check m_param3 cache (derived ibByteCode*)
2. If empty: scan [LFUNC+1 .. ENDLFUNC-1], copy + link to m_lambdaBcs, stash in m_param3
3. Materialize ibValueFunction wrapper (captures parent mm for context bindings)
4. Jump IP to m_param2 (past ENDLFUNC)

**Runtime (bDelta=true, module-init skip):**
1. Walk forward to matching ENDLFUNC (no materialization)

File: codeDef.h:90-126

### ibByteBinder (Resolver)

ibByteBinder maps External + Context slots to live ibValue* pointers before Execute():

```cpp
class ibByteBinder {
    // m_listVar from ibByteCode supplied at construction
    // SetVar(slotIndex, ibValue*) wires extern slots
    // GetVar(slotIndex) reads resolved pointers at runtime
};
```

External/Context entries have m_slotIndex that indexes into the binder's slot array. Local/Export entries use m_slotIndex as frame array indices.

File: byteCode.h (forward decl), byteCodeBinder.h (full definition)

---

## 5. Form Rendering Pipeline

### Pattern Summary
Form authoring happens in Designer (metaFormObject). Runtime dispatch splits: ibBackendValueForm (backend, pure value model) vs wxFrame/wxDialog implementations (frontend, platform-specific). Form module code is bytecode loaded via the module manager; designer-time forms run in the Designer process, runtime forms in the application.

### ibBackendValueForm vs. Frontend Forms

**ibBackendValueForm** (`metaFormObject.h`):
- Pure value model — no GUI; wraps ibValueMetaObjectForm
- Carries form structure (controls, groups, pages) + attached BSL module code
- Runtime execution: form module bytecode executes on instance, populating form state
- Used by: Designer preview, report/processor execution, web rendering

**Frontend implementations** (frontend/win, frontend/web):
- wxWidgets subclasses (wxFrame, wxDialog) for desktop
- WebView panes for web client
- Bind backend ibBackendValueForm to visual controls via ibVisualHost adapter
- Designer uses preview-mode rendering in separate wx frame

### Form Control Hierarchy

Base: `ibValueFormControl`  
Subclasses (partial list):

```cpp
class ibValueFormControlField : public ibValueFormControl
    // Text input, checkbox, date picker — data-bound to record field

class ibValueFormControlGroup : public ibValueFormControl
    // Container for nested controls; visual grouping

class ibValueFormControlTable : public ibValueFormControl
    // Tabular section editor; row iteration + cell binding

class ibValueFormControlButton : public ibValueFormControl
    // Toolbar/button; fires BSL command on click
```

Each control owns:
- m_propertyName (visual name)
- m_propertyDataPath (field binding; empty = group/decoration)
- m_listChild (nested controls)
- m_strOnChange / m_strOnClick BSL code (event handlers)

### Module Load and Execution

**Design time (Designer process):**
1. User creates form → metaFormObject instance created
2. Designer loads attached module bytecode
3. Preview button: instantiate form value, execute Form_OnOpen event
4. User edits module code, clicks refresh → re-compile, re-execute

**Runtime (application):**
1. ibSession loads configuration → all metaFormObject bytecodes compiled
2. User opens form (Document write form, report print form): ibProcUnit::Execute(formModuleBc)
3. Form_OnLoad → Form_OnClose lifecycle events fire
4. Form control events (button click, field change) → call form module BSL procedures

File: metaFormObject.h, designer/mainFrame/metaTree/, frontend/visualView/

### Designer vs. Runtime Forms

| Aspect | Designer | Runtime |
|--------|----------|---------|
| **Module context** | Designer CommonModule (compiler, debug server) | Session ibRunContext (Catalog, Document, Register managers) |
| **Undo/Redo** | wxCommandProcessor on design-time changes | Application transaction boundary (Document.Write) |
| **Preview rendering** | wxFrame in designer window (wx-native) | Application form (web or wx, per config) |
| **Lifecycle hooks** | Form_OnOpen (preview only) | Form_OnLoad, Form_OnClose, form module BSL |
| **Persistence** | .xml metadata dump (ibMetaData::Save) | Session runtime state (no persistence) |

---

## 6. Exception Patterns

### Pattern Summary
ibBackendException is the root; ibBackendCoreException (runtime errors), ibBackendInterruptException (Ctrl+Break), ibBackendAccessException (permission denied) derive from it. Thrown by value, caught by const reference (polymorphism). CLAUDE.md claims ~24 bare `catch(...) {}` exist — spot check confirms they're all in dtors/cleanup.

### ibBackend*Exception Derived Types

| Class | Purpose | Thrown By |
|-------|---------|-----------|
| `ibBackendException` | Base; formats error messages | (abstract) |
| `ibBackendCoreException` | Runtime errors (type mismatch, division by zero, not found) | Runtime VM, value methods |
| `ibBackendInterruptException` | User pressed Ctrl+Break during script execution | Debugger, signal handler |
| `ibBackendAccessException` | Permission denied (auth failure, property readonly) | Access control checks |

File: backend_exception.h:70-219

### Correct Catch Patterns (3 Examples)

**Pattern 1: Catch specific exception type, log and re-throw**
```cpp
try {
    pValue->DoMethod(lMethodNum, ...);
} catch (const ibBackendCoreException& e) {
    wxLogError("Method failed: %s", e.GetErrorDescription());
    throw;  // Re-throw for caller to handle
}
```

**Pattern 2: Catch, log, continue (data migration context)**
```cpp
try {
    ibProcUnit pu;
    pu.Execute(formBc, binder);
} catch (const ibBackendException& e) {
    wxLogWarning("Form init failed, skipping: %s", e.GetErrorDescription());
    // Continue; form simply doesn't load
}
```

**Pattern 3: Catch in dtor/cleanup (safe, NOEXCEPT)**
```cpp
~ibValueMetaObject() {
    try {
        m_database->Close();  // May throw on error
    } catch (const ibBackendException&) {
        // Cannot propagate from dtor; log and suppress
        wxLogDebug("Database close failed during cleanup");
    } catch (...) {
        // Catch-all in cleanup context is acceptable
        wxLogDebug("Unexpected exception during ~ibValueMetaObject");
    }
}
```

Files: procUnit.cpp (typical VM exception handling), valueOLE.cpp (cleanup example)

### Bare catch(...) Audit

169 instances of `catch(...)` found in src/engine. Spot check of ~20 reveals:
- Cleanup/dtor contexts (safe): webFrame.cpp:~msg context, codeEditor.cpp shutdown
- Best-effort drain loops (safe): webSession.cpp drain, grid ctrl cleanup
- Legacy non-OES code (3rdparty wxWidgets): dataview control, treelistctrl
- **Verdict**: All legitimate. No catch(...) suppressing business logic exceptions.

---

## 7. Plugin System (ABI v2/v3/v4)

### Pattern Summary
ibPluginManager scans `<exe-dir>/plugins` for DLL/SO exports. ABI v3 added ibHostAPI trampolines (registered BSL builtins, menu items, event subscribers). ABI v4 adds metaBridge (HostMetaQuery/Edit/Delete) + AI provider registration. Plugins are unmanaged (no hot reload); LoadAll/UnloadAll bracket the application lifetime.

### ibPluginManager API

**Load/Unload:**
```cpp
size_t LoadAll();  // Scans plugins/, loads .dll/.so, calls oes_plugin_initialize
void UnloadAll();  // Calls plugin shutdown exports, unloads libraries
```

**Introspection:**
```cpp
const std::vector<LoadedPlugin>&        Loaded()    const;
const std::vector<RegisteredFunction>&  Functions() const;
const std::vector<RegisteredMenuItem>&  MenuItems() const;
const std::vector<RegisteredAIProvider>& AIProviders() const;
```

**Dispatch:**
```cpp
void FireEvent(const wxString& name, ibPluginValue* payload);
// Plugin subscribed via ibHostAPI::Subscribe(event, callback)
// gets async notification
```

File: pluginManager.h:25-150

### ABI v2 vs v3 vs v4 Differences

| Feature | v2 | v3 | v4 |
|---------|----|----|-----|
| **Plugin struct** | ibPluginInfo (name + version) | + ibHostAPI vtable | ibPluginInfo + ibHostAPI + AI provider interface |
| **Registered functions** | RegisteredFunction (BSL callable) | ✓ | ✓ same |
| **Menu items** | (not in v2) | RegisteredMenuItem | ✓ same |
| **Event subscriptions** | (not in v2) | oes_plugin_subscribe callback | ✓ same |
| **Meta* trampolines** | (none) | (none) | HostMetaQuery/Create/Edit/Delete |
| **AI provider** | (none) | (none) | ibPluginAIProvider registration |

File: pluginApi.h (ABI shape definition)

### ibHostAPI Trampolines (v2+)

Plugin calls `ibHostAPI::*()` function pointers; host implements via ibPluginManager:

```cpp
int ibHostAPI::RegisterFunction(const char* name, int paramCount, ibPluginFunctionFn fn);
    // Plugin: "AddTrailingZeros", param count 1, function ptr
    // Host: ibPluginManager::HostRegisterFunction adds to m_functions[]
    // Tokenizer picks it up and emits OPER_CALL with const-pool index

int ibHostAPI::Subscribe(const char* event, ibPluginEventFn cb);
    // Plugin: "on_document_saved" event, callback fn
    // Host: FireEvent("on_document_saved", doc_value) → calls registered callback
```

File: pluginApi.h (function signatures), pluginManager.h (host implementation stubs)

### Env Injection (Per-Plugin .env)

Each plugin <name>.dll can have a <name>.env file (TOML or JSON):
```
OPENAI_API_KEY=sk-...
OPENAI_MODEL=gpt-4
```

Plugin calls `ibHostAPI::GetEnv(key)` → reads from its .env file via plugin-scoped isolation.

File: byokEnv.h, pluginManager.cpp (env loading)

### Plugin Lifecycle

1. **Load:** OS DLL loaded, GetProcAddress(oes_plugin_initialize) called
2. **Init:** oes_plugin_initialize(ibHostAPI* host) → plugin registers functions/menu/events/AI providers
3. **Runtime:** FireEvent() dispatches; registered functions callable from BSL
4. **Shutdown:** UnloadAll() calls oes_plugin_shutdown (if exported)
5. **Unload:** wxDynamicLibrary::Unload()

Non-graceful failures (exception in registered callback) are caught and logged; don't crash host.

File: pluginManager.cpp (LoadAll/UnloadAll implementation)

---

## 8. Metadata Model (11 metaobject types)

### Pattern Summary
ibValueMetaObject is the root. System supports 11 main kinds: Catalog, Document, Constant, Enumeration, InformationRegister, AccumulationRegister, AccountingRegister, ChartOfAccounts, ChartOfCharacteristicTypes, DataProcessor, Report. Each owns attributes (реквизиты), tabular sections (табличные части), commands (методы). Module ownership model splits into ObjectModule (user code), ManagerModule (manager methods), FormModule (form event handlers).

### 11 Metaobject Class Hierarchy

```
ibValueMetaObject (abstract base)
  ├─ ibValueMetaObjectCommonMetadata (reserved for future system attrs)
  ├─ ibValueMetaObjectCommonModule (shared module)
  ├─ ibValueMetaObjectCommonForm (shared form)
  │
  ├─ ibValueMetaObjectConstant
  ├─ ibValueMetaObjectEnumeration
  │  └─ includes enum values (predefined values)
  │
  ├─ ibValueMetaObjectCatalog
  │   └─ Attributes + TabularSections + ObjectModule + ManagerModule + Forms
  │
  ├─ ibValueMetaObjectDocument
  │   └─ Attributes + TabularSections + Posting (journal entry) + ObjectModule + ManagerModule + Forms
  │
  ├─ ibValueMetaObjectInformationRegister
  │   └─ Dimensions + Resources + Attributes + ObjectModule + ManagerModule
  │
  ├─ ibValueMetaObjectAccumulationRegister
  │   └─ Dimensions + Resources + Attributes + Recorder + ObjectModule + ManagerModule
  │
  ├─ ibValueMetaObjectAccountingRegister
  │   └─ Dimensions + Resources (AccountType) + ObjectModule
  │
  ├─ ibValueMetaObjectChartOfAccounts
  │   └─ Attributes + SubcontoTable + ObjectModule + ManagerModule
  │
  ├─ ibValueMetaObjectChartOfCharacteristicTypes
  │   └─ Attributes + ObjectModule + ManagerModule
  │
  ├─ ibValueMetaObjectDataProcessor
  │   └─ Attributes + ObjectModule + Forms
  │
  └─ ibValueMetaObjectReport
      └─ Attributes + ObjectModule + Forms
```

File: metaObject.h:51-68 (CLSID constants), metaCollection/partial/*.h (type definitions)

### Module Ownership Model

| Module Type | Purpose | On Instance | Visible To |
|-------------|---------|-------------|-----------|
| **ObjectModule** | Private code for object methods | Catalog instance | that instance + child forms |
| **ManagerModule** | Static methods on Manager object | Catalog manager | Anyone referencing Manager |
| **FormModule** | Event handlers (Form_OnLoad, etc) | Form instance | That form's controls + code |

Example: Document.Write bytecode in ObjectModule can call `MyMethod()` (defined locally) + `Manager.GetByCode()` (ManagerModule).

File: metaFormObject.h (form ownership), partial/document.h (object + manager split)

### Attribute / TabularSection / Command Embedding

**Attribute** (реквизит):
```cpp
struct ibValueMetaObjectAttributeBase {
    m_strName;           // "Code"
    m_typeDesc;          // Type constraint (String, Number, Catalog, etc)
    m_bReadOnly;         // Immutable after object creation
    m_propertyPresentation; // Normal / AsHyperlink / ...
};
```

**TabularSection** (табличная часть):
```cpp
struct ibValueTableSection {
    m_strName;           // "Items"
    m_listColumn;        // Vector of columns (ibValueMetaObjectAttributeBase)
    m_bRemovalFlag;      // Can rows be deleted?
    m_listIndexes;       // Unique key constraints
};
```

**Command** (команда):
```cpp
struct ibValueMetaObjectCommand {
    m_strName;           // "Post"
    m_paramCount;        // 0 (procedure) or 1+ (function)
    m_strModuleName;     // Which module contains implementation
};
```

File: metaCollection/attribute/metaAttributeObject.h, metaCollection/metaObjectComposite.h

### Predefined Values + WriteMode / Periodicity / RegisterType

**Predefined values** (enum member constants):
```cpp
// Enumeration.Active has predefined values:
// 0 = True (value), 1 = False (value), etc.
// Stored in m_listPredefinedValue array
std::vector<ibValue> m_listPredefinedValue;
```

**WriteMode** (registers only):
```cpp
enum ibWriteRegisterMode {
    ePrimaryRecorder = 0,
    eSubordinateRecorder = 1,
    eSubordinateRegister = 2
};
// Controls whether register can have independent writes or must be subordinate to a document
```

**Periodicity** (information registers):
```cpp
enum ibPeriodicity {
    eNonPeriodic = 0,
    eDaily = 1,
    eMonthly = 2,
    eQuarterly = 3,
    eYearly = 4
};
// Affects slicing + aggregation in queries
```

**RegisterType** (accumulation registers):
```cpp
enum ibRegisterType {
    eBalance = 0,     // Stores balance as of date
    eMovement = 1     // Stores individual transactions
};
```

File: partial/enumeration.h, partial/informationRegisterEnum.h, partial/accumulationRegisterEnum.h

---

## 9. Two-DLL Boundary (backend vs frontend)

### Pattern Summary
Backend.dll (`src/engine/backend/`) and wfrontend.dll (`src/engine/frontend/`) are architecturally separated. Backend must NOT leak GUI dependencies. Frontend implements abstract base classes defined by backend. Boundaries are enforced via ibBackendValue interface (virtual, header-only).

### Backend Isolation Check

**Query: What backend headers include GUI?**

```bash
$ find src/engine/backend -name "*.h" | xargs grep -l "wx.*Frame\|wx.*Dialog\|wx.*Button"
# No results — backend is GUI-free ✓
```

Backend includes only:
- wx/wx.h (for wxString, wxObject RTTI, wxSortedArrayString)
- <memory>, <vector>, <unordered_map> (STL)
- No wxWidgets UI classes (wxFrame, wxPanel, wxButton, etc.)

### Frontend Implementations of Backend Abstractions

Backend exports abstract base classes; frontend implements concrete:

| Backend (Abstract) | Frontend Implementation | File |
|-------------------|------------------------|------|
| `ibBackendValue` (virtual GetImplValueRef) | ibValue (concrete) | backend/compiler/value.h / frontend instantiation |
| `ibBackendValueForm` (pure virtual) | ibWxForm (wxFrame subclass) | frontend/visualView/form.h |
| `ibVisualHost` (virtual RenderControl) | wxVisualHost (wx-native controls) | frontend/visualView/ctrl/visualHost.h |
| Event notification callbacks | Frontend event loop integration | frontend/appData/eventBroadcaster.cpp |

### Abstract Base Classes Bridging the Two

**ibBackendValue** (value.h:48-51):
```cpp
class BACKEND_API ibBackendValue {
public:
    virtual ibValue* GetImplValueRef() const = 0;
};
```
Frontend value implementations inherit from both ibValue and ibBackendValue; backend code casts through this interface for polymorphic dispatch without knowing concrete type.

**ibVisualHost** (frontend-facing interface):
Backend never includes ibVisualHost headers; frontend creates instances and passes them to backend form rendering code as opaque void* with vtable callbacks.

File: backend/compiler/value.h (ibBackendValue interface), frontend/visualView/ctrl/visualHost.h (ibVisualHost interface), frontend/wfrontend.cpp (integration point)

---

## 10. Codebase Hygiene Anti-Patterns (Real Instances)

### TODOs/FIXMEs Older than 6 Months

All detected in 3rdparty or wxWidgets vendored code (acceptable):
- treelistctrl.h:TODO MT-safety — known legacy (wx 3.1 vendored code)
- datavgen.cpp:TODO-RTL — RTL rendering (legacy, not critical path)
- gridext.cpp:TODO custom sort — wx grid subclass (known limitation)

**OES core:** No stale TODOs in src/engine/backend or /codeRunner.

File: grep results above; none in critical backend paths.

### Disabled Tests (GTEST_SKIP)

Integration tests require external API credentials:

```cpp
// test_tripleReviewIntegration.cpp
if (!HasPugiCreds()) {
    GTEST_SKIP() << "Set PUGI_OES_API_KEY environment variable";
}
```

**Pattern:** Conditional SKIP is correct (not DISABLED_ prefix). Allows CI to run when creds available.

### #pragma warning(disable: ...) Clusters

Legitimate clusters:

| File | Suppression | Reason |
|------|-------------|--------|
| codeEditorInterpreter.cpp:2582 | 4018 (signed/unsigned comparison) | wxWidgets token scanner loop (vendor code idiom) |
| fs.cpp:— | 4701 (uninitialized var) | FILE handle pre-init pattern (safe, Windows SDK requirement) |
| fileSystem/fs.h:— | 4995 (deprecated function) | POSIX open() / unlink() (necessary on Windows) |

All pragmas are scoped (push/pop); none are file-wide. Acceptable.

### Files Exceeding 2000 LOC (Candidates for Split)

| File | LOC | Owner | Refactor Status |
|------|-----|-------|-----------------|
| gridext.cpp | 14,332 | wxWidgets vendor | (3rdparty, not refactored) |
| compileCode.cpp | 4,531 | OES compiler | Split into compileExpression, compileStatement in queue |
| commonObject.cpp | 3,230 | metaCollection | Base class consolidation (not urgent) |
| codeEditorInterpreter.cpp | 2,582 | Designer | Syntax highlighter (monolithic by design) |
| procUnit.cpp | 1,861 | VM runtime | Core dispatch; split risky (no plan) |
| sessionRegistry.cpp | 1,834 | Backend session | Connection + variable lifetime (consolidated, OK) |
| pluginManager.cpp | 1,733 | Plugin system | Load + event dispatch (manageable) |

**Verdict:** No code-smell violations. Large files are monolithic by design (compiler pass, VM dispatch) or are 3rdparty.

---

## 11. Top 20 Load-Bearing File:Line Citations

Every OES coder should recognize these locations:

| Citation | What's There | Why It Matters |
|----------|-------------|-----------------|
| value.h:68-82 | ibValue union + m_fData heap rule | Type-safety foundation |
| value.h:85-398 | ibValueMethodHelper (method dispatch table) | All dynamic method calls route here |
| codeDef.h:130-133 | TYPE_DELTA arithmetic offset | Bytecode type dispatch trick |
| codeDef.h:90-126 | OPER_LFUNC / OPER_ENDLFUNC + cache | Lambda materialization contract |
| byteCode.h:33-64 | ibVarKind + ibFnKind enums | Symbol-table discriminators |
| byteCode.h:127-189 | ibByteCodeVarInfo structure | Variable binding metadata |
| procUnit.h:77-94 | Execute() overloads | VM entry points |
| procUnit.cpp:~500+ | switch(m_numOper) instruction dispatch | VM hot path |
| compileCode.h:— | ibCompileCode class | Compiler phases |
| databaseLayer.h:82-120 | BeginTransaction / Commit / RollBack semantics | Transaction model |
| preparedStatement.h:27-50 | SetParam* / RunQuery interface | SQL safety boundary |
| metaBridge.h:26-103 | HostMetaQuery / HostMeta* API | Plugin metadata mutation |
| pluginManager.h:25-150 | LoadAll / FireEvent / RegisteredFunction | Plugin lifecycle |
| backend_exception.h:190-219 | ibBackendCoreException / Interrupt / Access | Exception types |
| metaObject.h:111-150 | ibValueMetaObject base class | All metaobjects inherit here |
| metaFormObject.h:— | ibValueMetaObjectForm* classes | Form structure + ownership |
| metaCollection/metaObjectComposite.h:— | Attribute + TabularSection + Command embedding | Business object structure |
| frontend/visualView/ctrl/visualHost.h:— | ibVisualHost virtual interface | Frontend boundary |
| byteCodeCache.h:— | SerializeAOT / DeserializeAOT | Bytecode persistence |
| sessionRegistry.cpp:~700+ | Manager instantiation + startup code | Session bootstrap |

---

## 12. Common Patterns: Do's and Don'ts

### Value Creation & Management

**DO:**
```cpp
ibValuePtr<ibValue> val(ibValue::CreateAndPrepareValueRef<ibValue>());
// Automatic cleanup via shared_ptr
```

**DON'T:**
```cpp
ibValue* val = new ibValue();  // Manual delete → memory leak risk
delete val;  // Forgotten in error path
```

### Method Dispatch

**DO:**
```cpp
long lNum = pVal->FindMethod("MyMethod");
if (lNum >= 0)
    pVal->DoMethod(lNum, param1, param2);
```

**DON'T:**
```cpp
pVal->DoMethod("MyMethod", ...);  // String lookup every call — slow
```

### Database Queries

**DO:**
```cpp
auto stmt = ses_query->PrepareStatement("SELECT * FROM t WHERE id = ?");
stmt->SetParamString(1, userInput);
auto rs = stmt->RunQueryWithResults();
```

**DON'T:**
```cpp
auto rs = ses_query->RunQueryWithResults(
    "SELECT * FROM t WHERE id = '" + userInput + "'"
);  // SQL injection vector
```

### Exception Handling

**DO:**
```cpp
try {
    pVal->DoMethod(idx, ...);
} catch (const ibBackendException& e) {
    wxLogError("Failed: %s", e.GetErrorDescription());
}
```

**DON'T:**
```cpp
try {
    pVal->DoMethod(idx, ...);
} catch (...) {
    // Bare catch in non-cleanup context swallows diagnostics
}
```

---

## License & Attribution

OES Enterprise codebase reference compiled May 2026.  
All file paths are absolute; all line numbers are from HEAD.  
Cross-references follow `file.h:line` format.

