#ifndef __IB_LOGGER_READER_H__
#define __IB_LOGGER_READER_H__

#include "backend/backend.h"
#include "backend/logger/loggerEntry.h"
#include "core/logger/loggerQuery.h"   // ibLogRow / ibLogFilter — what this reader is asked and answers with

#include <wx/string.h>

#include <cstddef>
#include <vector>

// Read-side facade. Opens .olg files in `dir` (no writes — sink owns
// writes, reader is purely a query surface). Each Query call walks
// only the files whose YYYY_MM falls within the filter's date range.
// Order: ts_ms DESC, id DESC (newest first).
class BACKEND_API ibLoggerReader {
public:
    explicit ibLoggerReader(const wxString& dir);

    std::vector<ibLogRow> Query(const ibLogFilter& filter);
    std::size_t           Count(const ibLogFilter& filter);

private:
    // Enumerate .olg files in m_dir whose (year, month) overlap with
    // the filter's [from_ms, to_ms) range. Sorted newest month first.
    std::vector<wxString> CollectFiles(const ibLogFilter& filter) const;

    wxString m_dir;
};

#endif
