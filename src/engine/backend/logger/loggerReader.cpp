#include "backend/logger/loggerReader.h"

#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/preparedStatement.h"
#include "backend/databaseLayer/databaseResultSet.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/datetime.h>

#include <algorithm>

namespace {

bool ParseLogFileName(const wxString& name, int& year, int& month)
{
    // Expected: oes_YYYY_MM.olg
    if (!name.StartsWith(wxT("oes_"))) return false;
    long y = 0, m = 0;
    if (!name.Mid(4, 4).ToLong(&y)) return false;
    if (!name.Mid(9, 2).ToLong(&m)) return false;
    year  = static_cast<int>(y);
    month = static_cast<int>(m);
    return year >= 1970 && month >= 1 && month <= 12;
}

// Returns true when the year-month bucket intersects the filter's
// date range. Bounded by 0 → "no constraint".
bool MonthInRange(int year, int month,
                  wxLongLong_t from_ms, wxLongLong_t to_ms)
{
    if (from_ms == 0 && to_ms == 0) return true;

    // Convert (year, month) bucket to ms range [start, nextMonthStart).
    wxDateTime start(1, static_cast<wxDateTime::Month>(month - 1), year, 0, 0, 0);
    wxDateTime nextMonth = start;
    nextMonth.Add(wxDateSpan::Month());

    const wxLongLong_t bucketStart = start.GetValue().GetValue();
    const wxLongLong_t bucketEnd   = nextMonth.GetValue().GetValue();

    if (from_ms != 0 && bucketEnd   <= from_ms) return false;
    if (to_ms   != 0 && bucketStart >= to_ms)   return false;
    return true;
}

// Build the WHERE clause + bind values from the filter. Returns the
// SQL fragment (without leading WHERE / AND). bindStrings receives
// string params in positional order; bindInts receives int params in
// positional order. The caller binds them after a shared statement
// prefix.
//
// We keep two parallel arrays (str / int) instead of one heterogeneous
// list to dodge wxVariant churn — bind sites are statically typed via
// SetParamString / SetParamInt at the call site below.
struct BoundFilter {
    wxString               where;       // "" or "ts_ms >= ? AND ..."
    std::vector<wxString>  strs;
    std::vector<wxLongLong_t> nums;     // mixed int / longlong storage
    // Order in which params are bound 1..N:
    //   p[i].first  == 'S' → strs[stringIdx++]
    //   p[i].first  == 'L' → nums[longIdx++] as double
    //   p[i].first  == 'I' → nums[longIdx++] as int
    std::vector<char> kinds;
};

BoundFilter BuildWhere(const ibLogFilter& f)
{
    BoundFilter b;
    wxString w;
    auto andAdd = [&](const wxString& clause) {
        if (!w.IsEmpty()) w += wxT(" AND ");
        w += clause;
    };

    if (f.from_ms != 0)        { andAdd(wxT("ts_ms >= ?")); b.nums.push_back(f.from_ms); b.kinds.push_back('L'); }
    if (f.to_ms   != 0)        { andAdd(wxT("ts_ms <  ?")); b.nums.push_back(f.to_ms);   b.kinds.push_back('L'); }
    if (f.min_level >= 0)      { andAdd(wxT("level >= ?")); b.nums.push_back(f.min_level); b.kinds.push_back('I'); }
    if (!f.user_name.IsEmpty()){ andAdd(wxT("user_name = ?")); b.strs.push_back(f.user_name); b.kinds.push_back('S'); }
    if (!f.session_id.IsEmpty()){ andAdd(wxT("session_id = ?")); b.strs.push_back(f.session_id); b.kinds.push_back('S'); }
    if (!f.source.IsEmpty())   { andAdd(wxT("source = ?"));    b.strs.push_back(f.source);    b.kinds.push_back('S'); }
    if (!f.event_type.IsEmpty()){andAdd(wxT("event_type = ?"));b.strs.push_back(f.event_type);b.kinds.push_back('S'); }
    if (!f.ref_guid.IsEmpty()) { andAdd(wxT("ref_guid = ?"));  b.strs.push_back(f.ref_guid);  b.kinds.push_back('S'); }
    if (!f.search.IsEmpty())   { andAdd(wxT("message LIKE ?"));b.strs.push_back(wxT("%") + f.search + wxT("%")); b.kinds.push_back('S'); }

    b.where = w;
    return b;
}

void BindParams(ibPreparedStatement* stmt, const BoundFilter& b)
{
    std::size_t s = 0, n = 0;
    int pos = 1;
    for (char k : b.kinds) {
        switch (k) {
        case 'S': stmt->SetParamString(pos, b.strs[s++]); break;
        case 'L': stmt->SetParamDouble(pos, static_cast<double>(b.nums[n++])); break;
        case 'I': stmt->SetParamInt   (pos, static_cast<int>(b.nums[n++])); break;
        }
        ++pos;
    }
}

}   // namespace

ibLoggerReader::ibLoggerReader(const wxString& dir)
    : m_dir(dir) {}

std::vector<wxString> ibLoggerReader::CollectFiles(const ibLogFilter& filter) const
{
    std::vector<wxString> files;
    if (m_dir.IsEmpty() || !wxFileName::DirExists(m_dir)) return files;

    wxDir d(m_dir);
    if (!d.IsOpened()) return files;

    // Pair (year*100 + month) → fullPath for deterministic newest-first sort.
    std::vector<std::pair<int, wxString>> tagged;
    wxString name;
    bool more = d.GetFirst(&name, wxT("*.olg"), wxDIR_FILES);
    while (more) {
        int y = 0, m = 0;
        if (ParseLogFileName(name, y, m) && MonthInRange(y, m, filter.from_ms, filter.to_ms)) {
            tagged.emplace_back(y * 100 + m,
                                m_dir + wxFileName::GetPathSeparator() + name);
        }
        more = d.GetNext(&name);
    }
    std::sort(tagged.begin(), tagged.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    files.reserve(tagged.size());
    for (auto& p : tagged) files.push_back(std::move(p.second));
    return files;
}

std::vector<ibLogRow> ibLoggerReader::Query(const ibLogFilter& filter)
{
    std::vector<ibLogRow> rows;
    const auto files = CollectFiles(filter);
    if (files.empty()) return rows;

    BoundFilter bf = BuildWhere(filter);
    wxString sql = wxT("SELECT ts_ms, level, session_id, user_name, host,"
                       " source, event_type, message, ref_guid, ref_meta_id"
                       " FROM log_entry");
    if (!bf.where.IsEmpty()) sql += wxT(" WHERE ") + bf.where;
    sql += wxT(" ORDER BY ts_ms DESC, id DESC");
    // LIMIT bound per-file (after offset within file order). Whole-result
    // pagination needs cross-file sort then cut; for MVP we cap per-file
    // at limit + offset so we always have enough to fulfil the final cut.
    if (filter.limit != 0) {
        sql += wxString::Format(wxT(" LIMIT %zu"), filter.limit + filter.offset);
    }

    const std::size_t cap = filter.limit == 0
        ? static_cast<std::size_t>(-1)
        : filter.limit;
    std::size_t skipped = 0;

    for (const wxString& path : files) {
        ibDatabaseLayerSQLite db;
        if (!db.Open(path)) continue;
        try {
            ibPreparedStatement* raw = db.PrepareStatement(sql);
            if (raw == nullptr) { db.Close(); continue; }
            ibStatementGuard stmt(&db, raw);
            BindParams(raw, bf);

            ibDatabaseResultSet* rs = raw->RunQueryWithResults();
            if (rs == nullptr) { db.Close(); continue; }

            while (rs->Next()) {
                if (filter.offset > 0 && skipped < filter.offset) {
                    ++skipped;
                    continue;
                }
                ibLogRow r;
                r.ts_ms       = static_cast<wxLongLong_t>(rs->GetResultDouble(1));
                r.level       = rs->GetResultInt(2);
                r.session_id  = rs->GetResultString(3);
                r.user_name   = rs->GetResultString(4);
                r.host        = rs->GetResultString(5);
                r.source      = rs->GetResultString(6);
                r.event_type  = rs->GetResultString(7);
                r.message     = rs->GetResultString(8);
                r.ref_guid    = rs->GetResultString(9);
                r.ref_meta_id = rs->GetResultInt(10);
                rows.push_back(std::move(r));
                if (rows.size() >= cap) break;
            }
            raw->CloseResultSet(rs);
        } catch (...) {
            // Swallow per-file errors — corrupted .olg in middle of
            // dir set should not kill the whole query.
        }
        db.Close();
        if (rows.size() >= cap) break;
    }
    return rows;
}

std::size_t ibLoggerReader::Count(const ibLogFilter& filter)
{
    const auto files = CollectFiles(filter);
    if (files.empty()) return 0;

    BoundFilter bf = BuildWhere(filter);
    wxString sql = wxT("SELECT COUNT(*) FROM log_entry");
    if (!bf.where.IsEmpty()) sql += wxT(" WHERE ") + bf.where;

    std::size_t total = 0;
    for (const wxString& path : files) {
        ibDatabaseLayerSQLite db;
        if (!db.Open(path)) continue;
        try {
            ibPreparedStatement* raw = db.PrepareStatement(sql);
            if (raw == nullptr) { db.Close(); continue; }
            ibStatementGuard stmt(&db, raw);
            BindParams(raw, bf);

            ibDatabaseResultSet* rs = raw->RunQueryWithResults();
            if (rs != nullptr && rs->Next()) {
                total += static_cast<std::size_t>(rs->GetResultInt(1));
            }
            if (rs) raw->CloseResultSet(rs);
        } catch (...) {
            // ignore — see Query.
        }
        db.Close();
    }
    return total;
}
