f f   # OES Architecture

## Table of Contents

1. [System Overview](#system-overview)
2. [Layer Descriptions](#layer-descriptions)
3. [Module Descriptions](#module-descriptions)
4. [Bytecode Engine](#bytecode-engine)
5. [Metadata System](#metadata-system)
6. [Database Abstraction](#database-abstraction)
7. [Form System](#form-system)
8. [Debugger Architecture](#debugger-architecture)
9. [Key Data Flows](#key-data-flows)

---

## System Overview

```
┌──────────────────────────────────────────────────────────────┐
│                      Executables                             │
│  designer.exe   enterprise.exe   launcher.exe   daemon.exe   │
│  codeRunner.exe   classChecker.exe                           │
└────────────┬─────────────────┬────────────────┬─────────────┘
             │                 │                │
             ▼                 ▼                ▼
┌────────────────────┐  ┌────────────────────────────────────┐
│   frontend.dll     │  │           backend.dll              │
│                    │  │                                    │
│  ibValueForm       │◄─►  ibApplicationData (singleton)     │
│  ibVisualHost      │  │  ibMetaDataConfiguration           │
│  22 Controls       │  │  ibCompileCode / ibProcUnit        │
│  ibMainFrame       │  │  ibDatabaseLayer (+ 5 drivers)     │
│  Property editor   │  │  ibDebuggerServer                  │
└────────────────────┘  └────────────────┬───────────────────┘
                                         │
                        ┌────────────────▼────────────────────┐
                        │         Database tier                │
                        │  Firebird  PostgreSQL  SQLite        │
                        │  MySQL     ODBC                      │
                        └─────────────────────────────────────┘
```

Communication between `frontend.dll` and `backend.dll` goes through abstract C++ interfaces exported from `backend.dll`. The frontend never accesses database drivers or the compiler directly.

---

## Layer Descriptions

### Application Layer (executables)

Each executable links against both DLLs and provides a `wxApp` subclass that selects the run mode:

| Executable | Run Mode (`ibRunMode`) | Purpose |
|---|---|---|
| `launcher.exe` | `eLAUNCHER_MODE` | Connection chooser; creates/selects database |
| `designer.exe` | `eDESIGNER_MODE` | Full IDE — metadata editor, form designer, debugger client |
| `enterprise.exe` | `eENTERPRISE_MODE` | Runtime; runs the compiled configuration |
| `daemon.exe` | `eSERVICE_MODE` | Headless background service |
| `codeRunner.exe` | `eSERVICE_MODE` | Executes a single script module |
| `classChecker.exe` | — | Validates metadata consistency |

### Backend Layer (`backend.dll`)

The backend is the core engine. It is self-contained — no GUI dependencies. Key singletons:

- **`ibApplicationData`** (`src/engine/backend/appData.h`) — master runtime object; holds database connection, user session, run mode, and application metadata. Accessed via the `appData` macro.
- **`ibMetaDataConfiguration`** (`src/engine/backend/metadataConfiguration.h`) — loads, saves, and manages the metadata tree (all business objects). Accessed via `activeMetaData`.
- **`ibDebuggerServer`** (`src/engine/backend/debugger/debugServer.h`) — TCP server that accepts designer connections and relays debugger events.

### Frontend Layer (`frontend.dll`)

The frontend owns all wxWidgets UI code. It implements the form system and document/view framework:

- **`ibValueForm`** — the runtime representation of an open form; holds the control tree and responds to user events
- **`ibVisualHost`** — the wxWindow that renders form controls and routes input
- **`ibMainFrame`** — main application window; manages open documents

---

## Module Descriptions

### `src/engine/backend/compiler/`

| File | Class | Role |
|---|---|---|
| `translateCode.h/cpp` | `ibTranslateCode` | Lexer: tokenises source text into `ibLexem` stream |
| `compileCode.h/cpp` | `ibCompileCode` | Parser and code generator: consumes lexemes, emits `ibByteCode` |
| `byteCode.h` | `ibByteCode`, `ibByteUnit` | Bytecode container: array of `ibByteUnit` instructions |
| `procUnit.h/cpp` | `ibProcUnit` | Interpreter: executes `ibByteCode` against a variable stack |
| `procContext.h/cpp` | `ibRunContext` | Execution context: local variable frame, call stack |
| `value.h/cpp` | `ibValue` | Universal value type (Undefined, Boolean, Number, Date, String, Reference) |
| `codeDef.h` | enums | Opcode (`OPER_*`) and keyword (`KEY_*`) definitions |

### `src/engine/backend/databaseLayer/`

| File | Class | Role |
|---|---|---|
| `databaseLayer.h/cpp` | `ibDatabaseLayer` | Abstract base: Open/Close/RunQuery/PrepareStatement/Transactions |
| `databaseResultSet.h/cpp` | `ibDatabaseResultSet` | Abstract result set cursor |
| `preparedStatement.h` | `ibPreparedStatement` | Abstract prepared statement |
| `firebird/` | `ibFirebirdDatabaseLayer` | Firebird 3/4 driver |
| `postgres/` | `ibPostgresDatabaseLayer` | PostgreSQL driver |
| `sqllite/` | `ibSqliteDatabaseLayer` | SQLite 3 driver (note: directory named `sqllite`) |
| `mysql/` | `ibMysqlDatabaseLayer` | MySQL driver |
| `odbc/` | `ibOdbcDatabaseLayer` | ODBC generic driver |

### `src/engine/backend/metaCollection/`

Metadata object hierarchy. Every business object type extends `ibValueMetaObject` (defined in `metaObject.h`), which itself extends `ibValue` so metadata objects can be passed as script values.

### `src/engine/backend/debugger/`

| File | Class | Role |
|---|---|---|
| `debugServer.h/cpp` | `ibDebuggerServer` | TCP server accepting designer connections |
| `debugClient.h/cpp` | `ibDebuggerClient` | Client-side connection (runs in designer) |
| `debugClientBridge.h/cpp` | `ibDebuggerClientBridge` | Bridge between client socket and UI |
| `debugDefs.h` | enums | `CommandId`, `EventId`, `ConnectionType` |

### `src/engine/backend/system/`

| File | Class | Role |
|---|---|---|
| `systemManager.h/cpp` | `ibSystemManager` | Built-in function dispatcher; registers 88 built-in functions |
| `systemEnum.h` | enums | System-level enumeration constants |

### `src/engine/frontend/visualView/`

| File/Dir | Content |
|---|---|
| `ctrl/form.h/cpp` | `ibValueForm`, form control collection, action wiring |
| `ctrl/control.h/cpp` | `ibControlFrame` base class for all controls |
| `ctrl/widgets.h` | Declarations for all control types |
| `visualHost.h/cpp` | `ibVisualHost` — wxWindow rendering surface |
| `visualHostClient.h/cpp` | Client-side form binding |
| `ctrl/` | Individual control implementations (see Form System) |

---

## Bytecode Engine

### Compiler Pipeline

```
Source text (wxString)
        │
        ▼
 ibTranslateCode::Translate()
   Lexer: produces vector<ibLexem>
   Each ibLexem: { type, keyword/delimiter number, string data, ibValue, line, position }
        │
        ▼
 ibCompileCode::Compile()
   Recursive-descent parser
   Emits ibByteUnit records into ibByteCode
   Resolves forward function references in a second pass
        │
        ▼
 ibByteCode
   vector<ibByteUnit>   — instruction stream
   map<wxString, ibByteFunction>  — function table (name → code line)
   vector<ibValue>      — constant pool
   vector<ibValue*>     — variable table
        │
        ▼
 ibProcUnit::Execute()
   Stack machine with ibRunContext frames
   Dispatches on ibByteUnit::m_numOper
```

### Opcode Categories

Opcodes are defined as plain integer constants in `src/engine/backend/compiler/codeDef.h`. The 66 opcodes fall into these groups:

| Category | Opcodes |
|---|---|
| Arithmetic | `OPER_ADD`, `OPER_SUB`, `OPER_MULT`, `OPER_DIV`, `OPER_MOD`, `OPER_INVERT` |
| Comparison | `OPER_GT`, `OPER_EQ`, `OPER_LS`, `OPER_GE`, `OPER_LE`, `OPER_NE` |
| Logical | `OPER_NOT`, `OPER_AND`, `OPER_OR` |
| Control flow | `OPER_GOTO`, `OPER_IF`, `OPER_FOR`, `OPER_FOREACH`, `OPER_IN`, `OPER_NEXT`, `OPER_NEXT_ITER` |
| Variables | `OPER_LET`, `OPER_CONST`, `OPER_CONSTN`, `OPER_SET`, `OPER_SETREF`, `OPER_SETCONST` |
| Functions | `OPER_FUNC`, `OPER_ENDFUNC`, `OPER_CALL`, `OPER_CALL_M`, `OPER_RET` |
| Arrays | `OPER_GET_ARRAY`, `OPER_SET_ARRAY`, `OPER_CHECK_ARRAY`, `OPER_SET_ARRAY_SIZE`, `OPER_ENTER_A`, `OPER_GET_A`, `OPER_SET_A` |
| Objects | `OPER_NEW`, `OPER_SET_TYPE` |
| Exceptions | `OPER_TRY`, `OPER_ENDTRY`, `OPER_RAISE`, `OPER_RAISE_T` |
| Optimised const variants | `OPER_ADDCONS`, `OPER_SUBCONS`, `OPER_MULTCONS`, `OPER_DIVCONS`, `OPER_MODCONS`, `OPER_GTCONS`, etc. |

Each opcode has type-specialised variants selected by adding `TYPE_DELTA1` (number), `TYPE_DELTA2` (string), `TYPE_DELTA3` (date), or `TYPE_DELTA4` (boolean) to the base opcode.

### Execution Model

`ibProcUnit` maintains a linked list of parent `ibProcUnit` objects representing the module scope chain. Each call creates an `ibRunContext` holding the local variable array. Exceptions use the throw-by-pointer pattern: `throw(new ibBackendInterruptException)`, caught as `catch(const ibBackendException*)`.

### Keyword Inventory

44 keywords defined in `KEY_*` enumerators (e.g., `KEY_IF`, `KEY_FOR`, `KEY_FOREACH`, `KEY_PROCEDURE`, `KEY_FUNCTION`, `KEY_TRY`, `KEY_EXCEPT`, `KEY_ENDTRY`, `KEY_RAISE`, `KEY_NEW`, etc.) plus preprocessor keywords (`KEY_DEFINE`, `KEY_UNDEF`, `KEY_IFDEF`, `KEY_IFNDEF`, `KEY_REGION`, `KEY_ENDREGION`). Two syntax modes allow English (`If…Then…EndIf`) and abbreviated forms.

---

## Metadata System

### ibValueMetaObject

`ibValueMetaObject` (defined in `src/engine/backend/metaCollection/metaObject.h`) is the abstract base for all configuration objects. It extends both `ibValue` (so it can be assigned to script variables) and `ibPropertyObjectHelper` (so properties appear in the designer's object inspector).

Key attributes:
- `ibMetaID m_metaId` — numeric identifier within the configuration
- `ibGuid m_metaGuid` — globally-unique GUID
- `wxString GetName()` / `GetSynonym()` — developer name and user-visible synonym
- `ibMetaData* m_metaData` — back-pointer to the owning metadata container

### Eight Business Object Types

| Type | Class | File |
|---|---|---|
| Catalog | `ibValueMetaObjectCatalog` | `metaCollection/partial/catalog.h` |
| Document | `ibValueMetaObjectDocument` | `metaCollection/partial/document.h` |
| Enumeration | `ibValueMetaObjectEnumeration` | `metaCollection/partial/enumeration.h` |
| Constant | `ibValueMetaObjectConstant` | `metaCollection/partial/constant.h` |
| InformationRegister | `ibValueMetaObjectInformationRegister` | `metaCollection/partial/informationRegister.h` |
| AccumulationRegister | `ibValueMetaObjectAccumulationRegister` | `metaCollection/partial/accumulationRegister.h` |
| DataProcessor | `ibValueMetaObjectDataProcessor` | `metaCollection/partial/dataProcessor.h` |
| Report | `ibValueMetaObjectReport` | `metaCollection/partial/dataReport.h` |

### Inheritance Chain (simplified)

```
ibValue
  └─ ibValueMetaObject
       ├─ ibValueMetaObjectAttribute         (scalar field descriptor)
       │    └─ ibValueMetaObjectConstant
       ├─ ibValueMetaObjectRecordData        (record-based objects)
       │    ├─ ibValueMetaObjectRecordDataMutableRef
       │    │    └─ ibValueMetaObjectDocument
       │    ├─ ibValueMetaObjectRecordDataHierarchyMutableRef
       │    │    └─ ibValueMetaObjectCatalog
       │    └─ ibValueMetaObjectRecordDataExt
       │         ├─ ibValueMetaObjectDataProcessor
       │         └─ ibValueMetaObjectReport
       ├─ ibValueMetaObjectRegisterData
       │    ├─ ibValueMetaObjectInformationRegister
       │    └─ ibValueMetaObjectAccumulationRegister
       └─ ibValueMetaObjectRecordDataEnumRef
            └─ ibValueMetaObjectEnumeration
```

### Configuration Persistence

`ibMetaDataConfiguration::LoadDatabase()` / `SaveDatabase()` serialise the entire metadata tree to/from the system database using `ibReaderMemory` / `ibWriterMemory` binary streams. Each metadata class registers a `ibClassID` CLSID (e.g. `MD_CAT` for Catalog) used as a type discriminator during loading.

---

## Database Abstraction

### Class Hierarchy

```
ibDatabaseLayer  (abstract — databaseLayer.h)
  ├─ ibFirebirdDatabaseLayer   (databaseLayer/firebird/)
  ├─ ibPostgresDatabaseLayer   (databaseLayer/postgres/)
  ├─ ibSqliteDatabaseLayer     (databaseLayer/sqllite/)
  ├─ ibMysqlDatabaseLayer      (databaseLayer/mysql/)
  └─ ibOdbcDatabaseLayer       (databaseLayer/odbc/)

ibDatabaseResultSet  (abstract)
  ├─ ibFirebirdResultSet
  ├─ ibPostgresResultSet
  ├─ ibSqliteResultSet
  ├─ ibMysqlPreparedStatementResultSet
  └─ ibOdbcResultSet

ibPreparedStatement  (abstract)
  ├─ ibFirebirdPreparedStatement
  ├─ ibPostgresPreparedStatement
  ├─ ibSqlitePreparedStatement
  ├─ ibMysqlPreparedStatement
  └─ ibOdbcPreparedStatement
```

### Access Pattern

All database access goes through the `db_query` macro, which calls `ibApplicationData::GetDatabaseLayer()` and returns the active `ibDatabaseLayer*`. Example:

```cpp
ibDatabaseResultSet* rs = db_query->RunQueryWithResults(
    wxT("SELECT * FROM %s WHERE guid = ?"), table_name);
```

Each driver folder contains an `engine/` subdirectory with the vendored native client library.

---

## Form System

### ibValueForm Hierarchy

```
ibBackendControlFrame  (abstract interface — backend_form.h)
  └─ ibBackendValueForm  (abstract — backend_form.h)
       └─ ibValueForm  (concrete — frontend/visualView/ctrl/form.h)
```

`ibValueForm` owns the complete control tree for one open form. It is created by `ibBackendValueForm::CreateNewForm()`, which resolves the `ibValueMetaObjectFormBase` descriptor from metadata and instantiates the correct form type.

### 22 Visual Controls

Implemented in `src/engine/frontend/visualView/ctrl/`:

| Control | File |
|---|---|
| Button | `button.cpp` |
| CheckBox | `checkbox.cpp` |
| Choice | `choice.cpp` |
| ComboBox | `combobox.cpp` |
| Form (root) | `form.cpp` |
| Frame | `frame.cpp` |
| Gauge | `gauge.cpp` |
| GridBox | `gridBox.cpp/.h` |
| HtmlBox | `htmlBox.cpp/.h` |
| ListBox | `listbox.cpp` |
| Notebook | `notebook.cpp/.h` |
| RadioButton | `radiobutton.cpp` |
| Slider | `slider.cpp` |
| StaticText | `statictext.cpp` |
| StaticLine | `staticline.cpp` |
| TableBox | `tableBox.cpp/.h` |
| TextBox | `textBox.cpp/.h` |
| TextCtrl | `textctrl.cpp` |
| ToolBar | `toolBar.cpp/.h` |
| BoxSizer | `boxsizer.cpp` |
| GridSizer | `gridsizer.cpp` |
| ChartBox | `chartBox.cpp/.h` |

### Form Rendering

`ibVisualHost` is the wxWindow that owns the visual surface. It receives layout instructions from `ibValueForm` and positions wxWidgets controls. `ibVisualHostClient` handles the reverse channel: user input events travel from wxWidgets up through the host into the form's script event handlers.

---

## Debugger Architecture

### Client-Server Model

```
enterprise.exe                     designer.exe
      │                                  │
ibDebuggerServer ◄──── TCP ──────► ibDebuggerClient
(src/engine/backend/debugger/)     (src/engine/frontend/ or designer/)

Default port: 1650  (defined as defaultDebuggerPort in debugDefs.h)
```

### Connection Types (`ConnectionType` enum)

| Value | Purpose |
|---|---|
| `ConnectionType_Scanner` | Designer scanning for debuggable processes |
| `ConnectionType_Waiter` | Enterprise waiting for a connection |
| `ConnectionType_Debugger` | Active debug session |

### Command Flow

The designer sends `CommandId` packets; enterprise responds with `EventId` packets:

```
Designer                           Enterprise
   │  CommandId_VerifyConnection      │
   │ ────────────────────────────────►│
   │  CommandId_SetConnectionType     │
   │ ◄────────────────────────────────│
   │  CommandId_StartSession          │
   │ ────────────────────────────────►│
   │  EventId_SessionStart            │
   │ ◄────────────────────────────────│
   │  CommandId_ToggleBreakpoint      │
   │ ────────────────────────────────►│
   │  CommandId_Continue              │
   │ ────────────────────────────────►│
   │  EventId_EnterLoop               │
   │ ◄────────────────────────────────│
   │  CommandId_GetArrayBreakpoint    │
   │  CommandId_SetLocalVariables     │
   │  CommandId_SetStack              │
   │ ◄────────────────────────────────│
```

The server runs each connection as a `wxThread` (`ibDebuggerServer::ibDebuggerServerConnection`). Raw binary packets are sent via `SendCommand` / `RecvCommand`.

---

## Key Data Flows

### User Login

```
launcher.exe
  └─ ibApplicationData::CreateServerAppDataEnv(mode, server, port, user, pwd, db, locale)
       └─ ibDatabaseLayer::Open(server, port, db, user, pwd)
            └─ ibApplicationData::Connect(userName, userPassword)
                 └─ ibApplicationData::AuthenticationAndSetUser()
                      └─ compare MD5(inputPassword) == stored hash
                           └─ StartSession() — inserts row into session table
                                └─ launch enterprise.exe / designer.exe via RunApplication()
```

### Form Open

```
Script: OpenForm("Catalog.Products.ListForm")
  └─ ibSystemManager dispatches to form-open built-in
       └─ ibBackendValueForm::CreateNewForm(metaFormObject, ownerControl, srcObject)
            └─ ibValueForm constructed, control tree built from metadata
                 └─ ibBackendMainFrame::CreateDocumentWindow()
                      └─ ibVisualHost created as wxWindow child
                           └─ controls instantiated and laid out
                                └─ OnOpen() script handler called via ibProcUnit::CallAsProc()
```

### Document Save

```
Script: Write()   (or user presses Save)
  └─ ibValueRecordDataObject::Write()
       └─ ibDatabaseLayer::BeginTransaction()
            └─ generate SQL INSERT/UPDATE for main table
                 └─ iterate tabular sections → INSERT/UPDATE child rows
                      └─ post RegisterRecords() for registers if Document
                           └─ ibDatabaseLayer::Commit()
                                └─ OnWrite() script handler called
```
