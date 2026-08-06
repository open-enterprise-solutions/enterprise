# Designer editors — code, role, interface

> **Scope:** the remaining Designer editors. The two big ones have their own docs:
> [form-editor.md](form-editor.md) (visual designer) and
> [spreadsheet-editor.md](spreadsheet-editor.md) (templates / report output).
> Companions: [metadata-tree.md](metadata-tree.md) (what opens them),
> [debugger-architecture.md](debugger-architecture.md),
> [compiler-pipeline.md](compiler-pipeline.md).
> This is foundation code.

---

## 1. The pattern that repeats — base in frontend, editing in designer

Every editor in this engine splits the same way:

| Runtime base (`frontend/`) | Designer subclass (`designer/`) | Adds |
|---|---|---|
| `ibVisualHost` | `ibVisualEditorHost` | canvas editing, undo/redo ([form-editor.md § 1.2](form-editor.md)) |
| `ibCodeEditor` | `ibCodeEditorDesigner` | debugger integration (§2.1) |
| `ibGrid` | `ibGridEditor` | cell editing, selection properties ([spreadsheet-editor.md](spreadsheet-editor.md)) |

**Read the direction:** anything general lives in `frontend/`; anything that only makes
sense while authoring lives in `designer/`. The runtime never links the editing half.

---

## 2. Code editor — `ibCodeEditor : wxStyledTextCtrl`

`frontend/win/editor/codeEditor/` — ~6 600 lines at the folder's top level (≈11 000 including
`components/` and `res/`). Scintilla-backed, and **sessionless**.

Three markers, `protected` so the designer subclass can reach them:

```cpp
enum { Breakpoint = 1, CurrentLine, BreakLine };
```

### 2.1 `ibCodeEditorDesigner` — where the debugger enters

The header states the split exactly:

> designer-side wrapper around frontend's `ibCodeEditor` that wires the debugger
> integration. **Frontend's editor is sessionless and knows nothing about `debugClient`;
> `codeRunner` uses it as-is.** Designer instantiates this subclass instead so breakpoints,
> autocomplete-eval, tooltip-eval, and module-patch notifications reach the running script.

```cpp
class ibCodeEditorDesigner : public ibCodeEditor {
    using ibCodeEditor::ibCodeEditor;           // inherit ctors
protected:
    bool IsDebuggerEnterLoop() const override;
    void OnEditDebugPoint(int line) override;
    void OnPatchModule(int line, int linesAdded, bool atLineStart) override;
    void OnEvaluateAutocomplete(const wxString& fileName, const wxString& docPath,
                                const wxString& expression, const wxString& keyword, int pos) override;
    void OnEvaluateToolTip(const wxString& fileName, const wxString& docPath,
                           const wxString& expression) override;
    void RefreshBreakpointMarkers() override;
};
```

Six virtuals — that is the entire debugger surface of the editor. Two of them are the
"evaluate in the live runtime" path from
[debugger-architecture.md § 5.1](debugger-architecture.md): autocomplete and tooltip are
**answered by the running process**, not by static analysis, when a session is attached.

`OnPatchModule(line, linesAdded, atLineStart)` is the one to notice: editing a module while
it is **stopped on a breakpoint** must shift the debuggee's line map, or every breakpoint
below the edit points at the wrong line.

### 2.2 Folding knows the language

`ibFoldLevelParser` (nested) folds by **language construct**, not by braces alone:

```cpp
enum FoldKind : short {
    FoldProcedure = 0, FoldFunction,
    FoldIf, FoldDo, FoldTry,
    FoldBrace,   // CES `{ … }` block — opens at `{`, closes at `}`
    FoldLinq,    // block-syntax LINQ — opens at `from`, closes at the terminal
                 // select / group / distinct
    FoldKindCount
};
struct FoldPoint { int line; short delta; };   // +1 open, 0 mark (Else/ElseIf/Except), -1 close
```

Two details worth keeping:

- **`delta == 0` is a *mark*, not a fold** — `Else` / `ElseIf` / `Except` neither open nor
  close a level; they punctuate one.
- **`FoldLinq` uses a per-kind counter** because a block LINQ can open unbalanced (multi-`from`
  with a single `select`). Trailing unclosed opens are trimmed by `UpdateFoldLevel`'s
  `hint_ignore_fold`, the same as an in-progress keyword fold elsewhere.

Both syntax modes fold: VES keywords *and* CES braces ([../CLAUDE.md](../CLAUDE.md) §
Compiler Quick Reference).

### 2.3 The editor parses with the engine's own lexer

```cpp
class FRONTEND_API ibParserModule  : public ibTranslateCode { … };   // codeEditorParser.h
class            ibPrecompileCode : public ibTranslateCode { … };   // codeEditorInterpreter.h
```

**Both derive `ibTranslateCode`** — the compiler's own translator
([compiler-pipeline.md § 2](compiler-pipeline.md)). The editor does not carry a second,
approximate grammar: outline, autocomplete and folding read the *same* lexemes the compiler
will read.

`ibParserModule` is a small recursive-descent reader over that lexeme stream:

```cpp
const ibLexem& PreviewGetLexem();   const ibLexem& GetLexem();   const ibLexem& ExpectLexem();
void ExpectDelimeter(const wxUniChar& c);   bool IsNextDelimeter(const wxUniChar& c);
bool IsNextKeyWord(int keyword);            void ExpectKeyword(int keyword);
wxString ExpectIdentifier(bool strRealName = false);   ibValue ExpectConstant();

bool ParseModule(const wxString& sModule);
std::vector<ibModuleElement>& GetAllContent();   // every procedure / function / variable found
```

The `Preview…` / `Get…` / `Expect…` trio is the whole parser vocabulary: peek, consume,
or consume-or-fail.

> A comment on `GetAllContent()` records a deletion worth imitating: the dedicated
> `GetVariables` / `GetFunctions` / `GetProcedures` helpers *"were unreachable and got
> removed"* — callers iterate and filter themselves.

`ibModuleCommandProcessor : wxCommandProcessor` provides undo/redo, mirroring the form
editor's processor ([form-editor.md § 3.2](form-editor.md)).

---

## 2a. Three tree editors, one shape (2026-08-06)

The role editor, the section editor and the common-attribute editor all answer the same
kind of question — *which objects does this apply to* — and all three now build their tree
the same way: **walk the metadata, group by metatype, caption and icon from the type
registry.**

```cpp
for (ibValueMetaObject* object : metaData->GetAnyArrayObject()) {
    if (object == nullptr || object->IsDeleted()) continue;
    if (!qualifies(object)) continue;
    AppendItem(GroupFor(object->GetClassType()), object);   // group created on first use
}
```

Each editor previously declared thirteen-to-fifteen named branches (`m_treeCATALOGS`,
`m_treeDOCUMENTS`, …), created them in `Init`, deleted them in `Clear`, and filled them
with a dozen near-identical blocks. The cost was not the length: **a metatype added later
was simply absent** until somebody remembered to add a branch — silently, and in the role
editor's case that means part of a configuration left unprotected.

What "qualifies" is asked of the object, never listed:

| Editor | Question | Where it lives |
|---|---|---|
| roles | `GetRoleCount() > 0` | `ibAccessObject` — already knew |
| sections | `IsInterfaceAllowed()` | `ibInterfaceObject`, beside `SetInterface` |
| common attributes | `IsCompositionAllowed()` | `ibCompositionObject` ([common-attributes.md](common-attributes.md)) |

⚠ Converting the role editor **widens what it shows**: commands, jobs and session parameters
declare rights and now appear, where before they were invisible to it. That is a behaviour
change worth verifying rather than assuming.

All three rebuilds are wrapped identically — `Freeze()` + `SetEvtHandlerEnabled(false)`
around clear-and-refill, restoring the selected row by identity. Muting events is not
cosmetic: `DeleteAllItems` / `SelectItem` raise native selection and focus events, and the
app follows focus by switching the active tab.

## 3. Role editor — `ibRoleEditor : wxSplitterWindow`

`designer/win/editor/roleEditor/` — a splitter: the object tree on one side, its rights on
the other. The tree is built as in § 2a.

Rights are declared **on the metaobject**, not in the editor
([command-interface.md § 2](command-interface.md)):

```cpp
ibRole* m_roleUse = ibValueMetaObject::CreateRole(wxT("Use"), _("Use"));
bool AccessRight_Use() const { return IsFullAccess() || AccessRight(m_roleUse); }
```

So the editor is a *view over declared roles* — adding a right means adding a `CreateRole`
member to the metaobject, and the editor picks it up. Enforcement is elsewhere:
`IsAllowed()` during the metadata walk ([property-system.md § 5](property-system.md)),
`AccessRight` / `IsInRole` in script ([system-functions.md § 2.12](system-functions.md)),
and RLS at the query door ([access-policy-rls.md](access-policy-rls.md)).

## 4. Section editor — `ibInterfaceEditor : wxWindow`

`designer/win/editor/interfaceEditor/` — edits the **Section** metaobject (the class keeps
the older "interface" name, [command-interface.md](command-interface.md)). Tree as in § 2a.

Opened as a document via `docViewInterface.{h,cpp}`, like every other editor
([metadata-tree.md § 4](metadata-tree.md)).

## 4a. Common-attribute editor — `ibCommonAttributeCompositionEditor : wxWindow`

`designer/win/editor/commonAttributeEditor/` + `docViewCommonAttribute.{h,cpp}`. Checking an
object in creates a real attribute inside it; unchecking removes it. The full mechanism is
[common-attributes.md](common-attributes.md).

---

## 5. Honest remainder

- **`ibRoleEditor` is a `wxSplitterWindow`, the other two tree editors are `wxWindow`, the
  code editor is a `wxStyledTextCtrl`, the form editor is a `wxAuiNotebook`.** Each editor
  inherits whatever wx class it happened to need — there is no common editor base. After
  § 2a the three tree editors share a *shape* (walk, group, ask) but still not a type: the
  loop, `GroupFor` and the freeze wrapper are written out in each. That is the next
  subtraction available here, and it was not taken because two of the three had just been
  rewritten and a base class extracted from a fresh pattern tends to fit only the last case
  seen.
- Enforcement of rights is elsewhere, not in the editor: `IsAllowed()` during the metadata
  walk, `AccessRight` / `IsInRole` in script, RLS at the query door.
- `ExpectDelimeter` is spelled with an `e` (`Delimeter`) throughout the parser — a rename
  candidate.
