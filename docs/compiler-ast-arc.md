# Compiler AST — the arc

**Status: PLAN. Nothing applied.** Reconnaissance done 2026-08-09; no code was changed for it.

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

| consumer | today | needs | tier |
|---|---|---|---|
| syntax highlighting | `ibCodeEditor::HighlightSyntaxAndCalculateFoldLevel` over the lexeme stream (`SetLexer(wxSTC_LEX_CONTAINER)` — OES colours it itself) | token class only | **lexemes** |
| folding | the same pass — `m_fp.RecalcFoldLevel()`, fed by `KEYWORD` lexems | nesting, not meaning | **lexemes** |
| symbol list (functions / procedures / vars / lambdas) | `codeEditorParser.cpp` → `ibModuleElement{name, kind, lineStart, lineEnd}` | declaration nodes + their spans | **syntax tree** |
| LINQ / query constructor (planned) | — | tree + render back to text | **syntax tree** |
| IntelliSense | `codeEditorInterpreter.cpp` (2 816 lines) | tree + resolved values | **semantic model** |
| SQL pushdown | `lambdaQueryAst.cpp` (349 lines) | tree + resolved values | **semantic model** |
| bytecode | `compileCode.cpp` | tree + emission | **semantic model** |

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
