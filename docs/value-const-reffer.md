# `TYPE_CONST_REFFER` — non-owning read-only references in `ibValue`

## The problem it solves

`ibValue::operator=` has overloads for `bool`, `ibValue*`, and (now) `const ibValue*`.
After the const-meta refactor, accessors like `GetMetaObject()` and members like
`m_metaObject` are `const ibValueMetaObject*`. Code such as:

```cpp
pvarRetValue = m_metaObject;        // const ibValueMetaObject*
pvarRetValue = GetMetaObject();     // const ibValueMetaObject*
```

used to compile by silently selecting `operator=(bool)` — a `const` pointer
converts to `bool` (non-null → `true`), while `operator=(ibValue*)` rejects it
(const → non-const is ill-formed). The result: the value became a **Boolean
`true`** instead of the object, so at runtime `Metadata.<member>` resolved to
nothing ("there is nothing there").

## The design

A new value type `TYPE_CONST_REFFER = 101` (right after `TYPE_REFFER = 100`,
`backend_core.h`) plus a `const ibValue* m_pConstRef` member in the `ibValue`
union (aliases `m_pRef` — same 8 bytes). `operator=(const ibValue*)` stores the
pointer as `TYPE_CONST_REFFER`, sets `m_bReadOnly = true`, and **does not**
`IncrRef`.

Semantics: a **non-owning, read-only** reference to an object the value does
**not** own — typically a `const ibValueMetaObject*` owned by the metadata tree.

| Concern | `TYPE_REFFER` (owned) | `TYPE_CONST_REFFER` (non-owned) |
|---|---|---|
| Ref-count | IncrRef / DecrRef | **never** touches the counter |
| `Reset()` / dtor | may `delete` at refcount 0 | **never** deletes (tree owns it) |
| `Reset()` write-denied throw | n/a | **excluded** — slot stays reassignable |
| Read (props/methods/compare/cast/GetRef) | delegates to object | delegates to object (same union pointer) |
| Write the object (`SetPropVal`/`SetType`) | delegates | **blocked** (Error + Debug assert) |
| Write the slot (`SetValue`/`SetBoolean`/…) | per type | reassigns the slot (object untouched) |

## Why the union pointer is "safe but sharp"

`m_pConstRef` and `m_pRef` are the **same memory** (union); they differ only in
the const-ness of the access type. This is deliberate — `ibValue` already
type-puns the union (`m_pStr` aliases `m_pRef`). The cost: the compiler's
const-guarantee is **gone** — what you stored as `m_pConstRef` can be read as
`m_pRef` and mutated, and the compiler won't catch it.

Protection therefore lives at **runtime**, not in the type system:
- Read paths use `IsReference()` (true for both ref types) and delegate.
- Object-write paths check `IsConstReference()` and refuse:
  - `SetPropVal` / `SetType` → graceful `ibBackendException` (script-facing) plus
    a `wxASSERT(!IsConstReference())` that fires loudly in Debug to catch a future
    caller that forgets the guard.
- Lifetime paths (`Reset`, dtor, Copy, Move, `CopyValue`, `MoveValue`) special-case
  `TYPE_CONST_REFFER` to copy the pointer weakly and never ref-count/delete.

**Rule for new code:** any new method that mutates the referenced object through
`m_pRef` must guard with `IsConstReference()`. Any new read switch over
`m_typeClass` that has a `TYPE_REFFER` case must also accept `TYPE_CONST_REFFER`
(use the `IsReference()` helper or a `case TYPE_CONST_REFFER:` fallthrough), or a
const-ref silently degrades to `TYPE_EMPTY` / empty result.

## Tests

`enterprise/tests/test_value.cpp`, suite `ValueConstRef`: the const-ptr-binds-
reference-not-boolean trap, predicates, read delegation, no-delete-of-non-owned,
slot reassignability, and weak read-only copy.
