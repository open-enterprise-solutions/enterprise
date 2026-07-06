////////////////////////////////////////////////////////////////////////////
//	Description : visual editor — the form source-attribute tree
////////////////////////////////////////////////////////////////////////////

#include "visualEditor.h"
#include "frontend/visualView/ctrl/formAttribute.h"                    // ibFormAttributeValue (+ clipboard statics)
#include "frontend/visualView/ctrl/form.h"                              // ibValueForm attribute API
#include "frontend/visualView/ctrl/widgets.h"                           // g_controlCheckboxCLSID / g_controlTextCtrlCLSID (node -> control map)
#include "frontend/visualView/ctrl/tableBox.h"                          // g_controlTableBoxCLSID
#include "frontend/mainFrame/objinspect/objinspect.h"                  // objectInspector
#include "backend/metaCollection/attribute/metaAttributeObject.h"      // GetIconGroup (default attribute icon)
#include "backend/srcDataObject.h"                                      // ibSourceDataObject / ibSourceExplorer / ConvertToMetaIds
#include "backend/sourceDescription.h"                                   // ibSourceDescription / ibSourceDescriptionMemory (drag payload = the source path, unified serialize)
#include "backend/fileSystem/fs.h"                                       // ibWriterMemory (drag payload buffer)
#include "backend/metaCollection/partial/reference/reference.h"        // ibValueReferenceDataObject::Create (reference-as-source)
#include "backend/system/value/valueTable.h"                           // ibValueModelTable — value-table attribute column add

#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/artprov.h>
#include <wx/menu.h>
#include <wx/dnd.h>       // wxDropSource / wxCustomDataObject / wxDataFormat — drag a node onto the form

typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTree     ibAttributeTree;
typedef ibVisualEditorNotebook::ibVisualEditor::ibVisualEditorAttributeTreeItemData ibAttributeTreeItemData;
typedef ibValueModelTable::ibValueModelTableColumnCollection::ibValueModelTableColumnInfo ibTableColumnInfo;

enum {
	ID_ATTR_ADD = wxID_HIGHEST + 2100,
	ID_ATTR_EDIT,
	ID_ATTR_REMOVE,
	ID_ATTR_SETMAIN,
	ID_ATTR_PROPERTIES,
	ID_ATTR_COPY,
	ID_ATTR_CUT,
	ID_ATTR_PASTE,
	ID_ATTR_ADD_COLUMN,
};

wxBEGIN_EVENT_TABLE(ibAttributeTree, wxPanel)
	EVT_TREE_SEL_CHANGED(wxID_ANY, ibAttributeTree::OnSelChanged)
	EVT_TREE_ITEM_ACTIVATED(wxID_ANY, ibAttributeTree::OnActivated)
	EVT_TREE_ITEM_EXPANDING(wxID_ANY, ibAttributeTree::OnItemExpanding)
	EVT_TREE_BEGIN_DRAG(wxID_ANY, ibAttributeTree::OnBeginDrag)
	EVT_CONTEXT_MENU(ibAttributeTree::OnContextMenu)
	EVT_TREE_END_LABEL_EDIT(wxID_ANY, ibAttributeTree::OnEndLabelEdit)
	EVT_MENU(ID_ATTR_ADD, ibAttributeTree::OnAdd)
	EVT_MENU(ID_ATTR_EDIT, ibAttributeTree::OnEdit)
	EVT_MENU(ID_ATTR_REMOVE, ibAttributeTree::OnRemove)
	EVT_MENU(ID_ATTR_SETMAIN, ibAttributeTree::OnSetMain)
	EVT_MENU(ID_ATTR_PROPERTIES, ibAttributeTree::OnProperties)
	EVT_MENU(ID_ATTR_COPY, ibAttributeTree::OnCopy)
	EVT_MENU(ID_ATTR_CUT, ibAttributeTree::OnCut)
	EVT_MENU(ID_ATTR_PASTE, ibAttributeTree::OnPaste)
	EVT_MENU(ID_ATTR_ADD_COLUMN, ibAttributeTree::OnAddColumn)
wxEND_EVENT_TABLE()

ibAttributeTree::ibVisualEditorAttributeTree(ibVisualEditor* owner, wxWindow* parent, int id)
	: wxPanel(parent, id), m_formHandler(owner)
{
	// No toolbar — every action lives on the context menu. wxTR_EDIT_LABELS enables the inline
	// rename used by "Edit".
	m_tcAttributes = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_FULL_ROW_HIGHLIGHT | wxTR_EDIT_LABELS);
	m_tcAttributes->SetDoubleBuffered(true);   // no flicker on RebuildTree (add / set-main), like the object tree

	// Every entry carries the default meta-attribute icon (index 0).
	wxImageList* images = new wxImageList(16, 16);
	images->Add(ibValueMetaObjectAttribute::GetIconGroup());
	m_tcAttributes->AssignImageList(images);

	// Single click always (re)selects — even the already-selected item — so ONE click surfaces the
	// attribute in the inspector (OnSelChanged fires only on CHANGE). Same as the object tree, which
	// routes its click-reselect through SelectItemData.
	m_tcAttributes->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
		const wxTreeItemId id = m_tcAttributes->HitTest(event.GetPosition());
		if (id.IsOk() && id == m_tcAttributes->GetSelection())
			if (!SelectColumnInInspector(id))          // a value-table column node re-surfaces its column-info
				SelectInInspector(GetEntryFromItem(id));   // else re-surface the already-selected attribute
		event.Skip();
	});

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(m_tcAttributes, 1, wxEXPAND);
	SetSizer(sizer);
}

// The control-class CLSID a node maps to (0 = not draggable). Mirrors BuildForm's type->control choice
// for ONE node: table / section -> Tablebox, boolean -> Checkbox, scalar / reference -> Textctrl. A
// composite OBJECT container (a non-table, non-reference metaobject: e.g. the main object attribute) has
// no single control -> not draggable; drag its fields instead. Returns the CLSID (kind-typed), not a
// class name: it is only the draggable gate now — the drop side re-resolves the class from the path.
static ibClassID DropControlClass(bool isTable, const std::vector<ibMetaID>& refTypes, const ibTypeDescription& typeDesc)
{
	if (isTable)
		return g_controlTableBoxCLSID;
	if (typeDesc.GetClsidCount() == 1) {
		if (typeDesc.ContainType(ibValueTypes::TYPE_BOOLEAN))
			return g_controlCheckboxCLSID;
		if (typeDesc.ContainType(ibValueTypes::TYPE_STRING)
			|| typeDesc.ContainType(ibValueTypes::TYPE_NUMBER)
			|| typeDesc.ContainType(ibValueTypes::TYPE_DATE))
			return g_controlTextCtrlCLSID;
	}
	if (!refTypes.empty())              // reference (incl. composite) -> a select-field textctrl
		return g_controlTextCtrlCLSID;
	return 0;                           // composite object container -> not a single control
}

// Does this attribute have a composition to expand? A reference type expands metadata-only (targets
// present); an object / list type expands through its live source's explorer (when materialised). A
// primitive (neither) is a leaf. Cheap — the actual children are built lazily on first expand.
static bool HasComposition(ibFormAttributeValue* entry, const std::vector<ibMetaID>& refTypes)
{
	if (!refTypes.empty())
		return true;
	if (ibSourceDataObject* src = entry != nullptr ? entry->GetSourceValue() : nullptr) {
		const ibSourceExplorer* explorer = src->GetSourceExplorer();
		return explorer != nullptr && explorer->GetHelperCount() > 0;
	}
	return false;
}

// Append a SOURCE's fields AND table sections as child nodes under `parent`, each carrying the full
// binding path (prefix + column id) + its view flags. Shows EVERYTHING — unlike the picker, which
// filters to sections OR fields; the attribute tree is a full-composition view. A REFERENCE column gets
// a dummy [+] and expands LAZILY (references are the only cyclic source — never eager). A table section
// is finite / non-cyclic, so its columns are built inline in this same pass (a reference column INSIDE
// it still expands lazily). `metaData` resolves reference targets; `suffix` disambiguates composite-
// reference branches by target name.
static void AppendComposition(wxTreeCtrl* tc, const wxTreeItemId& parent, const std::vector<ibSourceHop>& prefix,
	const ibSourceExplorer& explorer, const ibMetaData* metaData, const wxString& suffix = wxEmptyString,
	bool parentIsList = false)
{
	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const ibSourceExplorer* col = explorer.GetHelper(i);
		if (col == nullptr)
			continue;
		std::vector<ibSourceHop> childPath = prefix;
		childPath.push_back({ col->GetSourceId() });   // this column's hop (scalar — a reference pins its branch when ITS children descend)
		const std::vector<ibMetaID> refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetClsidList(), metaData);
		// The tree node is the column's IDENTITY — show its NAME, not its Caption/synonym (a control's
		// header takes the synonym; the attribute tree stays name-keyed so a column is findable by name).
		const wxString base = col->GetSourceName();
		const wxString label = suffix.IsEmpty() ? base : base + wxT(" - ") + suffix;
		// A field UNDER a list / table section is a COLUMN of that table. It IS draggable: dropped onto a
		// tablebox bound to the SAME source it appends a bound column (CreateBoundControl's tablebox branch);
		// dropped anywhere else it is declined (ResolveDropControlClass rejects a table column) so no broken
		// scalar is created. Object / reference fields (parentIsList == false) still drop as scalar controls.
		const ibClassID dropControl = DropControlClass(col->IsTableSection(), refTypes, col->GetTypeDesc());
		ibAttributeTreeItemData* itemData = new ibAttributeTreeItemData(nullptr, childPath, refTypes, col->IsTableSection(), dropControl, parentIsList);
		const wxTreeItemId child = tc->AppendItem(parent, label, 0, 0, itemData);
		if (!refTypes.empty()) {
			tc->AppendItem(child, wxEmptyString);   // reference -> lazy [+]
		}
		else if (col->GetHelperCount() > 0) {
			// table section / nested composite: finite, non-cyclic -> build its columns inline now. A
			// section's columns are list-like (not standalone-bindable) -> parentIsList = true for them.
			AppendComposition(tc, child, childPath, *col, metaData, wxEmptyString, true);
			itemData->SetLoaded();                  // already populated; OnItemExpanding no-ops on it
		}
	}
}

void ibAttributeTree::RebuildTree()
{
	if (m_tcAttributes == nullptr)
		return;

	// Remember the selected attribute BY IDENTITY (the holder pointer). The form owns the holders in
	// m_attributes; RebuildTree recreates only the tree items, so a survivor's pointer is stable —
	// the same key the object tree uses (m_listItem keyed by ibValueFrame*). A just-removed entry is
	// held alive by the undo command, so the compare below is safe and simply matches nothing.
	ibFormAttributeValue* keep = GetEntryFromItem(m_tcAttributes->GetSelection());

	m_rebuilding = true;        // suppress OnSelChanged while items churn (stale/freed holders)
	m_tcAttributes->Freeze();   // no flicker while the tree is torn down and re-appended
	m_tcAttributes->DeleteAllItems();
	const wxTreeItemId root = m_tcAttributes->AddRoot(wxT("Attributes"));

	if (ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr) {
		const ibMetaData* metaData = form->GetMetaData();
		for (unsigned int idx = 0; idx < form->GetAttributeCount(); idx++) {
			ibFormAttributeValue* entry = form->GetAttribute(idx);
			const ibMetaID attrId = entry->GetId();
			// A reference-typed attribute expands metadata-only (through a reference-as-source); an
			// object / list attribute expands through its live source's explorer (when materialised).
			const std::vector<ibMetaID> refTypes = ibValueReferenceDataObject::ConvertToMetaIds(entry->GetTypeDesc().GetClsidList(), metaData);
			ibSourceDataObject* src = entry->GetSourceValue();
			const bool isTable = src != nullptr && src->IsTableSource();   // list-typed attribute -> a tablebox
			const ibClassID dropControl = DropControlClass(isTable, refTypes, entry->GetTypeDesc());
			const wxTreeItemId id = m_tcAttributes->AppendItem(
				root, entry->GetName(), 0, 0,
				new ibAttributeTreeItemData(entry, std::vector<ibSourceHop>{ { attrId } }, refTypes, isTable, dropControl));   // ROOT: holder + head-of-path
			if (entry->IsMain())
				m_tcAttributes->SetItemBold(id);
			if (HasComposition(entry, refTypes))
				m_tcAttributes->AppendItem(id, wxEmptyString);   // dummy -> [+]; built lazily on expand
		}
	}
	m_tcAttributes->Thaw();
	m_rebuilding = false;

	SelectEntry(keep);   // restore by identity — now fires OnSelChanged with a LIVE holder
}

ibFormAttributeValue* ibAttributeTree::GetEntryFromItem(const wxTreeItemId& item) const
{
	if (item.IsOk())
		if (wxTreeItemData* data = m_tcAttributes->GetItemData(item))
			return ((ibAttributeTreeItemData*)data)->GetEntry();
	return nullptr;
}

void ibAttributeTree::SelectEntry(ibFormAttributeValue* entry)
{
	if (entry == nullptr || m_tcAttributes == nullptr)
		return;
	const wxTreeItemId root = m_tcAttributes->GetRootItem();
	if (!root.IsOk())
		return;
	wxTreeItemIdValue cookie;
	for (wxTreeItemId id = m_tcAttributes->GetFirstChild(root, cookie); id.IsOk();
		id = m_tcAttributes->GetNextChild(root, cookie)) {
		if (GetEntryFromItem(id) == entry) {   // match BY IDENTITY, like m_listItem.find(obj)
			m_tcAttributes->SelectItem(id);    // fires OnSelChanged → inspector shows it
			m_tcAttributes->EnsureVisible(id);
			return;
		}
	}
}

void ibAttributeTree::SelectInInspector(ibFormAttributeValue* entry)
{
	// The single decode-and-select point. Select the HOLDER (facade): it surfaces the attribute's +
	// the generated value's properties and intercepts their changes. SelectObject is a no-op while
	// the inspector is hidden, so raise it first.
	if (entry == nullptr)
		return;
	if (!objectInspector->IsShownInspector())
		objectInspector->ShowInspector();
	objectInspector->SelectObject(entry, true);
}

bool ibAttributeTree::SelectColumnInInspector(const wxTreeItemId& item)
{
	// A value-table column node carries NO holder (GetEntry == null) and a path [attrId, columnId].
	// Resolve attrId → the value-table held by that attribute, match columnId against its columns, and
	// select that column's editable property object (Name + Type) in the inspector.
	if (!item.IsOk() || m_tcAttributes == nullptr)
		return false;
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(item);
	if (data == nullptr || data->GetEntry() != nullptr)   // a root holder is handled by SelectInInspector
		return false;
	const std::vector<ibSourceHop>& path = data->GetPath();
	if (path.size() < 2)                                   // [attrId, columnId] — shallower can't name a column
		return false;

	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return false;
	ibFormAttributeValue* head = form->FindAttributeById(path.front().m_id);
	if (head == nullptr || head->GetBindValue() == nullptr)
		return false;

	// The head attribute's held value must be a value-table (cast the project way — no dynamic_cast).
	ibValueModelTable* valueTable = nullptr;
	head->GetBindValue()->ConvertToValue(valueTable);
	ibValueModel::ibValueModelColumnCollection* cols = valueTable != nullptr ? valueTable->GetColumnCollection() : nullptr;
	if (cols == nullptr)
		return false;

	const ibMetaID leaf = path.back().m_id;
	for (unsigned int i = 0; i < cols->GetColumnCount(); i++) {
		ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ci = cols->GetColumnInfo(i);
		if (ci == nullptr || (ibMetaID)ci->GetColumnID() != leaf)
			continue;
		// The concrete column-info IS the property object; reach it via ConvertToValue (it is an ibValue).
		ibTableColumnInfo* colInfo = nullptr;
		if (ci->ConvertToValue(colInfo) && colInfo != nullptr) {
			if (!objectInspector->IsShownInspector())
				objectInspector->ShowInspector();
			objectInspector->SelectObject(colInfo, true);
			return true;
		}
	}
	return false;
}

void ibAttributeTree::OnSelChanged(wxTreeEvent& event)
{
	if (m_rebuilding)
		return;   // mid-rebuild: the item's holder may be stale/freed — don't touch the inspector
	const wxTreeItemId item = event.GetItem();
	if (!SelectColumnInInspector(item))   // a value-table column node? select its column-info property object
		SelectInInspector(GetEntryFromItem(item));
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
	// A value-table attribute grows COLUMNS on the attribute side (edited here; the bound tablebox then
	// shows them). This is a pure DECISION (gate the menu item) → check the held value's kind by clsid, not
	// a cast (the cast is reserved for OnAddColumn, which must operate on the table).
	const bool isTable = selEntry != nullptr && selEntry->GetBindValue() != nullptr
		&& selEntry->GetBindValue()->GetClassType() == g_valueTableCLSID;

	wxMenu menu;
	auto appendItem = [&menu](int menuId, const wxString& label, const wxArtID& art, bool enabled) {
		wxMenuItem* item = new wxMenuItem(&menu, menuId, label);
		item->SetBitmap(wxArtProvider::GetBitmap(art, wxART_MENU));
		menu.Append(item);
		item->Enable(enabled);
	};

	appendItem(ID_ATTR_ADD, _("Add"), wxART_NEW, true);              // always available
	if (isTable)
		appendItem(ID_ATTR_ADD_COLUMN, _("Add column"), wxART_NEW, true);   // value-table only
	appendItem(ID_ATTR_EDIT, _("Edit"), wxART_EDIT, hasAttr);
	appendItem(ID_ATTR_REMOVE, _("Delete"), wxART_DELETE, hasAttr);
	menu.AppendSeparator();
	appendItem(ID_ATTR_CUT, _("Cut"), wxART_CUT, hasAttr);
	appendItem(ID_ATTR_COPY, _("Copy"), wxART_COPY, hasAttr);
	appendItem(ID_ATTR_PASTE, _("Paste"), wxART_PASTE, true);        // always available
	menu.AppendSeparator();
	appendItem(ID_ATTR_SETMAIN, isMain ? _("Unset main") : _("Set as main"), wxART_TICK_MARK, hasAttr);
	appendItem(ID_ATTR_PROPERTIES, _("Properties"), wxART_LIST_VIEW, hasAttr);

	PopupMenu(&menu);
}

void ibAttributeTree::OnActivated(wxTreeEvent& event)
{
	// Double-click / Enter on an item → activate its properties in the inspector, even when it is
	// already the selected item (OnSelChanged would not re-fire). "Activate the property."
	const wxTreeItemId item = event.GetItem();
	if (!SelectColumnInInspector(item))   // a value-table column node activates its column-info property object
		SelectInInspector(GetEntryFromItem(item));
}

void ibAttributeTree::OnItemExpanding(wxTreeEvent& event)
{
	// Lazy composition build — the payoff of the picker being a tree: a node ships with a single dummy
	// [+]; its real children are built HERE, once. References stay lazy (never eager) so a self / cyclic
	// reference (A.B.A…) can't blow the stack; a node already filled inline (a table section) is marked
	// loaded and no-ops here.
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(event.GetItem());
	if (data == nullptr || data->IsLoaded())
		return;
	data->SetLoaded();

	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	const ibMetaData* metaData = form != nullptr ? form->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return;

	m_tcAttributes->DeleteChildren(event.GetItem());   // drop the dummy [+]
	const std::vector<ibSourceHop>& childPrefix = data->GetPath();

	if (data->HasRef()) {
		// Reference node: descend into each TARGET type via an empty reference-as-source (metadata-only,
		// no live row needed) — the design-time twin of the runtime value hop. A COMPOSITE reference
		// (several targets) expands every branch, target-name-disambiguated.
		const bool multi = data->GetRefTypes().size() > 1;
		for (const ibMetaID& target : data->GetRefTypes()) {
			ibValue refValue = ibValueReferenceDataObject::Create(metaData, target);   // empty typed reference (ref-counted)
			ibSourceDataObject* refSource = nullptr;
			refValue.ConvertToValue(refSource);
			if (refSource == nullptr)
				continue;
			// PIN this reference hop to the branch we descend into — the child fields carry it, so the DRAG /
			// serialized path coerces the composite reference to THIS target (else it read back <not selected>).
			if (const ibSourceExplorer* explorer = refSource->GetSourceExplorer())
				AppendComposition(m_tcAttributes, event.GetItem(), data->ChildPrefix(reference_to_clsid(target)), *explorer, metaData,
					multi ? refSource->GetSourceCaption() : wxString(wxEmptyString), data->IsUnderList());
		}
	}
	else if (ibFormAttributeValue* entry = data->GetEntry()) {
		// Root attribute of an object / list type: expand its live source's explorer (fields + sections).
		// A LIST root's children are COLUMNS (not standalone sources) -> mark them under-list.
		if (ibSourceDataObject* src = entry->GetSourceValue())
			if (const ibSourceExplorer* explorer = src->GetSourceExplorer())
				AppendComposition(m_tcAttributes, event.GetItem(), childPrefix, *explorer, metaData,
					wxEmptyString, src->IsTableSource());
	}
}

void ibAttributeTree::OnBeginDrag(wxTreeEvent& event)
{
	// Drag a node onto the form canvas. Payload = the binding PATH (head = attribute id), serialized by the
	// ENGINE's source-description serializer (ibSourceDescriptionMemory — the SAME writer that persists a
	// control's source), NOT a hand-rolled byte format. The drop side loads it and resolves the control
	// class from the type AT the path (the canon: drag carries the source/path, the drop resolves the class).
	// A node with no mapped control (an object container) is not draggable — the tree's DropControl flag.
	ibAttributeTreeItemData* data = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(event.GetItem());
	if (data == nullptr || data->GetDropControl() == 0)
		return;

	// Payload = the binding PATH as RAW ids — resolution is via the source explorer (metadata-agnostic), so
	// the serializer needs no metaData. The drop loads it and resolves the control class from the path.
	ibSourceDescription srcDesc;   // build from the HOP path — reference hops carry the pinned branch type (composite)
	for (const ibSourceHop& hop : data->GetPath())
		srcDesc.AppendSource(hop.m_id, hop.m_type);
	ibWriterMemory writer;
	ibSourceDescriptionMemory::SaveData(writer, srcDesc);
	const wxMemoryBuffer buf = writer.buffer();

	wxCustomDataObject payload(wxDataFormat(wxT("oes_source_drag")));
	payload.SetData(buf.GetDataLen(), buf.GetData());
	wxDropSource source(payload, m_tcAttributes);
	source.DoDragDrop(wxDrag_CopyOnly);
}

void ibAttributeTree::OnAdd(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	// A new attribute is never empty: a UNIQUE name and a default Type (String) — the user changes
	// the Type via the inspector afterwards. Build it UNOWNED and run it through the command
	// processor so the add is UNDOABLE; the command attaches it and refreshes the editor (this tree
	// re-reads the form's attribute set). Then land the current row on the new entry BY IDENTITY —
	// the holder is ref-counted, so the raw pointer survives being handed to the command.
	ibValuePtr<ibFormAttributeValue> holder = form->MakeAttribute(form->MakeUniqueAttributeName());
	ibFormAttributeValue* added = holder;
	m_formHandler->InsertAttribute(holder);   // attach + RefreshEditor → RebuildTree
	SelectEntry(added);                        // select the new attribute (drives OnSelChanged → inspector)
}

void ibAttributeTree::OnAddColumn(wxCommandEvent& WXUNUSED(event))
{
	// Add a column to the selected VALUE-TABLE attribute. Columns live in the value's own collection and
	// serialize with the attribute (the value's WriteProperty); the tablebox bound to this attribute shows
	// them, and the tree reveals them under the (expanded) attribute node. Cast the held value the project
	// way (ConvertToValue) — no dynamic_cast, no clsid literal.
	ibFormAttributeValue* selEntry = GetEntryFromItem(m_tcAttributes->GetSelection());
	ibValueModelTable* valueTable = nullptr;
	if (selEntry != nullptr && selEntry->GetBindValue() != nullptr)
		selEntry->GetBindValue()->ConvertToValue(valueTable);
	ibValueModel::ibValueModelColumnCollection* cols = valueTable != nullptr ? valueTable->GetColumnCollection() : nullptr;
	if (cols == nullptr)
		return;

	// Column name unique among the table's own columns (Column, Column1, …).
	const wxString base = _("Column");
	wxString name = base;
	for (unsigned int suffix = 0;;) {
		bool taken = false;
		for (unsigned int c = 0; c < cols->GetColumnCount(); c++) {
			ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* ci = cols->GetColumnInfo(c);
			if (ci != nullptr && ci->GetColumnName() == name) { taken = true; break; }
		}
		if (!taken)
			break;
		name = wxString::Format(wxT("%s%u"), base, ++suffix);
	}

	ibValueModel::ibValueModelColumnCollection::ibValueModelColumnInfo* added =
		cols->AddColumn(name, ibTypeDescription(g_valueStringCLSID), name);   // String default; user changes Type after
	const ibMetaID newColId = added != nullptr ? (ibMetaID)added->GetColumnID() : wxNOT_FOUND;
	if (m_formHandler != nullptr)
		m_formHandler->Modify(true);
	RebuildTree();

	// Land the current row ON THE NEW COLUMN (not just re-select the table attribute): expand the attribute
	// so its columns build (lazy), then select the freshly-added column node by its id — so the user can
	// edit its Caption / Type straight away (drives OnSelChanged → SelectColumnInInspector).
	const wxTreeItemId root = m_tcAttributes != nullptr ? m_tcAttributes->GetRootItem() : wxTreeItemId();
	wxTreeItemIdValue cookie;
	for (wxTreeItemId attrItem = root.IsOk() ? m_tcAttributes->GetFirstChild(root, cookie) : wxTreeItemId();
		attrItem.IsOk() && newColId != wxNOT_FOUND; attrItem = m_tcAttributes->GetNextChild(root, cookie)) {
		if (GetEntryFromItem(attrItem) != selEntry)
			continue;
		m_tcAttributes->Expand(attrItem);   // OnItemExpanding builds the column child nodes
		wxTreeItemIdValue childCookie;
		for (wxTreeItemId col = m_tcAttributes->GetFirstChild(attrItem, childCookie); col.IsOk();
			col = m_tcAttributes->GetNextChild(attrItem, childCookie)) {
			const ibAttributeTreeItemData* colData = (ibAttributeTreeItemData*)m_tcAttributes->GetItemData(col);
			if (colData != nullptr && !colData->GetPath().empty() && colData->GetPath().back().m_id == newColId) {
				m_tcAttributes->SelectItem(col);
				m_tcAttributes->EnsureVisible(col);
				return;
			}
		}
		break;
	}
	SelectEntry(selEntry);   // fallback: keep the table attribute selected
}

void ibAttributeTree::OnEdit(wxCommandEvent& WXUNUSED(event))
{
	// "Edit" = rename inline; OnEndLabelEdit validates (non-empty + unique) and applies.
	const wxTreeItemId sel = m_tcAttributes->GetSelection();
	if (sel.IsOk())
		m_tcAttributes->EditLabel(sel);
}

void ibAttributeTree::OnEndLabelEdit(wxTreeEvent& event)
{
	if (event.IsEditCancelled())
		return;

	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibFormAttributeValue* entry = GetEntryFromItem(event.GetItem());
	if (form == nullptr || entry == nullptr) { event.Veto(); return; }

	// Reject an empty or duplicate name — RenameAttribute re-binds the module variable on success.
	if (!form->RenameAttribute(entry, event.GetLabel())) {
		event.Veto();
		return;
	}
	m_formHandler->Modify(true);
	m_formHandler->NotifyEditorSaved();

	SelectInInspector(entry);
}

void ibAttributeTree::OnRemove(wxCommandEvent& WXUNUSED(event))
{
	// Through the command processor (undoable). The command detaches the entry from the form,
	// marks the editor modified and refreshes it — this tree (and controls bound to the deleted
	// attribute, which now render broken/empty) re-read along with the rest.
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		m_formHandler->RemoveAttribute(entry);
}

void ibAttributeTree::OnSetMain(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (form == nullptr || entry == nullptr)
		return;

	// Toggle: clicking on the CURRENT main clears it (no main → its controls get their own command
	// bar back); otherwise make it main. SetMainAttribute keeps the sole-main invariant.
	form->SetMainAttribute(entry->IsMain() ? nullptr : entry);
	m_formHandler->Modify(true);
	m_formHandler->NotifyEditorSaved();
	RebuildTree();
	m_formHandler->RefreshEditor();   // source moved to the new main → controls re-render accordingly
}

void ibAttributeTree::OnProperties(wxCommandEvent& WXUNUSED(event))
{
	SelectInInspector(GetEntryFromItem(m_tcAttributes->GetSelection()));
}

void ibAttributeTree::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	// Clipboard copy lives on the holder (serialization-based, own format id).
	if (ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection()))
		ibFormAttributeValue::CopyToClipboard(entry);
}

void ibAttributeTree::OnCut(wxCommandEvent& WXUNUSED(event))
{
	// Cut = copy to the clipboard, then remove through the command processor (the removal is undoable).
	ibFormAttributeValue* entry = GetEntryFromItem(m_tcAttributes->GetSelection());
	if (entry == nullptr)
		return;
	ibFormAttributeValue::CopyToClipboard(entry);
	m_formHandler->RemoveAttribute(entry);
}

void ibAttributeTree::OnPaste(wxCommandEvent& WXUNUSED(event))
{
	ibValueForm* form = m_formHandler != nullptr ? m_formHandler->GetValueForm() : nullptr;
	if (form == nullptr)
		return;

	if (ibFormAttributeValue* entry = ibFormAttributeValue::PasteFromClipboard(form)) {
		m_formHandler->Modify(true);
		RebuildTree();
		SelectEntry(entry);   // land on the pasted attribute BY IDENTITY (drives OnSelChanged → inspector)
	}
}
