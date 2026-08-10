# Compiler AST — the arc

**Status: ATTEMPTED AND REVERTED (2026-08-10). The problem stands; the method was wrong.**

The arc was built and then rolled back the same week. What follows is still the analysis of what
the absent tree costs — that part held up — but read §0 first: it says what actually happened,
which of the claims below survived contact, and what replaces the method.

---

## 0. What the attempt cost, and what it bought

**The method was the mistake.** The reading half was REWRITTEN rather than moved. The single-pass
reader demanded its tokens (`GETKeyWord(KEY_THEN)` — take it or raise); the rewrite asked for them
(`if (IsNextKeyWord(KEY_THEN)) GetLexem();`). It also replaced the block's exit rule — the
single-pass `default: return true`, where any unhandled keyword ends the block and the CALLER
demands its fence — with a hand-written list of terminators. Between those two, VES stopped being
a dialect: `if x { … }` with no `Then` and no `EndIf` passed the syntax check clean.

Every regression that followed was of that shape, not of the tree's:

- fences optional in VES; braces accepted there too (the dialect gate ran one way only);
- a braceless CES body swallowed the statements after it (the one-statement rule was dropped);
- `If … ElseIf … EndIf` would have demanded two `EndIf` (ElseIf read as a statement of its own);
- `Procedure … EndFunction` became legal (the closer stopped matching the opener);
- the method name went into the pool upper-cased, so a missing `Message` was reported as
  `'MESSAGE'`;
- the caller's argument count was replaced by the declared count, so `Message("text")` was told a
  status argument had been supplied.

**What it bought, measured:** net −1863 lines, two files removed
(`codeEditorInterpreterContext.cpp`, `compileContextLinqData.h`), and IntelliSense's private
lexeme cursor deleted. **What it did not buy:** any measurable performance
([runtime-perf.md](runtime-perf.md) §1e — the arc cost nothing and gained nothing).

**What survived the revert, and is the real return on the week:**

- the **script corpus** (`tests/test_scriptCorpus.cpp`) — 4 defects, including `ibNumber(double)`
  under a non-invariant locale and a correlated join;
- the **emission contract** (`tests/test_compilerContract.cpp`) and the **shape suite**
  (`tests/test_compileTree.cpp`) — the net that then caught a regression introduced by the
  carry-over itself;
- the **runtime work** — frame-entry cost, carried onto the canonical compiler and re-measured
  ([runtime-perf.md](runtime-perf.md) §7): every call-bearing row about a fifth cheaper;
- two defects found in the canonical compiler once attention was free:
  the assignment fold rewriting an instruction that did not produce its temp (`x++` returned the
  new value and never stored), and a label address of 0 being indistinguishable from "not
  declared" (a label at the top of a body was never found).

**The method that replaces it.** Do not write a second reader. The compile module already builds a
tree — it just prints it straight to bytecode. Give the SAME reader a second output, or carry the
extra information on the bytecode itself and slice it away at the boundary; the runtime already
takes a pointer to base, and the AOT writer already knows only base fields, so the strip is
structural rather than a step someone can forget. And the editor does not need a stored model at
all: `m_onlyFunction` compiles heads without bodies, `ibCompileEval` compiles ONE expression in an
existing scope, and between them a caret is served without ever compiling broken text.

The rest of this document is the 2026-08-09 reconnaissance, unchanged.

---

**Original status line: PLAN. Nothing applied.** Reconnaissance done 2026-08-09; no code was changed for it.

The script compiler is single-pass recursive descent that emits `ibByteUnit` records as it
parses, with no intermediate tree ([compiler-pipeline.md](compiler-pipeline.md) §3). That was a
reasonable choice and it is still the fastest way to compile. This document is about what it now
costs, what the cost is *not*, and the order in which a tree can be introduced without a rewrite.

---

## 1. What the absence of a tree costs today

### 1.1 Every construct is implemented three times

| implementation | lines | what it does |
|---|---|---|
| `compiler/compileCode.cpp` | 4 984 | parse **and** emit — LINQ alone is lines 2263–3010 |
| `frontend/…/codeEditorInterpreter.cpp` (+`codeEditorParser.cpp`) | 2 816 (+423) | a second parse, for IntelliSense |
| `compiler/lambdaQueryAst.cpp` | 349 | a third parse, for SQL pushdown |

While the constructs were statements (`If`, `While`, `For`) this was tolerable — they are simple
and nearly identical in all three. LINQ breaks the tolerance, because it is not a statement but a
**sub-language with its own scoping**. The code does not grow linearly with the language; it grows
threefold per addition.

### 1.2 The second parser gives up, and that costs query performance

`lambdaQueryAst.cpp` recovers the *shape* of a lambda body (`row => row.k = a.k`) by re-parsing the
raw lexeme span, because there is no tree to consult. Its own header states the policy:
*"Conservative: any doubt = null = RAM path."* There are **26 bail-out points**, and the recognised
subset is deliberately tiny — `Column / Literal / Param / Arith / Compare / Logical / Not`, literals
primitive only (the same list gates AOT persistence, `byteCodeAOT.cpp` `AstSerializable`).

Every bail-out is a query that could have run in the database and instead pulls rows into the
process. On a large table that is not a percentage, it is an order of magnitude. The subset is
narrow not by preference but because widening a parser that guesses from lexemes is frightening;
reading a tree is not.

> Note: `byteCode.h` and `valueQueryable.cpp` both still carry a comment saying the lambda AST is
> deliberately NOT serialised into the AOT cache and that a cache hit degrades to RAM. **That is
> stale** — v14 added serialisation (`byteCodeAOT.cpp`, presence byte + recursive dump). The real
> boundary is the subset above, not the cache.

### 1.3 The third parser is knowingly weaker, exactly where the language got interesting

The editor's precompiler registers LINQ bindings **untyped**:

```cpp
m_activeContext->AddVariable(realName, wxEmptyString, false, false, ibValue());
// GetExpression();   // source expression; element type unknown
```

with the comment: *"Real LINQ scoping is per-block with RETURN_BLOCK chain delegation; precompile
flattens everything onto the active context for IntelliSense convenience."* Meanwhile the real
`CompileLinqBlock` **does** know the element type — it must, to bind columns. The knowledge exists
in one walk and is absent from the other.

### 1.4 Two smaller ones

- **The `shortLet` peephole exists only because codegen emits as it parses.** `x = a op b` cannot
  be written straight into `x`, so a pair of opcodes is pattern-matched afterwards. It matches one
  shape and is blind to nesting: `x = (a+b)*(c-d)` still allocates a temp per subexpression. It was
  also globally dead for a long time (the `TYPE_DELTA1` macro bug, §3.1 of compiler-pipeline) and
  the language silently paid O(n²) string concatenation for it. An emitter walking a tree writes
  into the destination by construction, and has nothing to be dead.
- **`PrepareFromCurrent`** re-lexes a source region through a whole second `ibTranslateCode` to
  handle `#Define` / `#Ifdef`.

### 1.5 The one coming due

The roadmap's language service and `script_check` over MCP are the same request as §1.3, plus
"report **all** errors". A single-pass parser dies on the first one.

---

## 2. The asymmetry that settles the argument

The **query** side already has a tree, and it has already paid for itself:

- `query/queryAst.h` — 284 lines, 15 expression kinds;
- `queryRender.{h,cpp}` — prints it back to text; this is what makes the query constructor possible
  at all ([query-constructor.md](query-constructor.md));
- `queryRewrite` — moves a condition into `HAVING` by rewriting the tree;
- `tests/test_queryRoundTrip.cpp` — a property test, `render(parse(render(x))) == render(x)`, which
  found two defects unreachable by clicking.

A constructor over script LINQ needs the same three things. The script language has none of them.
The design is not being chosen here — it was chosen once, built, and verified on the query side.

---

## 3. What does NOT change

This is the whole reason the arc is tractable. **The bytecode contract stays.**

`ibByteCode` is consumed by the interpreter (`procUnit.cpp`), the AOT cache (`byteCodeAOT.cpp`),
the debugger (line map, breakpoints) and the binder. A tree inserted *between* parse and emit
touches none of them. `ibCompileContext` — all the scope machinery — survives unchanged; it is
merely consulted at emit time rather than during one interleaved walk. The lexer is **already
shared**: `ibParserModule : public ibTranslateCode`, and `codeEditorInterpreter.h` includes
`backend/compiler/compileCode.h`.

So this is not a new architecture. It is **moving an existing seam up by one level**:

```
lexer  →  parse (one tree)  →  resolve (one resolver)  →  { emit bytecode | answer about a caret }
         ^^^^^^ shared today only up to here ^^^^^^
```

---

## 3a. Who asks what — and at which layer

Six consumers ask "what is this code", and today they are soldered together across four
implementations. The design question is not "how do we put everything on the tree" — it is **which
is the cheapest layer that can answer each question**, because they run at wildly different
frequencies.

| consumer | today | needs | tier | moves? |
|---|---|---|---|---|
| **translator** (lexer + preprocessor) | `ibTranslateCode` — already incremental | — | produces tier 1 | **no — unchanged** |
| **syntax highlighting** | its own range-scoped re-lex through the scratch translator `m_tc` (`SetLexer(wxSTC_LEX_CONTAINER)` — OES colours it itself) | token class over a range | lexemes | **stays at tier 1, but should read the SHARED cache** — see §3d; the second translator instance goes away |
| **folding** | `m_fp`, fed `KEYWORD` lexems from the shared cache | nesting, not meaning | lexemes | **no** |
| **context help** | part of `codeEditorInterpreter.cpp` | the resolved value under the caret | semantic model | **with IntelliSense** — it is a second question to the same model, not a separate consumer (LSP calls the pair `hover` / `signatureHelp`) |
| **symbol list** (functions / procedures / vars / lambdas) | `codeEditorParser.cpp` → `ibModuleElement{name, kind, lineStart, lineEnd}` | a list of declarations | syntax tree | **no — deliberately left alone**; it is answered fine today, and the rule of this section is that a consumer climbs only when the tier below cannot answer it |
| **LINQ constructor** (the point of the arc) | — | tree + render → text, **and** scope resolution | syntax tree **+** semantic model | **new — it is the acceptance test (§3f-pre)** |
| **IntelliSense** | `codeEditorInterpreter.cpp` (2 816 lines) | tree + resolved values, PATH | semantic model | **yes** |
| **SQL pushdown** | `lambdaQueryAst.cpp` (349 lines) | tree + resolved values, PATH | semantic model | **yes — deleted, step 2** |
| **compiler / interpreter** | `compileCode.cpp` (4 984 lines) | tree + emission, TOTAL | semantic model | **yes** |

Three tiers, and the split is a **performance** layering as much as a conceptual one, because the
three run at three frequencies: per keystroke over the visible region (tier 1), per edit and
debounced (tier 2), on demand at one caret (tier 3). Only tier 3 pays for resolution, and only
tier 3 needs to be lazy and cached.

**Highlighting and folding should NOT move to the tree.** They are already at the right layer, they
are fast, and — decisively — they work on text that is broken, half-typed and unbalanced, which is
the state an editor is in most of the time. Moving them onto a tree would trade that robustness for
nothing they need. The rule for the arc: **a consumer climbs a tier only when the tier below cannot
answer its question**, never because the tree is new and interesting.

That leaves the actual consolidation smaller than it first looks: the symbol list and the
constructor take tier 2, and the three implementations that genuinely duplicate each other —
IntelliSense, pushdown, bytecode — meet at tier 3.

---

## 3b. Why the duplication was inevitable: everyone *is-a* translator

```
ibTranslateCode                          lexer + preprocessor; already incremental —
│                                        PrepareLexem(line, ±lines, ±len) patches the
│                                        lexeme array for one edit
├── ibCompileCode → ibCompileModule      parse + emit bytecode
├── ibPrecompileCode      (frontend)     parse again → IntelliSense
└── ibParserModule        (frontend)     parse again → symbol list
    ibFoldLevelParser                    not a subclass; eats KEYWORD lexems → fold points
    ibCodeEditor::m_tc                   a SECOND translator instance, re-lexes a text range
                                         for styling
    lambdaQueryAst                       parses a lexeme span → ibQueryAstExpr
```

**Inheritance is the cause.** To reach the lexemes you must *become* a translator, and once you are
one, walking them yourself is the path of least resistance. Nothing here was careless; the shape
made duplication the cheap option.

The structural move is therefore not "add a tree" but **stop inheriting, start consuming**: the
artefacts become values that are passed, not base classes that are derived.

```
text → ibTranslateCode ──► lexeme stream ──┬─► highlighting      (tier 1, per keystroke)
        (unchanged)                        └─► folding           (tier 1, per keystroke)
                              │
                       ibSyntaxTree  (NEW) ─┬─► symbol list      (tier 2, per edit)
                    fragment-tolerant,      └─► render → text    (tier 2, constructor)
                    error-local, spans
                              │
                     ibSemanticModel (NEW) ─┬─► IntelliSense     (tier 3, PATH)
                     resolution over        ├─► SQL pushdown     (tier 3, PATH)
                     the tree               └─► bytecode         (tier 3, TOTAL)
```

`ibCompileCode` stops deriving from the translator and becomes a consumer of a tree like everyone
else. That is the one change that makes the duplication *impossible* rather than merely discouraged.

## 3c. The two modes — PATH and TOTAL, and why there is still only one resolver

The distinction that makes this work, stated as the requirement it is:

- **PATH** — resolve only what one node depends on. Anything unresolvable becomes `unknown` and
  propagates. Nothing else in the module has to be well-formed. This is IntelliSense at a caret,
  and it is pushdown of one lambda.
- **TOTAL** — every node must resolve; a failure is an error. This is bytecode emission, and it is
  the **only** consumer that needs it.

The consequence worth writing down: **PATH is the general case and TOTAL is the special one.**
TOTAL is PATH applied to every node, plus a policy that says `unknown` is fatal. So this is one
resolver with a policy parameter, not two engines that must be kept in step — which is exactly what
went wrong the first time.

## 3d. One cache, many readers — the constraint the arc must not break

The editor already works this way, and it is not an optimisation to be re-litigated: **one shared
result is computed and then patched; every consumer reuses it rather than recomputing.**

| | source | when |
|---|---|---|
| shared lexeme cache | `m_precompileModule`'s array | full pass on module open; afterwards **patched** per edit: `PrepareLexem(line, ±lines, ±len)` |
| folding | reads that cache (`KEYWORD` lexems → fold points) | reuse only |
| IntelliSense | reads that cache | reuse only |
| highlighting | today: its **own** range-scoped lex through the scratch translator `m_tc`, driven by Scintilla's `OnStyleNeeded` | per visible range |

**Highlighting should join the readers.** The cache already holds every token's class and position,
and positions are already maintained under patching — so styling a range becomes a binary search
into a position-sorted array plus an iterate, which is strictly cheaper than re-lexing the range.
The scratch translator `m_tc` disappears with it, leaving one lexing path instead of two. Ordering
already works: the edit handler patches the cache before Scintilla asks for styling.

### Who owns the cache

The natural reading of "IntelliSense patches it, everyone else reads it" puts the cache on the
IntelliSense object — which is where it sits today (`ibPrecompileCode` owns the lexeme array and
`PrepareLexem` patches it). That has two consequences worth avoiding:

- **an inverted dependency** — highlighting, the cheapest and most frequent consumer, would depend
  on the most expensive one;
- **it is exactly why the LINQ constructor cannot reach the data** (§3f-pre, blocker 1):
  `ibPrecompileCode` lives in `frontend/…/codeEditor/` and is bound to `ibCodeEditor`, so anything
  that is not the editor has to drag the editor along or copy it.

So: **one writer, many readers — but the writer is the document, not a consumer.** A module-level
source model owns the lexeme cache and, later, the tree; it exposes one write door (`patch(edit)`,
called by the editor on text change) and read doors for tokens-in-range, fold points, declarations,
and tree + resolution. IntelliSense stops being the owner and becomes a reader like the rest, and
the constructor can reach the same model without an editor in the picture.

This is the same conclusion as §3b — stop inheriting, start consuming — arrived at from the caching
side instead of the parsing side.

**Why this is a hard requirement and not a preference.** The compiler measures ~160 K lines/s
(parse + emit, [runtime-perf.md](runtime-perf.md) §6). A 100 K-line module therefore costs on the
order of half a second for a full pass — per keystroke that is not slow, it is unusable. Lexing
alone is cheaper than that, but not cheap enough to do globally on every edit, which is exactly why
the incremental path exists.

**So the rule for this arc: the tree must not become a second thing that is recomputed globally.**
It inherits the same lifecycle as the lexeme cache — a full parse when the module is opened, and a
**spliced node** on every edit thereafter. An arc that produced a correct tree by reparsing 100 K
lines per keystroke would be a regression against what exists today, however clean the tree looked.

Folding and highlighting are also the reason §3a leaves them where they are: they already consume
the cheapest thing that answers them, one of them by reuse and one by a scoped pass, and moving
either onto the tree would make a per-keystroke path depend on a per-edit one.

### The patch unit moves up a layer, and gets cheaper

Today the patch unit is the lexeme array. With a tree carrying spans it becomes a **node**:

1. Patch the lexeme array as today (unchanged).
2. Find the smallest node whose span contains the edit.
3. Re-parse **that span only**; splice the new subtree in.
4. Invalidate resolution on that subtree.

This is *less* work than today, not more: currently an edit re-runs the whole precompile walk over
the patched array. The prerequisite is that every node stores its lexeme span — which the debugger's
line map needs anyway (§5).

Step 4 is the one to be careful with: an edit inside one function can change what a *later*
function sees (a new export). Start with the conservative rule — patch the syntax tree, drop
resolution for the whole module — because parsing is the expensive half and resolution is lazy by
construction. Narrow it later, with a measurement.

**The pathological case, named up front.** An edit that changes *block structure* — deleting an
`EndIf`, typing a `}` — moves everything after it into a different node, so "the smallest node
containing the edit" is the wrong unit: it is correct only while the edit stays inside one block.

The practical answer is to make **the enclosing function the patch unit**, not the smallest node. A
function is a natural boundary, is usually tens of lines rather than thousands, and the editor
already tracks exactly these boundaries — `ibFoldLevelParser` has `FoldProcedure` / `FoldFunction`
among its kinds. Re-parsing one function per edit is bounded work regardless of module size, which
is the property that matters at 100 K lines. Only an edit outside every function (a module-level
declaration) falls back to a full parse, and those are rare.

The unbalanced-fragment case then needs no special handling in the parser: the function being
edited parses into a partial subtree with an unclosed node, resolution over it is error-local
(§5.2), and everything outside that function is untouched and still correct.

---

## 3e. The plan, step by step

Each step has a gate, and steps 1–3 do not touch IntelliSense at all — the editor keeps working
while the foundation lands.

| # | step | gate |
|---|---|---|
| **0** | **Freeze the contract.** A corpus of modules, compiled, with the emitted `ibByteCode` hashed into a test. | the hash test itself — this is what makes every later step safe |
| **1** | **Expression tree, compiler-internal.** `GetExpression` splits into `ParseExpression` → node and `EmitExpression` → opcodes. Slot allocation moves into Emit (§5.1). | corpus hashes unchanged, byte for byte |
| **2** | **Delete `lambdaQueryAst.cpp`.** Pushdown reads the real tree instead of re-parsing lexemes. | `test_lambdaRecorder` green, plus new cases for shapes that used to bail — **first visible payoff, and it is a query-performance one** |
| **3** | **Declarations + spans.** Function / procedure / variable declaration nodes — the semantic model has to know what is declared, and spans are what make incremental patching (§3d) and the debugger's line map work. `ibParserModule` is **not** migrated: the symbol list keeps its own walk. | declarations present and spans correct — *not* "symbol list identical", because the symbol list does not move |
| **4** | **The semantic model.** `ibPrecompileCode`'s resolution logic moves onto the tree as the shared resolver with the PATH/TOTAL policy (§3c). The big one, last on purpose. | IntelliSense answers identical on a corpus of caret positions |
| **5** | **Statements.** Only if it earns it. | corpus hashes unchanged |

---

## 3f-pre. The LINQ constructor is the arc's acceptance test

Hold the plan this way rather than as "five steps and then, hopefully, something". The LINQ
constructor needs every part of the semantic model and nothing else does — so if it works, the arc
is done.

**Where it differs from the query constructor, exactly.** A query constructor's sources are
**metadata**: pick `Catalog.Contractors` and the field list comes from the configuration,
statically, with no code involved. A LINQ constructor's sources are **values in scope**:
`from x in <expr>`, where `<expr>` may be a local, a parameter, a session parameter, or a call into
a common module. That single difference is the whole difference between reading metadata and
running a semantic model.

Its three questions, and where each is answered:

| question | answered by | exists? |
|---|---|---|
| what is in scope at this point? | semantic model, PATH mode | **no — step 4** |
| what does this source expression denote? | the same resolver | **no — step 4** |
| what fields / methods can be taken off that value? | `ibMemberTable` (`compiler/value.h`) — names, param counts, help text, `Readable`/`Writable`/`Scoped` flags | **yes, already, and in the backend** |

So no new field extractor is needed; the door IntelliSense already uses is the door the constructor
needs. What is missing is only that today it cannot be reached without *being* the editor's
precompiler.

**Same discipline as the query constructor: the engine is the only judge.** That window composes
text and hands it to `ibQueryParser::ParsePackage`, showing the engine's own words verbatim
([query-constructor.md](query-constructor.md)). The LINQ constructor must do the same — compose
script text and hand it to the compiler — which means it also needs render(tree) → script text.
That is tier 2 on top of tier 3, and it is precisely why this feature is the culmination rather
than another consumer.

**Narrower on one axis, wider on the other.** It does *not* need totals or batching (the query
constructor's `TOTALS`, the `;`-package). It *does* need scope resolution, which the query one
never needed. Worth stating so nobody rebuilds totals into it by symmetry.

This gives step 4 a better acceptance criterion than "IntelliSense answers identically": **inside a
half-written LINQ block, offer the members of the source expression at the caret.** That is a test
that can be written.

### Where IntelliSense and the query constructor become one thing

This is the convergence the feature forces, and the reason it reads as a wall.

"What can I take from this source" has **two** answers in the codebase today:

- the query side — source → `DescribeOutput` → columns, each with an id and a type;
- the script side — value → `ibMemberTable` → properties and methods, with `Readable`/`Writable`/
  `Scoped` flags, arity and help text.

In a LINQ constructor the source may be either, and the user must not be able to feel the seam.
That is not a UI problem: it is one question with two mechanisms and nowhere for them to meet.

The meeting place is not a bridge between metadata and values, because in OES **metaobjects already
are values** (`ibValueMetaObject : ibValue`). It is a single door asked of a value — `ibMemberTable`
— and the mechanism for *"a value whose member surface is computed per instance at runtime"* is
already built and already in use: `ibValueDynamicMembers` + `BindContainerNames(helper, ctx)` +
`Invalidate()`, which is how `ibValueContainer` publishes a Structure's keys as members.

The concrete gap: **`ibValueQueryable` derives from plain `ibValue`, not from
`ibValueDynamicMembers`** — so a queryable publishes no member surface, and its columns are
reachable only through the query-side path. Filling a queryable's member table from its own output
description is what makes the two lists one: **one door, two fillers**, not a new abstraction.

Worth noting for sequencing: this piece is a *value-system* change and is largely independent of
the tree. It can land before or beside the arc, and on its own it would let IntelliSense offer the
columns of a queryable in scope — a cheap early win that also de-risks step 4.

> **Hard rule the constructor forces.** Resolving `from x in GetContractors()` must **not execute**
> user code to answer a UI question. PATH resolution answers with the *shape* a value has, never by
> invoking. The precedent already exists as `m_calcValue` — the debugger-watch path computes,
> the autocomplete path does not — and in the model it stops being a flag on a walker and becomes a
> property of the mode: **only an explicit watch may evaluate.**

---

## 3f. One decision to take before designing the nodes

**Does the LINQ constructor have to preserve the user's formatting?**

The query constructor renders from the AST and *regenerates* text — comments and spacing are not
preserved, and nobody minds, because a query literal is usually machine-shaped anyway. Script LINQ
sits inside hand-written code a person owns and formats.

If round-tripping must preserve formatting, the tree has to be **full-fidelity**: every space,
newline and comment attached as trivia, and every node able to reproduce its exact source text.
That is a materially bigger node design and it cannot be retrofitted cheaply. If regeneration is
acceptable — as it is for queries — the tree stays lean.

This is a product decision, not a technical one, and it should be made before step 1.

---

## 4. Order of work — expressions first, statements last

Counter-intuitive, and it is the point:

1. **Expressions.** A caret is always inside an expression — "what may I type here" is a question
   about an expression, never about a `While`. `lambdaQueryAst` re-parses an expression and nothing
   else. A constructor must render an expression back to text. Statements (`If`/`While`/`For`) are
   where a tree buys the *least*; they can stay emit-as-you-go indefinitely.
2. **LINQ clauses**, which is where the threefold cost actually bites.
3. **Statements**, mechanically, if ever.

Node set: **extend `ibQueryAstExpr`, do not invent a second expression tree.** A second expression
AST in one engine is precisely the duplicated currency this codebase removes on sight.

The payoff arrives after step 1, not after the phase: once expressions parse to a tree,
`lambdaQueryAst.cpp` **is deleted** — pushdown reads the parse instead of guessing it, and its
subset widens to whatever the lowering supports — while the editor's statement walk can still be
its own.

---

## 5. The three real constraints

**5.1 Slot allocation must leave parsing.** Today every `Compile*` returns an `ibParamUnit` — an
already-allocated slot — so temporaries are allocated *during* the parse. This is the one genuinely
invasive move, and it is required twice over: a tree needs allocation at emit, and a *shared*
resolver needs to be side-effect-free. Expressible as a checkable property: **no `Parse*` may touch
`CreateVariable`.**

**5.2 An error is LOCAL to its node — the walk resolves everything else anyway.** This is stronger
than "tolerate holes", and it is the requirement that decides the design. The compiler resolves a
complete, valid module and may stop at the first error. The resolver may not: a mistake can sit
anywhere — after the caret, before it, in the middle, or be simply something forgotten — and none
of those may prevent the rest of the tree from being resolved. Everything computable **must** be
computed, so the path through the graph really goes through. "Unknown" is a value that propagates
upward, never a reason to abandon the walk. This is very likely why the two walks forked in the
first place. The discipline exists already in `lambdaQueryAst` ("any doubt = null") — it only has
to mean "unknown, keep going" instead of "give up".

**5.2a The parser must accept a FRAGMENT, and an unbalanced one.** An editor asks about a region of
code, not a module: a construct may be opened and never closed, and the enclosing scope may have to
be established by scanning back from the caret rather than forward from the top. This already
exists as a working stopgap (§5a) and is a first-class requirement of the tree parser, not an
afterthought — a parser that only accepts whole valid modules cannot serve the editor at all, which
is precisely how the third implementation came to exist.

**5.3 Two driving modes over one resolver.** The compiler wants resolution eagerly for a whole
module; IntelliSense wants only the path from root to caret, on every keystroke. Same code, lazy
per-node with a cache. (This mode already exists as a flag — §5a.)

Also: nodes must carry source position. `AddLineInfo` stamps a line onto every opcode for the
debugger and breakpoints; emitting from a tree must preserve that, or the debugger silently drifts.
And `kAOTFormatVersion` (currently 20) bumps if emission order shifts anywhere — routine, done five
times.

### 5a. The caret machinery already exists — carry it forward, do not re-invent it

`ibPrecompileCode` (`codeEditorInterpreter.h`) already implements fragment-scoped resolution, and
it is designed rather than improvised. Each piece has a direct counterpart on the tree:

| today (side effect of a walk) | on the tree (a query) |
|---|---|
| `m_caretPos` — the user's caret, deliberately kept separate from the lexer's `m_currentPos` (shadowing them breaks `IsEnd()`) | an offset; the tree is asked which node contains it |
| `m_cursorContext` — the context captured while walking past the caret | the scope chain **at** a node, derivable at any time without re-walking |
| `declPos` per variable, and `LoadSysKeyword` filtering out `declPos > caret` | node positions are already in the tree — visibility is "declared before this node", answerable directly |
| `m_calcValue` — compute `ibValue`s (debugger watch) or skip them (autocomplete preview) | §5.3's two driving modes, already anticipated as a flag |
| `m_lastExpression` / `m_lastKeyword` / `m_lastParentKeyword` — what the walk had reached | the node at the caret and its ancestor path — what LSP calls the node path |

The pattern to notice: every one of these is **state the walk had to leave behind because there was
nowhere to put it**. On a tree they stop being state and become questions. That is the whole
mechanical content of the change on the IntelliSense side — the hard-won logic about what is
visible where is kept verbatim; only its storage moves.

---

## 6. The proof gate

Both paths must produce the **same `ibByteCode`**, so each step can demand byte-identical emission
against the old path. That is a falsifiable assertion, not a hope, and it lets the tree arrive one
construct at a time behind a flag. Backed by 1 166+ tests and `ParserBench`.

---

## 7. What this arc is NOT

**It is not a performance project.** The 2026-08-09 measurements
([runtime-perf.md](runtime-perf.md) §6) close that door honestly: the arith body already compiles
to five opcodes and cannot be shortened; dispatch is ~15.4 ns/opcode with ×1.3–1.5 of headroom
left. Where runtime time actually goes — `Structure::Insert` at ~1 630 ns and a dotted field read at
550 ns — is *inside* what an opcode does, which codegen cannot reach. That is a container problem
(`std::map` keyed on a whole `ibValue`, plus member-table invalidation per mutation), and it is the
separate, larger runtime win.

Judge this arc on two things only: **three implementations collapsing to one**, and **the query
pushdown giving up less often**. Compile throughput will probably get slightly *worse* (a tree costs
allocations; emit-as-you-go is the fastest way to compile), which barely matters because the AOT
cache means a module compiles once per configuration.

---

## 8. Cheap experiment before committing

Instrument `ibBuildLambdaQueryAst` to count null returns and run a real configuration. A day's work,
and it converts the largest claim in §1.2 from an argument into a number: whether 2 % of lambdas
fall to RAM or 60 %.

---

## 9. Prior art worth reading

The shape has a name, and it is not searchable as "AST": **compiler as a service** — an immutable
full-fidelity syntax tree, a **semantic model** over it (tree + symbol resolution), and an
**error-tolerant parser** that yields a tree for broken input. Those are exactly §4, §5.2 and §1.5.
Roslyn (C#) for the layering; the LSP specification for the question set a semantic model is
expected to answer (`hover`, `completion`, `signatureHelp`, `definition`) — which is also the list
`script_check` will expose over MCP.
