#include "backend/logger/loggerSweep.h"

#include "core/fileSystem.h"   // ibDir, ibRemoveFile

#include <wx/datetime.h>

namespace {

bool ParseLogFileMonth(const ibString& name, int& year, int& month)
{
    if (!name.StartsWith(ibT("oes_"))) return false;
    if (!name.EndsWith(ibT(".olg")))   return false;
    long y = 0, m = 0;
    if (!name.Mid(4, 4).ToLong(&y)) return false;
    if (!name.Mid(9, 2).ToLong(&m)) return false;
    year  = static_cast<int>(y);
    month = static_cast<int>(m);
    return year >= 1970 && month >= 1 && month <= 12;
}

}   // namespace

int ibLoggerSweep::RunOnce(const ibString& dir, int retentionDays)
{
    if (retentionDays <= 0) return 0;
    if (dir.IsEmpty() || !ibDirExists(dir)) return 0;

    // Cutoff = first instant we still want to keep. A file's month
    // bucket must END strictly after cutoff to survive — i.e. any
    // file whose bucket's last day is before cutoff goes away.
    const wxDateTime cutoff = wxDateTime::Now()
        .Subtract(wxDateSpan::Days(retentionDays));

    ibDir d(dir);
    if (!d.IsOpened()) return 0;

    int removed = 0;
    ibString name;
    bool more = d.GetFirst(&name, ibT("*.olg"), ibDIR_FILES);
    while (more) {
        int y = 0, m = 0;
        if (ParseLogFileMonth(name, y, m)) {
            // Bucket end = first day of (year, month+1). If that's <= cutoff
            // the whole bucket is older than retention → delete.
            wxDateTime bucketStart(1, static_cast<wxDateTime::Month>(m - 1), y, 0, 0, 0);
            wxDateTime bucketEnd = bucketStart;
            bucketEnd.Add(wxDateSpan::Month());
            if (bucketEnd <= cutoff) {
                const ibString path = dir + ibFILE_SEP_PATH + name;
                if (ibRemoveFile(path)) ++removed;
                // Also clean up the WAL sidecar files SQLite leaves behind. They
                // are transient — SQLite removes them on a clean close, so their
                // absence is the NORMAL case, and ibRemoveFile says nothing of it.
                ibRemoveFile(path + ibT("-wal"));
                ibRemoveFile(path + ibT("-shm"));
            }
        }
        more = d.GetNext(&name);
    }
    return removed;
}
