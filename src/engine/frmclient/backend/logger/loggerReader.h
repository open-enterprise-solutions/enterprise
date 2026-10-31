#ifndef _FRMCLIENT_BACKEND_LOGGER_READER_H__
#define _FRMCLIENT_BACKEND_LOGGER_READER_H__

#include <wx/string.h>

#include <vector>

#include "core/logger/loggerQuery.h"   // ibLogRow / ibLogFilter — what the server's journal is asked and sends

// THE ENGINE'S READER (backend/logger/loggerReader.h) as the client's journal asks it: the journal is the server's,
// read through the tab that shows it — fetch {Tab, Request: the filter}. Order: newest first.
class ibLoggerReader {
public:
    explicit ibLoggerReader(long long tabId) : m_tabId(tabId) {}

    std::vector<ibLogRow> Query(const ibLogFilter& filter);

private:
    const long long m_tabId;
};

#endif
