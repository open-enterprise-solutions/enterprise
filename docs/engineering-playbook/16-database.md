# 16. Database

## Stack

| Component | Description |
|-----------|----------|
| **Firebird** | Primary DBMS (embedded + server) |
| **PostgreSQL** | Supported DBMS for enterprise installations |
| **SQLite** | Supported DBMS for local/offline installations |
| **MySQL / ODBC** | Additional supported sources |
| **ibDatabaseLayer** | Custom abstract data-access layer |
| **ibPreparedStatement** | Parameterized queries (mandatory for all data operations) |
| **ibResultSet** | Iteration over query results |

---

## Abstract data-access layer

### Principle: no direct SQL without the abstraction

All code goes through the `ibDatabaseLayer` interface. The concrete DBMS is wired in via a factory — the rest of the code doesn't know which DB is in use.

```cpp
// database_factory.h
#pragma once
#include "ibDatabaseLayer.h"

enum class DbDriver { Firebird, PostgreSQL, SQLite, MySQL, ODBC };

struct DbConnectionParams
{
    DbDriver  driver   = DbDriver::Firebird;
    wxString  host     = "localhost";
    int       port     = 0;        // 0 = driver default port
    wxString  database;            // file path or DB name
    wxString  user;
    wxString  password;
    int       poolSize = 1;        // single-user app = 1

    wxString BuildDSN() const;
};

// Creates the required driver
std::unique_ptr<ibDatabaseLayer> CreateDatabaseLayer(const DbConnectionParams& params);
```

```cpp
// database_factory.cpp
#include "FirebirdDatabaseLayer.h"
#include "PostgresDatabaseLayer.h"
#include "SqliteDatabaseLayer.h"
#include "MysqlDatabaseLayer.h"
#include "OdbcDatabaseLayer.h"

std::unique_ptr<ibDatabaseLayer> CreateDatabaseLayer(const DbConnectionParams& p)
{
    switch (p.driver)
    {
        case DbDriver::Firebird:
            return std::make_unique<FirebirdDatabaseLayer>(
                p.host, p.port ? p.port : 3050, p.database, p.user, p.password);

        case DbDriver::PostgreSQL:
            return std::make_unique<PostgresDatabaseLayer>(
                p.host, p.port ? p.port : 5432, p.database, p.user, p.password);

        case DbDriver::SQLite:
            return std::make_unique<SqliteDatabaseLayer>(p.database);

        case DbDriver::MySQL:
            return std::make_unique<MysqlDatabaseLayer>(
                p.host, p.port ? p.port : 3306, p.database, p.user, p.password);

        case DbDriver::ODBC:
            return std::make_unique<OdbcDatabaseLayer>(p.BuildDSN());

        default:
            wxLogError("[Database] Unknown DB driver");
            return nullptr;
    }
}
```

---

## Connection management

### Connection Manager

```cpp
// src/engine/backend/appData.h (excerpt — connection management)
class ibApplicationData
{
public:
    static ibApplicationData& Instance();

    bool  Open(const DbConnectionParams& params);
    void  Close();
    bool  IsOpen() const;
    bool  Ping();   // SELECT 1 — connection liveness check

    ibDatabaseLayer* Get();    // non-owning pointer

    // Reconnect on connection loss
    bool EnsureConnected();

private:
    ibApplicationData() = default;
    std::unique_ptr<ibDatabaseLayer> m_db;
    DbConnectionParams               m_params;
    bool                             m_isOpen = false;
};
```

```cpp
// src/engine/backend/appData.cpp (excerpt)
bool ibApplicationData::Open(const DbConnectionParams& params)
{
    m_params = params;
    wxLogMessage("[Database] Opening connection | driver=%d host=%s db=%s",
        (int)params.driver, params.host, params.database);

    m_db = CreateDatabaseLayer(params);
    if (!m_db)
    {
        wxLogError("[Database] Failed to create DB driver");
        return false;
    }

    if (!m_db->IsOpen())
    {
        wxLogError("[Database] Connection open error: %s",
            m_db->GetErrorMessage());
        return false;
    }

    m_isOpen = true;
    wxLogMessage("[Database] Connection opened");
    return true;
}

bool ibApplicationData::EnsureConnected()
{
    if (!m_isOpen || !m_db) return Open(m_params);

    if (!Ping())
    {
        wxLogWarning("[Database] Connection lost, reconnecting...");
        Close();
        return Open(m_params);
    }
    return true;
}

bool ibApplicationData::Ping()
{
    // Use IsOpen() + a portable SELECT 1 query
    // so we don't depend on the Firebird-specific RDB$DATABASE table.
    // SELECT 1 is supported by every DBMS: Firebird, PostgreSQL, SQLite, MySQL.
    if (!m_db || !m_db->IsOpen()) return false;
    try
    {
        ibResultSet* rs = m_db->RunQueryWithResults("SELECT 1");
        if (rs) { rs->Close(); return true; }
    }
    catch (...) {}
    return false;
}
```

---

## Parameterized queries (mandatory)

### Never concatenate SQL with user data

```cpp
// RIGHT — ibPreparedStatement
bool OesDocumentRepository::FindByName(const wxString& name,
                                        std::vector<DocumentInfo>& out,
                                        wxString& err)
{
    ibDatabaseLayer* db = ibApplicationData::Instance().Get();

    ibPreparedStatement* stmt = db->PrepareStatement(
        "SELECT id, name, status, created_at "
        "FROM documents "
        "WHERE name LIKE ? AND is_deleted = 0 "
        "ORDER BY created_at DESC"
    );

    if (!stmt)
    {
        err = db->GetErrorMessage();
        wxLogError("[Database] Prepare error | context=FindByName error=%s", err);
        return false;
    }

    stmt->SetParamString(1, "%" + name + "%");

    ibResultSet* rs = stmt->RunQueryWithResults();
    if (!rs)
    {
        err = db->GetErrorMessage();
        wxLogError("[Database] Execute error | context=FindByName error=%s", err);
        stmt->Close();
        return false;
    }

    while (rs->Next())
    {
        DocumentInfo info;
        info.id        = rs->GetResultInt("id");
        info.name      = rs->GetResultString("name");
        info.status    = rs->GetResultString("status");
        info.createdAt = rs->GetResultString("created_at");
        out.push_back(info);
    }

    rs->Close();
    stmt->Close();
    return true;
}

// WRONG — SQL injection!
wxString sql = wxString::Format(
    "SELECT * FROM documents WHERE name = '%s'", name);  // DANGEROUS
db->RunQuery(sql);
```

### RAII wrapper for automatic cleanup

```cpp
// Helper class to prevent leaks
class OesResultSetGuard
{
public:
    explicit OesResultSetGuard(ibResultSet* rs) : m_rs(rs) {}
    ~OesResultSetGuard() { if (m_rs) m_rs->Close(); }

    ibResultSet*       operator->()       { return m_rs; }
    const ibResultSet* operator->() const { return m_rs; }
    ibResultSet*       get()              { return m_rs; }
    const ibResultSet* get()        const { return m_rs; }
    bool               ok()         const { return m_rs != nullptr; }

    OesResultSetGuard(const OesResultSetGuard&) = delete;
    OesResultSetGuard& operator=(const OesResultSetGuard&) = delete;

private:
    ibResultSet* m_rs;
};

class OesStatementGuard
{
public:
    explicit OesStatementGuard(ibPreparedStatement* s) : m_stmt(s) {}
    ~OesStatementGuard() { if (m_stmt) m_stmt->Close(); }

    ibPreparedStatement* operator->() { return m_stmt; }
    bool ok() const { return m_stmt != nullptr; }

private:
    ibPreparedStatement* m_stmt;
};

// Usage
bool OesDocumentRepository::GetById(int id, DocumentData& out, wxString& err)
{
    ibDatabaseLayer* db = ibApplicationData::Instance().Get();

    OesStatementGuard stmt(db->PrepareStatement(
        "SELECT id, name, status, content FROM documents WHERE id = ?"));

    if (!stmt.ok())
    {
        err = db->GetErrorMessage();
        return false;
    }

    stmt->SetParamInt(1, id);

    OesResultSetGuard rs(stmt->RunQueryWithResults());
    if (!rs.ok()) { err = db->GetErrorMessage(); return false; }

    if (!rs->Next())
    {
        err = wxString::Format("Document id=%d not found", id);
        return false;
    }

    out.id      = rs->GetResultInt("id");
    out.name    = rs->GetResultString("name");
    out.status  = rs->GetResultString("status");
    out.content = rs->GetResultString("content");
    return true;
}
```

---

## Transactions

### Always use transactions for related operations

```cpp
// RAII transaction wrapper
class ibTransactionGuard
{
public:
    explicit ibTransactionGuard(ibDatabaseLayer* db)
        : m_db(db), m_committed(false)
    {
        m_db->BeginTransaction();
    }

    ~ibTransactionGuard()
    {
        if (!m_committed)
        {
            m_db->RollBack();  // ibDatabaseLayer API: RollBack() (capital B)
            wxLogWarning("[Database] Transaction rolled back (rollback on scope exit)");
        }
    }

    void Commit()
    {
        m_db->Commit();
        m_committed = true;
    }

private:
    ibDatabaseLayer* m_db;
    bool             m_committed;
};

// Usage — automatic rollback on exception or early exit
OperationResult OesDocumentRepository::CreateWithSections(
    const DocumentData& doc,
    const std::vector<SectionData>& sections,
    int& outDocId)
{
    ibDatabaseLayer* db = ibApplicationData::Instance().Get();
    ibTransactionGuard txn(db);

    // Insert document
    OesStatementGuard stmtDoc(db->PrepareStatement(
        "INSERT INTO documents (name, status) VALUES (?, ?) "
        "RETURNING id"));
    if (!stmtDoc.ok())
        return OperationResult::Fail(db->GetErrorMessage());

    stmtDoc->SetParamString(1, doc.name);
    stmtDoc->SetParamString(2, doc.status);

    OesResultSetGuard rsDoc(stmtDoc->RunQueryWithResults());
    if (!rsDoc.ok())
        return OperationResult::Fail(db->GetErrorMessage());

    rsDoc->Next();
    outDocId = rsDoc->GetResultInt("id");

    // Insert sections
    for (const auto& section : sections)
    {
        OesStatementGuard stmtSec(db->PrepareStatement(
            "INSERT INTO document_sections (doc_id, title, content) "
            "VALUES (?, ?, ?)"));
        if (!stmtSec.ok())
            return OperationResult::Fail(db->GetErrorMessage());  // txn rolls back

        stmtSec->SetParamInt(1, outDocId);
        stmtSec->SetParamString(2, section.title);
        stmtSec->SetParamString(3, section.content);

        if (!stmtSec->RunQuery())
            return OperationResult::Fail(db->GetErrorMessage());
    }

    txn.Commit();
    wxLogMessage("[Database] Document created | id=%d sections=%d",
        outDocId, (int)sections.size());
    return OperationResult::Success();
}
```

---

## Naming conventions

### Tables: snake_case, plural

```
documents           (not Document, not tbl_document)
document_sections   (not DocumentSection)
report_templates    (not ReportTemplate)
user_settings       (not UserSetting)
```

### Columns: snake_case

```
created_at       is_deleted       doc_type_id
updated_at       share_token      parent_id
```

### Primary keys

For Firebird — INTEGER with a sequence:

```sql
-- Firebird 3.0+: use CREATE SEQUENCE (standard SQL syntax)
-- CREATE GENERATOR is a deprecated synonym, kept for backward compatibility
CREATE SEQUENCE gen_documents_id;

CREATE TABLE documents (
  id         INTEGER NOT NULL DEFAULT NEXT VALUE FOR gen_documents_id,
  name       VARCHAR(255) NOT NULL,
  status     VARCHAR(50)  NOT NULL DEFAULT 'draft',
  created_at TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP,
  is_deleted SMALLINT     NOT NULL DEFAULT 0,
  CONSTRAINT pk_documents PRIMARY KEY (id)
);
```

For PostgreSQL — SERIAL or IDENTITY:

```sql
CREATE TABLE documents (
  id         SERIAL PRIMARY KEY,
  name       VARCHAR(255) NOT NULL,
  status     VARCHAR(50)  NOT NULL DEFAULT 'draft',
  created_at TIMESTAMP    NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMP,
  is_deleted BOOLEAN      NOT NULL DEFAULT FALSE
);
```

### Required columns in every table

```
id          — primary key
created_at  — creation timestamp (automatic)
updated_at  — modification timestamp (updated in an UPDATE trigger or by the app)
is_deleted  — soft delete (never DELETE physically)
```

---

## Migrations

### Rules

1. **NEVER** modify the DB schema manually in production
2. All schema changes — through migration SQL scripts in `db/migrations/`
3. Naming format: `YYYYMMDD_NNN_description.sql`
4. Every script has an `-- UP` and a `-- DOWN` section
5. Before destructive migrations — back up

### Migration folder layout

```
db/
├── migrations/
│   ├── 20260101_001_initial_schema.sql
│   ├── 20260201_002_add_document_sections.sql
│   ├── 20260310_003_add_share_token.sql
│   └── ...
├── seeds/
│   ├── initial_data.sql
│   └── demo_data.sql
└── schema_version.sql    — current schema version
```

### Example migration script

```sql
-- 20260310_003_add_share_token.sql
-- Description: Add share_token column to the documents table

-- =====================
-- UP
-- =====================

ALTER TABLE documents
  ADD share_token VARCHAR(64);

CREATE UNIQUE INDEX idx_documents_share_token
  ON documents (share_token);

UPDATE schema_version SET version = '20260310_003', applied_at = CURRENT_TIMESTAMP;

-- =====================
-- DOWN
-- =====================

-- DROP INDEX idx_documents_share_token;
-- ALTER TABLE documents DROP share_token;
-- UPDATE schema_version SET version = '20260201_002', applied_at = CURRENT_TIMESTAMP;
```

### Schema version manager

```cpp
class OesMigrationManager
{
public:
    explicit OesMigrationManager(ibDatabaseLayer* db) : m_db(db) {}

    wxString GetCurrentVersion();
    bool ApplyMigration(const wxString& scriptPath);

    /**
     * @brief Applies every pending migration in the directory.
     *
     * Algorithm:
     * 1. Calls EnsureVersionTable() — creates schema_version if it doesn't exist.
     * 2. Reads the list of already applied versions via GetAppliedVersions().
     * 3. Scans migrationsDir for *.sql files, sorted by name (YYYYMMDD_NNN_).
     * 4. For every file not in the applied list, calls ApplyMigration().
     * 5. ApplyMigration() runs the script in a transaction and records the version
     *    in schema_version.
     * 6. On error — rolls back the transaction and returns false (remaining are not applied).
     *
     * @param migrationsDir Path to the directory with migration SQL scripts.
     * @return true if every pending migration was applied successfully, false on the first error.
     */
    bool ApplyAllPending(const wxString& migrationsDir);

private:
    bool EnsureVersionTable();
    std::vector<wxString> GetAppliedVersions();

    ibDatabaseLayer* m_db;
};
```

### Two-phase column removal

```
Phase 1: code stops using the column → deploy version N
Phase 2: migration drops the column   → deploy version N+1
```

---

## Indexes

### Where indexes are required

```sql
-- Foreign keys
CREATE INDEX idx_document_sections_doc_id ON document_sections (doc_id);
CREATE INDEX idx_documents_doc_type_id    ON documents (doc_type_id);

-- Columns used for filtering
CREATE INDEX idx_documents_status      ON documents (status);
CREATE INDEX idx_documents_is_deleted  ON documents (is_deleted);

-- Columns used for sorting (frequently)
CREATE INDEX idx_documents_created_at  ON documents (created_at DESC);

-- Unique values
CREATE UNIQUE INDEX idx_documents_share_token ON documents (share_token);
```

### What NOT to do with indexes

- Don't index every column: each INDEX slows down INSERT/UPDATE
- Don't create composite indexes without analyzing real queries
- Periodically check index usage through PLAN analysis

---

## Backups

### Firebird

```batch
REM daily backup (Windows Task Scheduler)
gbak -b -user SYSDBA -password masterkey ^
  localhost:C:\OES\data\oes.fdb ^
  C:\OES\backups\oes-%date:~-4,4%%date:~-7,2%%date:~-10,2%.fbk

REM restore
gbak -c -user SYSDBA -password masterkey ^
  C:\OES\backups\oes-20260310.fbk ^
  localhost:C:\OES\data\oes_restored.fdb
```

### PostgreSQL

```bash
# Daily backup
pg_dump -U oesuser oes_db | gzip > /backups/oes-$(date +%Y%m%d).sql.gz

# Restore
gunzip < /backups/oes-20260310.sql.gz | psql -U oesuser oes_db
```

### SQLite

```cpp
// For SQLite — just copy the file (while the DB is closed).
// Implemented as an ibApplicationData method because it needs access
// to m_params to reopen the connection after copying.
void ibApplicationData::BackupSqliteDb(const wxString& backupDir)
{
    wxString dbPath = m_params.database;
    Close();

    wxString dest = backupDir + wxString::Format("/oes_%s.db",
        wxDateTime::Now().Format("%Y%m%d_%H%M%S"));

    wxCopyFile(dbPath, dest);

    Open(m_params);  // reopen with the same parameters
    wxLogMessage("[Database] SQLite backup created: %s", dest);
}
```

Add `void BackupSqliteDb(const wxString& backupDir);` to the `ibApplicationData` class declaration (see the header above).

---

## What NOT to do

| Forbidden | Why | Alternative |
|-----------|--------|-------------|
| Concatenate SQL with user data | SQL injection | `ibPreparedStatement` with parameters |
| `SELECT *` in production code | Extra data, hidden dependencies | List the columns you need explicitly |
| Store files in the DB | Bloats the DB, slow backups | Store files on disk, only the path in the DB |
| Physical delete instead of soft delete | Cannot recover | `is_deleted = 1` + periodic archive |
| Change a column type without checks | Data loss | New column + data migration + drop the old one |
| Direct ALTER TABLE in production | No history, no rollback | Migration SQL script |
| Plain-text DB password in code | Security | Configuration file with restricted access |

---

## Database checklist

### Before release

- [ ] All new queries use `ibPreparedStatement`
- [ ] No SQL concatenation with user data
- [ ] Migration scripts written for new tables and columns
- [ ] Migrations tested on staging
- [ ] New foreign keys have indexes
- [ ] Transactions used for related INSERT/UPDATE

### Periodically

- [ ] Review the slow query log — queries > 1 sec
- [ ] Analyze PLAN for heavy queries
- [ ] Verify that backups are being created and restorable
- [ ] Purge obsolete rows (is_deleted = 1 older than N days)
