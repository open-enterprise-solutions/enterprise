#ifndef __PROTOCOL_PROTOCOL_H__
#define __PROTOCOL_PROTOCOL_H__

#include <cstring>   // std::strcmp — a method by its name

// THE PROTOCOL — the one place its names and numbers are written, for every side of it: the server's forms (frmserver),
// the thin client (frmclient) and, later, the web server, which tells a browser the same. Nothing here depends on
// anything — no wx, no engine type — so whoever speaks the protocol includes it and nothing comes with it. A number
// below is never renumbered; a new one takes the next. The wire itself is described in docs/public/client-protocol.md.

// THE WIRE'S NAMES — every field this client reads or writes, written once. A name misspelt where it is used is not an
// error anywhere: the server skips a field it does not know and the client reads an absent one as its default, so a
// typo is a silence. Spelt here, it is a compile error.
namespace ibProtocolName {

	// The envelope — JSON-RPC 2.0's own.
	inline constexpr const char* RpcVersion = "jsonrpc";
	inline constexpr const char* RpcId      = "id";
	inline constexpr const char* RpcMethod  = "method";
	inline constexpr const char* RpcParams  = "params";
	inline constexpr const char* RpcResult  = "result";
	inline constexpr const char* RpcError   = "error";
	inline constexpr const char* RpcCode    = "code";
	inline constexpr const char* RpcMessage = "message";

	// What the server says unasked — a notification's method: a client's frame changed by itself (params: Client).
	inline constexpr const char* Changed = "changed";

	// A node, as the wire writes one — and a patch's own words.
	inline constexpr const char* NodeId            = "NodeId";
	inline constexpr const char* NodeType          = "NodeType";
	inline constexpr const char* NodeChildren      = "NodeChildren";
	inline constexpr const char* NodeRemoved       = "NodeRemoved";
	inline constexpr const char* NodeRemovedIds    = "NodeRemovedIds";
	inline constexpr const char* NodeOrder         = "NodeOrder";
	inline constexpr const char* NodeChildrenWhole = "NodeChildrenWhole";

	// The login, and what every call names.
	inline constexpr const char* User     = "User";
	inline constexpr const char* Password = "Password";
	inline constexpr const char* Token    = "Token";   // login's bearer, once, in the password login's answer. The
	                                                   // database keeps its hash (docs/public/client-protocol.md)
	inline constexpr const char* Mode     = "Mode";
	inline constexpr const char* Protocol = "Protocol";
	inline constexpr const char* Features = "Features";
	inline constexpr const char* FeatureResume = "resume";   // the same-process window only. A client names it in
	                                                          // login's Features to opt into the idempotency slot.
	inline constexpr const char* Client   = "Client";
	inline constexpr const char* Since    = "Since";

	// The frame — and the answer's own beside it.
	inline constexpr const char* Frame     = "Frame";
	inline constexpr const char* Patch     = "Patch";
	inline constexpr const char* Messages  = "Messages";
	inline constexpr const char* Clear     = "Clear";
	inline constexpr const char* Exit      = "Exit";   // the answer's: the client is to go — Exit done
	inline constexpr const char* Title     = "Title";
	inline constexpr const char* Status    = "Status";
	inline constexpr const char* Menu      = "Menu";
	inline constexpr const char* Tabs      = "Tabs";
	inline constexpr const char* ActiveTab = "ActiveTab";
	inline constexpr const char* Schemas   = "Schemas";
	inline constexpr const char* Templates = "Templates";
	inline constexpr const char* Request   = "Request";
	inline constexpr const char* View      = "View";

	// What the frame's parts carry: a menu item, a tab, a message, a question and its answer.
	inline constexpr const char* Separator    = "Separator";
	inline constexpr const char* Command      = "Command";
	inline constexpr const char* Schema       = "Schema";
	inline constexpr const char* Shortcut     = "Shortcut";
	inline constexpr const char* StockPicture = "StockPicture";
	inline constexpr const char* Enabled      = "Enabled";
	inline constexpr const char* Icon      = "Icon";
	inline constexpr const char* Locked    = "Locked";
	inline constexpr const char* Commands  = "Commands";   // a tab's: the commands its document takes now
	inline constexpr const char* Formats   = "Formats";    // a tab's: what its document saves as — Title, Mask each
	inline constexpr const char* Tab       = "Tab";
	inline constexpr const char* Level     = "Level";
	inline constexpr const char* Text      = "Text";
	inline constexpr const char* Id        = "Id";
	inline constexpr const char* Kind      = "Kind";
	inline constexpr const char* Caption   = "Caption";
	inline constexpr const char* Style     = "Style";
	inline constexpr const char* Button    = "Button";
	inline constexpr const char* Response  = "Response";
	inline constexpr const char* Selected  = "Selected";
	inline constexpr const char* Multiple  = "Multiple";   // a choice's: several may be chosen — answered with Ids
	inline constexpr const char* Ids       = "Ids";
	inline constexpr const char* Act       = "Act";       // a saved settings' answer: what was done to the shelf
	inline constexpr const char* Default   = "Default";   // …the entry restored on open
	inline constexpr const char* Done      = "Done";      // …the shelf asked again: how the act before went
	inline constexpr const char* Controls   = "Controls";     // a form editor's Apply: the form's controls, each by its Control id
	inline constexpr const char* Classes    = "Classes";      // …the controls' classes: Name, Picture
	inline constexpr const char* Position   = "Position";     // …where a control stands among its siblings
	inline constexpr const char* HasSetting = "HasSetting";   // …a setting of the person's is saved for this form
	inline constexpr const char* Result     = "Result";       // …how a save or a reset went (ibProtocolFormSettingsResult)
	inline constexpr const char* Settings   = "Settings";     // a list's settings window: the setting, as the schema writes it
	inline constexpr const char* Fields     = "Fields";       // …the list's fields: Name, Id, Type, Available
	inline constexpr const char* Available  = "Available";    // …a field the options of the base leave to it
	inline constexpr const char* Error      = "Error";        // …why the server refused the setting it was given
	inline constexpr const char* References = "References";   // …which types are references, and to what (Type, Targets)
	inline constexpr const char* Targets    = "Targets";      // …the metaobjects a reference type points at
	inline constexpr const char* Expanded   = "Expanded";     // …the fields of the targets a reference field opened on
	inline constexpr const char* Presentation = "Presentation";   // …how a field reads — its synonym; none: its name
	inline constexpr const char* Chosen     = "Chosen";       // …a value chosen on the server's choice form: Value, Choice
	inline constexpr const char* Choice     = "Choice";       // …which choice it answers — a number the client took it by
	inline constexpr const char* Picture   = "Picture";
	inline constexpr const char* Mask      = "Mask";   // a template's, a file request's item: what files it offers, Title beside it
	inline constexpr const char* File      = "File";   // an uploaded file's id
	inline constexpr const char* Extension = "Extension";   // a template's; open's, without a File: the template a new document is made of
	inline constexpr const char* OnlyOpen  = "OnlyOpen";    // a template's: it opens a file, and makes no new document
	inline constexpr const char* Data      = "Data";   // a file's part going up or down, in base64
	inline constexpr const char* Part      = "Part";   // download's: the part asked for, from 0
	inline constexpr const char* Parts     = "Parts";  // …and how many the file has

	// A schema's — what it shows, and what a person did in it.
	inline constexpr const char* Args        = "Args";
	inline constexpr const char* Item        = "Item";
	inline constexpr const char* Type        = "Type";
	inline constexpr const char* Groups      = "Groups";
	inline constexpr const char* Sections    = "Sections";
	inline constexpr const char* Blocks      = "Blocks";
	inline constexpr const char* Column      = "Column";
	inline constexpr const char* Holder      = "Holder";      // a column moved: the group it now stands in — 0, the table
	inline constexpr const char* Tooltip     = "Tooltip";
	inline constexpr const char* Sessions    = "Sessions";
	inline constexpr const char* Locks       = "Locks";
	inline constexpr const char* Application = "Application";
	inline constexpr const char* Started     = "Started";
	inline constexpr const char* Computer    = "Computer";
	inline constexpr const char* Session     = "Session";
	inline constexpr const char* Namespace   = "Namespace";
	inline constexpr const char* Key         = "Key";
	inline constexpr const char* Acquired    = "Acquired";
	inline constexpr const char* Lock        = "Lock";
	inline constexpr const char* Build       = "Build";
	inline constexpr const char* Plugins     = "Plugins";

	// A view — a tab's form, its controls as the server writes them (visualView/): what each was saved with, beside its
	// State, what the server reads it as now; and what the person did with one, back by its id.
	inline constexpr const char* ControlId        = "ControlId";
	inline constexpr const char* State            = "State";
	inline constexpr const char* Control          = "Control";
	inline constexpr const char* Event            = "Event";
	inline constexpr const char* Form             = "Form";
	inline constexpr const char* Visible          = "Visible";
	inline constexpr const char* ReadOnly         = "ReadOnly";
	inline constexpr const char* Link             = "Link";
	inline constexpr const char* Checked          = "Checked";
	inline constexpr const char* Representation   = "Representation";
	inline constexpr const char* Members          = "Members";
	inline constexpr const char* Member           = "Member";
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
	inline constexpr const char* Rows             = "Rows";
	inline constexpr const char* Cols             = "Cols";
	inline constexpr const char* TitleLocation    = "TitleLocation";
	inline constexpr const char* PasswordMode     = "PasswordMode";
	inline constexpr const char* MultilineMode    = "MultilineMode";
	inline constexpr const char* ButtonSelect     = "ButtonSelect";
	inline constexpr const char* ButtonOpen       = "ButtonOpen";
	inline constexpr const char* ButtonClear      = "ButtonClear";

	// A command bar — the State's CommandBar of a form, a table, a spreadsheet: its entries (Id, Caption, Picture,
	// Tooltip, Enabled, Kind; a separator; a group's commands as its children), pressed back as Command {Id, Member}.
	inline constexpr const char* CommandBar  = "CommandBar";
	inline constexpr const char* ContextMenu = "ContextMenu";   // a table's: its standard commands (Id, Caption, Picture,
	                                                            // Enabled), picked back as Action {Id}

	// A table: its columns (and their groups) in the view, its rows fetched — {Direction, Anchor, Parent, Count} → rows
	// by their handles (Container, Group, ReadOnly, Picture) of cells by the columns' ids (Text, Checked, Number), End,
	// CurrentRow; Pictures the rows' pictures the client is given once (Id, Picture).
	inline constexpr const char* Header           = "Header";
	inline constexpr const char* HeaderHeight     = "HeaderHeight";
	inline constexpr const char* Footer           = "Footer";
	inline constexpr const char* FooterHeight     = "FooterHeight";
	inline constexpr const char* FreezeRow        = "FrezeeRow";   // the server's spelling
	inline constexpr const char* FreezeCol        = "FrezeeCol";
	inline constexpr const char* RowSelectionMode = "RowSelectionMode";
	inline constexpr const char* ViewMode         = "ViewMode";
	inline constexpr const char* ChoiceMode       = "ChoiceMode";
	inline constexpr const char* CurrentRow       = "CurrentRow";
	inline constexpr const char* CurrentColumn    = "CurrentColumn";
	inline constexpr const char* Version          = "Version";
	inline constexpr const char* Width            = "Width";
	inline constexpr const char* HeaderAlign      = "HeaderAlign";
	inline constexpr const char* FooterAlign      = "FooterAlign";
	inline constexpr const char* Resizable        = "Resizable";
	inline constexpr const char* Reorderable      = "Reorderable";
	inline constexpr const char* Sortable         = "Sortable";
	inline constexpr const char* Sort             = "Sort";
	inline constexpr const char* FooterText       = "FooterText";
	inline constexpr const char* HeaderPicture    = "HeaderPicture";
	inline constexpr const char* FooterPicture    = "FooterPicture";
	inline constexpr const char* Grouping         = "Grouping";
	inline constexpr const char* ShowTitle        = "ShowTitle";
	inline constexpr const char* Direction        = "Direction";
	inline constexpr const char* Anchor           = "Anchor";
	inline constexpr const char* Parent           = "Parent";
	inline constexpr const char* Count            = "Count";
	inline constexpr const char* End              = "End";
	inline constexpr const char* Container        = "Container";
	inline constexpr const char* Group            = "Group";
	inline constexpr const char* Number           = "Number";
	inline constexpr const char* Pictures         = "Pictures";
	inline constexpr const char* Row              = "Row";
	inline constexpr const char* Italic           = "Italic";          // a cell's look: with TextColour,
	inline constexpr const char* Strikethrough    = "Strikethrough";   // BackgroundColour, Bold, Align —
	inline constexpr const char* Underlined       = "Underlined";      // and the rest of a font, laid over
	inline constexpr const char* Face             = "Face";            // the table's, with Size
	inline constexpr const char* Value            = "Value";           // a cell's value with its type — in a cell that may be edited
	inline constexpr const char* Edit             = "Edit";            // the State's cell whose editor is open: Row, Column
	inline constexpr const char* TexteditMode     = "TexteditMode";    // a column's: may its cells be typed into

	// A spreadsheet: the version of the sheet on show in the view, the sheet fetched whole — Sheet, in the form a
	// template is stored in (the engine's ibSpreadsheetDescriptionMemory::WriteNode), ReadOnly beside it; edited here,
	// it goes back the same way, Change {Sheet}; a cell clicked, Cell {Row, Col}.
	inline constexpr const char* Composing  = "Composing";
	inline constexpr const char* GridLines  = "GridLines";   // the State's: its grid lines shown — a document's sheet
	inline constexpr const char* Sheet      = "Sheet";
	inline constexpr const char* Col        = "Col";
	inline constexpr const char* Bold       = "Bold";
	inline constexpr const char* Align      = "Align";
	inline constexpr const char* TextColour = "TextColour";
	inline constexpr const char* Size       = "Size";

	// Where a file base lives — what fileserver opens it by (fileserver/fileBase.h).
	inline constexpr const char* Name      = "Name";
	inline constexpr const char* Locale    = "Locale";
	inline constexpr const char* Directory = "Directory";
	inline constexpr const char* Server    = "Server";
	inline constexpr const char* Port      = "Port";
	inline constexpr const char* Database  = "Database";
	// …and whether the engine in the client's process brings its debug server up (the designer's Start debugging).
	inline constexpr const char* Debug     = "Debug";

}

// A VIEW'S NODES — the types a tab's form is written in (NodeType): the server's names of its controls.
namespace ibProtocolType {

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

}

// The version of the protocol — the newest both sides here speak; a client names its own at login, and the two go on
// with the older. Raised when a change needs it, never for a field added: an absent field is its default.
//   1  a patch is from the frame last sent;
//   2  …and its View from the view last sent for the tab active NOW, when the client was sent one; else from the
//      last frame's, as in 1. A client keeps each tab's view, puts the one of the tab the patch makes active in
//      place (keeping the view it holds when it has none for that tab), and applies the patch's View to it; a frame
//      sent whole begins the store again — back on a tab, it is sent what changed there, not the whole form.
constexpr int ibProtocolVersion = 2;

// WHAT A CLIENT ASKS THE SERVER. On the wire a method is a name, as JSON-RPC has it; in code it is a type: the server
// reads the name once (ibProtocolMethodFromName) and nothing past it compares strings. A name is never changed; a new
// method takes a new one. Every answer but logout's, fetch's, a schema's own and a file's is the client's frame.
enum class ibProtocolMethod {
	Unknown,        // a name the protocol does not have
	Login,          // {User, Password[, Mode][, Protocol][, Features]} → Client, Protocol, Features, and the frame.
	                // A thin runtime whose window is on also gets Token, once. Features on the request names what the
	                // client accepts; `resume` there opts into the idempotency slot. {Token[, Protocol]} and no User,
	                // only while that client is detached, → the previous Client, Protocol, Features, and no frame
	                // (the next frame {Since} is the patch). A token login does not return Token again. Mode:
	                // ibProtocolMode, Runtime when absent; Protocol: the newest version the client speaks, 1 when
	                // absent — answered with the one both speak, and the features the server offers at it
	Logout,         // {Client}
	Frame,          // {Client} → the frame
	Schema,         // {Client, Schema[, Command[, Args]]} → what that schema shows; with a Command, the schema does it and
	                // the answer is the frame (Schema: ibProtocolSchema; Command: the schema's own); one the client's mode
	                // does not offer is refused
	Execute,        // {Client, Command[, Type]} — an object of the configuration, as the navigation executes it
	Event,          // {Client, Control, Event[, Args][, Form]} — Event: ibProtocolEvent; Form: the Key of the form drawn,
	                // when it is not the tab's own (a start page's cell)
	Respond,        // {Client, Id, Response} — Id: the pending request's
	Fetch,          // {Client, Control, Request[, Form]} → what the control's Fetch writes: a table's rows, a sheet
	Activate,       // {Client, Tab} — Tab: the tab's id, its NodeId in the frame's Tabs
	Close,          // {Client, Tab}
	Upload,         // {Client, Name, Data} → File: a new temporary file, Data its first part; {Client, File, Data} → File:
	                // its next part (Data: base64)
	Download,       // {Client, File, Part} → Name, Part, Parts, Data — the file's part, from 0, of how many there are
	Open,           // {Client, File} → the frame: the file opened by the template its name says (the frame's
	                // Templates); {Client, Extension} → the frame: a new document of the template that opens that extension
	Command,        // {Client, Command} → the frame: a command of the frame's Menu done on the active tab's document
	                // (Command: ibProtocolCommand); one not enabled now is refused
	Presentation,   // {Client, Value[, Format]} → Text: a value as a person reads it, the server formatting it — Value:
	                // as the server serialises a value (a field's state carries one); Format: the codes Format() takes
};

// The name a method goes by on the wire.
inline const char* ibProtocolMethodName(ibProtocolMethod method)
{
	switch (method) {
	case ibProtocolMethod::Unknown:      return "";
	case ibProtocolMethod::Login:        return "login";
	case ibProtocolMethod::Logout:       return "logout";
	case ibProtocolMethod::Frame:        return "frame";
	case ibProtocolMethod::Schema:       return "schema";
	case ibProtocolMethod::Execute:      return "execute";
	case ibProtocolMethod::Event:        return "event";
	case ibProtocolMethod::Respond:      return "respond";
	case ibProtocolMethod::Fetch:        return "fetch";
	case ibProtocolMethod::Activate:     return "activate";
	case ibProtocolMethod::Close:        return "close";
	case ibProtocolMethod::Upload:       return "upload";
	case ibProtocolMethod::Download:     return "download";
	case ibProtocolMethod::Open:         return "open";
	case ibProtocolMethod::Command:      return "command";
	case ibProtocolMethod::Presentation: return "presentation";
	}
	return "";
}

// …and the method a name means; Unknown for a name the protocol does not have.
inline ibProtocolMethod ibProtocolMethodFromName(const char* name)
{
	for (int method = static_cast<int>(ibProtocolMethod::Login); method <= static_cast<int>(ibProtocolMethod::Presentation); ++method) {
		if (std::strcmp(name, ibProtocolMethodName(static_cast<ibProtocolMethod>(method))) == 0)
			return static_cast<ibProtocolMethod>(method);
	}
	return ibProtocolMethod::Unknown;
}

// WHY A CALL IS REFUSED — what a client branches on; the text beside it is for a person and may change. On the wire
// it is the JSON-RPC error's code — in the body, never an HTTP status: a WebSocket's messages and a call in process
// have none, and a refusal must arrive the same by every transport. (The envelope's own — a message that is not
// JSON-RPC, a method name the protocol does not have — is JSON-RPC's code, said by the port; a call in process has no
// envelope, and a method it does not have is NotFound.) The numbers are HTTP's by meaning, so a client's author reads
// them without a table; where two reasons share one, the method tells them apart (only login is refused for the
// password or the start).
enum class ibProtocolRefusal : int {
	None         = 0,
	BadParameter = 400,   // a parameter missing or malformed: Data not base64, a part over the limit, a new file without a Name
	LoginRefused = 401,   // login: the user or the password
	NoSession    = 401,   // any other call: no such client, or its session is gone — log in anew
	NoRight      = 403,   // the person has no right to it
	StartRefused = 403,   // login: the application refused to start for this person
	NotFound     = 404,   // no such client's thing: schema, command, item, tab, file, part, request
	NotNow       = 409,   // offered, but not now: the active document does not take the command
	Failed       = 500,   // the server could not do it: a part not written, a frame not drawn
};

// THE MODE a client works in — login's Mode: which application's frame it gets. The desktop had a program per mode;
// the server has one door, and a client's frame is made for one mode.
enum class ibProtocolMode : int {
	Runtime  = 1,   // the application, as enterprise.exe ran it
	Designer = 2,   // the configuration, as designer.exe edited it
};

// A dialog of the desktop held as data — `schema {Schema}` shows it, `schema {Schema, Command, Args}` does what was
// picked in it. Which of them a client's application offers is the frame's Schemas.
enum class ibProtocolSchema : int {
	AllFunctions = 1,   // the runtime's: Groups (Title, Icon) of items (Item, Title, Icon); command 1 Open {Item, Type}
	ActiveUser   = 2,   // both modes': Sessions (User, Application, Type, Started, Computer, Session) and Locks
	                    // (Namespace, Key, Mode, User, Acquired, Lock)
	Sections     = 3,   // the runtime's section panel: Sections (NodeId, Title, Picture) with their Blocks (Column 1|2,
	                    // Title, Picture, Tooltip) of items (Item, Title, Icon, Type); command 1 Open {Item, Type}
	About        = 4,   // both modes': Build, Picture, Database, Application, User, Locale and Plugins (Name, Version)
};

// A schema's own commands — `schema {Schema, Command, Args}`. All functions and Sections take the one: Open
// {Item, Type} — an item they show, opened as the navigation opens it.
enum class ibProtocolSchemaCommand : int {
	Open = 1,
};

// What the server asks the person — the frame's Request, by its Kind; answered with `respond {Id, Response}`.
enum class ibProtocolRequestKind : int {
	Message = 1,   // Text, Caption, Style (wx's button flags) → Button (wx's code: 2 Yes, 8 No, 4 OK, 16 Cancel)
	Choice  = 2,   // Caption and children Id, Caption, Picture, Selected → Id — one of those offered; none — cancelled.
	               // With Multiple → Ids — those of them ticked
	Help    = 3,   // Title, Text → nothing
	Edit    = 4,   // Command (ibProtocolCommand: Cut … SelectAll) — done to the field under the focus → nothing
	File    = 5,   // Caption and children Title, Mask — a file of the person's chosen, uploaded → File; none — cancelled
	Generation = 6,   // the desktop's ibDialogGeneration: Picture (the window's) and children Id, Caption, Picture — the
	                  // objects this one generates → Id — the one to base a new one on; none — cancelled
	ViewMode   = 7,   // the desktop's wxTableViewModeDialog: Picture (the window's), Mode (ibProtocolViewMode) — the one in
	                  // force → Mode — the one chosen; none — cancelled
	SavedSettings = 8,   // the desktop's ibDialogSavedSettings: Mode (0 save, 1 restore), Key, Default and children Id,
	                     // Caption — the shelf → Act (ibProtocolSettingsAct), Id, Name — done by the server, which asks
	                     // again with the shelf as it now stands and Done; none — closed
	Menu = 9,   // the desktop's popup menu at the cursor: children Id, Caption, Checked, Enabled (false — greyed) and
	            // their own children — a submenu → Id — the item picked; none — dismissed
	FormEditor = 10,   // the desktop's ibDialogFormEditor over the active tab's form, its controls as the frame has them:
	                   // Picture, HasSetting, Result (the act before's), Classes (Name, Picture) → Act
	                   // (ibProtocolFormEditorAct) — Apply with Controls (Control, Position and the allowed properties — a
	                   // saved form setting's shape) — done by the server, which asks again; none — closed
	ListSettings = 11,   // the desktop's ibDialogListSettings over a table's model: Settings — the setting in force, as
	                     // ibSettingsDescriptionMemory writes it — Fields, its columns (children Name, Presentation, Id, Type —
	                     // as ibTypeDescriptionMemory writes it — Available), and References: which types are references and
	                     // to what (children Type — its class id as text — and Targets, the metaobjects) → Settings — the
	                     // setting edited, which the server puts on and asks no more; Error beside the setting when it refused
	                     // one; none — cancelled. On the way: Act Expand (ibProtocolListSettingsAct) with Targets — asked
	                     // again with Expanded, the fields of those targets (children as Fields') and their References
	SimpleChoice = 12,   // the desktop's ibTypeControlFactory::SimpleChoice under the field pressed: Value — its value with its
	                     // type ({t, v}), a number (the calculator) or a date (the calendar) → Value — the one chosen; none —
	                     // dismissed
};

// What a form editor did — its answer's Act.
enum class ibProtocolFormEditorAct : int {
	Apply = 1,   // the form as Controls say it stands, put on
	Save  = 2,   // the person's arrangement saved for this form
	Reset = 3,   // …and dropped: the form comes back as the author made it on the next open
};

// What a list's settings window asked on the way — its answer's Act.
enum class ibProtocolListSettingsAct : int {
	Expand = 1,   // a reference field opened: the fields of its Targets
	Choose = 2,   // a value of a type of the server's chosen (Type — its class id as text, Value — the one held, Choice — the
	              // number the client asks it under): its quick choice, or its choice form — a tab like any other; what
	              // is chosen comes back in the window's question as Chosen (Choice, and Value with its Presentation
	              // beside {t, v}), whenever it is
};

// How a save or a reset of a form's setting went — the request's Result (the server's ibFormSettingsResult).
enum class ibProtocolFormSettingsResult : int {
	Ok        = 0,
	NoStorage = 1,   // no settings in this session
	NoAddress = 2,   // a form generated from its data — nothing to keep a setting under
	Refused   = 3,
};

// What a saved settings' window did to the shelf — its answer's Act.
enum class ibProtocolSettingsAct : int {
	Save    = 1,   // what is in force put into Id (none — a new entry), named Name
	Restore = 2,   // Id put in force
	Default = 3,   // Id restored on open (none — nothing)
	Rename  = 4,   // Id named Name
	Remove  = 5,   // Id dropped
};

// The commands — `command {Command}`, each done by the server; those whose subject is the client's (the field under the
// focus) the server asks back of it (an Edit request). Named here the ones this client names itself: a document's, which
// its doc manager does through the server (Undo, Redo, Save — a tab's Commands say which its document takes now), Exit,
// which closing its window is, and those an Edit request names.
enum class ibProtocolCommand : int {
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
};

// The picture a menu item or a tool is shown with — one of the client's stock pictures, by number (StockPicture).
enum class ibProtocolStockPicture : int {
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

// What a tab's view shows — the tab's Kind: the view of its own a client draws it with. Absent — a form.
enum class ibProtocolViewKind : int {
	Form        = 1,
	Text        = 2,   // View: Text, ReadOnly; what was typed handed back as event {Tab, Event: Change, Args: {Text}}
	Spreadsheet = 3,
};

// WHAT A PERSON DID WITH A CONTROL of a view — `event {Control, Event, Args}`, the control named by its ControlId.
// A TABLE'S CELL IS A FIELD ON A ROW: the field's events name the cell — Row (a handle) and Column (the column's
// control id) — and mean what they mean on a form's field.
enum class ibProtocolEvent : int {
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
	Move    = 11,   // dragged to another place: a notebook page's tab (Position); a table's column (Column, Holder: the
	                // group or 0, Position)
	Row     = 12,   // a table's cursor moved (Row: its handle; Column — across to that column)
	Sort    = 13,   // a table's column header clicked (Column: its control id)
	Cell    = 14,   // a spreadsheet cell clicked (Row, Col)
	Resize  = 15,   // a table column's edge dragged (Column, Width: the width it asks for)
	Action  = 16,   // one of the control's own commands, picked in its context menu (Id: the command's — a table's
	                // standard commands, its State's ContextMenu)
};

// What a button shows — its State's Representation (Auto is resolved by the server before it is written).
enum class ibProtocolRepresentation : int {
	Auto           = 0,
	Text           = 1,
	Picture        = 2,
	PictureAndText = 3,
};

// What an entry of a command bar is — its Kind.
enum class ibProtocolCommandKind : int {
	Command     = 0,   // a command: pressed, it runs
	QuickFilter = 1,   // a filter by a value: pressed, the server asks for the value
	Group       = 2,   // a group: its commands are its children, one of them pressed (Member — its place among them)
};

// How a table shows its rows — its State's ViewMode.
enum class ibProtocolViewMode : int {
	Tree         = 0,   // the whole hierarchy, a folder opened in place
	Hierarchical = 1,   // one level, a folder entered
	List         = 2,   // every row, flat
};

// How a message to the person is meant — its Level.
enum class ibProtocolMessageLevel : int {
	Information = 1,
	Warning     = 2,
	Error       = 3,
};

#endif
