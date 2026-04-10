# 09. Окружения и деплой

## Три окружения

| Окружение | Ветка | Назначение |
|-----------|-------|-----------|
| **Local** | любая | Разработка и отладка на рабочей машине |
| **Staging** | `dev` или `release/*` | Тестирование релиза перед выдачей клиенту |
| **Production** | `master` | Релиз у клиентов (установщик, дистрибутив) |

### Принцип

Код проходит путь: **Local → Staging → Production**. Новая версия не выпускается клиентам без проверки на staging-стенде.

В отличие от веб-сервисов, «деплой» OES — это сборка установщика и его распространение (через GitHub Releases, общий диск, или прямую передачу клиенту). Серверная часть — только СУБД (Firebird/PostgreSQL), которую клиент разворачивает самостоятельно или с помощью команды поддержки.

---

## Конфигурация окружений

### Конфигурационные файлы

| Файл | Назначение | В git? |
|------|-----------|--------|
| `config.ini.example` | Шаблон со всеми параметрами (без реальных значений) | Да |
| `config.ini` | Реальная конфигурация для текущей среды | Нет |
| `config.debug.ini` | Переопределения для локальной отладки (например, более подробный уровень логирования, тестовая БД); загружается поверх `config.ini` только в Debug-сборке | Нет |

### Разница между средами

```ini
; config.ini (локальная разработка)
[app]
environment=development
log_level=debug
log_to_file=true
log_file=oes_debug.log

[database]
type=firebird
host=localhost
port=3050
database=C:\OES\dev\oes_dev.fdb
user=SYSDBA
password=masterkey

; config.ini (staging — тестовый стенд)
[app]
environment=staging
log_level=debug
log_to_file=true
log_file=C:\OES\logs\oes_staging.log

[database]
type=firebird
host=10.0.0.10
port=3050
database=D:\Databases\oes_staging.fdb
user=OES_APP
password=<staging_password>

; config.ini (production — у клиента)
[app]
environment=production
log_level=info
log_to_file=true
log_file=C:\ProgramData\OES\logs\oes.log

[database]
type=firebird
host=localhost
port=3050
database=C:\ProgramData\OES\oes.fdb
user=OES_APP
password=<client_password>
```

### Правило: config.ini.example ВСЕГДА актуален

При добавлении новой конфигурационной опции:
1. Добавить в `config.ini.example` с комментарием
2. Добавить в свой `config.ini`
3. Сообщить команде (и обновить staging/production конфиги)
4. Обновить CLAUDE.md если параметр важен для понимания проекта

---

## Локальная разработка

### Установка зависимостей (Windows)

OES не использует менеджер пакетов автоматически. Зависимости устанавливаются вручную или через vcpkg:

```powershell
# Через vcpkg (рекомендуется для новых зависимостей)
vcpkg install wxwidgets:x64-windows
vcpkg install gtest:x64-windows
vcpkg install libpq:x64-windows    # PostgreSQL клиент
vcpkg install sqlite3:x64-windows

# Интеграция с Visual Studio
vcpkg integrate install
```

Firebird и IBPP устанавливаются отдельно (см. документацию Firebird и `third-party/IBPP/`).

### Запуск локально

```powershell
# 1. Убедиться что Firebird запущен
sc query FirebirdServerDefaultInstance
# Если не запущен:
sc start FirebirdServerDefaultInstance

# 2. Создать тестовую БД (если ещё нет)
isql-fb -user SYSDBA -password masterkey
# CREATE DATABASE 'C:\OES\dev\oes_dev.fdb' PAGE_SIZE 16384 DEFAULT CHARACTER SET UTF8;
# EXIT;

# 3. Собрать проект
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64 /m

# 4. Запустить
.\x64\Debug\OES.exe
```

### Почему нет Docker для локальной разработки

OES — это desktop-приложение, использующее wxWidgets GUI. Docker не подходит для разработки GUI-приложений. Для локальной разработки:
- Сервер Firebird устанавливается нативно
- Приложение собирается и запускается напрямую через Visual Studio (Windows) или CMake (macOS/Linux)
- Отладка через MSVC (Windows) или LLDB/GDB (macOS/Linux)

**Поддерживаемые платформы:**
- **Windows** — основная платформа, сборка через `enterprise.sln` (MSBuild/Visual Studio 2017+)
- **macOS / Linux** — цель кросс-платформенного развития; сборка через CMake (создаётся отдельно)

Для кросс-платформенной разработки (macOS/Linux) используется нативная установка зависимостей через пакетный менеджер дистрибутива.

---

## Staging

### Что такое staging для OES

Staging — это тестовый стенд, максимально приближённый к реальной среде клиента:
- Отдельная машина (физическая или виртуальная) с Windows
- Отдельная БД Firebird с близкой к production схемой
- Тестовые данные (не реальные клиентские!)
- Release-сборка (не Debug)

### Деплой на staging

**Ручной деплой на staging-машину:**

```powershell
# 1. Собрать Release версию
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# 2. Собрать установщик (NSIS или WiX)
# scripts/build-installer.ps1
.\scripts\build-installer.ps1 -Version "1.2.3"

# 3. Скопировать установщик на staging машину
# (через общую папку, sftp, или вручную)
Copy-Item ".\dist\OES-1.2.3-setup.exe" "\\staging-server\deploy\"

# 4. На staging машине: запустить установщик
# \\staging-server\deploy\OES-1.2.3-setup.exe /S

# 5. Запустить smoke-тесты
# ... проверить основные функции вручную ...
```

**Через GitHub Actions (автоматический билд, см. 17-ci-cd.md):**

```yaml
# .github/workflows/build-staging.yml
name: Build Staging

on:
  push:
    branches: [dev]

jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: microsoft/setup-msbuild@v2

      - name: Build Release
        run: msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m
        # enterprise.sln — основная система сборки OES (Windows/MSBuild)

      - name: Run tests
        run: ctest --test-dir build --output-on-failure

      - name: Build installer
        run: .\scripts\build-installer.ps1 -Version "${{ github.run_number }}"

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: oes-staging-${{ github.run_number }}
          path: dist/OES-*-setup.exe
```

### Проверка после деплоя на staging

- [ ] Установщик устанавливается без ошибок
- [ ] Приложение запускается
- [ ] Подключение к БД Firebird успешно
- [ ] Основные функции работают (открытие форм, дизайнер)
- [ ] Логи в C:\ProgramData\OES\logs\ без критических ошибок
- [ ] Схема БД соответствует ожидаемой (миграции применены)
- [ ] Производительность приемлема (нет видимых тормозов)

---

## Production

### Деплой в production (выпуск релиза)

Production-деплой для OES — это публикация нового релиза, который устанавливается у клиентов.

**Процесс:**

```bash
# 1. На GitHub: создать PR dev → master (или release → master)
# 2. Описать что входит в релиз (changelog)
# 3. Получить approve от тимлида
# 4. Merge

# 5. Создать тег версии
git tag -a v1.2.0 -m "Release v1.2.0: описание изменений"
git push origin v1.2.0
```

```powershell
# 6. Собрать финальный Release
msbuild enterprise.sln /p:Configuration=Release /p:Platform=x64 /m

# 7. Прогнать полный набор тестов
ctest --test-dir build -C Release --output-on-failure

# 8. Собрать подписанный установщик
# (требует code signing certificate)
.\scripts\build-installer.ps1 -Version "1.2.0" -Sign

# 9. Загрузить на GitHub Releases
gh release create v1.2.0 `
    --title "OES v1.2.0" `
    --notes-file CHANGELOG.md `
    dist/OES-1.2.0-setup.exe

# 10. Уведомить клиентов / команду поддержки
```

### Миграции базы данных

При обновлении схемы БД нужно предоставить скрипты миграции:

```
db/migrations/
├── v1.1.0_to_v1.2.0.sql    — SQL-скрипт миграции
└── v1.1.0_to_v1.2.0.sh     — Скрипт применения миграции
```

```sql
-- db/migrations/v1.1.0_to_v1.2.0.sql
-- Миграция: добавить поле email в таблицу users
-- Автор: Ivan Petrov
-- Дата: 2026-03-15

ALTER TABLE USERS ADD EMAIL VARCHAR(255);
CREATE INDEX IDX_USERS_EMAIL ON USERS(EMAIL);

-- Обновить версию схемы
UPDATE DB_VERSION SET VERSION = '1.2.0', UPDATED_AT = CURRENT_TIMESTAMP;
```

```bash
#!/bin/bash
# db/migrations/v1.1.0_to_v1.2.0.sh
# Применить через isql-fb

isql-fb -user "$DB_USER" -password "$DB_PASSWORD" \
    "$DB_HOST:$DB_PATH" \
    -i "$(dirname "$0")/v1.1.0_to_v1.2.0.sql"

echo "Migration v1.1.0 → v1.2.0 applied successfully"
```

**Важно:** Перед применением миграции — создать бэкап БД!

### Важно: бэкап перед обновлением у клиента

```powershell
# scripts/pre-upgrade-backup.ps1
param(
    [string]$DbPath = "C:\ProgramData\OES\oes.fdb",
    [string]$BackupDir = "C:\ProgramData\OES\backups"
)

$timestamp = Get-Date -Format "yyyyMMdd_HHmm"
$backupFile = Join-Path $BackupDir "oes_pre_upgrade_${timestamp}.fbk"

# Создать директорию если не существует
New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

# Бэкап через gbak
& gbak -backup -user SYSDBA -password $env:SYSDBA_PASSWORD `
    "localhost:$DbPath" $backupFile

Write-Host "Backup created: $backupFile"
```

---

## Структура дистрибутива

### Типичная организация файлов в дистрибутиве OES

```
OES-1.2.0-setup.exe         — Установщик (NSIS/WiX)
    ↓ устанавливает в:
C:\Program Files\OES\
├── OES.exe                 — Основное приложение
├── config.ini.example      — Шаблон конфигурации
├── wxbase33u_vc_custom.dll — wxWidgets runtime
├── fbclient.dll            — Firebird клиент
├── plugins\               — Плагины и расширения
│   ├── db_firebird.dll
│   ├── db_postgresql.dll
│   └── db_sqlite.dll
└── resources\              — Иконки, переводы
    ├── i18n\
    └── icons\

C:\ProgramData\OES\         — Данные приложения (не в Program Files)
├── config.ini              — Рабочая конфигурация
├── oes.fdb                 — БД Firebird (если embedded)
├── logs\
│   └── oes.log
└── backups\                — Автоматические бэкапы
```

### Скрипт сборки установщика

```powershell
# scripts/build-installer.ps1
param(
    [string]$Version = "0.0.0",
    [switch]$Sign = $false
)

Write-Host "Building OES installer v$Version"

# 1. Проверить что Release собран
if (-not (Test-Path "x64\Release\OES.exe")) {
    throw "Release build not found. Run MSBuild first."
}

# 2. Собрать NSIS installer
& makensis /DVERSION=$Version installer\oes-setup.nsi

# 3. Подписать (если запрошено)
if ($Sign) {
    $cert = Get-Item "Cert:\CurrentUser\My\<THUMBPRINT>"
    Set-AuthenticodeSignature -FilePath "dist\OES-$Version-setup.exe" -Certificate $cert
}

Write-Host "Installer created: dist\OES-$Version-setup.exe"
```

---

## Откат

### Если новая версия сломала что-то у клиента

OES — desktop-приложение. Откат означает установку предыдущей версии.

```powershell
# 1. Восстановить предыдущий установщик из GitHub Releases
gh release download v1.1.0 --pattern "*.exe" --dir ./rollback/

# 2. Сохранить конфиг клиента (если изменился при обновлении)
Copy-Item "C:\Program Files\OES\config.ini" "C:\Temp\config_backup.ini"

# 3. Удалить новую версию
# Панель управления → Программы → OES → Удалить

# 4. Установить предыдущую версию
.\rollback\OES-1.1.0-setup.exe

# 5. Восстановить конфиг
Copy-Item "C:\Temp\config_backup.ini" "C:\Program Files\OES\config.ini"
```

### Откат миграции БД

```bash
# Перед миграцией ОБЯЗАТЕЛЬНО делать бэкап (см. выше)
# Откат — восстановление из бэкапа

gbak -restore -user SYSDBA -password "$PASSWORD" \
    /backups/oes_pre_upgrade_20260315.fbk \
    /path/to/oes.fdb

echo "Database restored from backup"
```

---

## Мониторинг и логирование

### Логи OES

```cpp
// OES пишет логи через wxLog
wxLogMessage("Application started, version %s", OES_VERSION);
wxLogWarning("Database query took %ldms (threshold: %ldms)", elapsed, threshold);
wxLogError("Failed to connect to database: %s", error.c_str());

// Файл лога: C:\ProgramData\OES\logs\oes.log (production)
//            ./oes_debug.log (development)
```

### Проверка здоровья при запуске

```cpp
// src/engine/enterprise/mainApp.cpp (точка входа enterprise)
// src/engine/designer/mainApp.cpp   (точка входа designer)
//
// ibApplicationData (appData.cpp) — отвечает за инициализацию приложения:
//   - AuthenticationAndSetUser() — авторизация пользователя
//   - Подключение к ibDatabaseLayer нужного типа (Firebird/Postgres/etc.)
//   - Загрузка метаданных через ibValueMetaObjectCatalog / ibValueMetaObjectDocument

bool OESApp::OnInit() {
    // 1. Проверить конфигурацию
    if (!m_config->IsValid()) {
        wxLogFatalError("Invalid configuration. Check config.ini");
        return false;
    }

    // 2. Инициализировать ibApplicationData и подключиться к БД
    ibApplicationData* appData = ibApplicationData::Get();
    if (!appData->Connect(m_config)) {
        wxMessageBox(
            "Cannot connect to database.\n"
            "Check config.ini and ensure Firebird server is running.",
            "Connection Error",
            wxOK | wxICON_ERROR
        );
        return false;
    }

    // 3. Авторизация пользователя
    if (!appData->AuthenticationAndSetUser(user, password)) {
        wxLogWarning("Authentication failed.");
        return false;
    }

    wxLogMessage("OES started successfully");
    return true;
}
```

### Что мониторить у клиентов

```
Регулярно проверять (при наличии удалённого доступа):
- C:\ProgramData\OES\logs\oes.log — нет ли критических ошибок?
- Размер .fdb файла — не заканчивается ли место на диске?
- Производительность запросов (из лога при DEBUG-уровне)
- Наличие свежих бэкапов в C:\ProgramData\OES\backups\
```

### Простой скрипт мониторинга логов

```powershell
# scripts/check-logs.ps1
# Найти критические ошибки в логе за последние 24 часа

$logFile = "C:\ProgramData\OES\logs\oes.log"
$yesterday = (Get-Date).AddDays(-1)

$errors = Get-Content $logFile |
    Where-Object { $_ -match "ERROR|FATAL" } |
    Where-Object {
        if ($_ -match '(\d{4}-\d{2}-\d{2})') {
            [datetime]$Matches[1] -gt $yesterday
        }
    }

if ($errors) {
    Write-Warning "Found $($errors.Count) error(s) in OES log:"
    $errors | ForEach-Object { Write-Host $_ -ForegroundColor Red }
}
else {
    Write-Host "No errors in OES log (last 24h)" -ForegroundColor Green
}
```

---

## Версионирование

### Схема версий

OES использует **Semantic Versioning**: `MAJOR.MINOR.PATCH`

| Тип | Пример | Когда |
|-----|--------|-------|
| **PATCH** | 1.2.0 → 1.2.1 | Баг-фиксы, без изменений схемы БД |
| **MINOR** | 1.2.0 → 1.3.0 | Новые функции, обратно совместимые изменения схемы |
| **MAJOR** | 1.2.0 → 2.0.0 | Breaking changes, несовместимые изменения схемы |

### Версия в коде

```cpp
// src/engine/backend/version.h  (или аналогичный файл в backend/)
#pragma once

#define OES_VERSION_MAJOR 1
#define OES_VERSION_MINOR 2
#define OES_VERSION_PATCH 0

#define OES_VERSION_STRING "1.2.0"
#define OES_VERSION_BUILD  __DATE__ " " __TIME__
```

### Changelog

Перед каждым релизом обновлять `CHANGELOG.md`:

```markdown
## [1.2.0] - 2026-03-15

### Added
- Поддержка PostgreSQL 16
- Новый тип компонента: DateRangePicker

### Fixed
- Исправлен crash при открытии дизайнера на Windows 11
- Исправлена SQL-инъекция в модуле отчётов

### Security
- Обновлён OpenSSL до 3.2.1 (CVE-2024-XXXX)
```

