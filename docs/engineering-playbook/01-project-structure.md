# 01. Структура проекта

## Организация репозиториев

Все проекты хранятся в **одной GitHub организации** с разграничением доступа через **Teams**.

<!-- TODO: вставить ссылку на организацию -->

### Структура организации

```
Organization (GitHub Org)
│
├── Teams (управление доступом):
│   ├── oes-core          — доступ к основным репозиториям платформы
│   ├── oes-plugins       — доступ к репозиториям плагинов и расширений
│   ├── devops            — доступ ко всем репозиториям
│   └── management        — read-only ко всему
│
├── Репозитории платформы OES:
│   ├── enterprise        — Основной репозиторий C++ платформы (этот)
│   ├── oes-plugins       — Репозиторий плагинов и расширений
│   └── oes-docs          — Пользовательская документация
│
├── Общие:
│   ├── engineering-playbook — Этот каталог (стандарты разработки)
│   └── shared-cmake         — Общие CMake модули и toolchain-файлы
```

### Teams и права доступа

| Team | Свои repos | Чужие repos | Shared repos | Роль |
|------|-----------|-------------|--------------|------|
| oes-core | Write | — | Read | Разработчики ядра платформы |
| oes-plugins | — | Write | Read | Разработчики плагинов |
| devops | Admin | Admin | Admin | Инфраструктура, CI/CD |
| management | Read | Read | Read | Менеджмент, обзор |

**Преимущества Teams:**
- Человек видит только репозитории своей команды
- Права настраиваются на уровне команды, не на каждый репо
- Легко добавлять/удалять людей
- Audit log на уровне организации
- Отдельные секреты и deploy keys по команде

### Принцип: один продукт = один репозиторий

Каждый самостоятельный компонент или расширение платформы живёт в отдельном репозитории. Это упрощает сборку, управление доступом и CI/CD.

**Именование репозиториев:** `kebab-case`, описательно, с префиксом проекта при необходимости.

```
oes-enterprise         — не "OESCore" или "oes_enterprise"
oes-plugin-reporting   — не "ReportingPlugin"
engineering-playbook   — не "DevGuidelines"
```

---

## Стандартная структура файлов проекта

### Монолитный C++ репозиторий (текущая структура)

OES Enterprise — монолитный десктопный продукт. Всё живёт в одном репозитории.

```
enterprise/
├── src/                          — Исходный код
│   └── engine/                   — Движок платформы
│       ├── backend/              — Ядро платформы (бизнес-логика, компилятор, БД)
│       │   ├── appData.cpp       — ibApplicationData: авторизация, сессия (AuthenticationAndSetUser())
│       │   ├── appDataQuery.cpp  — Запросы сессий и пользователей
│       │   ├── metadataConfiguration.cpp — Управление конфигурацией метаданных
│       │   ├── compiler/         — Встроенный скриптовый движок
│       │   │   ├── compileCode.cpp  — ibCompileCode, ibTranslateCode
│       │   │   ├── procUnit.cpp     — ibProcUnit: интерпретатор байткода
│       │   │   └── value.h          — ibValue, ibNumber (ttmath 128-bit), ibValueTypes
│       │   ├── databaseLayer/    — Абстракция над СУБД
│       │   │   ├── databaseLayer.h        — ibDatabaseLayer (базовый класс)
│       │   │   ├── firebird/              — ibDatabaseLayerFirebird
│       │   │   ├── postgres/              — ibDatabaseLayerPostgres
│       │   │   ├── sqlite/                — ibDatabaseLayerSQLite
│       │   │   ├── mysql/                 — ibDatabaseLayerMySQL
│       │   │   └── odbc/                  — ibDatabaseLayerODBC
│       │   ├── metaCollection/   — Метаданные объектов
│       │   │   └── partial/
│       │   │       └── commonObjectQuery.cpp — CRUD запросы, CreateAndUpdateTableDB()
│       │   └── debugger/
│       │       └── debugServer.cpp  — ibDebuggerServer
│       ├── frontend/             — UI компоненты (wxWidgets)
│       │   └── visualView/
│       │       └── ctrl/         — ibValueForm, ibValueFrame, ibValueTextCtrl,
│       │                           ibValueButton, ibValueModelTableBox
│       ├── designer/             — Low-code дизайнер форм и отчётов
│       │   └── mainApp.cpp       — Точка входа дизайнера
│       └── enterprise/           — Среда исполнения (runtime)
│           └── mainApp.cpp       — Точка входа приложения
├── tests/                        — Google Test тесты
│   ├── unit/                     — Юнит-тесты (зеркалируют структуру src/)
│   │   ├── backend/
│   │   ├── designer/
│   │   └── frontend/
│   └── integration/              — Интеграционные тесты
├── docs/                         — Документация
│   ├── engineering-playbook/     — Стандарты разработки (этот каталог)
│   ├── architecture/             — Архитектурные описания
│   │   ├── overview.md
│   │   └── adr/                  — Architecture Decision Records
│   └── api/                      — API публичных интерфейсов
├── cmake/                        — CMake модули и вспомогательные скрипты
│   ├── FindFirebird.cmake
│   ├── FindwxWidgets.cmake
│   └── CompilerOptions.cmake
├── scripts/                      — Скрипты автоматизации (сборка, деплой)
│   ├── build.ps1                 — PowerShell сборка для Windows
│   └── package.ps1               — Упаковка дистрибутива
├── third_party/                  — Вендоренные зависимости (если не через vcpkg/conan)
├── resources/                    — Ресурсы приложения
│   ├── icons/
│   ├── strings/                  — Строки локализации
│   └── templates/                — Шаблоны
├── .github/
│   ├── workflows/                — GitHub Actions CI/CD
│   │   ├── build.yml
│   │   └── test.yml
│   └── PULL_REQUEST_TEMPLATE.md
├── CMakeLists.txt                — Корневой CMake (macOS/Linux; Windows использует MSBuild)
├── enterprise.sln                — Visual Studio Solution (основная система сборки, Windows)
├── .gitignore
├── CLAUDE.md
├── .claude.json
└── README.md
```

### Организация заголовочных и исходных файлов

В C++ каждый логический модуль состоит из пары `.h` / `.cpp`. Внутри подсистемы:

```
src/engine/backend/databaseLayer/
├── databaseLayer.h          — ibDatabaseLayer (базовый класс), ibPreparedStatement, ibDatabaseResultSet
├── databaseLayer.cpp        — реализация
├── firebird/
│   ├── databaseLayerFirebird.h   — ibDatabaseLayerFirebird
│   └── databaseLayerFirebird.cpp
└── postgres/
    ├── databaseLayerPostgres.h   — ibDatabaseLayerPostgres
    └── databaseLayerPostgres.cpp
```

**Правило:** заголовочный файл содержит **интерфейс** (объявления), `.cpp` — **реализацию**. Определения в заголовках допустимы только для шаблонов и `inline`-функций.

---

## Обязательные файлы

Каждый репозиторий **обязан** содержать:

### 1. README.md

Описание проекта, как собрать, как запустить. Подробнее в [04-documentation.md](./04-documentation.md).

```markdown
# OES Enterprise

Cross-platform enterprise low-code/no-code platform for building business applications.

## Tech Stack
- **Language:** C++17
- **GUI:** wxWidgets 3.3.2
- **Build:** MSBuild (VS 2017+), transitioning to CMake
- **Databases:** Firebird (primary), PostgreSQL, SQLite, MySQL, ODBC
- **Tests:** Google Test
- **License:** LGPL 2.1

## Prerequisites

- Visual Studio 2017+ (с Desktop C++ workload)
- wxWidgets 3.3.2 (собранные библиотеки)
- Firebird 4.x (для разработки с локальной БД)
- CMake 3.20+ (для новой системы сборки)

## Building

### MSBuild (текущий способ)
```
Открыть enterprise.sln в Visual Studio
Выбрать конфигурацию Debug или Release
Build → Build Solution (Ctrl+Shift+B)
```

### CMake (переходная цель)
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running Tests
```bash
cd build
ctest --output-on-failure
```

## Project Structure
Brief overview of key directories.
```

### 2. CLAUDE.md

Файл контекста для AI-агентов (Claude Code, Claude Desktop). Это самый важный файл для эффективной работы с AI — он заменяет часы объяснений при каждой новой сессии.

**Структура CLAUDE.md:**

```markdown
# CLAUDE.md — OES Enterprise

## Project Overview
Open Enterprise Solutions (OES) — C++ cross-platform low-code/no-code платформа
для создания бизнес-приложений. Позволяет конечным пользователям проектировать
формы, отчёты и бизнес-логику без написания кода.

## Tech Stack
- Language: C++17
- GUI: wxWidgets 3.3.2
- Build: MSBuild (VS 2017+), transitioning to CMake 3.20+
- Databases: Firebird 4.x (primary), PostgreSQL, SQLite, MySQL, ODBC
- Tests: Google Test (вводится)
- CI/CD: GitHub Actions (вводится)
- Platform: Windows primary, cross-platform goal
- License: LGPL 2.1

## Project Structure
- src/engine/backend/           — ядро платформы, компилятор, абстракция БД
  - compiler/compileCode.cpp    — ibCompileCode, ibTranslateCode
  - compiler/procUnit.cpp       — ibProcUnit (интерпретатор байткода)
  - compiler/value.h            — ibValue, ibNumber (ttmath 128-bit), ibValueTypes
  - appData.cpp                 — ibApplicationData (авторизация, AuthenticationAndSetUser())
  - appDataQuery.cpp            — запросы сессий и пользователей
  - databaseLayer/              — ibDatabaseLayer + драйверы Firebird/Postgres/SQLite/MySQL/ODBC
  - metaCollection/partial/commonObjectQuery.cpp — CRUD, CreateAndUpdateTableDB()
  - metadataConfiguration.cpp   — управление конфигурацией
  - debugger/debugServer.cpp    — ibDebuggerServer
- src/engine/frontend/          — wxWidgets UI компоненты
  - visualView/ctrl/            — ibValueForm, ibValueFrame, ibValueTextCtrl,
                                  ibValueButton, ibValueModelTableBox
- src/engine/designer/          — визуальный дизайнер форм и отчётов
  - mainApp.cpp                 — точка входа дизайнера
- src/engine/enterprise/        — среда исполнения (runtime)
  - mainApp.cpp                 — точка входа приложения
- tests/                        — Google Test (unit + integration)
- docs/                         — документация, ADR, playbook

## Key Patterns
- RAII для всех ресурсов: ibTransactionGuard (транзакции), файловые дескрипторы, GDI объекты
- std::unique_ptr / std::shared_ptr — без сырых owning указателей
- ibPreparedStatement с SetParamString/SetParamInt — параметризованные запросы
- Паттерн Observer через wxWidgets events
- ibApplicationData — Application-level singleton (авторизация, сессия)
- ibBackendCoreException / ibBackendInterruptException — иерархия исключений

## Build Commands
```bash
# MSBuild (Windows — основная система сборки)
msbuild enterprise.sln /p:Configuration=Debug /p:Platform=x64

# CMake (macOS/Linux — создаётся отдельно)
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DwxWidgets_ROOT_DIR=/usr/local
cmake --build build --config Debug

# Запуск тестов
ctest --test-dir build --output-on-failure
```

## Database
- Первичная СУБД: Firebird 4.x
- Базовый класс: ibDatabaseLayer; реализации ibDatabaseLayerFirebird, ibDatabaseLayerPostgres и др.
- Параметризованные запросы обязательны: ibPreparedStatement + SetParamString/SetParamInt — конкатенация SQL запрещена
- Транзакции через ibTransactionGuard (RAII-обёртка, commonObject.h)
- Схема обновляется через CreateAndUpdateTableDB() в commonObjectQuery.cpp

## Business Rules
- Документы нельзя удалять физически, только помечать удалёнными
- Все операции с БД логируются
- Плагины загружаются из каталога plugins/ рядом с exe

## Current State
Переход с MSBuild на CMake. Внедрение Google Test. Настройка GitHub Actions CI.
Известный технический долг: [ссылка на issues]

## Conventions
- Коммиты: type: description (на English)
- Ветки: feature/*, fix/*, hotfix/*
- Классы: PascalCase, методы: camelCase, константы: UPPER_SNAKE_CASE
- Файлы: lowercase camelCase для классов OES (databaseLayer.h, compileCode.cpp), lowercase для утилит
- Namespace: oes::core, oes::designer, oes::runtime

## Do NOT
- Не использовать сырые owning указатели (new без немедленного присвоения smart ptr)
- Не использовать исключения в деструкторах
- Не смешивать кириллицу в идентификаторах кода
- Не хардкодить пути к файлам или строки подключения к БД
- Не коммитить .env и конфиги с реальными credentials
- Не отключать warnings компилятора без комментария почему
```

### 3. .claude.json

Конфигурация для Claude Code — MCP (Model Context Protocol) подключения и настройки.

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/path/to/enterprise"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_TOKEN": "ghp_xxx"
      }
    }
  }
}
```

**Важно:** `.claude.json` может содержать токены для локальной работы. Добавьте его в `.gitignore` если в нём есть реальные credentials. Шаблон без credentials можно коммитить как `.claude.json.example`.

### 4. .gitignore

Стандартный `.gitignore` для C++ проектов на Visual Studio + CMake:

```gitignore
# Visual Studio
*.user
*.suo
*.sdf
*.opensdf
*.VC.db
*.VC.opendb
.vs/
ipch/

# Build output (MSBuild)
Debug/
Release/
x64/
Win32/
*.obj
*.pdb
*.ilk
*.exp
*.lib
*.dll
*.exe

# Build output (CMake)
build/
cmake-build-*/
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
CTestTestfile.cmake
_deps/

# Dependency managers
vcpkg_installed/
conan_cache/

# IDE
.vscode/
.idea/
*.swp
*.swo

# OS
.DS_Store
Thumbs.db

# Logs
*.log
logs/

# Test results
Testing/
test-results/

# Keys and certificates
*.pem
*.key
*.crt

# Local configuration with credentials
.claude.json
*.env
local.config

# Compiled resources
*.res
```

---

## Именование файлов и директорий

**Классы и заголовки:** в OES используется префикс `ib` (IntelBase) без PascalCase-разделения. Имя файла соответствует имени класса:
```
databaseLayer.h / databaseLayer.cpp         — ibDatabaseLayer (базовый класс)
databaseLayerFirebird.h / .cpp              — ibDatabaseLayerFirebird
compileCode.h / compileCode.cpp             — ibCompileCode, ibTranslateCode
value.h                                     — ibValue, ibValueTypes enum
```

**Утилиты и helpers:** `snake_case` или `camelCase` (консистентно в рамках модуля).
```
string_utils.h
date_helpers.cpp
```

**Директории:** `lowercase` или `kebab-case`.
```
src/core/database/
src/gui/controls/
docs/engineering-playbook/
```

**Плохо:**
```
Src/Core/Database/     — заглавные буквы в директориях
databasemanager.h      — не отражает имя класса
DB_Manager.h           — смешение стилей
```

---

## Создание нового модуля — чеклист

1. Создать директорию в `src/engine/backend/` (бэкенд) или `src/engine/frontend/` (UI) по смыслу
2. Определить интерфейс через чисто виртуальный класс (например `INotification.h`) — если модуль расширяем
3. Реализовать класс `Notification.h` / `Notification.cpp`
4. Добавить в `CMakeLists.txt` (macOS/Linux) или `enterprise.sln`-проект (Windows/MSBuild) соответствующие файлы
5. Добавить юнит-тесты в `tests/unit/backend/notifications/` (или `frontend/`)
6. Обновить `CLAUDE.md` — добавить описание нового модуля
7. Создать `config.ini.example` (или обновить существующий) если модуль вводит новые конфигурационные параметры
8. Первый коммит: `feat: add notifications module skeleton`
9. При изменении публичного интерфейса — обновить `docs/architecture/`

---

## Namespace-конвенция

Классы OES используют префикс `ib` (IntelBase) и не обёрнуты в C++ namespace. Это историческое соглашение проекта:

```cpp
// Реальные классы платформы — префикс ib, без namespace
class ibDatabaseLayer { ... };        // src/engine/backend/databaseLayer/
class ibApplicationData { ... };      // src/engine/backend/appData.cpp
class ibCompileCode { ... };          // src/engine/backend/compiler/
class ibValueForm { ... };            // src/engine/frontend/visualView/ctrl/
class ibTransactionGuard { ... };     // src/engine/backend/ (RAII для транзакций)
class ibPreparedStatement { ... };    // src/engine/backend/databaseLayer/
```

Для новых тестовых и вспомогательных модулей допустимо использовать namespace `oes`:
```cpp
namespace oes::tests {
    // тестовые фабрики, хелперы
}
```

Сторонние библиотеки (wxWidgets, Google Test) используются как есть.
