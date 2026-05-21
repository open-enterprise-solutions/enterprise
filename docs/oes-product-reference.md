# OES Enterprise — Product Reference

**Status:** v1.0 (comprehensive reference for OES Enterprise platform)  
**Date:** 2026-05-21  
**Audience:** Product managers, architects, integration partners, platform engineers

---

## 1. What is OES Enterprise

OES (Open Enterprise Solutions) is an open-source rapid application development (RAD) platform designed to model and execute business-critical information systems. It is a spiritual successor to 1С:Enterprise, the dominant ERP platform in Russian-speaking markets, adapted for open-source infrastructure and designed with first-class plugin extensibility, AI-assisted development, and modern web deployment.

### Core identity

- **Domain:** Business metadata modeling + runtime execution + form-driven UI + reporting
- **Architecture:** Thick-client desktop (wxWidgets) + Web (HTTP) + Headless daemon modes
- **Metadata:** 11 semantic object types (Catalog, Document, Register, etc.) stored in SQL database
- **Script:** Two syntax modes (VES = Visual Basic–style; CES = C-style) compiled to bytecode and executed in-process
- **Database:** Driver-agnostic SQL layer supporting Firebird, PostgreSQL, SQLite, MySQL, ODBC
- **Deployment:** Single-tenant per database; multi-user through sessions registered in `sys_session` table

### Not OES

- Not a CMS (no content versioning or publishing workflows built-in)
- Not a workflow engine (forms and scripts are imperative, no BPMN/state-machine DSL)
- Not a reporting stack (the Report metadata type is a script that renders HTML; charting is embedded, no BI cube model)
- Not a real-time sync platform (web deployment is request-response, not WebSocket push)

### Who uses it

- **Business consultants:** Deploy custom applications without writing bare SQL / HTML
- **System integrators:** Parameterize configurations for customers (Accounting, Inventory, Sales, etc.)
- **In-house IT teams:** Develop mission-critical apps on open infrastructure (no Microsoft/.NET requirement)
- **Government / regulated industries:** Full-stack visibility; source code in Git; no vendor lock-in

---

## 2. Build Targets & Runtime Modes

### Executables (6 primary)

| Exe | Mode Enum | Purpose | GUI |
|---|---|---|---|
| `launcher.exe` | `eLAUNCHER_MODE` | Connection wizard; create/select DB file; switch users | wxWidgets |
| `designer.exe` | `eDESIGNER_MODE` | IDE — metadata editor, form designer, code editor, debugger client | wxWidgets |
| `enterprise.exe` | `eENTERPRISE_MODE` | Thick-client runtime; one session per process; forms + scripts | wxWidgets |
| `wenterprise-server.exe` | `eWEB_ENTERPRISE_MODE` | HTTP server; N sessions per process (per cookie); web client serialized as JSON | HTTP |
| `daemon.exe` | `eSERVICE_MODE` | Headless; no UI; background jobs / batch processing | None |
| `codeRunner.exe` | `eSERVICE_MODE` | Execute single script module from stdin; scratch runner | None |

### DLLs (2 shared)

| DLL | Size | Role |
|---|---|---|
| `backend.dll` | ~4–6 MB | Metadata, compiler, database drivers, debugger server, plugin manager, session registry. Zero GUI deps. |
| `frontend.dll` | ~3–5 MB | wxWidgets controls (22 types), form runtime, visual host, code editor, property inspector |

### Database Drivers (5)

| Driver | Status | Features |
|---|---|---|
| **Firebird** | Primary | Embedded mode (no server), transactions, stored procs (not used), triggers (not used) |
| **PostgreSQL** | Production | Remote server, BOOLEAN type, NUMERIC(precision, scale), full ACID |
| **SQLite** | Embedded | Single-file, no server, limited concurrency, suitable for single-user / mobile |
| **MySQL** | Legacy | Remote server; lacks some OES features (subqueries in FROM clause, CTEs) |
| **ODBC** | Generic bridge | Plugs into any ODBC-compliant DB; limited feature set tested |

### Build Configuration

| Platform | Build System | Submodules |
|---|---|---|
| **Windows** | MSBuild + Visual Studio | wxWidgets pre-built binaries (not submodule) |
| **macOS** | CMake | wxWidgets built from `src/3rdparty/wxWidgets` submodule |
| **Linux** | CMake | wxWidgets built from submodule |

Third-party vendored:
- **wxWidgets 3.3.2** — GUI toolkit (submodule)
- **nlohmann/json 3.11.3** — JSON serialization (header-only)
- **cpp-httplib 0.18.1** — HTTP client/server (single-header)
- **md4c 0.5.2** — Markdown parser (for AI Assistant pane)

---

## 3. Metadata Model (Semantic Foundation)

The 11 metadata types model a complete business domain. Each is a **semantic layer**, not just a C++ class.

### The 11 Types

| Type | CLSID | Semantic Role | Example |
|---|---|---|---|
| **Catalog** | `MD_CAT` | Master reference data; hierarchical (parent–child); named instances | Products, Vendors, Employees |
| **Document** | `MD_DOC` | Transactional records; immutable when posted; triggers register postings | Invoice, Purchase Order, Journal Entry |
| **Enumeration** | `MD_ENUM` | Closed set of symbolic values; non-hierarchical | DocumentStatus { Draft, Posted, Archived } |
| **Constant** | `MD_CONST` | Scalar configuration values; global, editable at runtime | TaxRate, DefaultWarehouse |
| **InformationRegister** | `MD_IREG` | Event log / fact table; unordered; no aggregation | WebLog, AuditTrail |
| **AccumulationRegister** | `MD_AREG` | Turning-band journal; records Debit/Credit amounts; balance queries | StockLedger, CashFlow |
| **ChartOfCharacteristicTypes** | `MD_CHRC` | Subconto (analytical dimension) type defs; each element holds allowed value types | CostCenter, Department |
| **ChartOfAccounts** | `MD_CHOA` | Chart of accounts; Active/Passive/AP account types; links to subcontos | GL 1000 Assets, GL 2000 Liabilities |
| **AccountingRegister** | `MD_AREG` (v2) | Double-entry register; Account + Subconto columns; Balance/Turnovers queries | General Ledger |
| **DataProcessor** | `MD_DP` | Stateless script object; callable from forms / reports | BatchInvoiceGenerator, ReportBuilder |
| **Report** | `MD_REPORT` | Script + form that produces output (HTML / PDF); queries registers | MonthlySalesReport, TrialBalance |

### Standard Attributes (Every Type)

| Type | Has `Code` | Has `Description` | Has `Hierarchy` | Storeable |
|---|---|---|---|---|
| Catalog | ✓ | ✓ | ✓ | ✓ |
| Document | ✗ | ✗ | ✗ | ✓ |
| Enumeration | ✗ | ✗ | ✗ | — |
| Constant | ✗ | ✗ | ✗ | ✓ |
| InformationRegister | ✗ | ✗ | ✗ | ✓ |
| AccumulationRegister | ✗ | ✗ | ✗ | ✓ |
| ChartOfCharacteristicTypes | ✓ | ✓ | — | ✓ |
| ChartOfAccounts | ✓ | ✓ | — | ✓ |
| AccountingRegister | ✗ | ✗ | ✗ | ✓ |
| DataProcessor | ✗ | ✗ | ✗ | — |
| Report | ✗ | ✗ | ✗ | — |

### Structural Children (Composite)

Every metadata type can own:

- **Attributes** — scalar typed fields (Number, String, Date, Reference, etc.)
- **TabularSections** — inline tables (e.g., Document.Items = lines)
- **Forms** — ItemForm, ListForm, ChoiceForm, SelectionForm
- **Commands** — buttons + menu items + keyboard shortcuts
- **Modules** — object module (methods), common module (shared code), manager module

**Bindings (type-aware references):**
- Document → Catalog (e.g., Invoice.Customer → Customers)
- ChartOfAccounts → ChartOfCharacteristicTypes (subconto type definition)
- AccountingRegister → ChartOfAccounts (posting accounts)

### Runtime Methods (Per Type)

**Catalog.Get(id) → Ref**
Get a catalog element by reference. Returns undefined if not found.

**Catalog.FindByCode(code) → Ref**
Lookup by `Code` attribute. Returns undefined if not found.

**Catalog.FindByDescription(desc) → Ref**
Lookup by `Description` attribute.

**Catalog.FindOrCreate(code, desc?) → Ref**
Get by code; if not found, create with description and return new ref.

**Document.Write()**
Persist the document to the database (INSERT/UPDATE main table + rows of TabularSections).

**Document.Post()**
Mark the document as posted (set `Posted = true`, `PostedDate = now()`); register movements (write AccumulationRegister/AccountingRegister records).

**Document.Unpost()**
Reverse; delete register records, unset posted flag.

**Register.Add(row) → void**
Insert a record into an InformationRegister or AccumulationRegister.

**Register.Delete(row) → void**
Remove by matching key columns.

**Register.GetBalance(dt?, dim?) → Number**
Query AccumulationRegister balance at a point in time with optional dimensional filter.

**Register.GetTurnovers(dt1, dt2, dim?) → Table**
Query debit/credit turnovers over a period.

All of these methods are implemented as native C++ functions dispatched from the script bytecode, not as database queries written in script. SQL generation is automatic.

### Database Mapping (Table Schema)

Every metadata object type generates SQL DDL:

| Type | Table(s) | Key Columns | Notes |
|---|---|---|---|
| Catalog | `{name}` | `id` (INT PRIMARY KEY AUTO_INCREMENT) | `name`, `code`, `description`, `parent_id` (NULL if root), `deleted` (BOOL) |
| Document | `{name}` | `id` | `num`, `date`, `posted` (BOOL), `posted_date`, `user`, `modified` (TIMESTAMP) |
| InformationRegister | `{name}` | `id`, `period` (TIMESTAMP) | No aggregation; pure fact-table append |
| AccumulationRegister | `{name}` | `id`, `period`, `account` | `debit_amount`, `credit_amount` |
| ChartOfAccounts | `accounts` | `id` | `code`, `name`, `type` (Active/Passive/AP), `deleted` |
| TabularSection (under Doc) | `{doc}_{section}` | `parent_id`, `line_num` | Child-table referencing parent via FK |

The schema is generated by `ibDatabaseLayer::CreateTable()` and schema migration is manual (OES has no migrations DSL).

---

## 4. Configuration Text Format (XML & JSON)

OES configurations can be exported to text for Git VCS, AI generation, and manual editing. Two formats are defined:

### OES-XML-2.0

**Structure:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<Configuration>
  <Metadata Version="1.0" Language="English">
    <Catalogs>
      <Catalog ID="..." Name="Products" Code="PROD" Synonym="Products">
        <Attributes>
          <Attribute Name="PartNumber" Type="String" Length="20" />
          <Attribute Name="UnitCost" Type="Number" Precision="15" Scale="2" />
        </Attributes>
        <TabularSections>
          <TabularSection Name="Suppliers">
            <Attributes>
              <Attribute Name="SupplierRef" Type="Reference" RefType="Vendors" />
              <Attribute Name="Cost" Type="Number" Precision="15" Scale="2" />
            </Attributes>
          </TabularSection>
        </TabularSections>
        <Forms>
          <Form Name="ItemForm" Type="Item">
            <Base64>...</Base64>
          </Form>
        </Forms>
      </Catalog>
    </Catalogs>
    <Documents>...</Documents>
    <Enumerations>...</Enumerations>
    ...
  </Metadata>
</Configuration>
```

**Serialization (` metadataConfiguration.cpp`):**
- `SaveConfigToXML(path)` — write entire tree recursively
- `LoadConfigFromXML(path)` — parse and instantiate metadata objects

**Type qualifiers:**
- `Type="Number"` with `Precision`, `Scale` for decimal values
- `Type="String"` with `Length` for VARCHAR
- `Type="Reference"` with `RefType` specifying the Catalog/Document/etc. name
- `Type="Structure"` for ad-hoc objects

### OES-JSON-1.0

**Structure** (using nlohmann/json):
```json
{
  "version": "1.0",
  "language": "English",
  "catalogs": [
    {
      "id": "...",
      "name": "Products",
      "code": "PROD",
      "synonym": "Products",
      "attributes": [
        {
          "name": "PartNumber",
          "type": "String",
          "length": 20
        }
      ],
      "forms": [
        {
          "name": "ItemForm",
          "type": "Item",
          "base64": "..."
        }
      ]
    }
  ],
  "documents": [],
  "enumerations": []
}
```

**Serialization** (`metadataConfigurationJSON.cpp`):
- `SaveConfigToJSON(path)` — nlohmann/json::dump with 2-space indent
- `LoadConfigFromJSON(path)` — nlohmann/json::parse and instantiate

**Field mapping:**
- CLSID numeric IDs are mapped to type names at serialization boundary
- Form binary (wxFormBuilder XML + module code) is base64-encoded
- All metadata IDs (GUIDs) are preserved for round-trip identity

### Round-trip guarantee

Both formats support full serialization of all 11 types with no loss; re-loading produces identical metadata objects.

---

## 5. Built-in Language Reference

### Syntax modes

| Mode | Style | Example | Default since |
|---|---|---|---|
| **VES** | Visual Basic | `If x Then … EndIf` | Pre-2026-05-10 |
| **CES** | C-style | `if (x) { … }` | 2026-05-10 onward |

Both compile to identical bytecode. Mode is global per session, set via `ibCompileCode::SetCodeStyle()`.

### 43 Keywords

**Control flow:** `if`, `then`, `else`, `elseif`, `endif`, `for`, `foreach`, `to`, `in`, `do`, `enddo`, `while`, `goto`, `not`, `and`, `or`

**Functions:** `procedure`, `endprocedure`, `function`, `endfunction`, `export`, `return`, `val`

**Exceptions:** `try`, `except`, `endtry`, `raise`

**Variables:** `var`, `new`, `undefined`, `null`, `true`, `false`

**Preprocessor:** `#define`, `#undef`, `#ifdef`, `#ifndef`, `#elsedef`, `#endifdef`, `#region`, `#endregion`

**LINQ (query):** `from`, `where`, `select`, `orderby`, `ascending`, `descending`, `take`, `skip`, `distinct`, `join`, `on`, `equals`, `group`, `by`, `into`

Total: 43 keywords (English-only, no Cyrillic variants unlike 1C:Enterprise).

### 69 Opcodes (Bytecode Instructions)

Grouped by category:

| Category | Opcodes |
|---|---|
| Arithmetic | `OPER_ADD`, `OPER_SUB`, `OPER_MULT`, `OPER_DIV`, `OPER_MOD`, `OPER_INVERT` (and const-variants: `ADDCONS`, `SUBCONS`, etc.) |
| Comparison | `OPER_GT`, `OPER_EQ`, `OPER_LS`, `OPER_GE`, `OPER_LE`, `OPER_NE` |
| Logical | `OPER_NOT`, `OPER_AND`, `OPER_OR` |
| Control | `OPER_GOTO`, `OPER_IF`, `OPER_FOR`, `OPER_FOREACH`, `OPER_IN`, `OPER_NEXT`, `OPER_NEXT_ITER` |
| Variables | `OPER_LET`, `OPER_CONST`, `OPER_CONSTN`, `OPER_SET`, `OPER_SETREF`, `OPER_SETCONST` |
| Functions | `OPER_FUNC`, `OPER_ENDFUNC`, `OPER_CALL`, `OPER_CALL_METHOD`, `OPER_CALL_CLOSURE`, `OPER_RET` |
| Lambdas | `OPER_LFUNC`, `OPER_ENDLFUNC`, `OPER_CALL_LAMBDA` |
| LINQ | `OPER_CALL_LINQ` — dispatches to `ibValue::DispatchLinqMethod` by enum id |
| Arrays | `OPER_GET_ARRAY`, `OPER_SET_ARRAY`, `OPER_CHECK_ARRAY`, `OPER_SET_ARRAY_SIZE`, `OPER_ENTER_A`, `OPER_GET_A`, `OPER_SET_A` |
| Objects | `OPER_NEW`, `OPER_SET_TYPE` |
| Exceptions | `OPER_TRY`, `OPER_ENDTRY`, `OPER_RAISE`, `OPER_RAISE_T` |
| Type-specialised variants | Add `TYPE_DELTA1` (number), `TYPE_DELTA2` (string), `TYPE_DELTA3` (date), `TYPE_DELTA4` (boolean) to base opcode |

Each opcode is a 32-bit integer constant defined in `codeDef.h`.

### 91 Built-in Functions

**Math:** `Abs`, `Int`, `Round`, `Exp`, `Log`, `Sqrt`, `Sin`, `Cos`, `Tan`, `ASin`, `ACos`, `ATan`, `ATan2`, `Pow`

**String:** `StrLen`, `Left`, `Right`, `Mid`, `Lower`, `Upper`, `TrimL`, `TrimR`, `Trim`, `Find`, `Replace`, `Substitute`, `StrConcat`, `StrSplit`, `StrRepeat`, `StrReverse`, `StrStartsWith`, `StrEndsWith`, `StrCompare`, `Format`, `FormatNumber`, `FormatDate`, `FormatTime`, `FormatBoolean`

**Date/Time:** `Now`, `Date`, `Time`, `Year`, `Month`, `Day`, `Hour`, `Minute`, `Second`, `DayOfWeek`, `AddMonths`, `BeginOfMonth`, `EndOfMonth`, `BeginOfQuarter`, `EndOfQuarter`, `BeginOfYear`, `EndOfYear`, `WeekBefore`, `MonthBefore`, `QuarterBefore`, `YearBefore`

**Type checking:** `TypeOf`, `IsUndefined`, `IsNull`, `IsNumber`, `IsString`, `IsDate`, `IsBoolean`, `IsReference`, `IsStructure`, `IsArray`

**Array/Structure:** `ArrayNew`, `ArrayAppend`, `ArrayInsert`, `ArrayDelete`, `ArrayResize`, `ArrayClone`, `ArrayIndexOf`, `StructureNew`, `StructureInsert`, `StructureDelete`, `StructureProperties`, `StructurePropertyValue`, `StructureClear`

**Database:** `Query`, `UploadResults`, `SaveResults`

**Script:** `Execute`, `CallProcedure`, `CallFunction`, `Eval`

**Debugging:** `Break`, `LogLine`, `LogTable`

**Miscellaneous:** `Random`, `MD5`, `Hash`, `Convert`, `PresentationValueToObject`, `ObjectPresentationValue`

Total: **91 built-in functions** compiled into the runtime dispatcher (`systemManager.cpp`).

### Operator Precedence (CES)

1. **Highest:** Primary (literals, parens, member access `.`)
2. Unary (`-`, `not`)
3. Exponentiation (`**` if supported)
4. Multiplicative (`*`, `/`, `%`)
5. Additive (`+`, `-`)
6. Comparison (`<`, `>`, `<=`, `>=`, `==`, `!=`)
7. Logical AND (`and`)
8. **Lowest:** Logical OR (`or`)

### Type Coercion (ibValue Arithmetic)

| Operation | Rule |
|---|---|
| `number ± number` | Arithmetic; result is number |
| `string + string` | Concatenation; result is string |
| `string + number` | Convert number to string, concatenate |
| `date + number` | Add days to date; result is date |
| `date - date` | Difference in days; result is number |
| `boolean and boolean` | Logical AND |
| `boolean or boolean` | Logical OR |
| `undefined op anything` | Result is undefined (propagates) |
| `null op anything` | Result is null (propagates) |

### Literals

**Number:** `-123`, `3.14`, `1e-10` (IEEE 754 double)

**String:** `"double-quoted"`, `'single-quoted'`; `\"` escapes quote, `\\` escapes backslash

**Date:** `Date(2026, 5, 21)` or string parse `"2026-05-21"`

**Boolean:** `true`, `false`

**Null/Undefined:** `null`, `undefined`

**Array:** `[1, 2, 3]` or `New("Array")`

**Structure:** `{prop1: val1, prop2: val2}` or `New("Structure")`

---

## 6. Database Driver Capabilities Matrix

| Feature | Firebird | PostgreSQL | SQLite | MySQL | ODBC |
|---|---|---|---|---|---|
| **Mode** | Embedded | Remote | Single-file | Remote | Generic |
| **Transactions** | ACID, levels RC/RR | ACID, levels RU/RC/RR/SER | DEFERRED/IMMEDIATE/EXCLUSIVE | ACID, levels RU/RC/RR/SER | Depends on backend |
| **Connection string** | `firebird://file.fdb` | `postgresql://host:5432/db` | `sqlite://file.db` | `mysql://host:3306/db` | `odbc://DSN` |
| **BOOLEAN** | ✓ (mapped to SMALLINT) | ✓ native | ✓ (as INTEGER 0/1) | ✓ native | Varies |
| **NUMERIC(p,s)** | ✓ NUMERIC | ✓ NUMERIC | ✗ (uses REAL + rounding) | ✓ DECIMAL | Varies |
| **INT128** | ✓ INT128 (Firebird 4+) | ✗ (use NUMERIC) | ✗ | ✗ | ✗ |
| **Subqueries in FROM** | ✓ CTE + WITH | ✓ CTE + WITH | ✓ limited CTE | ✗ (MySQL 5.x) / ✓ (8.x) | Varies |
| **Row-level locking** | ✓ `WITH LOCK` | ✓ `FOR UPDATE` | ✗ (page-level) | ✓ implicit | Depends |
| **Foreign keys** | ✓ enforced | ✓ enforced | ✓ optional (pragma) | ✓ enforced | Depends |
| **Prepared statements** | ✓ native | ✓ native | ✓ native | ✓ native | ✓ (via ODBC) |
| **Stored procedures** | ✓ (not used by OES) | ✓ (not used by OES) | ✗ | ✓ (not used) | Depends |

**Abstraction layer:** `ibDatabaseLayer` and driver subclasses handle SQL dialect differences. Each driver implements `RunQuery(sql)`, `PrepareStatement(sql)`, `BeginTransaction()`, `Commit()`, `Rollback()` with identical semantics across backends.

---

## 7. Forms Model

### Rendering pipeline

**Design time:** XML layout (wxFormBuilder format) + module code (CES/VES) → stored in `ibValueMetaObjectFormBase`

**Runtime:**
```
Form descriptor (XML + bytecode)
  └─ ibValueForm (per-instance)
       ├─ control tree (22 visual types)
       ├─ property bindings (control ↔ script variable)
       ├─ event handlers (OnOpen, OnClose, button_Click, etc.)
       └─ module (containing the form script + local procedures)
```

### 22 Visual Controls

| Control | Role | Data binding |
|---|---|---|
| **Button** | Clickable; dispatches `OnClick` handler | Command (no data) |
| **CheckBox** | Boolean toggle | `Checked` (boolean) |
| **Choice** | Dropdown list (single select) | `Value` (Reference or scalar) |
| **ComboBox** | Dropdown + text input | `Value` + `Text` |
| **Form** | Container (root) | N/A (container) |
| **Frame** | Bordered container | N/A (container) |
| **Gauge** | Progress bar | `Value` (0–100) |
| **GridBox** | Multi-column data grid | Table (tabular section) |
| **HtmlBox** | Render HTML content | `HTML` (string) |
| **ListBox** | Vertical list (single/multi-select) | Array or Reference list |
| **Notebook** | Tab control | Active tab index |
| **RadioButton** | Mutually-exclusive option | `Value` (from enumeration) |
| **Slider** | Range slider | `Value` (min–max number) |
| **StaticText** | Read-only text label | Bound to script variable |
| **StaticLine** | Horizontal separator | N/A |
| **TableBox** | Flat-table control (like Excel sheet) | Tabular section |
| **TextBox** | Single-line or multi-line editor | `Value` (string) |
| **TextCtrl** | Legacy text input (wxTextCtrl) | `Value` (string) |
| **ToolBar** | Command bar | Button list + icons |
| **BoxSizer** | Horizontal/vertical layout box | Child arrangement |
| **GridSizer** | Grid-based layout | Child arrangement |
| **ChartBox** | Chart widget (bar, pie, line) | Table + column mappings |

### Form types

Each metadata object can declare multiple forms serving different roles:

| Form type | Purpose | When used |
|---|---|---|
| **ItemForm** | Single-item edit dialog | `OpenForm("Catalog.Products.ItemForm", ...);` with a ref; create new via wizard |
| **ListForm** | Browse/select multiple items | User clicks Catalog icon in GUI; SELECT list from table |
| **ChoiceForm** | Modal selector (dimmed list) | Script: `choice = Catalogs.Products.ChooseItem()` |
| **SelectionForm** | Multi-item picker | DataProcessor filters + reports use this |

### Form event lifecycle

```
OnOpen              — form instantiated, controls ready (load defaults)
  ↓
OnFormDataChange   — user edits (fires per keystroke/selection)
  ↓
Button_Click       — user clicks button
  ↓
OnBeforeWrite      — user clicks Save (veto via return false)
  ↓
Write()            — persist to DB (automatic, script can override)
  ↓
OnWrite            — post-save handlers
  ↓
OnClose            — form dismissed
```

### Module attachment

Every form owns an `ibValueModule` with script methods:
- `Procedure OnOpen()` — initialize form state
- `Procedure Button1_Click(sender)` — handler for Button1 click
- `Function ValidatePrice() Return value > 0 EndFunction` — custom validation
- Local variables and nested procedures in form scope

The form's module is a child of the owning object's module in the runtime parent chain.

---

## 8. Plugin ABI (Application Binary Interface)

### Versions

| ABI | Year | Status | Breaking change |
|---|---|---|---|
| v2 | 2024 | Deprecated | First public release; no longer used |
| v3 | 2025 | Legacy support | `prompt/completion` wire envelope |
| v4 | 2026 | Current | Envelope-based: `chat.send`, `chat.delta`, `agent.plan`, triple-review |

### ABI v4 Host Trampolines (C ABI)

| Trampoline | Purpose |
|---|---|
| `RegisterWebPane(paneId, descriptor)` | Plugin announces a dockable UI pane (used by AI Assistant) |
| `WebPaneSend(paneId, envelopeJson)` | Bidirectional envelope dispatch (chat.send, chat.delta, etc.) |
| `RegisterAIProvider(providerId, vtable)` | Plugin registers as a code-completion provider |
| `ReadPluginEnv(key, outBuf, bufLen)` | Read from plugin's scoped `.env` file (BYOK) |
| `MetaCreate(parentPath, kindCLSID, spec, outId)` | Create metadata object (permission-gated) |
| `MetaEdit(objectId, patchJson)` | Patch metadata object (permission-gated) |
| `MetaDelete(objectId)` | Delete metadata object (permission-gated) |

All return `int` (0 = success, non-zero = OES status code).

### Plugin discovery

Plugins are `.bundle` (macOS), `.dll` (Windows), `.so` (Linux) files in:
- `~/Library/Application Support/OES/plugins/` (macOS)
- `%APPDATA%\OES\plugins\` (Windows)
- `~/.local/share/oes/plugins/` (Linux)

At Designer startup, `ibPluginManager::Discover()` dlopen/LoadLibrary each plugin, calls `Plugin_OnLoad`, and registers trampolines.

### Plugin.env scoping (BYOK)

Each plugin has a private `.env` file at:
```
~/Library/Preferences/OES/plugins/<plugin-id>.env
mode 0600
```

Example (`aiBridge.env`):
```ini
PROTOCOL=pugimcp
PUGI_BASE_URL=https://anvil.tetracode.io/mcp/v1
PUGI_TENANT_TOKEN=cf_live_...
```

Keys are NOT added to process `setenv()`; they're injected only to the originating plugin via `ReadPluginEnv` trampoline.

---

## 9. Runtime Modes & Access Modes

### Run modes (ibRunMode enum)

| Mode | Executables | Sessions per process | GUI |
|---|---|---|---|
| **LAUNCHER** | launcher.exe | 0 (connection only) | wxWidgets (modal) |
| **DESIGNER** | designer.exe | 1 (no runtime) | wxWidgets (IDE) |
| **ENTERPRISE** | enterprise.exe | 1 (thick-client) | wxWidgets |
| **SERVICE** | daemon.exe, codeRunner.exe | 1 (headless) | None |
| **WEB_ENTERPRISE** | wenterprise-server.exe | N (HTTP per-cookie) | HTTP+JSON |

### Access modes (ibSession::AccessMode)

| Mode | Process | Session binding |
|---|---|---|
| **Single** | Desktop, daemon, codeRunner | One session per process; `Current()` returns singleton |
| **Shared** | Web server (wenterprise-server) | Per-thread binding via `ibSessionThreadBinding`; worker threads lease sessions from pool |

---

## 10. Git History & Milestones

### First commit
**e7bc657b** — "Initial release" (circa 2016–2017, archived history)

### Major architectural milestones

| Commit range | Event | Details |
|---|---|---|
| cd1ef91e | Accounting types land | ChartOfCharacteristicTypes, ChartOfAccounts, AccountingRegister implemented; balance/turnovers queries |
| 9831a03e (2026-05-10) | First-class lambdas | `Function(x, y) Return x+y EndFunction` syntax + bytecode support + debugger integration |
| 2f7cc80f (2026-05-10) | LINQ + closure capture | `Where/Select/OrderBy/GroupBy/Join/Skip/Take/Distinct` on iterables; outer-scope variable capture |
| c08a96c1 (2026-05-05) | Web session architecture | `ibSessionRegistry` + `ibSession` per-cookie model for wenterprise-server |
| 18c483d9 (2026-04-20) | Plugin ABI v4 initial | Envelope protocol, `MetaCreate/Edit/Delete` host trampolines, BYOK `.env` scoping |
| 824670cf (2026-05-15) | aiBridge plugin ships | First-party AI Assistant reference implementation (OpenAI + Anvil/Pugi MCP) |
| 975af347 (2026-05-20) | AI Assistant prod-grade | Full triple-review consensus, permission gates, history persistence |

### Major refactors

- **Session registry decoupling (2026-04):** Moved per-session user/frame ownership from `ibApplicationData` singleton to `ibSession` instances; enabled multi-user web mode
- **Plugin manager + ABI v4 (2026-04..05):** Replaced legacy v2 hook system with versioned C ABI; landed envelope protocol for AI integration
- **Runtime facade planning (in progress):** Decoupling compile state (shared descriptor) from per-session runtime (ProcUnit per session); unblocks concurrent session scaling
- **Accounting types WIP (2026-05):** Double-entry model landed in designer; SQL DDL and runtime bindings in active development

---

## Appendix: Glossary

| Term | Definition |
|---|---|
| **ABI** | Application Binary Interface; versioned contract between plugin and host (v4 current) |
| **AOT** | Ahead-of-time compilation; bytecode cache format (`byteCode_v5.blob`) |
| **Bytecode** | Compiled instruction stream; `ibByteCode` container, executed by `ibProcUnit` interpreter |
| **CES** | C-style script syntax (`if (x) { … }`); default since 2026-05-10 |
| **Catalog** | Master reference data type; hierarchical; e.g., Products, Vendors |
| **CLSID** | Class ID; numeric discriminator for metadata type (MD_CAT=1, MD_DOC=2, etc.) |
| **Closure** | Capture of outer function's local variables inside a lambda body |
| **Database layer** | `ibDatabaseLayer` abstraction; 5 driver implementations (Firebird, PostgreSQL, SQLite, MySQL, ODBC) |
| **Document** | Transactional record type; immutable when posted; triggers register postings |
| **Enumeration** | Closed set of symbolic values; non-hierarchical |
| **Form** | UI container + event handlers; ItemForm/ListForm/ChoiceForm/SelectionForm variants |
| **Lambda** | Anonymous function/procedure value; assignable to variables, callable at runtime |
| **LINQ** | Language Integrated Query; Where/Select/OrderBy/GroupBy/Join/Skip/Take/Distinct methods on iterables |
| **Metadata** | Compile-time schema (Catalogs, Documents, forms, modules); stored in `sys_metadata` table |
| **Module** | Scope for procedures/functions; every Catalog/Document/form owns a module |
| **Opcode** | Bytecode instruction (69 types: OPER_ADD, OPER_CALL, etc.) |
| **Permission gate** | 4-mode (Ask/AllowSession/AllowAlways/Deny) prompt for AI agent metadata mutations |
| **ProcUnit** | Interpreter (`ibProcUnit`); executes bytecode + maintains call stack (runtime) |
| **Register** | Data aggregation type (InformationRegister / AccumulationRegister) |
| **Run mode** | Process execution context (LAUNCHER/DESIGNER/ENTERPRISE/SERVICE/WEB_ENTERPRISE) |
| **Session** | Per-user runtime state (`ibSession`); owns metadata root module, forms, databases |
| **TabularSection** | Child table attached to Document/DataProcessor (e.g., Invoice.Items) |
| **VES** | Visual-Basic-style syntax (`If x Then … EndIf`); legacy before 2026-05-10 |

---

**Document version:** 1.0  
**Last updated:** 2026-05-21  
**Maintained by:** OES Platform Team

