#ifndef __PROTOCOL_PROTOCOL_H__
#define __PROTOCOL_PROTOCOL_H__

#include <cstring>   // std::strcmp — a method by its name

// THE PROTOCOL — the one place its names and numbers are written, for every side of it: the server's forms (frmserver),
// the thin client (frmclient) and, later, the web server, which tells a browser the same. Nothing here depends on
// anything — no wx, no engine type — so whoever speaks the protocol includes it and nothing comes with it. A number
// below is never renumbered; a new one takes the next. The wire itself is described in docs/public/client-protocol.md.
//
// THE WIRE'S NAMES — each written in the message that carries it: ibProtocol::ib<Part> — a method's call and answer, a
// request, a schema, a view, a form — and ib<Part>::ib<Sub>, a message inside it. A message lists every field it has,
// so a name two messages share is written in both. A name misspelt where it is used is not an error anywhere: the
// server skips a field it does not know and the client reads an absent one as its default, so a typo is a silence.
// Spelt here, it is a compile error — and so is a field taken from a message that has none of that name.
namespace ibProtocol {

	// The version of the protocol — the newest both sides here speak; a client names its own at login, and the two go on
	// with the older. Raised when a change needs it, never for a field added: an absent field is its default.
	//   1  a patch is from the frame last sent;
	//   2  …and its View from the view last sent for the tab active NOW, when the client was sent one; else from the
	//      last frame's, as in 1. A client keeps each tab's view, puts the one of the tab the patch makes active in
	//      place (keeping the view it holds when it has none for that tab), and applies the patch's View to it; a frame
	//      sent whole begins the store again — back on a tab, it is sent what changed there, not the whole form.
	//   3  …and a state event (ibEvent::IsStateEvent) its control's State does not list in Subscribed is held by the
	//      client, the latest of each kind per control, and carried by its next call, whatever it is, as Pending — which
	//      the server applies, in order, before the call itself.
	//   4  …and a reset fetch (Direction Reset) may name Before: the server reads up to that many rows ahead of the page
	//      too, puts them first, and answers Before — how many of the rows stand ahead of it. An answer without Before is
	//      a server below 4: the client reads them itself, a second fetch.
	//   5  …and a table's State may carry Rows with the Version: the first page, as fetch would give it (rows as children,
	//      End, Before, Pictures), so the frame that opens the tab need not be followed by a fetch. A server below 5, or
	//      one that omits Rows, is answered with fetch, as before.
	//   6  …and the designer's debugger rides the protocol (ibDebug): a second connection of the same client, bound to it by
	//      the key the first asks for, carries the debugger's commands, answered at once, and the server's `debug`
	//      notifications — a stop, the stack, the locals — sent as they happen, not as a frame.
	inline constexpr int Version = 6;

	// WHAT A CLIENT ASKS THE SERVER. On the wire a method is a name, as JSON-RPC has it; in code it is a type: the server
	// reads the name once (MethodFromName) and nothing past it compares strings. A name is never changed; a new method
	// takes a new one. Every answer but logout's, fetch's, a schema's own and a file's is the client's frame. A method's
	// fields are its message's below.
	enum class ibMethod {
		Unknown,        // a name the protocol does not have
		Login,          // ibLogin
		Users,          // ibLogin: the names the login window offers. Asked before the login, so it names no Client
		Logout,         // {Client}
		Frame,          // {Client} → the frame (ibFrame)
		Schema,         // ibSchema
		Execute,        // ibExecute
		Event,          // ibEvent
		Respond,        // ibRequest: the answer to the frame's Request
		Fetch,          // ibFetch
		Activate,       // {Client, Tab} — Tab: the tab's id, its node's id in the frame's Tabs
		Close,          // {Client, Tab}
		Upload,         // ibFile
		Download,       // ibFile
		Open,           // ibFile
		Command,        // {Client, Command} → the frame: a command of the frame's Menu done on the active tab's document
		                // (Command: ibFrame::ibCommand); one not enabled now is refused
		Presentation,   // ibPresentation
		Picture,        // ibPicture
		Debug,          // ibDebug — answered at once, never the frame
	};

	// The name a method goes by on the wire.
	inline const char* MethodName(ibMethod method)
	{
		switch (method) {
		case ibMethod::Unknown:      return "";
		case ibMethod::Login:        return "login";
		case ibMethod::Users:        return "users";
		case ibMethod::Logout:       return "logout";
		case ibMethod::Frame:        return "frame";
		case ibMethod::Schema:       return "schema";
		case ibMethod::Execute:      return "execute";
		case ibMethod::Event:        return "event";
		case ibMethod::Respond:      return "respond";
		case ibMethod::Fetch:        return "fetch";
		case ibMethod::Activate:     return "activate";
		case ibMethod::Close:        return "close";
		case ibMethod::Upload:       return "upload";
		case ibMethod::Download:     return "download";
		case ibMethod::Open:         return "open";
		case ibMethod::Command:      return "command";
		case ibMethod::Presentation: return "presentation";
		case ibMethod::Picture:      return "picture";
		case ibMethod::Debug:        return "debug";
		}
		return "";
	}

	// …and the method a name means; Unknown for a name the protocol does not have.
	inline ibMethod MethodFromName(const char* name)
	{
		for (int method = static_cast<int>(ibMethod::Login); method <= static_cast<int>(ibMethod::Debug); ++method) {
			if (std::strcmp(name, MethodName(static_cast<ibMethod>(method))) == 0)
				return static_cast<ibMethod>(method);
		}
		return ibMethod::Unknown;
	}

	// WHY A CALL IS REFUSED — what a client branches on; the text beside it is for a person and may change. On the wire
	// it is the JSON-RPC error's code — in the body, never an HTTP status: a WebSocket's messages and a call in process
	// have none, and a refusal must arrive the same by every transport. (The envelope's own — a message that is not
	// JSON-RPC, a method name the protocol does not have — is JSON-RPC's code, said by the port; a call in process has no
	// envelope, and a method it does not have is NotFound.) The numbers are HTTP's by meaning, so a client's author reads
	// them without a table; where two reasons share one, the method tells them apart (only login is refused for the
	// password or the start).
	enum class ibRefusal : int {
		None         = 0,
		BadParameter = 400,   // a parameter missing or malformed: Data not bytes, a part over the limit, a new file without a Name
		LoginRefused = 401,   // login: the user or the password
		NoSession    = 401,   // any other call: no such client, or its session is gone — log in anew
		NoRight      = 403,   // the person has no right to it
		StartRefused = 403,   // login: the application refused to start for this person
		NotFound     = 404,   // no such client's thing: schema, command, item, tab, file, part, request
		NotNow       = 409,   // offered, but not now: the active document does not take the command
		Failed       = 500,   // the server could not do it: a part not written, a frame not drawn
	};

	// THE ENVELOPE — JSON-RPC 2.0's own.
	namespace ibRpc {

		inline constexpr const char* Version = "jsonrpc";
		inline constexpr const char* Id      = "id";
		inline constexpr const char* Method  = "method";
		inline constexpr const char* Params  = "params";
		inline constexpr const char* Result  = "result";
		inline constexpr const char* Error   = "error";
		inline constexpr const char* Code    = "code";
		inline constexpr const char* Message = "message";
		// A packet's names of the types its nodes are of — a field per clsid, its digits the name and the type's name the
		// value (OES-RPC: a node carries its clsid, and only the answer knows the configuration's names).
		inline constexpr const char* Types   = "types";

		// What the server says unasked — a notification's method: a client's frame changed by itself (params: Client).
		inline constexpr const char* Changed = "changed";
		// …and the debugger's news, to the client's debug connection (params: Client, Event and its fields — ibDebug).
		inline constexpr const char* Debug   = "debug";

	}

	// A NODE, as the wire writes one — and a patch's own words.
	namespace ibNode {

		inline constexpr const char* Id            = "NodeId";
		inline constexpr const char* Type          = "NodeType";
		inline constexpr const char* Children      = "NodeChildren";
		inline constexpr const char* Raw           = "NodeRaw";
		inline constexpr const char* Removed       = "NodeRemoved";
		inline constexpr const char* RemovedIds    = "NodeRemovedIds";
		inline constexpr const char* Order         = "NodeOrder";
		inline constexpr const char* ChildrenWhole = "NodeChildrenWhole";

	}

	// THE LOGIN, and what every call names. login {User, Password[, Mode][, Protocol][, Features][, Read]} → Client,
	// Protocol, Features, and the frame. A thin runtime whose window is on also gets Token, once. Features on the request
	// names what the client accepts; FeatureResume there opts into the idempotency slot. {Token[, Protocol]} and no User,
	// only while that client is detached, → the previous Client, Protocol, Features, and no frame (the next frame {Since}
	// is the patch). A token login does not return Token again. Mode: ibMode, Runtime when absent; Protocol: the newest
	// version the client speaks, 1 when absent — answered with the one both speak, and the features the server offers at
	// it. users {} → Users.
	namespace ibLogin {

		inline constexpr const char* User          = "User";
		inline constexpr const char* Password      = "Password";
		inline constexpr const char* Mode          = "Mode";
		inline constexpr const char* Protocol      = "Protocol";
		inline constexpr const char* Features      = "Features";
		inline constexpr const char* FeatureResume = "resume";    // the same-process window only. A client names it in
		                                                          // Features to opt into the idempotency slot.
		inline constexpr const char* Read          = "Read";      // the schemas the client reads at its start ({Schema}),
		inline constexpr const char* Schema        = "Schema";    // answered beside the frame, each with its Schema
		inline constexpr const char* Client        = "Client";
		inline constexpr const char* Token         = "Token";     // the bearer, once, in the password login's answer. The
		                                                          // database keeps its hash (docs/public/client-protocol.md)
		inline constexpr const char* Users         = "Users";     // users' answer: the names the login window offers
		inline constexpr const char* Locale        = "Locale";    // the login's answer: the language the client's own
		                                                          // words are said in - the base's (core/localization/dictionary.h)
		// …and every call after it: {Client[, Since][, Pending]}.
		inline constexpr const char* Since         = "Since";
		inline constexpr const char* Pending       = "Pending";   // (protocol 3) the state events held since the last call,
		                                                          // in the order done — each as an event's params; applied first

		// THE MODE a client works in — login's Mode: which application's frame it gets. The desktop had a program per
		// mode; the server has one door, and a client's frame is made for one mode.
		enum class ibMode : int {
			Runtime  = 1,   // the application, as the runtime runs it
			Designer = 2,   // the configuration, as the designer edits it
		};

	}

	// THE FRAME — the client's window as the server draws it, and the answer's own beside it. Tabs, each a node (ibTab),
	// ActiveTab — the one shown, by its node's id, View — its form (ibForm) or its own view (ibView); Request — what the
	// server asks the person (ibRequest). command {Command[, Name]} — a command of its Menu done on the active tab's
	// document; activate and close {Tab}.
	namespace ibFrame {

		inline constexpr const char* Frame      = "Frame";
		inline constexpr const char* Patch      = "Patch";
		inline constexpr const char* Messages   = "Messages";   // ibMessage each
		inline constexpr const char* Clear      = "Clear";
		inline constexpr const char* Exit       = "Exit";       // the client is to go — Exit done
		inline constexpr const char* Raise      = "Raise";      // the client's window raised — a debugger left its loop
		inline constexpr const char* Title      = "Title";
		inline constexpr const char* Status     = "Status";
		inline constexpr const char* Menu       = "Menu";       // ibMenu each
		inline constexpr const char* Tabs       = "Tabs";       // ibTab each
		inline constexpr const char* ActiveTab  = "ActiveTab";
		inline constexpr const char* View       = "View";
		inline constexpr const char* Schemas    = "Schemas";    // the schemas the client's application offers
		inline constexpr const char* Templates  = "Templates";  // what the client opens and makes documents of — ibFile each
		inline constexpr const char* Request    = "Request";
		inline constexpr const char* MetaData   = "MetaData";   // the designer's: its configuration's tree, the node itself its
		                                                        // root row (ibSchema::ibMetaData)
		// An event of the answer: the rows of each object a door acted on (an edit, a new object, the one the tree selects
		// after a removal, a form editor's own), each as schema Properties writes them — the client shows them without
		// reading them.
		inline constexpr const char* Properties = "Properties";
		inline constexpr const char* Endpoint   = "Endpoint";   // the designer's assistant access: where it listens
		inline constexpr const char* Language   = "Language";
		inline constexpr const char* Tab        = "Tab";        // activate, close: the tab's id
		inline constexpr const char* Command    = "Command";    // command: ibCommand
		inline constexpr const char* Name       = "Name";       // …SaveAs: the file's name

		// A menu item: a command of a schema, done as `schema {Schema, Command}` does it; its children, a submenu.
		namespace ibMenu {

			inline constexpr const char* Title        = "Title";
			inline constexpr const char* Separator    = "Separator";
			inline constexpr const char* Schema       = "Schema";
			inline constexpr const char* Command      = "Command";        // …that schema's command
			inline constexpr const char* Shortcut     = "Shortcut";       // its default key — the user's own go over it, client-side
			inline constexpr const char* Enabled      = "Enabled";
			inline constexpr const char* StockPicture = "StockPicture";   // ibStockPicture — or Picture, by its id
			inline constexpr const char* Picture      = "Picture";

			// The picture a menu item or a tool is shown with — one of the client's stock pictures, by number.
			enum class ibStockPicture : int {
				None   = 0,
				New    = 1,
				Open   = 2,
				Save   = 3,
				SaveAs = 4,
				Close  = 5,
				Quit   = 6,
				Undo   = 7,
				Redo   = 8,
				Cut    = 9,
				Copy   = 10,
				Paste  = 11,
				Delete = 12,
				Find   = 13,
				Print  = 14,
			};

		}

		// A tab: its node's id is the tab's id; Kind — the view it is drawn with (ibView::ibKind; absent — a form).
		namespace ibTab {

			inline constexpr const char* Title    = "Title";
			inline constexpr const char* Icon     = "Icon";
			inline constexpr const char* Locked   = "Locked";
			inline constexpr const char* Kind     = "Kind";
			inline constexpr const char* Parent   = "Parent";
			inline constexpr const char* File     = "File";       // the file its document was saved as (SaveAs)
			inline constexpr const char* Commands = "Commands";   // the commands its document takes now
			inline constexpr const char* ReadOnly = "ReadOnly";   // what it shows takes no edit (its document's IsEditable)
			inline constexpr const char* Formats  = "Formats";    // what its document saves as — Title, Mask each
			inline constexpr const char* Mask     = "Mask";

		}

		// A message to the person.
		namespace ibMessage {

			inline constexpr const char* Level = "Level";   // ibLevel
			inline constexpr const char* Text  = "Text";

			// How a message is meant.
			enum class ibLevel : int {
				Information = 1,
				Warning     = 2,
				Error       = 3,
			};

		}

		// The commands — `command {Command}`, each done by the server; those whose subject is the client's (the field under
		// the focus) the server asks back of it (ibRequest::ibEdit). Named here the ones this client names itself: a
		// document's, which its doc manager does through the server (Undo, Redo, Save — a tab's Commands say which its
		// document takes now), Exit, which closing its window is, CloseAll, which its Window menu's Close All is, and
		// those an Edit request names.
		enum class ibCommand : int {
			Undo      = 1,
			Redo      = 2,
			Save      = 3,
			Close     = 4,
			Exit      = 6,
			Cut       = 7,
			Copy      = 8,
			Paste     = 9,
			Delete    = 10,
			SelectAll = 11,
			SaveAs    = 12,   // {Name}: saved as a file of that name — the tab's File, taken down to where the person keeps it
			CloseAll  = 13,   // every tab but the locked ones closed in one call — one kept keeps the rest
		};

	}

	// A FILE, going up and down. upload {Name, Data} → File: a new temporary file, Data its first part; {File, Data} →
	// File: its next part (Data: its bytes, a Binary). download {File, Part} → Name, Part, Parts, Data — the file's part, from 0, of
	// how many there are. open {File} → the frame: the file opened by the template its name says (the frame's
	// Templates — Title, Mask, Extension, OnlyOpen, Picture each); {Extension} → the frame: a new document of the
	// template that opens that extension.
	namespace ibFile {

		inline constexpr const char* File      = "File";        // an uploaded file's id
		inline constexpr const char* Name      = "Name";
		inline constexpr const char* Data      = "Data";        // a part going up or down, its bytes
		inline constexpr const char* Part      = "Part";        // download's: the part asked for, from 0
		inline constexpr const char* Parts     = "Parts";       // …and how many the file has
		inline constexpr const char* Extension = "Extension";   // a template's; open's, without a File: the template a new
		                                                        // document is made of
		inline constexpr const char* Title     = "Title";
		inline constexpr const char* Mask      = "Mask";        // a template's: what files it offers, Title beside it
		inline constexpr const char* OnlyOpen  = "OnlyOpen";    // a template's: it opens a file, and makes no new document
		inline constexpr const char* Picture   = "Picture";

	}

	// ⭐ A PICTURE IS NAMED BY ITS ID, EVERYWHERE — an answer carries the id alone, and the picture is loaded by this,
	// apart: picture {Pictures} → Pictures: the pictures of those ids, each {Id, Picture} (the PNG's bytes, a Binary) — a
	// backend one by its number (a registered picture, a class's own: its class id), a configuration's by its guid and its
	// version, `<guid>#<version>` (a changed picture is another version); several at once,
	// asked by a client that has not got them and kept by it. One there is none of comes back empty, not refused. A
	// picture with no id of its own (a file's) is not asked for: the field that names it holds its PNG instead.
	namespace ibPicture {

		inline constexpr const char* Pictures = "Pictures";
		inline constexpr const char* Id       = "Id";
		inline constexpr const char* Picture  = "Picture";

	}

	// presentation {Value[, Format]} → Text: a value as a person reads it, the server formatting it — Value: as the
	// server serialises a value (a field's state carries one).
	namespace ibPresentation {

		inline constexpr const char* Value  = "Value";
		inline constexpr const char* Format = "Format";   // the codes Format() takes
		inline constexpr const char* Text   = "Text";

	}

	// execute {Command[, Type]} — an object of the configuration, as the navigation executes it.
	namespace ibExecute {

		inline constexpr const char* Command = "Command";
		inline constexpr const char* Type    = "Type";

	}

	// fetch {Control, Request[, Form]} → what the control's Fetch writes: a table's rows, a sheet; {Tab, Request} — a
	// tab's own view's (ibView).
	namespace ibFetch {

		inline constexpr const char* Tab     = "Tab";
		inline constexpr const char* Control = "Control";
		inline constexpr const char* Form    = "Form";      // the Key of the form drawn, when it is not the tab's own
		inline constexpr const char* Request = "Request";

	}

	// THE DESIGNER'S DEBUGGER (protocol 6) — the server's, the one connected to the runtimes: the designer's windows are its
	// listeners over the wire, beside the assistant. On a connection of its own, so a step is not queued behind a frame:
	// debug {Command: Channel} on the client's connection → Channel, a key; debug {Command: Listen, Channel} on a second
	// connection of the same client binds that one — from then on it is answered the debugger's commands and is sent its
	// news, `debug {Client, Event, …}`, as they happen. A command is answered at once, never with the frame. Where a
	// breakpoint is kept is the debugger's store, a schema (ibSchema::ibDebugger).
	namespace ibDebug {

		inline constexpr const char* Command    = "Command";
		inline constexpr const char* Channel    = "Channel";      // Channel's answer, Listen's key
		inline constexpr const char* Event      = "Event";        // a notification's: ibEvent
		inline constexpr const char* Host       = "Host";
		inline constexpr const char* Port       = "Port";
		inline constexpr const char* Kill       = "Kill";         // Detach, Stop: the runtime is ended too
		inline constexpr const char* Rows       = "Rows";
		inline constexpr const char* Type       = "Type";         // a connection's (the runtime's ConnectionType), a value's type
		inline constexpr const char* Verified   = "Verified";     // a connection's: the runtime is of this configuration
		inline constexpr const char* Connected  = "Connected";    // a connection's: its socket is open; a session's news: a
		                                                          // runtime is still attached
		inline constexpr const char* User       = "User";
		inline constexpr const char* Computer   = "Computer";
		inline constexpr const char* Session    = "Session";      // the runtime's session the news is of
		inline constexpr const char* File       = "File";
		inline constexpr const char* Module     = "Module";
		inline constexpr const char* Line       = "Line";
		inline constexpr const char* Expression = "Expression";
		inline constexpr const char* Item       = "Item";         // a watched row — the asker's own number, said back
		inline constexpr const char* Asker      = "Asker";        // the listener that asked: its answer is its own
		inline constexpr const char* Level      = "Level";        // Stack: the call the locals are read in
		inline constexpr const char* Name       = "Name";
		inline constexpr const char* Value      = "Value";
		inline constexpr const char* Attributes = "Attributes";   // a value that unfolds
		inline constexpr const char* Text       = "Text";         // ToolTip's value, Message's line

		// What a client asks of the debugger — numbered of their own, as a schema's commands.
		enum class ibCommand : int {
			Channel          = 1,    // → Channel
			Listen           = 2,    // {Channel}
			Search           = 3,    // the runtimes of this machine looked for: a debug run just started waits for one
			Connections      = 4,    // → Rows {Host, Port, Type, Verified, Connected, User, Computer}
			Attach           = 5,    // {Host, Port}
			Detach           = 6,    // {Host, Port, Kill}
			Continue         = 7,
			StepOver         = 8,
			StepInto         = 9,
			StepOut          = 10,
			Pause            = 11,
			Stop             = 12,   // {Kill}
			ToolTip          = 13,   // {File, Module, Expression} → news ToolTip
			AddExpression    = 14,   // {Expression, Item, Asker} → news Variable
			ExpandExpression = 15,   // {Expression, Item, Asker} → news Expanded
			RemoveExpression = 16,   // {Item}
			Stack            = 17,   // {Level}
		};

		// …and what it tells — the listener's own words (ibDebuggerClientBridge).
		enum class ibEvent : int {
			SessionStart = 1,    // {Connected}
			SessionEnd   = 2,    // {Connected}
			EnterLoop    = 3,    // {Session, File, Module, Line}
			LeaveLoop    = 4,    // the same
			Message      = 5,    // {Session, File, Module, Line, Text}
			ToolTip      = 6,    // {Session, File, Module, Expression, Text}
			Stack        = 7,    // Rows {Module, Name — the call as the runtime names it, Line}
			Locals       = 8,    // Rows {Name, Value, Type, Attributes}
			Variable     = 9,    // {Item, Asker}, Rows {Item, Name, Value, Type, Attributes}
			Expanded     = 10,   // the same
		};

	}

	// WHAT A PERSON DID WITH A CONTROL of a view — event {Control, Event[, Args][, Form]}, the control named by its
	// ControlId; Form: the Key of the form drawn, when it is not the tab's own (a start page's cell). {Tab, Event, Args} —
	// a tab's own view's (ibView), its Args that view's. A TABLE'S CELL IS A FIELD ON A ROW: the field's events name the
	// cell — Row (a handle) and Column (the column's control id) — and mean what they mean on a form's field.
	namespace ibEvent {

		inline constexpr const char* Tab      = "Tab";
		inline constexpr const char* Control  = "Control";
		inline constexpr const char* Form     = "Form";
		inline constexpr const char* Event    = "Event";      // ibKind
		inline constexpr const char* Args     = "Args";
		// …the Args a control's event carries:
		inline constexpr const char* Id       = "Id";         // Command, Action: the command's
		inline constexpr const char* Member   = "Member";     // Command, Press: one command of a group, by its place in it
		inline constexpr const char* Text     = "Text";       // Change
		inline constexpr const char* Value    = "Value";      // Change: the value with its type, when the field holds one
		inline constexpr const char* Checked  = "Checked";    // Toggle
		inline constexpr const char* Page     = "Page";       // Page: the page picked, by its control id
		inline constexpr const char* Position = "Position";   // Move: the place it is dragged to
		inline constexpr const char* Holder   = "Holder";     // Move: the group a column now stands in — 0, the table
		inline constexpr const char* Row      = "Row";        // a table's row, by its handle; a spreadsheet's, from 1
		inline constexpr const char* Column   = "Column";     // a table's column, by its control id
		inline constexpr const char* Col      = "Col";        // a spreadsheet's column, from 1
		inline constexpr const char* Width    = "Width";      // Resize: the width it asks for

		enum class ibKind : int {
			Focus   = 1,    // the control became the active one
			Command = 2,    // an entry of its command bar pressed (Id; Member — one command of a group)
			Press   = 3,    // a button pressed (Member — one command of its group, by its place among the group's commands)
			Input   = 4,    // typing started in a field: nothing committed, the object is modified; in a cell — its editor opened
			Change  = 5,    // a field's text committed (Text) — a cell's, and its edit is over
			Select  = 6,    // a field's Select button
			Open    = 7,    // the value opened: a field's Open button, a caption's link; a table's row (Row, no Column) —
			                // double-click / Enter
			Clear   = 8,    // a field's Clear button
			Toggle  = 9,    // a check box toggled (Checked)
			Page    = 10,   // a notebook page picked (Page: its control id)
			Move    = 11,   // dragged to another place: a notebook page's tab (Position); a table's column (Column, Holder:
			                // the group or 0, Position)
			Row     = 12,   // a table's cursor moved (Row: its handle; Column — across to that column)
			Sort    = 13,   // a table's column header clicked (Column: its control id)
			Cell    = 14,   // a spreadsheet cell clicked (Row, Col)
			Resize  = 15,   // a table column's edge dragged (Column, Width: the width it asks for)
			Action  = 16,   // one of the control's own commands, picked in its context menu (Id: the command's — a table's
			                // standard commands, its State's ContextMenu)
		};

		// ⭐ AN EVENT THAT SAYS WHERE THE PERSON IS — the control in focus, the table's cursor, typing begun, a column's place
		// and width. Nothing on the server has to run for it unless the server is subscribed to it (the State's
		// Subscribed), so it waits for the client's next call (protocol 3, Pending): a walk down a list with the arrow keys
		// is one record, not a call a row. Every other event is something done, and goes at once.
		inline bool IsStateEvent(ibKind event)
		{
			switch (event) {
			case ibKind::Focus:
			case ibKind::Input:
			case ibKind::Row:
			case ibKind::Move:
			case ibKind::Resize:
				return true;
			default:
				return false;
			}
		}

	}

	// WHAT THE SERVER ASKS THE PERSON — the frame's Request, by its Kind; answered with respond {Id, Response}, the
	// Response that kind's message's.
	namespace ibRequest {

		inline constexpr const char* Id       = "Id";
		inline constexpr const char* Kind     = "Kind";       // ibKind
		inline constexpr const char* Response = "Response";

		enum class ibKind : int {
			Message          = 1,    // ibMessage
			Choice           = 2,    // ibChoice
			Edit             = 4,    // ibEdit
			File             = 5,    // ibFile
			Generation       = 6,    // ibGeneration
			ViewMode         = 7,    // ibViewMode
			SavedSettings    = 8,    // ibSavedSettings
			Menu             = 9,    // ibMenu
			FormEditor       = 10,   // ibFormEditor
			ListSettings     = 11,   // ibListSettings
			SimpleChoice     = 12,   // ibSimpleChoice
			ComposerSettings = 13,   // ibComposerSettings
			Error            = 14,   // ibError
			Print            = 15,   // ibPrint
			Schedule         = 16,   // ibSchedule
			ApplyChange      = 17,   // ibApplyChange
			FormType         = 18,   // ibFormType
			PredefinedSelector = 19, // ibPredefinedSelector
		};

		// Text, Caption, Style (wx's button flags) → Button (wx's code: 2 Yes, 8 No, 4 OK, 16 Cancel).
		namespace ibMessage {

			inline constexpr const char* Text    = "Text";
			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Style   = "Style";
			inline constexpr const char* Button  = "Button";

		}

		// Caption and children Id, Caption, Picture, Selected → Id — one of those offered; none — cancelled. With Multiple →
		// Ids — those of them ticked.
		namespace ibChoice {

			inline constexpr const char* Caption  = "Caption";
			inline constexpr const char* Id       = "Id";
			inline constexpr const char* Picture  = "Picture";
			inline constexpr const char* Selected = "Selected";
			inline constexpr const char* Multiple = "Multiple";   // several may be chosen — answered with Ids
			inline constexpr const char* Ids      = "Ids";

		}

		// Command (ibFrame::ibCommand: Cut … SelectAll) — done to the field under the focus → nothing.
		namespace ibEdit {

			inline constexpr const char* Command = "Command";

		}

		// Caption and children Title, Mask — a file of the person's chosen, uploaded → File; none — cancelled. With File —
		// that one of the server's taken down where the person chose → Done; none — cancelled.
		namespace ibFile {

			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Title   = "Title";
			inline constexpr const char* Mask    = "Mask";
			inline constexpr const char* File    = "File";
			inline constexpr const char* Done    = "Done";

		}

		// The desktop's ibDialogGeneration: Picture (the window's) and children Id, Caption, Picture — the objects this one
		// generates → Id — the one to base a new one on; none — cancelled.
		namespace ibGeneration {

			inline constexpr const char* Picture = "Picture";
			inline constexpr const char* Id      = "Id";
			inline constexpr const char* Caption = "Caption";

		}

		// The desktop's wxTableViewModeDialog: Picture (the window's), Mode (ibForm::ibTable::ibViewMode) — the one in force
		// → Mode — the one chosen; none — cancelled.
		namespace ibViewMode {

			inline constexpr const char* Picture = "Picture";
			inline constexpr const char* Mode    = "Mode";

		}

		// The desktop's ibDialogSavedSettings: Mode, Key, Default and children Id, Caption — the shelf → Act, Id, Name —
		// done by the server, which asks again with the shelf as it now stands and Done; none — closed.
		namespace ibSavedSettings {

			inline constexpr const char* Mode    = "Mode";      // ibMode
			inline constexpr const char* Key     = "Key";
			inline constexpr const char* Default = "Default";   // the entry restored on open
			inline constexpr const char* Id      = "Id";
			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Act     = "Act";       // ibAct: what was done to the shelf
			inline constexpr const char* Name    = "Name";
			inline constexpr const char* Done    = "Done";      // the shelf asked again: how the act before went

			// What the window is opened for.
			enum class ibMode : int {
				Save    = 0,
				Restore = 1,
			};

			// What the window did to the shelf.
			enum class ibAct : int {
				Save    = 1,   // what is in force put into Id (none — a new entry), named Name
				Restore = 2,   // Id put in force
				Default = 3,   // Id restored on open (none — nothing)
				Rename  = 4,   // Id named Name
				Remove  = 5,   // Id dropped
			};

		}

		// The desktop's popup menu at the cursor: children Id, Caption, Checked, Enabled (false — greyed) and their own
		// children — a submenu → Id — the item picked; none — dismissed.
		namespace ibMenu {

			inline constexpr const char* Id      = "Id";
			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Checked = "Checked";
			inline constexpr const char* Enabled = "Enabled";

		}

		// The desktop's ibDialogFormEditor over the active tab's form, its controls as the frame has them: Picture,
		// HasSetting, Result (the act before's), Classes (Name, Picture) → Act — Apply with Controls (Control, Position and
		// the allowed properties — a saved form setting's shape) — done by the server, which asks again; none — closed.
		namespace ibFormEditor {

			inline constexpr const char* Picture    = "Picture";
			inline constexpr const char* HasSetting = "HasSetting";   // a setting of the person's is saved for this form
			inline constexpr const char* Result     = "Result";       // ibResult: how a save or a reset went
			inline constexpr const char* Classes    = "Classes";      // the classes a control may be made of
			inline constexpr const char* Name       = "Name";
			inline constexpr const char* Act        = "Act";          // ibAct
			inline constexpr const char* Controls   = "Controls";
			inline constexpr const char* Control    = "Control";
			inline constexpr const char* Position   = "Position";     // where a control stands among its siblings

			// What the editor did.
			enum class ibAct : int {
				Apply = 1,   // the form as Controls say it stands, put on
				Save  = 2,   // the person's arrangement saved for this form
				Reset = 3,   // …and dropped: the form comes back as the author made it on the next open
			};

			// How a save or a reset of a form's setting went (the server's ibFormSettingsResult).
			enum class ibResult : int {
				Ok        = 0,
				NoStorage = 1,   // no settings in this session
				NoAddress = 2,   // a form generated from its data — nothing to keep a setting under
				Refused   = 3,
			};

		}

		// The desktop's ibDialogListSettings over a table's model: Settings — the setting in force, as
		// ibSettingsDescriptionMemory writes it — Fields, its columns (children Name, Presentation, Id, Type — as
		// ibTypeDescriptionMemory writes it — Available, Picture), and References: which types are references and to what
		// (children Type — its class id as text — and Targets, the metaobjects) → Settings — the setting edited, which the
		// server puts on and asks no more; Error beside the setting when it refused one; none — cancelled. On the way: an
		// Act (ibAct), asked again with what it brought.
		namespace ibListSettings {

			inline constexpr const char* Settings     = "Settings";
			inline constexpr const char* Fields       = "Fields";
			inline constexpr const char* Name         = "Name";
			inline constexpr const char* Presentation = "Presentation";   // how a field reads — its synonym; none: its name.
			                                                              // A value's: how it reads, as the server's GetString
			inline constexpr const char* Id           = "Id";
			inline constexpr const char* Type         = "Type";
			inline constexpr const char* Available    = "Available";      // a field the options of the base leave to it
			inline constexpr const char* Picture      = "Picture";
			inline constexpr const char* References   = "References";
			inline constexpr const char* Targets      = "Targets";        // the metaobjects a reference type points at
			inline constexpr const char* Expanded     = "Expanded";       // the fields of the targets a reference field opened on
			inline constexpr const char* Error        = "Error";          // why the server refused the setting it was given
			inline constexpr const char* Act          = "Act";            // ibAct
			inline constexpr const char* Choice       = "Choice";         // which choice a value answers — a number the client
			                                                              // took it by
			inline constexpr const char* Chosen       = "Chosen";         // a value chosen on the server's choice form: Value,
			                                                              // Choice
			inline constexpr const char* Value        = "Value";

			// What the window asked on the way.
			enum class ibAct : int {
				Expand = 1,   // a reference field opened: the fields of its Targets — asked again with Expanded, their fields
				              // (children as Fields') and their References
				Choose = 2,   // a value of a type of the server's chosen (Type — its class id as text, Value — the one held,
				              // Choice — the number the client asks it under): its quick choice, or its choice form — a tab
				              // like any other, in the designer its declared values (ibPredefinedSelector); what is chosen
				              // comes back in the window's question as Chosen (Choice, and Value with its Presentation
				              // beside {t, v}), whenever it is
			};

		}

		// The desktop's ibTypeControlFactory::SimpleChoice under the field pressed: Value — its value with its type ({t, v}),
		// a number (the calculator) or a date (the calendar) → Value — the one chosen; none — dismissed.
		namespace ibSimpleChoice {

			inline constexpr const char* Value = "Value";

		}

		// The desktop's ibDialogComposerSettings over a report's model: as ibListSettings, and Composition — the composition
		// it arranges, as ibCompositionDescriptionMemory writes it; Fields — those its query offers (Presentation — the
		// caption its source gives one) → as ibListSettings; the report waits for Compose
		// (ibForm::ibSpreadsheet::ibComposeState::Outdated). On the way: the same acts (ibListSettings::ibAct).
		namespace ibComposerSettings {

			inline constexpr const char* Composition = "Composition";

		}

		// The desktop's ibDialogError over a script's failure: Picture (the window's), Text — the error with its call stack
		// → Act; none — closed, as Close window.
		namespace ibError {

			inline constexpr const char* Picture = "Picture";
			inline constexpr const char* Text    = "Text";
			inline constexpr const char* Act     = "Act";   // ibAct

			// What the person chose. The answers are CUMULATIVE, as the desktop's were: each one does what the one before
			// it does, and more.
			enum class ibAct : int {
				CloseWindow  = 1,   // the error kept in the messages
				GotoDesigner = 2,   // …and sent to the designer debugging this base
				CloseProgram = 3,   // …and the client goes
			};

		}

		// The desktop's PrintSpreadsheetDocument over a script's sheet: Sheet — as a spreadsheet's fetch writes it → Done —
		// whether it was printed (not: the print dialog cancelled, or the printer failed).
		namespace ibPrint {

			inline constexpr const char* Sheet = "Sheet";
			inline constexpr const char* Done  = "Done";

		}

		// The desktop's ibDialogJobSchedule over a form's schedule: Schedule — as ibJobScheduleDescriptionMemory writes it →
		// Schedule — the one edited (the form is modified); none — cancelled.
		namespace ibSchedule {

			inline constexpr const char* Schedule = "Schedule";

		}

		// The desktop's ibDialogApplyChange over the restructure's ledger, the base held until it is answered: Picture (the
		// window's) and children Level (ibRestructure: 0 info, 1 warning, 2 error), Text → Done — applied; none —
		// cancelled, everything rolled back.
		namespace ibApplyChange {

			inline constexpr const char* Picture = "Picture";
			inline constexpr const char* Level   = "Level";
			inline constexpr const char* Text    = "Text";
			inline constexpr const char* Done    = "Done";

		}

		// The desktop's ibDialogSelectTypeForm over a form just made, asked inside the Create that made it: Caption, Name,
		// Synonym, Comment — the form's — and children Id, Caption, Name — the kinds its owner offers, each with the name
		// the form takes with it → Id, Name, Synonym, Comment — the kind chosen and what the person typed; none —
		// cancelled, and the form taken away again before the Create answers.
		namespace ibFormType {

			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Name    = "Name";
			inline constexpr const char* Synonym = "Synonym";
			inline constexpr const char* Comment = "Comment";
			inline constexpr const char* Id      = "Id";

		}

		// The desktop's ibShowPredefinedSelector, its second question — which DECLARED value, while a configuration is
		// written and there is no data: Types — a branch per admitted reference (Title), its values as children (Name,
		// Id), each holding what is declared under it (a predefined folder's items) → Id — the value chosen; none —
		// closed. The first question, which type, is the type list every road asks (ibChoice).
		namespace ibPredefinedSelector {

			inline constexpr const char* Types = "Types";
			inline constexpr const char* Title = "Title";
			inline constexpr const char* Name  = "Name";   // EmptyRef, an enumeration member, a predefined element
			inline constexpr const char* Id    = "Id";     // the server's number for the value

		}

	}

	// A DIALOG OF THE DESKTOP HELD AS DATA — `schema {Schema}` shows it, `schema {Schema, Command, Args}` does what was
	// picked in it; one the client's mode does not offer is refused, and with a Command the answer is the frame. Which
	// of them a client's application offers is the frame's Schemas.
	namespace ibSchema {

		inline constexpr const char* Schema  = "Schema";    // ibKind
		inline constexpr const char* Command = "Command";   // ibCommand, or the schema's own
		inline constexpr const char* Args    = "Args";

		enum class ibKind : int {
			AllFunctions = 1,    // ibAllFunctions
			ActiveUser   = 2,    // ibActiveUser
			Sections     = 3,    // ibSections
			About        = 4,    // ibAbout
			AuditLog     = 5,    // both modes': the registration journal — shows nothing; command Open opens its tab
			                     // (ibView::ibKind::AuditLog)
			Debugging    = 6,    // ibDebugging
			McpServer    = 7,    // ibMcpServer
			UserList     = 8,    // ibUserList
			MetaData     = 9,    // ibMetaData
			Properties   = 10,   // ibProperties
			Languages    = 11,   // ibLanguages
			Predefined   = 12,   // ibPredefined
			ObjectText   = 13,   // ibObjectText
			HomePage     = 14,   // ibHomePage
			Debugger     = 15,   // ibDebugger
			Database     = 16,   // the designer's base as a whole (Administration → Restore, Dump, Clear database) — shows
			                     // nothing; commands Load — restored from a file the person is asked for
			                     // (ibRequest::ibFile), every session of the base closed after; Save — dumped, and taken
			                     // down where the person chooses; Clear — asked first. Each said to the person by the server
			Assistant    = 17,   // the designer's assistant window (Administration) — shows nothing; command Open opens its
			                     // tab (ibView::ibKind::Assistant). Allowed while the base's assistant access is running
			ConfigCompare = 18,  // ibConfigCompare — the walk of two configurations; command Merge applies the ops chosen
		};

		// A schema's own commands — `schema {Schema, Command, Args}`. All functions and Sections take the one: Open
		// {Item, Type} — an item they show, opened as the navigation opens it. Debugging takes Start and Stop; McpServer
		// takes them and Save; Active users, the designer's, Kick, Reload and Release; the users Save and Remove; the
		// designer's inspector (Properties) Save; its predefined values editor Create, Set and Remove; its texts
		// (ObjectText) Save, and so the home page (HomePage); the base (Database) Load, Save and Clear; the assistant window
		// (Assistant) Open; ConfigCompare Merge. The designer's tree has its own (ibMetaData::ibCommand), and so its debugger's store.
		enum class ibCommand : int {
			Open    = 1,
			Start   = 2,
			Stop    = 3,
			Save    = 4,
			Kick    = 5,
			Reload  = 6,
			Release = 7,
			Remove  = 8,
			Create  = 9,
			Set     = 10,
			Load    = 11,
			Clear   = 12,
			Merge   = 13,  // ConfigCompare: apply selected Add/Delete/Replace ops
		};

		// THE DESIGNER'S CONFIGURATION COMPARE — Build {Mode, File?, File2?} walks two trees (ibMetaDiffWalker) →
		// Records (Status, Parent, Depth, Group, Name, Picture, Property, Left, Right, LeftId, RightId). Mode: File —
		// active vs uploaded File; Database — active vs its baseline; TwoFiles — File vs File2. Command Merge {Mode,
		// File?, File2?, Pull, Ops: [{Op, LeftId, RightId, ParentId}]} — CopyMetaObject / Remove / Paste on the server;
		// ParentId — what an Add goes under on the side written (the desktop's FindTargetParent; none: the root). Push
		// (Pull false) writes the right side back into File or File2.
		namespace ibConfigCompare {

			inline constexpr const char* Mode          = "Mode";
			inline constexpr const char* File          = "File";
			inline constexpr const char* File2         = "File2";
			inline constexpr const char* Records       = "Records";
			inline constexpr const char* Status        = "Status";
			inline constexpr const char* Parent        = "Parent";
			inline constexpr const char* Depth         = "Depth";
			inline constexpr const char* Group         = "Group";
			inline constexpr const char* Name          = "Name";
			inline constexpr const char* Picture       = "Picture";
			inline constexpr const char* Property      = "Property";
			inline constexpr const char* Left          = "Left";
			inline constexpr const char* Right         = "Right";
			inline constexpr const char* LeftId        = "LeftId";
			inline constexpr const char* RightId       = "RightId";
			inline constexpr const char* Pull          = "Pull";
			inline constexpr const char* Ops           = "Ops";
			inline constexpr const char* Op            = "Op";
			inline constexpr const char* ParentId      = "ParentId";

			enum class ibMode : int {
				File     = 1,   // active configuration vs uploaded File
				Database = 2,   // active vs its database baseline
				TwoFiles = 3,   // File vs File2
			};

			enum class ibOp : int {
				Add     = 1,
				Delete  = 2,
				Replace = 3,
			};

		}

		// The runtime's: Groups (Title, Icon) of items (Item, Title, Icon); command Open {Item, Type}.
		namespace ibAllFunctions {

			inline constexpr const char* Groups = "Groups";
			inline constexpr const char* Title  = "Title";
			inline constexpr const char* Icon   = "Icon";
			inline constexpr const char* Item   = "Item";
			inline constexpr const char* Type   = "Type";

		}

		// Both modes': Sessions (User, Application, Type, Started, Computer, Session, Activity, Picture) and Locks
		// (Namespace, Key, Mode, User, Acquired, Lock); the designer's commands Kick {Session} — that session closed,
		// Reload {Session} — its clients reloaded, Release {Lock} — that lock released by force.
		namespace ibActiveUser {

			inline constexpr const char* Icon        = "Icon";
			inline constexpr const char* Sessions    = "Sessions";
			inline constexpr const char* User        = "User";
			inline constexpr const char* Application = "Application";
			inline constexpr const char* Type        = "Type";
			inline constexpr const char* Picture     = "Picture";
			inline constexpr const char* Started     = "Started";
			inline constexpr const char* Computer    = "Computer";
			inline constexpr const char* Session     = "Session";
			inline constexpr const char* Activity    = "Activity";
			inline constexpr const char* Locks       = "Locks";
			inline constexpr const char* Namespace   = "Namespace";
			inline constexpr const char* Key         = "Key";
			inline constexpr const char* Mode        = "Mode";
			inline constexpr const char* Acquired    = "Acquired";
			inline constexpr const char* Lock        = "Lock";

		}

		// The runtime's section panel: Sections (ibNode::Id, Title, Picture) with their Blocks (Column, Title, Picture,
		// Tooltip) of items (Item, Title, Icon, Type); command Open {Item, Type}.
		namespace ibSections {

			inline constexpr const char* Sections = "Sections";
			inline constexpr const char* Title    = "Title";
			inline constexpr const char* Picture  = "Picture";
			inline constexpr const char* Blocks   = "Blocks";
			inline constexpr const char* Column   = "Column";    // ibColumn
			inline constexpr const char* Tooltip  = "Tooltip";
			inline constexpr const char* Item     = "Item";
			inline constexpr const char* Icon     = "Icon";
			inline constexpr const char* Type     = "Type";

			// The column a block stands in.
			enum class ibColumn : int {
				Left  = 1,
				Right = 2,
			};

		}

		// Both modes': Build, Picture, Database, Application, User, Locale and Plugins (Name, Version).
		namespace ibAbout {

			inline constexpr const char* Build       = "Build";
			inline constexpr const char* Picture     = "Picture";
			inline constexpr const char* Database    = "Database";
			inline constexpr const char* Application = "Application";
			inline constexpr const char* User        = "User";
			inline constexpr const char* Locale      = "Locale";
			inline constexpr const char* Plugins     = "Plugins";
			inline constexpr const char* Name        = "Name";
			inline constexpr const char* Version     = "Version";

		}

		// Debugging in the current session — the runtime's options: whether it is on, and whether it may be switched;
		// commands Start and Stop.
		namespace ibDebugging {

			inline constexpr const char* Debugging = "Debugging";   // the session's process listens for the designer
			inline constexpr const char* Allowed   = "Allowed";     // …and may be switched now: not on the application server,
			                                                        // not while a start waits for the designer

		}

		// The designer's assistant access, the person's: Enabled, Address, Port, Token — where it listens, whether it does,
		// and what a person hands their tool; Running, Endpoint, Allowed. Commands Start {Enabled, Address, Port, Token} —
		// started on those, Stop, Save {…} — kept as the person's.
		namespace ibMcpServer {

			inline constexpr const char* Enabled  = "Enabled";
			inline constexpr const char* Address  = "Address";
			inline constexpr const char* Port     = "Port";
			inline constexpr const char* Token    = "Token";
			inline constexpr const char* Running  = "Running";
			inline constexpr const char* Endpoint = "Endpoint";   // empty while it is not running
			inline constexpr const char* Allowed  = "Allowed";

		}

		// The designer's users of the base — a user's record as the engine's ibUserInfo holds it: Users (Guid, Name,
		// FullName), and the configuration's Roles (Guid, Name, Title, Mode) a user's record is chosen from (its languages —
		// ibLanguages); `Args: {Guid}` — that one's User {Guid, Name, FullName, Password — whether one is stored (never the
		// stored one), Language — its guid, Roles (Guid)}; commands Save {Guid, Name, FullName, Language, Roles (Guid)[,
		// Password — a new one, hashed by the server; absent: the stored one kept]}, Remove {Guid}.
		namespace ibUserList {

			inline constexpr const char* Users    = "Users";
			inline constexpr const char* User     = "User";
			inline constexpr const char* Guid     = "Guid";
			inline constexpr const char* Name     = "Name";
			inline constexpr const char* FullName = "FullName";
			inline constexpr const char* Password = "Password";
			inline constexpr const char* Language = "Language";
			inline constexpr const char* Roles    = "Roles";
			inline constexpr const char* Title    = "Title";
			inline constexpr const char* Mode     = "Mode";

		}

		// THE DESIGNER'S CONFIGURATION TREE — shows nothing: its rows ride the frame (ibFrame::MetaData), each row
		// ibNode::Id — its object's id, or below 0 a group row's place among its siblings; Title, Picture, Group — what New
		// makes under it, Type, Synonym, Position, Guid — its object's; a language's Code, a role's Mode; Menu. Its
		// commands are its own (ibCommand), each with Tab in that tab's tree — an external file's (ibView::ibKind
		// DataProcessor, Report) or a configuration browsed read-only (Configuration).
		namespace ibMetaData {

			inline constexpr const char* Title      = "Title";
			inline constexpr const char* Picture    = "Picture";
			inline constexpr const char* Group      = "Group";
			inline constexpr const char* Type       = "Type";
			inline constexpr const char* Synonym    = "Synonym";      // its object's synonym, when it has one
			inline constexpr const char* Position   = "Position";
			inline constexpr const char* Guid       = "Guid";
			inline constexpr const char* Code       = "Code";
			inline constexpr const char* Mode       = "Mode";
			inline constexpr const char* FormType   = "FormType";     // a form's row: its kind (the owner's form type)
			// …the root row's: the configuration's state, as the engine answers it — Modified (ibMetaData::IsModified),
			// Edited (IsEdited: somebody edited it), ConfigSave (ibMetaDataConfigurationBase::IsConfigSave: the database
			// holds it), Syntax — the code style its modules are written in.
			inline constexpr const char* Modified   = "Modified";
			inline constexpr const char* Edited     = "Edited";
			inline constexpr const char* ConfigSave = "ConfigSave";
			inline constexpr const char* Syntax     = "Syntax";
			// …the root row's: the kinds whose objects open an editor (Open) — children Type; none: ask Open for any.
			inline constexpr const char* Editors    = "Editors";
			// …an external file's root row's: the header above its tree — its object's rows, as ibProperties' — and its
			// comment.
			inline constexpr const char* Properties = "Properties";
			inline constexpr const char* Comment    = "Comment";
			inline constexpr const char* DefaultFormObject = "DefaultFormObject";   // …the default form's id (none: wxNOT_FOUND)
			inline constexpr const char* DefaultComposer   = "DefaultComposer";     // …a report's default composer's id
			// …a row's Menu: what its object offers to open (the engine's ibValueMetaObject::CollectContextMenu) — its
			// children the items: Kind (ibMetaMenuKind: 0 a module, 1 an object, 2 a modal editor), Name, Title, Object and
			// Type, Picture (none: the object's class's), Command (the editor's number, ProcessCommand; none: an object
			// opened).
			inline constexpr const char* Menu       = "Menu";
			inline constexpr const char* Standard   = "Standard";     // false — the tree's New / Edit / Remove / Properties
			                                                          // do not apply to the row (absent: they do)
			inline constexpr const char* Kind       = "Kind";
			inline constexpr const char* Name       = "Name";
			inline constexpr const char* Object     = "Object";       // the metaobject an item opens, by its id
			inline constexpr const char* Command    = "Command";
			// …a Load or Unload item's — a data processor's or a report's row (Load, Unload), its group's (Load): the file
			// it reads or writes, by its extension (ibFileExtension).
			inline constexpr const char* Extension  = "Extension";
			// …and its commands' Args.
			inline constexpr const char* Tab        = "Tab";
			inline constexpr const char* Id         = "Id";
			inline constexpr const char* Parent     = "Parent";
			inline constexpr const char* Module     = "Module";
			inline constexpr const char* File       = "File";
			inline constexpr const char* Select     = "Select";       // Remove: the object the tree selects after it — its
			                                                          // Properties shown with the answer

			// Its doors by an object's id, each with Tab in an external file's tree. Numbered of their own: the schema reads
			// them, no other.
			enum class ibCommand : int {
				Open     = 1,    // {Id} — the object's editor; or {Module} — the module of that address (a stop, a failure)
				Create   = 2,    // {Type, Parent} — a new one of that class under that object (0: the configuration's own), opened
				Remove   = 3,    // {Id}
				Move     = 4,    // {Id, Position} — put at that place among its parent's children
				Copy     = 5,    // {Id, File} — written into that file (uploaded empty)
				Paste    = 6,    // {Type, Parent, File} — read from that one
				Save     = 8,    // the configuration written
				// …the configuration's own (the desktop's Configuration menu), each said to the person by the server; refused
				// in an external file's tree:
				Rollback = 9,    // taken back from the base
				Apply    = 10,   // written and the base restructured, its ledger asked first (ibRequest::ibApplyChange)
				Load     = 11,   // {File} — read from that uploaded file
				Unload   = 12,   // {File} — written into that one (uploaded empty)
				Browse   = 13,   // the base's configuration in a tab, read-only (ibView::ibKind::Configuration): raised when
				                 // open, else opened
				// …and Load and Unload over a data processor or a report, as the file it is kept in apart (the desktop's
				// tree's Insert, Replace and Save): Load {Type, Parent, File} — one read from that file under that object;
				// Load {Id, File} — that one replaced by the one read; Unload {Id, File} — that one written into that file.
			};

		}

		// THE DESIGNER'S INSPECTOR: `Args: {Id, Tab}` — the metaobject of that id in the tab's tree (Tab: an external file's;
		// none: the configuration's), its Properties (ibNode::Id, Tab; categories Name, Title; property rows). What its
		// object's state hides is not sent. Save `{Id, Tab, Name, Value}` — that object's property or event given that
		// value.
		namespace ibProperties {

			inline constexpr const char* Id           = "Id";
			inline constexpr const char* Tab          = "Tab";
			inline constexpr const char* Properties   = "Properties";
			// A property row: Name, Title, Help, Class, Value as its node holds it, Presentation, ReadOnly, and what it
			// may be set to — its children Id, Title, Picture, Data; Multiple — several at once.
			inline constexpr const char* Name         = "Name";
			inline constexpr const char* Title        = "Title";
			inline constexpr const char* Help         = "Help";           // what it is for, as the inspector says it
			inline constexpr const char* Class        = "Class";          // the server's property class by name
			                                                              // (ibPropertyBoolean) — what the client's registry
			                                                              // shows it by
			inline constexpr const char* Value        = "Value";
			inline constexpr const char* Presentation = "Presentation";
			inline constexpr const char* ReadOnly     = "ReadOnly";
			inline constexpr const char* Picture      = "Picture";
			inline constexpr const char* Data         = "Data";           // a choice's data (ibPropertyChoiceList::GetData),
			                                                              // a std::uint64_t as a decimal
			inline constexpr const char* Multiple     = "Multiple";
			inline constexpr const char* Classes      = "Classes";        // the classes its choices' Data names (Type, Name,
			                                                              // Picture) — a source's, the type it holds
			inline constexpr const char* Type         = "Type";
			inline constexpr const char* Args         = "Args";           // an event's: its handler's parameters
			// …what a property's own editor reads beside them:
			inline constexpr const char* Kind         = "Kind";
			inline constexpr const char* Schedule     = "Schedule";       // a schedule's, in its stored form
			inline constexpr const char* Sources      = "Sources";        // a choice parameters row's: the fields beside it
			                                                              // that may supply a value (Title, the holder they
			                                                              // stand in); a source's: its window's tree
			inline constexpr const char* Default      = "Default";
			inline constexpr const char* Targets      = "Targets";
			inline constexpr const char* Pictures     = "Pictures";       // a picture's: those it may be (Guid, Name; Id —
			                                                              // the id it is drawn by)
			inline constexpr const char* Guid         = "Guid";
			inline constexpr const char* Object       = "Object";         // a link's: the metaobject it opens, by its id —
			                                                              // an inner module's module, else its own object
			// …a command source's: the desktop's command tree — sections, groups, sub-groups, a row each with its Name;
			// the command rows Name, Picture, CommandPath, Kind (ibInterfaceCommandType).
			inline constexpr const char* CommandTree  = "CommandTree";
			inline constexpr const char* CommandPath  = "CommandPath";    // the hops of a command — children Id
			// …a control's Source: what it may be bound to (SourceDataType, an ibSourceDataType), the tree its window
			// offers (Sources — rows as the attribute tree's: the roots «Name (type)», under them «Synonym (type)»); in its
			// Value, beside the path, the type the binding holds (SourceType — Value as a type is written, Presentation):
			// the desktop's Type row beneath it; unbound — none.
			inline constexpr const char* SourceDataType = "SourceDataType";
			inline constexpr const char* SourceType   = "SourceType";

			// What a control's Source may be bound to (the server's ibSourceDataType, as its GetFilterSourceDataType
			// answers); none said: an attribute.
			enum class ibSourceDataType : int {
				Table       = 0,   // a table of the form's data
				TableColumn = 1,   // a column of the table its table box is bound to
				Attribute   = 2,   // an attribute
			};

		}

		// Both modes': the configuration's Languages (Guid, Title, Code[, Default — true on the configuration's own
		// language, its DefaultLanguage; absent elsewhere]) — what a translated text is written in (the translation
		// window's boxes, a user's record's choice).
		namespace ibLanguages {

			inline constexpr const char* Tab       = "Tab";
			inline constexpr const char* Languages = "Languages";
			inline constexpr const char* Guid      = "Guid";
			inline constexpr const char* Title     = "Title";
			inline constexpr const char* Code      = "Code";
			inline constexpr const char* Default   = "Default";

		}

		// The desktop's predefined values editor over an object of the designer's tree: `schema {Schema, Args: {Id[, Tab]}}`
		// shows Type (the object's class) and Items, each as the engine stores it
		// (ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData): Guid, Name, Code, Description, IsFolder, Parent (the
		// Guid of the item it sits under; absent: the top); commands Create {Id, Name, Code, Description, IsFolder,
		// Parent}, Set {Id, Guid, …} — the item of that Guid given these (none of it: one more, of that Guid), Remove {Id,
		// Guid}. Each with Tab — an external file's tree, as ibMetaData's.
		namespace ibPredefined {

			inline constexpr const char* Id          = "Id";
			inline constexpr const char* Tab         = "Tab";
			inline constexpr const char* Type        = "Type";
			inline constexpr const char* Items       = "Items";
			inline constexpr const char* Guid        = "Guid";
			inline constexpr const char* Name        = "Name";
			inline constexpr const char* Code        = "Code";
			inline constexpr const char* Description = "Description";
			inline constexpr const char* IsFolder    = "IsFolder";
			inline constexpr const char* Parent      = "Parent";

		}

		// The desktop's help and technical information editors over an object of the designer's tree: `Args: {Id[, Tab]}`
		// shows its two texts — Help, what its user is told (the name a property row's help has too), and Note, what its
		// designers are; command Save {Id, Help} or {Id, Note} — the one given set. Each with Tab, as ibPredefined's.
		namespace ibObjectText {

			inline constexpr const char* Id   = "Id";
			inline constexpr const char* Tab  = "Tab";
			inline constexpr const char* Help = "Help";
			inline constexpr const char* Note = "Note";

		}

		// The desktop's home page workspace editor over the configuration's root: `Args: {Id}` shows HomePage — as the root
		// writes it (ibHomePageDescription), ReadOnly — when it takes no edit, and Forms, what it may show: Id, Name,
		// FullName, Picture; Common — a common form, else its owner's Owner (its id), OwnerName, OwnerPicture; command
		// Save {Id, HomePage}.
		namespace ibHomePage {

			inline constexpr const char* Id           = "Id";
			inline constexpr const char* Tab          = "Tab";
			inline constexpr const char* HomePage     = "HomePage";
			inline constexpr const char* ReadOnly     = "ReadOnly";
			inline constexpr const char* Forms        = "Forms";
			inline constexpr const char* Name         = "Name";
			inline constexpr const char* FullName     = "FullName";
			inline constexpr const char* Picture      = "Picture";
			inline constexpr const char* Common       = "Common";
			inline constexpr const char* Owner        = "Owner";
			inline constexpr const char* OwnerName    = "OwnerName";
			inline constexpr const char* OwnerPicture = "OwnerPicture";

		}

		// The designer's debugger's store — the debugger is the server's (ibDebug), and the configuration and its sys_dbg
		// are here: shows the configuration's Guid and Rows (Module — a module by its address, Count — its lines,
		// Breakpoints (Line, Condition)); `Args: {Module}` — that module's Breakpoints. A module view's State says its
		// Module too (ibView::ibModule).
		namespace ibDebugger {

			inline constexpr const char* Guid        = "Guid";
			inline constexpr const char* Rows        = "Rows";
			inline constexpr const char* Module      = "Module";
			inline constexpr const char* Count       = "Count";
			inline constexpr const char* Breakpoints = "Breakpoints";
			inline constexpr const char* Line        = "Line";
			inline constexpr const char* Condition   = "Condition";

			// The engine's ibDebuggerClient's rows of sys_dbg (debugClientQuery.cpp), a breakpoint by its module and its
			// committed line. Numbered of their own, as the tree's.
			enum class ibCommand : int {
				ToggleBreakpoint    = 1,    // {Module, Line, Condition} — set, or its condition replaced
				RemoveBreakpoint    = 2,    // {Module, Line}
				RemoveAllBreakpoint = 3,
			};

		}

	}

	// A TAB'S OWN VIEW — what the client draws a tab with when it is not a form: the tab's Kind. Its View carries State
	// and the view's own; it is fetched by the tab (fetch {Tab, Request}), and what the person did goes back as
	// event {Tab, Event, Args}.
	namespace ibView {

		inline constexpr const char* State    = "State";
		inline constexpr const char* MetaData = "MetaData";   // an external file's tree (DataProcessor, Report): its rows,
		                                                      // as the frame's MetaData; its doors — schema MetaData with the Tab

		enum class ibKind : int {
			Form             = 1,
			Text             = 2,    // ibText
			Spreadsheet      = 3,    // ibForm::ibSpreadsheet
			AuditLog         = 4,    // ibAuditLog
			DataProcessor    = 5,    // the designer's tree of an external file — of a data processor, of a report: MetaData
			Report           = 6,
			Module           = 7,    // ibModule
			FormDesigner     = 8,    // ibFormDesigner
			Interface        = 9,    // ibCheckTree: a section's — the objects in it
			CommonAttribute  = 10,   // ibCheckTree: a common attribute's — the objects that carry it
			FunctionalOption = 11,   // ibCheckTree: a functional option's — the objects and their fields and tables it hides
			                         // while off
			Role             = 12,   // ibCheckTree: a role's — every object that has rights, with its Roles
			Assistant        = 13,   // ibAssistant
			Composer         = 14,   // ibComposer
			Configuration    = 15,   // a configuration file (or the database's copy) browsed read-only: MetaData
			HomePage         = 16,   // ibHomePage: the start page — the forms the configuration places on it
		};

		// Text, ReadOnly; what was typed handed back as event {Tab, Event: Change, Args: {Text}}.
		namespace ibText {

			inline constexpr const char* Text     = "Text";
			inline constexpr const char* ReadOnly = "ReadOnly";

		}

		// THE REGISTRATION JOURNAL — its rows fetched by the tab, newest first, by the filter {From, To, Level, User,
		// Session, Source, Event, Ref, Search, Offset, Count} — each row Time, Level (the core logger's), Session, User,
		// Host, Source, Event, Message, Ref, RefMeta; a row's object opened as event Open {Ref, RefMeta}.
		namespace ibAuditLog {

			inline constexpr const char* From    = "From";      // ms since the epoch, inclusive — 0: no bound
			inline constexpr const char* To      = "To";        // …exclusive
			inline constexpr const char* Level   = "Level";
			inline constexpr const char* User    = "User";
			inline constexpr const char* Session = "Session";
			inline constexpr const char* Source  = "Source";
			inline constexpr const char* Event   = "Event";
			inline constexpr const char* Search  = "Search";    // a part of the message
			inline constexpr const char* Offset  = "Offset";    // the rows skipped — Count read after them
			inline constexpr const char* Count   = "Count";
			inline constexpr const char* Time    = "Time";      // a row's: ms since the epoch
			inline constexpr const char* Host    = "Host";
			inline constexpr const char* Message = "Message";
			inline constexpr const char* Ref     = "Ref";       // …the object it is about: its guid
			inline constexpr const char* RefMeta = "RefMeta";   // …and its metaobject's id — 0: a user (sys_user)

		}

		// The designer's editor of a module: State {Syntax, ReadOnly, Modified — the configuration the module is in,
		// Module}; the text fetched by the tab (fetch {Tab} — {Text}; what else — ibRequest); what was typed — event
		// {Tab, Event: Input} on the first key while not Modified, then {Tab, Event: Change, Args: {Text}}; its commands —
		// event {Tab, Event: Command, Args: {Id}} (ibCommand).
		namespace ibModule {

			inline constexpr const char* Syntax   = "Syntax";
			inline constexpr const char* ReadOnly = "ReadOnly";
			inline constexpr const char* Modified = "Modified";
			inline constexpr const char* Module   = "Module";
			inline constexpr const char* Text     = "Text";
			inline constexpr const char* Kind     = "Kind";
			inline constexpr const char* Position = "Position";
			inline constexpr const char* Name     = "Name";
			inline constexpr const char* Help     = "Help";
			inline constexpr const char* Args     = "Args";
			inline constexpr const char* Id       = "Id";

			// WHAT A MODULE'S TAB IS ASKED besides its text — `fetch {Tab, Request: {Kind, Text, Position}}`: what the
			// desktop's code editor asked the configuration itself. Text — the editor's text as it is now (not handed back
			// yet), Position — the caret in it, in characters.
			enum class ibRequest : int {
				Text       = 0,   // {Text} — the module's, as the server holds it (the request may be absent)
				Complete   = 1,   // rows {Kind (ibContentType), Name, Help} — what may be written at the caret
				CallTip    = 2,   // {Text} — the call form of what is being written at the caret; empty: none
				Procedures = 3,   // rows {Name, Args} — the module's default procedures, its handlers (no Text)
				Linq       = 4,   // ibLinq — what the LINQ constructor asks of the place it stands at, in one exchange
			};

			// A module tab's commands — event {Tab, Event: Command, Args: {Id}}.
			enum class ibCommand : int {
				SyntaxControl = 1,   // the module checked: "no errors" to the messages, or the error
				TestForm      = 2,   // {Form} — a form designer's: the form opened as the runtime shows it, a tab of its own
			};

			// WHAT THE LINQ CONSTRUCTOR ASKS — `fetch {Tab, Request: {Kind: Linq, Text, Position, …}}`: the desktop's
			// window asked the compiler itself (ibOutlineScriptQueries, ibNamesAtCaret, ibValueAtCaret, a kind's class
			// picture). Each part is asked by its field and answered beside the others, so one action is one exchange:
			//   · Open (true) — the window opening at Position: Query {From} — the innermost query the caret stands in, as
			//     the compiler bracketed it (none: none); Anchor — where the block will stand (the query's first word, or
			//     the caret); Names — rows {Name}: the values visible there (declared or the object's own, not callable);
			//     Values for the members of each of them and of `Data`; Kinds — rows {Name, Picture}: each of `Data`'s
			//     sections and the picture of the class it holds.
			//   · Probes — a list of probes, each a statement asked at its end (what follows its dot) after the module's
			//     Text up to the START of the line the block stands on (the opening's Anchor when Open is asked too,
			//     else Position) → Values; Deep — those of them whose names are asked too, each as the probe with
			//     `<name>.` after it.
			//   · Verdict (true) — Text is the module with the block in it, Position where the block starts: Read
			//     {Bindings — rows {Name, Origin, Offers — how many fields it offers}, Columns — rows {Name}}, as the
			//     compiler outlined the query that starts there; none: no query read there.
			// Values — rows {Text — the probe as asked, without the module's text before it (Open's: `linqProbe = <name>.`),
			// Resolved, Type — the class it resolved to, Names — rows {Name}: what may be written after its dot, the
			// frame's own left out}.
			namespace ibLinq {

				inline constexpr const char* Open     = "Open";
				inline constexpr const char* Probes   = "Probes";
				inline constexpr const char* Deep     = "Deep";
				inline constexpr const char* Verdict  = "Verdict";
				inline constexpr const char* Query    = "Query";
				inline constexpr const char* From     = "From";
				inline constexpr const char* Anchor   = "Anchor";
				inline constexpr const char* Names    = "Names";
				inline constexpr const char* Values   = "Values";
				inline constexpr const char* Kinds    = "Kinds";
				inline constexpr const char* Text     = "Text";
				inline constexpr const char* Name     = "Name";
				inline constexpr const char* Resolved = "Resolved";
				inline constexpr const char* Type     = "Type";
				inline constexpr const char* Picture  = "Picture";
				inline constexpr const char* Read     = "Read";
				inline constexpr const char* Bindings = "Bindings";
				inline constexpr const char* Origin   = "Origin";
				inline constexpr const char* Offers   = "Offers";
				inline constexpr const char* Columns  = "Columns";

			}

		}

		// THE DESIGNER'S EDITOR OF A FORM — a module's tab (ibModule: State, its requests, its Input and Change {Text})
		// whose form the CLIENT edits, as the desktop's editor did: fetch {Tab} — {Text, Form — the form's node as a frame
		// draws it (a control's rows asked as it is shown: ibRequest::Rows), Classes — the classes a control may be made of (Name, Picture, Type; Visible false — not offered to place; Menu — the classes a control of it adds
		// in itself, Name and Title each), AttributeTree, CommandTree}; State's Opening — once, in the frame that opens its
		// tab: what its Text fetch answers; what else — ibRequest; the form handed back whole — event {Tab, Event: Change,
		// Args: {Form}}.
		namespace ibFormDesigner {

			inline constexpr const char* Opening        = "Opening";
			inline constexpr const char* Caption        = "Caption";
			inline constexpr const char* Form           = "Form";
			inline constexpr const char* Controls       = "Controls";
			inline constexpr const char* Control        = "Control";
			inline constexpr const char* Classes        = "Classes";
			inline constexpr const char* Class          = "Class";
			inline constexpr const char* Name           = "Name";
			inline constexpr const char* Title          = "Title";
			inline constexpr const char* Picture        = "Picture";
			inline constexpr const char* Type           = "Type";
			inline constexpr const char* Visible        = "Visible";
			inline constexpr const char* Menu           = "Menu";
			// …a control's rows: the bar's rows (CommandBar — and Items, each command's of it by its Name) and a new
			// command's, with the node it is (Node) — the bar's commands the client adds.
			inline constexpr const char* CommandBar     = "CommandBar";
			inline constexpr const char* Items          = "Items";
			inline constexpr const char* CommandBarItem = "CommandBarItem";
			inline constexpr const char* Node           = "Node";
			// The attribute tree — the form's attributes, a row each (its node's id the attribute's) — Name, Main, Path,
			// Targets, TableSection, DropControl, HasComposition, Menu (Column — a value table's column); a row under one
			// (ibRequest::Composition) the same, and UnderList. A door about one names it by Attribute (0: a new one).
			inline constexpr const char* AttributeTree  = "AttributeTree";
			inline constexpr const char* Attribute      = "Attribute";
			inline constexpr const char* Main           = "Main";             // the form's main attribute
			inline constexpr const char* Path           = "Path";             // the hops from the attribute — children Id, Type
			inline constexpr const char* Targets        = "Targets";
			inline constexpr const char* TableSection   = "TableSection";     // a table or a section: its fields are columns
			inline constexpr const char* DropControl    = "DropControl";      // the class a drop of it makes (text, a clsid)
			inline constexpr const char* HasComposition = "HasComposition";   // it has rows under it, asked for when opened
			inline constexpr const char* UnderList      = "UnderList";        // it is in a list: a column, not a source
			inline constexpr const char* Column         = "Column";
			inline constexpr const char* Rows           = "Rows";
			// The command tree — the desktop's three sections (the form's own commands first, then the standard and the
			// global ones), a row each with its Name; under one, a source's sub-group rows and the command rows — Name,
			// Picture, CommandPath, Kind (ibInterfaceCommandType), a form command's row its node's id the command's. A door
			// about a form command names it by FormCommand (0: a new one).
			inline constexpr const char* CommandTree    = "CommandTree";
			inline constexpr const char* FormCommand    = "FormCommand";
			inline constexpr const char* CommandPath    = "CommandPath";      // the hops of a command — children Id
			inline constexpr const char* Kind           = "Kind";
			inline constexpr const char* Id             = "Id";
			// …and where the form's node keeps them, as the engine writes it (ibValueForm::WriteAttributes,
			// WriteFormCommands): a node each, its id the attribute's, the command's — what a copy of one is.
			inline constexpr const char* Attributes     = "Attributes";
			inline constexpr const char* FormCommands   = "FormCommands";
			// …what a request names and its answer gives.
			inline constexpr const char* Command        = "Command";
			inline constexpr const char* Value          = "Value";
			inline constexpr const char* Selected       = "Selected";         // the control to work on

			// WHAT A FORM'S TAB IS ASKED besides its module's — `fetch {Tab, Request: {Kind, …}}`, numbered past the
			// module's (ibModule::ibRequest), whose requests the same tab answers: what only the server knows of a form
			// edited on the client. Where the answer carries the form, it is the form as the server made it — {Form,
			// AttributeTree, CommandTree} as the tab's fetch writes them, and Selected, the control to work on —
			// and the client puts it in as an edit of its own: undone on the client. A request is about the form the server
			// holds — the client hands its own edits as a Change — and carries one only where it is about another (Rows,
			// Form); a door that changes the form writes it into its metaobject, the client hands it no more.
			enum class ibRequest : int {
				BoundControl   = 11,   // {Control, Path} — a control bound to the source at the path's end, dropped on that
				                       // control → the form
				CommandControl = 12,   // {Control, Value — {CommandPath, Kind}, as the command tree's row [, CommandBar]} —
				                       // a control running the command, dropped on that control (CommandBar: on its bar — an
				                       // item of it) → the form
				Rows           = 13,   // {Control [, Form — the form so edited]} — that control as the inspector shows it,
				                       // or after an edit of its properties: Node — as the server draws it (its State —
				                       // enabled, shown), Controls — its rows (some shown or hidden) and its sizer item's
				                       // (Control each)
				Composition    = 14,   // {Path, Targets, UnderList} of an attribute row → Rows, the rows under it
				                       // (AttributeTree's)
				Attribute      = 15,   // {Command (ibCommand), Attribute, Column, Name, Value} — a door of the attribute
				                       // tree (Column — of a value table's column) → the form, Attribute and Column — the one
				                       // to show
				FormCommand    = 16,   // {Command, FormCommand, Name, Value} — a door of the command tree → the form,
				                       // FormCommand — the one to show
				Properties     = 17,   // {Attribute [, Column] | FormCommand} — its rows, as the inspector writes them
				Form           = 18,   // {Form} → the form as the server draws it — after an edit of the client's that
				                       // changes what the server draws of it (a command bar's commands)
				Control        = 19,   // {Control, Class} — a control of that class made in that control (none: the form),
				                       // as the desktop's palette made it: one that binds to a source, bound to a fresh
				                       // attribute; {Control, Command Paste, Value} — the copied one put there → the form,
				                       // Selected — the one put
				Editor         = 20,   // {Control, Name} — what that control's property's own editor reads beside its row,
				                       // asked as the editor opens (ibSchema::ibProperties — a Source's: Sources, rows as
				                       // AttributeTree's, a reference's asked by Composition)
			};

			// A door of a form designer's tree — an attribute's, a form command's (ibRequest Attribute, FormCommand); a
			// control's paste (Control).
			enum class ibCommand : int {
				Create    = 1,   // a new one, uniquely named
				Remove    = 2,
				Paste     = 3,   // {Value — a node the client copied of the form's Attributes, FormCommands; a control's, as
				                 // the form draws it, in its sizer item} — a new one of it: its ids the server's, its names
				                 // unique on the form
				Set       = 4,   // {Name, Value} — its property (or event) of that name given that value, as the property's
				                 // node holds it
				SetMain   = 5,   // the form's main attribute — the attribute's
				AddColumn = 6,   // a column of the value table the attribute holds (its row's Menu has Column) — uniquely
				                 // named
			};

		}

		// THE DESIGNER'S CHECKABLE TREES over a metaobject (Interface, CommonAttribute, FunctionalOption, Role) — what it
		// applies to, ticked: State {ReadOnly — the object takes no edit}, Rows — the configuration's row, and under it its
		// groups and under them its objects, each ibNode::Id — its object's id (a group: below 0, its place among its
		// siblings), Title, Picture; an object's Checked, ReadOnly (its box greyed); a role's — its Roles (Name, Title,
		// Checked — the right given by the role, ReadOnly). A tick — event {Tab, Event: Toggle, Args: {ibNode::Id,
		// Checked}}; a role's names the right — Args: {ibNode::Id, Name, Checked}.
		namespace ibCheckTree {

			inline constexpr const char* ReadOnly = "ReadOnly";
			inline constexpr const char* Rows     = "Rows";
			inline constexpr const char* Title    = "Title";
			inline constexpr const char* Picture  = "Picture";
			inline constexpr const char* Checked  = "Checked";
			inline constexpr const char* Roles    = "Roles";
			inline constexpr const char* Name     = "Name";

		}

		// The designer's assistant window — the exchange with the assistant access: State {Waiting, Pending}, Rows — the
		// transcript, an entry a row (Text); what the person writes — event {Tab, Event: Change, Args: {Text}}.
		namespace ibAssistant {

			inline constexpr const char* Waiting = "Waiting";   // what the person sent has no answer yet
			inline constexpr const char* Pending = "Pending";   // the messages left for an assistant that none has collected
			inline constexpr const char* Rows    = "Rows";
			inline constexpr const char* Text    = "Text";

		}

		// The designer's editor of a report's composer — the composition panel: State {ReadOnly}, Composition — the
		// composer's description in its node form, Fields and References — what its query offers, as the settings
		// questions have them (Error — why it offers none); the fields of a text being edited, or of a reference's targets
		// — fetch {Tab, Request: {Kind (ibRequest), …}}; an edit — event {Tab, Event: Input}, what the panel holds — event
		// {Tab, Event: Change, Args: {Composition}}.
		namespace ibComposer {

			inline constexpr const char* ReadOnly    = "ReadOnly";
			inline constexpr const char* Composition = "Composition";
			inline constexpr const char* Fields      = "Fields";
			inline constexpr const char* References  = "References";
			inline constexpr const char* Error       = "Error";
			inline constexpr const char* Kind        = "Kind";
			inline constexpr const char* Text        = "Text";
			inline constexpr const char* Id          = "Id";
			inline constexpr const char* Targets     = "Targets";
			inline constexpr const char* Expanded    = "Expanded";

			// WHAT A COMPOSER'S TAB IS ASKED — `fetch {Tab, Request: {Kind, …}}`, numbered past the form's
			// (ibFormDesigner::ibRequest).
			enum class ibRequest : int {
				Fields = 21,   // {Text} — the fields that query text offers: Fields, References, Error — as the tab's view
				               // has them
				Expand = 22,   // {Targets} — a reference field opened: the fields of those targets, Expanded and their
				               // References — as a settings window's Act Expand has them asked again
				Source = 23,   // {Id, Text} — a dynamic list's fields: those of its main table (Id, optional) and what its
				               // query (Text, optional) adds — Fields, References, Error — as the desktop's list settings
				               // window reads them
			};

		}

		// THE DESIGNER'S QUERY CONSTRUCTOR — what its model (ibQueryConstructorModel) asks the configuration, of the tab
		// it was opened from (a module's, a form's, a composer's; a list settings window — its form's): `fetch {Tab,
		// Request: {Kind (ibRequest), …}}`, numbered past the composer's. The client keeps each answer for the tab: one
		// question per source, never one per edit. A reference's [+] is the composer's Expand (22), asked of the same tab.
		namespace ibQueryConstructor {

			inline constexpr const char* Kind             = "Kind";
			inline constexpr const char* Source           = "Source";            // a source as a FROM writes it, its arguments too
			inline constexpr const char* Slot             = "Slot";              // Fields: a condition parameter's — its fields
			inline constexpr const char* Sources          = "Sources";
			inline constexpr const char* Path             = "Path";
			inline constexpr const char* Presentation     = "Presentation";
			inline constexpr const char* Reference        = "Reference";         // the table's own reference type (a class id as text)
			inline constexpr const char* Fields           = "Fields";            // rows as a settings field tree reads them
			inline constexpr const char* References       = "References";        // …and which of their types are references
			inline constexpr const char* Parameters       = "Parameters";
			inline constexpr const char* Name             = "Name";
			inline constexpr const char* Condition        = "Condition";
			inline constexpr const char* Type             = "Type";
			inline constexpr const char* Required         = "Required";
			inline constexpr const char* Choices          = "Choices";
			inline constexpr const char* Description      = "Description";
			inline constexpr const char* ConsumedBySource = "ConsumedBySource";
			inline constexpr const char* Default          = "Default";
			inline constexpr const char* Error            = "Error";
			inline constexpr const char* Kinds            = "Kinds";
			inline constexpr const char* Picture          = "Picture";
			inline constexpr const char* Text             = "Text";
			inline constexpr const char* Statement        = "Statement";
			inline constexpr const char* UnionBranch      = "UnionBranch";
			inline constexpr const char* UngroupedProjections = "UngroupedProjections";

			enum class ibRequest : int {
				Sources          = 24,   // {} — every source a query may name: Sources, rows {Path, Presentation, Reference}
				                         // (GetSources); Kinds, rows {Name, Picture} (as Linq's Kinds)
				Fields           = 25,   // {Source [, Slot]} — its fields: Fields, References, Error (GetFields; with Slot —
				                         // GetConditionFields)
				SourceParameters = 26,   // {Source} — what a virtual table takes: Parameters, rows {Name, Condition, Type,
				                         // Required, Choices, Description, ConsumedBySource, Default} (GetSourceParameters)
				Check            = 27,   // {Text} — the whole query text: Error (CheckNames); with Statement, UnionBranch —
				                         // that select's UngroupedProjections
			};

		}

		// The start page a home page lays out, a tab's (Kind HomePage): Columns, the forms of each — Title, Icon, Height
		// (its weight in the column), View (the form, as a tab's) or Unavailable (why it is not); Gravity — the left
		// column's share of the width.
		namespace ibHomePage {

			inline constexpr const char* Columns     = "Columns";
			inline constexpr const char* Title       = "Title";
			inline constexpr const char* Icon        = "Icon";
			inline constexpr const char* Height      = "Height";
			inline constexpr const char* View        = "View";
			inline constexpr const char* Unavailable = "Unavailable";
			inline constexpr const char* Gravity     = "Gravity";

		}

	}

	// A FORM — a tab's form, its controls as the server writes them (visualView/): a node each, of its type (ibType), with
	// what it was saved with (ibProperty) beside its State, what the server reads it as now (ibState); and what the
	// person did with one, back by its id (ibEvent).
	namespace ibForm {

		inline constexpr const char* ControlId = "ControlId";
		inline constexpr const char* Name      = "Name";
		inline constexpr const char* Expanded  = "Expanded";
		inline constexpr const char* Key       = "Key";         // the form's: what an event or a fetch names it by

		// A control's node's type (ibNode::Type): the server's names of its controls.
		namespace ibType {

			inline constexpr const char* ClientForm          = "ClientForm";
			inline constexpr const char* SizerItem           = "SizerItem";
			inline constexpr const char* Boxsizer            = "Boxsizer";
			inline constexpr const char* Wrapsizer           = "Wrapsizer";
			inline constexpr const char* Gridsizer           = "Gridsizer";
			inline constexpr const char* Staticboxsizer      = "Staticboxsizer";
			inline constexpr const char* Statictext          = "Statictext";
			inline constexpr const char* Textctrl            = "Textctrl";
			inline constexpr const char* Button              = "Button";
			inline constexpr const char* Checkbox            = "Checkbox";
			inline constexpr const char* Staticline          = "Staticline";
			inline constexpr const char* Tablebox            = "Tablebox";
			inline constexpr const char* TableboxColumn      = "TableboxColumn";
			inline constexpr const char* TableboxColumnGroup = "TableboxColumnGroup";
			inline constexpr const char* Gridbox             = "Gridbox";
			inline constexpr const char* Textbox             = "Textbox";
			inline constexpr const char* Notebook            = "Notebook";
			inline constexpr const char* NotebookPage        = "NotebookPage";
			inline constexpr const char* Radiobutton         = "Radiobutton";
			inline constexpr const char* Slider              = "Slider";
			inline constexpr const char* Gauge               = "Gauge";
			inline constexpr const char* Htmlbox             = "Htmlbox";
			inline constexpr const char* Chartbox            = "Chartbox";

		}

		// What a control was saved with — its node's own fields, as its properties write them.
		namespace ibProperty {

			inline constexpr const char* Visible          = "Visible";
			inline constexpr const char* Enabled          = "Enabled";
			inline constexpr const char* Tooltip          = "Tooltip";
			inline constexpr const char* Orient           = "Orient";
			inline constexpr const char* MinimumSize      = "MinimumSize";
			inline constexpr const char* MaximumSize      = "MaximumSize";
			inline constexpr const char* Font             = "Font";
			inline constexpr const char* ForegroundColour = "ForegroundColour";
			inline constexpr const char* BackgroundColour = "BackgroundColour";
			inline constexpr const char* Proportion       = "Proportion";
			inline constexpr const char* BorderSize       = "BorderSize";
			inline constexpr const char* BorderLeft       = "BorderLeft";
			inline constexpr const char* BorderRight      = "BorderRight";
			inline constexpr const char* BorderTop        = "BorderTop";
			inline constexpr const char* BorderBottom     = "BorderBottom";
			inline constexpr const char* Stretch          = "Stretch";
			inline constexpr const char* Rows             = "Rows";            // a grid sizer's
			inline constexpr const char* Cols             = "Cols";
			inline constexpr const char* TitleLocation    = "TitleLocation";   // ibTitleLocation
			inline constexpr const char* PasswordMode     = "PasswordMode";
			inline constexpr const char* MultilineMode    = "MultilineMode";
			inline constexpr const char* ButtonSelect     = "ButtonSelect";
			inline constexpr const char* ButtonOpen       = "ButtonOpen";
			inline constexpr const char* ButtonClear      = "ButtonClear";
			inline constexpr const char* Range            = "Range";           // a gauge's: the value it is full at
			inline constexpr const char* MinValue         = "MinValue";        // a slider's: its ends
			inline constexpr const char* MaxValue         = "MaxValue";
			inline constexpr const char* Value            = "Value";           // a gauge's, a slider's

			// Where a field's title stands (the server's ibTitleLocation).
			enum class ibTitleLocation : int {
				Left  = 1,
				Right = 2,
			};

		}

		// What the server reads a control as now — its State, sent with the control and patched when it changes.
		namespace ibState {

			inline constexpr const char* State          = "State";
			inline constexpr const char* Subscribed     = "Subscribed";       // the state events the server is subscribed to
			                                                                  // on it — sent at once, by number
			inline constexpr const char* Visible        = "Visible";
			inline constexpr const char* Enabled        = "Enabled";
			inline constexpr const char* ReadOnly       = "ReadOnly";
			inline constexpr const char* Tooltip        = "Tooltip";
			inline constexpr const char* Title          = "Title";
			inline constexpr const char* Caption        = "Caption";
			inline constexpr const char* Picture        = "Picture";
			inline constexpr const char* Representation = "Representation";   // ibRepresentation: a button's, a column's
			inline constexpr const char* Text           = "Text";
			inline constexpr const char* Value          = "Value";            // a field's value with its type
			inline constexpr const char* Checked        = "Checked";
			inline constexpr const char* Selected       = "Selected";
			inline constexpr const char* Link           = "Link";
			inline constexpr const char* Page           = "Page";             // an HTML box's: the page it shows
			// …a form's:
			inline constexpr const char* Modified       = "Modified";
			inline constexpr const char* ViewOnly       = "ViewOnly";         // opened for viewing only
			inline constexpr const char* LockedBy       = "LockedBy";         // who holds its object's lock; absent: nobody

			// What a control shows (Auto is resolved by the server before it is written).
			enum class ibRepresentation : int {
				Auto           = 0,
				Text           = 1,
				Picture        = 2,
				PictureAndText = 3,
			};

		}

		// A COMMAND BAR — the State's CommandBar of a form, a table, a spreadsheet: its entries (Id, Caption, Picture,
		// Tooltip, Enabled, Kind; a separator; a group's commands as its children, Position each), pressed back as event
		// Command {Id, Member}; a table's ContextMenu, its standard commands, the same (Id, Caption, Picture, Enabled),
		// picked back as Action {Id}. Saved in its control's node's Layers (ibValueWindowComposite::WriteData).
		namespace ibCommandBar {

			inline constexpr const char* CommandBar = "CommandBar";
			inline constexpr const char* Layers     = "Layers";
			inline constexpr const char* Id         = "Id";
			inline constexpr const char* Separator  = "Separator";
			inline constexpr const char* Caption    = "Caption";
			inline constexpr const char* Picture    = "Picture";
			inline constexpr const char* Tooltip    = "Tooltip";
			inline constexpr const char* Enabled    = "Enabled";
			inline constexpr const char* Kind       = "Kind";       // ibKind
			inline constexpr const char* Position   = "Position";   // a group's command's: its place among the group's

			// What an entry is.
			enum class ibKind : int {
				Command     = 0,   // a command: pressed, it runs
				QuickFilter = 1,   // a filter by a value: pressed, the server asks for the value
				Group       = 2,   // a group: its commands are its children, one of them pressed (Member — its place
				                   // among them)
			};

		}

		// A button's group of commands — its State's Members, each Caption, Picture, Member (its place), pressed back as
		// event Press {Member}.
		namespace ibButton {

			inline constexpr const char* Members = "Members";
			inline constexpr const char* Member  = "Member";
			inline constexpr const char* Caption = "Caption";
			inline constexpr const char* Picture = "Picture";

		}

		// A notebook: where its tabs are; its State's page selected; a page's State shows a picture on its tab.
		namespace ibNotebook {

			inline constexpr const char* OrientPage     = "OrientPage";       // ibTabLocation
			inline constexpr const char* ActivePage     = "ActivePage";       // by its control id; 0 — none
			inline constexpr const char* PictureVisible = "PictureVisible";

			// Where the tabs are — the numbers a saved form holds (the server's ibTabLocation). None: no tabs, the
			// notebook is a panel showing its active page alone.
			enum class ibTabLocation : int {
				None   = 0,
				Top    = 1,
				Bottom = 8,
			};

		}

		// A TABLE: its columns (and their groups) in the view, its rows fetched — {Direction, Anchor, Parent, Count[,
		// Before]} → rows by their handles (Container, Group, ReadOnly, Picture — by its id) of cells by the columns' ids
		// (Text, Checked, Number, Value, the look), End, CurrentRow, Before.
		namespace ibTable {

			// …the table's node:
			inline constexpr const char* Header           = "Header";
			inline constexpr const char* HeaderHeight     = "HeaderHeight";
			inline constexpr const char* Footer           = "Footer";
			inline constexpr const char* FooterHeight     = "FooterHeight";
			inline constexpr const char* FreezeRow        = "FrezeeRow";   // the server's spelling
			inline constexpr const char* FreezeCol        = "FrezeeCol";
			inline constexpr const char* RowSelectionMode = "RowSelectionMode";
			// …its State:
			inline constexpr const char* ViewMode         = "ViewMode";      // ibViewMode
			inline constexpr const char* ChoiceMode       = "ChoiceMode";
			inline constexpr const char* Version          = "Version";
			inline constexpr const char* Rows             = "Rows";          // (protocol 5) the first page — same shape as
			                                                                 // fetch (rows as children, End, Before, Pictures)
			inline constexpr const char* CurrentRow       = "CurrentRow";
			inline constexpr const char* CurrentColumn    = "CurrentColumn";
			inline constexpr const char* Edit             = "Edit";          // the cell whose editor is open: Row, Column
			inline constexpr const char* Row              = "Row";
			inline constexpr const char* Column           = "Column";
			inline constexpr const char* ContextMenu      = "ContextMenu";   // its standard commands (ibCommandBar's entries)
			// …a column's node, and its State:
			inline constexpr const char* Width            = "Width";
			inline constexpr const char* HeaderAlign      = "HeaderAlign";
			inline constexpr const char* FooterAlign      = "FooterAlign";
			inline constexpr const char* Resizable        = "Resizable";
			inline constexpr const char* Reorderable      = "Reorderable";
			inline constexpr const char* TexteditMode     = "TexteditMode";   // may its cells be typed into
			inline constexpr const char* Sortable         = "Sortable";
			inline constexpr const char* Sort             = "Sort";
			inline constexpr const char* HeaderPicture    = "HeaderPicture";
			inline constexpr const char* FooterText       = "FooterText";
			inline constexpr const char* FooterPicture    = "FooterPicture";
			// …a column group's node:
			inline constexpr const char* Grouping         = "Grouping";
			inline constexpr const char* ShowTitle        = "ShowTitle";
			// …the fetch:
			inline constexpr const char* Direction        = "Direction";   // ibDirection
			inline constexpr const char* Anchor           = "Anchor";
			inline constexpr const char* Parent           = "Parent";
			inline constexpr const char* Count            = "Count";
			inline constexpr const char* Before           = "Before";      // (protocol 4) a reset fetch's: the rows wanted ahead
			                                                               // of the page — answered with how many stand first
			inline constexpr const char* End              = "End";
			// …a row:
			inline constexpr const char* Container        = "Container";
			inline constexpr const char* Group            = "Group";
			inline constexpr const char* ReadOnly         = "ReadOnly";
			inline constexpr const char* Picture          = "Picture";
			// …a cell:
			inline constexpr const char* Text             = "Text";
			inline constexpr const char* Checked          = "Checked";
			inline constexpr const char* Number           = "Number";
			inline constexpr const char* Value            = "Value";            // its value with its type — in a cell that
			                                                                    // may be edited
			inline constexpr const char* TextColour       = "TextColour";       // its look, laid over the table's
			inline constexpr const char* BackgroundColour = "BackgroundColour";
			inline constexpr const char* Bold             = "Bold";
			inline constexpr const char* Italic           = "Italic";
			inline constexpr const char* Strikethrough    = "Strikethrough";
			inline constexpr const char* Underlined       = "Underlined";
			inline constexpr const char* Face             = "Face";
			inline constexpr const char* Size             = "Size";
			inline constexpr const char* Align            = "Align";            // ibAlign

			// How a table shows its rows.
			enum class ibViewMode : int {
				Tree         = 0,   // the whole hierarchy, a folder opened in place
				Hierarchical = 1,   // one level, a folder entered
				List         = 2,   // every row, flat
			};

			// Which way a fetch reads from its Anchor.
			enum class ibDirection : int {
				Backward = -1,   // the rows before it
				Reset    = 0,    // the page at it, read anew
				Forward  = 1,    // the rows after it
			};

			// Where a cell's text stands.
			namespace ibAlign {

				inline constexpr const char* Left   = "Left";
				inline constexpr const char* Center = "Center";
				inline constexpr const char* Right  = "Right";

			}

		}

		// A SPREADSHEET: the version of the sheet on show in the view, the sheet fetched whole — Sheet, in the form a
		// template is stored in (the engine's ibSpreadsheetDescriptionMemory::WriteNode), ReadOnly beside it; edited here,
		// it goes back the same way, event Change {Sheet}; a cell clicked, Cell {Row, Col}. A tab's spreadsheet view
		// (ibView::ibKind::Spreadsheet) is the same.
		namespace ibSpreadsheet {

			inline constexpr const char* Version      = "Version";
			inline constexpr const char* Sheet        = "Sheet";
			inline constexpr const char* ReadOnly     = "ReadOnly";
			inline constexpr const char* ComposeState = "ComposeState";   // the State's: the sheet to its report (ibComposeState)
			inline constexpr const char* GridLines    = "GridLines";      // the State's: its grid lines shown — a document's
			inline constexpr const char* GridArea     = "GridArea";       // the State's: its areas shown — a template's

			// What the sheet on show is to the report behind it.
			enum class ibComposeState : int {
				Composed  = 0,   // what was built, as the setting in force builds it (or nothing composed yet)
				Composing = 1,   // a compose is running; the sheet on show stays until it is delivered
				Outdated  = 2,   // the setting changed since the sheet was built — composed again when the person says so
			};

		}

	}

	// WHERE A FILE BASE LIVES — what fileserver opens it by (fileserver/fileBase.h).
	namespace ibFileBase {

		inline constexpr const char* Name      = "Name";     // the base's — a web server's publication
		inline constexpr const char* Locale    = "Locale";
		inline constexpr const char* Directory = "Directory";
		inline constexpr const char* Server    = "Server";
		inline constexpr const char* Port      = "Port";
		inline constexpr const char* Database  = "Database";
		inline constexpr const char* User      = "User";
		inline constexpr const char* Password  = "Password";
		inline constexpr const char* Create    = "Create";   // made there when there is none yet (the designer's)
		inline constexpr const char* Debug     = "Debug";    // the engine in the client's process brings its debug server up
		                                                     // (the designer's Start debugging)

	}

}

#endif
