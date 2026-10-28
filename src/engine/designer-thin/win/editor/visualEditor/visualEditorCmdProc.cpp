////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"   // ibFormDragItem — a drop on a tablebox applied (WireTableboxDrops)

#include <set>

#include "designer-thin/docManager/templates/docViewModuleEditor.h"   // ibModuleEditDocument — Modify, Fetch, Post
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolValue, ibWriteProtocolNode — a control's values
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // a layer's properties — a row's each
#include "frmclient/mainFrame/objinspect/objinspect.h"

#include <wx/artprov.h>
#include <wx/clipbrd.h>
#include <wx/menu.h>

// ⭐ THE DESKTOP'S COMMAND PROCESSOR OVER THE FORM'S NODE (designer visualEditorCmdProc.cpp): the desktop's commands
// changed the controls and undid each change; here a command holds the form's node before it and after it — the node
// never changed in place — and draws the one it leaves (ibVisualEditor::Draw). What the desktop's form decided as it
// made a control (ibValueForm::CreateObject: what a parent takes, and whether in a sizer item) is decided on the
// classes' types, as the tab's answer brought them.

namespace {

// The nodes from `node` down to the control with that id, both included — false: it is not under it.
bool FindPath(const ibProtocolNode& node, long long controlId, std::vector<ibProtocolNode>& path)
{
	path.push_back(node);
	if (node.GetInt(ibProtocol::ibForm::ControlId) == controlId)
		return true;
	for (const ibProtocolNode& child : node.Children()) {
		if (FindPath(child, controlId, path))
			return true;
	}
	path.pop_back();
	return false;
}

// The ids a frame's patch follows (NodeId) from `node` down to the node of the control — none: the control is `node`;
// false — it is not under it.
bool FindControlPath(const ibProtocolNode& node, long long controlId, std::vector<long long>& path)
{
	if (node.GetInt(ibProtocol::ibForm::ControlId) == controlId)
		return true;
	for (const ibProtocolNode& child : node.Children()) {
		path.push_back(child.GetId());
		if (FindControlPath(child, controlId, path))
			return true;
		path.pop_back();
	}
	return false;
}

// The state the server drew a control with — and each under it with its own — put in the patch's step of it, in place
// of the one held (ibFormDesignView::FetchRows).
void PatchState(const ibProtocolNode& drawn, ibProtocolNode step)
{
	step.AddItem(ibProtocol::ibNode::Removed, wxString(ibProtocol::ibForm::ibState::State));
	step.SetValue(ibProtocol::ibForm::ibState::State, drawn.FindChild(ibProtocol::ibForm::ibState::State));
	for (const ibProtocolNode& child : drawn.Children()) {
		ibProtocolNode childStep = step.AddChild();
		childStep.SetValue(ibProtocol::ibNode::Id, child.GetId());
		PatchState(child, childStep);
	}
}

// Where a child is among its parent's — wxNOT_FOUND: not there.
int IndexOf(const ibProtocolNode& parent, const ibProtocolNode& child)
{
	const long long controlId = child.GetInt(ibProtocol::ibForm::ControlId);
	const std::vector<ibProtocolNode> children = parent.Children();
	for (size_t idx = 0; idx < children.size(); idx++) {
		if (children[idx].GetInt(ibProtocol::ibForm::ControlId) == controlId)
			return static_cast<int>(idx);
	}
	return wxNOT_FOUND;
}

// The children a node has, given anew — copies first: the handles into its list go with the list.
void SetChildren(ibProtocolNode node, const std::vector<ibProtocolNode>& children)
{
	std::vector<ibProtocolNode> copies;
	for (const ibProtocolNode& child : children)
		copies.push_back(child.Clone());
	node.Remove(ibProtocol::ibNode::Children);
	for (const ibProtocolNode& child : copies) {
		ibProtocolNode added = node.AddChild();
		for (const auto& [name, entry] : child.Entries())
			added.SetValue(name, entry);
	}
}

// The control a node holds — itself, or what its sizer items hold.
ibProtocolNode Unwrap(const ibProtocolNode& node)
{
	ibProtocolNode inner = node;
	while (inner.GetType() == ibProtocol::ibForm::ibType::SizerItem) {
		const std::vector<ibProtocolNode> children = inner.Children();
		if (children.empty())
			break;
		inner = children.front();
	}
	return inner;
}

// The desktop's DetermineObjectToSelect: the control now at the place one went from, else the last, else the parent.
long long DetermineObjectToSelect(const ibProtocolNode& parent, unsigned int pos)
{
	const std::vector<ibProtocolNode> children = parent.Children();
	if (children.empty())
		return parent.GetInt(ibProtocol::ibForm::ControlId);
	return Unwrap(children[std::min<size_t>(pos, children.size() - 1)]).GetInt(ibProtocol::ibForm::ControlId);
}

// A property of a node given that value, as the server's WriteData writes it — in place of what it was: a value that is
// a node is not merged into the old one.
void SetNodeProperty(ibProtocolNode node, const wxString& name, const ibDataValue& value)
{
	node.Remove(name.utf8_str());
	ibDataNode entries;
	entries.SetProperty(name, value);
	ibWriteProtocolNode(entries, node);
}

// The desktop's PropagateExpansion, into a frame's patch: the node at `step` and every one under it (`node`'s) closed —
// opened, the desktop's opened its parents instead, the path's steps (ExpandObject).
void PropagateExpansion(const ibProtocolNode& node, ibProtocolNode step, bool expand)
{
	step.SetValue(ibProtocol::ibForm::Expanded, expand);
	for (const ibProtocolNode& child : node.Children()) {
		ibProtocolNode childStep = step.AddChild();
		childStep.SetValue(ibProtocol::ibNode::Id, child.GetId());
		PropagateExpansion(child, childStep, expand);
	}
}

// A door of the server's editor over one of the form's attributes (`owner` Attribute) or commands (FormCommand), or a
// control's paste in a control (Control) — that one by its id; none: a door of the form's (Create, Paste).
ibProtocolNode EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest kind, ibProtocol::ibView::ibFormDesigner::ibCommand command, const char* owner, long long id)
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(kind));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Command, static_cast<int>(command));
	if (id != 0)
		request.SetValue(owner, id);
	return request;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////////////

// The form before a command and after it — and what the selection was each time, and its trees; `patch`, what of the
// form it changed, when that is all it changed (a property: drawn in place).
class ibVisualEditorFormCmd : public ibVisualEditorCmd {
public:

	ibVisualEditorFormCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, const ibProtocolNode& before,
		const ibProtocolNode& after, const ibProtocolNode& patch, long long selectedBefore, long long selectedAfter,
		const ibProtocolNode& treesBefore, const ibProtocolNode& treesAfter) :
		m_visualEditor(visualEditor), m_before(before), m_after(after), m_patch(patch),
		m_selectedBefore(selectedBefore), m_selectedAfter(selectedAfter), m_treesBefore(treesBefore), m_treesAfter(treesAfter) {
	}

protected:

	virtual void DoExecute() override {
		m_visualEditor->Draw(m_after, m_treesAfter, m_patch, m_selectedAfter);
	}
	virtual void DoRestore() override {
		m_visualEditor->Draw(m_before, m_treesBefore, ibProtocolNode(), m_selectedBefore);
	}

private:

	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	ibProtocolNode m_before;
	ibProtocolNode m_after;
	ibProtocolNode m_patch;
	long long m_selectedBefore;
	long long m_selectedAfter;
	ibProtocolNode m_treesBefore;
	ibProtocolNode m_treesAfter;
};

/**
* Command for modifying a property.
*/

class ibVisualEditorModifyPropertyCmd :
	public ibVisualEditorCmd {
public:

	ibVisualEditorModifyPropertyCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue);

protected:

	virtual void DoExecute() override;
	virtual void DoRestore() override;

private:
	// The property set to that value, the control drawn as it says — both found again by the control's id and the
	// property's name: the rows the server answers make the control's properties anew (UpdateControl), and a pointer
	// kept from the edit before pointed at one gone.
	void SetValue(const wxVariant& value);

	ibVisualEditorNotebook::ibVisualEditor* m_visualEditor;

	long long m_controlId;
	wxString m_name;
	wxVariant m_oldValue, m_newValue;
};

ibVisualEditorModifyPropertyCmd::ibVisualEditorModifyPropertyCmd(ibVisualEditorNotebook::ibVisualEditor* visualEditor, ibProperty* prop, const wxVariant& oldValue, const wxVariant& newValue) : m_visualEditor(visualEditor),
m_controlId(dynamic_cast<ibValueFrame*>(prop->GetPropertyObject())->GetControlID()), m_name(prop->GetName()),
m_oldValue(oldValue), m_newValue(newValue)
{
}

void ibVisualEditorModifyPropertyCmd::DoExecute()
{
	SetValue(m_newValue);
}

void ibVisualEditorModifyPropertyCmd::DoRestore()
{
	SetValue(m_oldValue);
}

void ibVisualEditorModifyPropertyCmd::SetValue(const wxVariant& value)
{
	ibValueForm* const valueForm = m_visualEditor->GetValueForm();
	ibValueFrame* const control = valueForm == nullptr ? nullptr
		: m_controlId == valueForm->GetControlID() ? valueForm : valueForm->FindControlByID(m_controlId);
	ibProperty* const property = control != nullptr ? control->GetProperty(m_name) : nullptr;
	if (property == nullptr)
		return;

	property->SetValue(value);
	m_visualEditor->UpdateControl(control);
	m_visualEditor->RefreshCurrentElement();
}

void ibVisualEditorNotebook::ibVisualEditor::Execute(ibVisualEditorCmd* cmd)
{
	// The form it leaves went to the server as it was drawn (Draw); the document said modified after it — what that posts
	// goes behind the form, never the form behind it.
	m_cmdProc.Execute(cmd);
	m_document->Modify(true);
}

bool ibVisualEditorNotebook::ibVisualEditor::Execute(const ibProtocolNode& request, ibProtocolNode& answer)
{
	if (!m_document->Fetch(request, answer))
		return false;
	const ibProtocolNode form = answer.FindChild(ibProtocol::ibView::ibFormDesigner::Form);
	if (form.GetType() != ibProtocol::ibForm::ibType::ClientForm)
		return false;

	// The rows held are of the form before it — each asked again as it is shown (FetchRows).
	m_rows.clear();
	ibProtocolNode trees;
	Receive(answer, trees);

	// ⭐ WHERE IT LANDS IS THE ANSWER'S — a control it selects (Selected), drawn so; the attribute it shows (Attribute, a
	// column of it: Column) the current element once drawn, the one current before let go of: drawn, it was shown again
	// and left at once (the inspector filled twice a paste, 2026-10-22).
	const bool showsAttribute = answer.Has(ibProtocol::ibView::ibFormDesigner::Attribute);
	if (showsAttribute)
		m_element = nullptr;

	// …done by the server, which holds the form it answered and wrote it into its metaobject (ibFormDesignView::Fetch):
	// drawn, not handed back.
	m_cmdProc.Receive(new ibVisualEditorFormCmd(this, m_formNode, form, ibProtocolNode(), m_selected,
		showsAttribute ? wxNOT_FOUND : answer.GetInt(ibProtocol::ibView::ibFormDesigner::Selected, m_selected), m_trees, trees));
	if (showsAttribute)
		m_attributeTree->SelectAttribute(answer.GetInt(ibProtocol::ibView::ibFormDesigner::Attribute),
			answer.GetInt(ibProtocol::ibView::ibFormDesigner::Column));
	m_document->Modify(true);
	return true;
}

///////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::CreateObject(const wxString& name)
{
	if (!IsEditable())
		return;

	// Made by the server's form, as the desktop's made it: under the selected control or the first of its parents that
	// can hold it, and one that binds to a source bound to a fresh attribute — the answer brings both.
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Control));
	if (m_selected != wxNOT_FOUND)
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, m_selected);
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Class, name);
	ibProtocolNode answer;
	Execute(request, answer);
}

void ibVisualEditorNotebook::ibVisualEditor::CreateBoundControl(ibValueFrame* parentHint, const ibSourceDescription& desc)
{
	if (!IsEditable() || !desc.IsOk())
		return;

	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::BoundControl));
	if (parentHint != nullptr)
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, parentHint->GetControlID());
	WritePath(request, desc.GetPath());
	ibProtocolNode answer;
	Execute(request, answer);
}

void ibVisualEditorNotebook::ibVisualEditor::CreateCommandButton(ibValueFrame* target, const ibCommandDescription& desc)
{
	if (!IsEditable() || !desc.IsOk())
		return;

	// The command under Value — the request's own Kind is the request's. A drop that landed ON a command-bar TOOLBAR
	// (the resolver tagged it) joins THAT bar as a new item (CommandBar), never a free button. Read + clear the per-drop
	// channel so it can't leak to the next drop.
	const long long dropBar = m_pendingDropBar;
	m_pendingDropBar = 0;
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::CommandControl));
	if (dropBar != 0) {
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, dropBar);
		request.SetValue(ibProtocol::ibView::ibFormDesigner::CommandBar, true);
	}
	else if (target != nullptr)
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, target->GetControlID());
	ibProtocolNode command = request.Child(ibProtocol::ibView::ibFormDesigner::Value);
	WritePath(command, desc);
	ibProtocolNode answer;
	Execute(request, answer);
}

void ibVisualEditorNotebook::ibVisualEditor::WireTableboxDrops(ibValueFrame* obj)
{
	if (obj == nullptr)
		return;
	// Set ONE drop target on the tablebox's window. Re-wired on every editor refresh, since the controls are built anew
	// and take their drop targets with them.
	if (obj->GetClassName() == ibProtocol::ibForm::ibType::Tablebox) {
		ibValueFrame* box = obj;
		if (wxWindow* const window = obj->GetWindow())
			// A drop ON the tablebox targets THIS table (fixed target = box, the point is irrelevant): a source path → a
			// bound column, a command → a command on this table. DEFERRED — this target lives ON the table, and the
			// apply draws the form anew, freeing THIS very target mid-callback (use-after-free); the target clones the
			// decoded item and runs the apply after the drag unwinds.
			window->SetDropTarget(new ibSourceDragDropTarget(
				// Drop on the tablebox targets THIS table; clear the per-drop command-bar channel (no form toolbar here) so
				// a command drop climbs to the table's OWN bar instead of reading a stale bar from a prior drop.
				[this, box](wxCoord, wxCoord) -> ibValueFrame* { m_pendingDropBar = 0; return box; },
				[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(this, target); },
				/*deferred*/ true));
	}
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		WireTableboxDrops(obj->GetChild(i));
}

void ibVisualEditorNotebook::ibVisualEditor::RemoveObject(ibValueFrame* obj)
{
	DoRemoveObject(obj, false);
}

void ibVisualEditorNotebook::ibVisualEditor::CutObject(ibValueFrame* obj)
{
	CopyObject(obj);
	DoRemoveObject(obj, true);
}

void ibVisualEditorNotebook::ibVisualEditor::DoRemoveObject(ibValueFrame* obj, bool WXUNUSED(cutObject))
{
	if (obj == nullptr || obj == GetValueForm() || !IsEditable())
		return;

	ibProtocolNode edited = m_formNode.Clone();
	std::vector<ibProtocolNode> path;
	if (!FindPath(edited, obj->GetControlID(), path) || path.size() < 2)
		return;

	// When removing an object it is important that the "item" objects are not left behind — the top item goes.
	size_t level = path.size() - 1;
	while (level > 1 && path[level - 1].GetType() == ibProtocol::ibForm::ibType::SizerItem)
		level--;

	ibProtocolNode parent = path[level - 1];
	std::vector<ibProtocolNode> children = parent.Children();
	const int pos = IndexOf(parent, path[level]);
	if (pos == wxNOT_FOUND)
		return;
	children.erase(children.begin() + pos);
	SetChildren(parent, children);

	Execute(new ibVisualEditorFormCmd(this, m_formNode, edited, ibProtocolNode(), m_selected,
		DetermineObjectToSelect(parent, pos), m_trees, m_trees));
}

void ibVisualEditorNotebook::ibVisualEditor::CopyObject(ibValueFrame* obj)
{
	if (obj == nullptr || obj == GetValueForm())
		return;

	// The item it is in goes with it — how it sits is part of it.
	std::vector<ibProtocolNode> path;
	if (!FindPath(m_formNode, obj->GetControlID(), path))
		return;
	const ibProtocolNode& copied = path.size() > 1 && path[path.size() - 2].GetType() == ibProtocol::ibForm::ibType::SizerItem ?
		path[path.size() - 2] : path.back();

	// THE COPY — the node as the form holds it: what the server's Paste reads (its controls' rows come with the answer).
	CopyEntry(copied, oes_clipboard_frame, obj->GetControlName());
}

bool ibVisualEditorNotebook::ibVisualEditor::PasteObject(ibValueFrame* dstObject)
{
	if (!IsEditable())
		return false;

	const ibProtocolNode copied = PasteEntry(oes_clipboard_frame);
	if (!copied.IsNode())
		return false;

	// ⭐ PUT BY THE SERVER'S FORM, as the desktop's put it — in the control pasted on, else in the first of its parents
	// that can hold it: its ids given where every control's are (one numbering — two gave two controls one id,
	// 2026-10-15), its names unique on the form; the answer brings it, and its controls' rows.
	ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Control, ibProtocol::ibView::ibFormDesigner::ibCommand::Paste,
		ibProtocol::ibView::ibFormDesigner::Control, dstObject != nullptr ? dstObject->GetControlID() : 0);
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Value, copied);
	ibProtocolNode answer;
	return Execute(request, answer);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::MovePosition(ibValueFrame* obj, bool right, unsigned int num)
{
	if (obj == nullptr || obj == GetValueForm() || !IsEditable())
		return;

	ibProtocolNode edited = m_formNode.Clone();
	std::vector<ibProtocolNode> path;
	if (!FindPath(edited, obj->GetControlID(), path) || path.size() < 2)
		return;

	// If the object is contained within an item, the item must be shifted.
	size_t level = path.size() - 1;
	while (level > 1 && path[level - 1].GetType() == ibProtocol::ibForm::ibType::SizerItem)
		level--;

	ibProtocolNode parent = path[level - 1];
	std::vector<ibProtocolNode> children = parent.Children();
	const int found = IndexOf(parent, path[level]);
	if (found == wxNOT_FOUND)
		return;
	const unsigned int pos = static_cast<unsigned int>(found);

	// make sure the bounds are correct
	if ((right && num + pos < children.size()) || (!right && num <= pos)) {
		const unsigned int to = right ? pos + num : pos - num;
		const ibProtocolNode moved = children[pos];
		children.erase(children.begin() + pos);
		children.insert(children.begin() + to, moved);
		SetChildren(parent, children);
		Execute(new ibVisualEditorFormCmd(this, m_formNode, edited, ibProtocolNode(), m_selected, obj->GetControlID(),
			m_trees, m_trees));
	}
}

void ibVisualEditorNotebook::ibVisualEditor::MovePosition(ibValueFrame* obj, ibValueFrame* parent, unsigned int toPos)
{
	if (obj == nullptr || parent == nullptr || obj == GetValueForm() || !IsEditable())
		return;

	// Once the event that moved it is over: the form drawn anew builds anew the control that told it.
	const long long controlId = obj->GetControlID(), parentId = parent->GetControlID();
	CallAfter([this, controlId, parentId, toPos]() {
		ibProtocolNode edited = m_formNode.Clone();
		std::vector<ibProtocolNode> path;
		if (!FindPath(edited, controlId, path) || path.size() < 2)
			return;

		// If the object is contained within an item, the item must be moved.
		size_t level = path.size() - 1;
		while (level > 1 && path[level - 1].GetType() == ibProtocol::ibForm::ibType::SizerItem)
			level--;

		ibProtocolNode from = path[level - 1];
		const int pos = IndexOf(from, path[level]);
		if (pos == wxNOT_FOUND || (from.GetInt(ibProtocol::ibForm::ControlId) == parentId && static_cast<unsigned int>(pos) == toPos))
			return;
		const ibProtocolNode moved = path[level].Clone();
		std::vector<ibProtocolNode> children = from.Children();
		children.erase(children.begin() + pos);
		SetChildren(from, children);

		// …and the parent found again: the list it was taken from was given anew, the handles into it with it.
		std::vector<ibProtocolNode> parentPath;
		if (!FindPath(edited, parentId, parentPath))
			return;
		ibProtocolNode to = parentPath.back();
		children = to.Children();
		children.insert(children.begin() + std::min<size_t>(toPos, children.size()), moved);
		SetChildren(to, children);
		Execute(new ibVisualEditorFormCmd(this, m_formNode, edited, ibProtocolNode(), m_selected, controlId, m_trees, m_trees));
	});
}

void ibVisualEditorNotebook::ibVisualEditor::ExpandObject(ibValueFrame* obj, bool expand)
{
	if (obj == nullptr)
		return;
	// A view-only form is not edited — open or closed in its tree only.
	if (!IsEditable()) {
		obj->SetExpanded(expand);
		return;
	}

	std::vector<ibProtocolNode> nodes;
	std::vector<long long> path;
	if (!FindPath(m_formNode, obj->GetControlID(), nodes) || !FindControlPath(m_formNode, obj->GetControlID(), path))
		return;

	// ⭐ ONE COMMAND, BY A PATCH OF THE FRAME — the desktop's were one an object (ExpandObject, PropagateExpansion): opened,
	// every one it is under opened too; closed, every one under it.
	ibProtocolNode patch;
	ibProtocolNode step = patch;
	if (expand)
		step.SetValue(ibProtocol::ibForm::Expanded, true);
	for (const long long id : path) {
		step = step.AddChild();
		step.SetValue(ibProtocol::ibNode::Id, id);
		if (expand)
			step.SetValue(ibProtocol::ibForm::Expanded, true);
	}
	if (!expand)
		PropagateExpansion(nodes.back(), step, false);

	ibProtocolNode edited = m_formNode.Clone();
	edited.Apply(patch);
	Execute(new ibVisualEditorFormCmd(this, m_formNode, edited, patch, m_selected, m_selected, m_trees, m_trees));
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ModifyProperty(ibProperty* prop, const wxVariant& oldValue,
	const wxVariant& newValue)
{
	// The pair is the value before the edit and after it, as the inspector moves them (the desktop's); what goes to the
	// server, the property's node.
	ibPropertyObject* const owner = prop->GetPropertyObject();
	ibDataValue value;
	if (owner == nullptr || !prop->CopyNodeValue(value))
		return;
	ibValueFrame* const control = dynamic_cast<ibValueFrame*>(owner);

	// AN EVENT GIVEN A HANDLER — its procedure written in the module first, as the desktop's editor did
	// (ibVisualEditorNotebook::ModifyEvent): the handler is its node's Value.
	const wxString type = prop->GetType();
	const auto handler = value.Kind() == ibDataKind::Child ? value.AsChild() : std::nullopt;
	if ((type == wxT("ibEventControl") || type == wxT("ibEventAction")) && handler)
		m_visualNotebook->ModifyEvent(prop, oldValue, newValue);

	// The command once the inspector's change is over: what it shows is not drawn again inside its own handler.
	const wxString name = prop->GetName();

	// An attribute's, a form command's — the server's editor's door; the form its answer draws shows it again (Draw).
	if (ibFormAttributeValue* const entry = m_attributeTree->FindEntry(owner)) {
		const long long id = entry->GetId();
		const long long column = entry->GetColumnId();
		CallAfter([this, id, column, name, value]() {
			ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::Set,
				ibProtocol::ibView::ibFormDesigner::Attribute, id);
			if (column != 0)
				request.SetValue(ibProtocol::ibView::ibFormDesigner::Column, column);
			request.SetValue(ibProtocol::ibView::ibFormDesigner::Name, name);
			SetNodeProperty(request, ibProtocol::ibView::ibFormDesigner::Value, value);
			ibProtocolNode answer;
			Execute(request, answer);
		});
		return;
	}
	if (ibFormCommandValue* const entry = m_commandTree->FindEntry(owner)) {
		const long long id = entry->GetId();
		CallAfter([this, id, name, value]() {
			ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::FormCommand, ibProtocol::ibView::ibFormDesigner::ibCommand::Set,
				ibProtocol::ibView::ibFormDesigner::FormCommand, id);
			request.SetValue(ibProtocol::ibView::ibFormDesigner::Name, name);
			SetNodeProperty(request, ibProtocol::ibView::ibFormDesigner::Value, value);
			ibProtocolNode answer;
			Execute(request, answer);
		});
		return;
	}

	// A layer's — its node in the control's, the form so edited drawn by the server; the layer shown again.
	if (ibValueLayerObject* const layer = m_objectTree->FindEntry(owner)) {
		CallAfter([this, layer, name, value]() {
			ibProtocolNode edited = m_formNode.Clone();
			std::vector<ibProtocolNode> path;
			if (!FindPath(edited, layer->GetControlID(), path))
				return;
			const ibProtocolNode node = layer->FindNode(path.back());
			if (!node.IsNode())
				return;
			SetNodeProperty(node, name, value);

			ibProtocolNode request;
			request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Form));
			request.SetValue(ibProtocol::ibView::ibFormDesigner::Form, edited);
			ibProtocolNode answer;
			if (Execute(request, answer))
				m_objectTree->SelectCommandItem(layer);
		});
		return;
	}

	if (control == nullptr || oldValue == newValue)
		return;

	// The tree told first: the command makes the control's properties anew from the server's rows (UpdateControl).
	CallAfter([this, prop, oldValue, newValue]() {
		NotifyPropertyModified(prop);
		Execute(new ibVisualEditorModifyPropertyCmd(this, prop, oldValue, newValue));
	});
}

void ibVisualEditorNotebook::ibVisualEditor::UpdateControl(ibValueFrame* control)
{
	// THE DESKTOP'S (ibVisualHost::UpdateControl) — the control drawn as its properties say: what of them its node does
	// not hold goes into it BY A PATCH OF THE FRAME, the road the server's answers take (ibVisualHostClient::Draw) — the
	// control drawn in place, never built anew; in place of what it was, a value that is a node not merged into the old
	// one; none — taken off the node.
	std::vector<ibProtocolNode> nodes;
	std::vector<long long> path;
	if (control == nullptr || !FindPath(m_formNode, control->GetControlID(), nodes) ||
		!FindControlPath(m_formNode, control->GetControlID(), path))
		return;

	ibProtocolNode patch;
	ibProtocolNode step = patch;
	for (const long long id : path) {
		step = step.AddChild();
		step.SetValue(ibProtocol::ibNode::Id, id);
	}
	ibDataNode entries;
	for (unsigned int idx = 0; idx < control->GetPropertyCount(); idx++) {
		const ibProperty* const property = control->GetPropertyByIndex(idx);
		ibDataValue value;
		if (!property->CopyNodeValue(value))
			continue;
		ibDataValue held;
		if (ibReadProtocolValue(nodes.back(), property->GetName(), held) ? held == value : value.IsEmpty())
			continue;
		step.AddItem(ibProtocol::ibNode::Removed, property->GetName());
		if (!value.IsEmpty())
			entries.SetProperty(property->GetName(), value);
	}
	ibWriteProtocolNode(entries, step);

	ibProtocolNode edited = m_formNode.Clone();
	edited.Apply(patch);

	// ⭐ ITS STATE IS THE SERVER'S — enabled with its form, shown where available and bound: asked of the form so edited
	// and put in the same patch (a control made not enabled was drawn enabled, 2026-10-15).
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Rows));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, control->GetControlID());
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Form, edited);
	ibProtocolNode answer;
	if (m_document->Fetch(request, answer)) {
		PatchState(answer.FindChild(ibProtocol::ibView::ibFormDesigner::Node), step);
		edited = m_formNode.Clone();
		edited.Apply(patch);

		// …and its rows as the server wrote them after the edit: a value's words are the server's to say — a source
		// picked reads as its name only then (it read "<not selected>", 2026-10-21). Changed, the control receives them
		// anew as it is drawn; the answer brings no trees — the form's stay. The others held are of the form before the
		// edit — a column's under its table edited — each asked again as it is shown.
		m_rows.clear();
		ibProtocolNode trees;
		Receive(answer, trees);

		// The request handed the form so edited, and the server holds it (written as a Change writes it): drawn, it is
		// not handed again.
		m_cmdProc.SetSavePoint();
	}

	Draw(edited, m_trees, patch, m_selected);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::CreateAttribute()
{
	if (!IsEditable())
		return;
	ibProtocolNode answer;
	Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::Create,
		ibProtocol::ibView::ibFormDesigner::Attribute, 0), answer);
}

void ibVisualEditorNotebook::ibVisualEditor::RemoveAttribute(ibFormAttributeValue* entry)
{
	if (entry == nullptr || !IsEditable())
		return;
	ibProtocolNode answer;
	Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::Remove,
		ibProtocol::ibView::ibFormDesigner::Attribute, entry->GetId()), answer);
}

void ibVisualEditorNotebook::ibVisualEditor::CutAttribute(ibFormAttributeValue* entry)
{
	CopyAttribute(entry);
	RemoveAttribute(entry);
}

void ibVisualEditorNotebook::ibVisualEditor::CopyAttribute(ibFormAttributeValue* entry)
{
	if (entry != nullptr)
		CopyEntry(ibProtocol::ibView::ibFormDesigner::Attributes, entry->GetId(), oes_clipboard_attribute, entry->GetName());
}

void ibVisualEditorNotebook::ibVisualEditor::PasteAttribute()
{
	const ibProtocolNode copied = PasteEntry(oes_clipboard_attribute);
	if (!copied.IsNode() || !IsEditable())
		return;
	ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::Paste,
		ibProtocol::ibView::ibFormDesigner::Attribute, 0);
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Value, copied);
	ibProtocolNode answer;
	Execute(request, answer);
}

bool ibVisualEditorNotebook::ibVisualEditor::RenameAttribute(ibFormAttributeValue* entry, const wxString& newName)
{
	if (entry == nullptr || !IsEditable())
		return false;
	ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::Set,
		ibProtocol::ibView::ibFormDesigner::Attribute, entry->GetId());
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Name, wxString(ibProtocol::ibView::ibFormDesigner::Name));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Value, newName);
	ibProtocolNode answer;
	return Execute(request, answer);
}

void ibVisualEditorNotebook::ibVisualEditor::SetMainAttribute(ibFormAttributeValue* entry)
{
	if (!IsEditable())
		return;
	ibProtocolNode answer;
	Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::SetMain,
		ibProtocol::ibView::ibFormDesigner::Attribute, entry != nullptr ? entry->GetId() : 0), answer);
}

void ibVisualEditorNotebook::ibVisualEditor::AddColumn(ibFormAttributeValue* entry)
{
	if (entry == nullptr || !IsEditable())
		return;
	ibProtocolNode answer;
	Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::Attribute, ibProtocol::ibView::ibFormDesigner::ibCommand::AddColumn,
		ibProtocol::ibView::ibFormDesigner::Attribute, entry->GetId()), answer);
}

/////////////////////////////////////////////////////////////////////////////////////

long long ibVisualEditorNotebook::ibVisualEditor::CreateFormCommand()
{
	if (!IsEditable())
		return 0;
	ibProtocolNode answer;
	if (!Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::FormCommand, ibProtocol::ibView::ibFormDesigner::ibCommand::Create,
			ibProtocol::ibView::ibFormDesigner::FormCommand, 0), answer))
		return 0;
	return answer.GetInt(ibProtocol::ibView::ibFormDesigner::FormCommand);
}

void ibVisualEditorNotebook::ibVisualEditor::RemoveFormCommand(ibFormCommandValue* entry)
{
	if (entry == nullptr || !IsEditable())
		return;
	ibProtocolNode answer;
	Execute(EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::FormCommand, ibProtocol::ibView::ibFormDesigner::ibCommand::Remove,
		ibProtocol::ibView::ibFormDesigner::FormCommand, entry->GetId()), answer);
}

void ibVisualEditorNotebook::ibVisualEditor::CutFormCommand(ibFormCommandValue* entry)
{
	CopyFormCommand(entry);
	RemoveFormCommand(entry);
}

void ibVisualEditorNotebook::ibVisualEditor::CopyFormCommand(ibFormCommandValue* entry)
{
	if (entry != nullptr)
		CopyEntry(ibProtocol::ibView::ibFormDesigner::FormCommands, entry->GetId(), oes_clipboard_command, entry->GetName());
}

long long ibVisualEditorNotebook::ibVisualEditor::PasteFormCommand()
{
	const ibProtocolNode copied = PasteEntry(oes_clipboard_command);
	if (!copied.IsNode() || !IsEditable())
		return 0;
	ibProtocolNode request = EntryRequest(ibProtocol::ibView::ibFormDesigner::ibRequest::FormCommand, ibProtocol::ibView::ibFormDesigner::ibCommand::Paste,
		ibProtocol::ibView::ibFormDesigner::FormCommand, 0);
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Value, copied);
	ibProtocolNode answer;
	if (!Execute(request, answer))
		return 0;
	return answer.GetInt(ibProtocol::ibView::ibFormDesigner::FormCommand);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::CopyEntry(const char* owner, long long id, const wxString& format,
	const wxString& name)
{
	// THE COPY — the entry's node as the form holds it (its description, its held value): what the server's Paste reads.
	for (const ibProtocolNode& entry : m_formNode.FindChild(owner).Children()) {
		if (entry.GetId() == id) {
			CopyEntry(entry, format, name);
			return;
		}
	}
}

void ibVisualEditorNotebook::ibVisualEditor::CopyEntry(const ibProtocolNode& entry, const wxString& format,
	const wxString& name)
{
	// The clipboard's are bytes — UTF-8, made here once, and read back once in PasteEntry.
	const std::string text = entry.Write().ToUtf8();
	if (wxTheClipboard->Open()) {
		wxDataObjectComposite* const composite = new wxDataObjectComposite;
		wxCustomDataObject* const data = new wxCustomDataObject(wxDataFormat(format));
		data->SetData(text.size(), text.data());
		composite->Add(data, true);
		composite->Add(new wxTextDataObject(name));
		wxTheClipboard->SetData(composite);
		wxTheClipboard->Close();
	}
}

ibProtocolNode ibVisualEditorNotebook::ibVisualEditor::PasteEntry(const wxString& format) const
{
	ibProtocolNode copied;
	if (!wxTheClipboard->Open())
		return copied;
	const wxDataFormat dataFormat(format);
	if (wxTheClipboard->IsSupported(dataFormat)) {
		wxCustomDataObject data(dataFormat);
		if (wxTheClipboard->GetData(data))
			ibProtocolNode::Read(ibString::FromUTF8(static_cast<const char*>(data.GetData()), data.GetSize()), copied);
	}
	wxTheClipboard->Close();
	return copied;
}

void ibVisualEditorNotebook::ibVisualEditor::WritePath(ibProtocolNode& node, const std::vector<ibSourceHop>& path)
{
	// A Path item per hop: its id, and the type it is pinned to — a clsid as the wire carries it, 64 bits as text; none
	// pinned: no Type (the server's ReadPath, its twin, reads it back).
	for (const ibSourceHop& hop : path) {
		ibProtocolNode item = node.AddItem(ibProtocol::ibView::ibFormDesigner::Path);
		item.SetValue(ibProtocol::ibView::ibFormDesigner::Id, static_cast<long long>(hop.m_id));
		if (hop.m_type != g_valueUndefinedCLSID)
			item.SetValue(ibProtocol::ibView::ibFormDesigner::Type, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(hop.m_type)));
	}
}

void ibVisualEditorNotebook::ibVisualEditor::WritePath(ibProtocolNode& node, const ibCommandDescription& desc)
{
	// A CommandPath item per hop, its id; and the command's kind (the server's ReadPath, its twin, reads it back).
	for (const ibCommandHop& hop : desc.GetPath())
		node.AddItem(ibProtocol::ibView::ibFormDesigner::CommandPath).SetValue(ibProtocol::ibView::ibFormDesigner::Id, static_cast<long long>(hop.m_id));
	node.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<long long>(desc.GetCommandType()));
}

void ibVisualEditorNotebook::ibVisualEditor::TestForm()
{
	ibProtocolNode args;
	args.SetValue(ibProtocol::ibView::ibModule::Id, static_cast<int>(ibProtocol::ibView::ibModule::ibCommand::TestForm));
	args.SetValue(ibProtocol::ibView::ibFormDesigner::Form, m_formNode);
	m_document->Post(ibProtocol::ibEvent::ibKind::Command, args);
}

/////////////////////////////////////////////////////////////////////////////////////

bool ibVisualEditorNotebook::ibVisualEditor::SelectObject(ibValueFrame* obj, bool force, bool notify)
{
	if (obj == nullptr)
		return false;
	// THE SAME ONE ACTIVATED AGAIN — its selection does not change, yet the inspector may show another's (the
	// configuration's tree, clicked after it): its properties activated all the same, as the desktop's tree activated an
	// item already selected (EVT_TREE_ITEM_ACTIVATED); the one shown — nothing.
	if (obj->GetControlID() == m_selected && !force) {
		objectInspector->SelectObject(obj);
		return false;
	}

	m_selected = obj->GetControlID();
	m_visualEditor->SetObjectSelect(obj);   // the form -> the canvas highlight dropped
	// SOFT: fills only an inspector already shown (the desktop's ibEditorSelection::Reveal).
	objectInspector->SelectObject(obj, true);

	if (notify)
		NotifyObjectSelected(obj, force);
	return true;
}

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::GetSelectedObject() const
{
	ibValueForm* const valueForm = GetValueForm();
	if (valueForm == nullptr)
		return nullptr;
	ibValueFrame* const frame = valueForm->FindControlByID(m_selected);
	return frame != nullptr ? frame : valueForm;
}

void ibVisualEditorNotebook::ibVisualEditor::SetCurrentElement(ibPropertyObject* element)
{
	if (element == nullptr)
		return;
	// A control clicked after is selected again — never "already selected" while the inspector shows the element.
	m_selected = wxNOT_FOUND;
	m_element = element;
	m_visualEditor->ClearObjectSelect();
	// SOFT: fills only an inspector already shown (the desktop's ibEditorSelection::Reveal), what it shows received anew.
	objectInspector->SelectObject(element, true);
}

ibPropertyObject* ibVisualEditorNotebook::ibVisualEditor::GetCurrentElement() const
{
	if (m_selected != wxNOT_FOUND)
		return GetSelectedObject();
	if (m_attributeTree->FindEntry(m_element) != nullptr || m_commandTree->FindEntry(m_element) != nullptr ||
		m_objectTree->FindEntry(m_element) != nullptr)
		return m_element;
	return nullptr;
}

void ibVisualEditorNotebook::ibVisualEditor::ShowCurrentElement(bool forceOpen)
{
	if (forceOpen && !objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	ibPropertyObject* const element = GetCurrentElement();
	objectInspector->SelectObject(element != nullptr ? element : GetSelectedObject(), true);
}

void ibVisualEditorNotebook::ibVisualEditor::RefreshCurrentElement()
{
	// The desktop's (ibEditorSelection::Reveal, soft): no panel — nothing to refresh; else the grid rebuilt on the current
	// element.
	ibPropertyObject* const element = GetCurrentElement();
	if (element == nullptr || !objectInspector->IsShownInspector())
		return;
	objectInspector->SelectObject(element, true);
}

void ibVisualEditorNotebook::ibVisualEditor::ScrollToObject(ibValueFrame* obj)
{
	m_visualEditor->ScrollToObject(obj);
}

/////////////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::Undo()
{
	if (!m_cmdProc.CanUndo())
		return;
	m_cmdProc.Undo();
	m_document->Modify(true);
}

void ibVisualEditorNotebook::ibVisualEditor::Redo()
{
	if (!m_cmdProc.CanRedo())
		return;
	m_cmdProc.Redo();
	m_document->Modify(true);
}

bool ibVisualEditorNotebook::ibVisualEditor::CanPasteObject() const
{
	if (GetSelectedObject() == nullptr || !wxTheClipboard->Open())
		return false;
	const bool canPasteObject = wxTheClipboard->IsSupported(wxDataFormat(oes_clipboard_frame));
	wxTheClipboard->Close();
	return canPasteObject;
}

bool ibVisualEditorNotebook::ibVisualEditor::CanCopyObject() const
{
	ibValueFrame* const obj = GetSelectedObject();
	return obj != nullptr && obj != GetValueForm();
}

bool ibVisualEditorNotebook::ibVisualEditor::CanPasteAttribute() const
{
	if (!wxTheClipboard->Open())
		return false;
	const bool canPaste = wxTheClipboard->IsSupported(wxDataFormat(oes_clipboard_attribute));
	wxTheClipboard->Close();
	return canPaste;
}

bool ibVisualEditorNotebook::ibVisualEditor::CanPasteFormCommand() const
{
	if (!wxTheClipboard->Open())
		return false;
	const bool canPaste = wxTheClipboard->IsSupported(wxDataFormat(oes_clipboard_command));
	wxTheClipboard->Close();
	return canPaste;
}

///////////////////////////////////////////////////////////////////////////////
// ibValueLayerObject — THE DESKTOP'S LAYERS (frontend visualView/layers/commandBar.cpp) OVER THE CONTROL'S NODE: what the
// desktop's bar did to its commands is done to its node's children, the form so edited the editor's command.
///////////////////////////////////////////////////////////////////////////////

namespace {

// The designer's layer-menu ids (the desktop's): a node is either the bar or one command, so the two sets never meet.
enum {
	kMenuAddCommand = wxID_HIGHEST + 3200,
	kMenuCut, kMenuCopy, kMenuPaste, kMenuDelete, kMenuMoveUp, kMenuMoveDown, kMenuProps,
};

// A control's command bar in its node (ibValueWindowComposite::WriteData) — none: an empty node.
ibProtocolNode FindCommandBar(const ibProtocolNode& control)
{
	return control.FindChild(ibProtocol::ibForm::ibCommandBar::Layers).FindChild(ibProtocol::ibForm::ibCommandBar::CommandBar);
}

} // namespace

ibValueLayerObject::ibValueLayerObject(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long controlId)
	: m_formHandler(formHandler), m_controlId(controlId)
{
}

bool ibValueLayerObject::IsEditable() const
{
	return m_formHandler->IsEditable();
}

void ibValueLayerObject::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	m_formHandler->ModifyProperty(property, oldValue, newValue);
}

ibProtocolNode ibValueLayerObject::GetNode() const
{
	std::vector<ibProtocolNode> path;
	if (!FindPath(m_formHandler->m_formNode, m_controlId, path))
		return ibProtocolNode();
	return FindNode(path.back());
}

bool ibValueLayerObject::Fetch()
{
	if (!m_formHandler->FetchRows(m_controlId))
		return false;
	const auto found = m_formHandler->m_rows.find(m_controlId);
	if (found == m_formHandler->m_rows.end())
		return false;
	ibPropertyObject::Receive(FindRows(found->second));
	ibVisualEditorNotebook::ibVisualEditor::Receive(this, GetNode());
	return true;
}

ibProperty* ibValueLayerObject::CreateProperty(ibPropertyCategory* cat, ibDataNode&& row)
{
	return ibPropertyObject::CreateProperty<ibPropertySerializable>(cat, std::move(row));
}

void ibValueLayerObject::SetCommandItems(const std::vector<ibProtocolNode>& items, int selected) const
{
	if (!m_formHandler->IsEditable())
		return;

	// Once the menu is gone: the tree the menu stands on is built anew — and this holder may go with it.
	ibVisualEditorNotebook::ibVisualEditor* const formHandler = m_formHandler;
	const long long controlId = m_controlId;
	formHandler->CallAfter([formHandler, controlId, items, selected]() {
		ibProtocolNode edited = formHandler->m_formNode.Clone();
		std::vector<ibProtocolNode> path;
		if (!FindPath(edited, controlId, path))
			return;
		ibProtocolNode bar = FindCommandBar(path.back());
		if (!bar.IsNode())
			return;

		// Each command's name unique among the bar's — a new one's, a pasted copy's that is taken gets the first free
		// CommandN (the desktop's GenerateItemName).
		std::set<wxString> taken;
		std::vector<ibProtocolNode> named;
		for (const ibProtocolNode& item : items) {
			ibProtocolNode copy = item.Clone();
			wxString name = copy.GetString(ibProtocol::ibView::ibFormDesigner::Name);
			for (int n = 1; name.IsEmpty() || taken.count(name) > 0; n++)
				name = wxString::Format(wxT("Command%d"), n);
			copy.SetValue(ibProtocol::ibView::ibFormDesigner::Name, name);
			taken.insert(name);
			named.push_back(copy);
		}
		SetChildren(bar, named);

		ibProtocolNode request;
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Form));
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Form, edited);
		ibProtocolNode answer;
		if (formHandler->Execute(request, answer) && selected != wxNOT_FOUND)
			formHandler->m_objectTree->SelectCommandItem(controlId, static_cast<unsigned int>(selected));
	});
}

///////////////////////////////////////////////////////////////////////////////

ibProtocolNode ibValueCommandBar::FindNode(const ibProtocolNode& control) const
{
	return FindCommandBar(control);
}

ibProtocolNode ibValueCommandBar::FindRows(const ibProtocolNode& control) const
{
	return control.FindChild(ibProtocol::ibView::ibFormDesigner::CommandBar);
}

void ibValueCommandBar::PrepareDefaultMenu(wxMenu* menu) const
{
	menu->Append(kMenuAddCommand, _("Add command"));
	if (m_formHandler->PasteEntry(oes_clipboard_command_bar_item).IsNode()) {
		wxMenuItem* paste = menu->Append(kMenuPaste, _("Paste\tCtrl+V"));
		paste->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	}
}

void ibValueCommandBar::ExecuteMenu(int menuId)
{
	// A new command is the server's (CommandBarItem's Node — the desktop's AddCommandItem), a pasted one the clipboard's;
	// either is put last, and shown.
	ibProtocolNode added;
	if (menuId == kMenuAddCommand && m_formHandler->FetchRows(m_controlId)) {
		const auto found = m_formHandler->m_rows.find(m_controlId);
		if (found != m_formHandler->m_rows.end())
			added = found->second.FindChild(ibProtocol::ibView::ibFormDesigner::CommandBarItem).FindChild(ibProtocol::ibView::ibFormDesigner::Node);
	}
	else if (menuId == kMenuPaste) {
		added = m_formHandler->PasteEntry(oes_clipboard_command_bar_item);
	}
	if (!added.IsNode())
		return;
	std::vector<ibProtocolNode> items = GetNode().Children();
	items.push_back(added);
	SetCommandItems(items, static_cast<int>(items.size()) - 1);
}

///////////////////////////////////////////////////////////////////////////////

bool ibValueCommandBarItem::OnPropertyChanging(ibProperty* property, const wxVariant& newValue)
{
	// Name must stay unique among the bar's items.
	if (property->GetName() != ibProtocol::ibView::ibFormDesigner::Name)
		return true;
	std::vector<ibProtocolNode> path;
	if (!FindPath(m_formHandler->m_formNode, m_controlId, path))
		return true;
	const std::vector<ibProtocolNode> items = FindCommandBar(path.back()).Children();
	for (size_t idx = 0; idx < items.size(); idx++) {
		if (idx != m_index && items[idx].GetString(ibProtocol::ibView::ibFormDesigner::Name) == newValue.GetString())
			return false;
	}
	return true;
}

ibProtocolNode ibValueCommandBarItem::FindNode(const ibProtocolNode& control) const
{
	const std::vector<ibProtocolNode> items = FindCommandBar(control).Children();
	return m_index < items.size() ? items[m_index] : ibProtocolNode();
}

ibProtocolNode ibValueCommandBarItem::FindRows(const ibProtocolNode& control) const
{
	const wxString name = GetNode().GetString(ibProtocol::ibView::ibFormDesigner::Name);
	for (const ibProtocolNode& rows : control.FindChild(ibProtocol::ibView::ibFormDesigner::CommandBar).FindChild(ibProtocol::ibView::ibFormDesigner::Items).Children()) {
		if (rows.GetString(ibProtocol::ibView::ibFormDesigner::Name) == name)
			return rows;
	}
	return control.FindChild(ibProtocol::ibView::ibFormDesigner::CommandBarItem);
}

// Same items / icons / hotkeys as the control popup menu (ibVisualEditorItemPopupMenu).
void ibValueCommandBarItem::PrepareDefaultMenu(wxMenu* menu) const
{
	wxMenuItem* item = nullptr;
	item = menu->Append(kMenuCut,   _("Cut\tCtrl+X"));   item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_CUT, wxART_MENU));
	item = menu->Append(kMenuCopy,  _("Copy\tCtrl+C"));  item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_COPY, wxART_MENU));
	item = menu->Append(kMenuPaste, _("Paste\tCtrl+V")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	item->Enable(m_formHandler->PasteEntry(oes_clipboard_command_bar_item).IsNode());
	menu->AppendSeparator();
	item = menu->Append(kMenuDelete, _("Delete\tCtrl+D")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_MENU));
	menu->AppendSeparator();
	item = menu->Append(kMenuMoveUp,   _("Move Up\tAlt+Up"));    item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_MENU));
	item = menu->Append(kMenuMoveDown, _("Move Down\tAlt+Down")); item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_MENU));
	menu->AppendSeparator();
	item = menu->Append(kMenuProps, _("Properties"));           item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_HELP_SETTINGS, wxART_MENU));
}

void ibValueCommandBarItem::ExecuteMenu(int menuId)
{
	std::vector<ibProtocolNode> path;
	if (!FindPath(m_formHandler->m_formNode, m_controlId, path))
		return;
	std::vector<ibProtocolNode> items = FindCommandBar(path.back()).Children();
	if (m_index >= items.size())
		return;

	switch (menuId) {
	case kMenuCopy:
		m_formHandler->CopyEntry(items[m_index], oes_clipboard_command_bar_item, items[m_index].GetString(ibProtocol::ibView::ibFormDesigner::Name));
		break;
	case kMenuCut:
		m_formHandler->CopyEntry(items[m_index], oes_clipboard_command_bar_item, items[m_index].GetString(ibProtocol::ibView::ibFormDesigner::Name));
		items.erase(items.begin() + m_index);
		SetCommandItems(items, wxNOT_FOUND);
		break;
	case kMenuPaste: {
		const ibProtocolNode pasted = m_formHandler->PasteEntry(oes_clipboard_command_bar_item);
		if (pasted.IsNode()) {
			items.push_back(pasted);
			SetCommandItems(items, static_cast<int>(items.size()) - 1);
		}
		break;
	}
	case kMenuDelete:
		items.erase(items.begin() + m_index);
		SetCommandItems(items, wxNOT_FOUND);
		break;
	case kMenuMoveUp:
		if (m_index > 0) {
			std::swap(items[m_index], items[m_index - 1]);
			SetCommandItems(items, static_cast<int>(m_index) - 1);
		}
		break;
	case kMenuMoveDown:
		if (m_index + 1 < items.size()) {
			std::swap(items[m_index], items[m_index + 1]);
			SetCommandItems(items, static_cast<int>(m_index) + 1);
		}
		break;
	case kMenuProps:
		// The EXPLICIT "Properties": the inspector raised, the command shown in it.
		if (!objectInspector->IsShownInspector())
			objectInspector->ShowInspector();
		m_formHandler->m_objectTree->SelectCommandItem(this);
		break;
	}
}
