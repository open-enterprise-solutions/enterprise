#ifndef __SCHEMA_DEBUGGER_H__
#define __SCHEMA_DEBUGGER_H__

// THE DESIGNER'S DEBUGGER'S STORE — the breakpoints of the designer's session's ibDebuggerClient, in the base the
// configuration is in, by the verbs that client writes them with (ibProtocol::ibSchema::ibDebugger::ibCommand): the
// committed lines — the editor's edits since are the client's to count (designer-thin/backend/debugger). The debugger
// itself — the runtimes, the stops, the watches — is that same engine's client, heard over its own connection
// (ibClientDebugger, ibProtocol::ibDebug).

#include "frmserver/schema/clientSchema.h"

class FRMSERVER_API ibSchemaDebugger : public ibClientSchema {
public:

	// The right to edit the configuration — the tree's (ibSchemaMetaData): debugging is the designer's.
	virtual bool AccessRight(const ibSession& session) const override;

	// The configuration's Guid (what a runtime is verified by) and Rows — its modules: Module (the address a runtime
	// stops at), Count (its lines); with Module — that module's Breakpoints (Line, from 0, as the editor's margin;
	// Condition).
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// The store's verbs (ibProtocol::ibSchema::ibDebugger::ibCommand). Refused: no debugger, a breakpoint on a line the configuration does
	// not have (its reason).
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, wxString& error) const override;
};

#endif
