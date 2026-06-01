#ifndef _IB_APP_ENV_H_
#define _IB_APP_ENV_H_

// appEnv — thin accessor surface for the application-environment
// subsystems, the ones that come up between `CreateAppDataEnv` and
// `DestroyAppDataEnv`. Namespace name picks up that symmetry.
//
// Inverts the include direction: callers that only need to reach a
// subsystem pull this header (forward decls + free-function
// signatures) instead of `backend/appData.h` (which transitively
// brings the connection pool, plugin manager, FB driver headers, ...).
//
// Ownership is unchanged — ibApplicationData still creates and destroys
// every subsystem via its m_X unique_ptr members; ctor of each subsystem
// stays private with `friend ibApplicationData`. The free functions here
// are out-of-line and delegate to ibApplicationData::GetX() inside
// appEnv.cpp, which is the only file that depends on the heavyweight
// appData.h.
//
// All accessors return nullptr when no appData is alive (pre-init and
// post-destroy). Callers MUST null-check — the signatures document the
// precondition explicitly. Subsystems that are mode-gated (logger,
// metadata, debugger) also return nullptr when the active runMode does
// not allocate them (e.g. launcher has no metadata; designer has no
// debug server; codeRunner has no metadata + no debug server).

#include "backend/backend.h"

class ibSessionRegistry;
class ibConnectionPool;
class ibLockManager;
class ibLogger;
class ibPluginManager;
class ibMetaDataConfigurationBase;
class ibDebuggerServer;
class ibDebuggerClient;

namespace appEnv {

// ---- Always-on subsystems (created in ibApplicationData ctor) ---------
// Present in every runMode that has appData at all. nullptr only between
// process start and ibApplicationData::CreateAppDataEnv, or after
// DestroyAppDataEnv.

BACKEND_API ibSessionRegistry* SessionRegistry();
BACKEND_API ibConnectionPool*  ConnectionPool();
BACKEND_API ibLockManager*     LockManager();
BACKEND_API ibPluginManager*   PluginManager();

// ---- Mode-gated subsystems --------------------------------------------
// Created during CreateFile/Server AppDataEnv only for runModes that
// actually use them. Caller's null-check matters more here.

// Audit + trace logger. Created for every mode that opens a DB
// (enterprise / designer / daemon / wes); nullptr in launcher.
BACKEND_API ibLogger* Logger();

// Active configuration metadata — the one the engine has loaded into
// appData. nullptr in launcher and in codeRunner (which opens metadata
// lazily per-DataProcessor).
//
// External metadata (.epf DataProcessor files, .erf Report files, .obk
// snapshots opened from disk) are NOT covered by this accessor — they
// live as runtime nodes under ibValueModuleRuntimeManagerExternalDataProcessor
// / ibValueModuleRuntimeManagerExternalReport on each open session, with their
// own per-instance ibMetaData. ActiveMetaData() is exclusively the
// process-wide configuration that backs the running database.
BACKEND_API ibMetaDataConfigurationBase* ActiveMetaData();

// Debugger endpoints — exclusive of each other. enterprise / wes own
// the DebugServer (when started with --debug); designer owns the
// DebugClient (always).
//
// Ownership chain: appData -> ActiveMetaData -> m_debugServer /
// m_debugClient. Debugger without metadata is meaningless — there is
// no compiled module to step through, no breakpoints to set, no eval
// context to bind. Living the debugger on metadata makes it impossible
// to construct an inconsistent state where you have a debug session
// but no code to debug.
//
// Practical effect for callers: nullptr when ActiveMetaData() is
// nullptr (launcher / codeRunner / pre-init) AND nullptr when the
// active mode doesn't host this endpoint (designer has no server,
// runtime without --debug has no server, runtime has no client).
BACKEND_API ibDebuggerServer* DebugServer();
BACKEND_API ibDebuggerClient* DebugClient();

} // namespace appEnv

#endif // _IB_APP_ENV_H_
