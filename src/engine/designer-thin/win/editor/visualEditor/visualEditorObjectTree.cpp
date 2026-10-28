////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : visual editor
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"   // ibFormDragItem — a row dropped on a node
#include "designerTreeCtrl.h"    // focus-safe DeleteAllItems (our wxTreeCtrl projection)

#include "core/diagnostics/journal.h"

#include "core/picturePredefined.h"   // g_picCommandBarCLSID — the command bar's node
#include "frmclient/artProvider/artProvider.h"
#include "frmclient/backend/backend_picture.h"   // ibBackendPicture::GetPicture — an engine's picture
#include "frmclient/mainFrame/objinspect/objinspect.h"
#include "frmclient/win/picture.h"   // ibProtocolPicture — a class's picture, as the answer brought it

#include <wx/imaglist.h>

namespace {

// The form as the tree shows it — each control, its class and name, nested as they are. The control is said by its
// address too: one built anew for the same node is another the tree must not keep.
void ShapeOf(ibValueFrame* obj, wxString& shape)
{
	shape << wxString::Format(wxT("%p"), static_cast<const void*>(obj)) << wxT(':') << obj->GetClassName() << wxT(':')
		<< obj->GetControlName() << wxT('[');
	for (unsigned int i = 0; i < obj->GetChildCount(); i++)
		ShapeOf(obj->GetChild(i), shape);
	shape << wxT(']');
}

// The desktop's HasCommandBar: the server's state of a control carries its bar only when it has one.
bool HasCommandBar(const ibProtocolNode& node)
{
	return node.FindChild(ibProtocol::ibForm::ibState::State).Has(ibProtocol::ibForm::ibCommandBar::CommandBar);
}

// …and the commands of each control's bar, as its node has them: one added, removed, moved or renamed is drawn too.
void ShapeOf(const ibProtocolNode& node, wxString& shape)
{
	if (HasCommandBar(node)) {
		shape << node.GetInt(ibProtocol::ibForm::ControlId) << wxT('{');
		for (const ibProtocolNode& item : node.FindChild(ibProtocol::ibForm::ibCommandBar::Layers).FindChild(ibProtocol::ibForm::ibCommandBar::CommandBar).Children())
			shape << item.GetString(ibProtocol::ibForm::Name) << wxT(';');
		shape << wxT('}');
	}
	for (const ibProtocolNode& child : node.Children())
		ShapeOf(child, shape);
}

// The desktop's CanDeleteControl (frmserver ibValueForm, ibValueNotebookPage, ibValueModelTableBoxColumn): never the
// form, nor the last page of a notebook, nor the last column of a table.
bool CanDeleteControl(const ibValueFrame* obj)
{
	if (obj == nullptr || obj->GetParent() == nullptr)
		return false;
	if (obj->GetClassName() == ibProtocol::ibForm::ibType::NotebookPage || obj->GetClassName() == ibProtocol::ibForm::ibType::TableboxColumn)
		return obj->GetParent()->GetChildCount() > 1;
	return true;
}

// The size of a picture in the tree.
const wxSize kPictureSize(16, 16);

// The popup menu's first id of the classes a control adds in itself; the clipboard, delete, move and properties go by
// wx's own ids.
constexpr int kCreateControlId = wxID_HIGHEST + 2050;

} // namespace

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::RebuildTree()
{
	wxWindow::Freeze();
	// BLOCK the tree's events for the churn: DeleteAllItems / SelectItem otherwise fire native selection + focus
	// events, and the app's OnSetFocus follows that focus and switches the active MDI tab — a background rebuild
	// would yank the tab from the editor the user is in. Re-enabled before Thaw, after the row is restored.
	m_tree->SetEvtHandlerEnabled(false);
	m_expanding = true;

	// Remember the selected LAYER by identity (a command bar / a command — its holder outlives the rebuild): without
	// this the DeleteAllItems below drops the highlight onto the root form. A control is lit again by the selection the
	// command left (OnObjectSelected).
	const ibVisualEditorObjectTreeItemData* const selected = GetItemData(m_tree->GetSelection());
	const ibValueLayerObject* const keepLayer = selected != nullptr ? selected->GetLayerObject() : nullptr;

	m_tree->DeleteAllItems();
	m_controlItems.clear();
	m_layerItemMap.clear();

	if (ibValueFrame* const valueForm = m_formHandler->GetValueForm()) {
		AddControl(valueForm, m_formHandler->m_formNode, wxTreeItemId());

		// Restore the row highlight by identity — under m_selecting so SelectItem sets the bold mark WITHOUT re-pushing
		// the inspector (OnSelChanged bolds before the guard early-out), matching OnObjectSelected.
		const auto kept = keepLayer != nullptr ? m_layerItemMap.find(keepLayer) : m_layerItemMap.end();
		if (kept != m_layerItemMap.end()) {
			m_selecting = true;
			m_tree->SelectItem(kept->second);
			m_selecting = false;
		}
	}

	// …and a layer's holder whose node is gone goes too — let go of first where it is shown.
	const auto gone = [this](const ibValueLayerObject* layer) {
		if (m_layerItemMap.find(layer) != m_layerItemMap.end())
			return false;
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == layer)
			objectInspector->SelectObject(nullptr);
		return true;
	};
	for (auto it = m_commandBars.begin(); it != m_commandBars.end(); )
		it = gone(it->second.get()) ? m_commandBars.erase(it) : std::next(it);
	for (auto it = m_commandBarItems.begin(); it != m_commandBarItems.end(); )
		it = gone(it->second.get()) ? m_commandBarItems.erase(it) : std::next(it);

	m_expanding = false;
	m_tree->SetEvtHandlerEnabled(true);   // churn done — resume normal event handling

	wxWindow::Thaw();

	wxWindow::Refresh();
	wxWindow::Update();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddControl(ibValueFrame* control,
	const ibProtocolNode& node, const wxTreeItemId& parent)
{
	const std::vector<ibProtocolNode> children = node.Children();
	const auto childNode = [&children](unsigned int idx) { return idx < children.size() ? children[idx] : ibProtocolNode(); };

	if (control->GetComponentType() == COMPONENT_TYPE_SIZERITEM) {
		if (control->GetChildCount() > 0) {
			AddControl(control->GetChild(0), childNode(0), parent);
			return;
		}
		// An item holds its control, always: an empty one is a form built wrong — said, and skipped.
		ibValueFrame* const holder = control->GetParent();
		ibJournalError(wxT("designer"), wxString::Format(wxT("A sizer item of '%s:%s' holds no control"),
			holder != nullptr ? holder->GetControlName() : wxString(),
			holder != nullptr ? holder->GetClassName() : ibString()));
		return;
	}

	const int picture = GetPictureIndex(control->GetClassName());
	ibVisualEditorObjectTreeItemData* const data = new ibVisualEditorObjectTreeItemData(control);
	const wxTreeItemId row = parent.IsOk()
		? m_tree->AppendItem(parent, wxEmptyString, picture, wxNOT_FOUND, data)
		: m_tree->AddRoot(wxEmptyString, picture, wxNOT_FOUND, data);
	m_controlItems[control] = row;
	UpdateItem(row, control);

	// Command interface (a chrome LAYER the frame carries): a static sub-node added FIRST, so it sits above the content
	// like the rendered toolbar.
	AddCommandBar(control, node, row);

	for (unsigned int i = 0; i < control->GetChildCount(); i++)
		AddControl(control->GetChild(i), childNode(i), row);

	// Open as the form keeps it — once the rows under it are there to show.
	if (control->GetExpanded())
		m_tree->Expand(row);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::AddCommandBar(ibValueFrame* obj,
	const ibProtocolNode& node, const wxTreeItemId& parent)
{
	if (!HasCommandBar(node))
		return;

	const long long controlId = obj->GetControlID();
	// Selecting it edits the bar's settings (AutoFill) — what it holds received as it is shown. Icon = the toolbar's.
	std::unique_ptr<ibValueCommandBar>& bar = m_commandBars[controlId];
	if (bar == nullptr)
		bar.reset(new ibValueCommandBar(m_formHandler, controlId));
	const wxTreeItemId barNode = m_tree->AppendItem(parent, _("Command interface"),
		GetPictureIndex(ibProtocol::ibView::ibFormDesigner::CommandBar), wxNOT_FOUND, new ibVisualEditorObjectTreeItemData(bar.get()));
	m_layerItemMap[bar.get()] = barNode;   // the bar node — re-selectable by identity after a rebuild

	// Its commands as sub-nodes — remembered too, so a just-added / moved command can be re-selected after the rebuild.
	const std::vector<ibProtocolNode> items = bar->GetNode().Children();
	for (unsigned int idx = 0; idx < items.size(); idx++) {
		std::unique_ptr<ibValueCommandBarItem>& item = m_commandBarItems[{ controlId, idx }];
		if (item == nullptr)
			item.reset(new ibValueCommandBarItem(m_formHandler, controlId, idx));
		const wxTreeItemId itemNode = m_tree->AppendItem(barNode, items[idx].GetString(ibProtocol::ibView::ibFormDesigner::Name),
			GetPictureIndex(ibProtocol::ibView::ibFormDesigner::CommandBarItem), wxNOT_FOUND, new ibVisualEditorObjectTreeItemData(item.get()));
		m_layerItemMap[item.get()] = itemNode;
	}

	// Open, so a freshly added command is visible without a manual expand (the desktop's default) — unless it was closed.
	if (!items.empty() && bar->IsTreeExpanded())
		m_tree->Expand(barNode);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::UpdateItem(const wxTreeItemId& row, ibValueFrame* control)
{
	m_tree->SetItemText(row, control->GetControlName() + wxT(" : ") + control->GetClassName());

	// The editor's selection — shown, not told back to the editor.
	if (m_formHandler == nullptr || control != m_formHandler->GetSelectedObject())
		return;
	m_selecting = true;
	m_tree->EnsureVisible(row);
	m_tree->SelectItem(row);
	m_tree->SetItemBold(row);
	m_selecting = false;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::CreateTree(const ibProtocolNode& classes)
{
	wxImageList* const pictures = new wxImageList(kPictureSize.x, kPictureSize.y);
	m_pictureIndex.clear();

	const auto add = [this, pictures](const wxString& name, wxBitmap picture) {
		if (!picture.IsOk())
			return;
		if (picture.GetSize() != kPictureSize)
			picture = wxBitmap(picture.ConvertToImage().Rescale(kPictureSize.x, kPictureSize.y, wxIMAGE_QUALITY_HIGH));
		const int index = pictures->Add(picture);
		if (index != wxNOT_FOUND)
			m_pictureIndex.emplace(name, index);
	};

	for (const ibProtocolNode& described : classes.Children())
		add(described.GetString(ibProtocol::ibView::ibFormDesigner::Name), ibProtocolPicture(described, ibProtocol::ibView::ibFormDesigner::Picture));

	// The command bar and its command — the engine's pictures, not a class the server describes (the desktop's were its
	// toolbar's and tool's).
	add(ibProtocol::ibView::ibFormDesigner::CommandBar, ibBackendPicture::GetPicture(g_picCommandBarCLSID));
	add(ibProtocol::ibView::ibFormDesigner::CommandBarItem, ibBackendPicture::GetPicture(g_picCommandBarItemCLSID));

	// The tree owns the list, and lets go of the one it had.
	m_tree->AssignImageList(pictures);
}

int ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::GetPictureIndex(const wxString& className) const
{
	const auto found = m_pictureIndex.find(className);
	return found != m_pictureIndex.end() ? found->second : wxNOT_FOUND;
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTreeItemData* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::GetItemData(const wxTreeItemId& row) const
{
	return row.IsOk() ? static_cast<ibVisualEditorObjectTreeItemData*>(m_tree->GetItemData(row)) : nullptr;
}

ibValueFrame* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::GetControl(const wxTreeItemId& row) const
{
	const ibVisualEditorObjectTreeItemData* const data = GetItemData(row);
	return data != nullptr ? data->GetObject() : nullptr;
}

/////////////////////////////////////////////////////////////////////////////
// The editor's notifications
/////////////////////////////////////////////////////////////////////////////

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorLoaded()
{
	m_shape.clear();
	OnEditorRefresh();
}

// THE FORM DRAWN AGAIN — the tree rebuilt only when what it shows changed (a control added, removed, moved or renamed, a
// command of a bar); a control the host makes anew where it lays it in is another, so the map is rebuilt with them.
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEditorRefresh()
{
	wxString shape;
	if (ibValueFrame* const valueForm = m_formHandler->GetValueForm())
		ShapeOf(valueForm, shape);
	ShapeOf(m_formHandler->m_formNode, shape);

	if (shape == m_shape && !m_controlItems.empty()) {
		// …open or closed as the form has it now — an expansion undone or redone leaves the shape as it was.
		m_expanding = true;
		for (const auto& listed : m_controlItems)
			OnObjectExpanded(listed.first);
		m_expanding = false;
		return;
	}

	m_shape = shape;
	RebuildTree();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectSelected(ibValueFrame* obj)
{
	// The editor's selection, shown: its row selected and scrolled to — opening the rows above it is no expansion of the form.
	const auto found = m_controlItems.find(obj);
	if (found == m_controlItems.end())
		return;

	m_selecting = m_expanding = true;
	m_tree->EnsureVisible(found->second);
	m_tree->SelectItem(found->second);
	m_selecting = m_expanding = false;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnObjectExpanded(ibValueFrame* obj)
{
	const auto found = m_controlItems.find(obj);
	if (found == m_controlItems.end() || m_tree->IsExpanded(found->second) == obj->GetExpanded())
		return;

	if (obj->GetExpanded())
		m_tree->Expand(found->second);
	else
		m_tree->Collapse(found->second);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnPropertyModified(ibProperty* prop)
{
	ibValueFrame* const control = dynamic_cast<ibValueFrame*>(prop->GetPropertyObject());
	const auto found = m_controlItems.find(control);
	if (found != m_controlItems.end())
		UpdateItem(found->second, control);
}

ibValueLayerObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::FindEntry(const ibPropertyObject* owner) const
{
	for (const auto& held : m_commandBars) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	for (const auto& held : m_commandBarItems) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	return nullptr;
}

ibValueLayerObject* ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::FindEntry(long long controlId) const
{
	const auto found = m_commandBars.find(controlId);
	return found != m_commandBars.end() ? found->second.get() : nullptr;
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectCommandItem(const ibValueLayerObject* layer)
{
	const auto found = m_layerItemMap.find(layer);
	if (found == m_layerItemMap.end() || !found->second.IsOk())
		return;

	m_selecting = true;
	m_tree->EnsureVisible(found->second);   // expands ancestor nodes so the item shows
	m_tree->SelectItem(found->second);
	m_selecting = false;

	// …and the inspector shows it — also when it was the selected node already.
	SelectItemData(GetItemData(found->second));
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectCommandItem(long long controlId, unsigned int index)
{
	const auto found = m_commandBarItems.find({ controlId, index });
	if (found != m_commandBarItems.end())
		SelectCommandItem(found->second.get());
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::ibVisualEditorObjectTree(ibVisualEditor* handler, wxWindow* parent, int id) :
	wxPanel(parent, id),
	m_formHandler(handler)
{
	m_tree = new ibDesignerTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_NO_LINES | wxSIMPLE_BORDER | wxTR_TWIST_BUTTONS);
	m_tree->SetDoubleBuffered(true);

	wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tree, 1, wxEXPAND);
	SetSizerAndFit(sizer);

	// Accept any registered drag KIND dropped ONTO a tree node: a source path → a bound control under that node
	// (its container), a command → a command button on the nearest bar up from it. ONE resolver for every kind:
	// hit-test the drop point to the tree item → its owning frame (a layer node resolves to its owner frame; an
	// empty area → null, which the source's CreateBoundControl falls back to the form root).
	m_tree->SetDropTarget(
		new ibSourceDragDropTarget(
			[this](wxCoord x, wxCoord y) -> ibValueFrame* {
				m_formHandler->SetPendingDropBar(0);   // no command-bar toolbar in the object tree — clear the
				                                       // per-drop channel so a command drop here can't read a stale bar
				int flags = 0;
				const ibVisualEditorObjectTreeItemData* const data = GetItemData(m_tree->HitTest(wxPoint(x, y), flags));
				if (data == nullptr)
					return nullptr;
				ibValueFrame* target = data->GetObject();
				ibValueForm* const valueForm = m_formHandler->GetValueForm();
				if (target == nullptr && valueForm != nullptr)
					if (ibValueLayerObject* const layer = data->GetLayerObject())
						target = layer->GetControlID() == valueForm->GetControlID() ?
							valueForm : valueForm->FindControlByID(layer->GetControlID());
				return target;
			},
			[this](const ibFormDragItem& item, ibValueFrame* target) { item.ApplyDrop(m_formHandler, target); }));

	// The tree's events, as they come up to the panel.
	Bind(wxEVT_TREE_SEL_CHANGED, &ibVisualEditorObjectTree::OnSelChanged, this);
	Bind(wxEVT_TREE_ITEM_RIGHT_CLICK, &ibVisualEditorObjectTree::OnRightClick, this);
	Bind(wxEVT_TREE_KEY_DOWN, &ibVisualEditorObjectTree::OnKeyDown, this);
	Bind(wxEVT_TREE_BEGIN_DRAG, &ibVisualEditorObjectTree::OnBeginDrag, this);
	Bind(wxEVT_TREE_END_DRAG, &ibVisualEditorObjectTree::OnEndDrag, this);
	Bind(wxEVT_TREE_ITEM_EXPANDED, &ibVisualEditorObjectTree::OnExpansionChange, this);
	Bind(wxEVT_TREE_ITEM_COLLAPSED, &ibVisualEditorObjectTree::OnExpansionChange, this);

	auto reselectOnClick = [this](wxMouseEvent& event) {
		const wxTreeItemId row = m_tree->HitTest(event.GetPosition());
		if (row.IsOk() && row == m_tree->GetSelection())
			SelectItemData(GetItemData(row));
		event.Skip();
	};
	m_tree->Bind(wxEVT_LEFT_DOWN, reselectOnClick);
	m_tree->Bind(wxEVT_RIGHT_DOWN, reselectOnClick);
	// ACTIVATING the tree (it gains focus) reveals its SELECTED node too — not only a click. So switching to the object
	// tree re-surfaces the current control / layer in the inspector (SelectObject early-outs if it is already current).
	m_tree->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
		event.Skip();
		SelectItemData(GetItemData(m_tree->GetSelection()));
	});
}

ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::~ibVisualEditorObjectTree()
{
	// The inspector lets go of a holder going with the tree — it is no property object's that clears it.
	const auto release = [](const ibValueLayerObject& layer) {
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == &layer)
			objectInspector->SelectObject(nullptr);
	};
	for (const auto& held : m_commandBars)
		release(*held.second);
	for (const auto& held : m_commandBarItems)
		release(*held.second);
}

// Decode a tree item's payload and select it — one place for the click-reselect and the
// selection-changed handlers. A layer node (bar/command) goes to the inspector; a control node
// goes to the visual editor (which also scrolls to it).
void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::SelectItemData(ibVisualEditorObjectTreeItemData* data)
{
	if (data == nullptr)
		return;
	if (ibValueLayerObject* const layer = data->GetLayerObject()) {
		m_formHandler->SetCurrentElement(layer);   // a tree click is SOFT — revealed in an open inspector, not popped open
		return;
	}
	ibValueFrame* const obj = data->GetObject();
	assert(obj);
	m_formHandler->SelectObject(obj, false, false);
	m_formHandler->ScrollToObject(obj);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnSelChanged(wxTreeEvent& event)
{
	// The selected row in bold — and its control or layer selected in the editor, unless the tree follows the editor.
	if (event.GetOldItem().IsOk())
		m_tree->SetItemBold(event.GetOldItem(), false);
	if (!event.GetItem().IsOk())
		return;
	m_tree->SetItemBold(event.GetItem());

	if (!m_selecting)
		SelectItemData(GetItemData(event.GetItem()));
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnRightClick(wxTreeEvent& event)
{
	ibVisualEditorObjectTreeItemData* const data = GetItemData(event.GetItem());
	if (data == nullptr)
		return;

	// Layer node (bar OR command) — the object fills its own menu and runs the choice; the tree never branches per kind.
	if (ibValueLayerObject* const layer = data->GetLayerObject()) {
		wxMenu menu;
		layer->PrepareDefaultMenu(&menu);
		const int chosen = GetPopupMenuSelectionFromUser(menu, event.GetPoint());
		if (chosen != wxID_NONE)
			layer->ExecuteMenu(chosen);
		return;
	}

	// A control's node — selected, and its menu over it: on the stack, PopupMenu takes no ownership and blocks until the
	// menu is dismissed.
	if (ibValueFrame* const control = data->GetObject()) {
		m_formHandler->SelectObject(control);
		ibVisualEditorItemPopupMenu menu(m_formHandler, this, control);
		menu.UpdateUI(&menu);
		PopupMenu(&menu, event.GetPoint());
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnKeyDown(wxTreeEvent& event)
{
	// Alt+Up / Alt+Down reorder the selected control among its siblings (GTK only — on MSVC the tree drives this
	// itself); not on a view-only form, the same gate as the MOVE menu and the drag. A layer node (command bar) or no
	// selection resolves to no control and moves nothing. Alt+Left/Right (move a control IN / OUT of a container) has no
	// editor primitive yet — it would be a real cross-platform reparent command with undo, not a keystroke.
#ifdef __WXGTK__
	const int key = event.GetKeyCode();
	if (event.GetKeyEvent().AltDown() && (key == WXK_UP || key == WXK_DOWN) && m_formHandler->IsEditable()) {
		if (ibValueFrame* const control = GetControl(m_tree->GetSelection())) {
			m_formHandler->MovePosition(control, key == WXK_DOWN);
			return;
		}
	}
#endif
	event.Skip();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnBeginDrag(wxTreeEvent& event)
{
	// A drag is cancelled unless allowed: not of the form itself, and not on a view-only form, whose layout is frozen —
	// the same gate as the menu's MOVE items.
	if (event.GetItem() == m_tree->GetRootItem() || !m_formHandler->IsEditable())
		return;

	m_dragged = event.GetItem();
	event.Allow();
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnEndDrag(wxTreeEvent& event)
{
	const wxTreeItemId source = m_dragged;
	m_dragged = wxTreeItemId();

	// Never into itself, nor into a row under it.
	for (wxTreeItemId row = event.GetItem(); row.IsOk(); row = m_tree->GetItemParent(row)) {
		if (row == source)
			return;
	}

	const ibValueFrame* const from = GetControl(source);
	const ibValueFrame* const to = GetControl(event.GetItem());
	if (from == nullptr || to == nullptr)
		return;

	// Done once the drag is over — and each step on the controls as the one before left them: a command draws the
	// form anew, the controls with it, so the target is found again by its id. Ctrl held copies; else the control
	// moves, and a paste refused puts the cut back.
	ibVisualEditor* const handler = m_formHandler;
	const bool copy = wxGetKeyState(WXK_CONTROL);
	const long long fromId = from->GetControlID(), toId = to->GetControlID();
	handler->CallAfter([handler, copy, fromId, toId]() {
		const auto find = [handler](long long id) -> ibValueFrame* {
			ibValueForm* const form = handler->GetValueForm();
			if (form == nullptr)
				return nullptr;
			return form->GetControlID() == id ? form : form->FindControlByID(id);
		};

		if (copy) {
			handler->CopyObject(find(fromId));
			handler->PasteObject(find(toId));
		}
		else {
			handler->CutObject(find(fromId));
			if (!handler->PasteObject(find(toId)))
				handler->Undo();
		}
	});
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorObjectTree::OnExpansionChange(wxTreeEvent& event)
{
	// Opened or closed by the person: a control's the form keeps (a command), a layer's its holder, so a rebuild opens
	// it again (a leaf: nothing to keep).
	const ibVisualEditorObjectTreeItemData* const data = GetItemData(event.GetItem());
	if (m_expanding || data == nullptr)
		return;

	const bool expanded = m_tree->IsExpanded(event.GetItem());
	if (ibValueFrame* const control = data->GetObject()) {
		m_expanding = true;
		m_formHandler->ExpandObject(control, expanded);
		m_expanding = false;
	}
	else if (ibValueLayerObject* const layer = data->GetLayerObject()) {
		layer->SetTreeExpanded(expanded);
	}
}

///////////////////////////////////////////////////////////////////////////////

// The control's own items first (a table's "Add column", a notebook's "Add page" — the desktop's PrepareDefaultMenu):
// its class's Menu in the tab's answer, each the class it adds and its title.
ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::ibVisualEditorItemPopupMenu(ibVisualEditor* handler, wxWindow* WXUNUSED(parent), ibValueFrame* obj)
	: wxMenu(), m_object(obj), m_formHandler(handler)
{
	wxMenuItem* item = nullptr;

	for (const ibProtocolNode& described : handler->m_classes.Children()) {
		if (described.GetString(ibProtocol::ibView::ibFormDesigner::Name) != obj->GetClassName())
			continue;
		for (const ibProtocolNode& added : described.FindChild(ibProtocol::ibView::ibFormDesigner::Menu).Children()) {
			const wxString className = added.GetString(ibProtocol::ibView::ibFormDesigner::Name);
			item = Append(kCreateControlId + static_cast<int>(m_classNames.size()), added.GetString(ibProtocol::ibView::ibFormDesigner::Title));
			for (const ibProtocolNode& made : handler->m_classes.Children()) {
				if (made.GetString(ibProtocol::ibView::ibFormDesigner::Name) != className)
					continue;
				const wxBitmap picture = ibProtocolPicture(made, ibProtocol::ibView::ibFormDesigner::Picture);
				if (picture.IsOk())
					item->SetBitmap(picture);
				break;
			}
			m_classNames.push_back(className);
		}
		break;
	}
	if (!m_classNames.empty())
		AppendSeparator();

	item = Append(wxID_CUT, _("Cut\tCtrl+X"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_CUT, wxART_MENU));
	item = Append(wxID_COPY, _("Copy\tCtrl+C"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_COPY, wxART_MENU));
	item = Append(wxID_PASTE, _("Paste\tCtrl+V"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PASTE, wxART_MENU));
	AppendSeparator();
	item = Append(wxID_DELETE, _("Delete\tCtrl+D"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_MENU));
	AppendSeparator();
	item = Append(wxID_UP, _("Move Up\tAlt+Up"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_UP, wxART_MENU));
	item = Append(wxID_DOWN, _("Move Down\tAlt+Down"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_GO_DOWN, wxART_MENU));
	AppendSeparator();
	item = Append(wxID_PROPERTIES, _("Properties"));
	item->SetBitmap(wxArtProvider::GetBitmapBundle(wxART_PROPERTY, wxART_SERVICE));

	Bind(wxEVT_MENU, &ibVisualEditorItemPopupMenu::OnMenuEvent, this);
	Bind(wxEVT_UPDATE_UI, &ibVisualEditorItemPopupMenu::OnUpdateEvent, this);
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnMenuEvent(wxCommandEvent& event)
{
	// Done once the menu is gone: a command draws the form anew, and the control's window the menu stands on with it —
	// the control is the selected one then (the click selected it).
	ibVisualEditor* const handler = m_formHandler;

	switch (event.GetId())
	{
	case wxID_CUT: handler->CallAfter([handler]() { handler->CutObject(handler->GetSelectedObject()); }); break;
	case wxID_COPY: handler->CallAfter([handler]() { handler->CopyObject(handler->GetSelectedObject()); }); break;
	case wxID_PASTE: handler->CallAfter([handler]() { handler->PasteObject(handler->GetSelectedObject()); }); break;
	case wxID_DELETE: handler->CallAfter([handler]() { handler->RemoveObject(handler->GetSelectedObject()); }); break;
	case wxID_UP: handler->CallAfter([handler]() { handler->MovePosition(handler->GetSelectedObject(), false); }); break;
	case wxID_DOWN: handler->CallAfter([handler]() { handler->MovePosition(handler->GetSelectedObject(), true); }); break;

	case wxID_PROPERTIES:
		// Explicit "Properties" — select the control (canvas highlight + scroll) then FORCE-open the inspector on it.
		m_formHandler->SelectObject(m_object, true);
		m_formHandler->ShowCurrentElement(true);
		break;

	default:
		if (event.GetId() >= kCreateControlId && event.GetId() < kCreateControlId + static_cast<int>(m_classNames.size())) {
			const wxString className = m_classNames[event.GetId() - kCreateControlId];
			handler->CallAfter([handler, className]() { handler->CreateObject(className); });
		}
		break;
	}
}

void ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorItemPopupMenu::OnUpdateEvent(wxUpdateUIEvent& event)
{
	// Nothing changes a view-only form; Properties only shows.
	const bool editable = m_formHandler->IsEditable();
	const int id = event.GetId();
	if (id == wxID_CUT || id == wxID_COPY || id == wxID_UP || id == wxID_DOWN)
		event.Enable(editable && m_formHandler->CanCopyObject());
	else if (id == wxID_DELETE)
		event.Enable(editable && m_formHandler->CanCopyObject() && CanDeleteControl(m_formHandler->GetSelectedObject()));
	else if (id == wxID_PASTE)
		event.Enable(editable && m_formHandler->CanPasteObject());
	else if (id >= kCreateControlId && id < kCreateControlId + static_cast<int>(m_classNames.size()))
		event.Enable(editable);
}
