# 19. Performance

## Target metrics

| Operation | Target | Critical |
|----------|------|---------|
| App startup | < 3 sec | > 10 sec |
| Opening a document | < 1 sec | > 5 sec |
| Generating a report (100 rows) | < 2 sec | > 10 sec |
| Saving a document | < 500 ms | > 3 sec |
| SQL query (single object) | < 50 ms | > 500 ms |
| SQL query (list, 1000 rows) | < 500 ms | > 3 sec |
| Designer page rendering | < 16 ms (60 fps) | > 100 ms |
| Tab/dialog switching | < 200 ms | > 1 sec |

---

## Profiling

### Intel VTune Profiler (Windows)

The primary tool for CPU hotspot analysis:

```
1. Run OES in Release with /DEBUG symbols
2. VTune → New Analysis → Hotspots (CPU)
3. Run the app, reproduce the slow scenario
4. Stop the collection
5. Analysis:
   - Bottom-Up → find functions with the highest CPU Time
   - Call Stack → understand where the hot function is called from
   - Source View → exact spot in the code
```

### Very Sleepy (free, Windows)

```
1. Run OES
2. Very Sleepy → Attach to process → pick OES.exe
3. Start / Stop profiling during the slow operation
4. Inspect Call Tree — find the hotspot
```

### Visual Studio Diagnostic Tools

```
Debug → Performance Profiler → CPU Usage
— no need to stop the app, runs straight in the IDE
— useful for spotting regressions quickly
```

### perf (Linux / cross-platform build)

```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Profile
perf record -g ./OES
perf report --stdio

# Flamegraph
# Requires cloning brendangregg/FlameGraph:
#   git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
# Then add it to PATH:
#   export PATH="$HOME/FlameGraph:$PATH"
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

---

## Database optimization

### N+1 queries — the main performance problem

```cpp
// WRONG — N+1: one query for the list + N queries for details
std::vector<DocumentInfo> docs = m_repo->GetList();  // 1 query
for (auto& doc : docs)
{
    // Each call — a separate SELECT
    doc.sections = m_sectionRepo->GetByDocId(doc.id);  // N queries
    doc.owner    = m_userRepo->GetById(doc.ownerId);    // another N queries
}

// RIGHT — JOIN in a single query
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
// Then assemble the object graph from a flat result set
```

### Prepared statement optimization

```cpp
// Reuse prepared statements — do not re-create on every call
class OesDocumentRepository
{
public:
    explicit OesDocumentRepository(ibDatabaseLayer* db)
        : m_db(db)
    {
        // Prepare once when the repository is created
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
        // Reuse the prepared statement
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

### Pagination — mandatory for lists

```cpp
// Always cap the result set — never SELECT without LIMIT
struct QueryParams
{
    int     page      = 1;
    int     pageSize  = 50;   // max 500
    wxString sortField = "created_at";
    bool    sortAsc   = false;
    wxString filter;
};

// Firebird: ROWS M TO N
// NOTE: the "ROWS ? TO ?" syntax is not supported as parameterized
// placeholders by every version of the ibDatabase/IBPP driver.
// If the driver does not support it — use the safe alternative:
//   "SELECT ... FROM ... ORDER BY ... ROWS " + rowFrom + " TO " + rowTo
// or the preferred FIRST/SKIP syntax (note the argument order):
//   "SELECT FIRST ? SKIP ? id, name, status FROM documents ..."
// where FIRST = pageSize, SKIP = (page-1)*pageSize.
// Verify the docs of the Firebird API wrapper you're using.

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

// More portable Firebird alternative (FIRST ? SKIP ?):
// ibPreparedStatement* stmt = db->PrepareStatement(
//     "SELECT FIRST ? SKIP ? id, name, status "
//     "FROM documents WHERE is_deleted = 0 ORDER BY created_at DESC"
// );
// stmt->SetParamInt(1, params.pageSize);
// stmt->SetParamInt(2, (params.page - 1) * params.pageSize);

// PostgreSQL / SQLite: LIMIT + OFFSET
// ... "LIMIT ? OFFSET ?"
```

### Indexes — query plan analysis

```sql
-- Firebird: plan analysis
SET PLANONLY;
SELECT * FROM documents WHERE status = 'active' ORDER BY created_at DESC;
-- Output: PLAN (DOCUMENTS ORDER IDX_DOCUMENTS_STATUS) — index used
-- Or:     PLAN (DOCUMENTS NATURAL) — full scan, needs an index!

-- Add the index
CREATE INDEX idx_documents_status_created
  ON documents (status, created_at DESC);
```

### Batch operations (Batch Insert/Update)

```cpp
// Slow — a separate INSERT for every row
for (const auto& item : items)
{
    ibPreparedStatement* stmt = db->PrepareStatement(
        "INSERT INTO items (doc_id, name) VALUES (?, ?)");
    stmt->SetParamInt(1, docId);
    stmt->SetParamString(2, item.name);
    stmt->RunQuery();
    stmt->Close();
}

// Fast — one prepared statement, one transaction, batch
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
// One transaction + one PreparedStatement = 10-100x faster
```

---

## UI optimization

### Avoid extra Refresh / Repaint

```cpp
// Wrong — repaint on every change
for (const auto& item : items)
{
    m_grid->SetCellValue(row, 0, item.name);   // each call — repaint
    m_grid->SetCellValue(row, 1, item.status);
    row++;
}

// Right — freeze updates
m_grid->BeginBatch();
for (const auto& item : items)
{
    m_grid->SetCellValue(row, 0, item.name);
    m_grid->SetCellValue(row, 1, item.status);
    row++;
}
m_grid->EndBatch();
```

### Virtual list for large data

```cpp
// wxListCtrl in wxLC_VIRTUAL mode — doesn't keep every item in memory
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
    // Called only for visible rows
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

### Long operations on a background thread

```cpp
// Do not block the UI thread with operations > 100 ms
// Use wxThread or std::thread + wxQueueEvent

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

        // Send the result to the UI thread (thread-safe)
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

// In the UI handler:
void OesReportView::OnGenerateReport(wxCommandEvent&)
{
    m_progressBar->Show();
    m_btnGenerate->Disable();

    auto* thread = new OesReportGeneratorThread(this, GetParams());
    if (thread->Run() != wxTHREAD_NO_ERROR)
    {
        wxLogError("[Report] Failed to start the generation thread");
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
        wxMessageBox(result.errorMsg, "Report generation error",
                     wxOK | wxICON_ERROR, this);
}
```

---

## Memory optimization

### Memory profiling

**Dr. Memory (Windows, free):**
```
drmemory -light -- OES.exe
— finds leaks, use-after-free, invalid reads/writes
```

**Address Sanitizer (MSVC 2019+, Debug build):**
```
In .vcxproj: C/C++ → Enable Address Sanitizer: Yes (/fsanitize=address)
— catches buffer overflows, use-after-free at runtime
```

**Visual Studio Diagnostic Tools:**
```
Debug → Windows → Diagnostic Tools → Memory Usage
— heap snapshot, compare before/after an operation
```

### Avoid unnecessary copies

```cpp
// Expensive — copies the string on every call
wxString GetDocumentName(int id)
{
    return m_documents[id].name;   // wxString copy
}

// Cheaper — const reference (if the object lives long enough)
const wxString& GetDocumentName(int id) const
{
    return m_documents[id].name;
}

// Move semantics for passing large objects
void SetDocumentList(std::vector<DocumentInfo> items)
{
    m_items = std::move(items);   // O(1) instead of O(n)
}
```

### Caching heavy computations

```cpp
class OesDashboardModel
{
public:
    // Stats cache — recomputed only when data changes
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
        m_statsDirty = true;  // invalidate the cache
    }

private:
    DashboardStats CalculateStats();

    DashboardStats m_stats;
    bool           m_statsDirty = true;
};
```

### Cache-friendly data structures

```cpp
// Bad — Array of Structures (AoS): iteration — cache miss on each field
struct DocumentRecord {
    int     id;
    wxString name;    // big object
    wxString status;  // big object
    wxString content; // very big object
    wxDateTime createdAt;
};
std::vector<DocumentRecord> m_docs;

// When rendering the list (id, name, status only) — pulls the whole content into cache

// Better — split "hot" and "cold" data
struct DocumentSummary {  // small, for lists
    int     id;
    wxString name;
    wxString status;
};

struct DocumentDetail {   // large, loaded on demand
    int     id;
    wxString content;
    wxString rawData;
};

std::vector<DocumentSummary> m_summaries;  // always in memory
// Details — loaded when a specific document is opened
```

---

## Build optimization

### Release vs Debug

```
Debug:
  /MDd, /Od (no optimization), /Z7 (debug symbols)
  → slow, but easy to debug

Release:
  /MD, /O2 (speed) or /Os (size)
  /Oi (intrinsics), /GL (whole program optimization)
  → maximum performance

RelWithDebInfo (for profiling):
  /MD, /O2, /Zi + /DEBUG at link time
  → Release speed + symbols for the profiler
```

### Precompiled Headers (PCH)

```cpp
// stdafx.h / pch.h — precompiled header
// Include all heavy, rarely-changing headers
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

### Unity Build (compilation speedup)

```cmake
# CMakeLists.txt — combine multiple .cpp into a single translation unit
set_target_properties(OES PROPERTIES UNITY_BUILD ON)
# Cuts compile time by 2-5x for large projects
```

---

## Load testing

### Test with large data volumes

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

    EXPECT_LT(elapsed, 5000) << "Inserting 1000 documents took " << elapsed << " ms";
}

// IMPORTANT: OesDocumentList is a wxListCtrl (derived from wxWindow).
// wxWidgets controls need a parent window; passing nullptr is undefined
// behaviour or causes a crash. Such a test is an INTEGRATION test and
// must run with a parent frame.
//
// wxTestableFrame pattern (available in wxWidgets >= 3.1):
//
//   class RenderListTest : public wxTestCase   // wxWidgets test suite
//   {
//       void TestRenderList() {
//           wxTestableFrame* frame = new wxTestableFrame();
//           OesDocumentList* list  = new OesDocumentList(frame);
//           // ... test ...
//           frame->Destroy();
//       }
//   };
//
// In a Google Test environment (no event loop) use wxApp::SetInstance +
// wxEntryStart / wxEntryCleanup, or extract this test into a separate
// integration-test binary that runs in CI on Windows.

// Integration test example (requires wxApp and a visible frame):
// TEST(PerformanceIntegrationTest, RenderList_10000Items_Under200ms)
// {
//     // Assumes wxApp is already initialized in main_test.cpp
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
//     EXPECT_LT(elapsed, 200) << "Setting 10000 items took " << elapsed << " ms";
//     frame->Destroy();
// }
```

---

## Tools

| Tool | Purpose | Platform |
|------------|-----------|-----------|
| Intel VTune | CPU hotspots, threading | Windows |
| Very Sleepy | CPU profiling (free) | Windows |
| Visual Studio Diagnostic Tools | CPU, memory, in-IDE | Windows |
| Dr. Memory | Memory leaks, invalid access | Windows / Linux |
| Address Sanitizer (ASan) | Buffer overflow, use-after-free | MSVC 2019+ / GCC / Clang |
| perf + Flamegraph | CPU profiling | Linux |
| Valgrind (massif) | Heap analysis | Linux |
| WinDbg | Dump analysis, memory | Windows |
| `wxStopWatch` | In-code timing | Cross-platform |

---

## Performance checklist

### Before release

- [ ] App starts in < 3 sec on target hardware (Core i5, 8 GB RAM, HDD)
- [ ] Opening a typical document < 1 sec
- [ ] No N+1 queries in new repositories
- [ ] Long operations (> 500 ms) moved to a background thread
- [ ] Pagination enabled on every list (pageSize ≤ 500)
- [ ] Indexes added for new filter/sort columns
- [ ] No `SELECT *` in production queries
- [ ] Transactions used for batch operations

### Periodically

- [ ] Profile the "open large project" scenario (VTune / Very Sleepy)
- [ ] Analyze plans for slow SQL queries (PLAN / EXPLAIN)
- [ ] Check for memory leaks (Dr. Memory or ASan build)
- [ ] Load test: 10 000 documents in the list, 500-row report generation
- [ ] RAM usage check after 8 hours of work (no leaks)
