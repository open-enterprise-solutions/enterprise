# 16. База данных

## Стек

| Компонент | Описание |
|-----------|----------|
| **Firebird** | Основная СУБД (встраиваемая + серверная) |
| **PostgreSQL** | Поддерживаемая СУБД для корпоративных установок |
| **SQLite** | Поддерживаемая СУБД для локальных/автономных установок |
| **MySQL / ODBC** | Дополнительные поддерживаемые источники |
| **ibDatabaseLayer** | Собственный абстрактный слой доступа к данным |
| **ibPreparedStatement** | Параметризованные запросы (обязательно для всех операций с данными) |
| **ibResultSet** | Итерация по результатам запроса |

---

## Абстрактный слой доступа к данным

### Принцип: никакого прямого SQL без абстракции

Весь код работает через интерфейс `ibDatabaseLayer`. Конкретная СУБД подключается через фабрику — остальной код не знает о типе базы.

```cpp
// database_factory.h
#pragma once
#include "ibDatabaseLayer.h"

enum class DbDriver { Firebird, PostgreSQL, SQLite, MySQL, ODBC };

struct DbConnectionParams
{
    DbDriver  driver   = DbDriver::Firebird;
    wxString  host     = "localhost";
    int       port     = 0;        // 0 = дефолтный порт драйвера
    wxString  database;            // путь к файлу или имя БД
    wxString  user;
    wxString  password;
    int       poolSize = 1;        // для однопользовательского приложения = 1

    wxString BuildDSN() const;
};

// Создаёт нужный драйвер
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
            wxLogError("[Database] Неизвестный драйвер БД");
            return nullptr;
    }
}
```

---

## Управление соединениями

### Connection Manager

```cpp
// src/engine/backend/appData.h (фрагмент — управление соединением)
class ibApplicationData
{
public:
    static ibApplicationData& Instance();

    bool  Open(const DbConnectionParams& params);
    void  Close();
    bool  IsOpen() const;
    bool  Ping();   // SELECT 1 — проверка живости соединения

    ibDatabaseLayer* Get();    // не-владеющий указатель

    // Reconnect при потере соединения
    bool EnsureConnected();

private:
    ibApplicationData() = default;
    std::unique_ptr<ibDatabaseLayer> m_db;
    DbConnectionParams               m_params;
    bool                             m_isOpen = false;
};
```

```cpp
// src/engine/backend/appData.cpp (фрагмент)
bool ibApplicationData::Open(const DbConnectionParams& params)
{
    m_params = params;
    wxLogMessage("[Database] Открытие соединения | driver=%d host=%s db=%s",
        (int)params.driver, params.host, params.database);

    m_db = CreateDatabaseLayer(params);
    if (!m_db)
    {
        wxLogError("[Database] Не удалось создать драйвер БД");
        return false;
    }

    if (!m_db->IsOpen())
    {
        wxLogError("[Database] Ошибка открытия соединения: %s",
            m_db->GetErrorMessage());
        return false;
    }

    m_isOpen = true;
    wxLogMessage("[Database] Соединение открыто");
    return true;
}

bool ibApplicationData::EnsureConnected()
{
    if (!m_isOpen || !m_db) return Open(m_params);

    if (!Ping())
    {
        wxLogWarning("[Database] Соединение потеряно, переподключение...");
        Close();
        return Open(m_params);
    }
    return true;
}

bool ibApplicationData::Ping()
{
    // Используем IsOpen() + универсальный запрос SELECT 1,
    // чтобы не привязываться к Firebird-специфичной таблице RDB$DATABASE.
    // SELECT 1 поддерживается всеми СУБД: Firebird, PostgreSQL, SQLite, MySQL.
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

## Параметризованные запросы (обязательно)

### Никогда не конкатенировать SQL с пользовательскими данными

```cpp
// ПРАВИЛЬНО — ibPreparedStatement
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
        wxLogError("[Database] Ошибка подготовки запроса | context=FindByName error=%s", err);
        return false;
    }

    stmt->SetParamString(1, "%" + name + "%");

    ibResultSet* rs = stmt->RunQueryWithResults();
    if (!rs)
    {
        err = db->GetErrorMessage();
        wxLogError("[Database] Ошибка выполнения запроса | context=FindByName error=%s", err);
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

// НЕПРАВИЛЬНО — SQL-инъекция!
wxString sql = wxString::Format(
    "SELECT * FROM documents WHERE name = '%s'", name);  // ОПАСНО
db->RunQuery(sql);
```

### RAII-обёртка для автоматического закрытия

```cpp
// Вспомогательный класс для исключения утечек
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

// Использование
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
        err = wxString::Format("Документ id=%d не найден", id);
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

## Транзакции

### Всегда использовать транзакции для связанных операций

```cpp
// RAII-обёртка для транзакций
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
            m_db->RollBack();  // ibDatabaseLayer API: RollBack() (с заглавной B)
            wxLogWarning("[Database] Транзакция откачена (rollback при выходе из scope)");
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

// Использование — автоматический rollback при исключении или раннем выходе
OperationResult OesDocumentRepository::CreateWithSections(
    const DocumentData& doc,
    const std::vector<SectionData>& sections,
    int& outDocId)
{
    ibDatabaseLayer* db = ibApplicationData::Instance().Get();
    ibTransactionGuard txn(db);

    // Вставить документ
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

    // Вставить секции
    for (const auto& section : sections)
    {
        OesStatementGuard stmtSec(db->PrepareStatement(
            "INSERT INTO document_sections (doc_id, title, content) "
            "VALUES (?, ?, ?)"));
        if (!stmtSec.ok())
            return OperationResult::Fail(db->GetErrorMessage());  // txn откатится

        stmtSec->SetParamInt(1, outDocId);
        stmtSec->SetParamString(2, section.title);
        stmtSec->SetParamString(3, section.content);

        if (!stmtSec->RunQuery())
            return OperationResult::Fail(db->GetErrorMessage());
    }

    txn.Commit();
    wxLogMessage("[Database] Документ создан | id=%d sections=%d",
        outDocId, (int)sections.size());
    return OperationResult::Success();
}
```

---

## Соглашения по именованию

### Таблицы: snake_case, множественное число

```
documents           (не Document, не tbl_document)
document_sections   (не DocumentSection)
report_templates    (не ReportTemplate)
user_settings       (не UserSetting)
```

### Колонки: snake_case

```
created_at       is_deleted       doc_type_id
updated_at       share_token      parent_id
```

### Первичные ключи

Для Firebird — INTEGER с последовательностью (sequence):

```sql
-- Firebird 3.0+: используйте CREATE SEQUENCE (стандартный синтаксис SQL)
-- CREATE GENERATOR — устаревший синоним, поддерживается для обратной совместимости
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

Для PostgreSQL — SERIAL или IDENTITY:

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

### Обязательные поля в каждой таблице

```
id          — первичный ключ
created_at  — дата создания (автоматически)
updated_at  — дата изменения (обновлять в UPDATE-триггере или приложении)
is_deleted  — мягкое удаление (не DELETE физически)
```

---

## Миграции

### Правила

1. **НИКОГДА** не менять схему БД вручную в production
2. Все изменения схемы — через SQL-скрипты миграций в `db/migrations/`
3. Формат имени: `YYYYMMDD_NNN_description.sql`
4. Каждый скрипт содержит раздел `-- UP` и `-- DOWN`
5. Перед деструктивными миграциями — бэкап

### Структура папки миграций

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
└── schema_version.sql    — текущая версия схемы
```

### Пример скрипта миграции

```sql
-- 20260310_003_add_share_token.sql
-- Description: Добавить поле share_token в таблицу documents

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

### Менеджер версий схемы

```cpp
class OesMigrationManager
{
public:
    explicit OesMigrationManager(ibDatabaseLayer* db) : m_db(db) {}

    wxString GetCurrentVersion();
    bool ApplyMigration(const wxString& scriptPath);

    /**
     * @brief Применяет все незаявленные миграции из каталога.
     *
     * Алгоритм:
     * 1. Вызывает EnsureVersionTable() — создаёт таблицу schema_version, если не существует.
     * 2. Читает список уже применённых версий через GetAppliedVersions().
     * 3. Сканирует migrationsDir на наличие файлов *.sql, отсортированных по имени (YYYYMMDD_NNN_).
     * 4. Для каждого файла, отсутствующего в списке применённых, вызывает ApplyMigration().
     * 5. ApplyMigration() выполняет скрипт в транзакции и фиксирует версию в schema_version.
     * 6. При ошибке применения — откатывает транзакцию и возвращает false (остальные не применяются).
     *
     * @param migrationsDir Путь к каталогу с SQL-скриптами миграций.
     * @return true если все ожидающие миграции применены успешно, false при первой ошибке.
     */
    bool ApplyAllPending(const wxString& migrationsDir);

private:
    bool EnsureVersionTable();
    std::vector<wxString> GetAppliedVersions();

    ibDatabaseLayer* m_db;
};
```

### Двухэтапное удаление колонок

```
Этап 1: Код перестаёт использовать колонку → деплой версии N
Этап 2: Миграция удаляет колонку        → деплой версии N+1
```

---

## Индексы

### Где обязательно ставить индексы

```sql
-- Внешние ключи
CREATE INDEX idx_document_sections_doc_id ON document_sections (doc_id);
CREATE INDEX idx_documents_doc_type_id    ON documents (doc_type_id);

-- Поля для фильтрации
CREATE INDEX idx_documents_status      ON documents (status);
CREATE INDEX idx_documents_is_deleted  ON documents (is_deleted);

-- Поля для сортировки (часто используемые)
CREATE INDEX idx_documents_created_at  ON documents (created_at DESC);

-- Уникальные значения
CREATE UNIQUE INDEX idx_documents_share_token ON documents (share_token);
```

### Что не делать с индексами

- Не ставить индекс на каждую колонку: каждый INDEX замедляет INSERT/UPDATE
- Не создавать составные индексы без анализа реальных запросов
- Периодически проверять использование индексов через PLAN-анализ

---

## Резервное копирование

### Firebird

```batch
REM ежедневный бэкап (Windows Task Scheduler)
gbak -b -user SYSDBA -password masterkey ^
  localhost:C:\OES\data\oes.fdb ^
  C:\OES\backups\oes-%date:~-4,4%%date:~-7,2%%date:~-10,2%.fbk

REM восстановление
gbak -c -user SYSDBA -password masterkey ^
  C:\OES\backups\oes-20260310.fbk ^
  localhost:C:\OES\data\oes_restored.fdb
```

### PostgreSQL

```bash
# Ежедневный бэкап
pg_dump -U oesuser oes_db | gzip > /backups/oes-$(date +%Y%m%d).sql.gz

# Восстановление
gunzip < /backups/oes-20260310.sql.gz | psql -U oesuser oes_db
```

### SQLite

```cpp
// Для SQLite — простое копирование файла (при закрытой БД).
// Реализован как метод ibApplicationData, так как требует доступа
// к m_params для повторного открытия соединения после копирования.
void ibApplicationData::BackupSqliteDb(const wxString& backupDir)
{
    wxString dbPath = m_params.database;
    Close();

    wxString dest = backupDir + wxString::Format("/oes_%s.db",
        wxDateTime::Now().Format("%Y%m%d_%H%M%S"));

    wxCopyFile(dbPath, dest);

    Open(m_params);  // повторно открыть соединение с теми же параметрами
    wxLogMessage("[Database] Бэкап SQLite создан: %s", dest);
}
```

Добавьте `void BackupSqliteDb(const wxString& backupDir);` в объявление класса `ibApplicationData` (см. заголовок выше).

---

## Чего НЕ делать

| Запрещено | Почему | Альтернатива |
|-----------|--------|-------------|
| Конкатенировать SQL с данными пользователя | SQL-инъекция | `ibPreparedStatement` с параметрами |
| `SELECT *` в производственном коде | Лишние данные, скрытые зависимости | Перечислять нужные колонки явно |
| Хранить файлы в БД | Раздувает базу, медленные бэкапы | Хранить файлы в файловой системе, в БД — путь |
| Физическое удаление без мягкого | Невозможно восстановить | `is_deleted = 1` + периодическая архивация |
| Изменение типа колонки без проверки | Потеря данных | Новая колонка + миграция данных + удаление старой |
| Прямой ALTER TABLE в production | Нет истории, нет отката | SQL-скрипт миграции |
| Открытый пароль к БД в коде | Безопасность | Конфигурационный файл с ограниченным доступом |

---

## Чеклист базы данных

### Перед релизом

- [ ] Все новые запросы используют `ibPreparedStatement`
- [ ] Нет конкатенации SQL с пользовательскими данными
- [ ] Написаны скрипты миграций для новых таблиц и колонок
- [ ] Миграции протестированы на staging
- [ ] Новые внешние ключи имеют индексы
- [ ] Транзакции используются для связанных INSERT/UPDATE

### Периодически

- [ ] Проверить slow query log — запросы > 1 сек
- [ ] Проанализировать PLAN для тяжёлых запросов
- [ ] Убедиться, что бэкапы создаются и доступны для восстановления
- [ ] Очистить устаревшие записи (is_deleted = 1 старше N дней)
