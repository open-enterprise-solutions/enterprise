#ifndef __CLIENT_SCHEMA_H__
#define __CLIENT_SCHEMA_H__

// THE SCHEMAS A CLIENT ASKS FOR — the desktop's dialogs that are not a form (All functions, Active users, …), as the
// server holds them. On the server a dialog is not a window: it is a SCHEME of the data a client works with — what
// there is and how it is addressed — and the client decides how to show it (a window, a panel, a part of something
// of its own). What a person does in it the schema does itself, by its own commands (schema's Command).
//
// Which of them a client has is its application's: its frame's doc manager registers them — the shared ones the
// base ibDocManager's ctor, each mode's own its manager's ctor. They lie the same way: the shared
// ones here, each mode's in a folder of its own (runtime/, designer/) — All functions is the runtime's, since a
// designer loads no runtime.

#include <functional>

#include <wx/string.h>

#include "core/fileSystem/types.h"   // s32

#include "frmserver/frmserver.h"
#include "protocol/protocol.h"   // ibProtocolSchema, ibProtocolRefusal

class ibDataNode;
class ibSession;

// THE COMMON CLASS of a schema: what it shows, and what a person does in it — as a desktop dialog showed its list
// and handled what was picked there itself.
class FRMSERVER_API ibClientSchema {
public:

	virtual ~ibClientSchema() = default;

	// THE PERSON'S RIGHT TO IT — asked of the schema itself, on the client's session, before anything it shows or
	// does: a client asks for a schema by its number, past any menu that would have hidden it. The menu asks the
	// same question (ibClientFrame), so it offers what the schema would let through. A schema without a right of
	// its own is everybody's.
	virtual bool AccessRight(const ibSession& WXUNUSED(session)) const { return true; }

	// What the schema shows, into `result` — of the client's session, handed in: the configuration it works in, the
	// base it works in. Asked on that session: what a schema lists may depend on the person's rights.
	virtual void Build(const ibSession& session, ibDataNode& result) const = 0;

	// WHAT A PERSON DID IN IT — one of the schema's own commands (a number of its own on the wire), with its
	// arguments: the dialog's own handler on the desktop (a double click in All functions opens the item). Asked on
	// the client's session, as Build — what a schema acts on may depend on the person's rights — and answered with
	// the WORK that does it, which the client's work then runs: what it opens the frame shows, and what it asks the
	// person waits for the response. Empty with a refusal — why, and its text: the schema takes no such command, or
	// not with these arguments — a schema acts only on what it offered; nothing is done then.
	virtual std::function<void()> Command(const ibSession& session, s32 command,
		const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const;
};

#endif
