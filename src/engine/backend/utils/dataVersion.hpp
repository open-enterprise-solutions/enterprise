/////////////////////////////////////////////////////////////////////////////
// DataVersion stamps for optimistic concurrency control on object Write.
//
// Every successful Write on a mutable-ref object (Catalog / Document /
// ChartOfAccounts / ChartOfCharacteristicTypes) bumps its DataVersion
// to a fresh stamp; the next Write compares the loaded-at-Read stamp
// against the row's current stamp and throws ibBackendLockException
// on mismatch ("changed by another user, please reload").
//
// Stamp format: 12-char base-62 (`[0-9A-Za-z]`) encoding of a 72-bit
// payload `(unix_ms_since_epoch << 16) | per_process_counter`. Wall-
// clock prefix keeps stamps roughly chronological for debugging, the
// 16-bit atomic counter rolls per process so two stamps minted within
// the same millisecond don't collide. We never compare stamps for
// ordering — only equality matters for the lost-update check, so
// clock skew across cluster nodes is harmless. Length matches the
// existing `DataVersion String(12)` predefined attribute declared on
// ibValueMetaObjectRecordDataMutableRef (commonObject.h:729).
//
// See docs/record-locks.md for the full design.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_DATA_VERSION_H_
#define _IB_DATA_VERSION_H_

#include "backend/backend.h"

namespace ibDataVersion {

// Mint a fresh stamp. Thread-safe. Always returns a 12-char string.
BACKEND_API wxString NewStamp();

// True if str matches the stamp shape (12 chars, all base-62).
// Used by tests and by Write paths that want to validate that the
// loaded value isn't garbage from a corrupted row.
BACKEND_API bool IsValidStamp(const wxString& str);

} // namespace ibDataVersion

#endif // _IB_DATA_VERSION_H_
