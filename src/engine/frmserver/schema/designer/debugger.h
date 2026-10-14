#ifndef __SCHEMA_DEBUGGER_H__
#define __SCHEMA_DEBUGGER_H__

// THE DESIGNER'S DEBUGGER'S STORE — the breakpoints the thin designer's debugger keeps (its engine's client, carried
// over: designer-thin/backend/debugger), in the base the configuration is in: the rows of its session's ibDebuggerClient,
// by the verbs that client writes them with (ibProtocolDebuggerCommand). The debugger itself — the runtimes, the stops,
// the watches — is the client's own. The designer's own.

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaDebugger : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData): debugging is the designer's.
	virtual bool AccessRight(const ibSession& session) const override;

	// The configuration's Guid (what a runtime is verified by) and Rows — its modules: Module (the address a runtime
	// stops at), Count (its lines); with Module — that module's Breakpoints (Line, from 0, as the editor's margin;
	// Condition).
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// The store's verbs (ibProtocolDebuggerCommand). Refused: no debugger, a breakpoint on a line the configuration does
	// not have (its reason).
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const override;
};

#endif
