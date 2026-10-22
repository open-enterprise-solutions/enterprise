////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the form source-attribute tree, its rows the server's
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "visualEditorDragItem.h"                                      // ibSourceDragItem — a row dragged onto the form
#include "designerTreeCtrl.h"                                          // focus-safe DeleteAllItems (our wxTreeCtrl projection)

#include "designer-thin/docManager/templates/docViewModuleEditor.h"    // ibModuleEditDocument — the tab the form is of

#include "frmclient/backend/backend_picture.h"                         // ibBackendPicture — the attribute's picture
#include "frmclient/backend/metaCollection/metaObject.h"               // g_metaAttributeCLSID — the attribute's picture
#include "frmclient/backend/propertyManager/property/propertySerializable.h"   // its properties — a row's each
#include "frmclient/mainFrame/mainFrame.h"
#include "frmclient/mainFrame/objinspect/objinspect.h"

#include <wx/artprov.h>
#include <wx/dialog.h>
#include <wx/imaglist.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>

#include <set>
#include <functional>

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

// A row's hops, as the server wrote them (Path — items {Id, Type}).
std::vector<ibSourceHop> PathOf(const ibProtocolNode& row)
{
	std::vector<ibSourceHop> path;
	for (const ibProtocolNode& hop : row.GetList(ibProtocol::ibView::ibFormDesigner::Path)) {
		const ibClassID type = ClsidOf(hop.GetString(ibProtocol::ibView::ibFormDesigner::Type));
		path.push_back({ static_cast<ibSourceId>(hop.GetInt(ibProtocol::ibView::ibFormDesigner::Id)), type != 0 ? type : g_valueUndefinedCLSID });
	}
	return path;
}

// The rows' shape — each root row's id, name, and what it is drawn as: a draw that says the same leaves the tree alone.
wxString ShapeOf(const ibProtocolNode& rows)
{
	wxString shape;
	for (const ibProtocolNode& row : rows.Children())
		shape << row.GetId() << wxT(':') << row.GetString(ibProtocol::ibView::ibFormDesigner::Name) << wxT(':')
			<< row.GetBool(ibProtocol::ibView::ibFormDesigner::Main) << row.GetBool(ibProtocol::ibView::ibFormDesigner::HasComposition)
			<< row.GetString(ibProtocol::ibView::ibFormDesigner::DropControl) << row.FindChild(ibProtocol::ibView::ibFormDesigner::Menu).IsNode() << wxT(';');
	return shape;
}

// The item of that name of a row's menu — none: a node with nothing in it (no Name; IsNode all the same).
ibProtocolNode FindMenuItem(const ibProtocolNode& menu, const char* name)
{
	for (const ibProtocolNode& item : menu.Children()) {
		if (item.GetString(ibProtocol::ibView::ibFormDesigner::Name) == wxString(name))
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
	: m_formHandler(formHandler), m_attributeId(id), m_columnId(column),
	  m_metaData(formHandler->GetEditorDocument() != nullptr ? formHandler->GetEditorDocument()->GetMetaData() : ibMetaData())
{
}

bool ibFormAttributeValue::IsEditable() const
{
	return m_formHandler->IsEditable();
}

bool ibFormAttributeValue::Fetch()
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Properties));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Attribute, m_attributeId);
	if (m_columnId != 0)
		request.SetValue(ibProtocol::ibView::ibFormDesigner::Column, m_columnId);
	ibProtocolNode answer;
	return m_formHandler->GetEditorDocument()->Fetch(request, answer) && ibPropertyObject::Receive(answer);
}

ibProperty* ibFormAttributeValue::CreateProperty(ibPropertyCategory* cat, ibDataNode&& row)
{
	return ibPropertyObject::CreateProperty<ibPropertySerializable>(cat, std::move(row));
}

void ibFormAttributeValue::OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue)
{
	m_formHandler->ModifyProperty(property, oldValue, newValue);
}

void ibFormAttributeValue::ReadRow(const ibProtocolNode& row)
{
	m_name = row.GetString(ibProtocol::ibView::ibFormDesigner::Name);
	m_main = row.GetBool(ibProtocol::ibView::ibFormDesigner::Main);
	m_menu = row.FindChild(ibProtocol::ibView::ibFormDesigner::Menu).Clone();
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
	// …and the form's attributes as it holds them: a row opened was asked of them then, so the same rows over attributes
	// changed say another composition — a column given a reference type showed none to open (2026-10-15). Compared as
	// nodes: written out as text to be compared, they were 14 % of a control's creation (2026-10-22).
	const wxString shape = ShapeOf(rows);
	if (shape == m_shape && attributes == m_attributes)
		return;
	m_shape = shape;
	m_attributes = attributes;

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

bool ibAttributeTree::SelectAttribute(long long attributeId, long long columnId)
{
	// A column's — the attribute's row opened, its columns asked, the column's row lit and its holder shown (the desktop's
	// OnAddColumn).
	if (columnId != 0)
		return SelectColumn(attributeId, columnId) && SelectColumnInInspector(m_tcAttributes->GetSelection());
	// …an attribute's — its row lit, its holder shown (the desktop's OnAdd / OnPaste, SelectEntry).
	const auto found = m_entries.find(attributeId);
	if (found == m_entries.end())
		return false;
	SelectEntry(found->first);
	SelectInInspector(found->second.get());
	return true;
}

void ibAttributeTree::AppendRows(const wxTreeItemId& parent, const ibProtocolNode& rows)
{
	const int image = m_tcAttributes->GetImageList() != nullptr && m_tcAttributes->GetImageList()->GetImageCount() > 0 ? 0 : -1;
	for (const ibProtocolNode& row : rows.Children()) {
		const std::vector<ibSourceHop> path = PathOf(row);
		std::vector<ibMetaID> refTypes;
		for (const ibProtocolNode& target : row.GetList(ibProtocol::ibView::ibFormDesigner::Targets))
			refTypes.push_back(static_cast<ibMetaID>(target.AsInt()));

		// A ROOT row: the holder + head-of-path; a composition row: its path alone.
		const auto found = row.GetId() != 0 ? m_entries.find(row.GetId()) : m_entries.end();
		ibFormAttributeValue* const entry = found != m_entries.end() ? found->second.get() : nullptr;

		ibAttributeTreeItemData* const itemData = new ibAttributeTreeItemData(entry, path, refTypes,
			row.GetBool(ibProtocol::ibView::ibFormDesigner::TableSection), ClsidOf(row.GetString(ibProtocol::ibView::ibFormDesigner::DropControl)),
			row.GetBool(ibProtocol::ibView::ibFormDesigner::UnderList));
		const wxTreeItemId item = m_tcAttributes->AppendItem(parent, row.GetString(ibProtocol::ibView::ibFormDesigner::Name), image, image, itemData);
		if (row.GetBool(ibProtocol::ibView::ibFormDesigner::Main))
			m_tcAttributes->SetItemBold(item);

		if (!row.Children().empty()) {
			// table section / nested composite: its columns came inline (the server built them in the same pass).
			AppendRows(item, row);
			itemData->SetLoaded();                  // already populated; OnItemExpanding no-ops on it
		}
		else if (row.GetBool(ibProtocol::ibView::ibFormDesigner::HasComposition)) {
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
	if (path.size() != 2)                                  // [attrId, columnId] — shallower can't name a column, and a
		return false;                                      // deeper row is a field of the column's type (its Date, 404)

	const long long attributeId = static_cast<long long>(path.front().m_id);
	const auto head = m_entries.find(attributeId);
	if (head == m_entries.end() || !FindMenuItem(head->second->GetMenu(), ibProtocol::ibView::ibFormDesigner::Column).Has(ibProtocol::ibView::ibFormDesigner::Name))
		return false;

	const long long columnId = static_cast<long long>(path[1].m_id);
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
		FindMenuItem(selEntry->GetMenu(), ibProtocol::ibView::ibFormDesigner::Column) : ibProtocolNode();
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
	if (addColumn.Has(ibProtocol::ibView::ibFormDesigner::Name))
		appendItem(ID_ATTR_ADD_COLUMN, addColumn.GetString(ibProtocol::ibView::ibFormDesigner::Title), wxART_NEW, editable);   // value-table only
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
	// HERE, each time it opens (Composition): the row as it was written is what it is asked with, over the form as it
	// stands, as the server holds it — what the metadata has now, an attribute added to the object since shown, one taken
	// away gone. A node already filled inline (a table section) is marked loaded and no-ops here.
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(event.GetItem());
	if (data == nullptr || data->IsLoaded())
		return;

	m_tcAttributes->DeleteChildren(event.GetItem());   // drop the dummy [+], or what the last opening was answered
	ibProtocolNode rows;
	if (FetchComposition(data, rows))
		AppendRows(event.GetItem(), rows);
}

bool ibAttributeTree::FetchComposition(const ibAttributeTreeItemData* data, ibProtocolNode& rows) const
{
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Composition));
	ibVisualEditor::WritePath(request, data->GetPath());
	for (const ibMetaID& target : data->GetRefTypes())
		request.AddItem(ibProtocol::ibView::ibFormDesigner::Targets, static_cast<long long>(target));
	if (data->IsUnderList())
		request.SetValue(ibProtocol::ibView::ibFormDesigner::UnderList, true);

	ibProtocolNode answer;
	if (!m_formHandler->GetEditorDocument()->Fetch(request, answer))
		return false;
	rows = answer.FindChild(ibProtocol::ibView::ibFormDesigner::Rows);
	return true;
}

namespace {

// The same place — by the hops' ids: a reference hop's pinned type is the binding's to keep, not the place's.
bool SamePlace(const std::vector<ibSourceHop>& path, const std::vector<ibSourceHop>& other, size_t count)
{
	if (path.size() != count || other.size() < count)
		return false;
	for (size_t idx = 0; idx < count; idx++) {
		if (path[idx].m_id != other[idx].m_id)
			return false;
	}
	return true;
}

} // namespace

bool ibAttributeTree::PickSource(wxWindow* parent, const wxString& name,
	ibProtocol::ibSchema::ibProperties::ibSourceDataType filter, ibSourceDescription& binding)
{
	using ibSourceDataType = ibProtocol::ibSchema::ibProperties::ibSourceDataType;

	// WHERE THE TREE STARTS — what the selected control's Source of that name offers, asked as the window opens (Editor):
	// the server's, its editor's.
	if (m_formHandler->m_selected == wxNOT_FOUND)
		return false;
	ibProtocolNode request;
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Kind, static_cast<int>(ibProtocol::ibView::ibFormDesigner::ibRequest::Editor));
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Control, m_formHandler->m_selected);
	request.SetValue(ibProtocol::ibView::ibFormDesigner::Name, name);
	ibProtocolNode answer;
	if (!m_formHandler->GetEditorDocument()->Fetch(request, answer))
		return false;
	const ibProtocolNode rows = answer.FindChild(ibProtocol::ibSchema::ibProperties::Sources);

	wxDialog dlg(parent, wxID_ANY, _("Choice source"), wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxCLIP_CHILDREN);
	dlg.SetFont(parent->GetFont());   // the desktop's: the property grid's
	wxTreeCtrl* const tc = new wxTreeCtrl(&dlg, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxSUNKEN_BORDER | wxTR_SINGLE
		| (filter != ibSourceDataType::TableColumn ? wxTR_TWIST_BUTTONS : 0));

	// …each row with the attribute's picture, a table with its own (the row's Picture), as the desktop's had them.
	wxImageList* const images = new wxImageList(16, 16);
	const wxBitmap attributePicture = ibBackendPicture::GetPicture(g_metaAttributeCLSID);
	if (attributePicture.IsOk())
		images->Add(attributePicture);
	tc->AssignImageList(images);
	std::map<wxString, int> pictures;
	auto imageOf = [&](const ibProtocolNode& row) {
		const wxString picture = row.GetString(ibProtocol::ibView::ibFormDesigner::Picture);
		const int fallback = images->GetImageCount() > 0 ? 0 : -1;
		if (picture.IsEmpty())
			return fallback;
		const auto found = pictures.find(picture);
		if (found != pictures.end())
			return found->second;
		const wxBitmap bitmap = ibProtocolPicture(picture);
		return pictures[picture] = bitmap.IsOk() ? images->Add(bitmap) : fallback;
	};

	// THE ROWS — the server's, as it offers them; under a reference, the rows it answers with, a table section not
	// among them; a row with a composition not yet asked, the dummy [+].
	std::function<void(const wxTreeItemId&, const ibProtocolNode&, bool)> append = [&](const wxTreeItemId& under,
		const ibProtocolNode& list, bool composition) {
		for (const ibProtocolNode& row : list.Children()) {
			const bool tableSection = row.GetBool(ibProtocol::ibView::ibFormDesigner::TableSection);
			if (composition && tableSection)
				continue;
			std::vector<ibMetaID> refTypes;
			for (const ibProtocolNode& target : row.GetList(ibProtocol::ibView::ibFormDesigner::Targets))
				refTypes.push_back(static_cast<ibMetaID>(target.AsInt()));
			ibAttributeTreeItemData* const itemData = new ibAttributeTreeItemData(nullptr, PathOf(row), refTypes,
				tableSection, 0, row.GetBool(ibProtocol::ibView::ibFormDesigner::UnderList));
			const int image = imageOf(row);
			const wxTreeItemId item = tc->AppendItem(under, row.GetString(ibProtocol::ibView::ibFormDesigner::Name), image, image, itemData);
			if (tableSection)
				tc->SetItemBold(item);
			if (!row.Children().empty()) {
				append(item, row, composition);
				itemData->SetLoaded();
			}
			else if (row.GetBool(ibProtocol::ibView::ibFormDesigner::HasComposition)) {
				tc->AppendItem(item, wxEmptyString);
			}
		}
	};
	const wxTreeItemId root = tc->AddRoot(wxEmptyString);
	append(root, rows, false);

	// …a reference opened as the tree opens it — its composition asked once.
	tc->Bind(wxEVT_TREE_ITEM_EXPANDING, [&](wxTreeEvent& event) {
		ibAttributeTreeItemData* const data = static_cast<ibAttributeTreeItemData*>(tc->GetItemData(event.GetItem()));
		if (data == nullptr || data->IsLoaded())
			return;
		data->SetLoaded();
		tc->DeleteChildren(event.GetItem());
		ibProtocolNode composition;
		if (FetchComposition(data, composition))
			append(event.GetItem(), composition, true);
	});

	// …every root open, as the desktop's: a reference under it stays shut, opened as the tree opens it.
	{
		wxTreeItemIdValue cookie;
		for (wxTreeItemId top = tc->GetFirstChild(root, cookie); top.IsOk(); top = tc->GetNextChild(root, cookie))
			tc->Expand(top);
	}

	// What may be chosen: a table section for a table, anything else for the rest.
	auto chosen = [&]() -> ibAttributeTreeItemData* {
		const wxTreeItemId item = tc->GetSelection();
		ibAttributeTreeItemData* const data = item.IsOk() ? static_cast<ibAttributeTreeItemData*>(tc->GetItemData(item)) : nullptr;
		if (data == nullptr || data->GetPath().empty())
			return nullptr;
		return filter == ibSourceDataType::Table && !data->IsTableSection() ? nullptr : data;
	};
	tc->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&](wxTreeEvent& event) {
		if (chosen() != nullptr)
			dlg.EndModal(wxID_OK);
		else
			event.Skip();
	});

	// THE BINDING'S OWN ROW — walked down its path: at each level the row it goes through, the longest (a row with no
	// path — a column's table, an object above it — through its rows), each row on the way opened (a reference asked as
	// it opens).
	{
		const std::vector<ibSourceHop>& path = binding.GetPath();
		std::function<wxTreeItemId(const wxTreeItemId&, size_t&)> through = [&](const wxTreeItemId& under, size_t& length) {
			wxTreeItemId match;
			wxTreeItemIdValue cookie;
			for (wxTreeItemId child = tc->GetFirstChild(under, cookie); child.IsOk(); child = tc->GetNextChild(under, cookie)) {
				const ibAttributeTreeItemData* const data = static_cast<ibAttributeTreeItemData*>(tc->GetItemData(child));
				if (data == nullptr)
					continue;
				const size_t size = data->GetPath().size();
				if (size == 0) {
					const wxTreeItemId inner = through(child, length);
					if (inner.IsOk())
						match = inner;
				}
				else if (size > length && SamePlace(data->GetPath(), path, size)) {
					match = child;
					length = size;
				}
			}
			return match;
		};
		wxTreeItemId found;
		size_t length = 0;
		for (wxTreeItemId under = root; length < path.size();) {
			const wxTreeItemId match = through(under, length);
			if (!match.IsOk())
				break;
			found = match;
			if (length < path.size())
				tc->Expand(match);
			under = match;
		}
		if (found.IsOk()) {
			tc->SelectItem(found);
			tc->EnsureVisible(found);
		}
	}

	wxBoxSizer* const topsizer = new wxBoxSizer(wxVERTICAL);
	topsizer->Add(tc, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(5)));
	topsizer->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags(0).Right().Border(wxBOTTOM | wxRIGHT, FromDIP(5)));
	dlg.SetSizer(topsizer);
	dlg.SetSize(dlg.FromDIP(wxSize(400, 300)));
	dlg.CentreOnParent();
	dlg.Bind(wxEVT_BUTTON, [&](wxCommandEvent& event) {
		if (chosen() != nullptr)
			event.Skip();   // …the dialog ends as it would
	}, wxID_OK);
	tc->SetFocus();

	if (dlg.ShowModal() != wxID_OK)
		return false;
	const ibAttributeTreeItemData* const data = chosen();
	if (data == nullptr)
		return false;
	ibSourceDescription picked;
	for (const ibSourceHop& hop : data->GetPath())
		picked.AppendSource(hop.m_id, hop.m_type);
	binding = picked;
	return true;
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
	// its command processor (undoable); the tree lands on it and shows it, as the desktop's OnAdd (SelectEntry) — the
	// answer shows it (SelectAttribute).
	m_formHandler->CreateAttribute();
}

void ibAttributeTree::OnAddColumn(wxCommandEvent& WXUNUSED(event))
{
	// A column of the selected VALUE-TABLE attribute — a UNIQUE name (Column, Column1, …) and a default Type (String),
	// made by the server's editor; the tree lands on it and shows it, as the desktop's OnAddColumn — the answer shows it
	// (SelectAttribute): the attribute opened, its columns asked with this one.
	m_formHandler->AddColumn(GetEntryFromItem(m_tcAttributes->GetSelection()));
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
	// OnPaste (SelectEntry) — the answer shows it (SelectAttribute).
	m_formHandler->PasteAttribute();
}
