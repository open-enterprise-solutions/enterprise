#ifndef __IB_PLATFORM_JOBS_H__
#define __IB_PLATFORM_JOBS_H__

// The platform's OWN jobs — the ones a configuration neither declares nor can
// remove, because they keep the engine's own state in shape.
//
// Kept apart from ibJobManager on purpose. The manager is a MECHANISM: a
// schedule, sessions, and a handoff to the worker pool, knowing nothing about
// totals or Firebird. This file is the POLICY: which of the engine's own
// housekeeping runs unattended and how often. One list, one place to read the
// answer to "what does this process do on its own?".
//
// WHO CALLS THIS. ibApplicationData, when a database opens — so every host that
// opens one gets the same list without repeating it in its own main.
//
// NOT a privileged path. This file holds no mechanism of its own: it fills in
// ibJobDescription and calls ibJobManager::Register, exactly as any other code
// would. Reach the manager through ibApplicationData::GetJobManager() and
// register whatever you like, whenever it makes sense for you to — this is just
// where the ENGINE's own jobs happen to be listed.
//
// Registration is idempotent by name — a second call is refused per job and
// logged, never a silent second copy.

#include "backend/backend.h"

// Register every platform-owned job with ibApplicationData::GetJobManager().
// No-op when there is no job manager (launcher, pre-bootstrap). Individual
// failures are logged and skipped: a housekeeping job that could not register
// must not stop the application from starting.
BACKEND_API void ibRegisterPlatformJobs();

#endif // !__IB_PLATFORM_JOBS_H__
