# 15. Проектирование внутренних API и интерфейсов модулей

## Принципы

В OES «API» — это C++ интерфейсы между модулями: абстрактные классы, виртуальные функции, DLL-границы. Правильно спроектированный интерфейс позволяет:
- Подменять реализации (mock в тестах, другой драйвер БД)
- Поддерживать стабильный ABI при обновлении DLL
- Изолировать изменения внутри модуля без влияния на вызывающий код

---

## Абстрактные интерфейсы

### Правило: отделять интерфейс от реализации

Каждый значимый модуль обязан иметь абстрактный интерфейс (чистый виртуальный класс). Вызывающий код зависит только от интерфейса, не от конкретного класса.

```cpp
// ies_report_engine.h — публичный интерфейс модуля отчётов
#pragma once
#include <wx/string.h>
#include <memory>

// Параметры генерации
struct ReportParams
{
    wxString reportId;
    wxString outputPath;
    wxString format;       // "pdf", "xlsx", "html"
    bool     preview = false;
};

// Результат операции
struct ReportResult
{
    bool      success  = false;
    wxString  errorMsg;
    wxString  outputPath;
    long      durationMs = 0;
};

// Интерфейс — только чистые виртуальные функции
class IReportEngine
{
public:
    virtual ~IReportEngine() = default;

    virtual ReportResult Generate(const ReportParams& params) = 0;
    virtual bool         IsFormatSupported(const wxString& fmt) const = 0;
    virtual wxArrayString GetAvailableReports() const = 0;
};

// Фабричная функция — единственная точка создания
std::unique_ptr<IReportEngine> CreateReportEngine();
```

```cpp
// report_engine_impl.cpp — реализация скрыта
class ReportEngineImpl : public IReportEngine
{
public:
    ReportResult Generate(const ReportParams& params) override;
    bool IsFormatSupported(const wxString& fmt) const override;
    wxArrayString GetAvailableReports() const override;

private:
    // Детали реализации — не видны снаружи
    void LoadTemplate(const wxString& reportId);
    void FillData(const ReportParams& params);
};

std::unique_ptr<IReportEngine> CreateReportEngine()
{
    return std::make_unique<ReportEngineImpl>();
}
```

### Именование интерфейсов

| Тип | Префикс | Пример |
|-----|---------|--------|
| Абстрактный интерфейс | `I` | `IReportEngine`, `IDatabaseLayer` |
| Базовый класс с поведением | `OesBase` | `OesBaseDocument` |
| Конкретная реализация | `Impl` суффикс | `ReportEngineImpl` |
| Mock для тестов | `Mock` префикс | `MockDatabaseLayer` |

---

## Проектирование методов интерфейса

### Возвращаемые значения: явный результат

Не использовать исключения через DLL-границы. Использовать структуры результата или коды ошибок.

```cpp
// Правильно — явный результат
struct OperationResult
{
    bool     ok = false;
    wxString error;

    static OperationResult Success()         { return {true, ""}; }
    static OperationResult Fail(const wxString& e) { return {false, e}; }
};

class IDocumentStorage
{
public:
    virtual ~IDocumentStorage() = default;

    // Возвращает результат, не бросает исключений через границу модуля
    virtual OperationResult Save(const DocumentData& doc) = 0;
    virtual OperationResult Delete(int docId) = 0;

    // Загрузка: Optional-паттерн через out-параметр
    virtual bool Load(int docId, DocumentData& outDoc,
                      wxString& outError) = 0;

    // Список: возвращает через out-параметр для совместимости с MSVC ABI
    virtual bool GetList(const DocumentFilter& filter,   // const ref — не копируем
                         std::vector<DocumentInfo>& outList,
                         wxString& outError) = 0;
};

// Неправильно — бросать исключения через DLL-границу
virtual DocumentData Load(int docId) = 0;  // бросает при ошибке — опасно
```

### Параметры: const ref для входных, ref для выходных

```cpp
// Правильно
virtual OperationResult CreateDocument(
    const DocumentCreateParams& params,    // входной — const ref
    int& outNewId                          // выходной — ref
) = 0;

// Неправильно — передача по значению сложных объектов
virtual OperationResult CreateDocument(
    DocumentCreateParams params,   // копирование — лишние расходы
    int* outNewId                  // сырой указатель — неясно владение
) = 0;
```

### Не возвращать сырые указатели на объекты с управляемым временем жизни

```cpp
// Правильно
virtual std::shared_ptr<IDocument> OpenDocument(int docId) = 0;

// Допустимо (не-владеющий указатель, время жизни управляется вызывающим)
virtual IDocument* GetActiveDocument() = 0;   // ясно: возвращает наблюдатель

// Неправильно — возвращать сырой указатель на новый объект
virtual IDocument* CreateDocument() = 0;      // кто освобождает?
```

---

## ABI-стабильность и DLL-границы

### Правила для публичных заголовков DLL

Эти правила актуальны для OES-модулей, поставляемых как `.dll`:

1. **Не экспортировать шаблонные классы** — каждая единица компиляции инстанцирует их по-своему
2. **Не использовать `std::string` / `std::vector` в публичных методах** — используйте `wxString` и передачу через out-параметры или собственные POD-структуры
3. **Не изменять порядок виртуальных функций** в интерфейсе после релиза
4. **Добавлять новые методы только в конец** виртуальной таблицы
5. **Версионировать интерфейсы** при ломающих изменениях

```cpp
// Безопасно добавлять в конец — ABI не ломается
class IDocumentStorage
{
public:
    virtual ~IDocumentStorage() = default;
    virtual OperationResult Save(const DocumentData& doc) = 0;   // v1.0
    virtual bool Load(int id, DocumentData& out,
                      wxString& err) = 0;                         // v1.0

    // Добавлено в v1.1 — в конце, старый код работает с vtable v1.0
    // ВАЖНО: std::vector нарушает правило ABI-стабильности (разные аллокаторы).
    // Используйте C array + count для DLL-границы:
    virtual OperationResult SaveBatch(
        const DocumentData* docs,   // C-массив — ABI-стабилен
        int                 count   // количество элементов
    ) = 0;                                                        // v1.1
};

// Версионирование при ломающих изменениях
class IDocumentStorage2 : public IDocumentStorage
{
public:
    // Новый метод с другой сигнатурой
    virtual OperationResult SaveV2(const DocumentDataV2& doc) = 0;
};
```

### Экспорт из DLL

```cpp
// oes_module_api.h
#ifdef OES_MODULE_EXPORTS
    #define OES_MODULE_API __declspec(dllexport)
#else
    #define OES_MODULE_API __declspec(dllimport)
#endif

// Единственная экспортируемая C-функция (не ломает ABI)
extern "C" OES_MODULE_API IReportEngine* CreateReportEngineInstance();
extern "C" OES_MODULE_API void           DestroyReportEngineInstance(IReportEngine*);

// В клиентском коде — использование через smart pointer
// Примечание: лямбда должна быть non-capturing (без захвата переменных),
// иначе decltype(deleter) не является указателем на функцию и тип
// unique_ptr меняется от экземпляра к экземпляру.
auto deleter = [](IReportEngine* p){ DestroyReportEngineInstance(p); };
std::unique_ptr<IReportEngine, decltype(deleter)>
    engine(CreateReportEngineInstance(), deleter);
```

---

## Паттерны интерфейсов в OES

### Слой доступа к данным (репозиторий)

```cpp
// Запрос с фильтрацией и пагинацией
struct DocumentQuery
{
    wxString statusFilter;     // "" = все статусы
    wxString searchText;       // "" = без поиска
    int      pageNumber  = 1;
    int      pageSize    = 50;
    wxString sortField   = "created_at";
    bool     sortAsc     = false;
};

struct DocumentPage
{
    std::vector<DocumentInfo> items;
    int total      = 0;
    int pageNumber = 1;
    int pageSize   = 50;
};

class IDocumentRepository
{
public:
    virtual ~IDocumentRepository() = default;

    virtual bool Query(const DocumentQuery& q, DocumentPage& out,
                       wxString& err) = 0;
    virtual bool GetById(int id, DocumentData& out,
                         wxString& err) = 0;
    virtual OperationResult Create(const DocumentData& data, int& outId) = 0;
    virtual OperationResult Update(const DocumentData& data) = 0;
    virtual OperationResult Remove(int id) = 0;
};
```

### Подписки на события (Observer)

```cpp
// Интерфейс наблюдателя
class IDocumentObserver
{
public:
    virtual ~IDocumentObserver() = default;
    virtual void OnDocumentSaved(int docId)   {}
    virtual void OnDocumentDeleted(int docId) {}
    virtual void OnDocumentOpened(int docId)  {}
};

// В интерфейсе источника событий
class IDocumentManager
{
public:
    virtual ~IDocumentManager() = default;

    virtual void AddObserver(IDocumentObserver* obs) = 0;
    virtual void RemoveObserver(IDocumentObserver* obs) = 0;

    virtual OperationResult SaveDocument(const DocumentData& data) = 0;
};
```

### Расширяемые плагины

```cpp
// Интерфейс плагина-экспортёра
class IExportPlugin
{
public:
    virtual ~IExportPlugin() = default;

    virtual wxString  GetId()          const = 0;   // "pdf", "xlsx"
    virtual wxString  GetDisplayName() const = 0;
    virtual wxString  GetExtension()   const = 0;
    virtual bool      CanExport(const DocumentData& doc) const = 0;
    virtual OperationResult Export(const DocumentData& doc,
                                   const wxString& path) = 0;
};

// Реестр плагинов
class IExportPluginRegistry
{
public:
    virtual ~IExportPluginRegistry() = default;

    virtual void Register(std::unique_ptr<IExportPlugin> plugin) = 0;
    virtual IExportPlugin* Find(const wxString& id) const = 0;
    virtual std::vector<IExportPlugin*> GetAll() const = 0;
};
```

---

## Документирование интерфейсов

### Обязательные комментарии для публичного API

```cpp
/**
 * @brief Репозиторий документов.
 *
 * Обеспечивает CRUD-операции над документами в базе данных.
 * Реализация определяет конкретный тип БД (Firebird, PostgreSQL и т.д.).
 *
 * Потокобезопасность: не потокобезопасен. Использовать один экземпляр
 * на поток или обеспечить внешнюю синхронизацию.
 */
class IDocumentRepository
{
public:
    /**
     * @brief Загружает документ по идентификатору.
     * @param[in]  id  Идентификатор документа.
     * @param[out] out Загруженный документ (заполняется при success=true).
     * @param[out] err Сообщение об ошибке (заполняется при failure).
     * @return true если документ найден и загружен, false иначе.
     */
    virtual bool GetById(int id, DocumentData& out, wxString& err) = 0;

    /**
     * @brief Создаёт новый документ.
     * @param[in]  data  Данные документа. data.id игнорируется.
     * @param[out] outId Назначенный идентификатор нового документа.
     * @return OperationResult::Success() или OperationResult::Fail(reason).
     */
    virtual OperationResult Create(const DocumentData& data, int& outId) = 0;
};
```

---

## Чего НЕ делать

| Запрещено | Почему | Альтернатива |
|-----------|--------|-------------|
| Возвращать `std::string` через DLL-границу | Разные рантаймы, разные аллокаторы | `wxString` или out-параметр |
| Бросать исключения через DLL-границу | Неопределённое поведение | Коды ошибок / OperationResult |
| Изменять порядок virtual методов в релизном интерфейсе | Ломает ABI | Добавлять только в конец |
| Экспортировать `std::vector<T>` как поле структуры | ABI зависит от компилятора | Передавать через параметры |
| Зависеть от конкретного класса вместо интерфейса | Невозможно подменить | Зависеть от `I*` интерфейса |
| Публичные поля в интерфейсных структурах без версионирования | Ломает бинарную совместимость | POD-структуры с явным версионированием |

---

## Чеклист проектирования интерфейса

- [ ] Интерфейс — чистый виртуальный класс (`= 0` методы)
- [ ] Деструктор виртуальный: `virtual ~IFoo() = default`
- [ ] Нет `std::string` / `std::vector` в публичных методах DLL
- [ ] Нет исключений через DLL-границы
- [ ] Новые методы добавлены в конец vtable
- [ ] Фабричная функция экспортируется как `extern "C"`
- [ ] Все параметры задокументированы (`@param[in]`, `@param[out]`)
- [ ] Mock-реализация существует для юнит-тестов
- [ ] Интерфейс версионирован при ломающих изменениях
