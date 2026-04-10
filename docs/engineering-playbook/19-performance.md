# 19. Производительность

## Целевые показатели

| Операция | Цель | Критично |
|----------|------|---------|
| Запуск приложения | < 3 сек | > 10 сек |
| Открытие документа | < 1 сек | > 5 сек |
| Генерация отчёта (100 строк) | < 2 сек | > 10 сек |
| Сохранение документа | < 500 мс | > 3 сек |
| SQL-запрос (один объект) | < 50 мс | > 500 мс |
| SQL-запрос (список, 1000 строк) | < 500 мс | > 3 сек |
| Рендер страницы дизайнера | < 16 мс (60 fps) | > 100 мс |
| Переключение вкладок / диалогов | < 200 мс | > 1 сек |

---

## Профилирование

### Intel VTune Profiler (Windows)

Основной инструмент для анализа CPU hotspots:

```
1. Запустить OES в конфигурации Release с /DEBUG символами
2. VTune → New Analysis → Hotspots (CPU)
3. Запустить приложение, воспроизвести медленный сценарий
4. Остановить коллекцию
5. Анализ:
   - Bottom-Up → найти функции с наибольшим CPU Time
   - Call Stack → понять, откуда вызывается горячая функция
   - Source View → точное место в коде
```

### Very Sleepy (бесплатный, Windows)

```
1. Запустить OES
2. Very Sleepy → Attach to process → выбрать OES.exe
3. Start / Stop профилирование во время медленной операции
4. Анализ Call Tree — найти hotspot
```

### Visual Studio Diagnostic Tools

```
Debug → Performance Profiler → CPU Usage
— без остановки приложения, прямо в IDE
— полезно для быстрого выявления регрессий
```

### perf (Linux / кросс-платформенная сборка)

```bash
# Собрать с символами отладки
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Профилирование
perf record -g ./OES
perf report --stdio

# Flamegraph
# Требуется клонировать репозиторий brendangregg/FlameGraph:
#   git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
# Затем добавить в PATH:
#   export PATH="$HOME/FlameGraph:$PATH"
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

---

## Оптимизация работы с базой данных

### N+1 запросы — главная проблема производительности

```cpp
// НЕПРАВИЛЬНО — N+1: один запрос на список + N запросов на детали
std::vector<DocumentInfo> docs = m_repo->GetList();  // 1 запрос
for (auto& doc : docs)
{
    // Каждый вызов — отдельный SELECT
    doc.sections = m_sectionRepo->GetByDocId(doc.id);  // N запросов
    doc.owner    = m_userRepo->GetById(doc.ownerId);    // ещё N запросов
}

// ПРАВИЛЬНО — JOIN в одном запросе
ibPreparedStatement* stmt = db->PrepareStatement(R"(
    SELECT d.id, d.name, d.status,
           s.id AS sec_id, s.title AS sec_title,
           u.name AS owner_name
    FROM documents d
    LEFT JOIN document_sections s ON s.doc_id = d.id
    LEFT JOIN users u ON u.id = d.owner_id
    WHERE d.is_deleted = 0
    ORDER BY d.id, s.sort_order
)");
// Затем собрать объектный граф из flat result set
```

### Оптимизация подготовленных выражений

```cpp
// Переиспользуйте prepared statements — не пересоздавайте на каждый вызов
class OesDocumentRepository
{
public:
    explicit OesDocumentRepository(ibDatabaseLayer* db)
        : m_db(db)
    {
        // Подготовить один раз при создании репозитория
        m_stmtGetById = m_db->PrepareStatement(
            "SELECT id, name, status FROM documents WHERE id = ?");
        m_stmtInsert  = m_db->PrepareStatement(
            "INSERT INTO documents (name, status) VALUES (?, ?) RETURNING id");
    }

    ~OesDocumentRepository()
    {
        if (m_stmtGetById) m_stmtGetById->Close();
        if (m_stmtInsert)  m_stmtInsert->Close();
    }

    bool GetById(int id, DocumentData& out, wxString& err)
    {
        // Использовать уже подготовленное выражение
        m_stmtGetById->SetParamInt(1, id);
        OesResultSetGuard rs(m_stmtGetById->RunQueryWithResults());
        // ...
    }

private:
    ibDatabaseLayer*     m_db;
    ibPreparedStatement* m_stmtGetById = nullptr;
    ibPreparedStatement* m_stmtInsert  = nullptr;
};
```

### Пагинация — обязательно для списков

```cpp
// Всегда ограничивать выборку — никогда SELECT без LIMIT
struct QueryParams
{
    int     page      = 1;
    int     pageSize  = 50;   // максимум 500
    wxString sortField = "created_at";
    bool    sortAsc   = false;
    wxString filter;
};

// Firebird: ROWS M TO N
// ВНИМАНИЕ: синтаксис "ROWS ? TO ?" поддерживается не всеми версиями
// драйверов ibDatabase/IBPP как параметризованные placeholder-ы.
// Если драйвер не поддерживает — используйте безопасную альтернативу:
//   "SELECT ... FROM ... ORDER BY ... ROWS " + rowFrom + " TO " + rowTo
// или предпочтительный синтаксис FIRST/SKIP (обратный порядок аргументов):
//   "SELECT FIRST ? SKIP ? id, name, status FROM documents ..."
// где FIRST = pageSize, SKIP = (page-1)*pageSize.
// Проверяйте документацию используемой версии обёртки над Firebird API.

ibPreparedStatement* stmt = db->PrepareStatement(
    "SELECT id, name, status "
    "FROM documents "
    "WHERE is_deleted = 0 "
    "ORDER BY created_at DESC "
    "ROWS ? TO ?"
);
int rowFrom = (params.page - 1) * params.pageSize + 1;
int rowTo   = params.page * params.pageSize;
stmt->SetParamInt(1, rowFrom);
stmt->SetParamInt(2, rowTo);

// Более переносимая альтернатива для Firebird (FIRST ? SKIP ?):
// ibPreparedStatement* stmt = db->PrepareStatement(
//     "SELECT FIRST ? SKIP ? id, name, status "
//     "FROM documents WHERE is_deleted = 0 ORDER BY created_at DESC"
// );
// stmt->SetParamInt(1, params.pageSize);
// stmt->SetParamInt(2, (params.page - 1) * params.pageSize);

// PostgreSQL / SQLite: LIMIT + OFFSET
// ... "LIMIT ? OFFSET ?"
```

### Индексы — анализ планов запросов

```sql
-- Firebird: анализ плана
SET PLANONLY;
SELECT * FROM documents WHERE status = 'active' ORDER BY created_at DESC;
-- Вывод: PLAN (DOCUMENTS ORDER IDX_DOCUMENTS_STATUS) — индекс используется
-- Или:   PLAN (DOCUMENTS NATURAL) — full scan, нужен индекс!

-- Добавить индекс
CREATE INDEX idx_documents_status_created
  ON documents (status, created_at DESC);
```

### Пакетные операции (Batch Insert/Update)

```cpp
// Медленно — отдельный INSERT на каждую строку
for (const auto& item : items)
{
    ibPreparedStatement* stmt = db->PrepareStatement(
        "INSERT INTO items (doc_id, name) VALUES (?, ?)");
    stmt->SetParamInt(1, docId);
    stmt->SetParamString(2, item.name);
    stmt->RunQuery();
    stmt->Close();
}

// Быстро — один prepared statement, транзакция, batch
{
    ibTransactionGuard txn(db);
    OesStatementGuard stmt(db->PrepareStatement(
        "INSERT INTO items (doc_id, name) VALUES (?, ?)"));

    for (const auto& item : items)
    {
        stmt->SetParamInt(1, docId);
        stmt->SetParamString(2, item.name);
        stmt->RunQuery();
    }
    txn.Commit();
}
// Транзакция + один PreparedStatement = 10-100x быстрее
```

---

## Оптимизация UI

### Избегать лишних Refresh / Repaint

```cpp
// Неправильно — перерисовка на каждое изменение
for (const auto& item : items)
{
    m_grid->SetCellValue(row, 0, item.name);   // каждый вызов — repaint
    m_grid->SetCellValue(row, 1, item.status);
    row++;
}

// Правильно — заморозить обновление
m_grid->BeginBatch();
for (const auto& item : items)
{
    m_grid->SetCellValue(row, 0, item.name);
    m_grid->SetCellValue(row, 1, item.status);
    row++;
}
m_grid->EndBatch();
```

### Виртуальный список для больших данных

```cpp
// wxListCtrl в режиме wxLC_VIRTUAL — не хранит все элементы в памяти
class OesDocumentList : public wxListCtrl
{
public:
    OesDocumentList(wxWindow* parent)
        : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                     wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL)
    {}

    void SetItems(std::vector<DocumentInfo> items)
    {
        m_items = std::move(items);
        SetItemCount((long)m_items.size());
        Refresh();
    }

protected:
    // Вызывается только для видимых строк
    wxString OnGetItemText(long item, long column) const override
    {
        if (item < 0 || item >= (long)m_items.size()) return {};
        switch (column)
        {
            case 0: return m_items[item].name;
            case 1: return m_items[item].status;
            case 2: return m_items[item].createdAt;
        }
        return {};
    }

private:
    std::vector<DocumentInfo> m_items;
};
```

### Длительные операции в фоновом потоке

```cpp
// Не блокировать UI-поток операциями > 100 мс
// Использовать wxThread или std::thread + wxQueueEvent

class OesReportGeneratorThread : public wxThread
{
public:
    OesReportGeneratorThread(wxEvtHandler* handler,
                              const ReportParams& params)
        : wxThread(wxTHREAD_DETACHED)
        , m_handler(handler)
        , m_params(params)
    {}

protected:
    ExitCode Entry() override
    {
        ReportResult result = m_engine->Generate(m_params);

        // Отправить результат в UI-поток (потокобезопасно)
        auto* evt = new wxThreadEvent(OES_EVT_REPORT_DONE);
        evt->SetPayload(result);
        wxQueueEvent(m_handler, evt);
        return 0;
    }

private:
    wxEvtHandler*        m_handler;
    ReportParams         m_params;
    std::unique_ptr<IReportEngine> m_engine = CreateReportEngine();
};

// В UI-обработчике:
void OesReportView::OnGenerateReport(wxCommandEvent&)
{
    m_progressBar->Show();
    m_btnGenerate->Disable();

    auto* thread = new OesReportGeneratorThread(this, GetParams());
    if (thread->Run() != wxTHREAD_NO_ERROR)
    {
        wxLogError("[Report] Не удалось запустить поток генерации");
        delete thread;
    }
}

void OesReportView::OnReportDone(wxThreadEvent& evt)
{
    ReportResult result = evt.GetPayload<ReportResult>();
    m_progressBar->Hide();
    m_btnGenerate->Enable();

    if (result.success)
        LoadResult(result);
    else
        wxMessageBox(result.errorMsg, "Ошибка генерации отчёта",
                     wxOK | wxICON_ERROR, this);
}
```

---

## Оптимизация памяти

### Профилирование памяти

**Dr. Memory (Windows, бесплатный):**
```
drmemory -light -- OES.exe
— Находит утечки, use-after-free, invalid reads/writes
```

**Address Sanitizer (MSVC 2019+, Debug-сборка):**
```
В .vcxproj: C/C++ → Enable Address Sanitizer: Yes (/fsanitize=address)
— Ловит переполнения буфера, use-after-free в runtime
```

**Visual Studio Diagnostic Tools:**
```
Debug → Windows → Diagnostic Tools → Memory Usage
— Снимок кучи, сравнение снимков до/после операции
```

### Избегать лишних копирований

```cpp
// Дорого — копирование строки при каждом вызове
wxString GetDocumentName(int id)
{
    return m_documents[id].name;   // копирование wxString
}

// Дешевле — константная ссылка (если объект живёт достаточно долго)
const wxString& GetDocumentName(int id) const
{
    return m_documents[id].name;
}

// Move-семантика для передачи больших объектов
void SetDocumentList(std::vector<DocumentInfo> items)
{
    m_items = std::move(items);   // O(1) вместо O(n)
}
```

### Кэширование тяжёлых вычислений

```cpp
class OesDashboardModel
{
public:
    // Кэш статистики — пересчитывается только при изменении данных
    const DashboardStats& GetStats()
    {
        if (m_statsDirty)
        {
            m_stats = CalculateStats();
            m_statsDirty = false;
        }
        return m_stats;
    }

    void OnDocumentChanged()
    {
        m_statsDirty = true;  // инвалидация кэша
    }

private:
    DashboardStats CalculateStats();

    DashboardStats m_stats;
    bool           m_statsDirty = true;
};
```

### Cache-friendly структуры данных

```cpp
// Плохо — Array of Structures (AoS): при итерации — cache miss на каждом поле
struct DocumentRecord {
    int     id;
    wxString name;    // большой объект
    wxString status;  // большой объект
    wxString content; // очень большой объект
    wxDateTime createdAt;
};
std::vector<DocumentRecord> m_docs;

// При рендере списка (только id, name, status) — тянем весь content в кэш

// Лучше — разделить "горячие" и "холодные" данные
struct DocumentSummary {  // маленький, для списков
    int     id;
    wxString name;
    wxString status;
};

struct DocumentDetail {   // большой, загружается по требованию
    int     id;
    wxString content;
    wxString rawData;
};

std::vector<DocumentSummary> m_summaries;  // всегда в памяти
// Детали — загружаются при открытии конкретного документа
```

---

## Оптимизация сборки

### Release vs Debug

```
Debug:
  /MDd, /Od (без оптимизации), /Z7 (символы отладки)
  → медленно, но удобно для отладки

Release:
  /MD, /O2 (оптимизация скорости) или /Os (размер)
  /Oi (intrinsics), /GL (whole program optimization)
  → максимальная производительность

RelWithDebInfo (для профилирования):
  /MD, /O2, /Zi + /DEBUG при линковке
  → скорость Release + символы для профайлера
```

### Precompiled Headers (PCH)

```cpp
// stdafx.h / pch.h — предкомпилированный заголовок
// Включить все тяжёлые, редко меняющиеся заголовки
#pragma once
#include <wx/wx.h>
#include <wx/string.h>
#include <wx/datetime.h>
#include <wx/log.h>
#include <vector>
#include <memory>
#include <string>

// MSBuild: C/C++ → Precompiled Headers → Use (/Yu"stdafx.h")
// CMake:
// target_precompile_headers(OES PRIVATE src/stdafx.h)
```

### Unity Build (ускорение компиляции)

```cmake
# CMakeLists.txt — объединить несколько .cpp в одну единицу компиляции
set_target_properties(OES PROPERTIES UNITY_BUILD ON)
# Сокращает время компиляции в 2-5 раз для больших проектов
```

---

## Нагрузочное тестирование

### Тест с большими объёмами данных

```cpp
// tests/performance/test_bulk_operations.cpp
TEST(PerformanceTest, BulkInsert_1000Documents_Under5Seconds)
{
    auto db    = CreateTestDatabase();
    auto repo  = std::make_unique<OesDocumentRepository>(db.get());

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < 1000; ++i)
    {
        DocumentData doc;
        doc.name   = wxString::Format("Test Document %d", i);
        doc.status = "draft";
        int id;
        wxString err;
        auto res = repo->Create(doc, id);
        ASSERT_TRUE(res.ok) << res.error;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count();

    EXPECT_LT(elapsed, 5000) << "Вставка 1000 документов заняла " << elapsed << " мс";
}

// ВАЖНО: OesDocumentList — это wxListCtrl (наследник wxWindow).
// wxWidgets-контролы требуют родительского окна; передача nullptr приводит
// к неопределённому поведению или крашу. Такой тест является
// ИНТЕГРАЦИОННЫМ и должен выполняться с родительским фреймом.
//
// Паттерн wxTestableFrame (доступен в wxWidgets >= 3.1):
//
//   class RenderListTest : public wxTestCase   // wxWidgets test suite
//   {
//       void TestRenderList() {
//           wxTestableFrame* frame = new wxTestableFrame();
//           OesDocumentList* list  = new OesDocumentList(frame);
//           // ... тест ...
//           frame->Destroy();
//       }
//   };
//
// В среде Google Test (без event loop) используйте wxApp::SetInstance +
// wxEntryStart / wxEntryCleanup, либо вынесите этот тест в отдельный
// исполняемый файл интеграционных тестов, запускаемых в CI на Windows.

// Пример интеграционного теста (requires wxApp и visible frame):
// TEST(PerformanceIntegrationTest, RenderList_10000Items_Under200ms)
// {
//     // Предполагается, что wxApp уже инициализирован в main_test.cpp
//     wxFrame* frame = new wxFrame(nullptr, wxID_ANY, "Test");
//     frame->Show();
//
//     std::vector<DocumentSummary> items(10000);
//     for (int i = 0; i < 10000; ++i)
//         items[i] = {i, wxString::Format("Doc %d", i), "draft"};
//
//     auto start = std::chrono::high_resolution_clock::now();
//
//     OesDocumentList* list = new OesDocumentList(frame);
//     list->SetItems(std::move(items));
//
//     auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::high_resolution_clock::now() - start).count();
//
//     EXPECT_LT(elapsed, 200) << "Установка 10000 элементов заняла " << elapsed << " мс";
//     frame->Destroy();
// }
```

---

## Инструменты

| Инструмент | Назначение | Платформа |
|------------|-----------|-----------|
| Intel VTune | CPU hotspots, threading | Windows |
| Very Sleepy | CPU profiling (бесплатный) | Windows |
| Visual Studio Diagnostic Tools | CPU, память, в IDE | Windows |
| Dr. Memory | Утечки памяти, invalid access | Windows / Linux |
| Address Sanitizer (ASan) | Buffer overflow, use-after-free | MSVC 2019+ / GCC / Clang |
| perf + Flamegraph | CPU profiling | Linux |
| Valgrind (massif) | Анализ кучи | Linux |
| WinDbg | Анализ дампов, memory | Windows |
| `wxStopWatch` | Замер времени в коде | Кросс-платформенный |

---

## Чеклист производительности

### Перед релизом

- [ ] Приложение запускается < 3 сек на целевом железе (Core i5, 8 GB RAM, HDD)
- [ ] Открытие типичного документа < 1 сек
- [ ] Нет N+1 запросов в новых репозиториях
- [ ] Длительные операции (> 500 мс) вынесены в фоновый поток
- [ ] Пагинация включена на всех списках (pageSize ≤ 500)
- [ ] Индексы добавлены для новых полей фильтрации и сортировки
- [ ] Нет `SELECT *` в production-запросах
- [ ] Транзакции используются для batch-операций

### Периодически

- [ ] Профилирование сценария "открытие большого проекта" (VTune / Very Sleepy)
- [ ] Анализ планов медленных SQL-запросов (PLAN / EXPLAIN)
- [ ] Проверка утечек памяти (Dr. Memory или ASan-сборка)
- [ ] Нагрузочный тест: 10 000 документов в списке, генерация 500-строчного отчёта
- [ ] Проверка потребления RAM при работе в течение 8 часов (нет утечек)
