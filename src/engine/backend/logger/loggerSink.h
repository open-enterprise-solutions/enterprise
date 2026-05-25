#ifndef __IB_LOGGER_SINK_H__
#define __IB_LOGGER_SINK_H__

#include "backend/backend.h"
#include "backend/logger/loggerEntry.h"

#include <vector>

// Abstract destination for log entries. The writer thread calls
// WriteBatch once per drain. Sink owns its own connection / file handle
// and any rotation logic. Implementations must not throw — catch
// internally and best-effort recover on next call.
class BACKEND_API ibLoggerSink {
public:
    virtual ~ibLoggerSink() = default;

    virtual void WriteBatch(const std::vector<ibLogEntry>& batch) = 0;

    // Called once before the writer thread exits.
    virtual void Flush() {}
};

#endif
