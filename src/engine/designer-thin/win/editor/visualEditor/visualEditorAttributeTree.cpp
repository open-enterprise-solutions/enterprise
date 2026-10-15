////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the form source-attribute tree, its rows the server's
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"                                      // ibSourceDragItem — a row dragged onto the form
#include "designerTreeCtrl.h"                                          // focus-safe DeleteAllItems (our wxTreeCtrl projection)

#include "designer-thin/docManager/templates/docViewModuleEditor.h"    // ibModuleEditDocument — the tab the form is of

#include "frmclient/backend/backend_picture.h"                         // ibBackendPicture — the attribute's picture
#include "frmclient/backend/metaCollection/metaObject.h"               // g_metaAttributeCLSID — the attribute's picture
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/objinspect/objinspect.h"

#include <wx/artprov.h>
#include <wx/imaglist.h>
#include <wx/menu.h>

#include <set>

typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTree     ibAttributeTree;
typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTreeItemData ibAttributeTreeItemData;

enum {
	ID_ATTR_ADD = wxID_HIGHEST + 2100,
	ID_ATTR_ADD_COLUMN,
	ID_ATTR_EDIT,
	ID_ATTR_REMOVE,
	ID_ATTR_SETMAIN,
	ID_ATTR_PROPERTIES,
	ID_ATTR_COPY,
	ID_ATTR_CUT,
	ID_ATTR_PASTE,
};

namespace {

// A clsid as the wire carries it — 64 bits as text; none: 0.
ibClassID ClsidOf(const wxString& text)
{
	unsigned long long clsid = 0;
	return text.ToULongLong(&clsid) ? static_cast<ibClassID>(clsid) : 0;
}

// The rows' shape — each root row's id, name, and what it is drawn as: a draw that says the same leaves the tree alone.
// …and the form's attributes as it holds them: a row opened was asked of them then, so the same rows over attributes
// changed say another composition — a column given a reference type showed none to open (2026-10-15).
wxString ShapeOf(const ibProtocolNode& rows, const ibProtocolNode& attributes)
{
	wxString shape;
	for (const ibProtocolNode& row : rows.Children())
		shape << row.GetId() << wxT(':') << row.GetString(ibProtocolName::Name) << wxT(':')
			<< row.GetBool(ibProtocolName::Main) << row.GetBool(ibProtocolName::HasComposition)
			<< row.GetString(ibProtocolName::DropControl) << row.FindChild(ibProtocolName::Menu).IsNode() << wxT(';');
	shape << wxString::FromUTF8(attributes.Write());
	return shape;
}

// The item of that name of a row's menu — none: a node with nothing in it (no Name; IsNode all the same).
ibProtocolNode FindMenuItem(const ibProtocolNode& menu, const char* name)
{
	for (const ibProtocolNode& item : menu.Children()) {
		if (item.GetString(ibProtocolName::Name) == wxString(name))
			return item;
	}
	return ibProtocolNode();
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// ibFormAttributeValue
///////////////////////////////////////////////////////////////////////////////

ibFormAttributeValue::ibFormAttributeValue(ibVisualEditorNotebook::ibVisualEditor* formHandler, long long id,
	long long column)
	: m_formHandler(formHandler), m_attributeId(id), m_columnId(column)
{
}

bool ibFormAttributeValue::IsEditable() const
{
	return m_formHandler->IsEditable();
}

bool ibFormAttributeValue::Receive()
{
	ibProtocolNode request;
	request.SetValue(ibProtocolName::Kind, static_cast<int>(ibProtocolFormDesignerRequest::Properties));
	request.SetValue(ibProtocolName::Form, m_formHandler->m_formNode);
	request.SetValue(ibProtocolName::Attribute, m_attributeId);
	if (m_columnId != 0)
		request.SetValue(ibProtocolName::Column, m_columnId);
	ibProtocolNode answer;
	return m_formHandler->GetEditorDocument()->Fetch(request, answer) && ibPropertyObject::Receive(answer);
}

void ibFormAttributeValue::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	m_formHandler->ModifyProperty(property, oldValue, newValue);
}

void ibFormAttributeValue::ReadRow(const ibProtocolNode& row)
{
	m_name = row.GetString(ibProtocolName::Name);
	m_main = row.GetBool(ibProtocolName::Main);
	m_menu = row.FindChild(ibProtocolName::Menu).Clone();
}

///////////////////////////////////////////////////////////////////////////////
// ibVisualEditorAttributeTree
///////////////////////////////////////////////////////////////////////////////

wxBEGIN_EVENT_TABLE(ibAttributeTree, wxPanel)
	EVT_TREE_SEL_CHANGED(wxID_ANY, ibAttributeTree::OnSelChanged)
	EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibAttributeTree::OnActivated)
	EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibAttributeTree::OnItemExpanding)
	EVT_TREE_BEGIN_DRAG(wxID_ANY, ibAttributeTree::OnBeginDrag)
	EVT_CONTEXT_MENU(ibAttributeTree::OnContextMenu)
	EVT_TREE_END_LABEL_EDIT(wxID_ANY, ibAttributeTree::OnEndLabelEdit)
	EVT_MENU(ID_ATTR_ADD, ibAttributeTree::OnAdd)
	EVT_MENU(ID_ATTR_ADD_COLUMN, ibAttributeTree::OnAddColumn)
	EVT_MENU(ID_ATTR_EDIT, ibAttributeTree::OnEdit)
	EVT_MENU(ID_ATTR_REMOVE, ibAttributeTree::OnRemove)
	EVT_MENU(ID_ATTR_SETMAIN, ibAttributeTree::OnSetMain)
	EVT_MENU(ID_ATTR_PROPERTIES, ibAttributeTree::OnProperties)
	EVT_MENU(ID_ATTR_COPY, ibAttributeTree::OnCopy)
	EVT_MENU(ID_ATTR_CUT, ibAttributeTree::OnCut)
	EVT_MENU(ID_ATTR_PASTE, ibAttributeTree::OnPaste)
wxEND_EVENT_TABLE()

ibAttributeTree::ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	// No toolbar — every action lives on the context menu. wxTR_EDIT_LABELS enables the inline
	// rename used by "Edit".
	m_tcAttributes = new ibDesignerTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxTR_EDIT_LABELS);
	m_tcAttributes->SetDoubleBuffered(true);   // no flicker on a rebuild, like the object tree

	// Ctrl+C / Ctrl+X / Ctrl+V / Delete — via an accelerator table ON THE TREE (same pattern the metadata trees
	// and the visual-editor panel use). It must sit on the tree itself: the parent editor panel already maps
	// Ctrl+C→wxID_COPY (control copy), so a table closer in the focus chain is what routes the keys to the
	// attribute handlers instead. The IDs fire the same EVT_MENU handlers as the context menu.
	wxAcceleratorEntry accelEntries[4];
	accelEntries[0].Set(wxACCEL_CTRL,   (int)'C',    ID_ATTR_COPY);
	accelEntries[1].Set(wxACCEL_CTRL,   (int)'X',    ID_ATTR_CUT);
	accelEntries[2].Set(wxACCEL_CTRL,   (int)'V',    ID_ATTR_PASTE);
	accelEntries[3].Set(wxACCEL_NORMAL, WXK_DELETE,  ID_ATTR_REMOVE);
	m_tcAttributes->SetAcceleratorTable(wxAcceleratorTable(4, accelEntries));

	// Every entry carries the attribute's picture (index 0) — the one the server gave its class (the desktop's
	// ibValueMetaObjectAttribute::GetIconGroup).
	wxImageList* images = new wxImageList(16, 16);
	const wxBitmap attributePicture = ibBackendPicture::GetPicture(g_metaAttributeCLSID);
	if (attributePicture.IsOk())
		images->Add(attributePicture);
	m_tcAttributes->AssignImageList(images);

	// Single click always (re)selects — even the already-selected item — so ONE click surfaces the
	// attribute in the inspector (OnSelChanged fires only on CHANGE). Same as the object tree, which
	// routes its click-reselect through SelectItemData.
	m_tcAttributes->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
		const wxTreeItemId id = m_tcAttributes->HitTest(event.GetPosition());
		if (id.IsOk() && id == m_tcAttributes->GetSelection())
			if (!SelectColumnInInspector(id))          // a value-table column node re-surfaces its column
				SelectInInspector(GetEntryFromItem(id));   // else re-surface the already-selected attribute
		event.Skip();
	});

	// ACTIVATING the tree (it gains focus) reveals its SELECTED element too — not only a click. Soft: fills an open
	// inspector, never pops it open, never steals focus.
	m_tcAttributes->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
		event.Skip();
		const wxTreeItemId id = m_tcAttributes->GetSelection();
		if (id.IsOk() && !SelectColumnInInspector(id))
			SelectInInspector(GetEntryFromItem(id));
	});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcAttributes, 1, wxEXPAND);
	SetSizer(sizer);
}

ibAttributeTree::~ibVisualEditorAttributeTree()
{
	// The inspector lets go of a holder going with the tree — it is no property object's that clears it.
	for (const auto& held : m_entries) {
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == held.second.get())
			objectInspector->SelectObject(nullptr);
	}
	for (const auto& held : m_columns) {
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == held.second.get())
			objectInspector->SelectObject(nullptr);
	}
}

void ibAttributeTree::Draw(const ibProtocolNode& rows, const ibProtocolNode& attributes)
{
	const wxString shape = ShapeOf(rows, attributes);
	if (shape == m_shape)
		return;
	m_shape = shape;

	// THE HOLDERS — one per root row, kept by its id: the one the inspector shows survives the rebuild.
	std::set<long long> drawn;
	for (const ibProtocolNode& row : rows.Children()) {
		std::unique_ptr<ibFormAttributeValue>& held = m_entries[row.GetId()];
		if (held == nullptr)
			held.reset(new ibFormAttributeValue(m_formHandler, row.GetId()));
		held->ReadRow(row);
		drawn.insert(row.GetId());
	}

	m_rebuilding = true;        // suppress OnSelChanged while items churn
	m_tcAttributes->Freeze();   // no flicker while the tree is torn down and re-appended
	// BLOCK the tree's events for the whole churn: DeleteAllItems / SelectItem otherwise generate native selection +
	// focus events, and the app's OnSetFocus follows that focus and switches the active MDI tab — a background
	// rebuild would yank the tab from the editor the user is in. Re-enabled after the row is restored below.
	m_tcAttributes->SetEvtHandlerEnabled(false);
	m_tcAttributes->DeleteAllItems();
	AppendRows(m_tcAttributes->AddRoot(wxT("Attributes")), rows);
	m_tcAttributes->Thaw();

	// …and a holder whose row is gone goes too — let go of first where it is shown.
	for (auto it = m_entries.begin(); it != m_entries.end(); ) {
		if (drawn.find(it->first) != drawn.end()) {
			++it;
			continue;
		}
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == it->second.get())
			objectInspector->SelectObject(nullptr);
		it = m_entries.erase(it);
	}
	for (auto it = m_columns.begin(); it != m_columns.end(); ) {
		if (drawn.find(it->first.first) != drawn.end()) {
			++it;
			continue;
		}
		if (objectInspector != nullptr && objectInspector->GetSelectedObject() == it->second.get())
			objectInspector->SelectObject(nullptr);
		it = m_columns.erase(it);
	}

	SelectEntry(m_selectedId);
	m_tcAttributes->SetEvtHandlerEnabled(true);   // churn done, row restored — resume normal event handling
	m_rebuilding = false;
}

ibFormAttributeValue* ibAttributeTree::FindEntry(const ibPropertyObject* owner) const
{
	for (const auto& held : m_entries) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	for (const auto& held : m_columns) {
		if (held.second.get() == owner)
			return held.second.get();
	}
	return nullptr;
}

void ibAttributeTree::AppendRows(const wxTreeItemId& parent, const ibProtocolNode& rows)
{
	const int image = m_tcAttributes->GetImageList() != nullptr && m_tcAttributes->GetImageList()->GetImageCount() > 0 ? 0 : -1;
	for (const ibProtocolNode& row : rows.Children()) {
		std::vector<ibSourceHop> path;
		for (const ibProtocolNode& hop : row.GetList(ibProtocolName::Path)) {
			const ibClassID type = ClsidOf(hop.GetString(ibProtocolName::Type));
			path.push_back({ static_cast<ibSourceId>(hop.GetInt(ibProtocolName::Id)), type != 0 ? type : g_valueUndefinedCLSID });
		}
		std::vector<ibMetaID> refTypes;
		for (const ibProtocolNode& target : row.GetList(ibProtocolName::Targets))
			refTypes.push_back(static_cast<ibMetaID>(target.AsInt()));

		// A ROOT row: the holder + head-of-path; a composition row: its path alone.
		const auto found = row.GetId() != 0 ? m_entries.find(row.GetId()) : m_entries.end();
		ibFormAttributeValue* const entry = found != m_entries.end() ? found->second.get() : nullptr;

		ibAttributeTreeItemData* const itemData = new ibAttributeTreeItemData(entry, path, refTypes,
			row.GetBool(ibProtocolName::TableSection), ClsidOf(row.GetString(ibProtocolName::DropControl)),
			row.GetBool(ibProtocolName::UnderList));
		const wxTreeItemId item = m_tcAttributes->AppendItem(parent, row.GetString(ibProtocolName::Name), image, image, itemData);
		if (row.GetBool(ibProtocolName::Main))
			m_tcAttributes->SetItemBold(item);

		if (!row.Children().empty()) {
			// table section / nested composite: its columns came inline (the server built them in the same pass).
			AppendRows(item, row);
			itemData->SetLoaded();                  // already populated; OnItemExpanding no-ops on it
		}
		else if (row.GetBool(ibProtocolName::HasComposition)) {
			m_tcAttributes->AppendItem(item, wxEmptyString);   // dummy -> [+]; asked lazily on expand
		}
	}
}

ibFormAttributeValue* ibAttributeTree::GetEntryFromItem(const wxTreeItemId& item) const
{
	if (item.IsOk())
		if (wxTreeItemData* data = m_tcAttributes->GetItemData(item))
			return ((ibAttributeTreeItemData*)data)->GetEntry();
	return nullptr;
}

void ibAttributeTree::SelectEntry(long long id)
{
	if (id == 0 || m_tcAttributes == nullptr)
		return;
	const wxTreeItemId root = m_tcAttributes->GetRootItem();
	if (!root.IsOk())
		return;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId item = m_tcAttributes->GetFirstChild(root, cookie); item.IsOk();
		item = m_tcAttributes->GetNextChild(root, cookie)) {
		const ibFormAttributeValue* const entry = GetEntryFromItem(item);
		if (entry != nullptr && entry->GetId() == id) {
			// …the row lit, not said again: what selected it is the server's already.
			const bool rebuilding = m_rebuilding;
			m_rebuilding = true;
			m_tcAttributes->SelectItem(item);
			m_tcAttributes->EnsureVisible(item);
			m_rebuilding = rebuilding;
			return;
		}
	}
}

void ibAttributeTree::SelectInInspector(ibFormAttributeValue* entry)
{
	// The single decode-and-select point: the holder made the current element — soft: fills an open inspector, never pops
	// it open.
	if (entry == nullptr)
		return;
	m_selectedId = entry->GetId();
	m_formHandler->SetCurrentElement(entry);
}

bool ibAttributeTree::SelectColumnInInspector(const wxTreeItemId& item)
{
	// A value-table column node carries NO holder (GetEntry == null) and a path [attrId, columnId] — under an attribute
	// whose row adds columns (its Menu has Column): the column's holder the current element.
	if (!item.IsOk() || m_tcAttributes == nullptr)
		return false;
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(item);
	if (data == nullptr || data->GetEntry() != nullptr)   // a root holder is handled by SelectInInspector
		return false;
	const std::vector<ibSourceHop>& path = data->GetPath();
	if (path.size() < 2)                                   // [attrId, columnId] — shallower can't name a column
		return false;

	const long long attributeId = static_cast<long long>(path.front().m_id);
	const auto head = m_entries.find(attributeId);
	if (head == m_entries.end() || !FindMenuItem(head->second->GetMenu(), ibProtocolName::Column).Has(ibProtocolName::Name))
		return false;

	const long long columnId = static_cast<long long>(path.back().m_id);
	std::unique_ptr<ibFormAttributeValue>& held = m_columns[{ attributeId, columnId }];
	if (held == nullptr)
		held.reset(new ibFormAttributeValue(m_formHandler, attributeId, columnId));
	m_selectedId = attributeId;
	m_formHandler->SetCurrentElement(held.get());
	return true;
}

bool ibAttributeTree::SelectColumn(long long attributeId, long long columnId)
{
	const wxTreeItemId root = m_tcAttributes->GetRootItem();
	if (!root.IsOk())
		return false;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId attrItem = m_tcAttributes->GetFirstChild(root, cookie); attrItem.IsOk();
		attrItem = m_tcAttributes->GetNextChild(root, cookie)) {
		const ibFormAttributeValue* const entry = GetEntryFromItem(attrItem);
		if (entry == nullptr || entry->GetId() != attributeId)
			continue;
		m_tcAttributes->Expand(attrItem);   // OnItemExpanding asks for the column rows
		wxTreeItemIdValue childCookie;
		for (wxTreeItemId col = m_tcAttributes->GetFirstChild(attrItem, childCookie); col.IsOk();
			col = m_tcAttributes->GetNextChild(attrItem, childCookie)) {
			const ibAttributeTreeItemData* colData = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(col);
			if (colData == nullptr || colData->GetPath().empty() ||
				static_cast<long long>(colData->GetPath().back().m_id) != columnId)
				continue;
			// …the row lit, not said again: what selected it is the server's already.
			const bool rebuilding = m_rebuilding;
			m_rebuilding = true;
			m_tcAttributes->SelectItem(col);
			m_tcAttributes->EnsureVisible(col);
			m_rebuilding = rebuilding;
			return true;
		}
		return false;
	}
	return false;
}

void ibAttributeTree::OnSelChanged(wxTreeEvent& event)
{
	if (m_rebuilding)
		return;   // mid-rebuild: not the person's selection
	if (!SelectColumnInInspector(event.GetItem()))   // a value-table column node? select its column
		SelectInInspector(GetEntryFromItem(event.GetItem()));
}

void ibAttributeTree::OnContextMenu(wxContextMenuEvent& event)
{
	// Fires on an item OR empty space — so Add / Paste stay reachable even with no attributes
	// and no selection (the old item-only menu showed nothing on an empty tree).
	const wxPoint pos = event.GetPosition();
	if (pos != wxDefaultPosition) {
		int flags = 0;
		const wxTreeItemId hit = m_tcAttributes->HitTest(m_tcAttributes->ScreenToClient(pos), flags);
		if (hit.IsOk())
			m_tcAttributes->SelectItem(hit);
	}
	// A composition node (a field / section, expanded lazily) carries no holder → attribute actions
	// gate on hasAttr, not mere selection; only Add / Paste stay available on it.
	ibFormAttributeValue* selEntry = GetEntryFromItem(m_tcAttributes->GetSelection());
	const bool hasAttr = selEntry != nullptr;
	const bool isMain = selEntry != nullptr && selEntry->IsMain();
	// A value-table attribute grows COLUMNS on the attribute side (edited here; the bound tablebox then shows them) —
	// its row's menu says so, and its title.
	const ibProtocolNode addColumn = selEntry != nullptr ?
		FindMenuItem(selEntry->GetMenu(), ibProtocolName::Column) : ibProtocolNode();
	// A LOCKED form (over read-only metadata — a config opened from a DB / file) can be VIEWED but not
	// restructured: every MUTATING action is disabled; the read-only-safe ones (Copy, Properties) stay live.
	const bool editable = m_formHandler->IsEditable();

	wxMenu menu;
	auto appendItem = [&menu](int menuId, const wxString& label, const wxArtID& art, bool enabled) {
		wxMenuItem* item = new wxMenuItem(&menu, menuId, label);
		item->SetBitmap(wxArtProvider::GetBitmap(art, wxART_MENU));
		menu.Append(item);
		item->Enable(enabled);
	};

	appendItem(ID_ATTR_ADD, _("Add"), wxART_NEW, editable);          // mutates → locked when read-only
	if (addColumn.Has(ibProtocolName::Name))
		appendItem(ID_ATTR_ADD_COLUMN, addColumn.GetString(ibProtocolName::Title), wxART_NEW, editable);   // value-table only
	appendItem(ID_ATTR_EDIT, _("Edit"), wxART_EDIT, hasAttr && editable);
	appendItem(ID_ATTR_REMOVE, _("Delete"), wxART_DELETE, hasAttr && editable);
	menu.AppendSeparator();
	appendItem(ID_ATTR_CUT, _("Cut"), wxART_CUT, hasAttr && editable);
	appendItem(ID_ATTR_COPY, _("Copy"), wxART_COPY, hasAttr);        // read-only safe
	appendItem(ID_ATTR_PASTE, _("Paste"), wxART_PASTE, editable && m_formHandler->CanPasteAttribute());   // grey when clipboard empty
	menu.AppendSeparator();
	appendItem(ID_ATTR_SETMAIN, isMain ? _("Unset main") : _("Set as main"), wxART_TICK_MARK, hasAttr && editable);
	appendItem(ID_ATTR_PROPERTIES, _("Properties"), wxART_LIST_VIEW, hasAttr);   // read-only safe

	PopupMenu(&menu);
}

void ibAttributeTree::OnActivated(wxTreeEvent& event)
{
	// Double-click / Enter on an item → activate its properties in the inspector, even when it is
	// already the selected item (OnSelChanged would not re-fire). "Activate the property."
	if (!SelectColumnInInspector(event.GetItem()))   // a value-table column node activates its column
		SelectInInspector(GetEntryFromItem(event.GetItem()));
}

void ibAttributeTree::OnItemExpanding(wxTreeEvent& event)
{
	// Lazy composition build — a node ships with a single dummy [+]; its real children are the server's answer, asked
	// HERE, once (Composition): the row as it was written is what it is asked with, and the form as it stands. A node
	// already filled inline (a table section) is marked loaded and no-ops here.
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(event.GetItem());
	if (data == nullptr || data->IsLoaded())
		return;
	data->SetLoaded();

	ibProtocolNode request;
	request.SetValue(ibProtocolName::Kind, static_cast<int>(ibProtocolFormDesignerRequest::Composition));
	request.SetValue(ibProtocolName::Form, m_formHandler->m_formNode);
	ibVisualEditor::WritePath(request, data->GetPath());
	for (const ibMetaID& target : data->GetRefTypes())
		request.AddItem(ibProtocolName::Targets, static_cast<long long>(target));
	if (data->IsUnderList())
		request.SetValue(ibProtocolName::UnderList, true);

	m_tcAttributes->DeleteChildren(event.GetItem());   // drop the dummy [+]
	ibProtocolNode answer;
	if (m_formHandler->GetEditorDocument()->Fetch(request, answer))
		AppendRows(event.GetItem(), answer.FindChild(ibProtocolName::Rows));
}

void ibAttributeTree::OnBeginDrag(wxTreeEvent& event)
{
	// Drag a node onto the form canvas. Payload = the binding PATH (head = attribute id), serialized by the ENGINE's
	// source-description serializer (ibSourceDescriptionMemory), raw ids; the drop says it to the server, which resolves
	// the control class from the type AT the path (ibVisualEditorHost::CreateBoundControl). A node with no mapped control
	// (an object container) is not draggable — the row's DropControl.
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(event.GetItem());
	if (data == nullptr || data->GetDropControl() == 0)
		return;

	// Dropping a node onto the form CREATES a bound control — a form edit; blocked on a view-only form (leaving the
	// drag unstarted, so nothing follows the cursor).
	if (!m_formHandler->IsEditable())
		return;

	ibSourceDescription srcDesc;   // build from the HOP path — reference hops carry the pinned branch type (composite)
	for (const ibSourceHop& hop : data->GetPath())
		srcDesc.AppendSource(hop.m_id, hop.m_type);
	ibSourceDragItem(m_tcAttributes, srcDesc);   // construction IS the drag
}

void ibAttributeTree::OnAdd(wxCommandEvent& WXUNUSED(event))
{
	// A new attribute is never empty: a UNIQUE name and a default Type (String) — the server's editor makes it, through
	// its command processor (undoable); the tree lands on it and shows it, as the desktop's OnAdd (SelectEntry).
	const auto found = m_entries.find(m_formHandler->CreateAttribute());
	if (found == m_entries.end())
		return;
	SelectEntry(found->first);
	SelectInInspector(found->second.get());
}

void ibAttributeTree::OnAddColumn(wxCommandEvent& WXUNUSED(event))
{
	// A column of the selected VALUE-TABLE attribute — a UNIQUE name (Column, Column1, …) and a default Type (String),
	// made by the server's editor; the tree lands on it and shows it, as the desktop's OnAddColumn.
	ibFormAttributeValue* const entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (entry == nullptr)
		return;
	const long long id = entry->GetId();
	const long long column = m_formHandler->AddColumn(entry);
	if (column == 0)
		return;
	// The tree built anew with the attributes changed (Draw) — the attribute opened, its columns asked with this one.
	SelectColumn(id, column);
	SelectColumnInInspector(m_tcAttributes->GetSelection());
}

void ibAttributeTree::OnEdit(wxCommandEvent& WXUNUSED(event))
{
	// "Edit" = rename inline; OnEndLabelEdit hands the name to the server, which takes it or refuses it.
	const wxTreeItemId sel = m_tcAttributes->GetSelection();
	if (sel.IsOk() && GetEntryFromItem(sel) != nullptr)
		m_tcAttributes->EditLabel(sel);
}

void ibAttributeTree::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled())
		return;

	ibFormAttributeValue* entry = GetEntryFromItem(event.GetItem());
	if (entry == nullptr) {
		event.Veto();
		return;
	}

	// THE ROWS BRING THE NAME BACK — the label edit is let go of first, and the name said after it: the answer builds
	// the tree anew. Refused (empty, another's), the old name stays.
	event.Veto();
	const long long id = entry->GetId();
	const wxString newName = event.GetLabel();
	CallAfter([this, id, newName]() {
		const auto found = m_entries.find(id);
		if (found != m_entries.end())
			m_formHandler->RenameAttribute(found->second.get(), newName);
	});
}

void ibAttributeTree::OnRemove(wxCommandEvent& WXUNUSED(event))
{
	// Through the server's command processor (undoable): the rows lose it, and controls bound to it render broken/empty.
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		m_formHandler->RemoveAttribute(entry);
}

void ibAttributeTree::OnSetMain(wxCommandEvent& WXUNUSED(event))
{
	ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (entry == nullptr)
		return;

	// Toggle: clicking on the CURRENT main clears it (no main → its controls get their own command
	// bar back); otherwise make it main. The server keeps the sole-main invariant.
	m_formHandler->SetMainAttribute(entry->IsMain() ? nullptr : entry);
}

void ibAttributeTree::OnProperties(wxCommandEvent& WXUNUSED(event))
{
	// The EXPLICIT "Properties": the inspector raised, the attribute shown in it.
	if (mainFrame != nullptr)
		mainFrame->ShowInspector();
	SelectInInspector(GetEntryFromItem(m_tcAttributes->GetSelection()));
}

void ibAttributeTree::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	// The copy is written on the server (serialization-based, own format id) and put on this computer's clipboard.
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		m_formHandler->CopyAttribute(entry);
}

void ibAttributeTree::OnCut(wxCommandEvent& WXUNUSED(event))
{
	// Cut = copy to the clipboard, then remove through the command processor (the removal is undoable).
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		m_formHandler->CutAttribute(entry);
}

void ibAttributeTree::OnPaste(wxCommandEvent& WXUNUSED(event))
{
	// The server pastes it (a fresh, non-main, uniquely named one); the tree lands on it and shows it, as the desktop's
	// OnPaste (SelectEntry).
	const auto found = m_entries.find(m_formHandler->PasteAttribute());
	if (found == m_entries.end())
		return;
	SelectEntry(found->first);
	SelectInInspector(found->second.get());
}
