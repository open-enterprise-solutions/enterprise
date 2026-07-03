#ifndef _COMMAND_BAR_H__
#define _COMMAND_BAR_H__

#include <vector>
#include "frontend/visualView/layerObject.h"   // ibValueLayerObject — the common runtime+property base
#include "backend/actionInfo.h"          // ibActionID, ibPictureDescription, ibActionDescription
#include "backend/propertyManager/property/eventAction.h"    // ibEventAction (the Action event)
#include "backend/propertyManager/property/propertyPicture.h"// ibPropertyPicture
#include "frontend/visualView/controlEnum.h"   // ibRepresentation / ibValueEnumRepresentation
#include "frontend/frontendTypes.h"            // ibFrontendWindow

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
	ibCommandEntry() : id(wxNOT_FOUND), representation(ibRepresentation_Auto), enabled(true), item(nullptr) {}
	ibCommandEntry(const ibActionID& i, const wxString& c, const ibPictureDescription& p, ibRepresentation r, bool en = true, ibValueCommandBarItem* it = nullptr)
		: id(i), caption(c), picture(p), representation(r), enabled(en), item(it) {}
};

// One CHILD command of the bar — a LEAF layer object (ibValueLayerObject), edited in the designer
// under the command-interface node. Manual commands (AutoFill off) come from these children. The
// shared runtime/property/metadata/routing machinery lives in the base; this adds the command's
// own fields + its designer menu.
class FRONTEND_API ibValueCommandBarItem : public ibValueLayerObject {
public:

	virtual ~ibValueCommandBarItem() {}

	wxString GetName() const { wxString s; m_propertyName->GetValueAsString(s); return s; }
	void SetName(const wxString& name) { m_propertyName->SetValue(name); }
	wxString GetCaption() const { return m_propertyCaption->GetValueAsTranslateString(); }
	ibRepresentation GetRepresentation() const { return m_propertyRepresentation->GetValueAsEnum(); }

	// Picture / tooltip / Enabled / Visible, mirroring a toolbar item.
	bool IsEmptyPicture() const { return m_propertyPicture->IsEmptyProperty(); }
	ibPictureDescription GetPictureDesc() const { return m_propertyPicture->GetValueAsPictureDesc(); }
	wxString GetToolTip() const { return m_propertyTooltip->GetValueAsTranslateString(); }
	bool IsEnabled() const { return m_propertyEnabled->GetValueAsBoolean(); }
	bool IsVisible() const { return m_propertyVisible->GetValueAsBoolean(); }

	// The bound Action — its system id feeds the command's real action (empty -> synthetic).
	void SetAction(const ibActionDescription& action) { m_eventAction->SetValue(action); }
	const ibActionDescription& GetAction() const { return m_eventAction->GetValueAsActionDesc(); }
	ibActionID GetActionId() const { return m_eventAction->GetValueAsActionDesc().GetSystemAction(); }

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

	// Fills the Action picker with the owner frame's actions (like the toolbar item's GetToolAction).
	bool GetItemAction(ibEventAction* evtList);

private:

	class ibValueCommandBar* m_bar = nullptr;

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(wxT("Command"), _("Command"));
	ibPropertyUString* m_propertyName = ibPropertyObject::CreateProperty<ibPropertyUString>(m_category, wxT("Name"), _("Name"), _("Command"));
	ibPropertyTString* m_propertyCaption = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, wxT("Caption"), _("Caption"), _("Command"));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_category, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_category, wxT("Picture"), _("Picture"));
	ibPropertyTString* m_propertyTooltip = ibPropertyObject::CreateProperty<ibPropertyTString>(m_category, wxT("Tooltip"), _("Tooltip"), wxEmptyString);
	ibPropertyBoolean* m_propertyEnabled = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("Enabled"), _("Enabled"), true);
	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("Visible"), _("Visible"), true);
	ibEventAction* m_eventAction = ibPropertyObject::CreateEvent<ibEventAction>(m_category, wxT("Action"), _("Action"), wxArrayString{ wxT("Command") }, &ibValueCommandBarItem::GetItemAction, wxNOT_FOUND);
};

// A command bar is a CONTAINER layer object (ibValueLayerObject) — NOT a control; it is PART OF a
// control (a form LAYER the frame owns), rendered as a toolbar over the content. It CONSISTS OF
// commands (m_commands, built from AutoFill or the child items) plus the AutoFill flag. A frame
// OWNS one via ibValuePtr and points it back at itself (SetOwner). The host reads BuildCommands().
class ibValueCommandBar : public ibValueLayerObject {
public:

	virtual ~ibValueCommandBar() {}

	// The control that owns this bar — its action collection feeds AutoFill.
	void SetOwner(ibValueFrame* owner) { m_owner = owner; }
	virtual ibValueFrame* GetOwnerFrame() const override { return m_owner; }
	virtual bool IsLayerContainer() const override { return true; }

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

	// Dispatch a command (by id) — routed to the owner's ExecuteAction. This is the
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
	bool                        m_treeExpanded = true;        // designer tree node open-state

	ibPropertyCategory* m_category = ibPropertyObject::CreatePropertyCategory(wxT("CommandBar"), _("Command bar"));
	ibPropertyBoolean* m_propertyAutoFill = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_category, wxT("AutoFill"), _("Auto fill"), true);
};

#ifndef OES_USE_WEB
// The ONE place that turns a command STORE's entries into a wx toolbar: build it into
// `parent`, or nullptr when empty. Each entry carries its own display mode; clicks
// dispatch through the STORE's own hook. Shared by the form (visualHost) and the
// composite control chrome — form and control render identically.
ibFrontendWindow* BuildCommandBarToolBar(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form);
// UPDATE in place: refresh an EXISTING toolbar's tools from the STORE — same bar object, so
// the stable ref stays valid (no recreation).
void RefreshCommandBarToolBar(ibFrontendWindow* existing, ibValueCommandBar* cbar, ibValueForm* form);
#endif

#endif
