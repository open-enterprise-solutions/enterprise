/////////////////////////////////////////////////////////////////////////////
// metaBridge — implements the ABI v4 Meta* host trampolines against
// activeMetaData (the live configuration). Phase 3 of the Sigma chat
// pane spec uses these entry points to let an AI agent read and mutate
// metadata atomically — every mutation is wrapped in the active
// document's wxCommandProcessor so a single Ctrl+Z reverts the whole
// agent turn.
//
// Phase 3.1 (this header) only ships the read path — MetaQuery.
// Mutation paths (MetaCreate / MetaEdit / MetaDelete) follow in
// subsequent commits and reuse the kind ↔ CLSID resolver here.
//
// Memory contract: jsonOut / errorMsg out-parameters are allocated by
// the host with malloc(); the plugin caller frees them with free().
// On failure both pointers are set to NULL unless the failure mode
// itself carries diagnostic text (then errorMsg is allocated).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_META_BRIDGE_H_
#define _IB_META_BRIDGE_H_

#include "backend/backend.h"

class ibPluginManager;

namespace metaBridge {

// Resolve a kind label ("Catalog", "Document", "AccountingRegister", …)
// to its corresponding metaobject CLSID. Returns 0 for unknown kinds.
unsigned long long KindStringToCLSID(const char* kind);

// MetaQuery host impl. Returns 0 on success and writes a malloc'd
// UTF-8 JSON document to *jsonOut. Returns non-zero on failure and
// (optionally) writes a malloc'd UTF-8 diagnostic to *errorMsg.
//
// fullName layout: "<Kind>.<Name>" — top-level only in Phase 3.1.
// fieldsFilter is currently ignored (reserved for partial-field reads
// once the agent learns to scope its context window).
int HostMetaQuery(const char* fullName,
                   const char* fieldsFilter,
                   char**      jsonOut,
                   char**      errorMsg);

// Phase 3.2 — mutation surface. Behaviour shared by all three:
//   * Main-thread only (asserted via wxIsMainThread).
//   * Policy gate first: CheckMutationAllowed(pluginId, opName) is the
//     industry-standard 4-mode permission check (Ask / AllowSession /
//     AllowAlways / Deny). Failure returns IB_PLUGIN_PERMISSION_DENIED.
//   * Every successful mutation pushes an undo lambda onto a turn stack;
//     UndoLastAgentMutation() reverts the most recent push. Designer
//     wires this into Ctrl+Z in Phase 3.3.
//   * Audit: every allow/deny + every mutation logs via wxLogMessage.
//
// pluginId carries the caller identity for policy keying. Phase 3.2
// trampoline pulls it from the ibPluginCallScope; for tests we accept
// it as the first argument so unit cases don't need a live plugin.
int HostMetaCreate(const char* pluginId,
                    const char* objectKind,
                    const char* fullName,
                    const char* propertiesJson,
                    char**      errorMsg);

int HostMetaEdit(const char* pluginId,
                  const char* fullName,
                  const char* jsonPatch,
                  char**      errorMsg);

int HostMetaDelete(const char* pluginId,
                    const char* fullName,
                    const char* propertiesJson,
                    char**      errorMsg);

// Revert the most recently pushed agent mutation. Returns 0 when at
// least one mutation was undone; non-zero when the undo stack is empty
// or every remaining entry belongs to a stale configuration epoch
// (those get dropped silently with a wxLog audit line).
int UndoLastAgentMutation();

// Designer / pluginManager calls this when activeMetaData is about to
// be replaced (configuration reload, project switch, abort). Advances
// the internal epoch + clears the stack so shared_ptr-owned victims
// from MetaDelete are freed cleanly + pending undo entries become
// inert. MUST run on the main thread before the old activeMetaData is
// freed. Idempotent — safe to call repeatedly (e.g. UnloadAll fires
// twice on process exit: once from ~ibApplicationData, once from
// ~ibPluginManager). Each repeated call clears an already-empty stack
// and bumps the epoch.
void NotifyConfigurationUnload();

// Test-only hook — wipes the undo stack + advances the epoch so unit
// cases run independently from any prior state.
void ClearUndoStackForTests();

// Test-only hook — inject a pluginManager pointer that the Meta*
// trampolines use instead of pulling appData->GetPluginManager().
// Call with a manager constructed in test scope; clear with nullptr
// in TearDown so production paths return to using appData.
//
// Lets the GTEST_SKIP'd Phase 3.4 integration cases run without
// booting a full ibApplicationData fixture.
void SetPluginManagerOverrideForTests(::ibPluginManager* override);

} // namespace metaBridge

#endif // _IB_META_BRIDGE_H_
