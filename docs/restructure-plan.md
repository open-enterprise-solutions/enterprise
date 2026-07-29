# Code restructuring plan — grouping, declaration order, naming

> **Status: PARTIALLY APPLIED** (re-verified 2026-07-29). Wave B1 landed — `ibValuib*` is now
> `ibValueEnum*` (`system/systemManagerEnum.h`) and `AttributibRecordType` is now
> `AttributeRecordType`; so did B2's `ibDatatabaseParameterFirebird` → `ibDatabaseParameterFirebird`.
> All three grep to **zero** occurrences today. Waves A1, A2, the B2 remainder (`Sybsystem`,
> `ExpectDelimeter`) and Wave C are still outstanding and their line references still check out.
> Scope: **naming, declaration grouping and comments. The logic stays exactly as it is.**
> No behaviour change, no moved responsibilities, no "while I'm here" fixes.
> File/folder renames are a separate plan ([naming-plan.md](naming-plan.md)).
> Every count below was measured with grep on 2026-07-15.

---

## 1. The rule for this whole plan

**Logic as-is. Rename, regroup, document — nothing else.**

That is what makes it reviewable: every diff in this plan should be verifiable by reading,
and a full rebuild is a sufficient test. The moment a change alters behaviour it belongs in
a different commit and a different review.

Corollary: **a rename that changes a registry key or a serialised string is not a rename** —
it is a breaking change. See §2.

---

## 2. Off limits

| Thing | Why |
|---|---|
| CLSID keys (`"VL_ARR"`, `"EN_ITMO"`, `"PC_CLOSE"`) | the string is the clsid body ([script-value-types.md § 1.1](script-value-types.md)) |
| Script-facing names (`"Array"`, `"IndexingMode"`, `externalManagerDataProcessor`) | user API + registry key |
| `ibValue` / `ib*` prefix | the convention, not a defect |
| Vendored code (wxCharts, Firebird engine, sqlite3.c, quicklz, lzhuf) | upstream |
| Forked upstream headers (`gridext.h`, `datavgen.*`) | keep upstream attribution ([wx-fork.md](wx-fork.md)) |

---

## 3. Wave A — names that can be *wrong*, not just ugly

### A1. ⚠ `ibMetaDataTree` vs `ibMetadataTree`

```cpp
// designer/mainFrame/metaTree/treeConfiguration.h
:12   class ibMetaDataTree : public wxPanel, public ibBackendMetadataTree { … };  // the BASE
:95   class ibMetadataTree : public ibMetaDataTree { … };                         // the CONFIG tree
```

**Two classes, one letter's case apart, 83 lines apart in the same header.** A typo in
either direction compiles and silently resolves to the wrong class.

**Proposal:**

| From | To | Reason |
|---|---|---|
| `ibMetaDataTree` | `ibMetaTreeBase` | it is the panel + contract base, not "the tree" |
| `ibMetadataTree` | `ibConfigurationTree` | it edits the **configuration** — and it becomes symmetric with its siblings `ibDataReportTree` / `ibDataProcessorTree` ([metadata-tree.md § 2.1](metadata-tree.md)) |

This is the single highest-value rename in the tree. Do it **alone**, in its own commit.

### A2. `IsCellReadOnly` — a getter with a setter's signature

```cpp
bool IsCellReadOnly(int row, int col, bool isReadOnly = true) { return m_spreadsheetDesc.IsCellReadOnly(row, col); }
```

The third parameter is never used. Not a rename — a **signature** fix (drop the arg).
Callers passing it exist? Check before removing; if any do, they are asking for something
the function never did.

### A3. `GetValueBySourceHop` returns `false` silently

Base returns `false`; a source that forgets to override resolves nothing and a bound control
reads as unset ([source-object.md § 3](source-object.md)). **Not a rename** — proposal is a
one-line debug assert so the omission surfaces in Debug instead of at a user's desk. Include
here only because it is the same "make the wrong thing loud" theme; land separately.

---

## 4. Wave B — typos and broken names (cheap, safe)

### B1. ⚠ Broken class names — a search/replace that mangled its targets

Two families of identifiers are **corrupted**, not merely ugly. Both look like the same
accident: a replace of `e<Word>` → `ib<Word>` that hit the middle of longer identifiers.

**`ibValuib*` — `backend/system/systemManagerEnum.{h,cpp}`, 16 occurrences:**

```cpp
class ibValuibStatusMessage      : public ibValueEnumeration<ibStatusMessage>     { … };
class ibValuibQuestionMode       : public ibValueEnumeration<ibQuestionMode>      { … };
class ibValuibQuestionReturnCode : public ibValueEnumeration<ibQuestionReturnCode>{ … };
class ibValuibRoundMode          : public ibValueEnumeration<ibRoundMode>         { … };
```

Reconstruct it: `ibValueEnumStatusMessage` minus `eEnum` plus `ib` = `ibValuibStatusMessage`.
Their siblings elsewhere are named correctly — `ibValueEnumItemMode`,
`ibValueEnumIndexingMode`, `ibValueEnumComparisonKind`
([enumerations.md § 4](enumerations.md)) — so the intended names are not a guess:

| From | To |
|---|---|
| `ibValuibStatusMessage` | `ibValueEnumStatusMessage` |
| `ibValuibQuestionMode` | `ibValueEnumQuestionMode` |
| `ibValuibQuestionReturnCode` | `ibValueEnumQuestionReturnCode` |
| `ibValuibRoundMode` | `ibValueEnumRoundMode` |

**`AttributibRecordType` — accumulation register, 19 occurrences across 3 files**
(`accumulationRegister.h`, `accumulationRegisterMetadata.cpp`,
`accumulationRegisterMetadataProperty.cpp`):

```cpp
m_propertyAttributibRecordType      // → m_propertyAttributeRecordType
m_attributibRecordType              // (in a commented-out line)
```

`AttributeRecordType` minus `eRecord` plus `ibRecord` = `AttributibRecordType`. Same shape.

**Why this is safe to fix and worth fixing now:**

- The **script names and clsids are correct** — `ENUM_TYPE_REGISTER(ibValuibStatusMessage,
  "StatusMessage", enum_to_clsid("EN_STMS"))` registers `"StatusMessage"` / `"EN_STMS"`.
  Only the C++ identifier is mangled, so the rename touches **no registry key and no
  serialised data** (§2).
- 35 occurrences in 5 files, all local. Pure `git grep` + replace, verified by a rebuild.
- These are the only identifiers in the tree that are *wrong rather than inconsistent*.

### B2. Plain typos

| From | To | Sites |
|---|---|---|
| `Sybsystem` (banner in `interfaceHelper.h`) | `Subsystem` | 1 |
| `ExpectDelimeter` | `ExpectDelimiter` | 4 files |
| `s_dateLoaderSpreadsheet` (copy-paste from `advpropDate.cpp`) | `s_spreadsheetLoader` | 1 |
| `ibDatatabaseParameterFirebird` / `…Collection` — "Data-ta-base" | `ibDatabaseParameterFirebird` | **82 occurrences, 6 files** |

`sqllite` → `sqlite` is a **folder** and lives in [naming-plan.md § 4.2](naming-plan.md).

---

## 5. Wave C — member naming against the stated convention

[../CLAUDE.md](../CLAUDE.md) says: members `m_`, statics `s_`, constants `g_`, classes `ib`.
Two families break it.

### C1. `UPPER_SNAKE` members

```
m_treeCONSTANTS  m_treeCATALOGS  m_treeDOCUMENTS  m_treeENUMERATIONS
m_treeINFORMATION_REGISTERS  m_treeACCUMULATION_REGISTERS
m_treeCHARTS_OF_CHARACTERISTIC_TYPES  m_treeCHARTS_OF_ACCOUNTS  m_treeACCOUNTING_REGISTERS
m_treeDATAPROCESSORS  m_treeREPORTS  m_treeMETADATA
m_GRID_VALUE_STRING  m_ODBCDLL
```

→ `m_treeConstants`, `m_treeCatalogs`, `m_treeInformationRegisters`, `m_gridValueString`, …

Contained: the tree ones are all in `treeConfiguration*`; renaming them is local.

### C2. Hungarian pointer prefixes

```
m_pMDIP   m_pODBCS   m_pSQLA   m_pSQLB   m_pSQLC   m_pByteCode   m_pRef
```

`m_p*` predates the convention. **Proposal: leave the widely-used ones** (`m_pByteCode`,
`m_pRef` — hundreds of sites, zero ambiguity) and fix only the opaque ODBC/SQL ones, where
`m_pSQLA/B/C` says nothing at all. Renaming those needs a read of what they hold — this is
the one item in the plan that requires understanding, not mechanics.

---

## 6. Wave D — grouping: `#pragma region` is already the mechanism, in three dialects

The codebase **already groups declarations** with `#pragma region`. The idea is sound and
should stay. The naming is in three incompatible styles:

| Style | Examples | Count |
|---|---|---|
| `__name_h__` | `__filter_h__` (11), `__array_h__` (11), `__generic_h__` (5) | many |
| `_name_h_` | `_form_builder_h_` (20), `_data_model_h_` (5), `_value_` (4) | many |
| `name` | `enumeration` (8), `access` (7), `access_generic` (6), `role` (5), `item` (4), `serialization` (3) | many |

**Proposal:** one style — **bare lowercase words**, matching the largest existing group and
the only one that reads as English:

```cpp
#pragma region filter        // was __filter_h__
#pragma region array         // was __array_h__
#pragma region form builder  // was _form_builder_h_
#pragma region data model    // was _data_model_h_
```

The `_h_` suffix is meaningless — a region is not a file.

**And name the standard groups.** Most classes here already order declarations the same
way; make it explicit so new code has a template:

```
#pragma region construction     ctors / dtor / Init
#pragma region access           rights (AccessRight_*, roles)
#pragma region property         property + category members
#pragma region array            child collections (Get*ArrayObject)
#pragma region filter           Find*ByFilter
#pragma region serialization    ReadData / WriteData / Read/WriteProperty
#pragma region events           On*
#pragma region runtime          script surface (CallAsFunc / Get/SetPropVal)
```

This is a **documentation** act as much as a naming one: the regions become the map of what
a metaobject *is*.

## 7. Wave E — declaration order

**134 classes** open like this:

```cpp
class BACKEND_API ibValueMetaObjectAttribute : public ibValueMetaObjectAttributeBase {
	public:              // ← indented, immediately after the brace
	enum { … };
```

and some then do:

```cpp
class BACKEND_API ibValueMetaObjectInterface : public ibValueMetaObject {
	public:
protected:               // ← different indentation, same class
```

The access specifiers are **indented inconsistently**, and an empty leading `public:` is
common. Neither changes semantics.

**Proposal:**

1. Access specifiers at **column 0** (wx and the majority of this tree already do).
2. Drop empty leading `public:`.
3. Standard order where it does not fight the class: `public` → `protected` → `private`.

**Do this with a formatter, not by hand**, and in a commit that contains *only* it — a
whitespace commit that also changes a name is unreviewable. Note it will touch a large
number of files and will conflict with in-flight branches; land it when the tree is quiet.

---

## 8. Wave F — comments and descriptions

The best files here already carry their own rationale — `ctorRegistry.h`,
`queryableFactory.h`, `connectionHolder.h`, `backend_mainFrame.h` explain *why* at the top.
That is the standard to spread, and the 22 docs written on 2026-07-15 now give each
subsystem a home to link to.

**Proposal — a header comment on every public class that answers three questions:**

```cpp
// ibSourceExplorer: a source's column/field TEMPLATE (one-time form generation + the
// picker). METADATA-FREE: a node holds plain values + UI flags; a column is appended from
// the neutral ibBackendQueryColumn — never a metaobject pointer.
```

1. **What is it** in one line;
2. **What is the rule** a reader must not break (the metadata-free bit above);
3. **Where does it live** in the bigger picture — link the doc (`see docs/source-object.md`).

**Priority targets** (big, load-bearing, currently under-commented):

| File | Lines | Why |
|---|---|---|
| `compiler/value.h` | 1 485 | the type every script variable is |
| `designer/win/editor/visualEditor/visualEditor.h` | 1 025 | 10+ nested classes, no map |
| `backend/propertyManager/propertyObject.h` | 701 | the skeleton ([property-system.md](property-system.md)) — already good, needs the doc link |
| `backend/typeDescription.h` | 548 | why all three qualifiers coexist ([descriptions.md § 2.2](descriptions.md)) |

**Rule to keep:** comment the **constraint**, not the mechanics. `// re-entrant erase guard`
above a `ibValuePtr dying(*it)` earns its line; `// loop over children` does not.

---

## 9. Order, cost, risk

| Wave | What | Risk | Reviewable by |
|---|---|---|---|
| **B** typos | `Sybsystem`, `ExpectDelimeter`, loader name | none | reading |
| **A1** `ibMetadataTree` | the case-collision rename | **low, high value** | rebuild |
| **C1** UPPER_SNAKE | contained to `treeConfiguration*` | low | rebuild |
| **D** regions | one style + named standard groups | low | reading |
| **F** comments | header comments + doc links | none | reading |
| **E** access specifiers | formatter, 134 classes | low, **huge churn** | rebuild |
| **C2** `m_pSQLA/B/C` | needs understanding first | medium | reading + rebuild |
| **A2/A3** | signature / assert — **behaviour-adjacent** | — | separate commits |

**Suggested first cut:** B + A1 + C1. Small, entirely mechanical, and it removes the one
rename that can actually cause a wrong-class bug.

**Leave E for last** — it is the largest diff and the least valuable per line; landing it
early would make every other wave conflict.

**Never combine waves in one commit.** Each of these is individually boring to review and
collectively unreviewable.
