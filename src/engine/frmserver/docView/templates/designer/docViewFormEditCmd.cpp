////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : the form editor's create on a drop — the desktop editor's (designer/win/editor/visualEditor/
//	              visualEditorCmdProc.cpp), on the form the client holds: a control bound to a dropped attribute row,
//	              one running a dropped command (BoundControl, CommandControl)
////////////////////////////////////////////////////////////////////////////

#include "docViewFormEdit.h"

#include "backend/query/queryColumn.h"                   // ibBackendSourceColumn::GetName — a dropped control named after its field
#include "backend/srcDataObject.h"                       // ibSourceExplorer — a dropped path's leaf type → the control's class
#include "backend/system/value/valueDataComposition.h"   // g_valueDataCompositionCLSID — drops as a gridbox
#include "backend/system/value/valueJob.h"               // g_valueScheduleCLSID — a dropped schedule builds as static text
#include "backend/system/value/valueSpreadsheet.h"       // g_valueSpreadsheetCLSID — so does a document
#include "core/serialize/dataBuilder.h"
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"   // ibFormAttributeValue — a dropped path's head
#include "frmserver/visualView/ctrl/tableBox.h"        // g_controlTableBoxCLSID — a drop onto a tablebox appends a column
#include "frmserver/visualView/ctrl/typeControl.h"     // ibTypeControlFactory — a dropped control's binding
#include "frmserver/visualView/ctrl/widgets.h"         // ibValueButton / g_controlButtonCLSID — a dropped command's button
#include "frmserver/visualView/layers/commandBar.h"    // GatherFormCommands / ibValueCommandBarItem — a dropped command
#include "protocol/protocol.h"

namespace {

// The control a sizer item holds — the item itself is never what is selected.
ibValueFrame* SkipSizerItems(ibValueFrame* obj)
{
	while (obj != nullptr && obj->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
		obj = obj->GetChildCount() > 0 ? obj->GetChild(0) : nullptr;
	return obj;
}

// The ids of a control and of what is in it — made for what has none (put on the form).
void GenerateId(ibValueFrame* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		GenerateId(object->GetChild(idx));
	if (object->GetControlID() == 0)
		object->GenerateNewID();
}

// …and taken off — a pasted copy's were those of the form it was copied from (the desktop's ResetId).
void ResetId(ibValueFrame* object)
{
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		ResetId(object->GetChild(idx));
	object->SetControlID(0);
}

// The names of a control and of what is in it made unique on its form — a pasted copy's (the desktop's PasteObject).
void ResolveNameConflict(ibValueForm* form, ibValueFrame* object)
{
	form->ResolveNameConflict(object);
	for (unsigned int idx = 0; idx < object->GetChildCount(); idx++)
		ResolveNameConflict(form, object->GetChild(idx));
}

// Bind a just-created control to a source path — the drop counterpart of BuildForm's per-control
// SetSource. Goes through the COMMON source factory: SetDefaultSourceType is a non-virtual helper over the
// mutable GetSourceDesc (twin of SetDefaultMetaType over GetTypeDesc), so the editor needs no concrete-
// control header.
void SetBoundSource(ibValueFrame* control, const ibSourceDescription& desc)
{
	ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(control);
	if (factory == nullptr)
		return;
	factory->SetDefaultSourceType(desc);

	// Give the dropped control a NAME (identifier), like BuildForm, so it isn't a bare "Textctrl1".
	// A NESTED binding (a NON-main head attribute + a deeper leaf, e.g. Catalog1.Section) CONCATENATES
	// its segments — the dotted path becomes one joined identifier ("Catalog1Section"), never a literal
	// dot. A direct field on the (implicit) MAIN attribute keeps just the field name; a BARE attribute
	// (no leaf column) falls back to the attribute name. The TITLE (form label) is NOT assigned here —
	// the control DERIVES it on render from the binding (GetControlTitle: Title -> field synonym ->
	// attribute Synonym/Name).
	wxString name;
	ibBackendFormAttributeValue* head = !desc.IsOk() ? nullptr : factory->FindSourceHolder(desc.GetFirst());
	if (head != nullptr && !head->IsMain())
		name = head->GetName();
	if (const ibBackendSourceColumn* leaf = factory->GetSourceAttributeObject())
		name += leaf->GetName();
	if (name.IsEmpty() && head != nullptr)
		name = head->GetName();

	if (!name.IsEmpty()) {
		control->SetControlName(name);
		// Make it UNIQUE on the form through the SAME conflict-resolver a normal create / paste runs
		// (frame.cpp Init / PasteObject) — otherwise a second drop of the same field yields two
		// identically-named controls. The control is already attached (CreateObject added it), so the
		// resolver sees every sibling name and bumps the numeric suffix if the base is taken.
		if (ibValueForm* form = control->GetOwnerForm())
			form->ResolveNameConflict(control);
	}
}

// Resolve the control class for a dropped source path — the DROP side's type->control choice (the canon:
// the drag carries the path, the drop picks the class from the type AT the leaf). Mirrors BuildForm's
// per-field choice: a list / table-section leaf -> Tablebox, a single-clsid boolean -> Checkbox, everything
// else (scalar / reference / multi-type) -> Textctrl. Empty = nothing to create (a bad path).
wxString ResolveDropControlClass(ibValueForm* form, const ibSourceDescription& desc)
{
	const std::vector<ibSourceHop>& path = desc.GetPath();
	if (form == nullptr || path.empty())
		return wxEmptyString;
	ibFormAttributeValue* head = form->FindAttributeById(desc.GetFirst());
	if (head == nullptr)
		return wxEmptyString;
	ibSourceDataObject* source = head->GetSourceValue();

	// The leaf is either the whole attribute (a 1-hop path) or a node reached by walking the head's source
	// explorer down the remaining hops. Deeper hops go through THE shared structure walk (WalkColumns — the
	// same door the caption / type / gate resolvers use), so a dotted path THROUGH A REFERENCE resolves: the
	// reference vends its target's columns and the walk descends into them.
	bool isTable = false, containerIsTable = false;
	ibTypeDescription typeDesc;
	if (path.size() == 1) {
		isTable = source != nullptr && source->IsTableSource();
		typeDesc = head->GetTypeDesc();
	}
	else {
		const ibBackendSourceColumn* leaf = nullptr;
		if (source == nullptr || !source->WalkColumns(path, 1, leaf, nullptr, &isTable, &containerIsTable))
			return wxEmptyString;   // broken binding (a hop missed) -> nothing to create
		// A field inside a list / table section is a COLUMN, bindable only INTO that table; the drop's tablebox
		// branch serves the valid drop, so a canvas / non-table drop here declines instead of a broken control.
		if (containerIsTable)
			return wxEmptyString;
		if (!isTable) {
			if (leaf == nullptr)
				return wxEmptyString;   // no descriptor and not a table -> nothing bindable
			typeDesc = leaf->GetTypeDesc();
		}
	}

	if (isTable)
		return ibProtocol::ibForm::ibType::Tablebox;
	// ⭐ A COMPOSITION OR A SPREADSHEET DOCUMENT DROPS AS A GRIDBOX (Max, 2026-08-19). Both are shown
	// by the same control and by no other: the document IS what a gridbox displays, and a composition
	// composes one. Dropping either therefore builds the box that can show it, bound to the attribute
	// that was dragged.
	if (typeDesc.GetClsidCount() == 1 &&
		(typeDesc.ContainType(g_valueDataCompositionCLSID) || typeDesc.ContainType(g_valueSpreadsheetCLSID)))
		return ibProtocol::ibForm::ibType::Gridbox;
	if (typeDesc.GetClsidCount() == 1 && typeDesc.ContainType(ibValueTypes::TYPE_BOOLEAN))
		return ibProtocol::ibForm::ibType::Checkbox;
	// A SCHEDULE is shown, not typed — the static text renders it as its own sentence and opens
	// the four-tab editor on a click. Same choice the runtime auto-build makes (formObject.cpp),
	// so dropping one and generating one give the same form.
	if (typeDesc.GetClsidCount() == 1 && typeDesc.ContainType(g_valueScheduleCLSID))
		return ibProtocol::ibForm::ibType::Statictext;
	return ibProtocol::ibForm::ibType::Textctrl;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// The tab's requests
///////////////////////////////////////////////////////////////////////////////

bool ibFormDesignView::FetchControl(const ibDataNode& request, ibDataNode& response)
{
	// The desktop's palette (ibVisualEditor::CreateObject): made in the selected control — none selected: on the form —
	// or, where that cannot hold it, in the first of its parents that can; its paste (Paste — the copy, Value) put so
	// too. No control's rows: the client asks them of the one it shows (Rows).
	ibValueFrame* const target = m_valueForm->FindControlByID(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Control));
	ibValueFrame* made = nullptr;
	if (static_cast<ibProtocol::ibView::ibFormDesigner::ibCommand>(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Command)) ==
		ibProtocol::ibView::ibFormDesigner::ibCommand::Paste) {
		const ibDataNode* const copied = request.FindChild(ibProtocol::ibView::ibFormDesigner::Value);
		if (copied == nullptr)
			return false;
		made = PasteObject(target != nullptr ? target : GetValueForm(), *copied);
	}
	else {
		made = CreateControlOfClass(target != nullptr ? target : GetValueForm(),
			request.GetValue<wxString>(ibProtocol::ibView::ibFormDesigner::Class), nullptr);
	}
	if (made == nullptr)
		return false;
	WriteForm(response);
	response.SetValue(ibProtocol::ibView::ibFormDesigner::Selected, static_cast<std::int32_t>(made->GetControlID()));
	return true;
}

bool ibFormDesignView::FetchBoundControl(const ibDataNode& request, ibDataNode& response)
{
	// Dropped on a control — none named: on the form.
	ibValueFrame* const target = m_valueForm->FindControlByID(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Control));
	ibSourceDescription desc;
	ReadPath(request, desc);
	ibValueFrame* const made = CreateBoundControl(target != nullptr ? target : GetValueForm(), desc);
	if (made == nullptr)
		return false;
	WriteForm(response);
	response.SetValue(ibProtocol::ibView::ibFormDesigner::Selected, static_cast<std::int32_t>(made->GetControlID()));
	return true;
}

bool ibFormDesignView::FetchCommandControl(const ibDataNode& request, ibDataNode& response)
{
	// The command as the command tree's row has it (CommandPath, Kind) — under Value: the request's own Kind is the
	// request's.
	ibValueFrame* const target = m_valueForm->FindControlByID(request.GetValue<std::int32_t>(ibProtocol::ibView::ibFormDesigner::Control));
	const ibDataNode* const command = request.FindChild(ibProtocol::ibView::ibFormDesigner::Value);
	if (command == nullptr)
		return false;
	ibCommandDescription desc;
	ReadPath(*command, desc);
	// A drop that landed ON a command-bar TOOLBAR (CommandBar) joins THAT bar — the one of the control it is of.
	ibValueFrame* const owner = target != nullptr ? target : GetValueForm();
	ibValueCommandBar* const dropBar = request.GetValue<bool>(ibProtocol::ibView::ibFormDesigner::CommandBar) && owner->HasCommandBar() ?
		owner->GetCommandBar() : nullptr;
	ibValueFrame* const made = CreateCommandButton(owner, desc, dropBar);
	if (made == nullptr)
		return false;
	WriteForm(response);
	response.SetValue(ibProtocol::ibView::ibFormDesigner::Selected, static_cast<std::int32_t>(made->GetControlID()));
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// The editor's doors
///////////////////////////////////////////////////////////////////////////////

ibValueFrame* ibFormDesignView::CreateBoundControl(ibValueFrame* parentHint, const ibSourceDescription& desc)
{
	wxASSERT(m_valueForm);

	// A drop onto a TABLEBOX means "add a column", not "create a control". Climb from the hit-tested target
	// to the nearest tablebox (type by clsid). A field UNDER the table's own bound source (its path a strict
	// prefix of the field's) becomes a ROW column; a single-value source from elsewhere (an attribute /
	// object field) becomes a FOREIGN / object column (Mode 2). Either way append a view column bound to the
	// field — added at the END and bound in ONE step. Bind BEFORE the widget builds so it renders visible.
	for (ibValueFrame* box = parentHint; box != nullptr; box = box->GetParent()) {
		if (box->GetClassType() != g_controlTableBoxCLSID)
			continue;
		const ibTypeControlFactory* boxFactory = dynamic_cast<const ibTypeControlFactory*>(box);
		if (boxFactory == nullptr)
			break;
		const std::vector<ibSourceHop>& boxPath = boxFactory->GetSourceDesc().GetPath();
		const std::vector<ibSourceHop>& fieldPath = desc.GetPath();
		if (fieldPath.empty() || fieldPath == boxPath)
			break;                                             // the table's own bare source -> nothing to add
		bool underTable = fieldPath.size() > boxPath.size();   // a field PAST the table's own source
		for (size_t i = 0; underTable && i < boxPath.size(); i++)
			if (fieldPath[i].m_id != boxPath[i].m_id)
				underTable = false;                            // diverges from the table's source (structural, by id)
		if (!underTable) {
			// Not a column of THIS table's source. Allow a SINGLE-value source (a scalar / reference
			// attribute or object field) as a FOREIGN / object column (Mode 2 — one value per row); a column
			// of a DIFFERENT table (ResolveDropControlClass declines it) has no per-row value here.
			const wxString foreignClass = ResolveDropControlClass(m_valueForm, desc);
			if (foreignClass != ibProtocol::ibForm::ibType::Textctrl && foreignClass != ibProtocol::ibForm::ibType::Checkbox)
				break;
		}

		ibValueFrame* column = m_valueForm->CreateObject(ibProtocol::ibForm::ibType::TableboxColumn, box);
		if (column == nullptr)
			break;
		SetBoundSource(column, desc);   // bind BEFORE build -> renders visible
		InsertObject(column, box);      // append at the end
		return column;
	}

	const wxString className = ResolveDropControlClass(m_valueForm, desc);
	if (className.IsEmpty())
		return nullptr;
	// A scalar control shows its value / a tablebox refills its columns on first build because the source is
	// bound BEFORE the widget is created — CreateControlOfClass runs the bind step ahead of the insert.
	return CreateControlOfClass(parentHint, className, [&desc](ibValueFrame* control) {
		SetBoundSource(control, desc);
	});
}

ibValueFrame* ibFormDesignView::CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc,
	ibValueCommandBar* dropBar)
{
	if (m_valueForm == nullptr || !desc.IsOk())
		return nullptr;

	// Resolve the display name by matching the dropped id path against the form's gathered command sources (the
	// ONE gather that also feeds the navigator + picker) — no per-hop kind needed. Only the caption is seeded;
	// the icon stays LIVE (BuildCommands / the button's Update re-resolve it from the command).
	wxString caption;   // the command-source CELL text = the full PATH (fullName), so the binding reads as its path
	for (const ibCommandSourceEntry& entry : GatherFormCommands(m_valueForm))
		if (entry.desc == desc) { caption = entry.fullName.IsEmpty() ? entry.label : entry.fullName; break; }

	// A drop that landed ON a command-bar TOOLBAR (the form's own or a tablebox's) joins THAT bar as a new item, never a
	// free button — its control is what is selected after.
	if (dropBar != nullptr) {
		ibValueCommandBarItem* item = dropBar->AddCommandItem();   // fresh unique name + SetBar
		item->SetCommandDesc(desc, caption);   // the item's Caption stays unset -> the command supplies the DEFAULT look
		UpdateControl(dropBar->GetOwnerFrame());
		return dropBar->GetOwnerFrame();
	}

	// A drop onto a TABLEBOX projects the command onto ITS OWN command bar (a bar item where table-scoped commands
	// live) — the box is what is selected after. Every other drop — the form or a plain control — creates a BUTTON
	// control bound to the command.
	for (ibValueFrame* box = target; box != nullptr; box = box->GetParent()) {
		if (box->GetClassType() != g_controlTableBoxCLSID)
			continue;
		if (!box->HasCommandBar())
			break;
		ibValueCommandBarItem* item = box->GetCommandBar()->AddCommandItem();   // fresh unique name + SetBar
		item->SetCommandDesc(desc, caption);   // bind the command id path + its display name; the CAPTION is left
		// unset on purpose so the command provides the DEFAULT look (BuildCommands fills it via the command door).
		UpdateControl(box);
		return box;
	}

	// A drop ALWAYS ADDS a new command button — even onto an existing button — never overwrites the target's
	// binding. The new button lands next to the target, through the SAME create flow any dropped control uses
	// (CreateControlOfClass). The bind leaves the Title UNSET so the command supplies the DEFAULT caption + icon
	// (button->Update resolves them live via the command door).
	return CreateControlOfClass(target, ibProtocol::ibForm::ibType::Button, [&desc, &caption](ibValueFrame* control) {
		if (control != nullptr && control->GetClassType() == g_controlButtonCLSID) {   // type by clsid, not dynamic_cast
			ibValueButton* button = static_cast<ibValueButton*>(control);
			button->SetCommandDesc(desc, caption);
			button->SetCaption(wxEmptyString);
		}
	});
}

ibValueFrame* ibFormDesignView::CreateControlOfClass(ibValueFrame* parentHint, const wxString& className,
	const std::function<void(ibValueFrame*)>& bind)
{
	// Walk up until a parent that can hold the control (an item / leaf can't be a parent — climb to its container).
	ibValueFrame* parent = parentHint != nullptr ? parentHint : GetValueForm();
	ibValueFrame* obj = nullptr;
	while (parent != nullptr && obj == nullptr) {
		obj = m_valueForm->CreateObject(className, parent);
		if (obj == nullptr) {
			parent = parent->GetParent();
			while (parent != nullptr && parent->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
				parent = parent->GetParent();
		}
	}
	if (obj == nullptr)
		return nullptr;

	// Unwrap the sizer-item wrapper to the real control, then bind BEFORE the build: InsertObject runs the
	// control's Create -> Update pipeline, and Update reads the binding — so it renders correct on first paint.
	ibValueFrame* control = SkipSizerItems(obj);
	if (control != nullptr && bind)
		bind(control);

	InsertObject(obj, parent, CalcPositionOfInsertion(parentHint, parent));   // builds the widget (already bound)

	// A source-bound control (a tablebox) fills its DEFAULT columns from its source explorer now the widget
	// exists — the designer twin of the runtime CreateColumnCollection. A no-op for a scalar / a plain button
	// (not an ibTypeControlFactory).
	if (ibTypeControlFactory* factory = dynamic_cast<ibTypeControlFactory*>(control))
		factory->RefillFromSource();
	return control;
}

ibValueFrame* ibFormDesignView::PasteObject(ibValueFrame* dstObject, const ibDataNode& copied)
{
	wxASSERT(m_valueForm);

	// The copy read as the form reads a control of its own (LoadNode), made in what it is pasted on, then taken off it:
	// anchored across that — a child is owned by its parent's list.
	ibValueFrame* parentObject = dstObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM ? dstObject->GetParent() : dstObject;
	ibDataNode node = copied;
	RestoreProperties(node);
	ibValueFrame* const clipboard = m_valueForm->NewObject(node.GetClsid(), parentObject, false);
	if (clipboard == nullptr)
		return nullptr;
	const ibValuePtr<ibValueFrame> clipboardKeep(clipboard);
	const bool loaded = clipboard->LoadNode(node);
	parentObject->RemoveChild(clipboard);
	clipboard->SetParent(nullptr);
	if (!loaded)
		return nullptr;

	// If it cannot be put in what it was pasted on, it is put in its parent, right after it — and so on up: the first
	// that makes one of its class (CreateObject — the one made only asked).
	ibValueFrame* const content = SkipSizerItems(clipboard);
	if (content == nullptr)
		return nullptr;
	for (; parentObject != nullptr; parentObject = parentObject->GetParent()) {
		if (parentObject->GetComponentType() == COMPONENT_TYPE_SIZERITEM)
			continue;
		if (ibValueFrame* const made = m_valueForm->CreateObject(content->GetClassName(), parentObject)) {
			parentObject->RemoveChild(made);
			break;
		}
	}
	if (parentObject == nullptr)
		return nullptr;

	// …under this form's ids, its names unique on it — the ids first: a name is compared with the controls that have one.
	ResetId(clipboard);
	parentObject->AddChild(clipboard);
	clipboard->SetParent(parentObject);
	GenerateId(clipboard);
	ResolveNameConflict(m_valueForm, clipboard);
	InsertObject(clipboard, parentObject, CalcPositionOfInsertion(dstObject, parentObject));
	return content;
}

void ibFormDesignView::InsertObject(ibValueFrame* object, ibValueFrame* parent, int pos)
{
	// The desktop's InsertObjectCmd::Do — CreateObject put it in its parent already, at the end: its ids, its place,
	// its controls, the form's names. Not undoable here: the client takes the form answered as an edit of its own.
	GenerateId(object);
	if (pos >= 0)
		parent->ChangeChildPosition(object, pos);
	CreateControl(object, true);
	m_valueForm->InvalidateNames();
}

int ibFormDesignView::CalcPositionOfInsertion(ibValueFrame* selected, ibValueFrame* parent)
{
	// Right after the parent's child that holds the selection; at the end when the selection is not inside the parent.
	for (ibValueFrame* object = selected; parent != nullptr && object != nullptr; object = object->GetParent()) {
		if (object->GetParent() == parent)
			return parent->GetChildPosition(object) + 1;
	}
	return wxNOT_FOUND;
}
