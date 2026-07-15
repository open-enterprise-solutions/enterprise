# System functions — the global script API

> **Scope:** `ibValueSystemFunction` (`backend/system/systemManager.{h,cpp}`,
> `systemManagerFunc.cpp`) — the built-ins every script module can call without qualifying
> anything. **89 functions + 6 procedures** as of 2026-07-15.
> Companions: [../CLAUDE.md](../CLAUDE.md) (compiler quick reference),
> [name-binding.md](name-binding.md), [syntax-helper-design.md](syntax-helper-design.md).
> This is foundation code — it predates most arcs in this folder and everything rides on it.

---

## 1. How a built-in is wired

Four things must stay in lock-step. Adding a function means touching all of them:

| Piece | Where | What |
|---|---|---|
| Declaration | `systemManager.h` | `static` member of `ibValueSystemFunction` |
| Enumerator | `systemManager.cpp` | `enBoolean = 0, enNumber, …` — **index order is the contract** |
| Registration | `systemManager.cpp` → `ibValueSystemFunction_BindNames` | `helper.AppendFunc(name, argc, signature)` |
| Body | `systemManagerFunc.cpp` (997 lines) | the implementation |
| Dispatch | `systemManager.cpp` → `CallAsFunc` / `CallAsProc` | `switch` on the enumerator |

```cpp
class BACKEND_API ibValueSystemFunction : public ibValueStaticMembers<&ibValueSystemFunction_BindNames> {
    ibValueSystemFunction() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}
    virtual bool CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray);
    virtual bool CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray);
    virtual bool IsEmpty() const { return false; }
};
```

Registration carries the **arity and a human signature**, which is what the syntax helper
and autocomplete display:

```cpp
helper.AppendFunc(wxT("Boolean"), 1,  wxT("Boolean(value : any)"));
helper.AppendFunc(wxT("Round"),   3,  wxT("Round(num : number, number, roundMode)"));
helper.AppendFunc(wxT("Max"),    -1,  wxT("Max(num : number, ...)"));   // -1 = variadic
```

`ibValueStaticMembers<&BindNames>` means the name table is **static** — built once, not per
instance ([preparenames-bind-arc.md](preparenames-bind-arc.md)). `-1` as the argument count
is the variadic marker; the body then reads `paParams` / `lSizeArray` itself.

**Functions vs procedures:** `AppendFunc` → callable in an expression (`CallAsFunc`);
`AppendProc` → statement only (`CallAsProc`). See
[feedback: Func vs Proc](../CLAUDE.md) — a procedure returns nothing and cannot be used as
a value.

Constants come as macros, not registrations (`systemManager.h`):

```cpp
#define PageBreak wxT("\n\n")
#define LineBreak wxT("\n")
#define TabSymbol wxT("\t")
```

---

## 2. The catalogue

### 2.1 Basic — type conversion

`Boolean(value)` · `Number(value)` · `Date(value)` · `String(value)`

The four casts. Every one takes `any`.

### 2.2 Math

`Round(num, precision = 0, mode = Round15as20)` · `Int(num)` · `Log10(num)` · `Ln(num)` ·
`Max(...)` · `Min(...)` · `Sqrt(num)`

`Round` carries an `ibRoundMode` — the default is `ibRoundMode_Round15as20` (banker's-style
rounding choice matters for accounting; it is an explicit parameter, not a global). `Max` /
`Min` are variadic. Arithmetic is exact-decimal via `ibNumber` ([../CLAUDE.md](../CLAUDE.md) §2a).

### 2.3 Strings

`StrLen` · `IsBlankString` · `TrimL` · `TrimR` · `TrimAll` · `Left(s, n)` · `Right(s, n)` ·
`Mid(s, first, count)` · `Find(s, sub, start)` · `StrReplace(src, from, to)` ·
`StrCountOccur(src, sub)` · `StrLineCount(src)` · `StrGetLine(s, line)` · `Upper` ·
`Lower` · `Chr(code)` · `Asc(s)` · `TStr(s, language)`

These index **by character position**, which is why strings are wchar in memory — UTF-8
storage would make each an O(n) transcode ([value-audit.md](value-audit.md)).

`TStr(source, language)` is the localisation lookup — the odd one out in this group.

### 2.4 Date and time

Current: `CurrentDate()` · `WorkingDate()`

Arithmetic: `AddMonth(date, n = 1)`

Boundaries: `BegOfMonth` · `EndOfMonth` · `BegOfQuart` · `EndOfQuart` · `BegOfYear` ·
`EndOfYear` · `BegOfWeek` · `EndOfWeek` · `BegOfDay` · `EndOfDay`

Parts: `GetYear` · `GetMonth` · `GetDay` · `GetHour` · `GetMinute` · `GetSecond` ·
`GetWeekOfYear` · `GetDayOfYear` · `GetDayOfWeek` · `GetQuartOfYear`

**`WorkingDate()` is not `CurrentDate()`.** The working date is process state —
`static wxDateTime ibValueSystemFunction::ms_workDate` — the date the user is *operating
as*, used when a document defaults its date. `CurrentDate()` is the wall clock. Note
`ms_workDate` is a plain static: it is process-wide, not per-session.

### 2.5 Files

`CopyFile(src, dst)` · `DeleteFile(file)` · `GetTempDir()` · `GetTempFileName()`

Deliberately thin. Real file work goes through the file-system layer
(`backend/fileSystem/`).

### 2.6 Windows

`ActiveWindow()` → `ibBackendValueForm*`

The only window function here; it returns the **backend** form interface, so the built-in
stays GUI-free.

### 2.7 Notifications and errors

`Message(text, status = Information)` · `Alert(text)` · `Question(text, mode = OK)` ·
`SetStatus(text)` · `ClearMessage()` · `SetError(text)` · `Raise(text)` ·
`ErrorDescription()`

`Raise` throws into the platform's exception taxonomy ([../CLAUDE.md](../CLAUDE.md) §5);
`ErrorDescription()` reads the current error's text inside a handler.

### 2.8 Value inspection and reflection

`IsEmptyValue(v)` · `IsNull(v)` · `ValueIsFilled(v)` · `TypeOf(v)` · `Type(typeName)` ·
`Format(v, fmt = "")`

`Type(name)` resolves a **type by name**; `TypeOf(value)` reports a value's type — together
they are the script-side type reflection. Note the trio `IsEmptyValue` / `IsNull` /
`ValueIsFilled` are three different questions: `TYPE_EMPTY` (undefined), `TYPE_NULL` (DB
null), and "has a meaningful value".

### 2.9 Dynamic execution

`Evaluate(expression)` → value · `Execute(code)` → nothing

`Evaluate` compiles and runs an expression; `Execute` runs statements. Both go through
`ibCompileEval` — the same machinery the debugger's watch window uses
([eval-scope-refactor.md](eval-scope-refactor.md)).

### 2.10 Environment and process

`Rand()` · `ArgCount()` · `ArgValue(n)` · `ComputerName()` · `RunApp(command)` ·
`SetAppTitle(title)` · `UserDir()` · `UserName()` · `UserPassword()` ·
`GeneralLanguage()` · `EndJob(force = false)` · `UserInterruptProcessing()`

`ArgCount` / `ArgValue` expose the process command line — the entry point for
`codeRunner.exe` parameters. `UserInterruptProcessing()` is the cooperative
"let the user break out of a long loop" hook. `EndJob(force)` terminates the session.

### 2.11 Exclusive mode

`ExclusiveMode()` · `SetExclusive(on)`

Exclusive mode is what a metadata Apply needs — see
[apply exclusive UX](configuration-compare.md) and
[connection-pool.md](connection-pool.md).

### 2.12 Access rights

`AccessRight(roleName, value)` · `IsInRole(value)`

The script-side rights checks. The enforcing layer is elsewhere
([access-policy-rls.md](access-policy-rls.md)); these answer questions, they do not grant.

### 2.13 Common forms and templates

`GetCommonForm(name, owner, unique)` · `ShowCommonForm(name, owner, unique)` ·
`GetCommonTemplate(name)`

`GetCommonTemplate` returns a configuration-wide spreadsheet template
(`ibValueMetaObjectCommonSpreadsheet`, [report-engine.md § 4](report-engine.md)) — the
runtime entry to shared layouts.

### 2.14 Transactions

`BeginTransaction()` · `CommitTransaction()` · `RollBackTransaction()`

Script-level transaction control. Mind the long-transaction trap: holding one open across
UI interaction pins a pooled connection ([connection-pool.md](connection-pool.md)).

---

## 3. Honest remainder

- **`UserPassword()` returns the current user's password as a string.** It exists and is
  registered. Passwords are stored as PBKDF2-HMAC-SHA256 hashes
  ([../CLAUDE.md](../CLAUDE.md) § Known Issues), so what this can meaningfully return is
  worth verifying before any script relies on it — and worth questioning as an API.
- **`ms_workDate` is process-global.** A `static wxDateTime` on the class, not session
  state — under `wenterprise-server.exe` (N sessions per process,
  [ARCHITECTURE.md](ARCHITECTURE.md)) every session shares one working date. Verify before
  relying on it in web deployments.
- **The four-way lock-step is unguarded.** Enumerator order, `AppendFunc` order, and the
  `switch` are kept aligned by hand; nothing fails at compile time if they drift. Same
  shape as `s_listKeyWord` in `translateCode.cpp`.
- The live count is `grep -c AppendFunc/AppendProc systemManager.cpp` — 89 / 6 today.
