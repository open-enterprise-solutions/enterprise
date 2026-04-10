# 21. Дистрибуция, обновления и отказоустойчивость

## Сборка и дистрибуция

### Целевые платформы

| Платформа | Формат дистрибутива | Инструмент сборки |
|-----------|--------------------|--------------------|
| Windows (x64) | Inno Setup Installer (.exe) | MSBuild + Inno Setup |
| Linux (x64) | AppImage / .deb / .rpm | CMake + CPack |
| macOS (x64 / ARM64) | .dmg / .pkg | CMake + CPack |

### Матрица конфигураций сборки

| Конфигурация | Назначение | Оптимизации |
|-------------|-----------|-------------|
| Debug | Локальная разработка | Нет, полные символы отладки |
| Release | Production-сборки | /O2 (MSVC) / -O2 (GCC/Clang), без отладочных символов |
| RelWithDebInfo | Для crash-репортов | /O2 + PDB / -O2 + DWARF, символы отдельно |

**Правило:** все публичные релизы собираются в `RelWithDebInfo`. Символы (PDB / DWARF) сохраняются в защищённом хранилище и используются только для разбора краш-дампов.

### CMake / MSBuild — базовая структура

```cmake
# CMakeLists.txt (корень)
cmake_minimum_required(VERSION 3.22)
project(OES VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Версия приложения (из git tag или вручную)
configure_file(src/version.h.in src/version.h @ONLY)

# wxWidgets
find_package(wxWidgets 3.3.2 REQUIRED COMPONENTS core base aui adv)
include(${wxWidgets_USE_FILE})

# Основной исполняемый файл
add_executable(oes WIN32
    src/main.cpp
    src/app.cpp
    # ...
)
target_link_libraries(oes PRIVATE ${wxWidgets_LIBRARIES})

# Packaging
include(CPack)
set(CPACK_PACKAGE_NAME "OpenEnterpriseSolutions")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "OES Team")
```

```cpp
// src/version.h.in
#pragma once
#define OES_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define OES_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define OES_VERSION_PATCH @PROJECT_VERSION_PATCH@
#define OES_VERSION_STRING "@PROJECT_VERSION@"
```

---

## Механизм автоматического обновления

### Архитектура

```
┌──────────────────────────────────────────────────────────┐
│  OES Приложение (клиент)                                 │
│                                                          │
│  UpdateChecker (фоновый поток)                           │
│    ↓  HTTP GET /api/updates/check?version=2.0.0&os=win   │
│  Update Server (HTTPS)                                   │
│    ↓  JSON: { "latest": "2.1.0", "url": "...", ... }     │
│  Диалог уведомления пользователю                         │
│    ↓  Пользователь подтверждает                          │
│  Загрузка инсталлятора (с проверкой SHA-256)             │
│    ↓                                                     │
│  Запуск инсталлятора → закрытие приложения               │
└──────────────────────────────────────────────────────────┘
```

### Реализация проверки обновлений

```cpp
// update_checker.h
#pragma once
#include <string>
#include <functional>

struct UpdateInfo {
    std::string version;      // "2.1.0"
    std::string downloadUrl;
    std::string sha256;       // hex-строка
    std::string releaseNotes;
    bool        isMandatory;  // принудительное обновление
};

class UpdateChecker {
public:
    // Запустить проверку в фоновом потоке wxThread
    void CheckAsync(std::function<void(const UpdateInfo&)> onUpdateAvailable);

    // Скачать и проверить целостность
    bool DownloadAndVerify(const UpdateInfo& info, const std::string& destPath);

private:
    static constexpr const char* UPDATE_URL =
        "https://updates.oes-platform.com/api/v1/check";

    std::string GetPlatformString();  // "win64", "linux64", "macos"
    bool VerifySha256(const std::string& filePath,
                      const std::string& expected);
};
```

```cpp
// update_checker.cpp (схема реализации)
//
// ВАЖНО: std::thread().detach() опасен — поток продолжает выполняться
// после уничтожения объекта UpdateChecker, что может привести к use-after-free
// (обращение к this после деструктора). Вместо detach используйте:
//   - wxThread (wxTHREAD_DETACHED) — управляется wxWidgets, безопасно удаляет себя
//   - joinable std::thread, хранимый как член класса (join в деструкторе)
//
// Рекомендуемый вариант: wxThread (уже используется в OesReportGeneratorThread)

class UpdateCheckerThread : public wxThread
{
public:
    UpdateCheckerThread(UpdateChecker* owner,
                        std::function<void(const UpdateInfo&)> cb)
        : wxThread(wxTHREAD_DETACHED)
        , m_owner(owner)
        , m_cb(std::move(cb))
    {}

protected:
    ExitCode Entry() override
    {
        std::string url = std::string(UpdateChecker::UPDATE_URL)
            + "?version=" + OES_VERSION_STRING
            + "&os=" + m_owner->GetPlatformString();

        // HTTP GET через wxHTTP или libcurl
        // ...

        // Парсинг JSON-ответа
        // Если version > current → вызываем callback в главном потоке
        // wxTheApp->CallAfter([cb = m_cb, info]() { cb(info); });
        return 0;
    }

private:
    UpdateChecker*                       m_owner;
    std::function<void(const UpdateInfo&)> m_cb;
};

void UpdateChecker::CheckAsync(
    std::function<void(const UpdateInfo&)> onUpdateAvailable)
{
    // wxTHREAD_DETACHED — wxWidgets удаляет объект потока по завершении
    auto* thread = new UpdateCheckerThread(this, onUpdateAvailable);
    if (thread->Run() != wxTHREAD_NO_ERROR)
    {
        delete thread;
        wxLogWarning("[UpdateChecker] Не удалось запустить поток проверки обновлений");
    }
}
```

### Формат ответа сервера обновлений

```json
{
    "latest_version": "2.1.0",
    "min_required_version": "1.9.0",
    "download_url": "https://cdn.oes-platform.com/releases/oes-2.1.0-win64.exe",
    "sha256": "a3f8c2d1e9b0...",
    "size_bytes": 42567890,
    "release_date": "2026-04-01",
    "mandatory": false,
    "release_notes_url": "https://oes-platform.com/changelog/2.1.0",
    "release_notes": "Исправлен краш при работе с большими таблицами Firebird..."
}
```

### Проверка цифровой подписи инсталлятора

**Windows:** инсталлятор должен быть подписан code signing сертификатом.

```bash
# Проверка перед запуском (в коде приложения через WinAPI)
# WinVerifyTrust() — проверяет Authenticode подпись
```

```cpp
// Упрощённая проверка подписи на Windows
#ifdef _WIN32
#include <wintrust.h>
#include <softpub.h>
#pragma comment(lib, "wintrust")

bool VerifyCodeSignature(const std::wstring& filePath) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
    fileInfo.pcwszFilePath = filePath.c_str();

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trustData = {};
    trustData.cbStruct = sizeof(WINTRUST_DATA);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;

    LONG result = WinVerifyTrust(nullptr, &action, &trustData);
    return result == ERROR_SUCCESS;
}
#endif
```

---

## Сбор краш-репортов (Windows Minidumps)

### Обзор

При падении OES на Windows система создаёт minidump-файл (`.dmp`) — снимок стека вызовов, регистров и части памяти на момент краша. Это основной инструмент диагностики проблем у пользователей.

### Уровни защиты от крашей

```
Уровень 1: Vectored Exception Handler
├── Перехватывает ACCESS_VIOLATION, STACK_OVERFLOW и др.
├── Записывает minidump
└── Показывает диалог пользователю + предлагает отправить репорт

Уровень 2: Structured Exception Handling (SEH) в критических путях
├── Обёртки вокруг операций с БД, файлами, плагинами
└── Fallback логика при ошибках драйверов

Уровень 3: wxApp::OnUnhandledException()
└── Последний рубеж для C++ исключений wxWidgets
```

### Реализация crash handler (Windows)

```cpp
// crash_handler.h
#pragma once
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp")

class CrashHandler {
public:
    static void Install();
    static void SetReportDir(const std::wstring& dir);

private:
    static LONG WINAPI UnhandledExceptionFilter(
        EXCEPTION_POINTERS* exceptionInfo);

    static bool WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
                               const std::wstring& dumpPath);

    static std::wstring s_reportDir;
};
#endif
```

```cpp
// crash_handler.cpp
#ifdef _WIN32
#include "crash_handler.h"
#include <shlobj.h>
#include <ctime>

std::wstring CrashHandler::s_reportDir;

void CrashHandler::Install() {
    // Получить AppData\Local\OES\CrashReports
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData);
    s_reportDir = std::wstring(appData) + L"\\OES\\CrashReports";
    CreateDirectoryW(s_reportDir.c_str(), nullptr);

    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}

LONG WINAPI CrashHandler::UnhandledExceptionFilter(
    EXCEPTION_POINTERS* exceptionInfo)
{
    // Сформировать имя файла с timestamp
    time_t t = time(nullptr);
    wchar_t dumpName[256];
    swprintf_s(dumpName, L"oes_%lld.dmp", (long long)t);
    std::wstring dumpPath = s_reportDir + L"\\" + dumpName;

    WriteMiniDump(exceptionInfo, dumpPath);

    // Показать диалог (без рекурсии — напрямую через WinAPI)
    MessageBoxW(nullptr,
        L"Open Enterprise Solutions завершился с ошибкой.\n"
        L"Отчёт о сбое сохранён. Пожалуйста, отправьте его разработчикам.",
        L"Ошибка приложения",
        MB_OK | MB_ICONERROR);

    return EXCEPTION_EXECUTE_HANDLER;
}

bool CrashHandler::WriteMiniDump(EXCEPTION_POINTERS* exceptionInfo,
                                  const std::wstring& dumpPath)
{
    HANDLE hFile = CreateFileW(dumpPath.c_str(),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exceptionInfo;
    mei.ClientPointers = FALSE;

    MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(
        MiniDumpWithDataSegs |
        MiniDumpWithProcessThreadData |
        MiniDumpWithHandleData |
        MiniDumpWithThreadInfo);

    BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(),
        hFile, dumpType, &mei, nullptr, nullptr);

    CloseHandle(hFile);
    return ok == TRUE;
}
#endif
```

### Анализ minidump

```bash
# WinDbg / cdb (с символами PDB)
cdb -z crash_20260410_143200.dmp
# Команды:
# .sympath srv*c:\symbols*https://msdl.microsoft.com/download/symbols
# .sympath+ C:\OES\Symbols\2.0.0
# !analyze -v
# k        ← стек вызовов
# ~*k      ← стеки всех потоков

# Или через Visual Studio:
# File → Open → Crash Dump → открыть .dmp
# "Debug with Native Only" → автоматически загружает символы
```

### Linux — core dumps

```bash
# Включить core dumps
ulimit -c unlimited
# /proc/sys/kernel/core_pattern = /var/crash/core-%e-%p-%t

# Установить лимит для production
# /etc/security/limits.conf:
# oes_user soft core unlimited

# Анализ с GDB
gdb /usr/bin/oes /var/crash/core-oes-12345-1712345678
# (gdb) bt       ← backtrace
# (gdb) thread apply all bt  ← все потоки
```

### Отправка краш-репортов

```cpp
// Диалог отправки репорта
class CrashReportDialog : public wxDialog {
public:
    CrashReportDialog(const wxString& dumpPath)
        : wxDialog(nullptr, wxID_ANY, "Отправить отчёт о сбое",
                   wxDefaultPosition, wxSize(500, 320))
    {
        // Показываем путь к дампу, комментарий пользователя,
        // кнопки "Отправить" / "Не отправлять"
    }

    void OnSend(wxCommandEvent&) {
        // Загружаем .dmp + лог + комментарий на сервер
        // через multipart/form-data (libcurl или wxHTTP)
        // POST https://crashes.oes-platform.com/api/v1/report
    }
};
```

---

## Отказоустойчивость подключений к базам данных

### Уровни защиты

```
Уровень 1: Connection Pool
├── Минимальный пул соединений (2-5 для десктопа)
├── Периодический heartbeat SELECT 1 (ibDatabaseLayer)
└── Ленивое переподключение при обнаружении разрыва

Уровень 2: Retry-логика
├── Автоматический retry (3 попытки с exponential backoff)
├── Отличие временных ошибок от фатальных
└── Уведомление UI только при окончательном сбое

Уровень 3: Graceful degradation
├── Кэш последних данных в памяти при потере связи
├── Read-only режим при недоступности master-БД
└── Диалог повторного подключения без потери данных
```

### Реализация retry-логики

```cpp
// db_connection.h
#pragma once
#include <functional>
#include <stdexcept>
#include <thread>
#include <chrono>

class DatabaseConnection {
public:
    // Выполнить операцию с автоматическим retry
    template<typename Func>
    auto ExecuteWithRetry(Func&& func,
                          int maxRetries = 3,
                          int baseDelayMs = 500) -> decltype(func())
    {
        int attempt = 0;
        while (true) {
            try {
                EnsureConnected();
                return func();
            } catch (const DatabaseTemporaryError& e) {
                if (++attempt >= maxRetries)
                    throw;
                // Exponential backoff: 500ms, 1000ms, 2000ms
                int delayMs = baseDelayMs * (1 << (attempt - 1));
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(delayMs));
            }
            // DatabaseFatalError и прочие — не ловим, пробрасываем
        }
    }

    bool IsConnected() const;
    void Reconnect();

private:
    void EnsureConnected();
    bool Ping();  // SELECT 1 через ibDatabaseLayer (работает для Firebird, PostgreSQL, SQLite)
};
```

### Обработка потери связи с Firebird

```cpp
// Пример использования в слое данных
std::vector<Record> DataLayer::GetRecords(int projectId) {
    return m_db.ExecuteWithRetry([&]() {
        FBStatement stmt(m_db,
            "SELECT * FROM RECORDS WHERE PROJECT_ID = ?");
        stmt.SetParam(1, projectId);
        return stmt.FetchAll<Record>();
    });
}

// В UI-слое — обработка окончательных ошибок
void MainFrame::OnLoadData(wxCommandEvent&) {
    try {
        auto records = m_dataLayer->GetRecords(m_currentProjectId);
        m_grid->LoadRecords(records);
    } catch (const DatabaseFatalError& e) {
        wxMessageBox(
            wxString::Format(
                "Не удалось подключиться к базе данных:\n%s\n\n"
                "Проверьте настройки подключения.",
                e.what()),
            "Ошибка БД", wxOK | wxICON_ERROR);
    }
}
```

### Настройки подключения с повторным подключением

```ini
; oes.ini — настройки соединения
[Database]
Type=Firebird
Host=localhost
Port=3050
Database=/var/db/oes/main.fdb
Username=SYSDBA
ConnectionTimeout=10
StatementTimeout=30
MaxRetries=3
RetryDelayMs=500
HeartbeatIntervalSec=60
```

---

## Резервное копирование данных пользователя

### Стратегия 3-2-1 для настольного приложения

- **3** копии: оригинал БД + локальный бэкап + удалённый (если настроено)
- **2** носителя: основной диск + внешний/сетевой
- **1** offsite: опциональная синхронизация с сервером компании

### Автоматический локальный бэкап (Firebird gbak)

```cpp
// backup_manager.h
class BackupManager {
public:
    // Запускается при запуске приложения и по расписанию
    void ScheduleDailyBackup();

    // Создать бэкап через gbak (Firebird utility)
    bool CreateFirebirdBackup(const std::string& dbPath,
                               const std::string& backupDir);

    // Ротация: оставить последние N бэкапов
    void RotateBackups(const std::string& backupDir,
                       int keepCount = 7);
};
```

```cpp
bool BackupManager::CreateFirebirdBackup(
    const std::string& dbPath, const std::string& backupDir)
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M", std::localtime(&t));

    std::string backupFile = backupDir + "/oes_backup_"
        + timestamp + ".fbk";

    // Вызов gbak через wxExecute
    // ПРИМЕЧАНИЕ: режим "-service service_mgr" использует Services API Firebird
    // и требует запущенного сервера Firebird (не работает с Embedded-сборкой).
    // Для встроенного (Embedded) режима используйте прямое подключение к файлу:
    //   gbak -backup -user SYSDBA -password masterkey путь_к_файлу.fdb бэкап.fbk
    // Для серверного режима через Services API:
    //   gbak -backup -service service_mgr -user SYSDBA -password masterkey ...
    wxString cmd = wxString::Format(
        "gbak -backup -user SYSDBA -password masterkey %s %s",
        dbPath, backupFile);

    long exitCode = wxExecute(cmd, wxEXEC_SYNC);
    return exitCode == 0;
}
```

### Что и как часто бэкапить

| Что | Как часто | Где хранить | Хранить |
|-----|-----------|-------------|---------|
| Firebird (.fdb) | При старте + раз в сутки | AppData\OES\Backups | 7 дней |
| SQLite (.db) | При изменении | Рядом с файлом | 5 версий |
| Конфигурация (.ini) | При изменении | AppData\OES\Config | Бессрочно |
| Лицензионные ключи | При регистрации | AppData\OES\License | Бессрочно |
| Шаблоны/проекты | По запросу | Путь по выбору пользователя | По выбору |

### Тестирование бэкапов

**Правило: бэкап, который не тестировался — не бэкап.**

```bash
# Проверка восстановления из fbk (Firebird)
gbak -restore -replace oes_backup_20260410.fbk /tmp/test_restore.fdb
isql /tmp/test_restore.fdb -user SYSDBA -pass masterkey \
  -e "SELECT COUNT(*) FROM PROJECTS;"
```

---

## Installer (Windows — Inno Setup)

Инструмент сборки установщика — **Inno Setup** (согласуется с файлом 17-ci-cd.md).
Скрипт располагается в `installer\setup.iss`, собирается командой:

```
iscc /DAppVersion=2.0.0 installer\setup.iss
```

### Базовая структура Inno Setup скрипта

```iss
; installer\setup.iss
#define AppName    "Open Enterprise Solutions"
#define AppVersion "2.0.0"
#define AppPublisher "OES Team"
#define AppExeName "oes.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf64}\OES
DefaultGroupName={#AppName}
OutputBaseFilename=OES_Setup_{#AppVersion}
OutputDir=installer\Output
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Files]
; Основные файлы приложения
Source: "release\*"; DestDir: "{app}"; Flags: recursesubdirs

; Visual C++ Redistributable (устанавливается тихо, если ещё не установлен)
Source: "redist\vcredist_x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{commondesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"

[Run]
; Установить VC++ Redistributable перед запуском приложения
Filename: "{tmp}\vcredist_x64.exe"; Parameters: "/quiet /norestart"; \
  StatusMsg: "Установка Visual C++ Redistributable..."; \
  Flags: waituntilterminated

; Предложить запустить приложение после установки
Filename: "{app}\{#AppExeName}"; Description: "Запустить {#AppName}"; \
  Flags: nowait postinstall skipifsilent

[Code]
// Опциональная проверка .NET / VC++ перед установкой
procedure InitializeWizard();
begin
  // Дополнительные проверки при необходимости
end;
```

---

## Чеклист дистрибуции и отказоустойчивости

### Минимум (каждый релиз):
- [ ] Собирается в RelWithDebInfo с отдельными символами (PDB/DWARF)
- [ ] Символы сохранены в защищённом хранилище для данной версии
- [ ] CrashHandler установлен при старте приложения (Windows)
- [ ] Проверка целостности инсталлятора (SHA-256 на сайте)
- [ ] Инсталлятор подписан code signing сертификатом (Windows)
- [ ] Автоматический локальный бэкап данных при старте
- [ ] Retry-логика для подключения к БД
- [ ] Диалог уведомления о доступном обновлении
- [ ] Версия приложения отображается в About + вшита в ресурсы

### Продвинутый (критичные развёртывания):
- [ ] Автоматическая отправка краш-репортов (с согласия пользователя)
- [ ] Сервер мониторинга краш-репортов (crash.oes-platform.com)
- [ ] Принудительные обновления при критических уязвимостях
- [ ] Rollback-механизм (предыдущий инсталлятор сохраняется)
- [ ] Тихая установка для корпоративного развёртывания (SCCM/MSI)
- [ ] Ежемесячное тестирование восстановления из бэкапа
- [ ] Runbook для каждого типа инцидента
- [ ] Дежурства при крупных релизах
