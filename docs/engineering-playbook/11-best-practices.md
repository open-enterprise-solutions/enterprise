# 11. Best Practices

## C++17 — the project standard

### Compiler — warnings as errors

```cmake
# CMakeLists.txt
if(MSVC)
    target_compile_options(oes PRIVATE
        /W4         # High warning level
        /WX         # Warnings as errors
        /std:c++17  # C++17
        /permissive- # Strict standard conformance
    )
else()
    target_compile_options(oes PRIVATE
        -Wall -Wextra -Wpedantic
        -Werror                # Warnings as errors
        -std=c++17
        -Wno-unused-parameter  # wxWidgets generates these warnings
    )
endif()
```

### Compiler flags in Visual Studio

In `*.vcxproj` or via Property Sheets:
```xml
<ClCompile>
  <WarningLevel>Level4</WarningLevel>
  <TreatWarningAsError>true</TreatWarningAsError>
  <LanguageStandard>stdcpp17</LanguageStandard>
  <ConformanceMode>true</ConformanceMode>
</ClCompile>
```

---

## Memory management

### RAII — the core principle

Resources (memory, files, DB connections, mutexes) must be acquired in the constructor and released in the destructor. No bare `new`/`delete` in business logic.

```cpp
// BAD — bare new/delete, leaks on exceptions
class ReportGenerator {
    Document* m_doc;
public:
    ReportGenerator() : m_doc(new Document()) {}
    ~ReportGenerator() { delete m_doc; }  // Won't run if constructor throws!

    void generate() {
        Image* img = new Image("logo.png");
        renderPage(img);   // If it throws — img leaks
        delete img;
    }
};

// GOOD — RAII through smart pointers
class ReportGenerator {
    std::unique_ptr<Document> m_doc;
public:
    ReportGenerator() : m_doc(std::make_unique<Document>()) {}
    // Destructor generated automatically

    void generate() {
        auto img = std::make_unique<Image>("logo.png");
        renderPage(*img);
        // img is destroyed automatically when leaving scope
    }
};
```

### Smart pointers — selection rule

```cpp
// std::unique_ptr — single owner (default)
std::unique_ptr<DatabaseConnection> conn = createConnection();

// std::shared_ptr — shared ownership (only when truly required)
std::shared_ptr<Configuration> config = Configuration::load();
// Passed to multiple services:
UserService userSvc(config);
ReportService reportSvc(config);

// std::weak_ptr — observer without ownership (avoids cycles)

// BAD — if a child holds a shared_ptr to its parent
// and the parent holds a shared_ptr to the child — a cycle forms,
// and neither object is ever freed.
class NodeBad {
    std::shared_ptr<Node> m_parent;  // cyclic reference!
};

// GOOD — weak_ptr does not increment the reference count,
// so no cycle forms and memory is freed correctly.
class Node {
    std::weak_ptr<Node> m_parent;    // observer without ownership
};

// Raw pointers — ONLY for non-owning references
void processDocument(Document* doc) {  // OK — does not own
    // doc — temporary reference, lifetime managed by the caller
}
```

### No `new` outside factories and make_*

```cpp
// BAD — bare new in business logic
ibDatabaseLayer* db = new ibDatabaseLayerFirebird();
m_services.push_back(new UserService(db));

// GOOD
auto db = std::make_unique<ibDatabaseLayerFirebird>();
m_services.push_back(std::make_unique<UserService>(db.get()));

// GOOD — factory function that creates an ibDatabaseLayer of the right type
// (ibApplicationData in appData.cpp plays this role in the real code)
std::unique_ptr<ibDatabaseLayer> createDatabase(DatabaseType type) {
    switch (type) {
        case DatabaseType::Firebird:   return std::make_unique<ibDatabaseLayerFirebird>();
        case DatabaseType::PostgreSQL: return std::make_unique<ibDatabaseLayerPostgres>();
        case DatabaseType::SQLite:     return std::make_unique<ibDatabaseLayerSQLite>();
        default: throw ibBackendCoreException("Unknown database type");
    }
}
```

---

## Error handling

### Empty catch blocks — FORBIDDEN

```cpp
// BAD — error swallowed (common issue in OES)
try {
    m_dbLayer->connect(config);
} catch (...) {
    // empty
}

// GOOD — minimum: log and rethrow
try {
    m_dbLayer->connect(config);
} catch (const DatabaseException& e) {
    wxLogError("Database connection failed: %s", e.what());
    throw;  // or handle and return false
} catch (const std::exception& e) {
    wxLogError("Unexpected error during DB connect: %s", e.what());
    throw;
}

// GOOD — when you really must swallow (rare case)
try {
    // Non-critical operation (e.g. log flush)
    m_logger->flush();
} catch (const std::exception& e) {
    // Intentionally ignored: logging must not bring down the app
    // IMPORTANT: leave a comment explaining why ignore is acceptable
    (void)e;  // Explicitly mark the variable as intentionally unused
}
```

### Exceptions vs return codes

```cpp
// Exceptions — for exceptional situations (unexpected errors)
// Return codes — for expected outcomes

// GOOD — exception for an unexpected failure
DatabaseConnection openConnection(const Config& config) {
    // The connection MUST open, otherwise it's a program/config bug
    if (!connect(config.host, config.port)) {
        throw DatabaseException("Cannot connect to " + config.host);
    }
    return DatabaseConnection{...};
}

// GOOD — std::optional for "may not be found"
std::optional<User> findUserByEmail(const std::string& email) {
    // A user may not exist — that's normal
    auto result = m_db->query("SELECT ...", {email});
    if (result.isEmpty()) return std::nullopt;
    return User::fromRow(result.firstRow());
}

// GOOD — bool for "success/failure" of simple operations
bool saveDocument(const Document& doc) {
    try {
        m_db->execute("UPDATE ...", doc.toParams());
        return true;
    } catch (const DatabaseException& e) {
        wxLogError("Save failed: %s", e.what());
        return false;
    }
}
```

### OES exception hierarchy

OES uses its own exception hierarchy with the `ib` prefix:

```cpp
// Real OES exceptions (src/engine/backend/)

// ibBackendCoreException — engine base exception
// Thrown on compiler, DB, metadata errors
try {
    ibPreparedStatement* stmt = db->PrepareStatement(sql);
    stmt->SetParamInt(1, id);
    stmt->RunQuery();
} catch (const ibBackendCoreException& e) {
    wxLogError("Backend error: %s", e.what());
    // ibBackendCoreException carries an error code and description
}

// ibBackendInterruptException — script execution interrupted
// Thrown when ibProcUnit (the bytecode interpreter) is forcibly stopped
try {
    procUnit->Execute(byteCode);
} catch (const ibBackendInterruptException&) {
    // User pressed "Stop" or hit a timeout — not a program error
    wxLogMessage("Script execution interrupted by user");
}

// New modules may add specialized exceptions derived from ibBackendCoreException
```

---

## Logging

### wxLog — structured logging

```cpp
// BAD — std::cout in production code
std::cout << "User logged in: " << username << std::endl;
printf("Error: %s\n", error.c_str());

// GOOD — wxLog
wxLogMessage("User logged in: %s", username);     // INFO
wxLogWarning("Slow query (%ldms): %s", ms, sql);  // WARNING
wxLogError("Failed to save document: %s", err);   // ERROR

// For debug information (Debug build only)
wxLogDebug("Processing record id=%ld, type=%s", id, type);
```

### Log levels

```cpp
// src/engine/backend/utils/logger.h  (or similar path)
// We use wxLog with a configurable level

// config.ini:
// [app]
// log_level=info    (production)
// log_level=debug   (development)

class AppLogger {
public:
    static void Configure(const wxString& level, const wxString& logFile) {
        // Configure wxLogStderr + wxLogFile
        auto* fileLog = new wxLogFile(logFile);
        // SetActiveTarget() returns the previously active logger —
        // it must be deleted manually to avoid a memory leak.
        delete wxLog::SetActiveTarget(fileLog);

        if (level == "debug") {
            wxLog::SetLogLevel(wxLOG_Debug);
        } else if (level == "warning") {
            wxLog::SetLogLevel(wxLOG_Warning);
        } else {
            wxLog::SetLogLevel(wxLOG_Message);  // info
        }
    }
};
```

### What must be logged

```cpp
// Application start and stop
wxLogMessage("[STARTUP] OES v%s starting", OES_VERSION_STRING);
wxLogMessage("[SHUTDOWN] OES stopping, uptime=%lds", uptime);

// DB connection
wxLogMessage("[DB] Connected to %s:%d database '%s'",
    config.host, config.port, config.database);
wxLogError("[DB] Connection failed: %s", error);

// Login attempts (audit log)
wxLogMessage("[AUDIT] Login %s for user '%s' from %s",
    success ? "SUCCESS" : "FAILED", username, hostname);

// Data changes (audit log)
wxLogMessage("[AUDIT] %s record id=%ld in table '%s' by user '%s'",
    operation, recordId, tableName, currentUser);

// Critical errors
wxLogError("[CRITICAL] Unexpected exception: %s\nStack: %s",
    e.what(), stackTrace);
```

---

## Module architecture

### Separation of concerns

```cpp
// BAD — one class does everything
class MainWindow {
    void onSaveButtonClick() {
        // Builds SQL right here
        wxString sql = "UPDATE users SET name = '" + m_nameEdit->GetValue() + "'";
        // Executes the query
        m_db->Execute(sql);
        // Sends an email
        sendEmail("admin@company.com", "Record saved");
        // Updates the status bar
        m_statusBar->SetStatusText("Saved");
    }
};

// GOOD — separated: UI / Service / Repository
class UserService {
public:
    void saveUser(const UserData& data) {
        validate(data);               // Validation — here
        m_userRepo->save(data);       // Persistence — in the repository
        m_auditLog->recordSave(data); // Audit — in a separate service
    }
private:
    std::unique_ptr<IUserRepository> m_userRepo;
    std::unique_ptr<IAuditLog> m_auditLog;
};

class MainWindow {
    void onSaveButtonClick() {
        UserData data = gatherFormData();
        if (m_userService->saveUser(data)) {
            m_statusBar->SetStatusText("Saved");
        }
    }
};
```

### Interfaces (abstract classes) for dependencies

```cpp
// OES uses ibDatabaseLayer as the abstract base class
// (src/engine/backend/databaseLayer/databaseLayer.h)
//
// Concrete implementations:
//   ibDatabaseLayerFirebird  — Firebird (primary DBMS)
//   ibDatabaseLayerPostgres  — PostgreSQL
//   ibDatabaseLayerSQLite    — SQLite (embedded DB)
//   ibDatabaseLayerMySQL     — MySQL
//   ibDatabaseLayerODBC      — ODBC (universal driver)
//
// Polymorphism: code works with ibDatabaseLayer* without knowing the concrete DBMS

// Dependency through the constructor (Dependency Injection)
// ibApplicationData (appData.cpp) creates an ibDatabaseLayer of the right type
// and passes it to the rest of the components
class ibValueMetaObject {
public:
    explicit ibValueMetaObject(ibDatabaseLayer* db)
        : m_db(db) {}
protected:
    ibDatabaseLayer* m_db;  // does not own — lifetime managed by ibApplicationData
};

// Example creation through ibApplicationData
// ibApplicationData* appData = ibApplicationData::Get();
// ibDatabaseLayer* db = appData->GetDatabaseLayer();
```

---

## Project structure

### File and class naming

| What | Style | Example |
|-----|-------|--------|
| Files `.cpp`/`.h` | PascalCase | `DatabaseLayer.cpp`, `UserService.h` |
| Classes | PascalCase | `DatabaseLayer`, `ReportGenerator` |
| Methods | camelCase | `connectToDatabase()`, `getUserById()` |
| Member variables | `m_` prefix + camelCase | `m_dbLayer`, `m_userName` |
| Static members | `s_` prefix + camelCase | `s_instance`, `s_logger` |
| Constants (compile-time) | UPPER\_SNAKE\_CASE | `MAX_RETRY_COUNT`, `DEFAULT_PORT` |
| Enums (enum class) | PascalCase + PascalCase values | `DatabaseType::Firebird` |
| Namespaces | snake\_case | `oes::database`, `oes::ui` |
| Macros | UPPER\_SNAKE\_CASE | `OES_VERSION_STRING` |
| DB tables and columns | UPPER\_SNAKE\_CASE | `USER_PROFILES`, `CREATED_AT` |

### File and class size

- **Maximum ~500 lines** per `.cpp` file. Larger — split it
- One class per file (exception: small helper classes)
- One responsibility per file: `.h` declares, `.cpp` defines

### Headers

```cpp
// GOOD — re-inclusion guard
#pragma once

// Or classic include guards
#ifndef OES_DATABASE_LAYER_H
#define OES_DATABASE_LAYER_H
// ...
#endif

// Include order inside a .cpp file:
// 1. The class's own .h
#include "DatabaseLayer.h"

// 2. Headers from the same project
#include "core/Exceptions.h"
#include "utils/Logger.h"

// 3. Third-party libraries
#include <wx/wx.h>
#include <ibase.h>     // Firebird

// 4. Standard library
#include <memory>
#include <string>
#include <vector>
```

---

## Database

### Parameterized queries — always

```cpp
// BAD — string concatenation (SQL injection)
wxString sql = wxString::Format(
    "SELECT * FROM DOCUMENTS WHERE TITLE = '%s' AND USER_ID = %d",
    title, userId
);
m_db->Execute(sql);

// GOOD — ibPreparedStatement with SetParamString/SetParamInt (the OES way)
// ibDatabaseLayer* m_db — class field initialized through ibApplicationData
ibPreparedStatement* stmt = m_db->PrepareStatement(
    "SELECT * FROM DOCUMENTS WHERE TITLE = ? AND USER_ID = ?"
);
stmt->SetParamString(1, title);
stmt->SetParamInt(2, userId);
ibDatabaseResultSet* rs = stmt->RunQuery();
// Process rs, then release it
```

### Transactions through RAII

OES provides `ibTransactionGuard` (declared in `commonObject.h`) — an RAII transaction wrapper:

```cpp
// GOOD — ibTransactionGuard: automatic rollback on scope exit
// ibTransactionGuard tx(db) — starts a transaction
// tx.Commit()               — commits the transaction
// ~ibTransactionGuard()     — rolls back if Commit() was not called

void transferData(ibDatabaseLayer* db, int fromId, int toId, int amount) {
    ibTransactionGuard tx(db);  // Begin transaction

    ibPreparedStatement* stmt1 = db->PrepareStatement(
        "UPDATE ACCOUNTS SET BALANCE = BALANCE - ? WHERE ID = ?");
    stmt1->SetParamInt(1, amount);
    stmt1->SetParamInt(2, fromId);
    stmt1->RunQuery();

    ibPreparedStatement* stmt2 = db->PrepareStatement(
        "UPDATE ACCOUNTS SET BALANCE = BALANCE + ? WHERE ID = ?");
    stmt2->SetParamInt(1, amount);
    stmt2->SetParamInt(2, toId);
    stmt2->RunQuery();

    tx.Commit();  // If not called (exception) — automatic rollback
}
```

### Database migrations

All schema changes — only through versioned SQL scripts:

```
db/
├── schema/
│   └── initial_schema.sql      — Initial schema
└── migrations/
    ├── v1.0.0_to_v1.1.0.sql    — Migrations numbered by version
    ├── v1.1.0_to_v1.2.0.sql
    └── README.md               — How to apply migrations
```

**NEVER** modify the schema manually in production without a migration script.

### Indexes for performance

```sql
-- Add indexes for columns used in WHERE, JOIN, ORDER BY
CREATE INDEX IDX_DOCUMENTS_USER_ID ON DOCUMENTS(USER_ID);
CREATE INDEX IDX_DOCUMENTS_CREATED_AT ON DOCUMENTS(CREATED_AT DESC);
CREATE INDEX IDX_DOCUMENTS_STATUS_DATE ON DOCUMENTS(STATUS, CREATED_AT);
```

---

## Multithreading

### wxWidgets and the UI thread

```cpp
// wxWidgets — UI must be touched only from the main thread
// You CANNOT touch UI from a background thread!

// BAD — UI update from a background thread
void BackgroundWorker::run() {
    // ... long operation ...
    m_progressBar->SetValue(50);  // CRASH or UB!
}

// GOOD — post an event to the main thread
void BackgroundWorker::run() {
    // ... long operation ...
    wxQueueEvent(m_mainWindow, new ProgressEvent(50));
}

// Or via wxCallAfter (simpler)
void BackgroundWorker::run() {
    // ... long operation ...
    wxCallAfter([this]() {
        m_progressBar->SetValue(50);  // Safe — runs in the UI thread
    });
}
```

### Mutexes through RAII

```cpp
// GOOD — std::lock_guard, no manual unlock
std::mutex m_dataMutex;
std::vector<Record> m_records;

void addRecord(const Record& record) {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_records.push_back(record);
    // lock is released automatically on scope exit
}

// For shared_mutex (many readers, one writer)
std::shared_mutex m_configMutex;

std::string readConfig(const std::string& key) {
    std::shared_lock lock(m_configMutex);  // Shared lock
    return m_config.at(key);
}

void writeConfig(const std::string& key, const std::string& value) {
    std::unique_lock lock(m_configMutex);  // Exclusive lock
    m_config[key] = value;
}
```

---

## wxWidgets — specifics

### Strings: wxString and std::string

```cpp
// wxString — for UI (display, dialogs, file paths)
// std::string — for business logic, DB, network

// Conversion
wxString wxStr = wxString::FromUTF8(stdStr);
std::string stdStr = wxStr.ToUTF8().data();

// GOOD — explicit conversion at boundaries
void DocumentService::save(const Document& doc) {
    // Convert wxString → std::string when passing to the DB
    std::string title = doc.getTitle().ToUTF8().data();
    m_db->Execute("UPDATE DOCS SET TITLE = ?", {title});
}
```

### Memory management for wxWidgets objects

```cpp
// wxWidgets manages the memory of child widgets itself
// Do NOT manually delete widgets that were added to a parent!

wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "OES");
wxButton* btn = new wxButton(frame, wxID_OK, "OK");
// btn will be destroyed automatically when frame is destroyed

// BAD
delete btn;  // UB — wxWidgets will delete it

// Exception: objects without a parent must be deleted ourselves
wxBitmap* bmp = new wxBitmap("logo.png");
// ... use ...
delete bmp;  // OK — no parent, our responsibility
// Or better:
wxBitmap bmp("logo.png");  // On the stack when possible
```

---

## Performance

### Avoid unnecessary copies

```cpp
// BAD — copies the string on each call
void processTitle(wxString title) { /* ... */ }

// GOOD — const reference
void processTitle(const wxString& title) { /* ... */ }

// GOOD — move to transfer ownership
void setTitle(wxString title) {
    m_title = std::move(title);  // Move instead of copy
}
```

### Pagination when loading large result sets

```cpp
// BAD — loads every row into memory
auto allRecords = m_db->Execute("SELECT * FROM DOCUMENTS");
// May return millions of rows!

// GOOD — pagination (Firebird syntax)
auto page = m_db->Execute(
    "SELECT FIRST ? SKIP ? * FROM DOCUMENTS ORDER BY CREATED_AT DESC",
    {std::to_string(pageSize), std::to_string(offset)}
);

// Or ROWS ... TO ... (Firebird)
auto page = m_db->Execute(
    "SELECT * FROM DOCUMENTS ORDER BY CREATED_AT DESC ROWS ? TO ?",
    {std::to_string(startRow), std::to_string(endRow)}
);
```

### Profiling slow queries

```cpp
// Log slow queries
// OesScopeTimer — a proposed utility (not implemented in the codebase).
// Until it's added, use the inline pattern below:
class TimedQuery {
public:
    TimedQuery(ibDatabaseLayer* db, const wxString& sql,
               long thresholdMs = 1000)
        : m_start(std::chrono::steady_clock::now())
        , m_sql(sql) {
        // Use ibPreparedStatement + SetParamString/SetParamInt
        // to pass parameters (do not concatenate them into sql!)
        m_stmt = db->PrepareStatement(sql);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start
        ).count();
        if (elapsed > thresholdMs) {
            wxLogWarning("[PERF] Slow PrepareStatement (%ldms): %s", elapsed, sql);
        }
    }
    ibPreparedStatement* stmt() const { return m_stmt; }
private:
    std::chrono::steady_clock::time_point m_start;
    wxString m_sql;
    ibPreparedStatement* m_stmt = nullptr;
};
```

---

## Per-PR checklist

- [ ] No bare `new`/`delete` — smart pointers are used
- [ ] No empty `catch` blocks
- [ ] SQL queries use parameters, not concatenation
- [ ] Passwords and sensitive data are not in logs
- [ ] No `strcpy`/`sprintf` without buffer length checks
- [ ] New methods have correct error handling
- [ ] Resources are released through RAII
- [ ] UI updates happen only on the main thread (wxWidgets)
- [ ] cppcheck shows no new warnings
- [ ] Compiler builds with no warnings (`/W4` or `-Wall -Wextra`)
