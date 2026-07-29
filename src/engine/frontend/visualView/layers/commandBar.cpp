#include "commandBar.h"
#include "frontend/visualView/ctrl/frame.h"   // ibValueFrame — the owner's action collection + CallAsAction
#include "frontend/visualView/ctrl/form.h"    // ibValueForm::IsViewOnly — grey data-modifying commands in view-only
#include "frontend/visualView/ctrl/formCommand.h"   // ibFormCommandValue — form-local command (gather section 1)
#include "backend/serialize/dataBuilder.h"     // ibDataNode (layer -> node)
#include "backend/metaCollection/genericData.h"  // ibValueMetaObjectGenericData::GetCommandArrayObject — the owner object's own commands
#include "backend/metaCollection/metaCommandObject.h" // ibValueMetaObjectCommand::GetModifiesData (view-only greying)
#include "backend/metaData.h"                   // ibMetaData::FindAnyObjectByFilter
#include "frontend/visualView/ctrl/typeControl.h" // ibTypeControlFactory::GetSourceDesc — find the control a table-command's source hop points at
#include "backend/sourceDescription.h"          // ibSourceHop — the bound control's source path (source-hop match)
#include "backend/metaCollection/metaObject.h"  // g_metaCommandCLSID — general (root) commands
#include "backend/metaCollection/metaSectionObject.h" // ibValueMetaObjectSection — walk sections like the menu builder
#include <functional>                            // std::function — the recursive section walk
#include "frontend/visualView/ctrl/tableBox.h"  // g_controlTableBoxCLSID — the form's tables (their own commands)
#include "backend/backend_picture.h"            // ibBackendPicture::CreatePicture — an action-command's own picture
#include "backend/srcDataObject.h"              // ibSourceDataObject / ibSourceExplorer + IsReference — the form's data types (parameterized-command filter)
#include "backend/typeDescription.h"            // ibTypeDescription::GetClsidList — a command's parameter type
#include <set>                                  // std::set — the form's reference-type set
#ifndef OES_USE_WEB
#include <wx/menu.h>                           // wxMenu (designer layer menu)
#include <wx/app.h>                             // wxTheApp->CallAfter — reveal a new command AFTER the deferred rebuild
#include <wx/artprov.h>                         // menu icons (same art as the control menu)
#include "frontend/win/ctrls/toolBar.h"        // ibAuiToolBar
#include "frontend/win/theme/luna_toolbarart.h"
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

// Collect every tablebox under `frame` (recursive) — the form's tables, each vending its OWN commands.
static void CollectTableboxes(ibValueFrame* frame, std::vector<ibValueFrame*>& out)
{
	if (frame == nullptr)
		return;
	if (frame->GetClassType() == g_controlTableBoxCLSID)
		out.push_back(frame);
	for (unsigned int i = 0; i < frame->GetChildCount(); i++)
		CollectTableboxes(frame->GetChild(i), out);
}

// The three command sections of a form, in display order (1C model). GatherFormCommands labels its entries with
// exactly these, and the navigator / picker pre-create all three so they show even when empty. ONE source of the
// names -> the surfaces can't drift.
std::vector<wxString> GetCommandSections()
{
	// The 1C model — three command SOURCES: [0] the form's own commands, [1] standard actions (form + tables),
	// [2] global commands (config-wide: general commands + every object's own commands, by full name).
	return { _("Form commands"), _("Standard commands"), _("Global commands") };
}

// Collect the REFERENCE types present in a form's data — its primary source type + every explorer node
// (recursively). This is the set a PARAMETERIZED command is matched against.
static void CollectExplorerRefTypes(const ibSourceDataObject::ibSourceExplorer* node, std::set<ibClassID>& out)
{
	if (node == nullptr)
		return;
	for (const ibClassID& c : node->GetClsidList())
		if (IsReference(c))
			out.insert(c);
	for (unsigned int i = 0; i < node->GetHelperCount(); i++)
		CollectExplorerRefTypes(node->GetHelper(i), out);
}
static std::set<ibClassID> CollectFormDataTypes(ibValueForm* form)
{
	std::set<ibClassID> types;
	// The form's PRIMARY object type from metadata — always available, incl. the designer (where the runtime
	// source object below may not be populated yet).
	if (const ibValueMetaObjectGenericData* obj = form->GetMetaObject())
		types.insert(reference_to_clsid(obj->GetMetaID()));
	// Plus every reference type in the form's source data (primary + explorer nodes, recursively).
	if (ibSourceDataObject* src = form->GetSourceObject()) {
		if (IsReference(src->GetSourceClassType()))
			types.insert(src->GetSourceClassType());
		CollectExplorerRefTypes(src->GetSourceExplorer(), types);
	}
	return types;
}

// A "parameterizable" command (its Parameter type names >= 1 REFERENCE type) is available ONLY where the form
// carries data of a matching type — 1C's typed commands. A command with no reference parameter type is available
// everywhere (returns false = not excluded).
static bool CommandExcludedByType(const ibValueMetaObjectCommand* cmd, const std::set<ibClassID>& formTypes)
{
	bool parameterized = false;
	for (const ibClassID& t : cmd->GetParameterType().GetClsidList())
		if (IsReference(t)) {
			parameterized = true;
			if (formTypes.count(t) > 0)
				return false;   // matches a form data type -> keep
		}
	return parameterized;   // typed but no form type matches -> exclude; untyped -> keep
}

// The shared three-section gather (1C model) — the command-door twin of walking a form's source explorers. Feeds
// BOTH the navigator panel and the inspector's command-source picker, so they can never drift.
std::vector<ibCommandSourceEntry> GatherFormCommands(ibValueForm* form)
{
	std::vector<ibCommandSourceEntry> out;
	if (form == nullptr)
		return out;

	const ibMetaData* metaData = form->GetMetaData();
	// The full qualified name is built in ONE place — ibValueMetaObject::GetFullName (command override adds the
	// "GlobalCommands." root for a general command) and ibFormCommandValue::GetFullName ("Form.<name>"). The menu
	// builder, the section fill and this picker all read it, so the surfaces never drift.
	// The COMMAND determines its picture — from its OWN Picture property. No picture on the command -> NO icon
	// (text-only projection); we do NOT force the metatype glyph. A table action bakes its own picture (section 2).
	auto commandIcon = [](ibValueMetaObjectCommand* cmd) -> wxBitmap {
		return cmd->IsEmptyPicture() ? wxNullBitmap : cmd->GetPictureAsBitmap();
	};
	auto actionIcon = [metaData](const ibPictureDescription& pic) -> wxBitmap {
		return pic.IsEmptyPicture() ? wxNullBitmap : ibBackendPicture::CreatePicture(pic, metaData);
	};

	const std::vector<wxString> sections = GetCommandSections();
	const wxString& sForm = sections[0], sStd = sections[1], sGlobal = sections[2];

	// (Section 1's form commands are ibFormCommandValue, NOT command metaobjects, so section 3's by-classid pull of
	// ibValueMetaObjectCommand can never re-emit them — no cross-section dedup is needed.)

	// 1 — FORM COMMANDS: the form's OWN commands = form-local EVENTS (ibFormCommandValue property objects, managed
	// like attributes). A form command names a form-runtime procedure the button delegates to — NOT a metaobject.
	// A form command is a NORMAL 1-hop [id] path, like any other command — the walk resolves that id against the
	// gate form's own command registry (no special route). Bind by ID (stable across rename); the display shows the
	// current NAME, resolved live like a source path. A command with no handler yet is still real.
	for (const ibValuePtr<ibFormCommandValue>& fc : form->GetFormCommands()) {
		if (fc == nullptr)
			continue;
		const ibCommandDescription desc(fc->GetId());   // 1-hop [id] — the form command's own id
		const wxBitmap icon = fc->IsEmptyPicture() ? wxNullBitmap : fc->GetPictureBitmap();
		out.push_back({ sForm, fc->GetName(), desc, icon, wxEmptyString, fc->GetFullName() });   // tree: name; cell: Form.<name>
	}

	// 2 — STANDARD COMMANDS: the standard actions of the form's source AND of every tablebox control, all under
	// ONE section but SUB-grouped by source (the form vs each table). The form's actions carry [actionId]; a
	// table's carry [table-source, actionId] so a click lands on THAT table.
	{
		auto actions = form->GetStandardCommands(form->GetTypeForm());
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			const ibActionID id = actions.GetID(i);
			if (id != wxNOT_FOUND) {
				const wxString caption = actions.GetCaptionByID(id);   // tree label — the readable action text
				const wxString name    = actions.GetNameByID(id);
				// cell PATH = "Form.<action>" — by NAME (the configurator addresses by name), but a standard action
				// with no name falls back to the caption so the path never ends up crooked ("Form." with an empty leaf).
				out.push_back({ sStd, caption, ibCommandDescription(id), actionIcon(actions.GetPictureByID(id)), _("Form"),
				                wxT("Form.") + (name.IsEmpty() ? caption : name) });
			}
		}
	}
	std::vector<ibValueFrame*> tableboxes;
	CollectTableboxes(form, tableboxes);
	for (ibValueFrame* table : tableboxes) {
		// A table bound to the form's MAIN source has its OWN command interface SUPPRESSED — the form toolbar
		// already serves those commands. So its commands are reachable ONLY through the form (section 2 "Form"
		// above), NOT as a separate table sub-group here; listing them would duplicate the suppressed interface.
		if (table->IsMainSourceBound())
			continue;
		const ibTypeControlFactory* factory = dynamic_cast<const ibTypeControlFactory*>(table);
		if (factory == nullptr)
			continue;
		const std::vector<ibSourceHop>& srcPath = factory->GetSourceDesc().GetPath();
		if (srcPath.empty())
			continue;
		auto actions = table->GetStandardCommands(table->GetTypeForm());
		const wxString tableName = table->GetControlName();
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			const ibActionID id = actions.GetID(i);
			if (id == wxNOT_FOUND)
				continue;
			ibCommandDescription desc;
			desc.AppendCommand(srcPath.front().m_id);   // hop 1: the table's source
			desc.AppendCommand(id);                     // leaf: the action on it (resolved at walk time)
			const wxString caption = actions.GetCaptionByID(id);   // tree label — the readable action text
			// cell PATH = "Form.<table>.<action NAME>" — the FULL route to the command by NAME (configurator uses
			// names, not synonyms), so you can see WHERE a table command lives, not just its bare caption.
			out.push_back({ sStd, caption, desc, actionIcon(actions.GetPictureByID(id)), tableName,
			                wxT("Form.") + tableName + wxT(".") + actions.GetNameByID(id) });
		}
	}

	// 3 — GLOBAL COMMANDS: EVERY command metaobject in the config (recursive by classid). This is the flat "all
	// commands" source (form commands in section 1 are a different type, so they never appear here). Read from the config the form's
	// DATA belongs to (form->GetMetaObject()->GetMetaData()), NOT the form metaobject's own metaData nor the global
	// activeMetaData; fall back to the form's metaData only when the form has no data object. A PARAMETERIZED command
	// (reference Parameter type) shows ONLY where the form carries a matching data type; an untyped one always.
	const ibMetaData* globalMeta = form->GetMetaObject() != nullptr ? form->GetMetaObject()->GetMetaData() : metaData;
	const std::set<ibClassID> formTypes = CollectFormDataTypes(form);
	if (globalMeta != nullptr)
		for (ibValueMetaObjectCommand* cmd : globalMeta->GetAnyArrayObject<ibValueMetaObjectCommand>({ g_metaCommonCommandCLSID, g_metaCommandCLSID }, /*use_child_filter*/ true))
			if (cmd != nullptr && !cmd->IsDeleted() && !CommandExcludedByType(cmd, formTypes))
				out.push_back({ sGlobal, cmd->GetName(), ibCommandDescription(cmd->GetMetaID()), commandIcon(cmd), wxEmptyString, cmd->GetFullName() });

	// 4 — SECTION COMMANDS: the form sees sections EXACTLY as the runtime menu builder does (mainFrameEnterpriseInterface)
	// — each section's items laid out by AREA through the SAME method, GetInterfaceItemArrayObject (membership by
	// IsSetInterface, area by the item's GetCommandSection — property-driven for a command), subsections walked
	// recursively via GetInterfaceArrayObject. Only COMMAND items are pickable here; a command included in a section
	// shows under [section] > [area] and binds to a button the same way it runs in the menu. One traversal, no drift.
	if (globalMeta != nullptr) {
		const ibInterfaceCommandSection kAreas[] = {
			ibInterfaceCommandSection_Important, ibInterfaceCommandSection_Default,
			ibInterfaceCommandSection_Create,    ibInterfaceCommandSection_Report, ibInterfaceCommandSection_Service };
		auto areaLabel = [](ibInterfaceCommandSection area) -> wxString {
			switch (area) {
				case ibInterfaceCommandSection_Important: return _("Important");
				case ibInterfaceCommandSection_Create:    return _("Create");
				case ibInterfaceCommandSection_Report:    return _("Reports");
				case ibInterfaceCommandSection_Service:   return _("Service");
				default:                                  return _("Normal");
			}
		};
		std::function<void(ibValueMetaObjectSection*)> walkSection = [&](ibValueMetaObjectSection* section) {
			if (section == nullptr)
				return;
			for (ibInterfaceCommandSection area : kAreas) {
				std::vector<ibValueMetaObject*> items;
				section->GetInterfaceItemArrayObject(area, items);
				// EVERY item the section holds (a command, but also a constant / catalog / … checked into it) — like
				// the runtime menu builder. Icon BY METATYPE (item->GetIcon(), exactly as the menu's df->SetBitmap),
				// tree leaf = the item's NAME, cell = its full PATH. An OBJECT item carries the AREA's command TYPE
				// (Create -> create, else open the list), so the SAME object in Normal vs Create is a DISTINCT binding
				// that runs the right thing (Execute by command type), exactly as clicking it in the menu does.
				const ibInterfaceCommandType areaType = (area == ibInterfaceCommandSection_Create)
					? ibInterfaceCommandType_Create : ibInterfaceCommandType_Default;
				for (ibValueMetaObject* item : items) {
					if (item == nullptr || item->IsDeleted())
						continue;
					ibCommandDescription desc(item->GetMetaID());
					desc.SetCommandType(areaType);
					// NAME only (the group is already known from the tree — no redundant "Class.Name"), but a CREATE-area
					// item carries the ": Create" tag so the SAME object reads distinctly from its Normal-area "open list"
					// binding — on the navigator AND on a dropped button / bar item.
					wxString name = item->GetName();
					if (areaType == ibInterfaceCommandType_Create)
						name += wxT(": ") + wxString(_("Create"));
					out.push_back({ section->GetName(), name, desc, wxBitmap(item->GetIcon()), areaLabel(area), name });
				}
			}
			for (ibValueMetaObjectSection* sub : section->GetInterfaceArrayObject())
				walkSection(sub);
		};
		for (ibValueMetaObjectSection* section : globalMeta->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID))
			walkSection(section);
	}

	return out;
}

const std::vector<ibCommandEntry>& ibValueCommandBar::BuildCommands()
{
	// (Re)build from scratch so toggling AutoFill stays in sync (a stale auto-filled set must not
	// linger). AutoFill contributes the owner's standard actions; the manual child items are ALWAYS
	// appended on top (else a just-added command is invisible while AutoFill is on — the default).
	// NB: ibStandardCommandSet is a PROTECTED nested type of ibStandardCommandSource — it can't be named,
	// so every use goes through `auto` (deduced, never spelled). That also blocks hoisting it into
	// a named helper/param, so the manual branch fetches it per action-bound item (few in practice).
	m_commands.clear();
	m_autoItems.clear();   // release last round's transient object-command items before rebuilding
	// A view-only form greys every DATA-MODIFYING command (Save / Post / Create / Mark-for-delete / …);
	// read-only commands (Refresh / Filter / Sort / open) stay live. The modify flag is ALWAYS the command's own:
	// AutoFill reads it off the action collection / the object command, a manual item off its bound command (door).
	const bool viewOnly = m_owner != nullptr && m_owner->GetOwnerForm() != nullptr
		&& m_owner->GetOwnerForm()->IsViewOnly();
	if (IsAutoFill() && m_owner != nullptr) {
		auto actions = m_owner->GetStandardCommands(m_owner->GetTypeForm());
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
			const bool enabled = !(viewOnly && actions.GetModifiesDataByID(id));   // greyed if it changes data
			m_commands.emplace_back(id, actions.GetCaptionByID(id), pic, rep, enabled);
		}
	}
	// AutoFill ALSO surfaces the owner OBJECT's OWN commands — its structural command children (the
	// "Commands" node under a Catalog / Document / …). This is auto-generation honoring the object's own commands:
	// each object command becomes a TRANSIENT item (m_autoItems, not serialized) carrying the command
	// by metaId, so a click resolves + runs it through the exact same path a manually-bound command
	// button uses (ExecuteCommand -> GetCommandMetaId -> ibBackendCommandItem::Execute). The owner match
	// is by CONSTRUCTION — GetCommandArrayObject returns only THIS object's commands.
	// Gate on the FORM'S OWN bar (m_owner is the ibValueForm): a sub-control's bar (a tablebox / list) must
	// NOT auto-surface object commands — those land there only when one is deliberately dropped on it
	// (placement is the designer's choice, not forced).
	if (ibValueForm* ownerForm = (IsAutoFill() ? dynamic_cast<ibValueForm*>(m_owner) : nullptr)) {
		const ibValueMetaObjectGenericData* obj = ownerForm->GetMetaObject();
		if (obj != nullptr) {
			// object-command range: above any real action id, below the manual synthetic ids (32000+),
			// under the 32767 tool-id ceiling wxMenuItemBase asserts on.
			ibActionID cmdId = 31000;
			for (ibValueMetaObjectCommand* cmd : obj->GetCommandArrayObject()) {
				if (cmd == nullptr || cmd->IsDeleted())
					continue;
				ibValueCommandBarItem* item = new ibValueCommandBarItem();
				item->SetBar(this);
				item->SetName(cmd->GetName());
				item->SetCommandMetaId(cmd->GetMetaID());
				m_autoItems.emplace_back(item);   // owns it for the lifetime of m_commands (raw ref below)
				wxString caption = cmd->GetSynonym();
				if (caption.IsEmpty()) caption = cmd->GetName();
				const bool modifies = cmd->GetModifiesData();
				const wxBitmap icon = wxBitmap(cmd->GetIcon());   // the command determines its picture
				m_commands.emplace_back(cmdId++, caption, ibPictureDescription(),
					ibRepresentation_PictureAndText, !(viewOnly && modifies), item, icon);
			}
		}
	}
	// Manual commands — each maps to its bound Action's id when one is set (click dispatches through
	// the owner's CallAsAction), else a synthetic id. Caption / picture fall back to the action's
	// when the item leaves them empty. Hidden item (Visible off) dropped; disabled (Enabled off)
	// kept but greyed. Synthetic id stays < 32767 — the overflow dropdown builds a wxMenuItem from
	// it and wxMenuItemBase asserts outside [0, 32767); sit under the ceiling, above any real id.
	// Each item's existence + look come from ResolveCommand (below) — the ONE resolve every projection shares, so a
	// bar item, a bare button and the inspector cell can't drift on "is this command alive / what's its caption+icon".
	ibActionID synthId = 32000;
	for (const auto& item : m_items) {
		if (item == nullptr || !item->IsVisible())
			continue;
		const ibCommandDescription bindDesc = item->GetBindingDesc();
		// A command projection renders ONLY when it carries a command (actionEvent is retired — the command is the
		// sole binding). An item with none is inert — skip it, no empty tool on the bar (same rule as a bare button
		// and as a command dropped on a tableBox).
		if (!bindDesc.IsOk())
			continue;
		// The COMMAND owns the item's look AND its "modifies data" flag; the item merely obeys. THE one resolve
		// (ResolveCommand on the door): walk + reliable gather fallback -> existence + caption + icon + modifies, the
		// SAME decision the button and the inspector cell use. A truly gone command misses BOTH -> drop the projection
		// entirely (nothing to click). The item's own caption / picture only OVERRIDE the look where set.
		wxString cmdCaption; wxBitmap cmdIcon; bool cmdModifies = true; bool cmdPicAndText = true;
		if (!ResolveCommand(bindDesc, cmdCaption, cmdIcon, &cmdModifies, nullptr, &cmdPicAndText))
			continue;
		const wxString caption = item->GetCaption().IsEmpty() ? cmdCaption : item->GetCaption();
		const ibPictureDescription pic = item->IsEmptyPicture() ? ibPictureDescription() : item->GetPictureDesc();
		const wxBitmap icon = item->IsEmptyPicture() ? cmdIcon : wxBitmap();   // command icon only when the item sets none
		const bool enabled = item->IsEnabled() && !(viewOnly && cmdModifies);   // greyed per the COMMAND's flag
		// The item's OWN Representation wins; left on Auto it takes the COMMAND's default (a standard action like
		// Close / Update is picture-only, Add / Post is picture+text) — no longer forced to text+picture.
		ibRepresentation rep = item->GetRepresentation();
		if (rep == ibRepresentation::ibRepresentation_Auto)
			rep = cmdPicAndText ? ibRepresentation::ibRepresentation_PictureAndText : ibRepresentation::ibRepresentation_Picture;
		m_commands.emplace_back(synthId++, caption, pic, rep, enabled, item, icon);
	}
	return m_commands;
}

// The bar IS-A command door (ibFrontendCommandReceiver) — the walk's gate is the owner frame's form.
ibValueForm* ibValueCommandBar::GetCommandGateForm() const
{
	return GetOwnerFrame() != nullptr ? GetOwnerFrame()->GetOwnerForm() : nullptr;
}

// ibFrontendCommandReceiver gate — a bar item is a door that resolves from its BAR's gate form (the owner frame's
// form). The inherited ExecuteValueByPath / ResolveValueByPath / WalkCommand all start here; no per-item WalkCommand.
ibValueForm* ibValueCommandBarItem::GetCommandGateForm() const
{
	const ibValueCommandBar* bar = GetBar();
	return bar != nullptr ? bar->GetCommandGateForm() : nullptr;
}


void ibValueCommandBar::ExecuteCommand(const ibActionID& id, ibBackendValueForm* form)
{
	if (m_owner == nullptr)
		return;
	// A manual command carries a synthetic tool-id -> resolve the item's command-hop binding. AutoFill /
	// standard commands have no item (FindItemByCommandId == null) -> the tool-id IS the system action.
	if (ibValueCommandBarItem* item = FindItemByCommandId(id)) {
		// Walk the command-hop binding through the command OBJECT — it resolves each id and EXECUTES the leaf
		// (a source object walks a path to FETCH a value; this walks to RUN a command). actionEvent is retired,
		// so this hop path is the ONLY dispatch for a manual item.
		const ibCommandDescription desc = item->GetBindingDesc();
		if (desc.IsOk())
			ExecuteValueByPath(desc);   // the bar IS-A command door; gate = its owner frame's form
	}
	else
		m_owner->CallAsAction(id, form);   // AutoFill / standard command — the tool id IS the system action
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
		// The command's own live bitmap (resolved in BuildCommands) wins; fall back to a picture description.
		wxBitmap bmp = c.bitmap.IsOk() ? c.bitmap
			: (c.picture.IsEmptyPicture() ? wxNullBitmap : ibBackendPicture::CreatePicture(c.picture, metaData));
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

// The name every command-bar toolbar widget carries — the designer's drop resolver matches it, then reads the bar
// off the widget's client data. A private sentinel: only BuildCommandBarToolBar sets it, only the helper reads it.
static const wxString g_commandBarToolBarName = wxT("oes_command_bar_toolbar");

ibValueCommandBar* ibCommandBarFromToolBar(wxWindow* w)
{
	if (w == nullptr || w->GetName() != g_commandBarToolBarName)
		return nullptr;
	return static_cast<ibValueCommandBar*>(w->GetClientData());
}

// CREATE: build the toolbar once (stable ref). Empty command set -> nullptr (no part).
ibFrontendWindow* BuildCommandBarToolBar(ibFrontendWindow* parent, ibValueCommandBar* cbar, ibValueForm* form)
{
	if (cbar == nullptr)
		return nullptr;

	ibAuiToolBar* bar = new ibAuiToolBar(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxAUI_TB_NO_AUTORESIZE | wxAUI_TB_HORZ_TEXT | wxAUI_TB_OVERFLOW);
	bar->SetArtProvider(new wxAuiLunaToolBarArt());
	// Tag the widget with ITS command bar so a command DROPPED onto this toolbar joins THIS bar (a new item) instead
	// of spawning a free button. The designer's drop resolver reads the name sentinel, then the bar off the client data.
	bar->SetName(g_commandBarToolBarName);
	bar->SetClientData(cbar);

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
	node.SetProperty(m_propertyCommand->GetName(), m_propertyCommand->GetNodeValue());   // command SOURCE (the hop path)
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
	m_propertyCommand->ReadNodeValue(node.GetProperty(m_propertyCommand->GetName()));   // command SOURCE (the hop path)
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
