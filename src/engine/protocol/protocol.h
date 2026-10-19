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
	inline constexpr const char* Users    = "Users";   // users' answer: the names the login window offers
	inline constexpr const char* Token    = "Token";   // login's bearer, once, in the password login's answer. The
	                                                   // database keeps its hash (docs/public/client-protocol.md)
	inline constexpr const char* Mode     = "Mode";
	inline constexpr const char* Protocol = "Protocol";
	inline constexpr const char* Features = "Features";
	inline constexpr const char* FeatureResume = "resume";   // the same-process window only. A client names it in
	                                                          // login's Features to opt into the idempotency slot.
	inline constexpr const char* Read     = "Read";      // login: the schemas the client reads at its start ({Schema}),
	                                                   // answered beside the frame, each with its Schema
	inline constexpr const char* Client   = "Client";
	inline constexpr const char* Since    = "Since";
	inline constexpr const char* Pending  = "Pending";   // (protocol 3) the state events held since the last call, in
	                                                     // the order done — each as an event's params; applied first.
	                                                     // An assistant tab's State: the messages left for an assistant
	                                                     // that none has collected yet

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
	inline constexpr const char* MetaData  = "MetaData";   // the designer's: its configuration's tree, the node itself its
	                                                       // root row (ibProtocolSchema::MetaData)
	// …the root row's: the configuration's state, as the engine answers it — Modified (ibMetaData::IsModified), Edited
	// (IsEdited: somebody edited it), ConfigSave (ibMetaDataConfigurationBase::IsConfigSave: the database holds it),
	// Syntax — the code style its modules are written in.
	inline constexpr const char* Modified   = "Modified";
	inline constexpr const char* Edited     = "Edited";
	inline constexpr const char* ConfigSave = "ConfigSave";
	inline constexpr const char* Syntax     = "Syntax";
	inline constexpr const char* Synonym   = "Synonym";    // …a row's: its object's synonym, when it has one
	// …the root row's: the kinds whose objects open an editor (MetaData Open) — children Type; none: ask Open for any
	inline constexpr const char* Editors   = "Editors";
	inline constexpr const char* FormType  = "FormType";   // …a form's row: its kind (the owner's form type)
	inline constexpr const char* Comment   = "Comment";    // …an external file's root row: its comment
	inline constexpr const char* DefaultFormObject = "DefaultFormObject";   // …the default form's id (none: wxNOT_FOUND)
	inline constexpr const char* DefaultComposer   = "DefaultComposer";     // …a report's default composer's id
	// …a row's Menu: what its object offers to open (the engine's ibValueMetaObject::CollectContextMenu) — its children
	// the items: Kind (ibMetaMenuKind: 0 a module, 1 an object, 2 a modal editor), Name, Title, Object and Type,
	// Picture (none: the object's class's), Command (the editor's number, ProcessCommand; none: an object opened).
	inline constexpr const char* Object    = "Object";     // …an item's: the metaobject it opens, by its id
	inline constexpr const char* Standard  = "Standard";   // …the Menu's: false — the tree's New / Edit / Remove /
	                                                       // Properties do not apply to the row (absent: they do)
	// The designer's inspector: the object it shows (NodeId, Tab — an edit goes back by schema Properties), its categories
	// (Name, Title) and property rows — Name, Title, Help, Class, Value, Presentation, ReadOnly, and what a property may
	// be set to (its children Id, Title, Picture; Multiple — several at once). What its object's state hides is not sent.
	// …and an external file's root row's: the header above its tree — its object's rows, as the inspector's. And an event
	// of the frame's answer: the rows of each object a door acted on (an edit, a new object, the one the tree selects
	// after a removal, a form editor's own), each by its NodeId and Tab — the client shows them without reading them.
	inline constexpr const char* Properties = "Properties";
	inline constexpr const char* Help       = "Help";     // …a property row's: what it is for, as the inspector says it
	inline constexpr const char* Class      = "Class";    // …a property row's: the server's property class by name
	                                                      // (ibPropertyBoolean) — what the client's registry shows it by
	inline constexpr const char* Sources    = "Sources";  // …a choice parameters row's: the fields beside it that may
	                                                      // supply a value (Title, the holder they stand in)

	// What the frame's parts carry: a menu item, a tab, a message, a question and its answer.
	inline constexpr const char* Separator    = "Separator";
	inline constexpr const char* Command      = "Command";    // …a menu item's beside its Schema: that schema's
	                                                          // command, done as `schema {Schema, Command}` does it
	inline constexpr const char* Schema       = "Schema";
	inline constexpr const char* Shortcut     = "Shortcut";   // a command's default key — the user's own go over it, client-side
	inline constexpr const char* StockPicture = "StockPicture";   // …or the item's Picture, by its id
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
	inline constexpr const char* Select    = "Select";    // MetaData Remove: the object the tree selects after it — its
	                                                    // Properties shown with the answer
	inline constexpr const char* Multiple  = "Multiple";   // a choice's: several may be chosen — answered with Ids
	inline constexpr const char* Ids       = "Ids";
	inline constexpr const char* Act       = "Act";       // a saved settings' answer: what was done to the shelf
	inline constexpr const char* Default   = "Default";   // …the entry restored on open
	inline constexpr const char* Done      = "Done";      // …the shelf asked again: how the act before went; a print's answer
	inline constexpr const char* Schedule  = "Schedule";  // a schedule's editor: the schedule, in its stored form
	inline constexpr const char* Controls   = "Controls";     // a form designer's tab (ibProtocolViewKind::FormDesigner):
	                                                          // the form's controls' rows, each by its Control id
	inline constexpr const char* Classes    = "Classes";      // …the classes a control may be made of, as that tab's fetch
	                                                          // writes them
	inline constexpr const char* Node       = "Node";         // …a class's new control, as it is put on the form
	// A form designer's attribute tree — the form's attributes, a row each (its node's id the attribute's) — Name, Main,
	// Path, Targets, TableSection, DropControl, HasComposition; a row under one (the tab's Fetch Composition) the same,
	// and UnderList. A door about one names it by Attribute (0: a new one).
	inline constexpr const char* AttributeTree  = "AttributeTree";
	inline constexpr const char* Attribute      = "Attribute";
	inline constexpr const char* Main           = "Main";             // …the form's main attribute
	inline constexpr const char* Path           = "Path";             // …the hops from the attribute — children Id, Type
	inline constexpr const char* TableSection   = "TableSection";     // …a table or a section: its fields are columns
	inline constexpr const char* DropControl    = "DropControl";      // …the class a drop of it makes (text, a clsid)
	inline constexpr const char* HasComposition = "HasComposition";   // …it has rows under it, asked for when opened
	inline constexpr const char* UnderList      = "UnderList";        // …it is in a list: a column, not a source
	// …and its command tree — the desktop's three sections (the form's own commands first, then the standard and the
	// global ones), a row each with its Name; under one, a source's sub-group rows and the command rows — Name, Picture,
	// CommandPath, Kind (ibInterfaceCommandType), a form command's row its node id the command's. A door about a form
	// command names it by FormCommand (0: a new one).
	inline constexpr const char* CommandTree    = "CommandTree";
	inline constexpr const char* FormCommand    = "FormCommand";
	inline constexpr const char* CommandPath    = "CommandPath";      // …the hops of a command — children Id
	// …and where the form's node keeps them, as the engine writes it (ibValueForm::WriteAttributes, WriteFormCommands):
	// a node each, its id the attribute's, the command's — what a copy of one is.
	inline constexpr const char* Attributes     = "Attributes";
	inline constexpr const char* FormCommands   = "FormCommands";
	// …and a control's node, its layers (ibValueWindowComposite::WriteData): the command bar (CommandBar — its AutoFill,
	// a child per command).
	inline constexpr const char* Layers         = "Layers";
	inline constexpr const char* Position   = "Position";     // …where a control stands among its siblings
	inline constexpr const char* HasSetting = "HasSetting";   // …a setting of the person's is saved for this form
	inline constexpr const char* Result     = "Result";       // …how a save or a reset went (ibProtocolFormSettingsResult)
	inline constexpr const char* Settings   = "Settings";     // a list's settings window: the setting, as the schema writes it
	inline constexpr const char* Composition = "Composition";   // …a report's: the composition it arranges
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
	inline constexpr const char* Activity    = "Activity";
	inline constexpr const char* Namespace   = "Namespace";
	inline constexpr const char* Key         = "Key";
	inline constexpr const char* Acquired    = "Acquired";
	inline constexpr const char* Lock        = "Lock";
	inline constexpr const char* Build       = "Build";
	inline constexpr const char* Plugins     = "Plugins";

	// The base's users (ibProtocolSchema::UserList) — a user's record as the engine's ibUserInfo holds it, and the
	// configuration's roles and languages it is chosen from.
	inline constexpr const char* Guid        = "Guid";
	inline constexpr const char* FullName    = "FullName";
	inline constexpr const char* Language    = "Language";
	inline constexpr const char* Languages   = "Languages";
	inline constexpr const char* Roles       = "Roles";
	inline constexpr const char* Code        = "Code";

	// The predefined items of an object (ibProtocolSchema::Predefined) — Items, each as the engine stores it
	// (ibValueMetaObjectRecordDataHierarchyMutableRef::WriteData): Guid, Name, Code, Description, IsFolder, Parent (the
	// Guid of the item it sits under; absent: the top).
	inline constexpr const char* Items       = "Items";
	inline constexpr const char* Description = "Description";
	inline constexpr const char* IsFolder    = "IsFolder";

	// The two texts of an object (ibProtocolSchema::ObjectText) — Help, what its user is told (the name a property row's
	// help has too), and Note, what its designers are.
	inline constexpr const char* Note        = "Note";

	// The registration journal — a tab's (Kind AuditLog): its rows fetched by the tab, newest first, by the filter
	// {From, To, Level, User, Session, Source, Event, Ref, Search, Offset, Count} — each row Time, Level, Session, User,
	// Host, Source, Event, Message, Ref, RefMeta; a row's object opened as event Open {Ref, RefMeta}.
	inline constexpr const char* From    = "From";      // ms since the epoch, inclusive — 0: no bound
	inline constexpr const char* To      = "To";        // …exclusive
	inline constexpr const char* Source  = "Source";
	inline constexpr const char* Search  = "Search";    // a part of the message
	inline constexpr const char* Offset  = "Offset";    // the rows skipped — Count read after them
	inline constexpr const char* Time    = "Time";      // a row's: ms since the epoch
	inline constexpr const char* Host    = "Host";
	inline constexpr const char* Message = "Message";
	inline constexpr const char* Ref     = "Ref";       // …the object it is about: its guid
	inline constexpr const char* RefMeta = "RefMeta";   // …and its metaobject's id — 0: a user (sys_user)

	// Debugging in the current session — the schema's (Debugging): whether it is on, and whether it may be switched.
	inline constexpr const char* Debugging = "Debugging";   // the session's process listens for the designer
	inline constexpr const char* Allowed   = "Allowed";     // …and may be switched now: not on the application server,
	                                                        // not while a start waits for the designer

	// The designer's debugger's store (ibProtocolSchema::Debugger): a module by its address — Module, its lines, and its
	// Breakpoints (Line, Condition). A module view's State says its Module too.
	inline constexpr const char* Module      = "Module";
	inline constexpr const char* Line        = "Line";
	inline constexpr const char* Breakpoints = "Breakpoints";
	inline constexpr const char* Condition   = "Condition";

	// The designer's assistant access — the schema's (McpServer), beside Enabled, Port and Token: where it listens,
	// whether it does, and what a person hands their tool.
	inline constexpr const char* Address  = "Address";
	inline constexpr const char* Running  = "Running";
	inline constexpr const char* Endpoint = "Endpoint";   // empty while it is not running
	inline constexpr const char* Waiting  = "Waiting";    // an assistant tab's State: what the person sent has no answer
	                                                      // yet (ViewKind Assistant)

	// A view — a tab's form, its controls as the server writes them (visualView/): what each was saved with, beside its
	// State, what the server reads it as now; and what the person did with one, back by its id.
	inline constexpr const char* ControlId        = "ControlId";
	inline constexpr const char* State            = "State";
	inline constexpr const char* Opening          = "Opening";   // a form editor's State, once, in the frame that opens
	                                                               // its tab: what its Text fetch answers
	inline constexpr const char* Subscribed       = "Subscribed";   // a State's: the state events the server is subscribed
	                                                                // to on it — sent at once, by number
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
	inline constexpr const char* Rows             = "Rows";   // a sizer's: how many; (protocol 5) a table State's: the first page
	                                                            // — same shape as fetch (rows as children, End, Before, Pictures)
	inline constexpr const char* Cols             = "Cols";
	inline constexpr const char* TitleLocation    = "TitleLocation";
	inline constexpr const char* PasswordMode     = "PasswordMode";
	inline constexpr const char* MultilineMode    = "MultilineMode";
	inline constexpr const char* ButtonSelect     = "ButtonSelect";
	inline constexpr const char* ButtonOpen       = "ButtonOpen";
	inline constexpr const char* ButtonClear      = "ButtonClear";
	inline constexpr const char* OrientPage       = "OrientPage";   // a notebook's: where its tabs are (wxAUI_NB_TOP, _BOTTOM)
	inline constexpr const char* ActivePage       = "ActivePage";   // …the State's: the page selected, by its control id; 0 — none
	inline constexpr const char* Page             = "Page";         // a page picked, by its control id (event Page); an HTML
	                                                                // box's State: the page it shows
	inline constexpr const char* Range            = "Range";        // a gauge's: the value it is full at
	inline constexpr const char* MinValue         = "MinValue";     // a slider's: its ends
	inline constexpr const char* MaxValue         = "MaxValue";

	// A command bar — the State's CommandBar of a form, a table, a spreadsheet: its entries (Id, Caption, Picture,
	// Tooltip, Enabled, Kind; a separator; a group's commands as its children), pressed back as Command {Id, Member}.
	inline constexpr const char* CommandBar  = "CommandBar";
	// …and in a form editor's control rows (Controls), the bar's rows (CommandBar — and Items, each command's of it by its
	// Name) and a new command's, with the node it is (Node) — the bar's commands the client adds.
	inline constexpr const char* CommandBarItem = "CommandBarItem";
	inline constexpr const char* ContextMenu = "ContextMenu";   // a table's: its standard commands (Id, Caption, Picture,
	                                                            // Enabled), picked back as Action {Id}

	// A table: its columns (and their groups) in the view, its rows fetched — {Direction, Anchor, Parent, Count} → rows
	// by their handles (Container, Group, ReadOnly, Picture — by its id) of cells by the columns' ids (Text, Checked,
	// Number), End, CurrentRow.
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
	inline constexpr const char* Before           = "Before";   // (protocol 4) a reset fetch's: the rows wanted ahead of
	                                                            // the page — answered with how many stand first
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
	inline constexpr const char* ComposeState = "ComposeState";   // the State's: the sheet to its report (ibProtocolComposeState)
	inline constexpr const char* GridLines  = "GridLines";   // the State's: its grid lines shown — a document's sheet
	inline constexpr const char* GridArea   = "GridArea";    // the State's: its areas shown — a template's sheet
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
	// …whether it is made there when there is none yet (the designer's)…
	inline constexpr const char* Create    = "Create";
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
	inline constexpr const char* Notebook            = "Notebook";
	inline constexpr const char* NotebookPage        = "NotebookPage";
	inline constexpr const char* Radiobutton         = "Radiobutton";
	inline constexpr const char* Slider              = "Slider";
	inline constexpr const char* Gauge               = "Gauge";
	inline constexpr const char* Htmlbox             = "Htmlbox";
	inline constexpr const char* Chartbox            = "Chartbox";

}

// The version of the protocol — the newest both sides here speak; a client names its own at login, and the two go on
// with the older. Raised when a change needs it, never for a field added: an absent field is its default.
//   1  a patch is from the frame last sent;
//   2  …and its View from the view last sent for the tab active NOW, when the client was sent one; else from the
//      last frame's, as in 1. A client keeps each tab's view, puts the one of the tab the patch makes active in
//      place (keeping the view it holds when it has none for that tab), and applies the patch's View to it; a frame
//      sent whole begins the store again — back on a tab, it is sent what changed there, not the whole form.
//   3  …and a state event (ibProtocolIsStateEvent) its control's State does not list in Subscribed is held by the client,
//      the latest of each kind per control, and carried by its next call, whatever it is, as Pending — which the
//      server applies, in order, before the call itself.
//   4  …and a reset fetch (Direction 0) may name Before: the server reads up to that many rows ahead of the page too,
//      puts them first, and answers Before — how many of the rows stand ahead of it. An answer without Before is a
//      server below 4: the client reads them itself, a second fetch.
//   5  …and a table's State may carry Rows with the Version: the first page, as fetch would give it (rows as children,
//      End, Before, Pictures), so the frame that opens the tab need not be followed by a fetch. A server below 5, or
//      one that omits Rows, is answered with fetch, as before.
constexpr int ibProtocolVersion = 5;

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
	Users,          // {} → Users: the names the login window offers. Asked before the login, so it names no Client
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
	Activate,      // {Client, Tab} — Tab: the tab's id, its NodeId in the frame's Tabs
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
	// ⭐ A PICTURE IS NAMED BY ITS ID, EVERYWHERE — an answer carries the id alone, and the picture is loaded by this, apart:
	Picture,        // {Client, Pictures} → Pictures: the pictures of those ids, each {Id, Picture} (PNG, base64) — a backend
	                // one by its number (a registered picture, a class's own: its class id), a configuration's by its guid;
	                // several at once, asked by a client that has not got them and kept by it. One there is none of comes
	                // back empty, not refused
};

// The name a method goes by on the wire.
inline const char* ibProtocolMethodName(ibProtocolMethod method)
{
	switch (method) {
	case ibProtocolMethod::Unknown:      return "";
	case ibProtocolMethod::Login:        return "login";
	case ibProtocolMethod::Users:        return "users";
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
	case ibProtocolMethod::Picture:      return "picture";
	}
	return "";
}

// …and the method a name means; Unknown for a name the protocol does not have.
inline ibProtocolMethod ibProtocolMethodFromName(const char* name)
{
	for (int method = static_cast<int>(ibProtocolMethod::Login); method <= static_cast<int>(ibProtocolMethod::Picture); ++method) {
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
	ActiveUser   = 2,   // both modes': Sessions (User, Application, Type, Started, Computer, Session, Activity) and
	                    // Locks (Namespace, Key, Mode, User, Acquired, Lock); the designer's commands 5 Kick {Session}
	                    // — that session closed, 6 Reload {Session} — its clients reloaded, 7 Release {Lock} — that
	                    // lock released by force
	Sections     = 3,   // the runtime's section panel: Sections (NodeId, Title, Picture) with their Blocks (Column 1|2,
	                    // Title, Picture, Tooltip) of items (Item, Title, Icon, Type); command 1 Open {Item, Type}
	About        = 4,   // both modes': Build, Picture, Database, Application, User, Locale and Plugins (Name, Version)
	AuditLog     = 5,   // both modes': the registration journal — shows nothing; command 1 Open opens its tab (Kind
	                    // AuditLog)
	Debugging    = 6,   // the runtime's options: Debugging, Allowed; commands 2 Start and 3 Stop
	McpServer    = 7,   // the designer's assistant access, the person's: Enabled, Address, Port, Token, Running,
	                    // Endpoint, Allowed; commands 2 Start {Enabled, Address, Port, Token} — started on those, 3 Stop,
	                    // 4 Save {…} — kept as the person's
	UserList     = 8,   // the designer's users of the base: Users (Guid, Name, FullName), and the configuration's Roles
	                    // (Guid, Name, Title, Mode) a user's record is chosen from (its languages — schema Languages);
	                    // `Args: {Guid}` — that one's User {Guid, Name, FullName, Password — whether one is stored
	                    // (never the stored one), Language — its guid, Roles (Guid)}; commands 4 Save {Guid, Name,
	                    // FullName, Language, Roles (Guid)[, Password — a new one, hashed by the server; absent: the
	                    // stored one kept]}, 8 Remove {Guid}
	MetaData     = 9,   // the designer's configuration tree — shows nothing: its rows ride the frame (MetaData, each
	                    // row NodeId — its object's id, or below 0 a group row's place among its siblings; Title,
	                    // Picture, Group — what New makes under it, Type, Synonym, Position, Guid — its object's; a
	                    // language's Code, a role's Mode; the root's Editors — the kinds Open opens an editor of). Its commands are its own (ibProtocolMetaDataCommand), each
	                    // with Tab in that tab's tree — an external file's (ViewKind DataProcessor, Report)
	Properties   = 10,  // the designer's inspector: `Args: {Id, Tab}` — the metaobject of that id in the tab's tree (Tab:
	                    // an external file's; none: the configuration's), its Properties (NodeId, Tab; categories Name,
	                    // Title; properties Name, Title, Help, Class — the property's class as the source names it, Value
	                    // as its node holds it, Presentation, ReadOnly, what it may be set to — children Id, Title,
	                    // Picture; Multiple; what the object's state hides not sent). Save `{Id, Tab, Name, Value}` —
	                    // that object's property or event given that value
	Languages    = 11,  // both modes': the configuration's Languages (Guid, Title, Code) — what a translated text is
	                    // written in (the translation window's boxes, a user's record's choice)
	Predefined   = 12,  // the desktop's predefined values editor over an object of the designer's tree: `schema {Schema,
	                    // Args: {Id[, Tab]}}` shows Type (the object's class) and Items (Guid, Name, Code, Description,
	                    // IsFolder, Parent); commands 9 Create {Id, Name, Code, Description, IsFolder, Parent}, 10 Set {Id,
	                    // Guid, …} — the item of that Guid given these (none of it: one more, of that Guid), 8 Remove {Id,
	                    // Guid}. Each with Tab — an external file's tree, as MetaData's
	ObjectText   = 13,  // the desktop's help and technical information editors over an object of the designer's tree:
	                    // `Args: {Id[, Tab]}` shows Help and Note — its two texts; command 4 Save {Id, Help} or {Id,
	                    // Note} — the one given set. Each with Tab, as Predefined's
	HomePage     = 14,  // the desktop's home page workspace editor over the configuration's root: `Args: {Id}` shows
	                    // HomePage — as the root writes it (ibHomePageDescription), ReadOnly — when it takes no edit, and
	                    // Forms (Id, Name, FullName, Picture, Common; a form's owner: Owner, OwnerName, OwnerPicture) — what
	                    // it may show; command 4 Save {Id, HomePage}
	Debugger     = 15,  // the designer's debugger's store — the debugger is the thin designer's own, the engine's client
	                    // carried over; the configuration and its sys_dbg are here: shows the configuration's Guid and
	                    // Rows (Module, Count — its lines, Breakpoints); `Args: {Module}` — that module's Breakpoints. Its
	                    // commands ibProtocolDebuggerCommand
	Database     = 16,  // the designer's base as a whole (Administration → Restore, Dump, Clear database) — shows nothing;
	                    // commands 11 Load — restored from a file the person is asked for (Request File), every session
	                    // of the base closed after; 4 Save — dumped, and taken down where the person chooses; 12 Clear —
	                    // asked first. Each said to the person by the server
	Assistant    = 17,  // the designer's assistant window (Administration) — shows nothing; command 1 Open opens its tab
	                    // (ViewKind Assistant). Allowed while the base's assistant access is running
};

// A schema's own commands — `schema {Schema, Command, Args}`. All functions and Sections take the one: Open
// {Item, Type} — an item they show, opened as the navigation opens it. Debugging takes Start and Stop; McpServer takes
// them and Save; Active users, the designer's, Kick, Reload and Release; the users Save and Remove; the designer's
// inspector (Properties) Save; its predefined values editor Create, Set and Remove; its texts (ObjectText) Save, and so the
// home page (HomePage); the base (Database) Load, Save and Clear; the assistant window (Assistant) Open. The designer's
// tree has its own (ibProtocolMetaDataCommand).
enum class ibProtocolSchemaCommand : int {
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
};

// THE DESIGNER'S TREE'S COMMANDS — `schema {Schema: MetaData, Command, Args}`, its doors by an object's id, each with Tab
// in an external file's tree. Numbered of their own: the schema reads them, no other.
enum class ibProtocolMetaDataCommand : int {
	Open     = 1,    // {Id} — the object's editor; or {Module} — the module of that address (a stop, a failure)
	Create   = 2,    // {Type, Parent} — a new one of that class under that object (0: the configuration's own), opened
	Remove   = 3,    // {Id}
	Move     = 4,    // {Id, Position} — put at that place among its parent's children
	Copy     = 5,    // {Id, File} — written into that file (uploaded empty)
	Paste    = 6,    // {Type, Parent, File} — read from that one
	Save     = 8,    // the configuration written
	// …the configuration's own (the desktop's Configuration menu), each said to the person by the server; refused in an
	// external file's tree:
	Rollback = 9,    // taken back from the base
	Apply    = 10,   // written and the base restructured, its ledger asked first (Request ApplyChange)
	Load     = 11,   // {File} — read from that uploaded file
	Unload   = 12,   // {File} — written into that one (uploaded empty)
};

// THE DEBUGGER'S STORE'S COMMANDS — `schema {Schema: Debugger, Command, Args}`: the engine's ibDebuggerClient's rows of
// sys_dbg (debugClientQuery.cpp), a breakpoint by its module and its committed line. Numbered of their own, as the tree's.
enum class ibProtocolDebuggerCommand : int {
	ToggleBreakpoint    = 1,    // {Module, Line, Condition} — set, or its condition replaced
	RemoveBreakpoint    = 2,    // {Module, Line}
	RemoveAllBreakpoint = 3,
};

// What the server asks the person — the frame's Request, by its Kind; answered with `respond {Id, Response}`.
enum class ibProtocolRequestKind : int {
	Message = 1,   // Text, Caption, Style (wx's button flags) → Button (wx's code: 2 Yes, 8 No, 4 OK, 16 Cancel)
	Choice  = 2,   // Caption and children Id, Caption, Picture, Selected → Id — one of those offered; none — cancelled.
	               // With Multiple → Ids — those of them ticked
	Edit    = 4,   // Command (ibProtocolCommand: Cut … SelectAll) — done to the field under the focus → nothing
	File    = 5,   // Caption and children Title, Mask — a file of the person's chosen, uploaded → File; none — cancelled.
	               // With File — that one of the server's taken down where the person chose → Done; none — cancelled
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
	ComposerSettings = 13,   // the desktop's ibDialogComposerSettings over a report's model: as ListSettings, and Composition —
	                         // the composition it arranges, as ibCompositionDescriptionMemory writes it; Fields — those its
	                         // query offers (Presentation — the caption its source gives one) → as ListSettings; the report
	                         // waits for Compose (ibProtocolComposeState::Outdated). On the way: the same acts
	                         // (ibProtocolListSettingsAct)
	Error = 14,   // the desktop's ibDialogError over a script's failure: Picture (the window's), Text — the error with its
	              // call stack → Act (ibProtocolErrorAct); none — closed, as Close window
	Print = 15,   // the desktop's PrintSpreadsheetDocument over a script's sheet: Sheet — as a spreadsheet's fetch writes it →
	              // Done — whether it was printed (not: the print dialog cancelled, or the printer failed)
	Schedule = 16,   // the desktop's ibDialogJobSchedule over a form's schedule: Schedule — as
	                 // ibJobScheduleDescriptionMemory writes it → Schedule — the one edited (the form is modified); none —
	                 // cancelled
	ApplyChange = 17,   // the desktop's ibDialogApplyChange over the restructure's ledger, the base held until it is
	                    // answered: Picture (the window's) and children Level (ibRestructure: 0 info, 1 warning, 2 error),
	                    // Text → Done — applied; none — cancelled, everything rolled back
	FormType = 18,   // the desktop's ibDialogSelectTypeForm over a form just made, asked inside the Create that made it:
	                 // Caption, Name, Synonym, Comment — the form's — and children Id, Caption, Name — the kinds its owner
	                 // offers, each with the name the form takes with it → Id, Name, Synonym, Comment — the kind chosen and
	                 // what the person typed; none — cancelled, and the form taken away again before the Create answers
};

// What the person chose over a script's failure — an Error answer's Act. The answers are CUMULATIVE, as the desktop's
// were: each one does what the one before it does, and more.
enum class ibProtocolErrorAct : int {
	CloseWindow  = 1,   // the error kept in the messages
	GotoDesigner = 2,   // …and sent to the designer debugging this base
	CloseProgram = 3,   // …and the client goes
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
// which closing its window is, CloseAll, which its Window menu's Close All is, and those an Edit request names.
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
	CloseAll  = 13,   // every tab but the locked ones closed in one call — one kept keeps the rest
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
	AuditLog    = 4,   // the registration journal: its rows fetched by the tab (fetch {Tab, Request: the filter}), a
	                   // row's object opened as event {Tab, Event: Open, Args: {Ref, RefMeta}}
	DataProcessor = 5, // the designer's tree of an external file — of a data processor, of a report: View.MetaData — its
	Report        = 6, // rows, as the frame's MetaData; its doors — schema MetaData with the Tab
	Module        = 7, // the designer's editor of a module: View.State {Syntax, ReadOnly, Modified — the configuration the
	                   // module is in}; the text fetched by the tab (fetch {Tab} — {Text}; what else —
	                   // ibProtocolModuleRequest); what was typed — event {Tab, Event: Input} on the first key while not
	                   // Modified, then {Tab, Event: Change, Args: {Text}}; its commands —
	                   // event {Tab, Event: Command, Args: {Id}} (ibProtocolModuleCommand)
	FormDesigner  = 8, // the designer's editor of a form — a module's tab (State, its requests, its Input and Change {Text})
	                   // whose form the CLIENT edits, as the desktop's editor did: fetch {Tab} — {Text, Form — the form's
	                   // node as a frame draws it, Controls — each control's rows (Control, as the inspector writes them),
	                   // Classes — the classes a control may be made of (Name, Picture, Type; Visible false — not offered
	                   // to place; Menu — the classes a control of it adds in itself, Name and Title each): a control is
	                   // made and pasted by the server (ibProtocolFormDesignerRequest Control), AttributeTree,
	                   // CommandTree}; what else —
	                   // ibProtocolFormDesignerRequest; the form handed back whole — event {Tab, Event: Change, Args: {Form}}
	// THE DESIGNER'S CHECKABLE TREES over a metaobject — what it applies to, ticked: View.State {ReadOnly — the object
	// takes no edit}, View.Rows — the configuration's row, and under it its groups and under them its objects, each NodeId
	// — its object's id (a group: below 0, its place among its siblings), Title, Picture; an object's Checked, ReadOnly
	// (its box greyed); a tick — event {Tab, Event: Toggle, Args: {NodeId, Checked}}
	Interface        = 9,    // a section's — the objects in it
	CommonAttribute  = 10,   // a common attribute's — the objects that carry it
	FunctionalOption = 11,   // a functional option's — the objects and their fields and tables it hides while off
	Role             = 12,   // a role's — every object that has rights, with its Roles (Name, Title, Checked — the right
	                         // given by the role, ReadOnly); a tick names the right — Args: {NodeId, Name, Checked}
	Assistant        = 13,   // the designer's assistant window — the exchange with the assistant access: View.State
	                         // {Waiting — what the person sent has no answer yet, Pending — the messages no assistant has
	                         // collected}, View.Rows — the transcript, an entry a row (Text); what the person writes —
	                         // event {Tab, Event: Change, Args: {Text}}
	Composer         = 14,   // the designer's editor of a report's composer — the composition panel: View.State
	                         // {ReadOnly}, View.Composition — the composer's description in its node form, View.Fields
	                         // and View.References — what its query offers, as the settings questions have them
	                         // (View.Error — why it offers none); the fields of a text being edited, or of a reference's targets — fetch {Tab,
	                         // Request: {Kind (ibProtocolComposerRequest), Text}}; an edit — event {Tab, Event: Input},
	                         // what the panel holds — event {Tab, Event: Change, Args: {Composition}}
};

// WHAT A COMPOSER'S TAB IS ASKED — `fetch {Tab, Request: {Kind, …}}`, numbered past the form's
// (ibProtocolFormDesignerRequest).
enum class ibProtocolComposerRequest : int {
	Fields = 21,   // {Text} — the fields that query text offers: Fields, References, Error — as the tab's view has them
	Expand = 22,   // {Targets} — a reference field opened: the fields of those targets, Expanded and their References —
	               // as a settings window's Act Expand has them asked again
	Source = 23,   // {Id, Text} — a dynamic list's fields: those of its main table (Id, optional) and what its query (Text,
	               // optional) adds — Fields, References, Error — as the desktop's list settings window reads them
};

// WHAT A FORM'S TAB IS ASKED besides its module's — `fetch {Tab, Request: {Kind, …}}`, numbered past the module's
// (ibProtocolModuleRequest), whose requests the same tab answers: what only the server knows of a form edited on the
// client. Where the answer carries the form, it is the form as the server made it — {Form, Controls, AttributeTree,
// CommandTree} as the tab's fetch writes them, and Selected, the control to work on — and the client puts it in as an
// edit of its own: undone on the client.
enum class ibProtocolFormDesignerRequest : int {
	BoundControl   = 11,   // {Form, Control, Path} — a control bound to the source at the path's end, dropped on that
	                       // control → the form
	CommandControl = 12,   // {Form, Control, Value — {CommandPath, Kind}, as the command tree's row [, CommandBar]} — a
	                       // control running the command, dropped on that control (CommandBar: on its bar — an item of
	                       // it) → the form
	Rows           = 13,   // {Form — the form as it stands, Control} — that control after an edit of its properties: Node
	                       // — as the server draws it (its State — enabled, shown), Controls — its rows (some shown or
	                       // hidden)
	Composition    = 14,   // {Path, Targets, UnderList} of an attribute row → Rows, the rows under it (AttributeTree's)
	Attribute      = 15,   // {Form, Command (ibProtocolFormDesignerCommand), Attribute, Column, Name, Value} — a door of
	                       // the attribute tree (Column — of a value table's column) → the form, Attribute and Column —
	                       // the one to show
	FormCommand    = 16,   // {Form, Command, FormCommand, Name, Value} — a door of the command tree → the form,
	                       // FormCommand — the one to show
	Properties     = 17,   // {Form, Attribute [, Column] | FormCommand} — its rows, as the inspector writes them
	Form           = 18,   // {Form} → the form as the server draws it — after an edit of the client's that changes what
	                       // the server draws of it (a command bar's commands)
	Control        = 19,   // {Form, Control, Class} — a control of that class made in that control (none: the form), as
	                       // the desktop's palette made it: one that binds to a source, bound to a fresh attribute; {Form,
	                       // Control, Command Paste, Value} — the copied one put there → the form, Selected — the one put
};

// A door of a form designer's tree — an attribute's, a form command's (ibProtocolFormDesignerRequest Attribute, FormCommand);
// a control's paste (Control).
enum class ibProtocolFormDesignerCommand : int {
	Create  = 1,   // a new one, uniquely named
	Remove  = 2,
	Paste   = 3,   // {Value — a node the client copied of the form's Attributes, FormCommands; a control's, as the form
	               // draws it, in its sizer item} — a new one of it: its ids the server's, its names unique on the form
	Set     = 4,   // {Name, Value} — its property (or event) of that name given that value, as the property's node holds it
	SetMain = 5,   // the form's main attribute — the attribute's
	AddColumn = 6, // a column of the value table the attribute holds (its row's Menu has Column) — uniquely named
};

// WHAT A MODULE'S TAB IS ASKED besides its text — `fetch {Tab, Request: {Kind, Text, Position}}`: what the desktop's code
// editor asked the configuration itself. Text — the editor's text as it is now (not handed back yet), Position — the caret
// in it, in characters.
enum class ibProtocolModuleRequest : int {
	Text       = 0,   // {Text} — the module's, as the server holds it (the request may be absent)
	Complete   = 1,   // rows {Kind (ibContentType), Name, Help} — what may be written at the caret
	CallTip    = 2,   // {Text} — the call form of what is being written at the caret; empty: none
	Procedures = 3,   // rows {Name, Args} — the module's default procedures, its handlers (no Text)
};

// A module tab's commands — event {Tab, Event: Command, Args: {Id}}.
enum class ibProtocolModuleCommand : int {
	SyntaxControl = 1,   // the module checked: "no errors" to the messages, or the error
	TestForm      = 2,   // {Form} — a form designer's: the form opened as the runtime shows it, a tab of its own
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

// ⭐ AN EVENT THAT SAYS WHERE THE PERSON IS — the control in focus, the table's cursor, typing begun, a column's place and
// width. Nothing on the server has to run for it unless the server is subscribed to it (the State's Subscribed), so it
// waits for the client's next call (protocol 3, Pending): a walk down a list with the arrow keys is one record, not a
// call a row. Every other event is something done, and goes at once.
inline bool ibProtocolIsStateEvent(ibProtocolEvent event)
{
	switch (event) {
	case ibProtocolEvent::Focus:
	case ibProtocolEvent::Input:
	case ibProtocolEvent::Row:
	case ibProtocolEvent::Move:
	case ibProtocolEvent::Resize:
		return true;
	default:
		return false;
	}
}

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

// What a spreadsheet's sheet on show is to the report behind it — its State's ComposeState.
enum class ibProtocolComposeState : int {
	Composed  = 0,   // what was built, as the setting in force builds it (or nothing composed yet)
	Composing = 1,   // a compose is running; the sheet on show stays until it is delivered
	Outdated  = 2,   // the setting changed since the sheet was built — composed again when the person says so
};

// How a message to the person is meant — its Level.
enum class ibProtocolMessageLevel : int {
	Information = 1,
	Warning     = 2,
	Error       = 3,
};

#endif
