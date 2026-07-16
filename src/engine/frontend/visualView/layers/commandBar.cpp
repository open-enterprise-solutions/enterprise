#include "commandBar.h"
#include "frontend/visualView/ctrl/frame.h"   // ibValueFrame — the owner's action collection + CallAsAction
#include "backend/serialize/dataBuilder.h"     // ibDataNode (layer -> node)
#include "backend/compiler/procUnit.h"         // ibProcUnit::CallAsProc — full type for the CallAsEvent instantiation below (run a command's custom-action procedure)
#ifndef OES_USE_WEB
#include <wx/menu.h>                           // wxMenu (designer layer menu)
#include <wx/app.h>                             // wxTheApp->CallAfter — reveal a new command AFTER the deferred rebuild
#include <wx/artprov.h>                         // menu icons (same art as the control menu)
#include "frontend/win/ctrls/toolBar.h"        // ibAuiToolBar
#include "frontend/win/theme/luna_toolbarart.h"
#include "backend/backend_picture.h"           // ibBackendPicture
#include "backend/system/value/valueEvent.h"   // ibValueActionEvent (Action picker entries)
#include "frontend/visualView/ctrl/form.h"     // ibValueForm::GetMetaData
#include "frontend/visualView/visualHost.h"    // ibFrontendVisualEditorNotebook (designer refresh / select)

namespace {
	// Designer layer-menu command ids. A node is EITHER the bar OR one item, so the two id sets
	// never coexist on the same menu; Paste is shared (both can paste).
	enum {
		kMenuAddCommand = wxID_HIGHEST + 3200,
		kMenuCut, kMenuCopy, kMenuPaste, kMenuDelete, kMenuMoveUp, kMenuMoveDown, kMenuProps,
	};

	// Command clipboard — the last copied / cut command, serialized. Static so Copy in one bar can
	// Paste into another across the whole designer session (same idea as the control clipboard).
	ibDataNode s_commandClipboard;
	bool       s_hasClipboard = false;

	// Paste the clipboard into `bar` as a new command (appended). Keeps the name unique — if the
	// clipboard name collides in this bar, restore the freshly generated one. Returns the new item.
	ibValueCommandBarItem* PasteCommandInto(ibValueCommandBar* bar)
	{
		if (bar == nullptr || !s_hasClipboard)
			return nullptr;
		ibValueCommandBarItem* pasted = bar->AddCommandItem();   // fresh unique name
		const wxString generated = pasted->GetName();
		pasted->ReadData(s_commandClipboard);                    // overwrites fields (incl. name)
		if (bar->HasItemName(pasted->GetName(), pasted))
			pasted->SetName(generated);
		return pasted;
	}
}
#endif

// Command bar — a CONTAINER layer object (the shared runtime/property/metadata/routing base is
// ibValueLayerObject). Holds its commands + the AutoFill flag; the host renders it as a toolbar.

const std::vector<ibCommandEntry>& ibValueCommandBar::BuildCommands()
{
	// (Re)build from scratch so toggling AutoFill stays in sync (a stale auto-filled set must not
	// linger). AutoFill contributes the owner's standard actions; the manual child items are ALWAYS
	// appended on top (else a just-added command is invisible while AutoFill is on — the default).
	// NB: ibActionCollection is a PROTECTED nested type of ibActionDataObject — it can't be named,
	// so every use goes through `auto` (deduced, never spelled). That also blocks hoisting it into
	// a named helper/param, so the manual branch fetches it per action-bound item (few in practice).
	m_commands.clear();
	if (IsAutoFill() && m_owner != nullptr) {
		auto actions = m_owner->GetActionCollection(m_owner->GetTypeForm());
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			const ibActionID id = actions.GetID(i);
			if (id == wxNOT_FOUND) {
				m_commands.emplace_back();   // separator
				continue;
			}
			const ibPictureDescription pic = actions.GetPictureByID(id);
			// Each command takes its default display mode from the action: no picture -> show
			// text; otherwise honour the action's pictureAndText flag.
			const ibRepresentation rep = pic.IsEmptyPicture()
				? ibRepresentation_PictureAndText
				: (actions.IsCreatePictureAndText(id) ? ibRepresentation_PictureAndText : ibRepresentation_Picture);
			m_commands.emplace_back(id, actions.GetCaptionByID(id), pic, rep);
		}
	}
	// Manual commands — each maps to its bound Action's id when one is set (click dispatches through
	// the owner's CallAsAction), else a synthetic id. Caption / picture fall back to the action's
	// when the item leaves them empty. Hidden item (Visible off) dropped; disabled (Enabled off)
	// kept but greyed. Synthetic id stays < 32767 — the overflow dropdown builds a wxMenuItem from
	// it and wxMenuItemBase asserts outside [0, 32767); sit under the ceiling, above any real id.
	ibActionID synthId = 32000;
	for (const auto& item : m_items) {
		if (item == nullptr || !item->IsVisible())
			continue;
		const ibActionID actId = item->GetActionId();
		wxString caption = item->GetCaption();
		ibPictureDescription pic = item->IsEmptyPicture() ? ibPictureDescription() : item->GetPictureDesc();
		if (actId != wxNOT_FOUND && m_owner != nullptr) {
			auto actions = m_owner->GetActionCollection(m_owner->GetTypeForm());
			if (caption.IsEmpty()) caption = actions.GetCaptionByID(actId);
			if (item->IsEmptyPicture()) pic = actions.GetPictureByID(actId);
		}
		m_commands.emplace_back(synthId++, caption, pic, item->GetRepresentation(), item->IsEnabled(), item);
	}
	return m_commands;
}

void ibValueCommandBar::ExecuteCommand(const ibActionID& id, ibBackendValueForm* form)
{
	if (m_owner == nullptr)
		return;
	// A manual command carries a synthetic tool-id -> run its bound Action. The Action is EITHER a
	// SYSTEM action (a built-in id, dispatched through the owner's CallAsAction) OR a CUSTOM action
	// (a form-module handler name, run as a procedure via CallAsEvent) — the same CallAsAction-vs-
	// CallAsEvent split a toolbar item uses (see ibValueToolbar::OnTool). The clicked command item is
	// passed to the handler as its `Command` argument. An AutoFill command has no item
	// (FindItemByCommandId == null) -> the tool-id IS the system action.
	if (ibValueCommandBarItem* item = FindItemByCommandId(id)) {
		const ibActionDescription& actionDesc = item->GetAction();
		if (actionDesc.GetSystemAction() != wxNOT_FOUND)
			m_owner->CallAsAction(actionDesc.GetSystemAction(), form);
		else if (!actionDesc.GetCustomAction().IsEmpty())
			m_owner->CallAsEvent(actionDesc.GetCustomAction(), ibValue(static_cast<ibValue*>(item)));
	}
	else
		m_owner->CallAsAction(id, form);
}

// Designer menu — a container adds a child command (and pastes one when the clipboard is set).
void ibValueCommandBar::PrepareDefaultMenu(wxMenu* menu) const
{
#ifndef OES_USE_WEB
	if (menu == nullptr)
		return;
	menu->Append(kMenuAddCommand, _("Add command"));
	if (s_hasClipboard) {
		wxMenuItem* paste = menu->Append(kMenuPaste, _("Paste\tCtrl+V"));
		paste->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	}
#else
	(void)menu;
#endif
}

void ibValueCommandBar::ExecuteMenu(ibFrontendVisualEditorNotebook* editor, int menuId)
{
#ifndef OES_USE_WEB
	if (editor == nullptr)
		return;
	ibValueCommandBarItem* target = nullptr;
	if (menuId == kMenuAddCommand)
		target = AddCommandItem();
	else if (menuId == kMenuPaste)
		target = PasteCommandInto(this);
	if (target != nullptr) {
		editor->RefreshEditor();                // rebuild tree + re-render chrome (DEFERRED — coalesced onto the next tick)
		// Reveal + select the new command AFTER that rebuild. SelectPropertyObject reveals it in the object
		// tree via SelectCommandItem, which looks the node up in the tree's layer map — and that map is only
		// (re)populated by the deferred rebuild above. A synchronous reveal here would miss the not-yet-created
		// node (the inspector part survives, the tree row would not). CallAfter is FIFO, so it runs once the
		// rebuild has — the same shape as the attribute add's post-rebuild select.
		ibValueCommandBarItem* citem = target;
		wxTheApp->CallAfter([editor, citem] { editor->SelectPropertyObject(citem); });
	}
#else
	(void)editor; (void)menuId;
#endif
}

//***********************************************************************
//*                       Command bar ITEM (child)                      *
//***********************************************************************

ibValueFrame* ibValueCommandBarItem::GetOwnerFrame() const
{
	// Reach the owner frame through the bar — feeds metadata + designer routing (base uses this).
	return m_bar != nullptr ? m_bar->GetOwnerFrame() : nullptr;
}

// Designer menu — a leaf command. Same items / icons / hotkeys as the control popup menu
// (ibVisualEditorItemPopupMenu); the operations act on the command via its bar + the clipboard.
void ibValueCommandBarItem::PrepareDefaultMenu(wxMenu* menu) const
{
#ifndef OES_USE_WEB
	if (menu == nullptr)
		return;
	wxMenuItem* item = nullptr;
	item = menu->Append(kMenuCut,   _("Cut\tCtrl+X"));   item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_CUT, wxART_MENU));
	item = menu->Append(kMenuCopy,  _("Copy\tCtrl+C"));  item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_COPY, wxART_MENU));
	item = menu->Append(kMenuPaste, _("Paste\tCtrl+V")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	item->Enable(s_hasClipboard);
	menu->AppendSeparator();
	item = menu->Append(kMenuDelete, _("Delete\tCtrl+D")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_MENU));
	menu->AppendSeparator();
	item = menu->Append(kMenuMoveUp,   _("Move Up\tAlt+Up"));    item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_MENU));
	item = menu->Append(kMenuMoveDown, _("Move Down\tAlt+Down")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_MENU));
	menu->AppendSeparator();
	item = menu->Append(kMenuProps, _("Properties"));           item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_HELP_SETTINGS, wxART_MENU));
#else
	(void)menu;
#endif
}

void ibValueCommandBarItem::ExecuteMenu(ibFrontendVisualEditorNotebook* editor, int menuId)
{
#ifndef OES_USE_WEB
	if (editor == nullptr)
		return;
	switch (menuId) {
	case kMenuCopy:
		s_commandClipboard = ibDataNode();
		WriteData(s_commandClipboard);
		s_hasClipboard = true;
		break;
	case kMenuCut:
		s_commandClipboard = ibDataNode();
		WriteData(s_commandClipboard);
		s_hasClipboard = true;
		if (m_bar) { m_bar->RemoveCommandItem(this); editor->RefreshEditor(); }
		break;
	case kMenuPaste:
		if (ibValueCommandBarItem* pasted = PasteCommandInto(m_bar)) {
			editor->RefreshEditor();
			editor->SelectPropertyObject(pasted);
		}
		break;
	case kMenuDelete:
		if (m_bar) { m_bar->RemoveCommandItem(this); editor->RefreshEditor(); }
		break;
	case kMenuMoveUp:
		if (m_bar) { m_bar->MoveCommandItem(this, true);  editor->RefreshEditor(); editor->SelectPropertyObject(this); }
		break;
	case kMenuMoveDown:
		if (m_bar) { m_bar->MoveCommandItem(this, false); editor->RefreshEditor(); editor->SelectPropertyObject(this); }
		break;
	case kMenuProps:
		editor->SelectPropertyObject(this);
		break;
	}
#else
	(void)menuId; (void)editor;
#endif
}

bool ibValueCommandBarItem::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	// Name must stay unique among the bar's items.
	if (property == m_propertyName && m_bar != nullptr && m_bar->HasItemName(newValue.GetString(), this))
		return false;
	return true;
}

bool ibValueCommandBarItem::GetItemAction(ibEventAction* evtList)
{
#ifndef OES_USE_WEB
	// The picker lists the OWNER frame's actions (the toolbar item pulls from its ActionSource
	// control; a command bar's source is simply the control it belongs to).
	ibValueFrame* owner = m_bar != nullptr ? m_bar->GetOwnerFrame() : nullptr;
	if (owner == nullptr)
		return false;
	// ibActionCollection is a protected nested type — deduce it via auto, never name it.
	auto data = owner->GetActionCollection(owner->GetTypeForm());
	for (unsigned int i = 0; i < data.GetCount(); i++) {
		const ibActionID& id = data.GetID(i);
		if (id == wxNOT_FOUND)
			continue;
		const ibPictureDescription& pictureDesc = data.GetPictureByID(id);
		evtList->AppendItem(
			data.GetNameByID(id),
			data.GetCaptionByID(id),
			id,
			pictureDesc.IsEmptyPicture() ? wxNullBitmap : ibBackendPicture::CreatePicture(pictureDesc, owner->GetMetaData()),
			ibValue::CreateObjectValue<ibValueActionEvent>(data.GetNameByID(id), id)
		);
	}
	return true;
#else
	(void)evtList;
	return false;
#endif
}

//***********************************************************************
//*                  Shared toolbar render (form + control)             *
//***********************************************************************

#ifndef OES_USE_WEB
// Fill an EXISTING toolbar from the STORE's commands (ClearTools first, so it doubles as the
// in-place UPDATE — same bar object, refreshed content). Returns true if any tool was added.
static bool FillCommandBarToolBar(ibAuiToolBar* bar, ibValueCommandBar* cbar, ibValueForm* form)
{
	bar->ClearTools();
	const std::vector<ibCommandEntry>& cmds = cbar->BuildCommands();
	const ibMetaData* metaData = form != nullptr ? form->GetMetaData() : nullptr;
	bool anyTool = false;
	for (const ibCommandEntry& c : cmds) {
		if (c.id == wxNOT_FOUND) {
			if (anyTool) bar->AddSeparator();
			continue;
		}
		wxBitmap bmp = c.picture.IsEmptyPicture() ? wxNullBitmap
			: ibBackendPicture::CreatePicture(c.picture, metaData);
		wxString label = c.caption;
		if (c.representation == ibRepresentation_Picture) label = wxEmptyString;   // icon only
		else if (c.representation == ibRepresentation_Text) bmp = wxNullBitmap;    // text only
		bar->AddTool(c.id, label, bmp, wxNullBitmap,
			wxItemKind::wxITEM_NORMAL, c.caption, wxEmptyString, nullptr);
		if (!c.enabled)
			bar->EnableTool(c.id, false);   // Enabled off -> greyed but present
		anyTool = true;
	}
	bar->Realize();
	return anyTool;
}

// CREATE: build the toolbar once (stable ref). Empty command set -> nullptr (no part).
ibFrontendWindow* BuildCommandBarToolBar(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form)
{
	if (cbar == nullptr)
		return nullptr;

	ibAuiToolBar* bar = new ibAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxAUI_TB_NO_AUTORESIZE | wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);
	bar->SetArtProvider(new wxAuiLunaToolBarArt());

	// Designer only when the owner form is being edited (FindVisualEditor != null).
	ibValueFrame* ownerFrame = cbar->GetOwnerFrame();
	const bool designer = ownerFrame != nullptr && ownerFrame->FindVisualEditor() != nullptr;
	bar->SetDesignerMode(designer);   // show real width in the editor (no overflow collapse)

	// Build the part ONCE and keep it even when empty — just hide it. Destroying it on an empty
	// first build (e.g. a tableBox whose source/commands aren't set yet) would leave the chrome
	// with NO part, so a later Update (which only refreshes EXISTING parts) could never make the
	// toolbar appear. Hidden now, RefreshCommandBarToolBar shows it the moment a command lands.
	if (!FillCommandBarToolBar(bar, cbar, form))
		bar->Hide();
	bar->Bind(wxEVT_TOOL, [cbar, form, bar](wxCommandEvent& e) {
		// In the DESIGNER a tool click selects the underlying command in the inspector (edit it);
		// at RUNTIME it dispatches the action. FindVisualEditor() is non-null only in the designer.
		ibValueFrame* owner = cbar->GetOwnerFrame();
		ibFrontendVisualEditorNotebook* editor = owner != nullptr ? owner->FindVisualEditor() : nullptr;
		if (editor != nullptr) {
			if (ibValueCommandBarItem* item = cbar->FindItemByCommandId(e.GetId()))
				editor->SelectPropertyObject(item);   // ibValueCommandBarItem is-a ibPropertyObject
		}
		else {
			// Pull focus onto the toolbar first (mirrors ibValueToolbar::OnTool) so the field being
			// edited fires its OnKillFocus and commits the pending value into the source BEFORE the
			// command runs. A custom-drawn tool does not steal focus on its own, so without this a
			// Write reads a stale source (the register write then false-positived "already exists").
			bar->SetFocus();
			cbar->ExecuteCommand(e.GetId(), form);
		}
	});
	if (designer) {
		// Right-click anywhere on the bar shows the bar's OWN designer menu (Add command / Paste) —
		// the SAME PrepareDefaultMenu / ExecuteMenu the tree uses, so there's one place for it.
		bar->Bind(wxEVT_RIGHT_DOWN, [cbar, bar](wxMouseEvent& e) {
			ibValueFrame* owner = cbar->GetOwnerFrame();
			ibFrontendVisualEditorNotebook* editor = owner != nullptr ? owner->FindVisualEditor() : nullptr;
			if (editor == nullptr) { e.Skip(); return; }
			wxMenu menu;
			cbar->PrepareDefaultMenu(&menu);
			const int sel = bar->GetPopupMenuSelectionFromUser(menu, e.GetPosition());
			if (sel != wxID_NONE)
				cbar->ExecuteMenu(editor, sel);
		});
	}
	return bar;
}

// UPDATE in place: refresh the SAME bar's tools — no recreation, the ref stays valid.
void RefreshCommandBarToolBar(ibFrontendWindow* existing, ibValueCommandBar* cbar, ibValueForm* form)
{
	ibAuiToolBar* bar = wxDynamicCast(existing, ibAuiToolBar);
	if (bar == nullptr)
		return;
	// cbar == nullptr => the layer is SUPPRESSED (the control is now bound to the form's main
	// source, so its commands live on the FORM toolbar). Hide the strip in place; the parent
	// reclaims the space. A later Update with a live cbar shows it again — no rebuild.
	if (cbar == nullptr) {
		if (bar->IsShown()) {
			bar->Show(false);
			if (wxWindow* parent = bar->GetParent())
				parent->Layout();
		}
		return;
	}
	const bool anyTool = FillCommandBarToolBar(bar, cbar, form);
	// No items -> HIDE the whole toolbar layer (no empty strip); the parent re-lays out to
	// reclaim the space. Shown again as soon as it has tools.
	if (bar->IsShown() != anyTool) {
		bar->Show(anyTool);
		if (wxWindow* parent = bar->GetParent())
			parent->Layout();
	}
}
#endif

//***********************************************************************
//*                          Serialization                              *
//***********************************************************************
// The composite window writes a "Layers" block that holds the command bar (see window.cpp).
// The bar writes its AutoFill flag + one sub-node per child command; each command writes its
// own fields — same pattern a toolbar item uses.

bool ibValueCommandBarItem::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_eventAction->GetName(), m_eventAction->GetNodeValue());
	return true;
}

bool ibValueCommandBarItem::ReadData(const ibDataNode& node)
{
	m_propertyName->ReadNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->ReadNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyRepresentation->ReadNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyPicture->ReadNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyTooltip->ReadNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->ReadNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->ReadNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_eventAction->ReadNodeValue(node.GetProperty(m_eventAction->GetName()));
	return true;
}

bool ibValueCommandBar::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAutoFill->GetName(), m_propertyAutoFill->GetNodeValue());
	// One child sub-node per command, in order. clsid 0 + index — the read side iterates
	// Children() positionally, so only the order matters, not the node type/identity.
	ibMetaID idx = 0;
	for (const auto& item : m_items) {
		if (item != nullptr)
			item->WriteData(node.AddChild(0, idx++));
	}
	return true;
}

bool ibValueCommandBar::ReadData(const ibDataNode& node)
{
	m_propertyAutoFill->ReadNodeValue(node.GetProperty(m_propertyAutoFill->GetName()));
	m_items.clear();
	for (const ibDataNode& child : node.Children()) {
		ibValueCommandBarItem* item = AddCommandItem();   // creates + SetBar; ReadData overwrites the name
		item->ReadData(child);
	}
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

// A runtime value must be registered. It is a SYSTEM type (clsid = system_to_clsid("CommandBar")).
SYSTEM_TYPE_REGISTER(ibValueCommandBar, "CommandBar");
SYSTEM_TYPE_REGISTER(ibValueCommandBarItem, "CommandBarItem");
