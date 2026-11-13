#include "backend/logger/loggerSweep.h"

#include "core/fileSystem.h"   // ibDir, ibRemoveFile
#include "core/fdatetime.h"    // ibDateTime — the cutoff and a file's month, on the machine's clock

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
    const ibDateTime cutoff = ibDateTime::Now().AddDays(-retentionDays);

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
            const ibDateTime bucketEnd = m == 12 ? ibDateTime(y + 1, 1, 1) : ibDateTime(y, m + 1, 1);
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
