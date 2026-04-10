# 22. Система плагинов (C++ Plugin Architecture)

## Когда применимо

Этот раздел актуален для всех компонентов OES, которые должны поддерживать:
- Сторонние коннекторы к источникам данных (СУБД, REST API, файловые форматы)
- Кастомные виджеты и компоненты дизайнера форм
- Экспортёры отчётов (PDF, Excel, XML, HTML)
- Провайдеры аутентификации (LDAP, SAML, OAuth)
- Пользовательские действия (scripting hooks) в бизнес-логике

---

## Архитектурные принципы

### Золотое правило ABI-стабильности

> **Публичный C-интерфейс плагина НИКОГДА не меняется без инкремента мажорной версии ABI. Нарушение этого правила приводит к крашу при загрузке несовместимого плагина.**

### Ключевые ограничения C++ плагинов

| Проблема | Причина | Решение |
|----------|---------|---------|
| Несовместимость C++ ABI | Разные компиляторы/версии | `extern "C"` экспорты |
| Разные CRT (Windows) | Статическая/динамическая линковка | Единый `MSVCRT` в контракте |
| Исключения через границу DLL | Несовместимая RTTI | Не пробрасывать исключения |
| Разные `std::string` layouts | ABI между STL реализациями | Передавать только `const char*` |
| Объекты wxWidgets через границу | Версия wxWidgets | Только C-примитивы в API |

### Слои архитектуры

```
┌────────────────────────────────────────────────────────────┐
│  OES Core (ядро приложения)                                │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  PluginManager                                       │  │
│  │  - Загрузка/выгрузка DLL/SO                         │  │
│  │  - Проверка версии ABI                               │  │
│  │  - Реестр загруженных плагинов                       │  │
│  └────────────────┬────────────────────────────────────┘  │
│                   │ C ABI (extern "C")                      │
├───────────────────┼────────────────────────────────────────┤
│  Plugin Interface │ (oes_plugin_api.h — публичный заголовок)│
│  IOesPlugin (pure │ virtual через C-обёртки)                │
├───────────────────┼────────────────────────────────────────┤
│  Plugin Instance  │ (DLL/SO — сторонний код)                │
│  MyDataConnector  │ : IOesPlugin                            │
└────────────────────────────────────────────────────────────┘
```

---

## Публичный Plugin API (oes_plugin_api.h)

```cpp
// include/oes_plugin_api.h
// ПУБЛИЧНЫЙ ЗАГОЛОВОК — часть стабильного ABI
// Версия: изменения в MAJOR нарушают совместимость
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Версия ABI ───────────────────────────────────────────────────────────────
#define OES_PLUGIN_ABI_MAJOR 2
#define OES_PLUGIN_ABI_MINOR 0

// ─── Типы плагинов ────────────────────────────────────────────────────────────
typedef enum OesPluginType {
    OES_PLUGIN_DATA_SOURCE   = 1,  // Источник данных (СУБД, файл, REST)
    OES_PLUGIN_REPORT_EXPORT = 2,  // Экспортёр отчётов
    OES_PLUGIN_WIDGET        = 3,  // Кастомный UI-виджет
    OES_PLUGIN_AUTH_PROVIDER = 4,  // Провайдер аутентификации
    OES_PLUGIN_SCRIPT_HOOK   = 5,  // Хук бизнес-логики
} OesPluginType;

// ─── Коды результата ──────────────────────────────────────────────────────────
typedef enum OesResult {
    OES_OK              = 0,
    OES_ERROR           = 1,
    OES_ERROR_NOT_FOUND = 2,
    OES_ERROR_INVALID   = 3,
    OES_ERROR_IO        = 4,
    OES_ERROR_AUTH      = 5,
} OesResult;

// ─── Метаданные плагина ───────────────────────────────────────────────────────
typedef struct OesPluginMeta {
    uint32_t    abiMajor;       // Должно равняться OES_PLUGIN_ABI_MAJOR
    uint32_t    abiMinor;
    OesPluginType type;
    const char* id;             // "com.example.my-connector" — уникальный ID
    const char* name;           // "My DB Connector"
    const char* version;        // "1.2.0"
    const char* author;
    const char* licenseType;    // "LGPL-2.1", "Commercial", "MIT"
} OesPluginMeta;

// ─── Уровни логирования ───────────────────────────────────────────────────────
typedef enum OesLogLevel {
    OES_LOG_DEBUG   = 0,
    OES_LOG_INFO    = 1,
    OES_LOG_WARNING = 2,
    OES_LOG_ERROR   = 3,
} OesLogLevel;

// ─── Хост-контекст (что предоставляет ядро плагину) ──────────────────────────
typedef struct OesHostContext {
    // Логирование (плагин использует, не имея своего логгера)
    void (*logMessage)(void* hostData, OesLogLevel level, const char* message);

    // Доступ к настройкам плагина из хранилища OES
    const char* (*getSetting)(void* hostData, const char* key);

    void* hostData;  // Непрозрачный указатель ядра
} OesHostContext;

// ─── Базовый интерфейс плагина (таблица функций) ─────────────────────────────
typedef struct OesPluginVTable {
    // Инициализация с хост-контекстом. Возвращает OES_OK при успехе.
    OesResult (*initialize)(void* plugin, const OesHostContext* host);

    // Освобождение ресурсов. Всегда вызывается перед выгрузкой DLL.
    void      (*shutdown)(void* plugin);

    // Получить метаданные плагина.
    const OesPluginMeta* (*getMeta)(void* plugin);
} OesPluginVTable;

typedef struct OesPlugin {
    const OesPluginVTable* vtable;
    void*                  impl;   // Непрозрачный указатель на реализацию
} OesPlugin;

// ─── Обязательные экспорты DLL ────────────────────────────────────────────────

// Создать экземпляр плагина. Вызывается ядром после загрузки DLL.
// DLL владеет объектом до вызова OesDestroyPlugin.
#ifdef _WIN32
#  define OES_EXPORT __declspec(dllexport)
#else
#  define OES_EXPORT __attribute__((visibility("default")))
#endif

typedef OesPlugin* (*OesCreatePluginFn)(void);
typedef void       (*OesDestroyPluginFn)(OesPlugin*);

// Имена функций для GetProcAddress / dlsym
#define OES_FN_CREATE_PLUGIN  "OesCreatePlugin"
#define OES_FN_DESTROY_PLUGIN "OesDestroyPlugin"

#ifdef __cplusplus
}  // extern "C"
#endif
```

---

## PluginManager (ядро OES)

```cpp
// src/plugin_manager.h
#pragma once
#include "oes_plugin_api.h"
#include <wx/dynlib.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

struct LoadedPlugin {
    std::unique_ptr<wxDynamicLibrary> library;
    OesPlugin*        plugin    = nullptr;
    OesDestroyPluginFn destroyFn = nullptr;
    std::string       path;
    std::string       id;   // дублируется для удобства обратного обхода
};

class PluginManager {
public:
    explicit PluginManager(OesHostContext hostContext);
    ~PluginManager();

    // Загрузить один плагин из файла (DLL/SO)
    bool LoadPlugin(const std::string& path, std::string& errorOut);

    // Загрузить все плагины из директории
    int  LoadDirectory(const std::string& dir);

    // Выгрузить конкретный плагин по ID
    void UnloadPlugin(const std::string& pluginId);

    // Получить список загруженных плагинов
    std::vector<const OesPluginMeta*> GetAllMeta() const;

    // Получить плагин по ID и типу
    OesPlugin* FindPlugin(const std::string& id,
                          OesPluginType type) const;

private:
    OesHostContext m_hostContext;
    std::unordered_map<std::string, LoadedPlugin> m_plugins;
    // Порядок загрузки — для корректной выгрузки в обратном порядке
    std::vector<std::string> m_loadOrder;

    bool CheckAbiCompatibility(const OesPluginMeta* meta,
                                std::string& errorOut);
};
```

```cpp
// src/plugin_manager.cpp
#include "plugin_manager.h"
#include <wx/filename.h>
#include <wx/dir.h>

PluginManager::PluginManager(OesHostContext hostContext)
    : m_hostContext(hostContext)
{}

PluginManager::~PluginManager() {
    // Выгружаем СТРОГО в обратном порядке загрузки:
    // плагины могут зависеть от других плагинов, загруженных ранее,
    // поэтому итерация по unordered_map не подходит — порядок не определён.
    for (auto it = m_loadOrder.rbegin(); it != m_loadOrder.rend(); ++it) {
        auto mapIt = m_plugins.find(*it);
        if (mapIt == m_plugins.end()) continue;
        LoadedPlugin& lp = mapIt->second;
        if (lp.plugin && lp.destroyFn) {
            if (lp.plugin->vtable)
                lp.plugin->vtable->shutdown(lp.plugin->impl);
            lp.destroyFn(lp.plugin);
        }
    }
    m_plugins.clear();
    m_loadOrder.clear();
    // wxDynamicLibrary освобождается деструктором unique_ptr
}

bool PluginManager::LoadPlugin(const std::string& path,
                                std::string& errorOut)
{
    auto lib = std::make_unique<wxDynamicLibrary>();
    if (!lib->Load(path, wxDL_NOW | wxDL_QUIET)) {
        errorOut = "Не удалось загрузить библиотеку: " + path;
        return false;
    }

    // Получить фабричные функции
    auto createFn = reinterpret_cast<OesCreatePluginFn>(
        lib->GetSymbol(OES_FN_CREATE_PLUGIN));
    auto destroyFn = reinterpret_cast<OesDestroyPluginFn>(
        lib->GetSymbol(OES_FN_DESTROY_PLUGIN));

    if (!createFn || !destroyFn) {
        errorOut = "Плагин не экспортирует " OES_FN_CREATE_PLUGIN
                   " / " OES_FN_DESTROY_PLUGIN;
        return false;
    }

    OesPlugin* plugin = createFn();
    if (!plugin || !plugin->vtable) {
        errorOut = "OesCreatePlugin вернул nullptr";
        return false;
    }

    // Проверить совместимость ABI
    const OesPluginMeta* meta = plugin->vtable->getMeta(plugin->impl);
    if (!CheckAbiCompatibility(meta, errorOut)) {
        destroyFn(plugin);
        return false;
    }

    // Инициализировать плагин
    OesResult result = plugin->vtable->initialize(
        plugin->impl, &m_hostContext);
    if (result != OES_OK) {
        errorOut = std::string("Инициализация плагина ")
                   + meta->id + " завершилась с ошибкой: "
                   + std::to_string(result);
        destroyFn(plugin);
        return false;
    }

    LoadedPlugin lp;
    lp.library   = std::move(lib);
    lp.plugin    = plugin;
    lp.destroyFn = destroyFn;
    lp.path      = path;
    lp.id        = meta->id;

    m_loadOrder.push_back(meta->id);        // запомнить порядок загрузки
    m_plugins[meta->id] = std::move(lp);
    return true;
}

bool PluginManager::CheckAbiCompatibility(
    const OesPluginMeta* meta, std::string& errorOut)
{
    if (!meta) {
        errorOut = "getMeta() вернул nullptr";
        return false;
    }

    if (meta->abiMajor != OES_PLUGIN_ABI_MAJOR) {
        errorOut = std::string("Несовместимая версия ABI плагина ")
                   + meta->id + ": ожидается major="
                   + std::to_string(OES_PLUGIN_ABI_MAJOR)
                   + ", получено major="
                   + std::to_string(meta->abiMajor);
        return false;
    }

    return true;
}

int PluginManager::LoadDirectory(const std::string& dir) {
    int loaded = 0;
    wxDir wxdir(dir);
    if (!wxdir.IsOpened())
        return 0;

#if defined(_WIN32)
    const wxString mask = "*.dll";
#elif defined(__APPLE__)
    const wxString mask = "*.dylib";
#else
    const wxString mask = "*.so";
#endif

    wxString filename;
    bool found = wxdir.GetFirst(&filename, mask, wxDIR_FILES);
    while (found) {
        std::string fullPath = dir + "/" + filename.ToStdString();
        std::string err;
        if (LoadPlugin(fullPath, err))
            ++loaded;
        else
            wxLogWarning("Плагин '%s' не загружен: %s",
                         fullPath, err);
        found = wxdir.GetNext(&filename);
    }
    return loaded;
}
```

---

## Реализация плагина (сторонний код)

```cpp
// my_plugin/my_data_connector.cpp
// Пример плагина — коннектор к внешнему REST API

#include "oes_plugin_api.h"
#include <string>
#include <cstring>
#include <cstdlib>

// ─── Данные плагина ────────────────────────────────────────────────────────────
static const OesPluginMeta g_meta = {
    OES_PLUGIN_ABI_MAJOR,     // abiMajor — ДОЛЖНО совпадать с ядром
    OES_PLUGIN_ABI_MINOR,     // abiMinor
    OES_PLUGIN_DATA_SOURCE,   // type
    "com.example.rest-connector",
    "REST API Connector",
    "1.0.0",
    "Example Corp",
    "LGPL-2.1",
};

struct MyConnectorImpl {
    const OesHostContext* host = nullptr;
    std::string           baseUrl;
};

// ─── VTable функции ────────────────────────────────────────────────────────────
static OesResult Initialize(void* impl, const OesHostContext* host) {
    auto* self = static_cast<MyConnectorImpl*>(impl);
    self->host = host;

    const char* url = host->getSetting(host->hostData, "rest_base_url");
    if (!url || url[0] == '\0') {
        host->logMessage(host->hostData, OES_LOG_WARNING,
            "rest_base_url не задан — используем значение по умолчанию");
        self->baseUrl = "https://api.example.com";
    } else {
        self->baseUrl = url;
    }

    host->logMessage(host->hostData, OES_LOG_INFO,
        "REST Connector инициализирован");
    return OES_OK;
}

static void Shutdown(void* impl) {
    // Закрыть соединения, освободить ресурсы
    auto* self = static_cast<MyConnectorImpl*>(impl);
    // ...
    (void)self;
}

static const OesPluginMeta* GetMeta(void* /*impl*/) {
    return &g_meta;
}

static const OesPluginVTable g_vtable = {
    &Initialize,
    &Shutdown,
    &GetMeta,
};

// ─── Обязательные экспорты ────────────────────────────────────────────────────
extern "C" {

OES_EXPORT OesPlugin* OesCreatePlugin() {
    auto* impl   = new MyConnectorImpl();
    auto* plugin = new OesPlugin();
    plugin->vtable = &g_vtable;
    plugin->impl   = impl;
    return plugin;
}

OES_EXPORT void OesDestroyPlugin(OesPlugin* plugin) {
    if (!plugin) return;
    delete static_cast<MyConnectorImpl*>(plugin->impl);
    delete plugin;
}

}  // extern "C"
```

---

## Версионирование и совместимость

### Правила изменения версий

| Тип изменения | Действие | Совместимость |
|---------------|---------|---------------|
| Добавить новое поле в VTable | Инкремент MINOR | Старые плагины работают |
| Изменить сигнатуру функции в VTable | Инкремент MAJOR | Все старые плагины сломаны |
| Добавить новый тип плагина (OesPluginType) | Инкремент MINOR | Старые плагины работают |
| Изменить формат OesPlugin структуры | Инкремент MAJOR | Все старые плагины сломаны |
| Изменить реализацию плагина (не API) | Инкремент версии плагина | Ядро не затронуто |

### Backward-compatible расширение VTable

```cpp
// oes_plugin_api_v2_1.h — MINOR расширение
// Новые поля добавляются ТОЛЬКО в конец структуры
typedef struct OesPluginVTable {
    // v2.0
    OesResult (*initialize)(void*, const OesHostContext*);
    void      (*shutdown)(void*);
    const OesPluginMeta* (*getMeta)(void*);

    // v2.1 — новые поля в конце, указатели могут быть NULL для старых плагинов
    OesResult (*configure)(void*, const char* key, const char* value);
    const char* (*getCapabilities)(void*);  // JSON-строка возможностей
} OesPluginVTable;

// В ядре: безопасный вызов нового метода
inline OesResult SafeConfigure(OesPlugin* p,
                                const char* key, const char* value)
{
    if (p->vtable->configure)  // проверяем перед вызовом
        return p->vtable->configure(p->impl, key, value);
    return OES_ERROR_NOT_FOUND;
}
```

### Матрица совместимости

```
Ядро OES  │  Плагин v2.0  │  Плагин v2.1  │  Плагин v1.x  │  Плагин v3.x
──────────┼───────────────┼───────────────┼───────────────┼──────────────
  v2.0    │       ✅       │       ✅*      │       ❌       │       ❌
  v2.1    │       ✅**     │       ✅       │       ❌       │       ❌
  v3.0    │       ❌       │       ❌       │       ❌       │       ✅

* Плагин v2.1 заполняет новые поля VTable (configure, getCapabilities).
  Ядро v2.0 не знает о них — просто не вызывает, поля за концом структуры
  для ядра невидимы. Плагин работает штатно.

** Ядро v2.1 вызывает новые методы только после проверки указателя на nullptr
  (см. SafeConfigure выше). Плагин v2.0 имеет nullptr в новых полях VTable
  (C-инициализация нулём), поэтому ядро корректно пропускает вызов.
  ОТВЕТСТВЕННОСТЬ за проверку nullptr лежит на ЯДРЕ, а не на плагине.
```

---

## Директория плагинов

### Структура файловой системы

```
%PROGRAMFILES%\OES\
├── oes.exe
├── oes_core.dll
├── plugins\                       ← системные плагины
│   ├── oes_firebird_connector.dll
│   ├── oes_postgres_connector.dll
│   ├── oes_sqlite_connector.dll
│   └── oes_pdf_exporter.dll
└── ...

%APPDATA%\OES\plugins\            ← пользовательские плагины
    ├── my_custom_widget.dll
    └── rest_api_connector.dll
```

```cpp
// Загрузка плагинов при старте приложения
void OesApp::LoadPlugins() {
    OesHostContext host = CreateHostContext();
    m_pluginManager = std::make_unique<PluginManager>(host);

    // 1. Системные плагины
    wxString sysPluginsDir = wxStandardPaths::Get().GetPluginsDir()
                             + "/plugins";
    int sysLoaded = m_pluginManager->LoadDirectory(
        sysPluginsDir.ToStdString());

    // 2. Пользовательские плагины (AppData)
    wxString userPluginsDir = wxStandardPaths::Get().GetUserDataDir()
                              + "/plugins";
    int userLoaded = m_pluginManager->LoadDirectory(
        userPluginsDir.ToStdString());

    wxLogInfo("Плагины загружены: системных=%d, пользовательских=%d",
              sysLoaded, userLoaded);
}
```

---

## Безопасность плагинов

### Принципы

1. **Плагины выполняются в том же процессе** — нет изоляции памяти. Сбой в плагине = краш всего приложения.
2. **Доверяйте только подписанным плагинам** (для enterprise-развёртываний).
3. **Ограничивайте API:** плагин получает только то, что ему нужно, через `OesHostContext`.
4. **Никаких прямых указателей на внутренние структуры ядра** — только через OesHostContext callbacks.

### Подпись плагинов (Enterprise)

```cpp
// Перед загрузкой плагина — проверить цифровую подпись
#ifdef OES_ENTERPRISE_PLUGIN_SIGNING
bool PluginManager::IsPluginTrusted(const std::string& path) {
#ifdef _WIN32
    return VerifyCodeSignature(
        std::wstring(path.begin(), path.end()));
#else
    // На Linux/macOS — проверка GPG подписи для .so файла
    return VerifyGpgSignature(path + ".sig", path,
                               OES_TRUSTED_PUBLIC_KEY);
#endif
}
#endif
```

### Обработка сбоев плагина

```cpp
// SEH-защита при вызове плагина (Windows)
// Изолирует краш плагина от ядра
OesResult SafeCallPlugin(OesPlugin* plugin,
                          std::function<OesResult()> call)
{
#ifdef _WIN32
    __try {
        return call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        DWORD code = GetExceptionCode();
        wxLogError("Плагин '%s' вызвал исключение 0x%08X",
                   plugin->vtable->getMeta(plugin->impl)->id, code);
        return OES_ERROR;
    }
#else
    // На Linux — signal handler или setjmp/longjmp (ограниченно безопасно)
    return call();
#endif
}
```

---

## Чеклист разработчика плагина

### Перед публикацией плагина:

- [ ] `abiMajor` совпадает с целевой версией ядра OES
- [ ] Все `extern "C"` экспорты присутствуют: `OesCreatePlugin`, `OesDestroyPlugin`
- [ ] Нет исключений C++, выходящих за границу DLL (всё ловится внутри)
- [ ] `Shutdown()` корректно освобождает все ресурсы (RAII)
- [ ] Не используются `std::string`, `std::vector` в публичном ABI — только `const char*`, примитивы
- [ ] Плагин не хранит указатели на память ядра после `Shutdown()`
- [ ] Тестирование на всех поддерживаемых платформах (Win/Linux/macOS)
- [ ] Логи через `OesHostContext::logMessage`, не через `printf` / `std::cout`
- [ ] Подпись DLL/SO для enterprise-дистрибуции
- [ ] `licenseType` в метаданных соответствует реальной лицензии
- [ ] Документация: описание, настройки (ключи `getSetting`), зависимости

### Чеклист ядра при добавлении нового API:

- [ ] Новые поля VTable добавляются ТОЛЬКО в конец структуры
- [ ] Инкремент MINOR версии ABI при добавлении, MAJOR при ломающих изменениях
- [ ] Ядро проверяет `abiMajor` при загрузке и отказывает несовместимым плагинам
- [ ] Новые методы VTable вызываются только после проверки на `nullptr`
- [ ] CHANGELOG для публичного Plugin API обновлён
