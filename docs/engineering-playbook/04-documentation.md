# 04. Документация

## Принцип

Документация — это часть продукта. Код без документации — это технический долг. Если ты не задокументировал, считай что не сделал.

---

## Языковая политика

| Что | Язык | Почему |
|-----|------|--------|
| Код (переменные, функции, классы) | English | Стандарт индустрии |
| Комментарии в коде | English | Читается вместе с кодом |
| Коммиты | English | Единый формат |
| README.md, CLAUDE.md | English | Для AI-агентов и совместимости |
| Техническая документация (docs/) | English | Для AI-агентов |
| Пользовательский контент (UI тексты, справка) | Russian | Аудитория проекта |
| Внутренние обсуждения (Issues, PR описания) | Russian / English | По договорённости в команде |

---

## README.md — в каждом репозитории

README.md — первое, что видит человек (или AI) при открытии проекта. Он должен ответить на три вопроса: **что это**, **как собрать**, **как запустить**.

### Обязательные секции

```markdown
# OES Enterprise

Cross-platform enterprise low-code/no-code platform for building business applications
without writing code. Built with C++17 and wxWidgets.

## Tech Stack

- **Language:** C++17
- **GUI:** wxWidgets 3.3.2
- **Build:** MSBuild (VS 2017+), transitioning to CMake 3.20+
- **Databases:** Firebird 4.x (primary), PostgreSQL, SQLite, MySQL, ODBC
- **Tests:** Google Test
- **License:** LGPL 2.1

## Prerequisites

- Visual Studio 2017 or later (Desktop development with C++ workload)
- wxWidgets 3.3.2 (pre-built or build from source)
  - Set `WXWIN` environment variable to wxWidgets root directory
- Firebird 4.x client libraries (for Firebird database support)
- CMake 3.20+ (for CMake build path)

## Getting Started

### 1. Clone the repository

```bash
git clone git@github.com:<!-- ORG -->/enterprise.git
cd enterprise
```

### 2. Build with Visual Studio (current)

Open `enterprise.sln` in Visual Studio 2017+.
Select configuration `Debug` or `Release`, platform `x64`.
Build → Build Solution (`Ctrl+Shift+B`).

### 3. Build with CMake (transitional)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DwxWidgets_ROOT_DIR=%WXWIN%
cmake --build build --config Debug
```

### 4. Run tests

```bash
cd build
ctest --output-on-failure
```

## Available Build Configurations

| Configuration | Description |
|---------------|-------------|
| `Debug x64` | Debug symbols, assertions enabled |
| `Release x64` | Optimized, no debug symbols |

## Project Structure

Brief description of key directories — see `CLAUDE.md` for details.

## Deployment

How to build the installer and distribute — see `scripts/package.ps1`.

## Environment / Configuration

Database connection settings are read from `config.ini` (not committed).
See `config.ini.example` for all required settings.
```

---

## CLAUDE.md — контекст для AI-агентов

CLAUDE.md — самый важный файл для эффективной работы с AI-ассистентами. Это "мозг" проекта, который позволяет Claude Code (и другим AI) мгновенно понять контекст.

### Зачем нужен CLAUDE.md

Без CLAUDE.md каждая новая сессия с AI начинается с нуля — AI не знает структуру проекта, бизнес-правила, конвенции. CLAUDE.md решает эту проблему: AI читает его первым и сразу понимает контекст.

### Когда обновлять CLAUDE.md

- Добавлен новый модуль или подсистема
- Изменилась структура проекта
- Новые бизнес-правила или архитектурные решения
- Изменены команды сборки или запуска
- Появились новые зависимости или конфигурационные файлы
- Изменились соглашения кодирования

Полная структура CLAUDE.md описана в [01-project-structure.md](./01-project-structure.md).

> **Примечание: MCP-серверы** (описанные в разделе `.claude.json` в 01-project-structure.md) не применимы для стандартной сборки OES. Это локальный инструмент для разработчиков, использующих Claude Code. CI/CD и серверная сборка не зависят от MCP-конфигурации.

---

## config.ini.example — шаблон конфигурации

В C++ проекте нет `.env` файлов — конфигурация хранится в `config.ini` (или аналоге).

### Правила

1. **Все настройки** — каждая настройка из `config.ini` должна быть в `config.ini.example`
2. **С описаниями** — комментарий над каждой секцией или ключом
3. **Без реальных значений** — пароли и строки подключения оставляем пустыми
4. **Значения по умолчанию** — для нечувствительных настроек можно указать
5. **Коммитится в git** — это шаблон, не секрет

### Пример

```ini
; OES Enterprise Configuration Template
; Copy to config.ini and fill in your local values
; Windows example: database=C:\OES\oes_dev.fdb
; Linux example:   database=/var/lib/oes/oes_dev.fdb

[Application]
; Language: ru, en
Language=ru
; Log level: debug, info, warning, error
LogLevel=info
; Directory for user data and documents
; Windows: DataDir=C:\Users\YourName\AppData\Local\OES
; Linux:   DataDir=/home/yourname/.local/share/oes
DataDir=

[Database]
; Primary database type: firebird, postgresql, sqlite, mysql, odbc
Type=firebird

[Firebird]
; Firebird server hostname or IP
Host=localhost
; Firebird service port (default: 3050)
Port=3050
; Path to database file on the server
; Windows: DatabasePath=C:\OES\oes_dev.fdb
; Linux:   DatabasePath=/var/lib/oes/oes_dev.fdb
DatabasePath=
; Database user
User=SYSDBA
; Database password — NEVER commit real value
Password=

[PostgreSQL]
Host=localhost
Port=5432
DatabaseName=oes_dev
User=postgres
; Password — NEVER commit real value
Password=

[Plugins]
; Directory to load plugins from (relative to executable or absolute)
PluginsDir=plugins
; Comma-separated list of disabled plugins
Disabled=
```

---

## Doxygen — документация публичного API

Публичные методы и классы документируются в формате Doxygen.

### Настройка Doxygen

Файл `Doxyfile` хранится в корне репозитория. Генерация документации:

```bash
doxygen Doxyfile
# Документация генерируется в docs/api/html/
```

### Документирование классов

```cpp
/**
 * @brief Abstract base class for all OES database backends.
 *
 * ibDatabaseLayer provides a unified interface for working with multiple
 * database backends (Firebird, PostgreSQL, SQLite, MySQL, ODBC). Concrete
 * implementations: ibDatabaseLayerFirebird, ibDatabaseLayerPostgres, etc.
 *
 * Parameterized queries are mandatory — use ibPreparedStatement with
 * SetParamString/SetParamInt instead of string concatenation.
 *
 * File: src/engine/backend/databaseLayer/databaseLayer.h
 *
 * Usage:
 * @code
 * // ibTransactionGuard provides RAII rollback on scope exit (commonObject.h)
 * ibTransactionGuard tx(db);
 * auto* stmt = db->PrepareStatement("SELECT * FROM documents WHERE ID = ?");
 * stmt->SetParamInt(1, docId);
 * auto* rs = stmt->RunQuery();
 * tx.Commit();
 * @endcode
 */
class ibDatabaseLayer {
public:
    /**
     * @brief Prepares a parameterized SQL statement.
     * @param sql  SQL query with ? placeholders for parameters.
     * @return     Prepared statement — use SetParamString/SetParamInt to bind values.
     * @throws     ibBackendCoreException on SQL syntax error or connection loss.
     */
    virtual ibPreparedStatement* PrepareStatement(const wxString& sql) = 0;
};
```

### Документирование методов

```cpp
/**
 * @brief Calculates the discount for a given subscription tier.
 *
 * Discount tiers:
 * - basic:   0%
 * - premium: 15%
 * - vip:     25%
 *
 * @param originalPrice  Original price in kopecks (integer, must be >= 0).
 * @param tier           User subscription tier.
 * @return               Discounted price in kopecks.
 */
int CalculateDiscount(int originalPrice, SubscriptionTier tier) const;
```

### Когда комментировать (обычные комментарии)

```cpp
// Good: объясняет ПОЧЕМУ, а не ЧТО
// Price stored in kopecks to avoid floating-point rounding issues.
int priceInKopecks = static_cast<int>(priceInRubles * 100.0 + 0.5);

// Good: workaround с объяснением
// wxGrid requires ProcessPendingEvents() before changing column count,
// otherwise column headers get out of sync. wxWidgets bug #12345.
wxGetApp().ProcessPendingEvents();
grid->SetColCount(newCount);

// Good: TODO с ссылкой на задачу
// TODO(#142): Extract into separate thread when wxWidgets task system is available
DoHeavyOperation();

// Bad: очевидный комментарий
// Get document by id
auto doc = repository->FindById(id);

// Bad: закомментированный код
// int oldValue = GetValue();
// SetValue(newValue);
```

---

## Архитектурные решения (ADR)

### Структура docs/

```
docs/
├── engineering-playbook/    — Стандарты разработки (этот каталог)
├── architecture/
│   ├── overview.md          — Общая архитектура системы
│   ├── adr/                 — Architecture Decision Records
│   │   ├── 001-wxwidgets-gui.md
│   │   ├── 002-firebird-primary-db.md
│   │   ├── 003-plugin-interface.md
│   │   └── 004-cmake-migration.md
│   └── diagrams/            — Схемы и диаграммы (PNG, SVG, draw.io)
│       ├── system-overview.png
│       └── database-layer.png
└── api/                     — Документация публичных API
    └── plugin-api.md        — API для разработчиков плагинов
```

### Формат ADR (Architecture Decision Record)

```markdown
# ADR-004: Migrate build system from MSBuild to CMake

## Status
In Progress

## Context
The project currently uses Visual Studio MSBuild (.sln + .vcxproj). This makes
cross-platform builds difficult and ties us to Windows-specific tooling.
CMake is the industry standard for cross-platform C++ projects.

## Decision
Gradually migrate to CMake 3.20+, keeping MSBuild working in parallel during
the transition. Start with leaf modules (no dependencies on other modules),
then move to core modules.

CMake chosen because:
- Cross-platform (Windows, Linux, macOS) — required for wxWidgets cross-platform goal
- Integrates with vcpkg for dependency management
- Supported by all major IDEs (Visual Studio, CLion, VS Code)
- Industry standard for C++ projects

## Consequences
- Developers need to learn CMake syntax
- CI/CD must support both MSBuild and CMake during transition
- Existing .vcxproj files maintained until migration is complete
- Better dependency management through vcpkg in the future
```

---

## Confluence (для больших проектов)

### Когда нужен Confluence

| Размер проекта | Подход к документации |
|---------------|----------------------|
| Маленький (1-3 разработчика) | README.md + CLAUDE.md + docs/ в репо — достаточно |
| Средний (4-8 человек) | + GitHub Wiki для процессов и инструкций |
| Большой (8+ человек, несколько команд) | + **Confluence** как база знаний |

**Confluence нужен когда:**
- Есть не-технические участники (менеджеры, клиент) — им не удобно читать .md на GitHub
- Нужна структурированная база знаний с поиском по всем проектам
- Документы требуют обсуждения (комментарии, @mentions)
- Есть визуальный контент (диаграммы, скриншоты, видео-туториалы)

### Что живёт в Confluence, а что в коде

| Документ | Confluence | Репозиторий |
|----------|-----------|-------------|
| Бизнес-требования, PRD | ✅ | — |
| Архитектурные решения (ADR) | ✅ обсуждение | ✅ финальная версия в docs/ |
| API для разработчиков плагинов | ✅ обзор + примеры | ✅ Doxygen (авто) |
| README, CLAUDE.md | — | ✅ всегда |
| Runbooks (что делать при инциденте) | ✅ с картинками | — |
| Онбординг | ✅ с видео | ✅ чеклист |
| Release notes | ✅ для менеджмента | ✅ git tags |
| Код-стандарты (этот каталог) | — | ✅ engineering-playbook |
| Пользовательская документация | ✅ или отдельный репо | — |

---

## Практики документирования кода

### Заголовочные файлы (.h) — интерфейс

Заголовочный файл — это **контракт**. Он должен содержать:
- Doxygen-комментарии для публичных методов и классов
- Краткое описание назначения класса в начале файла
- `#pragma once` или include guards

```cpp
#pragma once
/**
 * @file ibValueForm.h
 * @brief OES form container — runtime and designer.
 *
 * ibValueForm is the root container for form controls at runtime.
 * Controls (ibValueTextCtrl, ibValueButton, ibValueModelTableBox, etc.)
 * are children of ibValueFrame.
 *
 * File: src/engine/frontend/visualView/ctrl/
 */

#include "ibValueFrame.h"
#include "databaseLayer/databaseLayer.h"

/**
 * @brief Root form object in the OES value system.
 *
 * ibValueForm owns the form's control tree and handles data binding
 * to ibDatabaseLayer. The form definition is stored as metadata in
 * ibValueMetaObjectCatalog / ibValueMetaObjectDocument.
 */
class ibValueForm : public ibValueFrame {
public:
    explicit ibValueForm(ibDatabaseLayer* db);
    virtual ~ibValueForm();

    /**
     * @brief Opens the form and binds it to data source.
     * @param metaId  Identifier of the form metadata object.
     * @return        true on success, false if metadata not found.
     * @throws        ibBackendCoreException on database or parse error.
     */
    bool OpenForm(int metaId);
};
```
