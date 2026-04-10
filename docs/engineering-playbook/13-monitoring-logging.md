# 13. Мониторинг и логирование

## Стек мониторинга

| Компонент | Инструмент | Назначение |
|-----------|-----------|------------|
| Логи приложения | wxLog + файловый вывод | Сбор и запись событий |
| Логи БД | Собственный обработчик ошибок ibDatabaseLayer | Медленные запросы, ошибки соединения |
| Метрики производительности | Intel VTune / Windows Performance Monitor | CPU, RAM, I/O, время выполнения |
| Дампы при сбоях | Windows Error Reporting / MiniDump | Анализ аварийных завершений |
| Диагностика в runtime | DebugView (Sysinternals) | Перехват OutputDebugString |
| Тест производительности | Very Sleepy / Superluminal | Профилирование CPU |

---

## Логирование

### Принципы

1. **Уровни** — `wxLogError`, `wxLogWarning`, `wxLogMessage` (info), `wxLogDebug` — в Release только Warning и выше
2. **Контекст** — каждое сообщение содержит: модуль, операцию, значимые параметры
3. **Без чувствительных данных** — никаких паролей, строк подключения в открытом виде, персональных данных пользователей
4. **Не логировать SQL с пользовательскими данными** — только шаблоны запросов
5. **Атомарность** — одна логическая операция = один лог-блок с результатом

### Уровни wxLog

| Макрос | Уровень | Когда использовать |
|--------|---------|-------------------|
| `wxLogError` | Error | Ошибка, требующая внимания; операция не выполнена |
| `wxLogWarning` | Warning | Нештатная ситуация, работа продолжена с ограничениями |
| `wxLogMessage` | Info | Значимые события: открытие БД, сохранение документа |
| `wxLogVerbose` | Verbose | Детальная диагностика (только Debug-сборка) |
| `wxLogDebug` | Debug | Отладочная информация (только Debug-сборка) |

### Формат сообщений

Единый формат для структурированного разбора:

```
[МОДУЛЬ] Действие: результат | param1=value1 param2=value2
```

Примеры:

```
[Database] Открытие соединения: успешно | dsn=firebird://localhost/mydb
[Database] Выполнение запроса: ошибка | table=documents duration_ms=0
[Designer] Сохранение документа: успешно | doc_id=1042 rows=15
[Report] Генерация отчёта: ошибка | report=balance detail=Нет данных за период
```

### Настройка wxLog в приложении

```cpp
// main.cpp — инициализация логирования

#include <wx/log.h>
#include <wx/ffile.h>

class OesFileLog : public wxLogFile
{
public:
    explicit OesFileLog(const wxString& filename)
        : wxLogFile(filename)
    {}

protected:
    void DoLogRecord(wxLogLevel level,
                     const wxString& msg,
                     const wxLogRecordInfo& info) override
    {
        wxString formatted;
        formatted.Printf("[%s] [%s:%d] %s",
            wxDateTime::Now().Format("%Y-%m-%d %H:%M:%S"),
            info.filename ? wxString::FromUTF8(info.filename) : wxString("?"),
            info.line,
            msg);
        wxLogFile::DoLogRecord(level, formatted, info);
    }
};

// В App::OnInit()
wxString logPath = wxStandardPaths::Get().GetUserDataDir() + "/logs/oes.log";
wxLog* fileLog = new OesFileLog(logPath);
wxLog::SetActiveTarget(fileLog);

#ifdef NDEBUG
    wxLog::SetLogLevel(wxLOG_Warning);   // Release: Warning и выше
#else
    wxLog::SetLogLevel(wxLOG_Debug);     // Debug: всё
#endif
```

### Что логировать

| Событие | Уровень | Пример |
|---------|---------|--------|
| Открытие/закрытие БД | Info | `[Database] Соединение открыто: firebird://localhost/db` |
| Ошибка подключения к БД | Error | `[Database] Ошибка соединения: connection refused` |
| Медленный запрос (> 1 сек) | Warning | `[Database] Медленный запрос: table=reports duration_ms=1540` |
| Сохранение документа | Info | `[Designer] Документ сохранён: id=42` |
| Ошибка сохранения | Error | `[Designer] Ошибка сохранения: disk full` |
| Инициализация модуля | Info | `[Module] Модуль отчётов инициализирован` |
| Неожиданное исключение | Error | `[Core] Unhandled exception: what()=...` |
| Смена пользователя/сессии | Info | `[Auth] Авторизация: user=admin` |

### Чего НЕ логировать

- Пароли и строки подключения целиком
- SQL-параметры с персональными данными (имена, ИНН, телефоны)
- Полные бинарные буферы
- Приватные ключи и токены

---

## Файлы логов

### Расположение

```
Windows: %APPDATA%\OES\logs\
  oes.log          — текущий лог
  oes_2026-04-10.log  — ротированный (по дате)

Cross-platform (wxStandardPaths):
  wxStandardPaths::Get().GetUserDataDir() + "/logs/"
```

### Ротация логов

```cpp
// Простая ротация: при старте приложения переименовать старый лог
void RotateLogFile(const wxString& logPath)
{
    if (wxFileExists(logPath))
    {
        wxString dated = logPath.BeforeLast('.') + "_"
            + wxDateTime::Now().Format("%Y-%m-%d")
            + ".log";
        wxRenameFile(logPath, dated);
    }

    // Удалить логи старше 30 дней
    wxString logDir = wxFileName(logPath).GetPath();
    wxArrayString oldLogs;
    wxDir::GetAllFiles(logDir, &oldLogs, "oes_*.log");
    wxDateTime cutoff = wxDateTime::Now() - wxDateSpan::Days(30);

    for (const auto& f : oldLogs)
    {
        wxFileName fn(f);
        wxDateTime modified;
        fn.GetTimes(nullptr, &modified, nullptr);
        if (modified.IsEarlierThan(cutoff))
            wxRemoveFile(f);
    }
}
```

---

## Обработка ошибок БД

### Логирование ошибок ibDatabaseLayer

```cpp
// Обёртка для безопасного выполнения запросов с логированием
bool ExecuteQuery(ibDatabaseLayer* db, const wxString& sql,
                  const wxString& context)
{
    wxStopWatch sw;
    sw.Start();

    bool ok = db->RunQuery(sql);
    long elapsed = sw.Time();

    if (!ok)
    {
        wxLogError("[Database] Ошибка запроса | context=%s error=%s",
            context, db->GetErrorMessage());
        return false;
    }

    if (elapsed > 1000)
    {
        wxLogWarning("[Database] Медленный запрос | context=%s duration_ms=%ld",
            context, elapsed);
    }
    else
    {
        wxLogVerbose("[Database] Запрос выполнен | context=%s duration_ms=%ld",
            context, elapsed);
    }

    return true;
}
```

### Логирование ошибок соединения

```cpp
bool ibDatabaseLayer::OpenConnection(const DbConnectionParams& params)
{
    wxLogMessage("[Database] Открытие соединения | driver=%s host=%s db=%s",
        params.driver, params.host, params.database);

    if (!Open(params.BuildDSN()))
    {
        wxLogError("[Database] Ошибка соединения | driver=%s host=%s error=%s",
            params.driver, params.host, GetErrorMessage());
        return false;
    }

    wxLogMessage("[Database] Соединение открыто успешно | driver=%s",
        params.driver);
    return true;
}
```

---

## Аварийные дампы (Crash Dumps)

> **Примечание: этот раздел актуален только для Windows.**
> На Linux аварийное завершение обрабатывается через обработчики сигналов (`SIGSEGV`, `SIGABRT`) и core dump-файлы.
> Для анализа core dumps на Linux используйте `gdb -c core ./OES` или `coredumpctl debug`.

### Windows MiniDump

```cpp
// crash_handler.cpp
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

LONG WINAPI OesUnhandledExceptionFilter(EXCEPTION_POINTERS* exInfo)
{
    wxString dumpPath = wxStandardPaths::Get().GetUserDataDir()
        + wxString::Format("/crash_%s.dmp",
            wxDateTime::Now().Format("%Y%m%d_%H%M%S"));

    HANDLE hFile = CreateFile(dumpPath.wc_str(), GENERIC_WRITE,
        0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION dumpInfo{};
        dumpInfo.ThreadId          = GetCurrentThreadId();
        dumpInfo.ExceptionPointers = exInfo;
        dumpInfo.ClientPointers    = FALSE;

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
            hFile, MiniDumpWithDataSegs, &dumpInfo, nullptr, nullptr);

        CloseHandle(hFile);
    }

    wxLogError("[Core] Критическое исключение. Дамп сохранён: %s", dumpPath);
    return EXCEPTION_EXECUTE_HANDLER;
}

// В App::OnInit() — до любой другой инициализации
SetUnhandledExceptionFilter(OesUnhandledExceptionFilter);
```

---

## Диагностика производительности

### Замер времени операций

```cpp
// Вспомогательный RAII-класс для замера и логирования
class OesScopeTimer
{
public:
    OesScopeTimer(const wxString& operation, long warnThresholdMs = 500)
        : m_operation(operation), m_warnMs(warnThresholdMs)
    {
        m_sw.Start();
    }

    ~OesScopeTimer()
    {
        long elapsed = m_sw.Time();
        if (elapsed >= m_warnMs)
        {
            wxLogWarning("[Perf] Медленная операция: %s | duration_ms=%ld",
                m_operation, elapsed);
        }
        else
        {
            wxLogVerbose("[Perf] Операция завершена: %s | duration_ms=%ld",
                m_operation, elapsed);
        }
    }

private:
    wxString   m_operation;
    wxStopWatch m_sw;
    long       m_warnMs;
};

// Использование
void ReportModule::GenerateReport(const ReportParams& params)
{
    OesScopeTimer timer("GenerateReport", 1000);
    // ... логика генерации
}
```

---

## Health Check (диагностика состояния)

### Функция проверки состояния приложения

```cpp
struct OesHealthStatus
{
    bool  databaseOk    = false;
    bool  configOk      = false;
    bool  diskSpaceOk   = false;
    wxString summary;
};

OesHealthStatus OesApp::CheckHealth()
{
    OesHealthStatus status;

    // Проверка соединения с БД
    if (m_db && m_db->IsOpen())
    {
        ibResultSet* rs = m_db->RunQueryWithResults("SELECT 1 FROM RDB$DATABASE");
        status.databaseOk = (rs != nullptr);
        if (rs) rs->Close();
    }

    // Проверка конфигурации
    status.configOk = wxFileExists(m_configPath);

    // Проверка свободного места на диске (минимум 100 МБ)
    // Проверяем место на диске там, где хранятся данные приложения,
    // а не в текущем рабочем каталоге (wxGetCwd() может указывать на системный диск).
    wxDiskspaceSize_t freeSpace = 0;
    wxString appDataDir = wxStandardPaths::Get().GetUserDataDir();
    wxGetDiskSpace(appDataDir, nullptr, &freeSpace);
    status.diskSpaceOk = (freeSpace > 100LL * 1024 * 1024);

    // Итоговый статус
    if (!status.databaseOk)
        status.summary += "Database: ERROR; ";
    if (!status.configOk)
        status.summary += "Config: ERROR; ";
    if (!status.diskSpaceOk)
        status.summary += "DiskSpace: WARNING; ";

    if (status.summary.IsEmpty())
        status.summary = "OK";

    return status;
}
```

---

## Чеклист мониторинга

### При разработке

- [ ] Все публичные методы слоя БД логируют ошибки через `wxLogError`
- [ ] Медленные операции (> 500 мс) логируются через `wxLogWarning`
- [ ] Инициализация модулей логируется через `wxLogMessage`
- [ ] Нет чувствительных данных в логах

### При сборке Release

- [ ] Уровень лога установлен в `wxLOG_Warning` (не Debug)
- [ ] Подключён обработчик аварийных завершений (MiniDump)
- [ ] Путь к логам указывает в `%APPDATA%\OES\logs\`
- [ ] Ротация логов активна

### Периодически

- [ ] Проверить логи на наличие повторяющихся ошибок
- [ ] Проанализировать предупреждения о медленных запросах
- [ ] Убедиться, что дампы (если есть) проанализированы
- [ ] Очистить старые лог-файлы (> 30 дней)
