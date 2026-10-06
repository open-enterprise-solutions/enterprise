#ifndef __CLIENT_METHOD_H__
#define __CLIENT_METHOD_H__

#include <wx/string.h>

#include "backend/fileSystem/types.h"   // s32 — the protocol's numbers

// WHAT A CLIENT ASKS THE SERVER — the protocol's methods, all of them here. On the wire a method is a name, as
// JSON-RPC has it; in code it is a type: the door reads the name once (ibClientMethodFromName) and nothing past
// it compares strings. A name is never changed; a new method takes a new one.
//
// Every answer but logout's, fetch's, a schema's own and a file's is the client's frame (ibClientHost::Call).
enum class ibClientMethod {
	Unknown,    // a name the protocol does not have
	Login,      // {User, Password[, Mode][, Protocol]} → Client, Protocol, Features, and the frame (Mode: ibClientMode — 1 runtime, the default; 2 designer; Protocol: the newest version the client speaks, 1 when absent — answered with the one both speak, and the features the server offers at it)
	Logout,     // {Client}
	Frame,      // {Client} → the frame
	Schema,     // {Client, Schema[, Command[, Args]]} → what that schema shows; with a Command, the schema does it and the answer is the frame (Schema: ibClientSchemaKind — sfrontend/schema/clientSchema.h; Command: the schema's own); one the client's mode does not offer is refused
	Execute,    // {Client, Command[, Type]} — an object of the configuration, as the navigation executes it (Type: ibInterfaceCommandType)
	Event,      // {Client, Control, Event[, Args][, Form]} — Event: ibClientEvent; Form: the Key of the form drawn, when it is not the tab's own (a start page's cell)
	Respond,    // {Client, Id, Response} — Id: the pending request's
	Fetch,      // {Client, Control, Request[, Form]} → what the control's Fetch writes
	Activate,   // {Client, Tab} — Tab: the tab's id, its NodeId in the frame's Tabs
	Close,      // {Client, Tab}
	Upload,     // {Client, Name, Data} → File: a new temporary file, Data its first part; {Client, File, Data} → File: its next part (Data: base64, at most ibTempStorage::kPartSize bytes)
	Download,   // {Client, File, Part} → Name, Part, Parts, Data — the file's part, from 0, of how many there are
	Open,       // {Client, File} → the frame: the file opened by the template its name says (the frame's Templates)
	Command,    // {Client, Command} → the frame: a command of the frame's Menu done on the active tab's document (Command: ibDocCommand — sfrontend/docView/docCommand.h); one not enabled now is refused
	Presentation, // {Client, Value[, Format]} → Text: a value as a person reads it, the server formatting it (a reference's name is the configuration's) — Value: as ibValue::Serialize writes it (a field's state carries one); Format: the codes Format() takes (ND=10;NFD=2)
};

// WHY A CALL IS REFUSED — what a client branches on; the text beside it is for a person and may change. On the wire
// it is the JSON-RPC error's code — in the body, never an HTTP status: a WebSocket's messages and a call in process
// have none, and a refusal must arrive the same by every transport. (The envelope's own — a message that is not
// JSON-RPC, a method name the protocol does not have — is JSON-RPC's code, said by the port; a call in process has no
// envelope, and a method it does not have is NotFound.) The numbers are HTTP's by meaning, so a client's
// author reads them without a table; where two reasons share one, the method tells them apart (only login is refused
// for the password or the start). Never renumbered; a new reason takes a number of its own.
enum class ibClientRefusal : s32 {
	None         = 0,
	BadParameter = 400,   // a parameter missing or malformed: Data not base64, a part over the limit, a new file without a Name
	LoginRefused = 401,   // login: the user or the password
	NoSession    = 401,   // any other call: no such client, or its session is gone — log in anew
	NoRight      = 403,   // the person has no right to it
	StartRefused = 403,   // login: the application refused to start for this person
	NotFound     = 404,   // no such client's thing: schema, command, item, tab, file, part, request
	NotNow       = 409,   // offered, but not now: the active document does not take the command
	Pending      = 423,   // a question to the person is pending — respond to it first
	Failed       = 500,   // the server could not do it: a part not written, a frame not drawn
};

// The version of the protocol this server speaks — the newest; a client names its own at login, and the two go on
// with the older. Raised when a change needs it, never for a field added: an absent field is its default.
//   1  a patch is from the frame last sent;
//   2  …and its View from the view last sent for the tab active NOW, when the client was sent one; else from the
//      last frame's, as in 1. A client keeps each tab's view, puts the one of the tab the patch makes active in
//      place (keeping the view it holds when it has none for that tab), and applies the patch's View to it; a frame
//      sent whole begins the store again — back on a tab, it is sent what changed there, not the whole form.
constexpr s32 ibClientProtocolVersion = 2;

// The method a name means; Unknown for a name the protocol does not have.
inline ibClientMethod ibClientMethodFromName(const wxString& name)
{
	static const struct { const wxChar* name; ibClientMethod method; } s_methods[] = {
		{ wxT("login"),     ibClientMethod::Login },
		{ wxT("logout"),    ibClientMethod::Logout },
		{ wxT("frame"),     ibClientMethod::Frame },
		{ wxT("schema"),    ibClientMethod::Schema },
		{ wxT("execute"),   ibClientMethod::Execute },
		{ wxT("event"),     ibClientMethod::Event },
		{ wxT("respond"),   ibClientMethod::Respond },
		{ wxT("fetch"),     ibClientMethod::Fetch },
		{ wxT("activate"),  ibClientMethod::Activate },
		{ wxT("close"),     ibClientMethod::Close },
		{ wxT("upload"),    ibClientMethod::Upload },
		{ wxT("download"),  ibClientMethod::Download },
		{ wxT("open"),      ibClientMethod::Open },
		{ wxT("command"),   ibClientMethod::Command },
		{ wxT("presentation"), ibClientMethod::Presentation },
	};
	for (const auto& entry : s_methods) {
		if (name == entry.name)
			return entry.method;
	}
	return ibClientMethod::Unknown;
}

#endif
