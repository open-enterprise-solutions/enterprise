# ibValue audit — footprint & refactor

> **Status:** Phase 0 (probe) + Phase 1 (footprint repack) + **Phase 2
> (tagged-union collapse via `ibString`)** landed in working tree (builds
> clean Debug|x86). Phase 3 (wxObject removal, header split) is planned,
> not started.
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

## Measured (x64, VS struct layout)

| Build state | sizeof | align | note |
|---|---|---|---|
| pre-Phase 1 | 80 | 8 | `m_refCount` in the tail (word + pad), `m_typeClass` = int |
| post-Phase 1 | 72 | 8 | enum→1B + `m_refCount` packed with the scalars at offset 16 |
| **post-Phase 2** | **landed** | 8 | `m_sData` (wxString) removed; TYPE_STRING now an `ibString*` folded **into the union** + `ibNumber` stays by-value. Debug|x86 probe: **ibValue 96 → 40** ([[project_value_footprint_audit]]). Re-run `test_value.cpp::SizeofReport` for the live x64 figure. |
| Phase 3 (drop `m_refData`) | ~48 | 8 | projected |

Dominant member: `m_sData` (wxString = `std::wstring`) = 32 of 72 on x64.

## Layout (x64; figures below were the x86 estimate)

`ibValue : public wxObject`, with own intrusive refcount.

| Member | ~bytes | Note |
|---|---|---|
| vptr (wxObject) | 4 | needed — ibValue has its own virtuals |
| `wxObject::m_refData` | 4 | **dead** — ibValue uses its own `m_refCount`, never wx COW |
| `m_typeClass` (enum) | 1 (was 4) | narrowed to `unsigned char` in Phase 1 |
| `m_bReadOnly` (bool) | 1 | |
| `m_refCount` (uint) | 4 | repacked next to the scalars in Phase 1 |
| `union {bool/date/ptr/ibString*}` | 8 | post-Phase 2: TYPE_STRING is `ibString* m_pStr` here, aliased with `m_pRef` |
| ~~`m_sData` (wxString)~~ | ~~28~~ | **removed in Phase 2** — string payload is now the pooled `ibString*` in the union |
| `m_fData` (ibNumber) | 8 | present even for non-numbers; stays by-value (8B, sharing the union would reinstate the random-delete bug) |

`wxUSE_STL_BASED_WXSTRING = 1`, `wxStringImpl = wxStdString` → `wxString`
is a `std::wstring` wrapper (~28 x86 / ~32 x64, larger in Debug due to
MSVC iterator-debug). It is the single dominant member.

**Core waste:** the three payload stores — `union (8) + wxString (28) +
ibNumber (8) = 44 bytes` — are all present simultaneously, though a value
is exactly one type at a time. ~36 of ~64 bytes is "storage for the other
types."

### The probe

`tests/test_value.cpp::ValueTest.SizeofReport` prints
`sizeof(ibValue / ibNumber / wxString)` at run time and records them as
test properties. It is informational (always `SUCCEED()`), kept across all
phases so each phase's delta is measurable. Run the gtest suite and read
the `[ footprint ]` line.

---

## Blast radius — direct member access (measured, `src/engine`)

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
- **F6 — header bloat (low priority; do NOT un-nest).** `ibValueMethodHelper`
  + ~40 `AppendProp/Func` overloads are inline in `value.h`; every TU
  including `value.h` re-parses them. Per-class, not per-instance — zero
  footprint. **Keep it a nested type of `ibValue` on purpose:** the
  nesting plus the population pattern (instances handed out via
  `GetPMethods()`, populated only from inside subclasses' `PrepareNames()`
  overrides) is the deliberate "only the value family touches this" signal.
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
| 3 | F3 (drop wxObject / wxRTTI → CLSID registry), F6 (header split) | high blast radius | planned |

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
exact — the same reason 1C uses UTF-16 internally. `std::string`/UTF-8 was
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
