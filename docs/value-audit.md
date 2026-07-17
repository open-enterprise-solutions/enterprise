# ibValue audit — footprint & refactor

> **Status:** Phase 0 (probe) + Phase 1 (repack) + Phase 2 (tagged-union
> collapse via `ibString`) + **Phase 3 (wxObject / wxRTTI removal → `typeid`
> registry)** all LANDED (builds clean Debug|x86). Phase 4 (`ibNumber` into the
> union) analysed and **rejected by default** — see below. F6 header split
> deferred / maybe never.
>
> Scope note: `ibValue` is the engine's universal value type *by design* —
> script values, metaobjects, functions, iterators all derive from it. This
> audit does **not** challenge that philosophy. It targets (a) per-instance
> memory footprint and (b) the fragility surface that the universal base
> exposes, without flattening the hierarchy.
>
> Source: `src/engine/backend/compiler/value.{h,cpp}`,
> `src/engine/backend/backend_core.h` (`ibValueTypes`),
> `src/engine/backend/fstring.h` (the `ibString` introduced by Phase 2),
> `src/engine/backend/fnumber.{h,cpp}` (`ibNumber` — renamed from the old
> `number.{h,cpp}` so the two compact value types sit side by side; the
> `.cpp` also regained high-precision transcendentals — see
> [[reference_ibnumber_transcendental_precision]]). Related memory:
> [[feedback_no_base_field_shadow]], [[project_value_dispatch_via_virtual]],
> [[project_value_footprint_audit]],
> [[reference_ibvalue_no_union_for_nontrivial.md]].

---

## Measured (Debug|x86 probe — `test_value.cpp::SizeofReport`)

| Build state | sizeof | note |
|---|---|---|
| pre-Phase 1 | 96 | `m_sData` (wxString) + `m_refCount` in the tail (word + pad), `m_typeClass` = int |
| post-Phase 1 | ~88 | enum→1B + `m_refCount` packed with the scalars (Debug-x86 padding eats most of the narrowing) |
| post-Phase 2 | ~56 | `m_sData` (wxString) removed; TYPE_STRING now an `ibString*` folded **into the union**; `ibNumber` stays by-value |
| **post-Phase 3 (final)** | **40** | dead `wxObject::m_refData` gone, `ibValue` no longer derives `wxObject` ([[project_value_footprint_audit]]) |

Re-run `test_value.cpp::SizeofReport` for the live x64 figure (smaller — no
Debug iterator-debug bloat in the now-pointer string member).

## Final layout (post-Phase 3)

`ibValue` (no base — `wxObject` dropped in Phase 3), own intrusive
`std::atomic<unsigned int> m_refCount`, own vptr (ibValue has its own virtuals).

| Member | ~bytes (x64) | Note |
|---|---|---|
| vptr | 8 | ibValue has its own virtuals |
| `m_typeClass` (`ibValueTypes : unsigned char`) | 1 | narrowed in Phase 1 |
| `m_bReadOnly` (bool) | 1 | |
| `m_refCount` (`std::atomic<uint>`) | 4 | repacked next to the 1-byte scalars (fills the hole before the 8-aligned union) |
| `union {bool/date/ibValue*/const ibValue*/ibString*}` | 8 | active member selected by `m_typeClass`; `m_pConstRef`/`m_pStr` alias `m_pRef` |
| `m_fData` (ibNumber) | 8 | present even for non-numbers; stays by-value (8B; sharing the union would reinstate the random-delete bug — see Phase 4) |

`wxUSE_STL_BASED_WXSTRING = 1`, `wxStringImpl = wxStdString` → `wxString` is a
`std::wstring` wrapper. Pre-Phase-2 it was the single dominant member (inline
~32B x64); it is now a pooled `ibString*` (8B in the union), so a Number/Bool
value no longer drags a string buffer.

**Historical waste (pre-Phase 2):** the three payload stores —
`union (8) + wxString (~32) + ibNumber (8)` — were all present simultaneously
though a value is exactly one type at a time. Phase 2 collapsed the string into
the union; Phase 4 (folding `ibNumber` too) is rejected by default.

### The probe

`tests/test_value.cpp::ValueTest.SizeofReport` prints
`sizeof(ibValue / ibNumber / wxString)` at run time and records them as
test properties. It is informational (always `SUCCEED()`), kept across all
phases so each phase's delta is measurable. Run the gtest suite and read
the `[ footprint ]` line.

---

## Blast radius — direct member access (pre-Phase-2 snapshot, `src/engine`)

> Historical — the numbers below were the input to the Phase 2 write-audit.
> `m_sData` no longer exists (replaced by the union `ibString* m_pStr`, accessed
> only via `GetString`/`SetString`); the `m_fData` direct-write sites were the
> ones routed through a constructed active member during Phase 2.


| Member | occurrences | concentration | outside core `value.*` |
|---|---|---|---|
| `m_pRef` | 156 | value.cpp **108**, procUnitValues.h 15, value_ptr.h 19 | ~15 |
| `m_fData` | 75 | value.cpp 23, **procUnit.cpp 25**, valueOLE.cpp 12 | ~50 |
| `m_dData` / `m_bData` | 82 | value.cpp 27, **procUnit.cpp 35** | ~20 |
| `m_sData` | 36 | value.cpp 14 | ~20 |
| `: public ibValue` | 47 across 28 files | — | (+ the metaobject tree above) |

Most direct access is **reads guarded by a type check** (safe under a
union). The dangerous sites for Phase 2 are direct **writes** to
`m_sData` / `m_fData` outside `value.cpp` (procUnit.cpp, valueOLE.cpp) —
those must go through a constructed active member.

---

## Findings (ranked by payoff)

- **F1 — payload not unified (biggest reducible cost).** Collapse
  `union + wxString + ibNumber` into one tagged store with manual lifetime
  → footprint ~64 → ~40 (≈24 B/value, Debug x86). Philosophy-neutral:
  storage only, the universal API is unchanged. Enabler: `Copy`/`Move`
  already switch on type, so the transition discipline is centralised.
  Cost: `Reset` must destroy the active non-trivial member (see F4); audit
  the writes in the blast-radius table. **→ Phase 2.**
- **F2 — narrow the tag + repack.** `ibValueTypes` → `unsigned char`
  (max enumerator 204 fits; AOT already serialises it as `uint8_t`, so
  binary-compatible). Move `m_refCount` next to the 1-byte scalars to fill
  the alignment hole before the 8-aligned union, reclaiming the tail word.
  Narrowing alone saves nothing (padding eats it); the repack is what wins.
  **→ Phase 1 (landed).**
- **F3 — `wxObject::m_refData` is dead (4/8 B).** Removing it means
  dropping `wxObject` entirely and replacing wxRTTI
  (`wxDECLARE_DYNAMIC_CLASS` / `CLASSINFO` / `wxClassInfo` factory) with the
  existing CLSID ctor registry (`ibCtorAbstractType`). Cleanest structural
  win, widest blast radius (every value subclass uses
  `wxDECLARE/IMPLEMENT_DYNAMIC_CLASS`). **→ Phase 3, own arc.**
- **F4 — `Reset()` doesn't free string/number buffers.** Today the buffer
  lingers until reassignment / dtor. Not a correctness bug (post-Reset type
  is `TYPE_EMPTY`, stale data is unreachable through `GetString()`).
  Eager free-on-Reset risks hot-path churn (string reuse in loops), so it
  is **not** done standalone. Under the union (F1) destroying the active
  member is mandatory and correct-by-construction. **→ Phase 2.**
- **F5 — fragility surface (the universal-base hazard).** Public mutable
  payload members + deep inheritance (47+ direct subclasses + the
  metaobject tree). [[feedback_no_base_field_shadow]] is the symptom.
  Structural defence = encapsulate payload behind the existing accessors
  (`GetString` / `GetNumber` / `GetRef`), which F1 forces anyway. Do **not**
  flatten the hierarchy — close the public-field shadow vector instead.
  **→ folds into Phase 2.**
- **F6 — header bloat (low priority; do NOT un-nest).** `ibValue::ibMemberTable`
  + its ~40 `AppendProp` / `AppendProc` / `AppendMethod` overloads are inline in
  `value.h`; every TU including `value.h` re-parses them. Per-class, not
  per-instance — zero footprint. **Keep it a nested type of `ibValue` on
  purpose:** the nesting plus the population pattern (instances handed out via
  `GetPMethods()` → virtual `DoGetPMethods()`, populated only from inside
  subclasses' `PrepareNames()` overrides) is the deliberate "only the value
  family touches this" signal.
  The *only* separable concern is the inline bodies — if build time ever
  bites, out-line the method bodies to `value.cpp`; that does not change
  the nesting or the access semantics. Otherwise leave as is. **→ defer /
  maybe never.**

---

## Plan

| Phase | Content | Risk | Status |
|---|---|---|---|
| 0 | sizeof probe in test_value.cpp | none | landed |
| 1 | F2 (narrow `ibValueTypes`, repack `m_refCount` — now `std::atomic`) | low | landed |
| 2 | F1 tagged-union (`ibString*` in union) + F4 (Reset frees active) + F5 (encapsulate) | medium | **landed** |
| 3 | F3 (drop wxObject / wxRTTI → **typeid** registry) | high blast radius | **landed** (F6 header split still deferred) |

**Verified (working tree):** the two script suites pass —
`tests/scripts/test_string_suite.txt` (native string functions return as
values, no "use procedure as function"; coercion e.g. `StrLen(12345) → 5`)
and `tests/scripts/test_math_suite.txt` (`Sqrt(2)`/`Ln(2)` carry >16 digits,
not double; round-trip `Sqrt(x)² == x`). Both also exercise the `m_bCodeRet`
path that lets these runtime functions be called as functions. Unit tests:
`tests/test_string.cpp`, `tests/bench_string.cpp`, plus the `ibNumber`
transcendental coverage in `tests/test_number.cpp`.

**Full gtest suite revived — 246/246 green.** The Google Test target had not
built or run for a long time; bringing it up on Windows (CMake) took: enabling
the Firebird/PostgreSQL drivers (dynamic client load, no link-time dependency),
exporting the AOT (`SerializeAOT`/`DeserializeAOT`) and `operator<<(ibGuid)`
symbols the tests link against, and refreshing tests stale against the Phase-2
headers (`m_sData`→`GetString`, nested `ibValueModelTableBase::ibValueTableRow`,
`CallAsFunc` variadic, the FNV `clsid` contract, the compact-zero `ibNumber`
buffer, a global VES code-style test environment). Running end to end it
surfaced — and we fixed — real value-lifecycle bugs unrelated to footprint:

- `ibProcUnit::GetPropVal` returned a `TYPE_REFFER` to a local-var array slot
  (refcount 0); the caller's REFFER `DecrRef`'d it to 0 and `delete`d a non-heap
  array element → heap corruption. Now dereferences (value copy).
- Self-recursive calls were mis-classified by a case-sensitive name guard
  (source-case `m_strCurFuncName` vs upper-cased call name) → immediate path →
  `OPER_CALL` emitted with frame size 0 → undersized frame → AV. Guard is now
  case-insensitive, so the self-call defers to the finalize trampoline where the
  local count is settled.
- AOT `ReadConstValue` set the readonly flag before `SetString`; post-Phase-2
  `SetString` Resets through the readonly guard and threw for every string
  const. Readonly is now set after the payload (`byteCodeAOT.cpp`).
- `ibNumber::Pow` took the integer fast-path for a fractional exponent
  (`ToInt` success ≠ integer) → `2^0.5 == 1`; guarded with `n == ibNumber(k)`.

**Phase 2 approach (LANDED): `ibString` — the `ibNumber` playbook for the
string member.** Rather than a bare tagged-union of `wxString` + `ibNumber`
(which leaves the ~56-byte `wxString` floor in every value and keeps the wx
dependency), introduce a self-contained compact string `ibString` — the
exact analog of `ibNumber`: tagged storage, inline (SSO) tier for short
strings + heap tier for long, convert-to/from `wxString`/std at the
boundaries, own minimal methods. This serves footprint **and** the
backend wxBase→std de-wx goal **and** consistency with `ibNumber`.

Why the surface is small (de-risked): the codebase already reads the string
through `GetString() → wxString` and does all rich manipulation
(`Format`/`Mid`/…) on that copy — `GetString()` is already the conversion
boundary. The write audit showed the *stored* member only ever sees
`=` / `+` (concat) / `Clear` / compare. So `ibString` is a storage+convert
type, not a string library — rich ops convert out to `wxString` at the call
site (exactly as `ibNumber` routes `Pow`/`Sqrt` through `double`).

Type decision (made): `ibString` is a zero-overhead wrapper over
**`std::wstring`** (not a hand-rolled tagged type, not UTF-8). `std::wstring`
is exactly what `wxString` already wraps here (`wxUSE_STL_BASED_WXSTRING`),
so the swap sheds `wxString`'s ~24 bytes of overhead (`sizeof` 56 → 32 →
ibValue 96 → ~56) with **zero** change to per-char cost or index semantics,
and `GetString()` becomes a near-free same-width copy.

Encoding decided — **wchar in memory, UTF-8 on the wire/DB.** The runtime
string functions (`StrLen`/`Left`/`Right`/`Mid`/`Find`, `systemManagerFunc.cpp`)
index by character position via `GetString() → wxString` (wxChar units).
UTF-8 storage would make each an O(n) transcode; wchar keeps them O(1) and
exact — the same reason Windows and Java use UTF-16 internally. `std::string`/UTF-8 was
considered and rejected for this. Rejected refs: FastString (fixed-cap,
truncates), SuperString (rope+COW+GC, v0.0.1 — too heavy a dependency under
the core type).

`backend/fstring.h` (**wired in**). `ibString` is `sizeof == sizeof(std::wstring)`
(the allocator folds away via EBO) and the rich API turned out to be worth
keeping after all: the runtime string functions in `systemManagerFunc.cpp`
(`StrLen` / `Left` / `Right` / `Mid` / `Find` / `TrimL`/`R`/`All` /
`StrReplace`) were migrated to operate **natively on `ibString`** via the new
`ibValue::GetString(ibString& scratch)` zero-copy accessor — no per-call
`wxString` round-trip. `wxString` remains the boundary type only (implicit
`operator wxString` / ctor for serialization, metadata lookups, wx APIs).
Allocator: a per-thread, size-classed free-list (`ibFStringPool`) — short
strings stay in `std::wstring`'s SSO and never allocate; longer ones reuse a
cached block. New native overloads on `ibValue` carry the moves:
`ibValue(ibString&&)`, `operator=(ibString&&)`, `SetString(ibString&&)`.

The swap order taken: build test-first (`test_string.cpp` + script suites),
swap `ibValue::m_sData` → `ibString* m_pStr` folded into the union, fold the
union, migrate the hot string call sites. Done.

Reads stay on `GetString` / `GetNumber` / `GetRef`; type transitions go
through the existing `Reset`-then-set path (`LetValue` / `SetTypeNumber` in
`procUnitValues.h` are the template). Typed-delta hot-path writes stay raw —
their destination slot's active union member is type-stable, so no
per-op branch.

---

## Phase 3 — drop `wxObject` / `wxRTTI` (final design: **typeid registry**)

> **Status: LANDED.** `ibValue` no longer derives `wxObject`; the hand-rolled
> wx-RTTI (originally MFC `CObject`/`DECLARE_DYNCREATE`/`CRuntimeClass`,
> transliterated to `wxObject`/`wxDECLARE_DYNAMIC_CLASS`/`wxClassInfo` during the
> MFC→wx port — see VTOOLS.RU 2002–2003 ancestry) is gone from the value tree.
> Full solution builds clean Debug|x86 (all 10 projects: backend/frontend/
> wfrontend + enterprise/designer/daemon/codeRunner/launcher/
> wenterprise-server/simplePlugin). This supersedes the earlier "per-class
> `GetClassType()` macro / CRTP" sketch — see *Design path* below.
>
> **Execution notes / gotchas (for the next de-wx arc):**
> - The `wxDECLARE_*_CLASS` macros expand starting with `public:`. Deleting them
>   wholesale dropped that access specifier and sent the members after them into
>   `private` (≈3500 C2248). Fix: re-insert `public:` after the opening brace of
>   every ibValue-derived class (the macro was always first-in-class per wx
>   convention). **Lesson: replace the macro with `public:`, don't just delete.**
> - `wxDECLARE_DYNAMIC_CLASS_NO_COPY` / `_NO_ASSIGN` variants don't match a
>   `..._CLASS\b` regex (suffix) — two survived (`ibValueOLE`,
>   `ibValueTypeDescription`) and only surfaced at link time (their `wxIMPLEMENT`
>   was removed, the `wxDECLARE` wasn't → unresolved `GetClassInfo`).
> - `wxDynamicCast(x, T)` silently `const_cast`s; native `dynamic_cast<T*>`
>   does not, and fails on a non-wxObject `T`. Frontend/designer had ~28 such
>   casts on ibValue targets → rewritten to `dynamic_cast`, with
>   const-correctness restored at the few `GetMetaObject()` (const) call sites
>   (`const ibValueMetaObjectRecordDataRef*`, no `const_cast`).
> - Incremental builds left **stale .obj** referencing the removed
>   `GetClassInfo`/`GetTypeIDByRef(wxClassInfo*)` symbols — a clean Rebuild was
>   needed to flush them before the real (2) link errors showed.
>
> **Follow-up — LANDED (2026-06-02): `ibCtorRegistry`.** The registry's linear
> scans are gone. `backend/ctorRegistry.h` introduces a reusable templated
> `ibCtorRegistry<T>` — single owner of the ctor pointers plus the lookup indices,
> mutated only through `Register`/`Unregister` (so the indices cannot drift). The
> **hot keys are O(1) hash indices** (`unordered_map<ibClassID, T*>` and
> `unordered_map<type_index, T*>` — `clsid` for `CreateObjectRef`/`IsRegisterCtor`/
> `GetVTByID`, `type_info` for live-object self-id in `GetClassType`). **Name
> lookup stays linear on purpose** — it is compile-time / low-frequency and, being
> case-insensitive, can't ride the case-sensitive `clsid` hash. Memory cost is one
> extra small index (not the three-map sprawl first sketched). The value factory
> (`valueFactory.cpp`) adopted it first; the per-metadata factories
> (`ibMetaData` base + `ibMetaDataDataProcessor` + `ibMetaDataReport`, formerly raw
> `std::find_if` over their own `m_factoryCtors` with an `activeMetaData` fallback)
> reuse the same class — hot `clsid` O(1), the metadata-specific `(metaValue,
> refType)` key kept linear via `ForEach`. This also **closes the Phase-3
> constant-factor regression**: post-typeid `GetTypeIDByRef` had been an O(n)
> *string* compare (MSVC `type_info::operator==` compares decorated names); it is
> now an O(1) `type_index` hash — better than even the original `wxClassInfo`
> pointer scan.
>
> **Also hardened (2026-06-02):** `GetTypeIDByRef(const ibValue*)` no longer
> re-delegates to `objectRef->GetClassType()` for an unrecognised object tag (that
> re-entered `GetClassType ↔ GetTypeIDByRef` → infinite recursion). The whitelist
> is now a `switch`; the `default` is a loud `wxFAIL_MSG` + `return 0`. And the
> `operator=(ibValueTypes)` leak/ineffective-null on a reused reffer/string slot
> (see [[reference_value_settype_operator_trap]]) was closed at its two live call
> sites (`metaAttributeObjectQuery.cpp`, `case ibFieldTypes_Null`) by switching to
> `retValue = ibValue(ibValueTypes::TYPE_NULL)` (fresh ctor → proper release).

### Where do we actually need RTTI?

The whole `wxDECLARE_DYNAMIC_CLASS` apparatus (168 sites across 77 backend
files) exists to answer exactly **one** question: *what is the type-id of this
live value object?* There are only two runtime-type needs in the value system,
and the C++ language already provides both for free:

1. **Downcast** — "is this `ibValue*` really an `ibValueArray`? give me the
   typed pointer." Served by `dynamic_cast` in `CastValue` (`value_cast.h:18`).
   This already uses C++ RTTI, **not** wxRTTI — zero work, untouched.
2. **Self-identity** — "what `clsid` is this live value?" Served by the
   polymorphic `ibValue::GetClassType()` (`value.cpp:921`). For fixed value
   classes it currently routes through `wxObject::GetClassInfo()` →
   `wxClassInfo*` → registry. `wxClassInfo` is a **hand-rolled `std::type_info`**;
   the compiler already emits a `std::type_info` per polymorphic class via the
   vtable, so `typeid(*this)` is the exact drop-in.

`GetClassType()` dispatches in three tiers (the structure is correct, not a
wart):

```cpp
ibClassID ibValue::GetClassType() const {
    if (m_pRef && IsReference())   return m_pRef->GetClassType();   // 1: reference → delegate
    if (m_typeClass < TYPE_REFFER) return GetIDByVT(m_typeClass);   // 2: primitive → tag→id (no RTTI)
    return GetTypeIDByRef(this);                                    // 3: object → type-id → registry
}
```

- Tier 2 (primitives): `m_typeClass` switch (`GetIDByVT`). One C++ class
  (`ibValue`) registered 6× with different clsids — not a per-class identity,
  the tag *is* the identity. Untouched.
- Tier 3 (objects): the only place a real C++ type-id is needed. Swap
  `GetClassInfo()`→`typeid(*this)` **inside `GetTypeIDByRef`**; the registry
  key changes `wxClassInfo*` → `std::type_info` (`type_index` for an O(1) map).
- Category B (metaobjects: `ibValueRecordDataObject`, …) **override**
  `GetClassType()` to return their metaobject-derived clsid (`O_<metaID>`,
  `R_<metaID>`, … via `clsFactory`, `commonObject.cpp:1478`). One C++ class
  ↔ many clsids, so `typeid` can't and shouldn't identify them — they never
  reach Tier 3.

### What gets thrown out vs what stays

**Out (the entire hand-rolled wxRTTI surface):** `: public wxObject` on
`ibValue`; all 168 `wxDECLARE_DYNAMIC_CLASS` / `wxIMPLEMENT_DYNAMIC_CLASS`;
`wxClassInfo*` storage + `GetClassInfo()` overrides; `GetAvailableCtor(wxClassInfo*)`,
`GetTypeIDByRef(wxClassInfo*)`, `CreateObjectRef(wxClassInfo*)`; `CLASSINFO(T)`
and `wxDynamicCast` in the value family (~20 + ~7 backend sites → `typeid(T)` /
`dynamic_cast`). The 168 macros are **deleted, not replaced** — `typeid` needs
no declaration.

**Stays (irreducible, both compiler built-ins, zero code):** `dynamic_cast`
(downcast) and `typeid(*this)` (self-id, one line). **Not RTTI but stays:** the
`clsid ↔ name ↔ factory` registry (a semantic OES type table, now keyed by
`std::type_index`), `GetIDByVT` (primitive tag switch), Category B's own
`GetClassType()`.

Footprint: x64 ~48→~40, x86 40→36 (dead `wxObject::m_refData` gone); the vptr
stays (`ibValue` has its own virtuals). The real win is structural — the
universal base no longer depends on wxRTTI (a step in the backend wxBase→std
goal), the `GetClassType ↔ GetClassInfo` recursion hazard is gone.

### Design path — why not a per-class macro / CRTP / external lib

The arc walked through and rejected several heavier designs:

- **Per-class `IB_DECLARE_VALUE_RTTI` macro** (each class returns its own
  clsid): works, but is an *insertion* into 168 sites and needs each class's
  exact registration tag copied — a (closeable) clsid-drift risk. The
  `typeid` path keeps the existing "registry resolves the mapping"
  architecture and turns the 168-site migration into **deletions**.
- **CRTP helper** (`ibValueRtti<Derived, Base>`): gives a free `using Self`,
  fine for *new* leaf classes, but rewrites the inheritance line of every
  class (layout-affecting) and needs a `Base` template param for the chains /
  multiple-inheritance that pervade the tree (`ibValueModel : ibValue,
  ibActionDataObject, ibTabularObject`). Strictly worse than a macro for the
  retrofit; kept on the table only as optional ergonomics for new code.
- **`royvandam/rtti`** (intrusive vptr type-id, FNV1a of the C++ type name):
  its id is hashed from the *C++* name, but our `clsid` is hashed from the
  *OES* name (`ib_clsid_hash("VL_ARR")`) and is **serialized to disk/DB** — we
  can't change the wire id. It also pushes us off the free, MI-safe
  `dynamic_cast` onto an intrusive walker that needs full base lists at every
  node, and parses `__PRETTY_FUNCTION__` (MSVC `__FUNCSIG__` differs). Net
  negative.
- **`veselink1/refl-cpp`** (compile-time *member* reflection): solves a
  different problem (serialization / property-table generation), has **no**
  runtime "what type is this base pointer" mechanism at all. Irrelevant to
  wxObject removal; parked as a speculative future for auto-generating
  `PrepareNames()` (conflicts with F6 "don't un-nest", and the prop model is
  not 1:1 with C++ fields).

### Pilot (additive, non-breaking, self-validating)

Rather than start by deleting macros, the pilot **proves the typeid path
reproduces the wxClassInfo clsid for every live object**, with `wxObject` and
all 168 macros left in place as a safety net:

- `ibCtorValueTypeBase` gains a `const std::type_info* m_typeInfo` alongside
  the existing `m_classInfo` (populated from `typeid(T)` in the ctor
  templates); new `GetAvailableCtor(const std::type_info&)` in `valueFactory.cpp`.
- `GetTypeIDByRef(const ibValue*)` (only caller: base `GetClassType`,
  `value.cpp:927` — Category B never reaches it) computes the clsid **both**
  ways under `#ifdef DEBUG` and `wxASSERT`s they match, logging any mismatch.
- Build `Debug|x86`, run a session: if the assert never fires across the whole
  value tree (`ibValueArray`/`ibValueMap`/Table/Enum/Function/Iterator/…),
  `wxClassInfo` is proven redundant and the big-bang (delete macros, drop
  `wxObject`, flip `GetTypeIDByRef` to the typeid value, retire the
  `wxClassInfo` overloads) is mechanical cleanup.

The big-bang is necessarily one commit (the moment `ibValue` stops being
`wxObject`, every `wxDECLARE_DYNAMIC_CLASS` in the subtree fails to compile),
so the dual-compute pilot is the de-risking step that precedes it.

### ⚠️ ABI — `ibValue` layout changed (the headline cross-DLL fact)

Dropping `wxObject` **changed `ibValue`'s binary layout**: the dead
`wxObject::m_refData` pointer is gone, so the object shrinks by one pointer
(−4 bytes x86 / −8 x64) and **every `ibValue` member offset shifts**
(`[vptr][m_refData][m_typeClass…]` → `[vptr][m_typeClass…]`). This is a
**binary-incompatible** change to the universal value type. Consequence:

- **Every** TU / DLL / plugin that touches `ibValue` MUST be rebuilt against
  the new header. A partial or incremental build, a stale `.obj`, or — the
  trap that actually bit at landing — **a running OES process holding the
  `bin\` DLLs locked so they can't be overwritten**, leaves a mix of old- and
  new-layout binaries in one process.
- The symptom of such a mismatch is **silent memory misread, not a crash or
  link error**: `m_typeClass` / `m_pRef` are read at the wrong offset, so
  `IsReference()` lies and a perfectly valid object reads as empty →
  e.g. *"a variable is not an aggregate object"* at runtime for a call like
  `ShowCommonForm(...)` whose context slot looked unbound. (This masqueraded
  as a binder/module-manager regression; it was an ABI mismatch.)
- **Landing procedure / lesson:** kill every running OES process (so nothing
  locks `bin\…\*.dll`, incl. the vendored `_fb` Firebird plugins), do a full
  clean `/t:Rebuild` (never trust incremental for a layout change — it leaves
  stale objects), then run only the freshly-built binaries from `bin\Win32\Debug`.
  In alpha this is free; once a stable plugin ABI is declared, this layout is
  what gets frozen (one more reason to have removed `wxObject` *now*).

### Build requirement — RTTI + cross-DLL typeinfo (secondary nuance)

The value system now depends on C++ RTTI being enabled (`/GR` on MSVC,
default `-frtti` on GCC/Clang) — `typeid(*this)` for self-id and `dynamic_cast`
for downcasting. This is a non-issue on desktop (RTTI is only ever disabled in
size-constrained embedded/game builds, which OES is not), but it is now a hard
build invariant: **never build the value-using projects with `/GR-` /
`-fno-rtti`.**

Cross-module identity resolution differs by platform, and both are fine *as
currently configured* — record this so a future cross-platform build doesn't
trip on it:

- **MSVC / Windows (primary):** `type_info::operator==` compares the decorated
  name **by string** (cached), not by address. Cross-DLL `typeid` equality and
  `dynamic_cast` work with **no export gymnastics** — robust, slightly slower on
  a miss. Nothing to do.
- **Itanium ABI / Linux+macOS (GCC/Clang, the CMake path):** `type_info`
  equality is **by address**, and the dynamic linker merges typeinfo symbols
  across `.so` via vague/weak linkage. If `ibValue` (or a value subclass that is
  `dynamic_cast` across a library boundary) had its typeinfo hidden
  (`-fvisibility=hidden` without export), you'd get a duplicate `type_info` per
  `.so` → cross-boundary `dynamic_cast` / registry lookup silently returns
  null. **Mitigation is already in place:** `ibValue` is exported via
  `BACKEND_API` (dllexport / visibility=default), so its typeinfo travels
  correctly — the same mechanism wx itself relies on for cross-`.so` types.
  **Action:** when the CMake/Linux build is first exercised in anger, verify
  the value base and any cross-`.so`-cast value classes are not hidden. This is
  a general property of RTTI in shared libs (true before this change too), not a
  regression — just the one thing to keep in view.

---

## Phase 4 — `ibNumber` into the union (analysed → **don't, by default**)

The last reducible payload cost: `ibNumber m_fData` is a **separate by-value
member** (8 B) carried by *every* value regardless of type — the one piece of
the F1 "tagged union" that Phase 2 left out. Folding it **by value into the
union** (`union { bool; date; ibValue*; ibString*; ibNumber }`) would take
`ibValue` 32 → 24 (x86, −8). So the size win is real.

**It is NOT a clean win, and it was already tried — large numbers gave UB.**
`ibNumber` is 8 bytes with two tiers:

- **immediate** (small numbers): tag+exp+mantissa packed in a `uint64` —
  trivial, owns nothing, byte-copyable. In a union this is fine. (Small numbers
  worked.)
- **heap** (big numbers): those 8 bytes hold a pointer to a heap `BigImpl`. Now
  the union member **owns a resource** → non-trivial. Without manual union
  lifetime this is UB, and it **only manifests for big numbers** (the heap
  tier): overwrite the union with another member without `~ibNumber()` → leak;
  `Reset` treats the bytes as `m_pRef` and `delete`s the `BigImpl*` as an
  `ibValue*` → random-delete corruption; copy-ctor byte-copies the union without
  deep-copying `BigImpl` → double-free. **This is exactly why `ibNumber` was
  pulled out to a separate member** (current layout).

**Why `ibString*` survives the union but `ibNumber`-by-value does not:**
`ibString*` is a *pointer* in the union — the heap object lives behind it, the
union holds only an address (trivial to copy/null; type-guarded `delete`).
`ibNumber` by value puts the **resource-owner's bytes** in the union, so the
owned `BigImpl*` must be threaded through every union reinterpretation by hand
(placement-new + explicit `~ibNumber()` + proper copy/move on **every** type
transition: `Reset` / `Copy` / `Move` / `SetNumber` / `operator=`), with the
hot arithmetic path writing into a slot that must hold a live-constructed
`ibNumber`.

**Verdict:** the 8-byte win is re-opening a **known-UB surface** that empirically
bit at the heap tier. The current separate by-value member is the pragmatic,
safe choice. Do Phase 4 **only** if 8 B/value is genuinely critical *and* you
commit to the full C++11 non-trivial-union-member treatment focused on the heap
tier — otherwise leave it. (Rejected the pointer variant `ibNumber*`-in-union
outright: same 8 union bytes as by-value, zero size win, plus a heap alloc per
number.)

---

## Adjacent — the per-object value store (`ibRowValues`, LANDED 2026-06-02)

The footprint audit above is about a *single* `ibValue`. The next-order cost is the
**container that holds many of them per object**: every record object and table row
keeps its attribute values in `ibMetaValueArray` — historically
`typedef std::map<ibMetaID, ibValue>` (red-black tree). That map sat on the single
most-trodden runtime path: every `Object.Attribute` read/write goes
`GetPropVal`/`SetPropVal` → `GetValueByMetaID`/`SetValueByMetaID` →
`find(id)`/`at(id)`, and the same typedef also backs table-row columns
(`model.h`), composite keys (`uniqueKey.h`) and selector rows.

**Swap (drop-in behind the typedef):** `std::map` → `ibRowValues<Key,T>`
(`backend/rowValues.h`) — a sorted `std::vector<pair<Key,T>>` with a
std::map-compatible API (`find`/`at`/`[]`/`insert_or_assign`/`erase`/`count`/
`begin..end`/`==`/`<`) and the **same sorted iteration order** (so serialization /
unique-key order is unchanged). The alias is renamed `ibMetaValueArray` →
**`ibRowMetaValues`** = `ibRowValues<ibMetaID, ibValue>` (`backend_core.h`). Zero
call-site behavioural change; full Debug|x86 green + runtime-validated.

Why it wins (same philosophy as the ibValue audit, one level up):
- **Lookup:** same O(log n) but over **contiguous** memory (binary search, no
  pointer chasing) — a row's keys often fit 1–2 cache lines. For the small n here
  (≈5–50 attributes) a markedly better constant factor.
- **Allocation:** **one** vector block per object instead of a heap node **per
  attribute** — ~N× fewer malloc/free on every object load / query result.
- **RAM:** drops the ~32-byte RB-node header per value (contiguous `pair` ≈ 48 B
  vs map node ≈ 60 B + its own heap block) — ~30–40% less per attribute entry,
  less heap fragmentation, across millions of loaded records / table rows.
- **Especially visible in Debug** (MSVC): Debug punishes exactly what `std::map`
  does most — per-node `new`/`delete` through the slow CRT debug heap, checked
  iterators (`_ITERATOR_DEBUG_LEVEL=2`) validating every tree op, no inlining of
  the RB-tree helpers. The Release gap is smaller (inlining + fast heap).

Costs (documented in `rowValues.h`): mid-life insert is O(n) shift, but the key set
is **fixed per type** (built once after `clear()`, then only value-updated — no
hot-path inserts); `operator[]`/`insert` refs are invalidated by the next
structural mutation (no call site borrows a ref across an insert); the iterator
exposes a mutable key (nothing mutates it). API kept lowercase/std::map-shaped on
purpose (range-for needs `begin`/`end`; it is used like a map) — see
[[reference_flat_map_over_stdmap]]. Reusable sibling from the same arc:
`ibCtorRegistry` (the type-ctor registry above).

> **Wider memory strategy.** `ibFStringPool` (the per-thread size-classed
> free-list behind `ibString`, above) and `ibRowValues` here are the first two
> bricks of a memory subsystem, each isolated to its own type. Whether to lift
> that into a unified allocator seam (`IMemoryManager`), drop-in mimalloc, pools,
> a query-scope arena, or per-session memory accounting — and the motives that
> would justify each — is captured in [memory-allocator.md](memory-allocator.md).
> Short version: the cross-DLL *correctness* motive is already closed by `/MD`, so
> the seam is a placeholder-when-convenient, with machinery grown by motive
> (governance / debug-heap), never by precedent.
