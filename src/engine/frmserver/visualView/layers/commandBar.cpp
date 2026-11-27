#include "commandBar.h"
#include "frmserver/visualView/ctrl/frame.h"   // ibValueFrame — the owner's action collection + CallAsAction
#include "frmserver/visualView/ctrl/form.h"    // ibValueForm::IsViewOnly — grey data-modifying commands in view-only
#include "frmserver/visualView/ctrl/formCommand.h"   // ibFormCommandValue — form-local command (gather section 1)
#include "core/serialize/dataBuilder.h"     // ibDataNode (layer -> node)
#include "backend/metaCollection/genericData.h"  // ibValueMetaObjectGenericData::GetCommandArrayObject — the owner object's own commands
#include "backend/metaCollection/metaCommandObject.h" // ibValueMetaObjectCommand::GetModifiesData (view-only greying)
#include "backend/metaData.h"                   // ibMetaData::FindAnyObjectByFilter
#include "frmserver/visualView/ctrl/typeControl.h" // ibTypeControlFactory::GetSourceDesc — find the control a table-command's source hop points at
#include "core/description/sourceDescription.h"          // ibSourceHop — the bound control's source path (source-hop match)
#include "backend/metaCollection/metaObject.h"  // g_metaCommandCLSID — general (root) commands
#include "backend/metaCollection/metaSectionObject.h" // ibValueMetaObjectSection — walk sections like the menu builder
#include "backend/metaCollection/metaCommandGroupObject.h" // the platform's groups + the declared ones, in order
#include <functional>                            // std::function — the recursive section walk
#include "frmserver/visualView/ctrl/tableBox.h"  // g_controlTableBoxCLSID — the form's tables (their own commands)
#include "frmserver/visualView/visualHost.h"    // IsStudioHost — who draws a command with nothing to run
#include "backend/backend_picture.h"            // ibBackendPicture::SendPicture — every command's picture, by its id
#include "backend/srcDataObject.h"              // ibSourceDataObject / ibSourceExplorer + IsReference — the form's data types (parameterized-command filter)
#include "core/description/typeDescription.h"            // ibTypeDescription::GetClsidList — a command's parameter type
#include "backend/tabularModel.h"               // ibValueModel::GetModelComposer — the setting a quick filter edits
#include "backend/composition/dataComposer.h"   // the two settings sections + GetCurrentFilterDesc
#include "backend/featureOption/featureOptionGate.h"   // ibFeatureOptionGate — commands of a part this base does not use
#include "backend/backend_composition.h"        // ibFilterNodeDescription / ibFilterDisplayMode_QuickAccess + ibStoredValue
#include <set>                                  // std::set — the form's reference-type set
#include <map>
#include "protocol/protocol.h"                  // ibProtocol::ibForm::ibCommandBar — the command tree's rows
#include "protocol/protocolStudio.h"   // ibProtocol::ibSchema::ibProperties

// Command bar — a CONTAINER layer object (the shared runtime/property/metadata base is
// ibValueLayerObject). Holds its commands + the AutoFill flag; a client draws it from the frame.

// Collect every table under `frame` (recursive) — the form's tables, each vending its OWN commands.
static void CollectTables(ibValueFrame* frame, std::vector<ibValueFrame*>& out)
{
	if (frame == nullptr)
		return;
	if (frame->GetClassType() == g_controlTableBoxCLSID)
		out.push_back(frame);
	for (unsigned int i = 0; i < frame->GetChildCount(); i++)
		CollectTables(frame->GetChild(i), out);
}

// The three command sections of a form, in display order. GatherFormCommands labels its entries with
// exactly these, and the navigator / picker pre-create all three so they show even when empty. ONE source of the
// names -> the surfaces can't drift.
std::vector<ibString> GetCommandSections()
{
	// Three command SOURCES: [0] the form's own commands, [1] standard actions (form + tables),
	// [2] global commands (application-wide: general commands + every object's own commands, by full name).
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
	// The form's PRIMARY object type from metadata — always available, incl. the studio (where the runtime
	// source object below may not be populated yet).
	if (const ibValueMetaObjectGenericData* obj = form->GetMetaObject())
		types.insert(reference_to_clsid(obj->GetMetaID(), clsid_metaclass(obj->GetClassType())));
	// Plus every reference type in the form's source data (primary + explorer nodes, recursively).
	if (ibSourceDataObject* src = form->GetSourceObject()) {
		if (IsReference(src->GetSourceClassType()))
			types.insert(src->GetSourceClassType());
		CollectExplorerRefTypes(src->GetSourceExplorer(), types);
	}
	return types;
}

// ⭐ A PARAMETER TYPE TAKES A FORM'S TYPE when it is that type or a barrier over it — a print command for
// `DocumentRef` stands on every document's form, one for `AnyRef` on every form with a reference. Compared as
// equal, a barrier matched no form at all. What a type admits is one range of ids (clsid_admitted_max), and
// the form's types are an ordered set: one lookup.
static bool AdmitsFormType(const ibClassID& parameterType, const std::set<ibClassID>& formTypes)
{
	const auto first = formTypes.lower_bound(parameterType);
	return first != formTypes.end() && *first <= clsid_admitted_max(parameterType);
}

// A "parameterizable" command (its Parameter type names >= 1 REFERENCE type) is available ONLY where the form
// carries data of a matching type — a TYPED command. A command with no reference parameter type is available
// everywhere (returns false = not excluded).
static bool CommandExcludedByType(const ibValueMetaObjectCommand* cmd, const std::set<ibClassID>& formTypes)
{
	bool parameterized = false;
	for (const ibClassID& t : cmd->GetParameterType().GetClsidList())
		if (IsReference(t)) {
			parameterized = true;
			if (AdmitsFormType(t, formTypes))
				return false;   // matches a form data type -> keep
		}
	return parameterized;   // typed but no form type matches -> exclude; untyped -> keep
}

// …the stricter question a form's OWN bar asks: is the command typed FOR this form's data? An untyped command
// is available everywhere (above), but it does not stand on every form's bar.
static bool CommandIsTypedFor(const ibValueMetaObjectCommand* cmd, const std::set<ibClassID>& formTypes)
{
	for (const ibClassID& t : cmd->GetParameterType().GetClsidList())
		if (IsReference(t) && AdmitsFormType(t, formTypes))
			return true;
	return false;
}

// The shared three-section gather — the command-door twin of walking a form's source explorers. Feeds
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
	auto commandPicture = [](ibValueMetaObjectCommand* cmd) -> ibPictureDescription {
		return cmd->IsEmptyPicture() ? ibPictureDescription() : cmd->GetPictureDesc();
	};

	const std::vector<ibString> sections = GetCommandSections();
	const ibString& sForm = sections[0], sStd = sections[1], sGlobal = sections[2];

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
		const ibPictureDescription picture = fc->IsEmptyPicture() ? ibPictureDescription() : fc->GetPictureDesc();
		out.push_back({ sForm, fc->GetName(), desc, picture, ibEmptyString, fc->GetFullName() });   // tree: name; cell: Form.<name>
	}

	// 2 — STANDARD COMMANDS: the standard actions of the form's source AND of every tablebox control, all under
	// ONE section but SUB-grouped by source (the form vs each table). The form's actions carry [actionId]; a
	// table's carry [table-source, actionId] so a click lands on THAT table.
	{
		auto actions = form->GetStandardCommands(form->GetTypeForm());
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			const ibActionID id = actions.GetID(i);
			if (id != ibNOT_FOUND) {
				const ibString caption = actions.GetCaptionByID(id);   // tree label — the readable action text
				const ibString name    = actions.GetNameByID(id);
				// cell PATH = "Form.<action>" — by NAME (the configurator addresses by name), but a standard action
				// with no name falls back to the caption so the path never ends up crooked ("Form." with an empty leaf).
				out.push_back({ sStd, caption, ibCommandDescription(id), actions.GetPictureByID(id), _("Form"),
				                ibT("Form.") + (name.IsEmpty() ? caption : name) });
			}
		}
	}
	std::vector<ibValueFrame*> tables;
	CollectTables(form, tables);
	for (ibValueFrame* table : tables) {
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
		const ibString tableName = table->GetControlName();
		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			const ibActionID id = actions.GetID(i);
			if (id == ibNOT_FOUND)
				continue;
			ibCommandDescription desc;
			desc.AppendCommand(srcPath.front().m_id);   // hop 1: the table's source
			desc.AppendCommand(id);                     // leaf: the action on it (resolved at walk time)
			const ibString caption = actions.GetCaptionByID(id);   // tree label — the readable action text
			// cell PATH = "Form.<table>.<action NAME>" — the FULL route to the command by NAME (configurator uses
			// names, not synonyms), so you can see WHERE a table command lives, not just its bare caption.
			out.push_back({ sStd, caption, desc, actions.GetPictureByID(id), tableName,
			                ibT("Form.") + tableName + ibT(".") + actions.GetNameByID(id) });
		}
	}

	// 3 — GLOBAL COMMANDS: EVERY command metaobject in the application (recursive by classid). This is the flat "all
	// commands" source (form commands in section 1 are a different type, so they never appear here). Read from the application the form's
	// DATA belongs to (form->GetMetaObject()->GetMetaData()), NOT the form metaobject's own metaData nor the global
	// activeMetaData; fall back to the form's metaData only when the form has no data object. A PARAMETERIZED command
	// (reference Parameter type) shows ONLY where the form carries a matching data type; an untyped one always.
	const ibMetaData* globalMeta = form->GetMetaObject() != nullptr ? form->GetMetaObject()->GetMetaData() : metaData;
	const std::set<ibClassID> formTypes = CollectFormDataTypes(form);
	if (globalMeta != nullptr)
		for (ibValueMetaObjectCommand* cmd : globalMeta->GetAnyArrayObject<ibValueMetaObjectCommand>({ g_metaSharedCommandCLSID, g_metaCommandCLSID }, /*use_child_filter*/ true))
			if (cmd != nullptr && !cmd->IsDeleted() && !CommandExcludedByType(cmd, formTypes))
				out.push_back({ sGlobal, cmd->GetName(), ibCommandDescription(cmd->GetMetaID()), commandPicture(cmd), ibEmptyString, cmd->GetFullName() });

	// 4 — SECTION COMMANDS: the form sees sections EXACTLY as the runtime menu builder does (mainFrameWorkspaceInterface)
	// — each section's items laid out by AREA through the SAME method, GetInterfaceItemArrayObject (membership by
	// IsSetInterface, area by the item's GetCommandSection — property-driven for a command), subsections walked
	// recursively via GetInterfaceArrayObject. Only COMMAND items are pickable here; a command included in a section
	// shows under [section] > [area] and binds to a button the same way it runs in the menu. One traversal, no drift.
	if (globalMeta != nullptr) {
		// EVERY item the section holds (a command, but also a constant / catalog / … checked into it) — like the
		// runtime menu builder. Icon BY METATYPE (item->GetIcon(), exactly as the menu's df->SetBitmap), tree leaf =
		// the item's NAME, cell = its full PATH. An OBJECT item carries its group's command TYPE (Create -> create,
		// else open the list), so the SAME object in Normal vs Create is a DISTINCT binding that runs the right thing
		// (Execute by command type), exactly as clicking it in the menu does.
		auto addItems = [&out](const ibValueMetaObjectSection* section, const std::vector<ibValueMetaObject*>& items,
			ibInterfaceCommandType groupType, const ibString& groupLabel) {
			for (ibValueMetaObject* item : items) {
				if (item == nullptr || item->IsDeleted())
					continue;
				ibCommandDescription desc(item->GetMetaID());
				desc.SetCommandType(groupType);
				// NAME only (the group is already known from the tree — no redundant "Class.Name"), but a CREATE-area
				// item carries the ": Create" tag so the SAME object reads distinctly from its Normal-area "open list"
				// binding — on the navigator AND on a dropped button / bar item.
				ibString name = item->GetName();
				if (groupType == ibInterfaceCommandType_Create)
					name += ibT(": ") + ibString(_("Create"));
				out.push_back({ section->GetName(), name, desc, ibPictureDescription(item->GetClassType()), groupLabel, name });
			}
		};
		const std::vector<ibValueMetaObjectCommandGroup*> declaredGroups =
			globalMeta->GetAnyArrayObject<ibValueMetaObjectCommandGroup>(g_metaCommandGroupCLSID);
		std::function<void(ibValueMetaObjectSection*)> walkSection = [&](ibValueMetaObjectSection* section) {
			if (section == nullptr)
				return;
			// The platform's groups, then the declared ones — the order and the captions the section page uses.
			for (const ibInterfaceCommandSection area : g_platformCommandGroups) {
				std::vector<ibValueMetaObject*> items;
				section->GetInterfaceItemArrayObject(area, items);
				addItems(section, items, area == ibInterfaceCommandSection_Create
					? ibInterfaceCommandType_Create : ibInterfaceCommandType_Default, ibCommandGroupCaption(area));
			}
			for (const ibValueMetaObjectCommandGroup* group : declaredGroups) {
				// the section page's groups only — a form command bar group's commands are reached from the form
				if (group == nullptr || group->IsDeleted() || group->GetCategory() == ibCommandGroupCategory_FormCommandBar)
					continue;
				std::vector<ibValueMetaObject*> items;
				section->GetInterfaceItemArrayObject(group, items);
				addItems(section, items, ibInterfaceCommandType_Default, group->GetSynonym());
			}
			for (ibValueMetaObjectSection* sub : section->GetInterfaceArrayObject())
				walkSection(sub);
		};
		for (ibValueMetaObjectSection* section : globalMeta->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID))
			walkSection(section);
	}

	return out;
}

// ⚠ THE TOOL-ID RANGE FOR QUICK FILTERS — below the object-command range (31000+) and the manual
// synthetic ids (32000+), under the 32767 ceiling wxMenuItemBase asserts on. A range of its own so
// the click can tell what it is looking at without a lookup.
static const ibActionID g_quickFilterIdFirst = 30000;
static const ibActionID g_quickFilterIdLast  = 30499;
// …and the groups of standard actions (ibCommandItem::m_group), above them.
static const ibActionID g_actionGroupIdFirst = 30500;

// THE MODEL BEHIND THIS BAR — a tablebox has one; a form's own bar and a report's do not.
ibValueModel* ibValueCommandBar::QuickFilterModel() const
{
	const ibValueModelTableBox* table = dynamic_cast<const ibValueModelTableBox*>(m_owner);
	return table != nullptr ? table->GetTableModel() : nullptr;
}

namespace {
// HOW ONE LINE READS ON THE BAR — its own wording when a person gave it one, else «field: value».
// The same rule the settings window and the report heading print a condition by, so one condition
// is not worded three ways depending on where it is shown.
ibString ibQuickFilterCaption(const ibFilterNodeDescription& node, const ibMetaData* metaData)
{
	if (!node.m_presentation.IsEmpty())
		return node.m_presentation;
	const ibString field = node.m_left.m_presentation.IsEmpty()
		? node.m_left.m_path : node.m_left.m_presentation;
	const ibString value = node.m_right.IsField()
		? (node.m_right.m_presentation.IsEmpty() ? node.m_right.m_path : node.m_right.m_presentation)
		: ibStoredValue(node.m_right.m_value, metaData).GetString();
	// AN UNSET VALUE STILL READS AS THE FIELD IT NARROWS — a bare field name says "click me to
	// narrow by this", which is what an unfilled quick filter is for.
	return value.IsEmpty() ? field : field + ibT(": ") + value;
}
}   // namespace

void ibValueCommandBar::BuildQuickFilters()
{
	// These stand for VALUES a reader picks at runtime — and this library serves the runtime only, so they
	// always build. (In the studio they are not built: the author edits the LINE, in the settings window.)
	const ibValueModel* model = QuickFilterModel();
	if (model == nullptr)
		return;

	// THE SETTING IN FORCE — the reader's where they set one, the author's variant otherwise. Not
	// the user section alone: a line the AUTHOR marked quick-access must reach the bar before the
	// reader has set anything, which is the whole point of marking it.
	const ibFilterDescription& filter = model->GetModelComposer().GetCurrentFilterDesc();

	// ⭐ A LINE ON A FIELD THE OPTIONS OF THE BASE TAKE AWAY IS NOT PUT UP — it stays in the filter and
	// applies, as the settings window hides it. Whose field it is, is the composer's to say: the line is
	// its setting, over its source (ibDataComposer::IsAvailable).
	const ibDataComposer& composer = model->GetModelComposer();

	ibActionID id = g_quickFilterIdFirst;
	for (size_t i = 0; i < filter.m_nodes.size() && id <= g_quickFilterIdLast; ++i) {
		const ibFilterNodeDescription& node = filter.m_nodes[i];
		// A GROUP IS NOT A QUICK FILTER — it has no value to edit, and «(A or B)» on a toolbar is a
		// question with no answer. Only conditions, and only at the top level: a line inside a group
		// means something only together with its siblings.
		if (node.m_kind != ibFilterNodeKind_Condition
		 || node.m_display != ibFilterDisplayMode_QuickAccess
		 || !composer.IsAvailable(node.m_left.m_path) || !composer.IsAvailable(node.m_right.m_path))
			continue;
		ibCommandEntry entry(id++, ibQuickFilterCaption(node, m_owner->GetMetaData()), ibPictureDescription(),
			ibRepresentation_Text);
		entry.kind = ibCommandEntryKind_QuickFilter;
		entry.filterLine = i;
		m_commands.push_back(entry);
	}
	// A SEPARATOR ONLY WHEN SOMETHING WAS ADDED — otherwise every bar with no quick filter would
	// open with a divider standing in front of its first real command.
	if (!m_commands.empty())
		m_commands.emplace_back();
}

namespace {
// An entry of the author's its id from `id` on, a group before what it holds — the bar's ids, wherever it stands.
void NumberEntry(ibCommandEntry& entry, ibActionID& id)
{
	entry.id = id++;
	for (ibCommandEntry& member : entry.members)
		NumberEntry(member, id);
}
}   // namespace

const std::vector<ibCommandEntry>& ibValueCommandBar::BuildCommands()
{
	// (Re)build from scratch so toggling AutoFill stays in sync (a stale auto-filled set must not
	// linger). AutoFill contributes the owner's standard actions; the manual child items are ALWAYS
	// appended on top (else a just-added command is invisible while AutoFill is on — the default).
	// NB: ibStandardCommandSet is a PROTECTED nested type of ibStandardCommandSource — it can't be named,
	// so every use goes through `auto` (deduced, never spelled). That also blocks hoisting it into
	// a named helper/param, so the manual branch fetches it per action-bound item (few in practice).
	m_commands.clear();

	// ⭐⭐ THE QUICK FILTERS COME FIRST — the lines the author marked «quick access», one tool each.
	// «Also in the list header — the ones people change daily» was the whole of that setting's spec,
	// and it had no home: a tablebox's window IS the grid, with no strip to put editors in. It does
	// own THIS bar, which is already rendered, already serialised, already designable — so a quick
	// filter became a KIND OF ENTRY on it rather than a container the control does not have
	// (Max, 2026-08-24, choosing between that and wrapping the tablebox in a panel).
	//
	// ⚠ NOT DESIGNABLE, and not stored here: they are read from the SETTING in force every rebuild,
	// so marking a line quick-access is the only act, and unmarking it takes the tool away again.
	BuildQuickFilters();
	// A view-only form greys every DATA-MODIFYING command (Save / Post / Create / Mark-for-delete / …);
	// read-only commands (Refresh / Filter / Sort / open) stay live. The modify flag is ALWAYS the command's own:
	// AutoFill reads it off the action collection / the object command, a manual item off its bound command (door).
	const bool viewOnly = m_owner != nullptr && m_owner->GetOwnerForm() != nullptr
		&& m_owner->GetOwnerForm()->IsViewOnly();
	// Gathered below and PLACED among the standard actions: the groups at the seam between the data's verbs and the
	// window's, the object's plain commands after all of them — see there.
	std::vector<ibCommandEntry> groupEntries;
	std::vector<ibCommandEntry> ownCommands;
	// AutoFill ALSO surfaces the owner OBJECT's OWN commands — its structural command children (the
	// "Commands" node under a Catalog / Document / …). This is auto-generation honoring the object's own commands:
	// each object command becomes an entry carrying the command by metaId (one hop), so a click runs it through
	// the exact same path a manually-bound command uses (ExecuteCommand -> ExecuteValueByPath). The owner match
	// is by CONSTRUCTION — GetCommandArrayObject returns only THIS object's commands.
	// Gate on the FORM'S OWN bar (m_owner is the ibValueForm): a sub-control's bar (a tablebox / list) must
	// NOT auto-surface object commands — those land there only when one is deliberately dropped on it
	// (placement is the studio's choice, not forced).
	if (ibValueForm* ownerForm = (IsAutoFill() ? dynamic_cast<ibValueForm*>(m_owner) : nullptr)) {
		const ibValueMetaObjectGenericData* obj = ownerForm->GetMetaObject();
		if (obj != nullptr) {
			// object-command range: above any real action id, below the manual synthetic ids (32000+),
			// under the 32767 tool-id ceiling wxMenuItemBase asserts on.
			ibActionID cmdId = 31000;
			// Each command an entry of its own id, standing on the bar or filed under a group alike: its caption, its
			// picture (the command determines it), greyed where a view-only form forbids a command that changes data,
			// and the command it runs, by its metaId.
			auto commandEntry = [&cmdId, viewOnly](const ibValueMetaObjectCommand* cmd) {
				ibString caption = cmd->GetSynonym();
				if (caption.IsEmpty()) caption = cmd->GetName();
				ibCommandEntry entry(cmdId++, caption, ibPictureDescription(cmd->GetClassType()),
					ibRepresentation_PictureAndText, !(viewOnly && cmd->GetModifiesData()));
				entry.command = ibCommandDescription(cmd->GetMetaID());
				return entry;
			};

			// ⭐⭐ A GROUP OF THE FORM COMMAND BAR IS ONE BUTTON — whatever is filed under it goes into ITS
			// submenu instead of standing on the bar by itself. Gathered in the order the groups are first met,
			// each with the commands in the order they came.
			std::vector<std::pair<const ibValueMetaObjectCommandGroup*, std::vector<ibCommandEntry>>> groups;
			auto fileUnder = [&groups](const ibValueMetaObjectCommandGroup* group, ibCommandEntry&& entry) {
				for (auto& g : groups)
					if (g.first == group) { g.second.push_back(std::move(entry)); return; }
				groups.push_back({ group, {} });
				groups.back().second.push_back(std::move(entry));
			};
			auto formBarGroupOf = [](const ibValueMetaObjectCommand* cmd) -> const ibValueMetaObjectCommandGroup* {
				const ibValueMetaObjectCommandGroup* group = cmd->GetCommandGroup();
				return group != nullptr && group->GetCategory() == ibCommandGroupCategory_FormCommandBar ? group : nullptr;
			};

			for (ibValueMetaObjectCommand* cmd : obj->GetCommandArrayObject()) {
				// …nor a command of a part of the system this base does not use (featureOptionGate.h).
				if (cmd == nullptr || cmd->IsDeleted() || !ibFeatureOptionGate::IsAvailable(cmd))
					continue;
				if (const ibValueMetaObjectCommandGroup* group = formBarGroupOf(cmd))
					fileUnder(group, commandEntry(cmd));
				else
					ownCommands.push_back(commandEntry(cmd));
			}

			// …AND THE SHARED COMMANDS FILED UNDER SUCH A GROUP, when they are typed FOR this form's OBJECT: a form
			// bar is where a command runs on the object in front of you, so an untyped one has no business here
			// (it would stand on every form of the application). This is what puts a common «Print» of an
			// invoice beside the invoice's own commands.
			//
			// ⚠ THE OBJECT'S OWN TYPE, not every reference the form's data carries. The picker's wider set
			// (CollectFormDataTypes) holds each column's type too, and asked here it put a «Print price tag» typed
			// for the goods catalog into the Print group of every invoice with a goods column — to run on the
			// invoice with a parameter of the wrong kind (found by the audit, 2026-09-22).
			if (const ibMetaData* metaData = obj->GetMetaData()) {
				const std::set<ibClassID> formTypes = { reference_to_clsid(obj->GetMetaID(), clsid_metaclass(obj->GetClassType())) };
				for (ibValueMetaObjectCommand* cmd : metaData->GetAnyArrayObject<ibValueMetaObjectCommand>({ g_metaSharedCommandCLSID }, /*use_child_filter*/ true)) {
					if (cmd == nullptr || cmd->IsDeleted() || !CommandIsTypedFor(cmd, formTypes) || !ibFeatureOptionGate::IsAvailable(cmd))
						continue;
					if (const ibValueMetaObjectCommandGroup* group = formBarGroupOf(cmd))
						fileUnder(group, commandEntry(cmd));
				}
			}

			for (auto& g : groups) {
				const ibValueMetaObjectCommandGroup* group = g.first;
				ibCommandEntry& entry = groupEntries.emplace_back(cmdId++, group->GetSynonym(),
					group->IsEmptyPicture() ? ibPictureDescription() : group->GetPictureDesc(), ibRepresentation_PictureAndText);
				entry.kind = ibCommandEntryKind_Group;
				entry.members = std::move(g.second);
				entry.tooltip = group->GetToolTip();
			}
		}
	}
	if (IsAutoFill() && m_owner != nullptr) {
		auto actions = m_owner->GetStandardCommands(m_owner->GetTypeForm());

		// ⭐ THE GROUPS STAND AT THE SEAM between the DATA's verbs and the WINDOW's — after Post, Generate, Clone,
		// before Close, Update, Help — each side kept apart by a separator (Max, 2026-09-22: "before the form's
		// commands", "after Clone"). Print is what a person comes to a finished document for: it belongs with what
		// is done TO the document, not past the window's chrome at the far end.
		//
		// The seam is the FORM's to say, and it says it by construction: its set is its command provider's actions
		// followed by its own (ibValueForm::GetStandardCommands). So the provider's count IS the seam — asked, not
		// found by looking for a verb called Close.
		size_t dataVerbs = 0;
		if (!groupEntries.empty())
			if (ibValueForm* ownerForm = dynamic_cast<ibValueForm*>(m_owner))
				if (ibStandardCommandSource* provider = ownerForm->GetCommandProvider()) {
					auto dataActions = provider->GetStandardCommands(m_owner->GetTypeForm());
					dataVerbs = dataActions.GetCount();
				}
		auto placeGroups = [&]() {
			if (groupEntries.empty())
				return;
			if (dataVerbs > 0)
				m_commands.emplace_back();   // separator — off the data's verbs
			m_commands.insert(m_commands.end(), groupEntries.begin(), groupEntries.end());
			if (dataVerbs == 0)
				m_commands.emplace_back();   // separator — the form's own one follows the data's verbs only
			groupEntries.clear();
		};

		// ⭐ A GROUP OF STANDARD ACTIONS IS ONE BUTTON TOO (a table's Search: Find, Advanced search, Cancel search),
		// standing where the first of them would: each action an entry under it, its id the action's own — pressed, it
		// runs as it would standing by itself (CallAsAction).
		std::map<ibString, size_t> actionGroups;   // a group's caption → its entry
		ibActionID groupId = g_actionGroupIdFirst;

		for (unsigned int i = 0; i < actions.GetCount(); i++) {
			if (i == dataVerbs)
				placeGroups();
			const ibActionID id = actions.GetID(i);
			if (id == ibNOT_FOUND) {
				m_commands.emplace_back();   // separator
				continue;
			}
			const ibPictureDescription pic = actions.GetPictureByID(id);
			// Each command takes its default display mode from the action: no picture -> show
			// text; otherwise honour the action's pictureAndText flag.
			const ibRepresentation rep = pic.IsEmptyPicture()
				? ibRepresentation_PictureAndText
				: (actions.IsCreatePictureAndText(id) ? ibRepresentation_PictureAndText : ibRepresentation_Picture);
			const bool enabled = actions.IsEnabled(id) && !(viewOnly && actions.GetModifiesDataByID(id));   // greyed if it changes data
			const ibString group = actions.GetGroupByID(id);
			if (!group.IsEmpty()) {
				auto found = actionGroups.find(group);
				if (found == actionGroups.end()) {
					ibCommandEntry& entry = m_commands.emplace_back(groupId++, group, pic, rep);
					entry.kind = ibCommandEntryKind_Group;
					found = actionGroups.emplace(group, m_commands.size() - 1).first;
				}
				m_commands[found->second].members.emplace_back(id, actions.GetCaptionByID(id), pic, rep, enabled).isDefault = actions.IsDefault(id);
				continue;
			}
			m_commands.emplace_back(id, actions.GetCaptionByID(id), pic, rep, enabled).isDefault = actions.IsDefault(id);
		}
	}
	// A seam never reached (a set shorter than its own provider's — not a shape a form builds) still shows the groups.
	m_commands.insert(m_commands.end(), groupEntries.begin(), groupEntries.end());
	// …and the object's plain commands after them, where they always stood.
	m_commands.insert(m_commands.end(), ownCommands.begin(), ownCommands.end());
	// Manual commands and submenus, synthetic ids from here. Synthetic id stays < 32767 — the overflow dropdown builds
	// a wxMenuItem from it and wxMenuItemBase asserts outside [0, 32767); sit under the ceiling, above any real id.
	ibActionID synthId = 32000;
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibCommandEntry entry;
		if (GetChild(idx)->GetCommandEntry(entry))
			NumberEntry(m_commands.emplace_back(std::move(entry)), synthId);
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


// ⭐⭐ CLICKING A QUICK FILTER EDITS ITS LINE — through the SAME conversation every other value goes
// through (ibTypeControlFactory::ChooseValue): the quick choice for a type that has one, the
// metaobject's own selection form otherwise. A second way of picking a value is where the two start
// to disagree — a filter cell and a form control already learned that once.
//
// ⭐ AND THE ANSWER LANDS IN THE READER'S SECTION, never the author's: a quick filter is somebody
// narrowing their own list, and the line they clicked may well be one the author declared. Same act
// and same door as «filter by this column» on the table's own menu.
namespace {
// The adapter the shared chooser talks to — a filter LINE pretending to be a control for the length
// of one pick. It owns nothing and lives on the stack: get the value, put the chosen one back.
class ibQuickFilterValueFrame : public ibControlFrame {
public:
	ibQuickFilterValueFrame(ibFilterNodeDescription& node, ibValueForm* form, const ibMetaData* metaData)
		: m_node(node), m_form(form), m_metaData(metaData) {}

	virtual bool GetControlValue(ibValue& value) const override { value = ibStoredValue(m_node.m_right.m_value, m_metaData); return true; }
	virtual bool SetControlValue(const ibValue& value = ibValue()) override {
		ibStoreValue(m_node.m_right.m_value, value); m_node.m_right.m_path.Clear(); return true;
	}
	virtual ibValueForm* GetOwnerForm() const override { return m_form; }
	virtual ibClassID GetClassType() const override { return ibStoredValue(m_node.m_right.m_value, m_metaData).GetClassType(); }
	// ⚠ THE METADATA IS HANDED IN, AND THERE IS NO FALLBACK. Several applications are open at once
	// (the base, a compared one, one from a file), so reaching for the "active" one would quietly
	// answer out of somebody else's — the filter cell still does exactly that. No metadata means no
	// ctor to ask, which is a truthful "no quick choice" rather than a guess.
	virtual bool HasQuickChoice() const override {
		const ibCtorAbstractType* ctor = m_metaData != nullptr
			? m_metaData->GetAvailableCtor(GetClassType()) : nullptr;
		return ::HasQuickChoice(ctor);
	}
	virtual void ChoiceProcessing(ibValue& selected) override { SetControlValue(selected); }
	// ⚠ REF-COUNTING IS A NO-OP BY CONSTRUCTION. `ibBackendControlFrame` demands the pair because a
	// real control is a ref-counted runtime value; this one is a STACK object that lives for exactly
	// one modal pick and is stored by nobody, so there is no count to keep. Saying so here is what
	// makes it concrete — the alternative would be to make a filter LINE into a runtime value.
	virtual void ControlIncrRef() override {}
	virtual void ControlDecrRef() override {}

private:
	ibFilterNodeDescription& m_node;
	ibValueForm*             m_form;
	const ibMetaData*        m_metaData;
};
}   // namespace

bool ibValueCommandBar::ExecuteQuickFilter(const ibActionID& id, ibValueForm* form)
{
	if (id < g_quickFilterIdFirst || id > g_quickFilterIdLast)
		return false;
	ibValueModel* model = QuickFilterModel();
	if (model == nullptr)
		return true;   // the id was ours; there is simply nothing behind it any more

	// WHICH LINE — off the entry that was built, not off the id's arithmetic: the ids are dense and
	// the lines are not (groups and ordinary conditions are skipped).
	size_t line = 0;
	bool found = false;
	for (const ibCommandEntry& entry : m_commands)
		if (entry.kind == ibCommandEntryKind_QuickFilter && entry.id == id) {
			line = entry.filterLine; found = true; break;
		}
	if (!found)
		return true;

	// ⭐ A COPY, EDITED, THEN ASSIGNED — the pattern every settings edit here follows. Editing the
	// setting in place would be editing the AUTHOR's when the reader has set nothing of their own.
	ibSettingsDescription settings = model->GetModelComposer().GetCurrentSettingsDesc();
	if (line >= settings.m_filter.m_nodes.size())
		return true;

	const ibMetaData* metaData = form != nullptr ? form->GetMetaData() : nullptr;
	ibQuickFilterValueFrame holder(settings.m_filter.m_nodes[line], form, metaData);
	if (!ibTypeControlFactory::ChooseValue(&holder))
		return true;   // the person closed the picker — nothing was chosen, nothing changes

	// A LINE THAT NOW HAS A VALUE IS ON. Picking a value and having to tick the line as well is the
	// second act nobody expects; clearing it is what turns the line off again.
	settings.m_filter.m_nodes[line].m_use =
		!ibStoredValue(settings.m_filter.m_nodes[line].m_right.m_value, metaData).IsEmpty();
	model->GetModelComposer().SetUserSettingsDesc(settings);
	model->RefetchAll();
	// …AND THE BAR RE-READS ITSELF. The tool's caption IS the line's value, so a pick that refetched
	// the rows and left the button saying the old value would be the one place on screen still
	// claiming the previous filter. The form's own update verb, not a private one: the same pass
	// that redraws the grid redraws the strip above it.
	if (form != nullptr)
		form->UpdateForm();
	return true;
}

void ibValueCommandBar::ExecuteCommand(const ibActionID& id, ibBackendValueForm* form)
{
	if (m_owner == nullptr)
		return;
	// THE QUICK FILTERS FIRST — they are entries on this bar with no command behind them, so the
	// lookup below would find no item and hand the id to CallAsAction as if it were a system action.
	if (ExecuteQuickFilter(id, m_owner->GetOwnerForm()))
		return;
	// …and a GROUP runs nothing by itself: its submenu does, which the toolbar opens under it; nor does a command of the
	// author's with nothing to run. Asked here for the same reason — with no command behind it, the id would reach
	// CallAsAction.
	const ibCommandEntry* const entry = FindEntry(id);
	if (entry != nullptr && (entry->kind == ibCommandEntryKind_Group || entry->IsUnbound()))
		return;
	// A command of the author's or of an object carries what it runs -> walk its command-hop path through the command
	// OBJECT, which resolves each id and EXECUTES the leaf (a source object walks a path to FETCH a value; this walks to
	// RUN a command), wherever the entry stands. A standard action carries none -> the tool-id IS the system action.
	if (entry != nullptr && entry->command.IsOk())
		ExecuteValueByPath(entry->command);   // the bar IS-A command door; gate = its owner frame's form
	else
		m_owner->CallAsAction(id, form);   // AutoFill / standard command — the tool id IS the system action
}

namespace {
// The entry of this id among `entries` and under each group of them, to any depth.
const ibCommandEntry* FindIn(const std::vector<ibCommandEntry>& entries, const ibActionID& id)
{
	for (const ibCommandEntry& entry : entries) {
		if (entry.id == id)
			return &entry;
		if (const ibCommandEntry* const found = FindIn(entry.members, id))
			return found;
	}
	return nullptr;
}
}   // namespace

const ibCommandEntry* ibValueCommandBar::FindEntry(const ibActionID& id) const
{
	return FindIn(m_commands, id);
}

void ibValueCommandBar::Search(const ibString& text)
{
	if (!IsShowSearch() || m_owner == nullptr || !m_owner->HasSearch())
		return;
	// The same text again is nothing new — a client that sent it twice (the × on a field already empty) reads nothing
	// anew.
	if (m_owner->GetSearchText() != text)
		m_owner->SetSearchText(text);
}

namespace {
// Whether `host` draws the entry: every one, but a command of the author's with nothing to run — the studio's alone.
bool IsDrawnBy(const ibCommandEntry& c, const ibVisualHost* host)
{
	return !c.IsUnbound() || host->IsStudioHost();
}

// ONE ENTRY as a client draws it — and a group's entries under it the same way, to any depth, those `host` draws. On the
// bar the representation decides which of the two a client shows — said here once, not left to each client; a line of a
// menu always says its caption.
void WriteEntry(ibDataNode& node, const ibCommandEntry& c, const ibMetaData* metaData, bool inMenu, const ibVisualHost* host)
{
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Id, c.id);
	if (c.id == ibNOT_FOUND) {
		node.SetValue(ibProtocol::ibForm::ibCommandBar::Separator, true);
		return;
	}
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Caption, !inMenu && c.representation == ibRepresentation_Picture ? ibString() : c.caption);
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Picture, c.representation == ibRepresentation_Text ? ibDataValue() : ibBackendPicture::SendPicture(c.picture, metaData));
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Tooltip, c.tooltip.IsEmpty() ? c.caption : c.tooltip);
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Enabled, c.enabled);
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Default, c.isDefault);
	node.SetValue(ibProtocol::ibForm::ibCommandBar::Kind, static_cast<std::int32_t>(c.kind));
	for (const ibCommandEntry& member : c.members)
		if (IsDrawnBy(member, host))
			WriteEntry(node.AddChild(0, 0), member, metaData, true, host);
}
}   // namespace

void ibValueCommandBar::Update(ibDataNode& state, ibVisualHost* host)
{
	for (const ibCommandEntry& c : BuildCommands()) {
		if (!IsDrawnBy(c, host))
			continue;
		ibDataNode& entry = state.AddChild(0, 0);
		WriteEntry(entry, c, GetMetaData(), false, host);

		// A command or a submenu the author put on the bar itself — its place among the bar's children, as its node is
		// written (WriteData): what the studio selects it by when its tool is pressed.
		if (c.item != nullptr)
			entry.SetValue(ibProtocol::ibForm::ibCommandBar::Position, static_cast<std::int32_t>(c.item->GetParentPosition()));
	}

	// …and the search field at its end, holding what is looked for.
	if (IsShowSearch() && m_owner != nullptr && m_owner->HasSearch())
		state.SetValue(ibProtocol::ibForm::ibCommandBar::Search, m_owner->GetSearchText());
}

//***********************************************************************
//*                   Command bar CHILDREN (shared)                     *
//***********************************************************************

namespace {
// Whether a child of `holder`, or of a submenu under it at any depth, other than `except`, is called `name`.
bool HasName(const ibValueLayerObject* holder, const ibString& name, const ibValueLayerObject* except)
{
	for (unsigned int idx = 0; idx < holder->GetChildCount(); idx++) {
		const ibValueLayerObject* const child = holder->GetChild(idx);
		if (child != except && child->GetName() == name)
			return true;
		if (child->IsLayerContainer() && HasName(child, name, except))
			return true;
	}
	return false;
}

// A new child of `holder` — a command or a submenu — last, under the first name of its kind free on the whole bar.
template <class T>
T* AddLayerChild(ibValueLayerObject* holder, const ibString& prefix)
{
	T* const child = new T();
	child->SetParent(holder);   // first: the free name is asked of the bar it now hangs in
	if (const ibValueCommandBar* const bar = child->GetBar())
		child->SetName(bar->GenerateItemName(prefix));
	holder->AddChild(child);
	return child;
}

// The children of `holder` (the bar, a submenu) as sub-nodes of `node`, in order — each of its type, so a submenu is
// told from a command when read back (here, and in the studio by its type's name).
void WriteChildren(const ibValueLayerObject* holder, ibDataNode& node)
{
	for (unsigned int idx = 0; idx < holder->GetChildCount(); idx++) {
		const ibValueLayerObject* const child = holder->GetChild(idx);
		child->WriteData(node.AddChild(child->GetClassType(), idx));
	}
}

// …and read back: a sub-node of a submenu's type a submenu, any other a command.
template <class THolder>
void ReadChildren(THolder* holder, const ibDataNode& node)
{
	holder->RemoveAllChildren();
	for (const ibDataNode& child : node.Children()) {
		if (child.GetClsid() == g_commandBarSubmenuCLSID)
			holder->AddSubmenu()->ReadData(child);   // ReadData overwrites the name
		else
			holder->AddCommandItem()->ReadData(child);
	}
}
}   // namespace

//***********************************************************************
//*                       Command bar ITEM (child)                      *
//***********************************************************************

ibValueFrame* ibValueCommandBarItem::GetOwnerFrame() const
{
	// Reach the owner frame through the bar — feeds metadata (base uses this).
	const ibValueCommandBar* const bar = GetBar();
	return bar != nullptr ? bar->GetOwnerFrame() : nullptr;
}

bool ibValueCommandBarItem::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	// Name must stay unique among the bar's commands and submenus.
	const ibValueCommandBar* const bar = GetBar();
	if (property == m_propertyName && bar != nullptr && bar->HasItemName(newValue.GetString(), this))
		return false;
	return true;
}

bool ibValueCommandBarItem::GetCommandEntry(ibCommandEntry& entry) const
{
	// Hidden (Visible off, or by the feature options it names) — none; disabled (Enabled off) — kept, greyed.
	if (!IsVisible() || !IsAvailable())
		return false;
	entry.item = this;
	// The COMMAND owns the item's look AND its "modifies data" flag; the item merely obeys. THE one resolve
	// (ResolveCommand on the door): walk + reliable gather fallback -> existence + caption + picture + modifies, the
	// SAME decision the button and the inspector cell use. The item's own caption / picture only OVERRIDE the look where
	// set.
	const ibCommandDescription bindDesc = GetBindingDesc();
	ibString cmdCaption; ibPictureDescription cmdPicture; bool cmdModifies = true; bool cmdPicAndText = true;
	if (!bindDesc.IsOk() || !ResolveCommand(bindDesc, cmdCaption, cmdPicture, &cmdModifies, nullptr, &cmdPicAndText)) {
		// Bound to nothing (actionEvent is retired — the command is the sole binding), or to a command since gone: nothing
		// to run (IsUnbound). Greyed under its own caption or else its name, with its own picture if it has one — the
		// studio draws it so, to be bound again; where the form runs it is not drawn (ibValueCommandBar::Update).
		entry.caption = GetCaption().IsEmpty() ? GetName() : GetCaption();
		if (!IsEmptyPicture())
			entry.picture = GetPictureDesc();
		entry.representation = ibRepresentation::ibRepresentation_PictureAndText;
		entry.enabled = false;
		return true;
	}
	entry.caption = GetCaption().IsEmpty() ? cmdCaption : GetCaption();
	entry.picture = IsEmptyPicture() ? cmdPicture : GetPictureDesc();
	// The item's OWN Representation wins; left on Auto it takes the COMMAND's default (a standard action like Close /
	// Update is picture-only, Add / Post is picture+text).
	entry.representation = GetRepresentation();
	if (entry.representation == ibRepresentation::ibRepresentation_Auto)
		entry.representation = cmdPicAndText ? ibRepresentation::ibRepresentation_PictureAndText : ibRepresentation::ibRepresentation_Picture;
	// A view-only form greys a command that changes data — its own form asked, the door's gate.
	const ibValueForm* const form = GetCommandGateForm();
	entry.enabled = IsEnabled() && !(cmdModifies && form != nullptr && form->IsViewOnly());
	entry.isDefault = IsDefault();
	entry.command = bindDesc;
	return true;
}

//***********************************************************************
//*                    Command bar SUBMENU (child)                      *
//***********************************************************************

ibValueFrame* ibValueCommandBarSubmenu::GetOwnerFrame() const
{
	const ibValueCommandBar* const bar = GetBar();
	return bar != nullptr ? bar->GetOwnerFrame() : nullptr;
}

bool ibValueCommandBarSubmenu::OnPropertyChanging(ibProperty* property, const ibVariant& newValue)
{
	// Name must stay unique among the bar's commands and submenus.
	const ibValueCommandBar* const bar = GetBar();
	if (property == m_propertyName && bar != nullptr && bar->HasItemName(newValue.GetString(), this))
		return false;
	return true;
}

bool ibValueCommandBarSubmenu::GetCommandEntry(ibCommandEntry& entry) const
{
	// Hidden, or hidden by the feature options it names — none, with everything it holds.
	if (!IsVisible() || !IsAvailable())
		return false;
	// Its caption empty: its name, so a new one is not a blank tool.
	entry.caption = GetCaption().IsEmpty() ? GetName() : GetCaption();
	if (!IsEmptyPicture())
		entry.picture = GetPictureDesc();
	entry.representation = GetRepresentation();
	if (entry.representation == ibRepresentation::ibRepresentation_Auto)
		entry.representation = ibRepresentation::ibRepresentation_PictureAndText;
	entry.enabled = IsEnabled();
	entry.tooltip = GetToolTip();
	entry.kind = ibCommandEntryKind_Group;
	entry.item = this;
	// …and what it holds, each child's entry the same way, to any depth.
	for (unsigned int idx = 0; idx < GetChildCount(); idx++) {
		ibCommandEntry member;
		if (GetChild(idx)->GetCommandEntry(member))
			entry.members.push_back(std::move(member));
	}
	return true;
}

ibValueCommandBarItem* ibValueCommandBarSubmenu::AddCommandItem()
{
	return AddLayerChild<ibValueCommandBarItem>(this, ibT("Command"));
}

ibValueCommandBarSubmenu* ibValueCommandBarSubmenu::AddSubmenu()
{
	return AddLayerChild<ibValueCommandBarSubmenu>(this, ibT("Submenu"));
}

//***********************************************************************
//*                    Command bar (its children)                       *
//***********************************************************************

ibValueCommandBarItem* ibValueCommandBar::AddCommandItem()
{
	return AddLayerChild<ibValueCommandBarItem>(this, ibT("Command"));
}

ibValueCommandBarSubmenu* ibValueCommandBar::AddSubmenu()
{
	return AddLayerChild<ibValueCommandBarSubmenu>(this, ibT("Submenu"));
}

bool ibValueCommandBar::HasItemName(const ibString& name, const ibValueLayerObject* except) const
{
	return HasName(this, name, except);
}

ibString ibValueCommandBar::GenerateItemName(const ibString& prefix) const
{
	for (int n = 1; ; ++n) {
		const ibString name = ibString::Format(ibT("%s%d"), prefix, n);
		if (!HasItemName(name))
			return name;
	}
}

//***********************************************************************
//*                          Serialization                              *
//***********************************************************************
// The composite window writes a "Layers" block that holds the command bar (see window.cpp).
// The bar writes its AutoFill flag + one sub-node per child command or submenu (WriteChildren); each writes its
// own fields — same pattern a toolbar item uses — and a submenu its children after them, the same way.

bool ibValueCommandBarItem::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_propertyDefault->GetName(), m_propertyDefault->GetNodeValue());
	node.SetProperty(m_propertyFeatureOptions->GetName(), m_propertyFeatureOptions->GetNodeValue());
	node.SetProperty(m_propertyCommand->GetName(), m_propertyCommand->GetNodeValue());   // command SOURCE (the hop path)
	return true;
}

bool ibValueCommandBarItem::ReadData(const ibDataNode& node)
{
	m_propertyName->SetNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->SetNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyDefault->SetNodeValue(node.GetProperty(m_propertyDefault->GetName()));
	m_propertyFeatureOptions->SetNodeValue(node.GetProperty(m_propertyFeatureOptions->GetName()));
	m_propertyCommand->SetNodeValue(node.GetProperty(m_propertyCommand->GetName()));   // command SOURCE (the hop path)
	return true;
}

bool ibValueCommandBarSubmenu::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyName->GetName(), m_propertyName->GetNodeValue());
	node.SetProperty(m_propertyCaption->GetName(), m_propertyCaption->GetNodeValue());
	node.SetProperty(m_propertyRepresentation->GetName(), m_propertyRepresentation->GetNodeValue());
	node.SetProperty(m_propertyPicture->GetName(), m_propertyPicture->GetNodeValue());
	node.SetProperty(m_propertyTooltip->GetName(), m_propertyTooltip->GetNodeValue());
	node.SetProperty(m_propertyEnabled->GetName(), m_propertyEnabled->GetNodeValue());
	node.SetProperty(m_propertyVisible->GetName(), m_propertyVisible->GetNodeValue());
	node.SetProperty(m_propertyFeatureOptions->GetName(), m_propertyFeatureOptions->GetNodeValue());
	WriteChildren(this, node);
	return true;
}

bool ibValueCommandBarSubmenu::ReadData(const ibDataNode& node)
{
	m_propertyName->SetNodeValue(node.GetProperty(m_propertyName->GetName()));
	m_propertyCaption->SetNodeValue(node.GetProperty(m_propertyCaption->GetName()));
	m_propertyRepresentation->SetNodeValue(node.GetProperty(m_propertyRepresentation->GetName()));
	m_propertyPicture->SetNodeValue(node.GetProperty(m_propertyPicture->GetName()));
	m_propertyTooltip->SetNodeValue(node.GetProperty(m_propertyTooltip->GetName()));
	m_propertyEnabled->SetNodeValue(node.GetProperty(m_propertyEnabled->GetName()));
	m_propertyVisible->SetNodeValue(node.GetProperty(m_propertyVisible->GetName()));
	m_propertyFeatureOptions->SetNodeValue(node.GetProperty(m_propertyFeatureOptions->GetName()));
	ReadChildren(this, node);
	return true;
}

bool ibValueCommandBar::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyAutoFill->GetName(), m_propertyAutoFill->GetNodeValue());
	node.SetProperty(m_propertyShowSearch->GetName(), m_propertyShowSearch->GetNodeValue());
	// One child sub-node per command or submenu, in order, each of its type — a submenu holding its own.
	WriteChildren(this, node);
	return true;
}

bool ibValueCommandBar::ReadData(const ibDataNode& node)
{
	m_propertyAutoFill->SetNodeValue(node.GetProperty(m_propertyAutoFill->GetName()));
	// A bar written before the flag was has none, and keeps the default — on.
	m_propertyShowSearch->SetNodeValue(node.GetProperty(m_propertyShowSearch->GetName()));
	ReadChildren(this, node);
	return true;
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

// A runtime value must be registered. It is a SYSTEM type (clsid = system_to_clsid("CommandBar")).
SYSTEM_TYPE_REGISTER(ibValueCommandBar, "CommandBar");
SYSTEM_TYPE_REGISTER(ibValueCommandBarItem, "CommandBarItem");
SYSTEM_TYPE_REGISTER(ibValueCommandBarSubmenu, "CommandBarSubmenu", g_commandBarSubmenuCLSID);

void WriteCommandTree(ibDataNode& node, ibValueForm* form)
{
	if (form == nullptr)
		return;

	// ALL three sections first (form / standard / global), in order, so they show even when EMPTY — GatherFormCommands
	// labels its entries with exactly these strings. Rows are kept as AddChild gives them: a handle stays on its row as
	// the tree grows around it (ibDataNode::Handle).
	const auto addGroup = [](ibDataNode& parent, const ibString& name) {
		ibDataNode& group = parent.AddChild(0, 0);
		group.SetValue(ibProtocol::ibSchema::ibProperties::Name, name);
		return &group;
	};
	std::map<ibString, ibDataNode*> sections;
	for (const ibString& section : GetCommandSections())
		sections[section] = addGroup(node, section);
	std::map<ibString, ibDataNode*> subgroups;   // "section\x1fsubgroup" -> its row in the section (Standard nests per source)

	for (const ibCommandSourceEntry& entry : GatherFormCommands(form)) {
		auto section = sections.find(entry.group);
		if (section == sections.end())   // defensive: an unexpected section -> appended at the end
			section = sections.emplace(entry.group, addGroup(node, entry.group)).first;
		ibDataNode* parent = section->second;
		if (!entry.subgroup.IsEmpty()) {   // nested under a per-source SUB-group (the form / a table)
			const ibString key = entry.group + ibT("\x1f") + entry.subgroup;
			auto sub = subgroups.find(key);
			if (sub == subgroups.end())
				sub = subgroups.emplace(key, addGroup(*parent, entry.subgroup)).first;
			parent = sub->second;
		}

		// A form command's row is known by its id — the inspector's rows of it are; any other's, by its path only.
		const ibFormCommandValue* const formCommand = entry.desc.IsOk() && !entry.desc.IsHop()
			? form->FindFormCommandById(entry.desc.GetLeaf()) : nullptr;
		ibDataNode& row = parent->AddChild(0, formCommand != nullptr ? formCommand->GetId() : 0);
		row.SetValue(ibProtocol::ibSchema::ibProperties::Name, entry.label);
		const ibDataValue picture = ibBackendPicture::SendPicture(entry.picture, form->GetMetaData());   // the command's own picture, else the tree's default
		if (!picture.IsEmpty())
			row.SetValue(ibProtocol::ibSchema::ibProperties::Picture, picture);

		std::vector<ibDataValue> hops;
		for (const ibCommandHop& hop : entry.desc.GetPath()) {
			std::shared_ptr<ibDataNode> hopNode = std::make_shared<ibDataNode>();
			hopNode->SetValue(ibProtocol::ibSchema::ibProperties::Id, static_cast<std::int32_t>(hop.m_id));
			hops.push_back(ibDataValue::Child(hopNode));
		}
		row.AddField(ibProtocol::ibSchema::ibProperties::CommandPath, ibDataValue::Array(std::move(hops)));
		row.SetValue(ibProtocol::ibSchema::ibProperties::Kind, static_cast<std::int32_t>(entry.desc.GetCommandType()));
	}
}
