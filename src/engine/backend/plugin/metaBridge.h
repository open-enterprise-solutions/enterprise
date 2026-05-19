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

} // namespace metaBridge

#endif // _IB_META_BRIDGE_H_
