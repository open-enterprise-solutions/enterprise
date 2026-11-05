#ifndef __PROTOCOL_PROTOCOL_H__
#define __PROTOCOL_PROTOCOL_H__

#include <cstring>   // std::strcmp — a method by its name

// THE PROTOCOL — the one place its names and numbers are written, for every side of it: the server's forms (frmserver),
// the thin client (frmclient) and, later, the web server, which tells a browser the same. Nothing here depends on
// anything — no wx, no engine type — so whoever speaks the protocol includes it and nothing comes with it. A number
// below is never renumbered; a new one takes the next. The wire itself is described in docs/public/client-protocol.md.
//
// TWO FILES, ONE NUMBERING. This one is what a client of the application speaks — the thin client, the browser; the
// designer speaks it too, and its own beside it: its debugger, its schemas and views, the questions asked while a
// configuration is written (protocolDesigner.h). Every number stays here, the designer's with the rest — the wire has
// one numbering — and a mode refuses one it does not offer.
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
	//   7  …and the assistant's MCP rides it the same way (ibMcp): a connection of its own, bound by a key of its own.
	//   8  …and a button says where its picture stands (its PictureLocation): left or right of its caption, or a TILE -
	//      its picture large over its caption, and its State's Note under them - and how it is drawn (its View): a
	//      button or a hyperlink. A client below 8 draws a button with its picture on the left.
	inline constexpr int Version = 8;

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
		Mcp,            // ibMcp — never the frame
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
		case ibMethod::Mcp:          return "mcp";
		}
		return "";
	}

	// …and the method a name means; Unknown for a name the protocol does not have.
	inline ibMethod MethodFromName(const char* name)
	{
		for (int method = static_cast<int>(ibMethod::Login); method <= static_cast<int>(ibMethod::Mcp); ++method) {
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
		Exception    = 520,   // the engine raised it doing the call — its words, for the person: a read that failed
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
		// …and what the assistant's server says unasked, to the client's MCP connection (params: Client, Message — ibMcp).
		inline constexpr const char* Mcp     = "mcp";

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

	// THE ASSISTANT'S MCP (protocol 7) — protocol inside the protocol: the client's end only carries it, and what a message
	// means is the server's (frmserver/mcp). On a connection of its own, so a tool working does not hold the window up, and
	// bound as the debugger's is: mcp {Command: Channel} on the client's connection → Channel, a key; mcp {Command: Listen,
	// Channel} on a second connection of the same client binds that one — from then on mcp {Command: Message} is answered
	// on it and it is sent what the server says unasked, `mcp {Client, Message}`. Refused NoRight to a person whose roles
	// do not give the assistant (ibSchema::ibMcpServer). The client's session answers: their rights, their configuration.
	namespace ibMcp {

		inline constexpr const char* Command         = "Command";
		inline constexpr const char* Channel         = "Channel";          // Channel's answer, Listen's key
		inline constexpr const char* Message         = "Message";          // the JSON text, as MCP wrote it; empty - the headers alone asked of
		inline constexpr const char* ProtocolVersion = "ProtocolVersion";  // a Message's: the HTTP header it came with, MCP-Protocol-Version
		inline constexpr const char* Method          = "Method";           // …Mcp-Method
		inline constexpr const char* Name            = "Name";             // …Mcp-Name
		inline constexpr const char* Status          = "Status";           // a Message's answer: the HTTP status to carry it with

		enum class ibCommand : int {
			Channel = 1,    // → Channel
			Listen  = 2,    // {Channel}
			Message = 3,    // {Message, ProtocolVersion, Method, Name} → {Message, Status}
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
			ObjectKind       = 18,   // ibObjectKind
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
		// Ids — those of them ticked. With Quick — no window: the desktop's quick choice, the items dropped under the field
		// pressed (a reference's, or those that match what was typed); none — dismissed.
		namespace ibChoice {

			inline constexpr const char* Caption  = "Caption";
			inline constexpr const char* Id       = "Id";
			inline constexpr const char* Picture  = "Picture";
			inline constexpr const char* Selected = "Selected";
			inline constexpr const char* Multiple = "Multiple";   // several may be chosen — answered with Ids
			inline constexpr const char* Ids      = "Ids";
			inline constexpr const char* Quick    = "Quick";      // dropped under the field pressed, not a window

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
		// (children Type — its class id as text — Targets, the metaobjects, and Value, its empty value as a value is written:
		// read as the server reads it, `CatalogRef.Goods.EmptyRef` in the designer; none for a family) → Settings — the
		// setting edited, which the server puts on and asks no more; Error beside the setting when it refused one; none —
		// cancelled. On the way: an Act (ibAct), asked again with what it brought.
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
			Types        = 19,   // ibTypes
			Style        = 20,   // ibStyle — both modes': how the person's interface is painted; command Save {Theme}
		};

		// A schema's own commands — `schema {Schema, Command, Args}`. All functions and Sections take the one: Open
		// {Item, Type} — an item they show, opened as the navigation opens it. Debugging takes Start and Stop; McpServer
		// takes them and Save; Active users, the designer's, Kick, Reload and Release; the users Save and Remove; the
		// designer's inspector (Properties) Save; its predefined values editor Create, Set and Remove; its texts
		// (ObjectText) Save, and so the home page (HomePage); the base (Database) Load, Save and Clear; the assistant window
		// (Assistant) Open; ConfigCompare Merge; the style (Style) Save. The designer's tree has its own (ibMetaData::ibCommand), and so its debugger's store.
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

		// Both modes': how the person's interface is painted — Theme, theirs, kept in the base (System when they chose none).
		// Command Save {Theme} keeps another. A client paints itself with it: the system's follows the machine's own
		// light or dark.
		namespace ibStyle {

			inline constexpr const char* Theme = "Theme";   // ibTheme

			enum class ibTheme : int {
				System = 0,   // as the machine the client runs on is set
				Light  = 1,
				Dark   = 2,
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

		// The assistant access, the person's: Enabled, Address, Port, Token — where their client listens (it does the
		// listening: ibMcp), whether it does, and what a person hands their tool. Command Save {…} — kept as the person's;
		// a token not made yet is made here. A person's roles not giving the assistant — no such schema (AccessRight).
		namespace ibMcpServer {

			inline constexpr const char* Allowed  = "Allowed";

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
			// The classes a type cell may name — the fields', the parameters' and the primitive kinds — each once, as the
			// inspector names its choices' (ibSchema::ibProperties: Type, Name, Picture): the client has no configuration
			// to name a kind by.
			inline constexpr const char* Classes     = "Classes";
			// …and the types a parameter may be given (Kind Types: each a decimal class id, named in Classes) — the offer
			// the client's type picker shows.
			inline constexpr const char* Types       = "Types";

			// WHAT A COMPOSER'S TAB IS ASKED — `fetch {Tab, Request: {Kind, …}}`, numbered past the form's
			// (ibFormDesigner::ibRequest). A cell's "..." is a settings window's Act Choose said to the tab — event {Tab,
			// Event: Select, Args: {Type, Value, Choice} (ibRequest::ibListSettings)}; the value chosen comes back with the
			// view, as the window's question carries it: State Chosen.
			enum class ibRequest : int {
				Fields = 21,   // {Text} — the fields that query text offers: Fields, References, Error — as the tab's view
				               // has them
				Expand = 22,   // {Targets} — a reference field opened: the fields of those targets, Expanded and their
				               // References — as a settings window's Act Expand has them asked again
				Source = 23,   // {Id, Text} — a dynamic list's fields: those of its main table (Id, optional) and what its
				               // query (Text, optional) adds — Fields, References, Error — as the desktop's list settings
				               // window reads them
				Types  = 28,   // {} — what a parameter may be given: Types, named in Classes — what the desktop's type picker
				               // asked its configuration for a reference-shaped field (GetTypesByFilter). Past the query
				               // constructor's, which a composer's tab answers too
				Check  = 29,   // {Text} — a parameter's expression compiled, not run: Error — why it does not compile;
				               // none — it does (the desktop panel's CheckExpression). Past the query constructor's
				               // (ibQueryConstructor::ibRequest, 24–27): the same tab answers both
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
			inline constexpr const char* PictureLocation  = "PictureLocation"; // ibPictureLocation: a button's
			inline constexpr const char* View             = "View";            // ibButtonView: a button's
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

			// Where a button's picture stands (the server's ibPictureLocation): Auto where the button puts it, on the left;
			// Tile, its picture large over its caption and its State's Note under them, the whole filling what its item
			// gives it.
			enum class ibPictureLocation : int {
				Auto  = 0,
				Left  = 1,
				Right = 2,
				Tile  = 3,
			};

			// How a button is drawn (the server's ibButtonView): a hyperlink has no face and no frame, its caption underlined.
			enum class ibButtonView : int {
				Button    = 0,
				Hyperlink = 1,
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
			inline constexpr const char* Note           = "Note";             // a tile's: the line under its caption
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
			inline constexpr const char* Begin            = "Begin";       // a reset's answer: no row stands before its rows
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
