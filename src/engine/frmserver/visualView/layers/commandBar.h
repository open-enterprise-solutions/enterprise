#ifndef _COMMAND_BAR_H__
#define _COMMAND_BAR_H__

#include <vector>
#include "frmserver/visualView/layerObject.h"   // ibValueLayerObject — the common runtime+property base
#include "backend/standardCommand.h"          // ibActionID, ibPictureDescription, ibStandardCommandDescription
#include "core/description/commandDescription.h"  //ibCommandDescription — the command-hop path a button binds to
#include "backend/propertyManager/property/propertyCommandSource.h" // ibPropertyCommandSource — the button's command SOURCE property (real picker)
#include "backend/backend_command.h"                                // ibBackendCommandReceiver — a bar item is a receiver (via the door it inherits)
#include "backend/propertyManager/property/propertyPicture.h"// ibPropertyPicture
#include "backend/propertyManager/property/propertyFeatureOptions.h" // ibPropertyFeatureOptions — the item's feature options
#include "frmserver/visualView/controlEnum.h"   // ibRepresentation / ibValueEnumRepresentation
#include "frmserver/visualView/layers/commandReceiver.h"   // ibFrontendCommandReceiver — the command door the bar IS-A

class ibValueFrame;
class ibValueForm;
class ibValueCommandBar;
class ibValueCommandBarItem;
class ibDataNode;   // serialize/dataBuilder.h — universal node (layer -> node)

// A submenu's node among the bar's children is of its type (ibValueCommandBar::WriteData) — what tells it from a command
// when read back, here and in the studio.
constexpr ibClassID g_commandBarSubmenuCLSID = system_to_clsid("CommandBarSubmenu");

//********************************************************************************************
//*                            Command bar STORE (a form LAYER)                              *
//********************************************************************************************

// One command the bar consists of. id == wxNOT_FOUND marks a separator. representation
// is the resolved display mode (picture / text / both) for THIS command; enabled greys the
// tool out (an item's Enabled flag) without dropping it from the bar. item is
// the source child (nullptr for an AutoFill command) — lets a studio click resolve back.
// ⭐⭐ WHAT A BAR ENTRY IS — and there are three answers, which is why this is a KIND and not a flag.
// A COMMAND stands for something to DO; a QUICK FILTER stands for a filter LINE the author marked
// «quick access», and clicking it edits that line's value rather than running anything; a GROUP stands
// for a submenu — the author's, a command group of the form command bar, a group of standard actions — and
// clicking it opens its entries.
//
// The kind is asked in exactly two places — the click (which door it opens) and the caption (a
// filter shows its value) — and nowhere else has to care, because all are entries on one bar.
enum ibCommandEntryKind {
	ibCommandEntryKind_Command = 0,
	ibCommandEntryKind_QuickFilter,
	ibCommandEntryKind_Group,
};

struct ibCommandEntry {
	ibActionID              id;
	ibString                caption;
	ibPictureDescription    picture;
	ibRepresentation        representation;
	bool                    enabled;
	// The bar's default command, drawn standing out: an item's Default, or the action's own (an object form's
	// «… and close», AutoFill).
	bool                    isDefault;
	// What the author put on the bar for it — a command or a submenu; nullptr for a standard action and an object's
	// command.
	const ibValueLayerObject* item;
	// …and WHAT IT RUNS, for a command of the author's (its binding) or of an object (its metaId, one hop): pressed, it is
	// walked from the bar's door. None — a standard action, run by its id (CallAsAction); a group runs nothing.
	ibCommandDescription    command;
	ibCommandEntryKind      kind;
	// …and WHICH filter line, for a quick filter: its index among the top-level nodes of the
	// setting in force. An index rather than a pointer because the settings are rebuilt under the
	// bar (a refetch, another variant), and a pointer into a vector dies the moment one is added.
	size_t                  filterLine;
	// …and WHAT IS UNDER IT, for a group: its entries in order, each with an id of its own — a command, or a group of its
	// own, to any depth. A press names the entry, wherever it stands. Empty for every other kind.
	std::vector<ibCommandEntry> members;
	// The hint on the tool. Empty: the caption.
	ibString                tooltip;
	ibCommandEntry() : id(ibNOT_FOUND), representation(ibRepresentation_Auto), enabled(true), isDefault(false),
		item(nullptr), kind(ibCommandEntryKind_Command), filterLine(0) {}
	ibCommandEntry(const ibActionID& i, const ibString& c, const ibPictureDescription& p, ibRepresentation r, bool en = true, const ibValueLayerObject* it = nullptr)
		: id(i), caption(c), picture(p), representation(r), enabled(en), isDefault(false), item(it),
		  kind(ibCommandEntryKind_Command), filterLine(0) {}

	// A command of the author's with nothing alive to run — bound to nothing, or to a command since gone: the studio
	// draws it greyed, so it can be bound again; where the form runs it is not drawn, and pressed it runs nothing.
	bool IsUnbound() const { return item != nullptr && kind == ibCommandEntryKind_Command && !command.IsOk(); }
};

// One CHILD command of the bar — a LEAF layer object (ibValueLayerObject). Manual commands (AutoFill
// off) come from these children. The shared runtime/property/metadata machinery lives in the base;
// this adds the command's own fields.
class FRMSERVER_API ibValueCommandBarItem : public ibValueLayerObject, public ibFrontendCommandReceiver {
public:

	virtual ~ibValueCommandBarItem() {}

	// ibFrontendCommandReceiver gate — a bar item IS-A command door, resolving its binding from its BAR's gate form
	// (the owner frame's form). WalkCommand (the backend receiver's validate) is inherited from the door.
	virtual ibValueForm* GetCommandGateForm() const override;

	void SetName(const ibString& name) { m_propertyName->SetValue(name); }
	ibString GetCaption() const { return m_propertyCaption->GetValueAsTranslateString(); }
	void SetCaption(const ibString& caption) { m_propertyCaption->SetValue(caption); }
	ibRepresentation GetRepresentation() const { return m_propertyRepresentation->GetValueAsEnum(); }

	// Picture / tooltip / Enabled / Visible, mirroring a toolbar item.
	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	ibPictureDescription GetPictureDesc() const { return m_propertyPicture->GetValueAsPictureDesc(); }
	ibString GetToolTip() const { return m_propertyTooltip->GetValueAsTranslateString(); }
	bool IsEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }
	void SetEnabled(bool value) { m_propertyEnabled->SetValue(value); }
	bool IsVisible() const { return m_propertyVisible->GetValueAsBoolean(); }
	bool IsDefault() const { return m_propertyDefault->GetValueAsBoolean(); }
	// Available by the feature options it names — false while all of them are off in this base. Beside
	// Visible, never written into it.
	bool IsAvailable() const { return m_propertyFeatureOptions->IsAvailable(); }
	// "Modifies data" is NOT the item's to own — it is a COMMAND attribute (a view-only form greys a data-changing
	// command). The item obeys: BuildCommands reads the flag off the bound command through the door, never here.

	// actionEvent is RETIRED — a command bar item binds solely through the command interface below (a standard
	// action reaches the bar as a "Main attribute" command, resolved via the hop path), so the item no longer
	// carries its own Action event.

	// The bound command as a HOP PATH — held in the command-SOURCE property (ibPropertyCommandSource), the
	// command-door twin of a control's ibPropertySource. A drag from the navigator OR a pick in the inspector
	// dialog both set this ONE property; the button binds to and renders from it.
	ibCommandDescription GetBindingDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }
	void SetCommandDesc(const ibCommandDescription& desc, const ibString& display = ibEmptyString) { m_propertyCommand->SetValue(desc, display); }
	const ibCommandDescription& GetCommandDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }

	// ibValueLayerObject — leaf, routes through the bar's owner frame (GetBar — the bar it hangs under, or a submenu of
	// it: the studio refresh, the name uniqueness, the Action picker's source).
	virtual ibValueFrame* GetOwnerFrame() const override;
	virtual bool IsLayerContainer() const override { return false; }
	virtual ibString GetName() const override { ibString s; m_propertyName->GetValueAsString(s); return s; }
	// …its entry: the command it is bound to, as that command shows (ResolveCommand — the one resolve every projection
	// shares), its own caption, picture and representation on top. Hidden or unavailable — none; bound to nothing alive —
	// one that runs nothing (IsUnbound), greyed under its caption or else its name.
	virtual bool GetCommandEntry(ibCommandEntry& entry) const override;

	// ibValue / ibPropertyObject concrete requirements specific to the item.
	virtual ibString GetString() const override { return GetCaption(); }
	virtual ibString GetClassName() const override { return ibT("CommandBarItem"); }
	virtual ibString GetObjectTypeName() const override { return ibT("CommandBarItem"); }
	virtual bool IsEditable() const override { return true; }
	// Reject a duplicate Name — unique among the bar's items (like a control's name).
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue) override;

	// Serialization — the item's own fields (mirrors a toolbar item's Read/WriteData).
	virtual bool WriteData(ibDataNode& node) const override;
	virtual bool ReadData(const ibDataNode& node) override;

private:

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(ibT("Command"), _("Command"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, ibT("Name"), _("Name"),
		_("The item's name, unique among the command bar's items. It identifies the item in the studio; the user never sees it."), _("Command"));
	// Empty by default ON PURPOSE: an unset caption lets the bound command provide the DEFAULT (BuildCommands
	// fills it via the command door). A studio who types one overrides that — own caption on top.
	ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, ibT("Caption"), _("Caption"),
		_("The item's caption on the bar. Empty: the bound command's own caption. Can be written per language."), ibT(""));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_category, ibT("Representation"), _("Representation"),
		_("What the item shows: text, picture, or both. Auto: what the bound command prefers (Close is a picture alone, Add a picture with text)."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_category, ibT("Picture"), _("Picture"),
		_("The item's icon. Empty: the bound command's own picture."));
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, ibT("Tooltip"), _("Tooltip"),
		_("Saved with the form, but the bar does not show it yet: an item's tooltip is always its caption."), ibEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("Enabled"), _("Enabled"),
		_("Whether the item can be pressed. Off: it is shown greyed out."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("Visible"), _("Visible"),
		_("Whether the item is shown on the bar."), true);
	ibPropertyBoolean* m_propertyDefault = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("Default"), _("Default button"),
		_("Whether the item is the bar's default command: drawn standing out, in the accent's colours, as a dialog's default button."), false);
	// The button's COMMAND-SOURCE property — the real picker (registered frontend editor ibPGCommandSourceProperty:
	// a type glyph + a dialog of the form's command sources), the command-door twin of a control's ibPropertySource.
	// Internal name MUST differ from the category name ("Command") — wxPropertyGrid keys categories and
	// properties in ONE per-page name map (a collision asserts in PrepareToAddItem). Label stays _("Command").
	ibPropertyCommandSource* m_propertyCommand = ibPropertyObject::CreateProperty<ibPropertyCommandSource>(m_category, ibT("BoundCommand"), _("Command"),
		_("The command the item runs: a form command, a standard command of the form or of one of its tables, or a command of the form's object or of the application."));
	// USER VISIBILITY — a group of its own, as on an element of a form (control.h).
	ibPropertyCategory* m_categoryUserVisibility = ibPropertyObject::CreatePropertyCategory(ibT("UserVisibility"), _("User visibility"));
	ibPropertyFeatureOptions* m_propertyFeatureOptions = ibPropertyObject::CreateProperty<ibPropertyFeatureOptions>(m_categoryUserVisibility, ibT("FeatureOptions"), _("Feature options"),
		_("The feature options this element is available under: while every one of them is off, it is neither shown nor offered, and neither is what it holds. Empty - available whatever the options are. Works together with Visible and does not change it."));
};

// ⭐ A SUBMENU OF THE BAR — what the author makes to hold commands and further submenus, to any depth, as a group holds
// controls: a dropdown on the bar, a nested menu under one. A CONTAINER as the bar is; its children are the bar's two
// kinds, added by the bar's two verbs.
class FRMSERVER_API ibValueCommandBarSubmenu : public ibValueLayerObject {
public:

	virtual ~ibValueCommandBarSubmenu() {}

	// ibValueLayerObject — a container, routing through the bar's owner frame.
	virtual ibValueFrame* GetOwnerFrame() const override;
	virtual bool IsLayerContainer() const override { return true; }
	virtual bool IsTreeExpanded() const override { return m_treeExpanded; }
	virtual void SetTreeExpanded(bool value) override { m_treeExpanded = value; }
	virtual ibString GetName() const override { ibString s; m_propertyName->GetValueAsString(s); return s; }
	// …its entry: a group, its caption its name while it has none of its own, its children's entries under it. Hidden or
	// unavailable — none, with all it holds.
	virtual bool GetCommandEntry(ibCommandEntry& entry) const override;

	// Serialization — its own fields, then its children as the bar writes its own.
	virtual bool WriteData(ibDataNode& node) const override;
	virtual bool ReadData(const ibDataNode& node) override;

	// ibValue / ibPropertyObject concrete requirements specific to the submenu.
	virtual ibString GetString() const override { return GetCaption(); }
	virtual ibString GetClassName() const override { return ibT("CommandBarSubmenu"); }
	virtual ibString GetObjectTypeName() const override { return ibT("CommandBarSubmenu"); }
	virtual bool IsEditable() const override { return true; }
	// Reject a duplicate Name — one namespace with the bar's commands and submenus.
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue) override;

	void SetName(const ibString& name) { m_propertyName->SetValue(name); }
	ibString GetCaption() const { return m_propertyCaption->GetValueAsTranslateString(); }
	ibRepresentation GetRepresentation() const { return m_propertyRepresentation->GetValueAsEnum(); }
	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	ibPictureDescription GetPictureDesc() const { return m_propertyPicture->GetValueAsPictureDesc(); }
	ibString GetToolTip() const { return m_propertyTooltip->GetValueAsTranslateString(); }
	bool IsEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }
	bool IsVisible() const { return m_propertyVisible->GetValueAsBoolean(); }
	// Available by the feature options it names, as an item is — off, it is dropped with all it holds.
	bool IsAvailable() const { return m_propertyFeatureOptions->IsAvailable(); }

	// What it holds — the bar's two verbs: a new command, a new submenu, last, under a name free on the whole bar.
	ibValueCommandBarItem* AddCommandItem();
	ibValueCommandBarSubmenu* AddSubmenu();

private:

	bool m_treeExpanded = true;   // studio tree node open-state

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(ibT("Submenu"), _("Submenu"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, ibT("Name"), _("Name"),
		_("The submenu's name, unique among the command bar's commands and submenus. It identifies the submenu in the studio; the user never sees it."), _("Submenu"));
	ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, ibT("Caption"), _("Caption"),
		_("The submenu's caption on the bar, or in the menu that holds it. Empty: its name. Can be written per language."), ibT(""));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_category, ibT("Representation"), _("Representation"),
		_("What the submenu shows on the bar: text, picture, or both. Auto: both."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_category, ibT("Picture"), _("Picture"),
		_("The submenu's icon. Empty: none."));
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, ibT("Tooltip"), _("Tooltip"),
		_("The hint on the submenu's tool. Empty: its caption."), ibEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("Enabled"), _("Enabled"),
		_("Whether the submenu can be opened. Off: it is shown greyed out."), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("Visible"), _("Visible"),
		_("Whether the submenu is shown, with everything it holds."), true);
	// USER VISIBILITY — a group of its own, as on a command of the bar.
	ibPropertyCategory* m_categoryUserVisibility = ibPropertyObject::CreatePropertyCategory(ibT("UserVisibility"), _("User visibility"));
	ibPropertyFeatureOptions* m_propertyFeatureOptions = ibPropertyObject::CreateProperty<ibPropertyFeatureOptions>(m_categoryUserVisibility, ibT("FeatureOptions"), _("Feature options"),
		_("The feature options this element is available under: while every one of them is off, it is neither shown nor offered, and neither is what it holds. Empty - available whatever the options are. Works together with Visible and does not change it."));
};

// A command bar is a CONTAINER layer object (ibValueLayerObject) — NOT a control; it is PART OF a
// control (a form LAYER the frame owns), rendered as a toolbar over the content. It CONSISTS OF
// commands (m_commands, built from AutoFill or the child items) plus the AutoFill flag. A frame
// OWNS one via ibValuePtr and points it back at itself (SetOwner). The host reads BuildCommands().
class FRMSERVER_API ibValueCommandBar : public ibValueLayerObject, public ibFrontendCommandReceiver {
public:

	virtual ~ibValueCommandBar() {}

	// The control that owns this bar — its action collection feeds AutoFill.
	void SetOwner(ibValueFrame* owner) { m_owner = owner; }
	virtual ibValueFrame* GetOwnerFrame() const override { return m_owner; }
	virtual const ibValueCommandBar* GetBar() const override { return this; }
	virtual bool IsLayerContainer() const override { return true; }

	// ibFrontendCommandReceiver gate — the bar IS-A command door; the walk starts at the owner frame's form.
	virtual ibValueForm* GetCommandGateForm() const override;

	// Child COMMANDS and SUBMENUS — the bar's children (GetChild, GetChildCount, RemoveChild), a submenu's under it.
	// Added via the studio; the manual set (AutoFill off). A new one goes last, under a name free on the whole bar.
	ibValueCommandBarItem* AddCommandItem();
	ibValueCommandBarSubmenu* AddSubmenu();
	// Whether a command or a submenu ANYWHERE on the bar is called `name` — one namespace, as a form's controls have one.
	bool HasItemName(const ibString& name, const ibValueLayerObject* except = nullptr) const;
	// The first `<prefix>N` no command or submenu of the bar is called: "Command" for a command, "Submenu" for a submenu.
	ibString GenerateItemName(const ibString& prefix) const;

	// Container tree open-state — mirrors a control's expand flag. Open by default so a freshly
	// added command is visible without a manual expand.
	virtual bool IsTreeExpanded() const override { return m_treeExpanded; }
	virtual void SetTreeExpanded(bool value) override { m_treeExpanded = value; }

	// AutoFill: on -> commands come from the owner's action collection; off ->
	// only the commands stored here.
	bool IsAutoFill() const { return m_propertyAutoFill->GetValueAsBoolean(); }
	void SetAutoFill(bool value) { m_propertyAutoFill->SetValue(value); }

	// ShowSearch: on -> the bar ends with a search field when what it serves looks for text (a table, or the table
	// whose commands a list form's bar carries); off -> no field.
	bool IsShowSearch() const { return m_propertyShowSearch->GetValueAsBoolean(); }
	void SetShowSearch(bool value) { m_propertyShowSearch->SetValue(value); }

	// The commands this bar consists of. AutoFill rebuilds them from the owner's
	// action collection; otherwise returns the manually-stored ones.
	const std::vector<ibCommandEntry>& BuildCommands();

	// Dispatch a command (by id) — routed to the owner's CallAsAction. This is the
	// bar's OWN event hook, so the client need not know about the control. The id is the entry's wherever it stands —
	// on the bar or in a submenu at any depth; a group runs nothing by itself.
	void ExecuteCommand(const ibActionID& id, class ibBackendValueForm* form);
	// …and its search field typed into: the text handed to its owner, which looks for it (ibValueFrame::SetSearchText —
	// a form passes it on to its command view). A bar showing no field takes nothing.
	void Search(const ibString& text);

	// ⭐ THE BAR AS IT STANDS NOW, one child of `state` per entry in order: a separator, or a command with its
	// caption and picture as the representation chooses them, its id, whether it is enabled, its tooltip, its kind —
	// and for a group, its entries under it the same way, to any depth, greyed where a view-only form forbids a
	// command that changes data. Everything a client needs to draw the bar and its submenus without asking again —
	// the pictures by their ids (ibBackendPicture::SendPicture). An entry the author put on the bar itself says its
	// place among the bar's children (Position — what the studio selects it by); one with nothing to run (IsUnbound) is
	// drawn by the studio's host alone. And, ShowSearch on, its search field: `Search` holding the text.
	void Update(ibDataNode& state, class ibVisualHost* host);

	// The entry built for this id, on the bar or in a submenu at any depth — what a click is resolved by (a group opens
	// its submenu). nullptr when the id is not one of this bar's.
	const ibCommandEntry* FindEntry(const ibActionID& id) const;

	// Serialization — the AutoFill and ShowSearch flags + the children (each as a sub-node of its type: a submenu's
	// g_commandBarSubmenuCLSID, holding its own).
	virtual bool WriteData(ibDataNode& node) const override;
	virtual bool ReadData(const ibDataNode& node) override;

	// ibValue / ibPropertyObject concrete requirements specific to the bar.
	virtual ibString GetString() const override { return _("Command bar"); }
	virtual ibString GetClassName() const override { return ibT("CommandBar"); }
	virtual ibString GetObjectTypeName() const override { return ibT("CommandBar"); }
	virtual bool IsEditable() const override { return true; }

private:

	// ⭐ THE QUICK-ACCESS FILTER LINES AS TOOLS — one per line the author marked, prepended to the
	// bar by BuildCommands. Reads the SETTING IN FORCE every time; nothing is stored on the bar, so
	// marking a line is the only act and unmarking it takes the tool away.
	void BuildQuickFilters();
	// …and the click on one: open the value chooser over that line, put the answer in the READER's
	// setting, refetch. True when the entry was a quick filter (a command click falls through).
	bool ExecuteQuickFilter(const ibActionID& id, class ibValueForm* form);
	// The model whose setting the quick filters stand in for — null when this bar's owner has none
	// (a form's own bar, a report's).
	class ibValueModel* QuickFilterModel() const;

	ibValueFrame*               m_owner = nullptr;
	std::vector<ibCommandEntry> m_commands;
	bool                        m_treeExpanded = true;        // studio tree node open-state

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(ibT("CommandBar"), _("Command bar"));
	ibPropertyBoolean* m_propertyAutoFill = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("AutoFill"), _("Auto fill"),
		_("Whether the bar fills itself with its owner's standard commands (the form's or the table's: Add, Copy, Delete, Write and so on) and the commands of the owner's object. On by default. Off: the bar holds only the items added to it by hand."),
		true);
	ibPropertyBoolean* m_propertyShowSearch = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, ibT("ShowSearch"), _("Show search"),
		_("Whether the bar ends with a search field when what it serves looks for text: a table, or the table whose commands a list form's bar carries. Typing into it narrows the rows to those holding the text. On by default."),
		true);
};

// One command SOURCE entry available to a form — the shared gather behind BOTH the navigator panel and the
// inspector's command-source picker (so the two can't drift). group = the source it comes from ("Object" /
// "Main attribute" / a table name / "General"); label = the command / action caption; desc = its command-hop
// path (a table command carries [source, action], a command a 1-hop path). Symmetric to the source explorer's
// columns feeding the data-source picker.
struct ibCommandSourceEntry {
	ibString             group;
	ibString             label;   // the TREE LEAF text — the command's own NAME (the tree's sub-folders already carry
	                              // the path, and the configurator works with NAMES, not synonyms).
	ibCommandDescription desc;
	ibPictureDescription picture;   // THE COMMAND'S picture — the command determines it (a command's own picture; an
	                                // action-command's own picture) and every surface (navigator / picker / button)
	                                // reads it from HERE, never from a separate action lookup.
	ibString             subgroup;   // optional SUB-group WITHIN the section (empty = directly under it). Used by
	                                 // the Standard section to nest actions per source: the form vs each tablebox.
	ibString             fullName;   // the FLAT full PATH name (GetFullName) — shown where there is NO tree to give
	                                 // the path: the bound button's command-source CELL. Empty = fall back to label.
};
// The THREE command sections of a form, in display order. The navigator / picker ALWAYS show
// these three (even empty), and GatherFormCommands labels its entries with exactly these strings, so the two
// can't drift. [0] Form commands (manually added), [1] Standard commands (from the form's sources), [2] Global
// commands (common + section commands).
FRMSERVER_API std::vector<ibString> GetCommandSections();

// Gather every command available to `form`, grouped into the three sections above:
//   [0] the form's own object commands (manually-added commands),
//   [1] the standard actions of the form's source AND of every tablebox control,
//   [2] every OTHER command metaobject in the application (common + section commands, recursive by classid).
// The command-door twin of walking the form's source explorers. Empty when form is null.
FRMSERVER_API std::vector<ibCommandSourceEntry> GatherFormCommands(ibValueForm* form);

// The same gather as the rows a client draws (the form editor's command tree, the inspector's command-source row): the
// three sections, each with a row per sub-group and per command — Name, Picture, CommandPath (children Id), Kind; a
// form command's row its node id the command's. Nothing for no form.
FRMSERVER_API void WriteCommandTree(class ibDataNode& node, ibValueForm* form);

#endif
