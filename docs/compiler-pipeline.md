# The spine — translate → compile → execute

> **Scope:** the backbone: how source text becomes bytecode and how the runtime assembles
> and starts it. `backend/compiler/` — ~18 900 lines, the largest single subsystem.
> **Not repeated here:** the pipeline diagram, the opcode table, the keyword inventory and
> the execution model already live in [ARCHITECTURE.md § Bytecode Engine](ARCHITECTURE.md)
> — read that first. This document is the **roles and the seams between them**.
> Companions: [lambda.md](lambda.md), [closure-capture.md](closure-capture.md),
> [eval-scope-refactor.md](eval-scope-refactor.md), [name-binding.md](name-binding.md),
> [module-manager-split.md](module-manager-split.md), [runtime-facade.md](runtime-facade.md).

---

## 1. Three links, one chain

| Link | Class | Turns | Into |
|---|---|---|---|
| **Translate** | `ibTranslateCode` (`translateCode.h`) | source text | lexemes |
| **Compile** | `ibCompileCode : ibTranslateCode` (`compileCode.h`) | lexemes | `ibByteCode` |
| **Execute** | `ibProcUnit` (`procUnit.h`) | `ibByteCode` | effects |

The inheritance is the design statement: **`ibCompileCode` *is a* `ibTranslateCode`.** The
parser does not call a lexer — it *is* the lexer, extended. One object owns the text, the
lexemes and the emission, so a parse error still knows the line, the file and the module.

Everything else in the folder hangs off these three:

- `byteCode.{h,cpp}` — the artefact (`ibByteCode`, `ibByteUnit`, symbol/function tables)
- `byteCodeAOT.cpp` — serialise/deserialise a compiled `ibByteCode` (skip the parser)
- `compileContext.{h,cpp}` / `procContext.{h,cpp}` — compile-time and run-time scope
- `codeDef.h` — `OPER_*` / `KEY_*` constants
- `procUnitLinq.cpp` / `lambdaQueryAst.cpp` — the LINQ and lambda-AST arms
- `value.{h,cpp}` / `valueFactory.cpp` / `typeCtor.h` — the value system the whole chain moves

---

## 2. Translate — lexer **and preprocessor**

`ibTranslateCode` produces `m_listLexem` (`vector<ibLexem>`; each lexeme carries type,
keyword/delimiter id, string data, an `ibValue`, line and position — see
[ARCHITECTURE.md](ARCHITECTURE.md)).

The part worth knowing: **the preprocessor lives here**, and a define is not text.

```cpp
class ibDefineCollection {
    void SetParent(ibDefineCollection* parent);
    ibLexemList* GetDefine(const wxString& strName);
    void SetDefine(const wxString& strName, ibLexemList*);
    void SetDefine(const wxString& strName, const wxString& strValue);
private:
    std::map<wxString, ibLexemList*> m_defineList;   // ← arrays of LEXEMES, not strings
    ibDefineCollection* m_parentDefine;
};
static ibDefineCollection ms_listDefine;             // ← PROCESS-WIDE
```

- **A define expands to lexemes, not to characters.** `#Define` substitution happens in
  token space, so a macro cannot produce a half-token or break the line map that
  breakpoints depend on.
- **Defines nest** (`SetParent`) — a lookup walks up to the parent collection.
- **`ms_listDefine` is `static`** — the define table is *process-wide*, not per-module or
  per-session. That is worth knowing under `wenterprise-server.exe`, where one process
  serves many sessions.

`ibLexem` reaches back into the translator (`GetModuleName` / `GetDocPath` /
`GetFileName`) through friendship — which is how an error or breakpoint in an included
module still reports the right file.

---

## 3. Compile — recursive descent, with a second pass

`ibCompileCode::Compile()` is a single-pass recursive-descent parser that emits `ibByteUnit`
records — plus **one deferred-resolution pass** for calls to functions not yet declared.

That backlog is `ibCallFunction`, and its fields say exactly what a forward reference must
remember:

```cpp
struct ibCallFunction {
    wxString    m_strName;        // called function
    wxString    m_strRealName;
    ibParamUnit m_puRetValue;     // slot the result goes to
    ibParamUnit m_puContextVal;   // the Context variable
    unsigned    m_numAddLine;     // WHERE IN THE BYTECODE the call was emitted — patched later
    unsigned    m_numError;       // for the message
    unsigned    m_numString;      // source text no. (errors)
    unsigned    m_numLine;        // source line   (breakpoints)
    int         m_numIsSet;       // "no assignment" marker
    wxString    m_strModuleName;  // calls may cross modules
};
```

So a call is emitted **immediately**, with its bytecode position recorded; the second pass
back-patches the target. `m_strModuleName` is why a module can call into another module's
exports — resolution is not confined to one translation unit.

Syntax modes (VES / CES) are the *lexer's* concern; both produce the same bytecode
([../CLAUDE.md](../CLAUDE.md) § Compiler Quick Reference).

### 3.1 The `shortLet` peephole — fuse `OP tmp; LET x` → `OP x`

A compound assignment `x = a op b` first emits the operator into a **temporary** slot, then a
`LET` copying that temp to `x`:

```
OP  tmp, a, b     ; tmp = a op b   (DEF_VAR_TEMP dest, array -3)
LET x, tmp        ; x = tmp
```

The `shortLet` peephole (`compileCode.cpp`, in the assignment handler) collapses the pair: when the
right-hand expression landed in a `DEF_VAR_TEMP` and the immediately-preceding opcode is one of
`ADD/SUB/MULT/DIV/MOD` or a comparison, it rewrites that opcode's destination to `x` and drops the
`LET` — one fewer opcode and one fewer `ibValue` copy per compound assignment, language-wide.

> **Trap (fixed 2026-07-19).** The peephole reads the base opcode with `m_numOper % TYPE_DELTA1`.
> `TYPE_DELTA1` was `#define TYPE_DELTA1 1 * (OPER_END + 1)` — **no outer parens** — so
> `x % TYPE_DELTA1` expanded to `(x % 1) * N == 0` (`%` and `*` share precedence, left-assoc). The
> base op was always `0` (`OPER_NOP`), the match never fired, and the peephole was **dead for every
> assignment**. Additive uses (`OPER_ADD + TYPE_DELTA1`) were unaffected, so nothing else broke and
> the bug hid for a long time. Parenthesising the macro revived it. The string branch of
> `ibProcUnit::AddValue` then exploits the resulting `dest == left` alias to append in place
> (`s = s + …` loops go O(n²) → O(n)); see [runtime-perf.md](runtime-perf.md) §1b.

---

## 4. The artefact — bytecode is a **template**

This is the seam that makes everything above reusable:

> Execute(bytecode, binder, retVal). Bytecode is a **pure template** (`m_listVar` entries
> with kind ∈ {External, Context} declare the …)

`ibByteCode` is not bound to anything. Its symbol table is **kind-tagged**
(`Local / Export / External / Context / ContextProp / Protected`), and the entries that are
*not* local are **declarations of what must be supplied**. `ibByteBinder` reads `m_listVar`
and binds exactly those (`IsBindRequired()` — [../CLAUDE.md](../CLAUDE.md) § Bytecode
resolver).

Three consequences worth stating:

- **One compile, many runs.** The same bytecode serves every session/instance; only the
  binding differs.
- **AOT is possible at all.** Because the artefact carries no pointers into a live world,
  `byteCodeAOT.cpp` can serialise it and deserialise it later, skipping the parser
  ([ARCHITECTURE.md](ARCHITECTURE.md) § Runtime infrastructure). Storage is `vector` —
  **stable order** is part of the format.
- **Designer compiles but does not run** (`eDESIGNER_MODE` — `AttachRuntime` returns early).
  Autocomplete, jump-to-definition and cascading recompile all read the *template*: they
  need the symbol tables, not an execution.

---

## 5. Execute — the scope chain is flattened

`ibProcUnit` is the stack machine (`Execute()` dispatches on `ibByteUnit::m_numOper`;
frames are `ibRunContext`). The structural decision is in `SetParent`:

```cpp
void SetParent(ibProcUnit* procParent) {
    m_procParent.clear();
    if (procParent != nullptr) {
        unsigned int count = procParent->m_procParent.size();
        m_procParent.push_back(procParent);                       // the parent …
        for (unsigned int i = 1; i <= count; i++)
            m_procParent.push_back(procParent->m_procParent[i - 1]);   // … and ALL of ITS ancestors
    }
}

ibProcUnit* GetParent(unsigned int iLevel = 0) const;   // O(1) — index, not a walk
unsigned int GetParentCount() const;
```

**The module scope chain is flattened into a vector, not kept as a linked list.** A unit
copies its parent's whole ancestor list on attach, so resolving "level N up" is an array
index — no pointer chasing per lookup, at the cost of duplicating the list per unit and
re-attaching if the chain changes. Name resolution happens constantly at runtime; parenting
happens once.

`ibProcUnitEvaluate : ibProcUnit` is the eval/watch variant — the same machine, hosting an
expression compiled by `ibCompileEval` ([eval-scope-refactor.md](eval-scope-refactor.md)).
That is what the debugger's Watch runs on
([debugger-architecture.md § 5.1](debugger-architecture.md)).

---

## 6. How the runtime assembles and starts

The chain above is inert. What turns it into a running application:

1. **Metadata opens** → `ibMetaImage` is created, and with it the metadata ctor factory
   ([factories.md § 3.2](factories.md)). A failed load rolls back via `LoadGuard`.
2. **Modules exist as metaobjects** — object modules, manager modules, common modules
   ([../CLAUDE.md](../CLAUDE.md) § Metadata Object Types).
3. **A session is created** (`ibSession`), and it builds its **own runtime root**:
   `m_root : ibValuePtr<ibValueModuleManagerRuntimeConfiguration>` via `CreateRoot`
   ([ARCHITECTURE.md](ARCHITECTURE.md) § Runtime infrastructure).
4. **Per-session ProcUnits** are created for the main + common modules
   (`moduleManager.h`), parented into the scope chain of §5 — so a common module's exports
   resolve from any module below it.
5. **Binding** — `ibByteBinder` supplies the `External` / `Context` slots each bytecode
   declared (§4). This is where a template becomes *this session's* code.
6. **Lambdas** get a per-session shim: `m_lambdaRuntime : unique_ptr<ibProcUnit>`, wired to
   the root's procUnit on first `GetLambdaRuntime()`. `ibSession::CompileRoot` reaches into
   `m_pppArrayList` / `m_ppArrayCode` / `m_cCurContext` directly — hence
   `friend class ibSession` on `ibProcUnit`.
7. **Execution** — a form event, a script call, a scheduled job enters `Execute()` /
   `CallAsFunc` on the right unit. Sessionless callers fall back to a
   `thread_local ibProcUnitState`.

Read the ownership as: **image → session → root module manager → procUnits**. The bytecode
is shared; the frames, bindings and lambda shim are per session
([runtime-facade.md](runtime-facade.md), [module-manager-split.md](module-manager-split.md)).

### 6.1 The module manager is the root module, not just a holder

`ibValueModuleManagerRuntimeConfiguration` is easy to mistake for a container. It is not —
it is **the central entry point and the root module every other module descends from**.

```cpp
class BACKEND_API ibValueModuleManager :
    public ibValueDynamicMembers, public ibRuntimeModuleDataObject { … };   // IS a module descriptor

class BACKEND_API ibValueModuleManagerRuntimeConfiguration :
    public ibValueModuleRuntimeManager, public ibRuntimeRoot {
    // GetRoot override — we are the root, return ourselves as the ibRuntimeRoot interface pointer.
    const ibRuntimeRoot* GetRoot() const override { return this; }

    bool BeforeStart();  void OnStart();  bool BeforeExit();  void OnExit();   // ← session lifecycle
    virtual bool CreateMainModule();   virtual bool DestroyMainModule();
    virtual bool StartMainModule(bool force = false);   virtual bool ExitMainModule(bool force = false);
};
```

Three roles in one object:

- **It is itself a module** (`ibRuntimeModuleDataObject`) — it has its own bytecode and
  ProcUnit, not merely pointers to other modules'.
- **It is the parent of every other module's scope.** Because `ibProcUnit::SetParent`
  copies the parent's whole ancestor list (§5), parenting a module onto the root makes the
  root's exports resolvable from *anywhere below* — that is what "everything else inherits
  from it" means mechanically. Rename an export on the root and every module sees it.
- **It is the session's object graph.** `ibSession::m_root` holds it; it owns the common
  modules, forms and per-instance object runtimes for that session — so "all the objects
  within a session" hang off this one node. Concurrent web sessions each get their own root,
  which is why there is no shared ProcUnit and no cross-session execution mutex
  ([ARCHITECTURE.md](ARCHITECTURE.md) § Sessions).
- **It is the entry point.** `BeforeStart` / `OnStart` / `BeforeExit` / `OnExit` are where a
  session's life actually begins and ends; `StartMainModule` runs the configuration's main
  module.

`ibRuntimeRoot` itself is an **empty interface** (`virtual ~ibRuntimeRoot() = default;` and
nothing else, `moduleInfo.h`) — an identity tag, the same technique as
`ibDatabaseConnectionHolder` ([database-layer.md § 3](database-layer.md)): it lets a child
say "my root" without dragging the manager's header in.

The Designer has a deliberately **different** root: `ibValueModuleManagerDesigner` derives
the *lightweight* base — no ProcUnit, no runtime common-module units, no `Attach`. It exists
so the code editor can read the `Manager` singleton, the ctor-context (Catalogs / Documents
/ Enums) and global constants from a holder that tracks designer state, while common modules
are surfaced from metadata storage + live text parsing — *"so no fragile runtime unit ever
enters the editor's read path"* ([module-manager-split.md](module-manager-split.md)).

---

## 7. A failure leaves as DATA — `ibDiagnostic` (landed 2026-08-04)

Both links of the chain fail the same way, and both report through one funnel:
`ibBackendException::ProcessExceptionError`. The compile side arrives from
`ibCompileCode::DoSetError` (text refused before it ever ran); the runtime side from
`ibProcUnit`'s catch, with the failing `ibByteUnit` in hand.

**What changed:** that funnel used to produce one assembled sentence —
`{Module(42)}: Divide by zero` — and everything downstream read it the way a person does. The
position was never missing; it was just never assembled. `ProcessError` already took file, module,
doc path, position and line as arguments, and the debug protocol has carried the same fields for
years (`ibDebugData`, `ibDebugLineData`).

Now the funnel builds `ibDiagnostic` (`backend/backend_diagnostic.h`) and **the message is
assembled from it**, never the other way round:

| Field | |
|---|---|
| `m_kind` | `Compile` / `Runtime` — a compile error means the text never ran; nothing in the message says that |
| `m_docPath` | the module's guid — the only stable address, and the one the designer navigates by |
| `m_moduleName`, `m_fileName` | what a person reads; the file is set for external reports / data processors |
| `m_line`, `m_position` | 1-based line; offset into the module text (a column derives from it later without touching any producer) |
| `m_code` | the engine's error code — machine-readable identity that a translated message does not change |
| `m_message`, `m_codeLine` | the failure without the `{Module(42)}:` decoration, and the offending source line |
| `m_stack` | frames as `{module, line}` pairs |

**Delivery is a sink, not a return value** (`ibDiagnostics::Subscribe` / `Publish`): a failure is
reported from deep inside the interpreter, and the stack between it and whoever cares is not ours
to change. Subscribers are process-wide because errors happen wherever they happen — a job session,
a compile in the designer, a background run — and a sink threaded through all of those would be
threaded through none. Publishing is best-effort: a sink that throws is swallowed, because an
exception is already unwinding.

Three consumers, one record: the **dialog** (text built from the record, `frame->BackendError`
unchanged), the **debugger** (as before), and a **headless caller** — a build step, a test, or an
AI assistant asking "compile this and tell me what is wrong", which is the consumer that cannot
exist while the answer is prose.

⚠️ **The stack is now collected whether or not anyone is looking.** It used to be gathered inside
the "is there a frame" branch, so failures in a job, a background run or a headless check — exactly
the ones nobody watches — reported no stack at all.

---

## 8. Honest remainder

- **`ms_listDefine` is process-global** (§2) — verify before relying on `#Define` under the
  multi-session web host.
- `compiler/` is ~18 900 lines with `value.h` alone at 1 485 — the split candidates are
  visible but the seams are load-bearing; treat as restructuring input, not a cleanup.
- The `ibCallFunction` backlog resolves *by name across modules*; a rename between compile
  and resolve is the failure mode that produces a "function not found" at the wrong line.
- Step 12 of the runtime facade (cross-bc metadata) is the one unlanded piece of this
  assembly ([runtime-facade.md](runtime-facade.md)).
