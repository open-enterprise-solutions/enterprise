#ifndef _FRMCLIENT_BACKEND_JOURNAL_H__
#define _FRMCLIENT_BACKEND_JOURNAL_H__

// THE TECHNOLOGY JOURNAL — the engine's (backend/diagnostics/journal.h) as the code copied from the desktop writes to
// it: the client's own journal (frmclient/diagnostics/journal.h), the same three marks.
#include "frmclient/diagnostics/journal.h"

#define ibJournalInfo    ibClientJournalInfo
#define ibJournalWarning ibClientJournalWarning
#define ibJournalError   ibClientJournalError

#endif
