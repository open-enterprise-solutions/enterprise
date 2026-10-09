////////////////////////////////////////////////////////////////////////////
//	Description : the assistant as a client of the protocol
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE PROTOCOL A RENDERER SPEAKS, SPOKEN THROUGH MCP — the same calls a browser or the wx renderer makes
// (ibClientHost::Call), made in memory as a client in the same process makes them: the method and its parameters
// as they are, no envelope. No side door into a form or a frame: what the assistant sees is exactly what a client
// is answered, so it reads the protocol the way a client will, and what it cannot do through it a client cannot
// do either.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/appData.h"

#include "frmserver/client/clientHost.h"

namespace {
using ibArg = ibMcpTool::ibMcpArgument;
const ibArg& ArgMethod() { static const ibArg a(wxT("method"), ibArg::Kind::Text, ibMcpText("The protocol's method: login, logout, frame, schema, execute, event, respond, fetch, activate, close, upload, download, open, command."), true); return a; }
const ibArg& ArgParams() { static const ibArg a(wxT("params"), ibArg::Kind::Node, ibMcpText("Its parameters, as the method takes them: {\"User\":\"...\",\"Password\":\"...\"} for login, {\"Client\":\"...\"} for frame, ..."), false); return a; }
} // namespace

class ibMcpToolClientCall : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("client_call"); }

	// It waits for a client's session — on that session's worker, never on this process's main thread.
	bool NeedsMainThread() const override { return false; }

	// A modal of this process stands in no client's way: the work goes to the clients' own sessions, and what a
	// client may do while it is asked something is the protocol's own rule (only respond).
	bool RunsWhileBusy() const override { return true; }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("working as a client of the base");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Work with the base AS A CLIENT does - the protocol a browser or the desktop renderer speaks, "
			"one call at a time: method and params; the answer is the method's result, a refusal is an error. "
			"Methods: login {User, Password, Mode: 1 runtime (default), 2 designer} -> "
			"Client (the id every other call names) and the frame; logout {Client}; frame {Client}; schema {Client, "
			"Schema: one the frame's Schemas lists - 1 All functions: Groups of the objects this person may open, each "
			"item by its Item; 2 Active users: Sessions and Locks} -> what it shows; schema {Client, Schema, Command, "
			"Args} -> the schema does it, the answer is the frame: All functions' Command 1 Open {Item, Type} opens the "
			"item as the navigation does (Type as execute's); execute {Client, Command: the metadata id of a "
			"catalog, document, report, data processor, common form or command, Type: 100 what the navigation panel "
			"opens - a catalog's or document's list, a report's or data processor's form, the common form, the "
			"command's code; 150 Create - a new object's form; 151 List; 152 Select}; "
			"event {Client, Control: a control id from the frame, Event, Args, Form: the Key of the form the control "
			"is in, when it is a start page's cell}; respond {Client, Id: the pending "
			"Request's id, Response}; fetch {Client, Control, Request, Form} - a table's rows {Direction 0/1/-1, Anchor, "
			"Count, Parent} or a spreadsheet's cells {Top, Left, Bottom, Right}; activate / close {Client, Tab}; "
			"upload {Client, Name, Data: base64, at most 1 MB} -> File (the id of a new temporary file of the client), "
			"then upload {Client, File, Data} for each next part; download {Client, File, Part} -> Name, Part, Parts, "
			"Data; open {Client, File} -> the frame, the file opened as a document of the template its name matches "
			"(the frame's Templates: Title, Mask). A client's files go when its session does. command {Client, Command} "
			"-> the frame: a command of the frame's Menu (1 Undo, 2 Redo, 3 Save, 4 Close) done on the active tab's "
			"document; one not Enabled is refused. The Menu is a tree of items: Separator; Command with Title, "
			"Shortcut (Ctrl/Alt/Shift+key) and Enabled - do it with command; Schema with Title - ask for it with "
			"schema; or a Title with items of its own. "
			"Event numbers: 1 Focus, 2 Command {Id[, Member]}, 3 Press [Member], 4 Input, 5 Change {Text}, "
			"6 Select, 7 Open, 8 Clear, 9 Toggle {Checked}, 10 Page {Page}, 11 Move {Position}, 12 Row {Row}, "
			"13 Sort {Column}, 14 Cell {Row, Col}, 15 Resize {Column, Width}. A table's cell is a field on a row: "
			"4/5/6/7/8 on a table name the cell {Row, Column[, Text]}; 7 Open with Row alone activates the row "
			"(double-click); 12 Row may carry Column; 11 Move on a table is {Column, Holder, Position}. A table's "
			"State.Edit {Row, Column} is the cell whose editor is open. Every answer but fetch's, a schema's tree, upload's and download's carries the frame: Title, Status, "
			"Schemas, Templates, Menu, "
			"Tabs (a Locked one - the start page - is first and cannot be closed), "
			"Messages, Request (when the person is asked something - Kind 1 Message -> respond {Button: 2 Yes, 8 No, "
			"4 OK, 16 Cancel}, 2 Choice "
			"-> respond {Id}, 3 Help), View (the active tab: a form - its Key and its controls as saved, each with a "
			"State of what it shows now; or the start page - Columns of cells, each with Title, Icon, Height and its "
			"form's View). While a Request is pending only respond is accepted.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgMethod(), ArgParams() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibClientHost* const host = appData != nullptr ? ibClientHost::Find(appData) : nullptr;
		if (host == nullptr) {
			refusal = ibMcpText("This process serves no clients of this base - the client protocol is served by the application server.");
			return false;
		}
		// A client in this same process: the call goes to the host as it is, no envelope, and its answer is the result.
		// A refusal says why by its number, as the wire does in the error's code.
		const ibDataNode* const given = params.FindChild(ArgParams().Name());
		ibProtocolRefusal why = ibProtocolRefusal::None;
		wxString error;
		if (host->Call(ibProtocolMethodFromName(ArgMethod().Text(params).utf8_str()), given != nullptr ? *given : ibDataNode(),
				result, why, error))
			return true;
		refusal = wxString::Format(wxT("refused %d: %s"), static_cast<s32>(why), error);
		return false;
	}
};

MCP_TOOL_REGISTER(ibMcpToolClientCall);
