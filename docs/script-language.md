# Script language — the reference

> **Scope:** the language a configuration is written in — lexis, both syntax dialects, every
> keyword, declarations, control flow, lambdas, the LINQ block, the preprocessor, and the
> global function set. Written to be enough to GENERATE correct code without reading the
> compiler.
>
> **Derived from code, not from memory:** keyword table `s_listKeyWord[]`
> (`compiler/translateCode.cpp`) in lock-step with the `KEY_*` enum (`compiler/codeDef.h`);
> dialect differences from `gs_codeStyle` branches in `compiler/compileCode.cpp`; the global
> API from `ibSystemManager` (`system/systemManager.cpp`). When the code changes, re-derive
> from those three places.
>
> Companions: [compiler-pipeline.md](compiler-pipeline.md) (how it compiles and runs),
> [script-value-types.md](script-value-types.md) (every value type: creatable vs vended),
> [lambda.md](lambda.md) + [closure-capture.md](closure-capture.md),
> [linq.md](linq.md) (the query block in depth),
> [system-functions.md](system-functions.md) (the global API, per-function detail),
> [form-engine.md](form-engine.md) (what a form module runs inside).

---

## 1. Two dialects, one language

The same keywords, the same bytecode; only **block shape** differs. The mode is process-global
(`ibCompileCode::SetCodeStyle` / `GetCodeStyle`) and stored per configuration; **CES is the
default for new configurations** (since 2026-05-10), VES is kept for migrated ones.

| | **CES** (C-flavoured, default) | **VES** (Visual-Basic-flavoured, legacy) |
|---|---|---|
| block | `{ … }` | keyword-fenced |
| condition | `if (x > 0) { … }` | `If x > 0 Then … EndIf` |
| loop | `while (i < 10) { … }` | `While i < 10 Do … EndDo` |
| routine | `Function F(a) { … }` | `Function F(a) … EndFunction` |
| try | `try { … } except { … }` | `Try … Except … Endtry` |

In CES the fence keywords **do not exist at all** — `Then`, `Do`, `EndIf`, `EndDo`,
`EndFunction`, `EndProcedure`, `Endtry` are filtered out of the keyword table
(`ibTranslateCode::IsAllowedKey`). Writing them in CES is an error, not a synonym.

Everything below is written in CES; the VES form is given where it differs.

---

## 2. Lexis

| element | form | note |
|---|---|---|
| comment | `// to end of line` | the only comment form |
| string | `"text"` | double quotes |
| **date** | `'YYYYMMDD'` / `'YYYYMMDDHHMMSS'` | **apostrophes** — a date literal, NOT a string |
| number | `123`, `123.45`, `-1` | exact decimal (`ibNumber`), not float |
| boolean | `True` / `False` | |
| absent value | `Undefined` | the empty value (`TYPE_EMPTY`) |
| database null | `Null` | distinct from `Undefined` |
| label (for `GoTo`) | `~LabelName` | `~` marks the label |
| identifiers | letters/digits/underscore | case-insensitive keywords; **PascalCase** is the house style |

`Undefined` vs `Null` is a real distinction: `Undefined` is "no value at all", `Null` is a
value that came from the database as SQL NULL. `IsEmptyValue(v)` tests the first,
`IsNull(v)` the second, `ValueIsFilled(v)` answers "is there anything meaningful in here".

---

## 3. Every keyword

The full table, in enum order. That order is load-bearing: the translator looks a keyword up
by index, so `s_listKeyWord[]` and `KEY_*` must stay in lock-step.

**Control flow** — `If` `Then` `Else` `Elseif` `Endif` `For` `Foreach` `To` `In` `Do` `EndDo`
`While` `GoTo` `Continue` `Break`

**Logic** — `Not` `And` `Or`

**Routines** — `Procedure` `EndProcedure` `Function` `EndFunction` `Return` `Val`

**Access modifiers** — `Public` `Private` `Protected`

**Errors** — `Try` `Except` `Endtry` `Raise`

**Declaration / construction** — `Var` `New`

**Literals** — `Undefined` `Null` `True` `False`

**Preprocessor** — `#Define` `#Undef` `#Ifdef` `#Ifndef` `#Else` `#Endif` `#Region` `#EndRegion`

**LINQ block** — `From` `Where` `Select` `OrderBy` `Ascending` `Descending` `Take` `Skip`
`Distinct` `Join` `On` `Equals` `Group` `By` `Into` `Restrict`

---

## 4. Declarations

```oes
Var counter;                 // module- or routine-local variable
Public Var sharedTotal;      // exported: visible to other modules
Protected Var forChildren;   // visible to children (an object → its forms)
Private Var localOnly;       // module-local (the default; explicit intent)
```

The same three modifiers apply to routines:

```oes
Public Procedure Recalculate(document, Val mode)
{
    …
}

Function Total(rows)
{
    Var sum = 0;
    Foreach (row In rows) { sum = sum + row.Amount; }
    Return sum;
}
```

- **`Val`** before a parameter passes it **by value** — the callee cannot change the caller's
  variable. Without it, a parameter is by reference.
- A **procedure** returns nothing; a **function** returns a value with `Return`.
- `Public` is what makes a name visible across modules (it was called `Export` historically).
- `Protected` is the object→its-forms direction: a catalog's object module exposes a name to
  the forms of that catalog, but not to the whole configuration.

---

## 5. Control flow

```oes
if (count > 0) {
    …
} else if (count == 0) {
    …
} else {
    …
}

while (i < 10) { i = i + 1; }

for (i = 1 To 10) { … }          // numeric range, inclusive
foreach (item In collection) { … }

try {
    Risky();
} except {
    Message(ErrorDescription());
}

Raise("Something is wrong");     // throw
```

VES form:

```oes
If count > 0 Then … Elseif count = 0 Then … Else … EndIf
While i < 10 Do … EndDo
For i = 1 To 10 Do … EndDo
Foreach item In collection Do … EndDo
Try … Except … Endtry
```

`Continue` / `Break` behave as expected. `GoTo ~Label` exists and jumps to a `~Label` mark —
it is legacy; do not generate it.

---

## 6. Creating objects — `New`

```oes
Var list  = New Array();          // parentheses optional: `New Array` parses too
Var lines = New Table();
Var id    = New Guid("…");        // constructor parameters are ordinary expressions
Var desc  = New TypeDescription(New Type("Number"), New QualifierNumber(10, 2));
```

Exactly how the compiler reads it (`compileCode.cpp`, the `KEY_NEW` branch):

1. **The type is an IDENTIFIER, not a string** — `New Array()`, never `New "Array"`.
2. **The parentheses are optional.** `New Array` and `New Array()` compile to the same thing.
3. **Parameters are expressions**, comma-separated — and a parameter may be **skipped**:
   `New Foo(a, , c)` is legal and passes "missing" in the gap.
4. **Only a value-kind type may be `New`-ed.** The check is
   `IsRegisterCtor(name, ibCtorObjectType_object_value)` and it happens **at compile time** —
   a wrong name is `ERROR_CALL_CONSTRUCTOR`, not a runtime surprise.

### What can be `New`-ed

The registration macro decides: `VALUE_TYPE_REGISTER` → constructible, `SYSTEM_TYPE_REGISTER`
→ **vended only** (`CreateObject()` returns null). The constructible set today:

| group | types |
|---|---|
| collections | `Array` `Structure` `Container` `Table` |
| query / data | `Query` `DynamicList` `DataComposer` |
| spreadsheet | `SpreadsheetDocument` `SpreadsheetBorderRow` |
| types & qualifiers | `Type` `TypeDescription` `QualifierNumber` `QualifierDate` `QualifierString` |
| database | `DatabaseLayer` |
| system | `File` `Guid` `ComObject` `Event` `StoragePicture` |
| presentation data | `Colour` `Font` `Point` `Size` |

Everything else a script holds is **handed to it**, never built: a `TableValueRow` comes from a
`Table`, a `QueryResult` from a `Query`, a reference / object / manager / form from a
metaobject. Trying to `New` one is a compile error, which is the intended design — see
[script-value-types.md](script-value-types.md) for the full catalogue and the V/S mark per type.

### The other way objects appear

Business objects are **not** `New`-ed — they come from their metaobject's manager, because only
it knows how to bind them to data:

```oes
Var item = Catalogs.Products.CreateElement();   // a new catalog item
item.Description = "Bolt";
item.Write();

Var doc = Documents.Sale.CreateDocument();
Var found = Catalogs.Products.FindByCode("00001");
if (ValueIsFilled(found)) { Message(found.Description); }

Var form = Catalogs.Products.GetListForm();     // a form value, vended
```

Member access is `.`; indexing is `[i]` where the value supports it. A form reaches itself as
**`ThisForm`**.

---

## 7. Lambdas and closures

An anonymous function is an **expression** — assignable, passable, callable through a variable:

```oes
Var isBig = Function(x) { Return x > 100; };
Var rows  = source.Where(isBig);

Var log = Procedure(text) { Message(text); };
log("done");
```

VES: `Function(x) … EndFunction` / `Procedure(x) … EndProcedure`.

Lambdas **capture the enclosing frame**: a lambda written inside a function may read and write
that function's locals, and it keeps them alive after the function returns
([closure-capture.md](closure-capture.md)).

---

## 8. The LINQ block

A query is written **in the language**, not as a string — it compiles to the same bytecode and
runs against the same sources (a database table, an in-memory collection, a value table):

```oes
Var report =
    from row in Documents.Sale.Records
    join client in Catalogs.Clients on row.Client equals client.Ref
    where row.Date >= BegOfMonth(CurrentDate()) And row.Amount > 0
    group row.Amount by client.Description into totals
    orderby totals.Sum descending
    take 10
    select totals;
```

Clauses: `from … in` (entry) · `join … in … on … equals …` · `where` · `group … by … into …` ·
`orderby … [ascending|descending]` · `skip` / `take` · `distinct` · `select` (projection).

**`restrict`** is the access-policy filter — the same shape, applied as a row-level rule rather
than a user query ([access-policy-rls.md](access-policy-rls.md)).

Depth, pushdown and what is executed where: [linq.md](linq.md) and
[query-engine-layers.md](query-engine-layers.md).

---

## 9. Preprocessor

```oes
#Define DEBUG_MODE

#Ifdef DEBUG_MODE
    Message("debug build");
#Else
    // production path
#Endif

#Region Recalculation
    …
#EndRegion
```

`#Region` / `#EndRegion` are folding markers only — they do not affect compilation.
`#Undef` removes a definition; `#Ifndef` is the negative test.

---

## 10. The global API — 89 functions + 6 procedures

Available in every module without qualification. Signatures below are the ones the platform
itself publishes (they drive the syntax helper), so they are exact.

**Conversion** — `Boolean(value)` `Number(value)` `Date(value)` `String(value)` `Type(strType)`
`TypeOf(value)` `Format(value, format)`

**Math** — `Round(num, digits, roundMode)` `Int(num)` `Sqrt(num)` `Log10(num)` `Ln(num)`
`Max(…)` `Min(…)` `Rand()`

**Strings** — `StrLen` `IsBlankString` `TrimL` `TrimR` `TrimAll` `Left(str, n)` `Right(str, n)`
`Mid(str, from, n)` `Find(str, sub, from)` `StrReplace(str, from, to)` `StrCountOccur(str, sub)`
`StrLineCount(str)` `StrGetLine(str, line)` `Upper` `Lower` `Chr(n)` `Asc(str)`
`Tstr(text, langCode)` — the last one is the translation lookup.

**Dates** — `CurrentDate()` `WorkingDate()` `AddMonth(d, n)` and the family
`BegOfMonth/EndOfMonth/BegOfQuart/EndOfQuart/BegOfYear/EndOfYear/BegOfWeek/EndOfWeek/BegOfDay/EndOfDay`,
plus `GetYear/GetMonth/GetDay/GetHour/GetMinute/GetSecond/GetWeekOfYear/GetDayOfYear/GetDayOfWeek/GetQuartOfYear`.

**Files** — `FileCopy(src, dst)` `FileDelete(name)` `GetTempDir()` `GetTempFileName()`

**Interaction** — `Message(text, statusMessage)` *(procedure)* `Alert(text)`
`Question(text, mode)` `SetStatus(text)` `ClearMessages()` `ActiveWindow()`
`GetCommonForm(name, owner, id)` `ShowCommonForm(name, owner, id)` *(procedure)*
`GetCommonTemplate(name)` `SetAppTitle(title)`

**Errors** — `Raise(text)` `SetError(text)` `ErrorDescription()`

**Values** — `IsEmptyValue(v)` `IsNull(v)` `ValueIsFilled(v)` `Evaluate(expr)` `Execute(expr)`

**Transactions** *(all procedures)* — `BeginTransaction()` `CommitTransaction()`
`RollBackTransaction()`

**Session / environment** — `UserName()` `UserPassword()` `UserDir()` `ComputerName()`
`GeneralLanguage()` `ExclusiveMode()` `SetExclusive(on)` *(procedure)* `AccessRight(role, metadata)`
`IsInRole(role)` `ArgCount()` `ArgValue(i)` `RunApp(command)` `EndJob(force)`
`UserInterruptProcessing()`

> The count drifts as features land. The live list is `AppendFunc` / `AppendProc` in
> `system/systemManager.cpp` — grep it rather than trusting a number.

---

## 11. Rules for generated code

1. **Pick the dialect from the configuration** (`Syntax` on the config root), and do not mix:
   in CES the fence keywords are not synonyms, they are errors.
2. **A date is `'20260130'`, not `"20260130"`.** The quote decides the type.
3. **`Undefined` ≠ `Null`.** Test with `IsEmptyValue` / `IsNull` / `ValueIsFilled`, not `==`.
4. **Numbers are exact decimals.** No float epsilon games; `Round(x, 2, mode)` is exact.
5. **Do not invent names.** Every member of a metaobject comes from the metadata; the compiler's
   second pass binds them, so an invented attribute fails at compile time, not at runtime —
   which is why generated code should always be compiled before it is offered.
6. **`Val` for parameters you do not intend to modify** — it documents intent and prevents a
   surprise write-back.
7. **`GoTo` is legacy** — never generate it.
