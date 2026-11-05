#ifndef __PROTOCOL_PROTOCOL_DESIGNER_H__
#define __PROTOCOL_PROTOCOL_DESIGNER_H__

#include "protocol/protocol.h"

// THE DESIGNER'S PART OF THE PROTOCOL — what only the designer speaks: its debugger's connection, the questions asked
// while a configuration is written, its schemas (the configuration's tree, the inspector, the editors over an object)
// and its tabs' views (a module, a form being designed, the check trees, the query constructor). Behind each the
// server's door is the designer's (frmserver/schema/designer/, docView/templates/designer/, the debugger); a client of
// the application — the thin client, the browser — speaks protocol.h alone. The numbers stay there, beside every
// other: the wire has one numbering, and a mode refuses a number it does not offer (the frame's Schemas).
namespace ibProtocol {

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

	namespace ibRequest {

		// The desktop's ibDialogApplyChange over the restructure's ledger, the base held until it is answered: Picture (the
		// window's) and children Level (ibRestructure: 0 info, 1 warning, 2 error), Text → Done — applied; none —
		// cancelled, everything rolled back.
		namespace ibApplyChange {

			inline constexpr const char* Picture = "Picture";
			inline constexpr const char* Level   = "Level";
			inline constexpr const char* Text    = "Text";
			inline constexpr const char* Done    = "Done";

		}

		// The desktop's ibDialogSelectTypeForm over a form just made, and a template's the same way (ibDialogSelectKind),
		// asked inside the Create that made it: Caption — the window's, Name, Synonym, Comment — the object's — and
		// children Id, Caption, Name — the kinds it is offered, each with the name the object takes with it → Id, Name,
		// Synonym, Comment — the kind chosen and what the person typed; none — cancelled, and the object taken away again
		// before the Create answers.
		namespace ibObjectKind {

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

	namespace ibSchema {

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

		// The designer's users of the base — a user's record as the engine's ibUserInfo holds it: Users (Guid, Name,
		// FullName), and the configuration's Roles (Guid, Name, Title, Mode) a user's record is chosen from (its languages —
		// ibLanguages); `Args: {Guid}` — that one's User {Guid, Name, FullName, Password — whether one is stored (never the
		// stored one), Language — its guid, Roles (Guid)}; commands Save {Guid, Name, FullName, Language, Roles (Guid)[,
		// Password — a new one, hashed by the server; absent: the stored one kept]}, Remove {Guid}.
		// The designer's syntax helper, for the names its articles do not write up: the language's types, which only the
		// server knows. Args Query — Types, a row each {Name}; Args Name — what that one offers: Methods {Name, Signature}
		// and Properties {Name}, or Note, why it cannot be asked.
		namespace ibTypes {

			inline constexpr const char* Query      = "Query";
			inline constexpr const char* Types      = "Types";
			inline constexpr const char* Name       = "Name";
			inline constexpr const char* Methods    = "Methods";
			inline constexpr const char* Signature  = "Signature";
			inline constexpr const char* Properties = "Properties";
			inline constexpr const char* Note       = "Note";
		}

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
			inline constexpr const char* TemplateData = "TemplateData";   // a template's: what it holds, of its kind
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
				// …and as a directory of files, one a part (ibFileProvider): the files travel as one node, in its binary
				// form, and the client writes them into the directory or reads them out of it.
				LoadFiles   = 14,   // {File} — read from that uploaded node of files
				UnloadFiles = 15,   // {File} — the node of files written into that one (uploaded empty)
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
			inline constexpr const char* ClassPictures = "ClassPictures"; // a picture's: the engine's class icons it may be
			                                                              // besides the core's (Name; Id — the class id it
			                                                              // is drawn and stored by)
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

		// The desktop's predefined values editor over an object of the designer's tree: `schema {Schema, Args: {Id[, Tab]}}`
		// shows Type (the object's class), Declares — the one schema of the object's predefined items
		// (ibPredefinedDescriptionMemory: the items as the configuration keeps them and what an item is entered with; the
		// window is built of it) — and Classes — the types a type is picked from, named as ibProperties' rows name them.
		// Commands Create {Id, an item in its node form (ibPredefinedItemDescriptionMemory)}, Set {Id, the item with
		// its Guid} — the item of that Guid given this one (none of it: one more, of that Guid), Remove {Id, Guid}. Each
		// with Tab — an external file's tree, as ibMetaData's.
		namespace ibPredefined {

			inline constexpr const char* Id          = "Id";
			inline constexpr const char* Tab         = "Tab";
			inline constexpr const char* Type        = "Type";
			inline constexpr const char* Declares    = "Declares";
			inline constexpr const char* Classes     = "Classes";
			inline constexpr const char* Guid        = "Guid";

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
		// are here: shows the configuration's Guid; `Args: {Module}` — that module's Breakpoints (Line, Condition), at the
		// editor's lines: the server's debugger keeps the one map, moved by the edits a module's Change carries
		// (ibView::ibModule). A module view's State says its Module too.
		namespace ibDebugger {

			inline constexpr const char* Guid        = "Guid";
			inline constexpr const char* Module      = "Module";
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

	namespace ibView {

		// The designer's editor of a module: State {Syntax, ReadOnly, Modified — the configuration the module is in,
		// Module}; the text fetched by the tab (fetch {Tab} — {Text}; what else — ibRequest); what was typed — event
		// {Tab, Event: Input} on the first key while not Modified, then {Tab, Event: Change, Args: {Text[, Line, Offset,
		// AtLineStart]}} — with the edit that moved its lines, when one did: the server's debugger moves its breakpoints
		// there (ibDebuggerClient::PatchModule); its commands — event {Tab, Event: Command, Args: {Id}} (ibCommand).
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
			// …a Change's edit that moved the lines:
			inline constexpr const char* Line        = "Line";          // the line it was at, from 0
			inline constexpr const char* Offset      = "Offset";        // the lines it added (removed, below 0)
			inline constexpr const char* AtLineStart = "AtLineStart";   // at the line's start: the line itself moved

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

	}

}

#endif
