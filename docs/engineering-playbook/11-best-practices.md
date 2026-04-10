# 11. Лучшие практики

## C++17 — стандарт проекта

### Компилятор — предупреждения как ошибки

```cmake
# CMakeLists.txt
if(MSVC)
    target_compile_options(oes PRIVATE
        /W4         # Высокий уровень предупреждений
        /WX         # Предупреждения как ошибки
        /std:c++17  # C++17
        /permissive- # Строгое соответствие стандарту
    )
else()
    target_compile_options(oes PRIVATE
        -Wall -Wextra -Wpedantic
        -Werror                # Предупреждения как ошибки
        -std=c++17
        -Wno-unused-parameter  # wxWidgets генерирует такие предупреждения
    )
endif()
```

### Флаги компилятора в Visual Studio

В `*.vcxproj` или через Property Sheets:
```xml
<ClCompile>
  <WarningLevel>Level4</WarningLevel>
  <TreatWarningAsError>true</TreatWarningAsError>
  <LanguageStandard>stdcpp17</LanguageStandard>
  <ConformanceMode>true</ConformanceMode>
</ClCompile>
```

---

## Управление памятью

### RAII — основной принцип

Ресурс (память, файл, соединение с БД, мьютекс) должен захватываться в конструкторе и освобождаться в деструкторе. Никаких голых `new`/`delete` в бизнес-логике.

```cpp
// ПЛОХО — голый new/delete, утечки при исключениях
class ReportGenerator {
    Document* m_doc;
public:
    ReportGenerator() : m_doc(new Document()) {}
    ~ReportGenerator() { delete m_doc; }  // Не вызовется при исключении в конструкторе!

    void generate() {
        Image* img = new Image("logo.png");
        renderPage(img);   // Если бросит исключение — утечка img
        delete img;
    }
};

// ХОРОШО — RAII через умные указатели
class ReportGenerator {
    std::unique_ptr<Document> m_doc;
public:
    ReportGenerator() : m_doc(std::make_unique<Document>()) {}
    // Деструктор генерируется автоматически

    void generate() {
        auto img = std::make_unique<Image>("logo.png");
        renderPage(*img);
        // img автоматически удалится при выходе из scope
    }
};
```

### Умные указатели — правило выбора

```cpp
// std::unique_ptr — единственный владелец (по умолчанию)
std::unique_ptr<DatabaseConnection> conn = createConnection();

// std::shared_ptr — разделяемое владение (только если действительно нужно)
std::shared_ptr<Configuration> config = Configuration::load();
// Передаётся нескольким сервисам:
UserService userSvc(config);
ReportService reportSvc(config);

// std::weak_ptr — наблюдатель без владения (избегает циклических ссылок)

// ПЛОХО — если дочерний узел держит shared_ptr на родителя,
// а родитель держит shared_ptr на дочерний — образуется цикл,
// и оба объекта никогда не будут освобождены.
class NodeBad {
    std::shared_ptr<Node> m_parent;  // циклическая ссылка!
};

// ХОРОШО — weak_ptr не увеличивает счётчик ссылок,
// поэтому цикл не образуется и память освобождается корректно.
class Node {
    std::weak_ptr<Node> m_parent;    // наблюдатель без владения
};

// Сырые указатели — ТОЛЬКО для не-владеющих ссылок
void processDocument(Document* doc) {  // OK — не владеет
    // doc — временная ссылка, lifetime управляется вызывающей стороной
}
```

### Никаких `new` вне фабрик и make_*

```cpp
// ПЛОХО — голый new в бизнес-логике
ibDatabaseLayer* db = new ibDatabaseLayerFirebird();
m_services.push_back(new UserService(db));

// ХОРОШО
auto db = std::make_unique<ibDatabaseLayerFirebird>();
m_services.push_back(std::make_unique<UserService>(db.get()));

// ХОРОШО — фабричная функция для создания ibDatabaseLayer нужного типа
// (ibApplicationData в appData.cpp выполняет эту роль в реальном коде)
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

## Обработка ошибок

### Пустые блоки catch — ЗАПРЕЩЕНЫ

```cpp
// ПЛОХО — ошибка проглочена (частая проблема в OES)
try {
    m_dbLayer->connect(config);
} catch (...) {
    // пусто
}

// ХОРОШО — минимум: логировать и пробросить
try {
    m_dbLayer->connect(config);
} catch (const DatabaseException& e) {
    wxLogError("Database connection failed: %s", e.what());
    throw;  // или обработать и вернуть false
} catch (const std::exception& e) {
    wxLogError("Unexpected error during DB connect: %s", e.what());
    throw;
}

// ХОРОШО — если нужно поглотить (редкий случай)
try {
    // Некритичная операция (например, запись в лог)
    m_logger->flush();
} catch (const std::exception& e) {
    // Намеренно игнорируем: логирование не должно ронять приложение
    // ВАЖНО: оставить комментарий почему ignore допустим
    (void)e;  // Явно показываем что переменная намеренно не используется
}
```

### Исключения vs коды возврата

```cpp
// Исключения — для исключительных ситуаций (не ожидаемые ошибки)
// Коды возврата — для ожидаемых исходов

// ХОРОШО — исключение для неожиданного провала
DatabaseConnection openConnection(const Config& config) {
    // Соединение ДОЛЖНО открыться, иначе это ошибка программы/конфига
    if (!connect(config.host, config.port)) {
        throw DatabaseException("Cannot connect to " + config.host);
    }
    return DatabaseConnection{...};
}

// ХОРОШО — std::optional для "может не найти"
std::optional<User> findUserByEmail(const std::string& email) {
    // Пользователь может не существовать — это нормально
    auto result = m_db->query("SELECT ...", {email});
    if (result.isEmpty()) return std::nullopt;
    return User::fromRow(result.firstRow());
}

// ХОРОШО — bool для "успех/неуспех" простых операций
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

### Иерархия исключений OES

OES использует собственную иерархию исключений с префиксом `ib`:

```cpp
// Реальные исключения OES (src/engine/backend/)

// ibBackendCoreException — базовое исключение движка
// Бросается при ошибках компилятора, БД, метаданных
try {
    ibPreparedStatement* stmt = db->PrepareStatement(sql);
    stmt->SetParamInt(1, id);
    stmt->RunQuery();
} catch (const ibBackendCoreException& e) {
    wxLogError("Backend error: %s", e.what());
    // ibBackendCoreException содержит код и описание ошибки
}

// ibBackendInterruptException — прерывание выполнения скрипта
// Бросается при принудительной остановке ibProcUnit (интерпретатора байткода)
try {
    procUnit->Execute(byteCode);
} catch (const ibBackendInterruptException&) {
    // Пользователь нажал "Стоп" или timeout — не ошибка программы
    wxLogMessage("Script execution interrupted by user");
}

// Для новых модулей допустимо добавлять специализированные исключения
// на базе ibBackendCoreException
```

---

## Логирование

### wxLog — структурированное логирование

```cpp
// ПЛОХО — std::cout в production коде
std::cout << "User logged in: " << username << std::endl;
printf("Error: %s\n", error.c_str());

// ХОРОШО — wxLog
wxLogMessage("User logged in: %s", username);     // INFO
wxLogWarning("Slow query (%ldms): %s", ms, sql);  // WARNING
wxLogError("Failed to save document: %s", err);   // ERROR

// Для отладочной информации (только в Debug сборке)
wxLogDebug("Processing record id=%ld, type=%s", id, type);
```

### Уровни логирования

```cpp
// src/engine/backend/utils/logger.h  (или аналогичный путь)
// Используем wxLog с настраиваемым уровнем

// config.ini:
// [app]
// log_level=info    (production)
// log_level=debug   (development)

class AppLogger {
public:
    static void Configure(const wxString& level, const wxString& logFile) {
        // Настроить wxLogStderr + wxLogFile
        auto* fileLog = new wxLogFile(logFile);
        // SetActiveTarget() возвращает предыдущий активный логгер —
        // его нужно удалить вручную, чтобы не было утечки памяти.
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

### Что логировать обязательно

```cpp
// Запуск и остановка приложения
wxLogMessage("[STARTUP] OES v%s starting", OES_VERSION_STRING);
wxLogMessage("[SHUTDOWN] OES stopping, uptime=%lds", uptime);

// Подключение к БД
wxLogMessage("[DB] Connected to %s:%d database '%s'",
    config.host, config.port, config.database);
wxLogError("[DB] Connection failed: %s", error);

// Попытки входа (audit log)
wxLogMessage("[AUDIT] Login %s for user '%s' from %s",
    success ? "SUCCESS" : "FAILED", username, hostname);

// Изменения данных (audit log)
wxLogMessage("[AUDIT] %s record id=%ld in table '%s' by user '%s'",
    operation, recordId, tableName, currentUser);

// Критические ошибки
wxLogError("[CRITICAL] Unexpected exception: %s\nStack: %s",
    e.what(), stackTrace);
```

---

## Архитектура модулей

### Разделение ответственности

```cpp
// ПЛОХО — один класс делает всё
class MainWindow {
    void onSaveButtonClick() {
        // Формирует SQL прямо здесь
        wxString sql = "UPDATE users SET name = '" + m_nameEdit->GetValue() + "'";
        // Выполняет запрос
        m_db->Execute(sql);
        // Отправляет email
        sendEmail("admin@company.com", "Record saved");
        // Обновляет статусбар
        m_statusBar->SetStatusText("Saved");
    }
};

// ХОРОШО — разделение: UI / Service / Repository
class UserService {
public:
    void saveUser(const UserData& data) {
        validate(data);               // Валидация — здесь
        m_userRepo->save(data);       // Сохранение — в репозитории
        m_auditLog->recordSave(data); // Аудит — в отдельном сервисе
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

### Интерфейсы (абстрактные классы) для зависимостей

```cpp
// OES использует ibDatabaseLayer как абстрактный базовый класс
// (src/engine/backend/databaseLayer/databaseLayer.h)
//
// Конкретные реализации:
//   ibDatabaseLayerFirebird  — Firebird (основная СУБД)
//   ibDatabaseLayerPostgres  — PostgreSQL
//   ibDatabaseLayerSQLite    — SQLite (встроенная БД)
//   ibDatabaseLayerMySQL     — MySQL
//   ibDatabaseLayerODBC      — ODBC (универсальный драйвер)
//
// Полиморфизм: код работает с ibDatabaseLayer* не зная о конкретной СУБД

// Зависимость через конструктор (Dependency Injection)
// ibApplicationData (appData.cpp) создаёт ibDatabaseLayer нужного типа
// и передаёт его в остальные компоненты
class ibValueMetaObject {
public:
    explicit ibValueMetaObject(ibDatabaseLayer* db)
        : m_db(db) {}
protected:
    ibDatabaseLayer* m_db;  // не владеет — lifetime управляется ibApplicationData
};

// Пример создания через ibApplicationData
// ibApplicationData* appData = ibApplicationData::Get();
// ibDatabaseLayer* db = appData->GetDatabaseLayer();
```

---

## Структура проекта

### Именование файлов и классов

| Что | Стиль | Пример |
|-----|-------|--------|
| Файлы `.cpp`/`.h` | PascalCase | `DatabaseLayer.cpp`, `UserService.h` |
| Классы | PascalCase | `DatabaseLayer`, `ReportGenerator` |
| Методы | camelCase | `connectToDatabase()`, `getUserById()` |
| Переменные члены | `m_` prefix + camelCase | `m_dbLayer`, `m_userName` |
| Статические члены | `s_` prefix + camelCase | `s_instance`, `s_logger` |
| Константы (compile-time) | UPPER\_SNAKE\_CASE | `MAX_RETRY_COUNT`, `DEFAULT_PORT` |
| Перечисления (enum class) | PascalCase + PascalCase values | `DatabaseType::Firebird` |
| Namespace | snake\_case | `oes::database`, `oes::ui` |
| Макросы | UPPER\_SNAKE\_CASE | `OES_VERSION_STRING` |
| Таблицы и поля БД | UPPER\_SNAKE\_CASE | `USER_PROFILES`, `CREATED_AT` |

### Размер файлов и классов

- **Максимум ~500 строк** на файл `.cpp`. Если больше — разбивайте
- Один класс на файл (исключение: небольшие вспомогательные классы)
- Один ответственный за один файл: `.h` объявляет, `.cpp` определяет

### Заголовочные файлы

```cpp
// ХОРОШО — защита от повторного включения
#pragma once

// Или классические include guards
#ifndef OES_DATABASE_LAYER_H
#define OES_DATABASE_LAYER_H
// ...
#endif

// Порядок includes в .cpp файле:
// 1. Собственный .h файл
#include "DatabaseLayer.h"

// 2. Заголовки из того же проекта
#include "core/Exceptions.h"
#include "utils/Logger.h"

// 3. Сторонние библиотеки
#include <wx/wx.h>
#include <ibase.h>     // Firebird

// 4. Стандартная библиотека
#include <memory>
#include <string>
#include <vector>
```

---

## Работа с базой данных

### Параметризованные запросы — всегда

```cpp
// ПЛОХО — конкатенация строк (SQL-инъекция)
wxString sql = wxString::Format(
    "SELECT * FROM DOCUMENTS WHERE TITLE = '%s' AND USER_ID = %d",
    title, userId
);
m_db->Execute(sql);

// ХОРОШО — ibPreparedStatement с SetParamString/SetParamInt (OES-способ)
// ibDatabaseLayer* m_db — поле класса, инициализировано через ibApplicationData
ibPreparedStatement* stmt = m_db->PrepareStatement(
    "SELECT * FROM DOCUMENTS WHERE TITLE = ? AND USER_ID = ?"
);
stmt->SetParamString(1, title);
stmt->SetParamInt(2, userId);
ibDatabaseResultSet* rs = stmt->RunQuery();
// Обработать rs, затем освободить
```

### Транзакции через RAII

OES предоставляет `ibTransactionGuard` (объявлен в `commonObject.h`) — RAII-обёртку для транзакций:

```cpp
// ХОРОШО — ibTransactionGuard: автоматический rollback при выходе из scope
// ibTransactionGuard tx(db) — начинает транзакцию
// tx.Commit()               — фиксирует транзакцию
// ~ibTransactionGuard()     — откатывает если Commit() не был вызван

void transferData(ibDatabaseLayer* db, int fromId, int toId, int amount) {
    ibTransactionGuard tx(db);  // Начало транзакции

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

    tx.Commit();  // Если не вызван (исключение) — автоматический rollback
}
```

### Миграции базы данных

Все изменения схемы — только через версионированные SQL-скрипты:

```
db/
├── schema/
│   └── initial_schema.sql      — Начальная схема
└── migrations/
    ├── v1.0.0_to_v1.1.0.sql    — Миграции нумеруются по версиям
    ├── v1.1.0_to_v1.2.0.sql
    └── README.md               — Как применять миграции
```

**НИКОГДА** не менять схему вручную в production без скрипта миграции.

### Индексы для производительности

```sql
-- Добавлять индексы для полей в WHERE, JOIN, ORDER BY
CREATE INDEX IDX_DOCUMENTS_USER_ID ON DOCUMENTS(USER_ID);
CREATE INDEX IDX_DOCUMENTS_CREATED_AT ON DOCUMENTS(CREATED_AT DESC);
CREATE INDEX IDX_DOCUMENTS_STATUS_DATE ON DOCUMENTS(STATUS, CREATED_AT);
```

---

## Многопоточность

### wxWidgets и UI thread

```cpp
// wxWidgets — UI работает только в главном потоке
// Из фонового потока НЕЛЬЗЯ трогать UI!

// ПЛОХО — обновление UI из фонового потока
void BackgroundWorker::run() {
    // ... длинная операция ...
    m_progressBar->SetValue(50);  // CRASH или UB!
}

// ХОРОШО — отправить событие в главный поток
void BackgroundWorker::run() {
    // ... длинная операция ...
    wxQueueEvent(m_mainWindow, new ProgressEvent(50));
}

// Или через wxCallAfter (проще)
void BackgroundWorker::run() {
    // ... длинная операция ...
    wxCallAfter([this]() {
        m_progressBar->SetValue(50);  // Безопасно — выполнится в UI thread
    });
}
```

### Мьютексы через RAII

```cpp
// ХОРОШО — std::lock_guard, не ручной unlock
std::mutex m_dataMutex;
std::vector<Record> m_records;

void addRecord(const Record& record) {
    std::lock_guard<std::mutex> lock(m_dataMutex);
    m_records.push_back(record);
    // lock автоматически освобождается при выходе из scope
}

// Для shared_mutex (много читателей, один писатель)
std::shared_mutex m_configMutex;

std::string readConfig(const std::string& key) {
    std::shared_lock lock(m_configMutex);  // Разделяемая блокировка
    return m_config.at(key);
}

void writeConfig(const std::string& key, const std::string& value) {
    std::unique_lock lock(m_configMutex);  // Эксклюзивная блокировка
    m_config[key] = value;
}
```

---

## wxWidgets — специфика

### Строки: wxString и std::string

```cpp
// wxString — для UI (отображение, диалоги, файловые пути)
// std::string — для бизнес-логики, БД, сети

// Конвертация
wxString wxStr = wxString::FromUTF8(stdStr);
std::string stdStr = wxStr.ToUTF8().data();

// ХОРОШО — явная конвертация в граничных точках
void DocumentService::save(const Document& doc) {
    // Конвертировать wxString → std::string при передаче в БД
    std::string title = doc.getTitle().ToUTF8().data();
    m_db->Execute("UPDATE DOCS SET TITLE = ?", {title});
}
```

### Управление памятью wxWidgets-объектов

```cpp
// wxWidgets управляет памятью дочерних виджетов самостоятельно
// НЕ удалять вручную виджеты, добавленные в parent!

wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "OES");
wxButton* btn = new wxButton(frame, wxID_OK, "OK");
// btn будет удалён автоматически когда frame уничтожится

// ПЛОХО
delete btn;  // UB — wxWidgets уже удалит его

// Исключение: объекты без parent удаляем сами
wxBitmap* bmp = new wxBitmap("logo.png");
// ... используем ...
delete bmp;  // OK — нет parent, наша ответственность
// Или лучше:
wxBitmap bmp("logo.png");  // На стеке, если возможно
```

---

## Производительность

### Избегайте лишних копий

```cpp
// ПЛОХО — копирует строку при каждом вызове
void processTitle(wxString title) { /* ... */ }

// ХОРОШО — константная ссылка
void processTitle(const wxString& title) { /* ... */ }

// ХОРОШО — move для передачи владения
void setTitle(wxString title) {
    m_title = std::move(title);  // Перемещение вместо копирования
}
```

### Пагинация при загрузке больших наборов данных

```cpp
// ПЛОХО — загружает все записи в память
auto allRecords = m_db->Execute("SELECT * FROM DOCUMENTS");
// Может вернуть миллионы записей!

// ХОРОШО — пагинация (Firebird синтаксис)
auto page = m_db->Execute(
    "SELECT FIRST ? SKIP ? * FROM DOCUMENTS ORDER BY CREATED_AT DESC",
    {std::to_string(pageSize), std::to_string(offset)}
);

// Или ROWS ... TO ... (Firebird)
auto page = m_db->Execute(
    "SELECT * FROM DOCUMENTS ORDER BY CREATED_AT DESC ROWS ? TO ?",
    {std::to_string(startRow), std::to_string(endRow)}
);
```

### Профилирование медленных запросов

```cpp
// Логировать медленные запросы
// OesScopeTimer — предложенная утилита (не реализована в кодовой базе).
// До добавления используйте следующий inline-паттерн:
class TimedQuery {
public:
    TimedQuery(ibDatabaseLayer* db, const wxString& sql,
               long thresholdMs = 1000)
        : m_start(std::chrono::steady_clock::now())
        , m_sql(sql) {
        // Использовать ibPreparedStatement + SetParamString/SetParamInt
        // для передачи параметров (не конкатенировать их в sql!)
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

## Чеклист для каждого PR

- [ ] Нет голых `new`/`delete` — используются умные указатели
- [ ] Нет пустых блоков `catch`
- [ ] SQL-запросы используют параметры, не конкатенацию
- [ ] Пароли и чувствительные данные не в логах
- [ ] Нет `strcpy`/`sprintf` без проверки длины буфера
- [ ] Новые методы с правильной обработкой ошибок
- [ ] Ресурсы освобождаются через RAII
- [ ] Обновления UI происходят только в главном потоке (wxWidgets)
- [ ] cppcheck не выдаёт новых предупреждений
- [ ] Компилятор собирает без предупреждений (`/W4` или `-Wall -Wextra`)
