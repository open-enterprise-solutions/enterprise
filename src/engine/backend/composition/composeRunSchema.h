#ifndef __IB_COMPOSE_RUN_SCHEMA_H__
#define __IB_COMPOSE_RUN_SCHEMA_H__

////////////////////////////////////////////////////////////////////////////
//	Description : RUN A COMPOSITION SCHEMA and read back what it answers -
//	              the half that runs where data may be touched, given a
//	              schema rather than the name of one.
////////////////////////////////////////////////////////////////////////////
//
// 🛑 THE DESIGNER DOES NOT WORK WITH DATA — a rule about RIGHTS, not about where a runtime lives. A
// designer holds a CONFIGURATION; a person's rows belong to the application they started, and
// starting it with the debugger attached is the visible act of consent.
//
// So the split falls where the rights do:
//
//   the designer  — resolves the composer, picks the variant, restores a saved setting. Metadata.
//   the wire      — carries a description, a settings section and the parameters. Nothing to look up.
//   the runtime   — builds a composer out of what arrived and RUNS it, on a Tenant rental: the rows
//                   are the ones that person sees, and their window is not blocked while it reads.

#include "backend/backend.h"
#include "backend/serialize/dataBuilder.h"

// THE REQUEST'S FIELDS, declared once because two processes read them. A name that drifts here is a
// field that silently stops arriving - which is not a compile error anywhere.
constexpr const wxChar* kComposeSchema     = wxT("schema");
constexpr const wxChar* kComposeSettings   = wxT("settings");
constexpr const wxChar* kComposeParameters = wxT("parameters");

class BACKEND_API ibComposeRunSchema {
public:

	// Build a composer out of `request` and run it; write what it answered into `result` - one table
	// per output, each a grouping or a cross-table, every figure carrying its address.
	//
	// ⚠ A refusal is words for whoever asked. This layer knows why a compose produced nothing (a
	// parameter nobody set, a source that is not there); a caller across a socket cannot work it out.
	static bool Run(const ibDataNode& request, ibDataNode& result, wxString& refusal);
};

#endif
