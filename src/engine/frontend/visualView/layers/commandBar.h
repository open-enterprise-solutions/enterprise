#ifndef _COMMAND_BAR_H__
#define _COMMAND_BAR_H__

#include <vector>
#include "frontend/visualView/layerObject.h"   // ibValueLayerObject — the common runtime+property base
#include "backend/standardCommand.h"          // ibActionID, ibPictureDescription, ibStandardCommandDescription
#include "backend/commandDescription.h"  // ibCommandDescription — the command-hop path a button binds to
#include "backend/propertyManager/property/propertyCommandSource.h" // ibPropertyCommandSource — the button's command SOURCE property (real picker)
#include "backend/backend_command.h"                                // ibBackendCommandReceiver — a bar item is a receiver (via the door it inherits)
#include "backend/propertyManager/property/propertyPicture.h"// ibPropertyPicture
#include "frontend/visualView/controlEnum.h"   // ibRepresentation / ibValueEnumRepresentation
#include "frontend/frontendTypes.h"            // ibFrontendWindow
#include "frontend/visualView/layers/commandReceiver.h"   // ibFrontendCommandReceiver — the command door the bar IS-A

class ibValueFrame;
class ibValueForm;
class ibValueCommandBarItem;
class ibDataNode;   // serialize/dataBuilder.h — universal node (layer -> node)

//********************************************************************************************
//*                            Command bar STORE (a form LAYER)                              *
//********************************************************************************************

// One command the bar consists of. id == wxNOT_FOUND marks a separator. representation
// is the resolved display mode (picture / text / both) for THIS command; enabled greys the
// tool out (an item's Enabled flag) without dropping it from the bar. item is
// the source child (nullptr for an AutoFill command) — lets a designer click resolve back.
struct ibCommandEntry {
	ibActionID              id;
	wxString                caption;
	ibPictureDescription    picture;
	ibRepresentation        representation;
	bool                    enabled;
	ibValueCommandBarItem*  item;
	wxBitmap                bitmap;   // the COMMAND's own live icon (from the command / gather); wins over `picture` when Ok
	ibCommandEntry() : id(wxNOT_FOUND), representation(ibRepresentation_Auto), enabled(true), item(nullptr) {}
	ibCommandEntry(const ibActionID& i, const wxString& c, const ibPictureDescription& p, ibRepresentation r, bool en = true, ibValueCommandBarItem* it = nullptr, const wxBitmap& bmp = wxNullBitmap)
		: id(i), caption(c), picture(p), representation(r), enabled(en), item(it), bitmap(bmp) {}
};

// One CHILD command of the bar — a LEAF layer object (ibValueLayerObject), edited in the designer
// under the command-interface node. Manual commands (AutoFill off) come from these children. The
// shared runtime/property/metadata/routing machinery lives in the base; this adds the command's
// own fields + its designer menu.
class FRONTEND_API ibValueCommandBarItem : public ibValueLayerObject, public ibFrontendCommandReceiver {
public:

	virtual ~ibValueCommandBarItem() {}

	// ibFrontendCommandReceiver gate — a bar item IS-A command door, resolving its binding from its BAR's gate form
	// (the owner frame's form). WalkCommand (the backend receiver's validate) is inherited from the door.
	virtual ibValueForm* GetCommandGateForm() const override;

	wxString GetName() const { wxString s; m_propertyName->GetValueAsString(s); return s; }
	void SetName(const wxString& name) { m_propertyName->SetValue(name); }
	wxString GetCaption() const { return m_propertyCaption->GetValueAsTranslateString(); }
	void SetCaption(const wxString& caption) { m_propertyCaption->SetValue(caption); }
	ibRepresentation GetRepresentation() const { return m_propertyRepresentation->GetValueAsEnum(); }

	// Picture / tooltip / Enabled / Visible, mirroring a toolbar item.
	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	ibPictureDescription GetPictureDesc() const { return m_propertyPicture->GetValueAsPictureDesc(); }
	wxString GetToolTip() const { return m_propertyTooltip->GetValueAsTranslateString(); }
	bool IsEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }
	bool IsVisible() const { return m_propertyVisible->GetValueAsBoolean(); }
	// "Modifies data" is NOT the item's to own — it is a COMMAND attribute (a view-only form greys a data-changing
	// command). The item obeys: BuildCommands reads the flag off the bound command through the door, never here.

	// actionEvent is RETIRED — a command bar item binds solely through the command interface below (a standard
	// action reaches the bar as a "Main attribute" command, resolved via the hop path), so the item no longer
	// carries its own Action event.

	// The bound command as a HOP PATH — held in the command-SOURCE property (ibPropertyCommandSource), the
	// command-door twin of a control's ibPropertySource. A drag from the navigator OR a pick in the inspector
	// dialog both set this ONE property; the button binds to and renders from it.
	ibCommandDescription GetBindingDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }
	void SetCommandDesc(const ibCommandDescription& desc, const wxString& display = wxEmptyString) { m_propertyCommand->SetValue(desc, display); }
	const ibCommandDescription& GetCommandDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }

	// legacy command accessors — the AutoFill transient items set a plain command metaId; it flows into the
	// command source as a 1-hop [metaId] path.
	ibMetaID GetCommandMetaId() const {
		const ibCommandDescription d = m_propertyCommand->GetValueAsCommandDesc();
		return d.IsOk() ? d.GetLeaf() : wxNOT_FOUND;
	}
	void SetCommandMetaId(const ibMetaID& id) { m_propertyCommand->SetValue(ibCommandDescription(id)); }

	// The bar this item belongs to — set on add; routes the designer refresh + name uniqueness,
	// and is the Action picker's source (the owner frame's action collection).
	void SetBar(class ibValueCommandBar* bar) { m_bar = bar; }
	class ibValueCommandBar* GetBar() const { return m_bar; }

	// ibValueLayerObject — leaf, routes through the bar's owner frame.
	virtual ibValueFrame* GetOwnerFrame() const override;
	virtual bool IsLayerContainer() const override { return false; }
	virtual void PrepareDefaultMenu(wxMenu* menu) const override;
	virtual void ExecuteMenu(ibFrontendVisualEditorNotebook* editor, int menuId) override;

	// ibValue / ibPropertyObject concrete requirements specific to the item.
	virtual wxString GetString() const override { return GetCaption(); }
	virtual wxString GetClassName() const override { return wxT("CommandBarItem"); }
	virtual wxString GetObjectTypeName() const override { return wxT("CommandBarItem"); }
	virtual bool IsEditable() const override { return true; }
	// Reject a duplicate Name — unique among the bar's items (like a control's name).
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue) override;

	// Serialization — the item's own fields (mirrors a toolbar item's Read/WriteData).
	bool WriteData(ibDataNode& node) const;
	bool ReadData(const ibDataNode& node);

private:

	class ibValueCommandBar* m_bar = nullptr;

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(wxT("Command"), _("Command"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Command"));
	// Empty by default ON PURPOSE: an unset caption lets the bound command provide the DEFAULT (BuildCommands
	// fills it via the command door). A designer who types one overrides that — own caption on top.
	ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, wxT("Caption"), _("Caption"), wxT(""));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_category, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_category, wxT("Picture"), _("Picture"));
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, wxT("Tooltip"), _("Tooltip"), wxEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("Enabled"), _("Enabled"), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("Visible"), _("Visible"), true);
	// The button's COMMAND-SOURCE property — the real picker (registered frontend editor ibPGCommandSourceProperty:
	// a type glyph + a dialog of the form's command sources), the command-door twin of a control's ibPropertySource.
	// Internal name MUST differ from the category name ("Command") — wxPropertyGrid keys categories and
	// properties in ONE per-page name map (a collision asserts in PrepareToAddItem). Label stays _("Command").
	ibPropertyCommandSource* m_propertyCommand = ibPropertyObject::CreateProperty<ibPropertyCommandSource>(m_category, wxT("BoundCommand"), _("Command"));
};

// A command bar is a CONTAINER layer object (ibValueLayerObject) — NOT a control; it is PART OF a
// control (a form LAYER the frame owns), rendered as a toolbar over the content. It CONSISTS OF
// commands (m_commands, built from AutoFill or the child items) plus the AutoFill flag. A frame
// OWNS one via ibValuePtr and points it back at itself (SetOwner). The host reads BuildCommands().
class ibValueCommandBar : public ibValueLayerObject, public ibFrontendCommandReceiver {
public:

	virtual ~ibValueCommandBar() {}

	// The control that owns this bar — its action collection feeds AutoFill.
	void SetOwner(ibValueFrame* owner) { m_owner = owner; }
	virtual ibValueFrame* GetOwnerFrame() const override { return m_owner; }
	virtual bool IsLayerContainer() const override { return true; }

	// ibFrontendCommandReceiver gate — the bar IS-A command door; the walk starts at the owner frame's form.
	virtual ibValueForm* GetCommandGateForm() const override;

	// Child COMMANDS (runtime items). Added via the designer; the manual set (AutoFill off).
	// Inline so the designer (separate module) links without exporting the symbol.
	ibValueCommandBarItem* AddCommandItem() {
		ibValueCommandBarItem* item = new ibValueCommandBarItem();
		item->SetBar(this);
		item->SetName(GenerateItemName());
		m_items.emplace_back(item);
		return item;
	}
	void RemoveCommandItem(ibValueCommandBarItem* item) {
		for (size_t i = 0; i < m_items.size(); ++i)
			if (m_items[i] == item) { m_items.erase(m_items.begin() + i); return; }
	}
	// Reorder — swap with the neighbour (up = towards the front).
	void MoveCommandItem(ibValueCommandBarItem* item, bool up) {
		for (size_t i = 0; i < m_items.size(); ++i) {
			if (m_items[i] != item) continue;
			const size_t j = up ? (i > 0 ? i - 1 : i) : (i + 1 < m_items.size() ? i + 1 : i);
			if (j != i) { ibValuePtr<ibValueCommandBarItem> t = m_items[i]; m_items[i] = m_items[j]; m_items[j] = t; }
			return;
		}
	}
	bool HasItemName(const wxString& name, const ibValueCommandBarItem* except = nullptr) const {
		for (const auto& it : m_items) {
			if (it != nullptr && it != except && it->GetName() == name)
				return true;
		}
		return false;
	}
	wxString GenerateItemName() const {
		for (int n = 1; ; ++n) {
			const wxString name = wxString::Format(wxT("Command%d"), n);
			if (!HasItemName(name)) return name;
		}
	}
	unsigned int GetCommandItemCount() const { return static_cast<unsigned int>(m_items.size()); }
	ibValueCommandBarItem* GetCommandItem(unsigned int idx) const { return idx < m_items.size() ? (ibValueCommandBarItem*)m_items[idx] : nullptr; }

	// Container tree open-state — mirrors a control's expand flag. Open by default so a freshly
	// added command is visible without a manual expand.
	virtual bool IsTreeExpanded() const override { return m_treeExpanded; }
	virtual void SetTreeExpanded(bool value) override { m_treeExpanded = value; }

	// AutoFill: on -> commands come from the owner's action collection; off ->
	// only the commands stored here.
	bool IsAutoFill() const { return m_propertyAutoFill->GetValueAsBoolean(); }
	void SetAutoFill(bool value) { m_propertyAutoFill->SetValue(value); }

	// The commands this bar consists of. AutoFill rebuilds them from the owner's
	// action collection; otherwise returns the manually-stored ones.
	const std::vector<ibCommandEntry>& BuildCommands();

	// Dispatch a command (by id) — routed to the owner's CallAsAction. This is the
	// bar's OWN event hook, so the toolbar need not know about the control.
	void ExecuteCommand(const ibActionID& id, class ibBackendValueForm* form);

	// Reverse-lookup the child that produced the command with this id (built by BuildCommands).
	// nullptr for an AutoFill command (no child). Used by the designer to select on tool click.
	ibValueCommandBarItem* FindItemByCommandId(const ibActionID& id) const {
		for (const ibCommandEntry& c : m_commands)
			if (c.id == id) return c.item;
		return nullptr;
	}

	// ibValueLayerObject — container, adds / pastes a command from its designer menu.
	virtual void PrepareDefaultMenu(wxMenu* menu) const override;
	virtual void ExecuteMenu(ibFrontendVisualEditorNotebook* editor, int menuId) override;

	// Serialization — AutoFill flag + the child command items (each as a sub-node).
	bool WriteData(ibDataNode& node) const;
	bool ReadData(const ibDataNode& node);

	// ibValue / ibPropertyObject concrete requirements specific to the bar.
	virtual wxString GetString() const override { return _("Command bar"); }
	virtual wxString GetClassName() const override { return wxT("CommandBar"); }
	virtual wxString GetObjectTypeName() const override { return wxT("CommandBar"); }
	virtual bool IsEditable() const override { return true; }

private:

	ibValueFrame*               m_owner = nullptr;
	std::vector<ibCommandEntry> m_commands;
	std::vector<ibValuePtr<ibValueCommandBarItem>> m_items;   // child COMMANDS (runtime)
	std::vector<ibValuePtr<ibValueCommandBarItem>> m_autoItems; // TRANSIENT AutoFill items for the owner object's own commands — rebuilt each BuildCommands, never serialized
	bool                        m_treeExpanded = true;        // designer tree node open-state

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(wxT("CommandBar"), _("Command bar"));
	ibPropertyBoolean* m_propertyAutoFill = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("AutoFill"), _("Auto fill"), true);
};

// One command SOURCE entry available to a form — the shared gather behind BOTH the navigator panel and the
// inspector's command-source picker (so the two can't drift). group = the source it comes from ("Object" /
// "Main attribute" / a table name / "General"); label = the command / action caption; desc = its command-hop
// path (a table command carries [source, action], a command a 1-hop path). Symmetric to the source explorer's
// columns feeding the data-source picker.
struct ibCommandSourceEntry {
	wxString             group;
	wxString             label;   // the TREE LEAF text — the command's own NAME (the tree's sub-folders already carry
	                              // the path, and the configurator works with NAMES, not synonyms).
	ibCommandDescription desc;
	wxBitmap             icon;   // THE COMMAND'S picture — the command determines it (a command's own icon; an
	                             // action-command's own picture) and every surface (navigator / picker / button)
	                             // reads it from HERE, never from a separate action lookup.
	wxString             subgroup;   // optional SUB-group WITHIN the section (empty = directly under it). Used by
	                                 // the Standard section to nest actions per source: the form vs each tablebox.
	wxString             fullName;   // the FLAT full PATH name (GetFullName) — shown where there is NO tree to give
	                                 // the path: the bound button's command-source CELL. Empty = fall back to label.
};
// The THREE command sections of a form, in display order. The navigator / picker ALWAYS show
// these three (even empty), and GatherFormCommands labels its entries with exactly these strings, so the two
// can't drift. [0] Form commands (manually added), [1] Standard commands (from the form's sources), [2] Global
// commands (common + section commands).
FRONTEND_API std::vector<wxString> GetCommandSections();

// Gather every command available to `form`, grouped into the three sections above:
//   [0] the form's own object commands (manually-added commands),
//   [1] the standard actions of the form's source AND of every tablebox control,
//   [2] every OTHER command metaobject in the config (common + section commands, recursive by classid).
// The command-door twin of walking the form's source explorers. Empty when form is null.
FRONTEND_API std::vector<ibCommandSourceEntry> GatherFormCommands(ibValueForm* form);

#ifndef OES_USE_WEB
// The ONE place that turns a command STORE's entries into a wx toolbar: build it into
// `parent`, or nullptr when empty. Each entry carries its own display mode; clicks
// dispatch through the STORE's own hook. Shared by the form (visualHost) and the
// composite control chrome — form and control render identically.
ibFrontendWindow* BuildCommandBarToolBar(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form);
// UPDATE in place: refresh an EXISTING toolbar's tools from the STORE — same bar object, so
// the stable ref stays valid (no recreation).
void RefreshCommandBarToolBar(ibFrontendWindow* existing, ibValueCommandBar* cbar, ibValueForm* form);
// If `w` IS a command-bar toolbar (tagged by BuildCommandBarToolBar), return its bar; else null. Lets the designer's
// drop resolver route a command DROPPED on a toolbar (the form's or a tablebox's) to that bar — a new item, not a
// free button. Walks nothing: the caller climbs the widget parents.
FRONTEND_API ibValueCommandBar* ibCommandBarFromToolBar(wxWindow* w);
#endif

#endif
