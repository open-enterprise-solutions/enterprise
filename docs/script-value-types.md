# Script value types — the catalogue

> **Scope:** every type a script can hold in a variable — what exists, which ones can be
> `New`-ed, and how a type gets registered. `backend/system/value/` + the register-side
> types in `backend/metaCollection/`.
> Companions: [../CLAUDE.md](../CLAUDE.md) §2 (`ibValue`), §6 (CLSID kind-typing),
> [system-functions.md](system-functions.md) (the functions that return these),
> [value-audit.md](value-audit.md).
> This is foundation code.

---

## 1. Two registrations — creatable vs not

A type is enrolled by one of two macros, and **the macro is the difference**
(`backend/compiler/typeCtor.h`):

| Macro | Ctor class | `CreateObject()` | Meaning |
|---|---|---|---|
| `VALUE_TYPE_REGISTER` | `ibCtorValueType<T>` | builds a `T` | script may **`New`** it |
| `SYSTEM_TYPE_REGISTER` | `ibCtorSystemType<T>` | **`return nullptr;`** | script may only **receive** it |

The source says it in one comment and one line:

```cpp
// object with non-create object
template <class T>
class ibCtorSystemType : public ibCtorValueTypeBase {
    virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType_object_system; }
    virtual ibValue* CreateObject() const { return nullptr; }   // ← cannot be constructed from script
};
```

So `New Array()` works and `New TableValueRow()` cannot: a row is something a `Table`
hands you, never something you build. **System types are vended, value types are
constructed.** That is the whole taxonomy below.

### 1.1 The key string is NOT the kind

Registration takes a legacy `"XX_YYY"` string that is only an **opaque unique key** for the
clsid body. The *kind* comes from **which macro registered it** — never from the prefix
([../CLAUDE.md](../CLAUDE.md) §6). The tree has live counter-examples, and they are correct:

```cpp
VALUE_TYPE_REGISTER (ibValueEvent,                  "Event",         value_to_clsid("SY_EVENT"));   // SY_ key, VALUE kind
VALUE_TYPE_REGISTER (ibValuePicture,                "StoragePicture",value_to_clsid("SY_PICTR"));   // SY_ key, VALUE kind
SYSTEM_TYPE_REGISTER(ibValueSpreadsheetDocumentArea,"SpreadsheetArea",system_to_clsid("VL_SPSTA")); // VL_ key, SYSTEM kind
```

Do not "fix" these prefixes — the string is a key, and changing it changes the clsid.

---

## 2. The catalogue

Script name · `key` · **V** = creatable (`New`) / **S** = system (vended only).

### 2.1 Collections

| Name | Key | | C++ |
|---|---|---|---|
| `Array` | `VL_ARR` | **V** | `ibValueArray` |
| `Structure` | `VL_STRUT` | **V** | `ibValueStructure` |
| `Container` | `VL_CONTR` | **V** | `ibValueContainer` |
| `KeyValue` | `VL_KEVAL` | S | `ibValueContainer::ibValueReturnContainer` |

`KeyValue` is what iterating a `Container` / `Structure` yields — hence system.

### 2.2 Value table

| Name | Key | | C++ |
|---|---|---|---|
| `Table` | `g_valueTableCLSID` | **V** | `ibValueModelTable` |
| `TableValueRow` | `VL_TVROW` | S | `…::ibValueModelTableReturnLine` |
| `TableValueColumn` | `VL_TVCLM` | S | `…::ibValueModelTableColumnCollection` |
| `TableValueColumnInfo` | `VL_TVCLI` | S | `…::…::ibValueModelTableColumnInfo` |

The row/column/column-info trio are nested classes of the table and vended by it — the
shape every collection here follows.

### 2.3 Query

| Name | Key | | C++ |
|---|---|---|---|
| `Query` | `VL_QURY` | **V** | `ibValueQueryExec` |
| `TempTablesManager` | `VL_TTMG` | **V** | `ibValueTempTablesManager` |
| `QueryResult` | `VL_QRES` | S | `ibValueQueryResult` |
| `QuerySelect` | `VL_QSEL` | S | `ibValueQuerySelect` |
| `Queryable` | `VL_QRBL` | S | `ibValueQueryable` |
| `QueryDecorator` | `VL_QDEC` | S | `ibValueQueryDecorator` |

You build a `Query`; everything downstream is handed to you. Its text is always a **package** —
an ordinary one-statement query is a package of one — so there are two verbs over one mechanism:
`ExecuteBatch()` returns an `Array` of results BY POSITION (a plain select yields its
`QueryResult`, a `SELECT … INTO` yields the row COUNT, a `DROP` yields nothing), and `Execute()`
is simply the first of them. No branch about which kind of text was handed in, and no verb that
sometimes returns a table and sometimes a number ([query-constructor.md](query-constructor.md) §5c).

`TempTablesManager` decides **how long a temp table lives**. Unset, the tables a query makes die
with it; set (`Query.TempTablesManager = manager`), they are kept by the manager and any other
query attached to the same one reads them by name. One verb — `Close()` — because that is the one
decision there is to make about them.

`Queryable` is the universal
source contract ([query-language-arc.md](query-language-arc.md)); `QueryDecorator` is what
RLS decorates a query with ([access-policy-rls.md](access-policy-rls.md)).

### 2.4 Lists and composition

| Name | Key | | C++ |
|---|---|---|---|
| `DynamicList` | `g_valueDynamicListCLSID` | **V** | `ibValueDynamicList` |
| `DynamicListRow` | `VL_DLROW` | S | `ibDynamicListReturnLine` |
| `DataComposer` | `VL_CMPS` | **V** | `ibValueDataComposer` |

See [dynamic-list.md](dynamic-list.md) and [data-composer.md](data-composer.md).

### 2.5 Spreadsheet

| Name | Key | | C++ |
|---|---|---|---|
| `SpreadsheetDocument` | `VL_SPSTD` | **V** | `ibValueSpreadsheetDocument` |
| `SpreadsheetArea` | `VL_SPSTA` | S | `ibValueSpreadsheetDocumentArea` |
| `SpreadsheetAreaRange` | `SY_SPPRA` | S | `ibValueSpreadsheetDocumentRange` |
| `SpreadsheetAreaCollection` | `SY_SPAEA` | S | `ibValueSpreadsheetDocumentAreaCollection` |
| `SpreadsheetParameterCollection` | `SY_SPPRM` | S | `ibValueSpreadsheetDocumentParameterCollection` |
| `SpreadsheetBorderRow` | `VL_SPSBO` | **V** | `ibValueSpreadsheetDocumentBorder` |

`New SpreadsheetDocument()` is how a report builds output; areas and parameter collections
come *from* it. See [report-engine.md](report-engine.md).

### 2.6 Types and reflection

| Name | Key | | C++ |
|---|---|---|---|
| `Type` | `VL_TYPE` | **V** | `ibValueType` |
| `TypeDescription` | `VL_TYPED` | **V** | `ibValueTypeDescription` |
| `QualifierNumber` | `VL_QNUM` | **V** | `ibValueQualifierNumber` |
| `QualifierDate` | `VL_QDAT` | **V** | `ibValueQualifierDate` |
| `QualifierString` | `VL_QSTR` | **V** | `ibValueQualifierString` |

The qualifiers are the length/precision/date-part constraints a `TypeDescription` carries —
the same qualifiers metadata attributes use. Paired with the `Type(name)` / `TypeOf(value)`
built-ins ([system-functions.md § 2.8](system-functions.md)).

### 2.7 Database access

| Name | Key | | C++ |
|---|---|---|---|
| `DatabaseLayer` | `VL_DBLY` | **V** | `ibValueDatabaseLayer` |
| `DatabasePreparedStatement` | `VL_DBPS` | S | `ibValuePreparedStatement` |
| `DatabaseResultSet` | `VL_DBRS` | S | `ibValueResultSet` |

The script-side mirror of `ibDatabaseLayer` ([../CLAUDE.md](../CLAUDE.md) §1): you make a
layer, it vends statements and result sets. Note that a **prepared statement is the only
way** to bind user values — the same rule as C++ ([../CLAUDE.md](../CLAUDE.md) § What Not
To Do).

### 2.8 System and OS

| Name | Key | | C++ |
|---|---|---|---|
| `File` | `VL_FILE` | **V** | `ibValueFile` |
| `Guid` | `VL_GUID` | **V** | `ibValueGuid` |
| `ComObject` | `VL_OLE` | **V** | `ibValueOLE` |

`ComObject` is Windows COM/OLE automation — the escape hatch to external applications.

### 2.9 UI primitives

| Name | Key | | C++ |
|---|---|---|---|
| `Colour` | `VL_COLOR` | **V** | `ibValueColour` |
| `Font` | `VL_FONT` | **V** | `ibValueFont` |
| `Point` | `VL_PONT` | **V** | `ibValuePoint` |
| `Size` | `VL_SIZE` | **V** | `ibValueSize` |
| `StoragePicture` | `SY_PICTR` | **V** | `ibValuePicture` |

Declared in `backend.dll` and GUI-free — they are data (a colour is four bytes), not
widgets.

### 2.10 Events

| Name | Key | | C++ |
|---|---|---|---|
| `Event` | `SY_EVENT` | **V** | `ibValueEvent` |
| `ActionEvent` | `SY_ATEVT` | S | `ibValueActionEvent` |

### 2.11 Registers and records (`backend/metaCollection/`)

Vended by metaobjects rather than declared in `system/value/`:

`RecordRegister` · `RecordSetRegisterColumn` · `RecordSetRegisterColumnInfo` ·
`RecordSetRegisterKey` · `RecordSetRegisterKeyDescription` · `InformationRecordManager` ·
`TabularSectionColumn` · `TabularSectionColumnInfo` · `ExternalManagerReport` ·
`externalManagerDataProcessor`

`ExternalManagerReport` is the external-report entry point
([metadata-tree.md § 5.2](metadata-tree.md)).

---

## 3. Honest remainder

- **`externalManagerDataProcessor` is registered in lowerCamelCase** while every sibling is
  PascalCase (`ExternalManagerReport`, `InformationRecordManager`). Script type names are
  user-facing; this is a naming-plan candidate — but changing the *name* changes the
  registry key, so it is not a free rename.
- The **V/S choice is the API contract** and is invisible at the call site: a wrong macro
  makes a type silently un-`New`-able (or wrongly creatable) with no compile error.
  `CreateObject()` returning `nullptr` is the only tell.
- Counts drift. Live list:
  `grep -rh "VALUE_TYPE_REGISTER(\|SYSTEM_TYPE_REGISTER(" --include=*.cpp backend/`.
