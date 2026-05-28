# 15. API Design and Module Interfaces

## Principles

In OES "API" means C++ interfaces between modules: abstract classes, virtual functions, DLL boundaries. A well-designed interface lets you:
- Substitute implementations (mock in tests, a different DB driver)
- Keep a stable ABI when updating a DLL
- Isolate changes inside a module without affecting callers

---

## Abstract interfaces

### Rule: separate interface from implementation

Every significant module must have an abstract interface (a pure virtual class). Callers depend only on the interface, not on a concrete class.

```cpp
// ies_report_engine.h — public interface of the reports module
#pragma once
#include <wx/string.h>
#include <memory>

// Generation parameters
struct ReportParams
{
    wxString reportId;
    wxString outputPath;
    wxString format;       // "pdf", "xlsx", "html"
    bool     preview = false;
};

// Operation result
struct ReportResult
{
    bool      success  = false;
    wxString  errorMsg;
    wxString  outputPath;
    long      durationMs = 0;
};

// Interface — pure virtual functions only
class IReportEngine
{
public:
    virtual ~IReportEngine() = default;

    virtual ReportResult Generate(const ReportParams& params) = 0;
    virtual bool         IsFormatSupported(const wxString& fmt) const = 0;
    virtual wxArrayString GetAvailableReports() const = 0;
};

// Factory function — single point of creation
std::unique_ptr<IReportEngine> CreateReportEngine();
```

```cpp
// report_engine_impl.cpp — implementation is hidden
class ReportEngineImpl : public IReportEngine
{
public:
    ReportResult Generate(const ReportParams& params) override;
    bool IsFormatSupported(const wxString& fmt) const override;
    wxArrayString GetAvailableReports() const override;

private:
    // Implementation details — invisible from outside
    void LoadTemplate(const wxString& reportId);
    void FillData(const ReportParams& params);
};

std::unique_ptr<IReportEngine> CreateReportEngine()
{
    return std::make_unique<ReportEngineImpl>();
}
```

### Interface naming

| Type | Prefix | Example |
|-----|---------|--------|
| Abstract interface | `I` | `IReportEngine`, `IDatabaseLayer` |
| Base class with behaviour | `OesBase` | `OesBaseDocument` |
| Concrete implementation | `Impl` suffix | `ReportEngineImpl` |
| Test mock | `Mock` prefix | `MockDatabaseLayer` |

---

## Designing interface methods

### Return values: explicit result

Don't propagate exceptions across DLL boundaries. Use result structs or error codes.

```cpp
// Right — explicit result
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

    // Returns a result, does not throw across the module boundary
    virtual OperationResult Save(const DocumentData& doc) = 0;
    virtual OperationResult Delete(int docId) = 0;

    // Load: Optional pattern via out parameter
    virtual bool Load(int docId, DocumentData& outDoc,
                      wxString& outError) = 0;

    // List: out parameter for MSVC ABI compatibility
    virtual bool GetList(const DocumentFilter& filter,   // const ref — no copy
                         std::vector<DocumentInfo>& outList,
                         wxString& outError) = 0;
};

// Wrong — throw exceptions across the DLL boundary
virtual DocumentData Load(int docId) = 0;  // throws on error — dangerous
```

### Parameters: const ref for inputs, ref for outputs

```cpp
// Right
virtual OperationResult CreateDocument(
    const DocumentCreateParams& params,    // input — const ref
    int& outNewId                          // output — ref
) = 0;

// Wrong — passing complex objects by value
virtual OperationResult CreateDocument(
    DocumentCreateParams params,   // copy — wasted cycles
    int* outNewId                  // raw pointer — ownership unclear
) = 0;
```

### Don't return raw pointers to objects with managed lifetime

```cpp
// Right
virtual std::shared_ptr<IDocument> OpenDocument(int docId) = 0;

// Acceptable (non-owning pointer, lifetime managed by the caller)
virtual IDocument* GetActiveDocument() = 0;   // clearly: returns an observer

// Wrong — returning a raw pointer to a new object
virtual IDocument* CreateDocument() = 0;      // who frees it?
```

---

## ABI stability and DLL boundaries

### Rules for public DLL headers

These rules apply to OES modules shipped as `.dll`:

1. **Do not export template classes** — every translation unit instantiates them differently
2. **Do not use `std::string` / `std::vector` in public methods** — use `wxString` and out parameters or your own POD structs
3. **Do not change the order of virtual functions** in an interface after release
4. **Add new methods only at the end** of the vtable
5. **Version interfaces** on breaking changes

```cpp
// Safe to append at the end — ABI stays intact
class IDocumentStorage
{
public:
    virtual ~IDocumentStorage() = default;
    virtual OperationResult Save(const DocumentData& doc) = 0;   // v1.0
    virtual bool Load(int id, DocumentData& out,
                      wxString& err) = 0;                         // v1.0

    // Added in v1.1 — at the end, old code works with v1.0 vtable
    // IMPORTANT: std::vector violates the ABI stability rule (different allocators).
    // Use a C array + count at the DLL boundary:
    virtual OperationResult SaveBatch(
        const DocumentData* docs,   // C array — ABI-stable
        int                 count   // element count
    ) = 0;                                                        // v1.1
};

// Version on breaking changes
class IDocumentStorage2 : public IDocumentStorage
{
public:
    // New method with a different signature
    virtual OperationResult SaveV2(const DocumentDataV2& doc) = 0;
};
```

### DLL export

```cpp
// oes_module_api.h
#ifdef OES_MODULE_EXPORTS
    #define OES_MODULE_API __declspec(dllexport)
#else
    #define OES_MODULE_API __declspec(dllimport)
#endif

// The only exported C function (does not break ABI)
extern "C" OES_MODULE_API IReportEngine* CreateReportEngineInstance();
extern "C" OES_MODULE_API void           DestroyReportEngineInstance(IReportEngine*);

// In client code — use through a smart pointer
// Note: the lambda must be non-capturing (no captures),
// otherwise decltype(deleter) is not a function pointer and the
// unique_ptr type varies from instance to instance.
auto deleter = [](IReportEngine* p){ DestroyReportEngineInstance(p); };
std::unique_ptr<IReportEngine, decltype(deleter)>
    engine(CreateReportEngineInstance(), deleter);
```

---

## Interface patterns in OES

### Data access layer (repository)

```cpp
// Filtered, paged query
struct DocumentQuery
{
    wxString statusFilter;     // "" = all statuses
    wxString searchText;       // "" = no search
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

### Event subscriptions (Observer)

```cpp
// Observer interface
class IDocumentObserver
{
public:
    virtual ~IDocumentObserver() = default;
    virtual void OnDocumentSaved(int docId)   {}
    virtual void OnDocumentDeleted(int docId) {}
    virtual void OnDocumentOpened(int docId)  {}
};

// On the event source interface
class IDocumentManager
{
public:
    virtual ~IDocumentManager() = default;

    virtual void AddObserver(IDocumentObserver* obs) = 0;
    virtual void RemoveObserver(IDocumentObserver* obs) = 0;

    virtual OperationResult SaveDocument(const DocumentData& data) = 0;
};
```

### Extensible plugins

```cpp
// Export plugin interface
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

// Plugin registry
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

## Documenting interfaces

### Required comments for public APIs

```cpp
/**
 * @brief Document repository.
 *
 * Provides CRUD operations on documents in the database.
 * The implementation determines the concrete DB type (Firebird, PostgreSQL, etc.).
 *
 * Thread safety: not thread-safe. Use one instance per thread,
 * or arrange external synchronization.
 */
class IDocumentRepository
{
public:
    /**
     * @brief Loads a document by identifier.
     * @param[in]  id  Document identifier.
     * @param[out] out Loaded document (populated when success=true).
     * @param[out] err Error message (populated on failure).
     * @return true if the document was found and loaded, false otherwise.
     */
    virtual bool GetById(int id, DocumentData& out, wxString& err) = 0;

    /**
     * @brief Creates a new document.
     * @param[in]  data  Document data. data.id is ignored.
     * @param[out] outId Identifier assigned to the new document.
     * @return OperationResult::Success() or OperationResult::Fail(reason).
     */
    virtual OperationResult Create(const DocumentData& data, int& outId) = 0;
};
```

---

## What NOT to do

| Forbidden | Why | Alternative |
|-----------|--------|-------------|
| Return `std::string` across a DLL boundary | Different runtimes, different allocators | `wxString` or an out parameter |
| Throw exceptions across a DLL boundary | Undefined behaviour | Error codes / OperationResult |
| Change the order of virtual methods in a released interface | Breaks ABI | Append at the end only |
| Export `std::vector<T>` as a struct field | ABI depends on the compiler | Pass through parameters |
| Depend on a concrete class instead of an interface | Cannot substitute | Depend on the `I*` interface |
| Public fields in interface structs without versioning | Breaks binary compatibility | POD structs with explicit versioning |

---

## Interface design checklist

- [ ] Interface is a pure virtual class (`= 0` methods)
- [ ] Virtual destructor: `virtual ~IFoo() = default`
- [ ] No `std::string` / `std::vector` in public DLL methods
- [ ] No exceptions across DLL boundaries
- [ ] New methods appended at the end of the vtable
- [ ] Factory function exported as `extern "C"`
- [ ] All parameters documented (`@param[in]`, `@param[out]`)
- [ ] A mock implementation exists for unit tests
- [ ] Interface versioned on breaking changes
