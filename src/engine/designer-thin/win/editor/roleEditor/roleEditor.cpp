#include "roleEditor.h"

#include "frmclient/win/picture.h"   // a row's picture, by its id

#include <vector>

#define ICON_SIZE 16

ibRoleEditor::ibRoleEditor(wxWindow* parent,
	wxWindowID winid, const ibTabDocument* document) :
	wxSplitterWindow(parent, winid, wxDefaultPosition, wxDefaultSize, wxSP_3D | wxSP_LIVE_UPDATE), m_document(document)
{
	m_roleCtrl = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxTR_TWIST_BUTTONS);
	m_roleCtrl->SetDoubleBuffered(true);
	m_roleCtrl->Bind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);

	//set image list
	m_roleCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	m_checkCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_HIDE_ROOT | wxCR_EMPTY_CHECK | wxSUNKEN_BORDER | wxTR_TWIST_BUTTONS);
	m_checkCtrl->SetDoubleBuffered(true);
	m_checkCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);

	InitRole();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* treeSizer = new wxBoxSizer(wxHORIZONTAL);
	treeSizer->Add(m_roleCtrl, 2, wxEXPAND);
	treeSizer->Add(m_checkCtrl, 1, wxEXPAND);
	mainSizer->Add(treeSizer, 1, wxEXPAND, 1);

	wxSplitterWindow::SetSashGravity(0.5);
	wxSplitterWindow::SplitVertically(m_roleCtrl, m_checkCtrl,
		250);

	m_roleCtrl->SelectItem(m_treeMETADATA);

	wxSplitterWindow::SetSizer(mainSizer);
	wxSplitterWindow::Layout();
}

ibRoleEditor::~ibRoleEditor() {
	m_roleCtrl->Unbind(wxEVT_TREE_SEL_CHANGED, &ibRoleEditor::OnSelectedItem, this);
	m_checkCtrl->Unbind(wxEVT_CHECKTREE_CHOICE, &ibRoleEditor::OnCheckItem, this);
}

void ibRoleEditor::OnCheckItem(wxTreeEvent& event)
{
	// The right's own row, in the check tree (the desktop's asked the object tree for it, with this tree's item).
	wxTreeItemRoleData* data = dynamic_cast<wxTreeItemRoleData*>(
		m_checkCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr && m_document != nullptr) {
		// Given by the role or taken away — the server's (ibRoleRightsView::OnClientEvent), and drawn back.
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibNode::Id, data->GetMetaId());
		args.SetValue(ibProtocol::ibView::ibCheckTree::Name, data->GetRole());
		args.SetValue(ibProtocol::ibView::ibCheckTree::Checked, event.GetExtraLong() != 0);
		m_document->Post(ibProtocol::ibEvent::ibKind::Toggle, args);
	}

	event.Skip();
}

void ibRoleEditor::OnSelectedItem(wxTreeEvent& event) {
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_roleCtrl->GetItemData(event.GetItem())
		);
	m_checkCtrl->Freeze();
	m_checkCtrl->DeleteAllItems();
	if (data != nullptr) {
		wxTreeItemId root = m_checkCtrl->AddRoot(wxEmptyString);
		for (const ibProtocolNode& role : data->GetRoles().Children()) {
			wxTreeItemId newItem = m_checkCtrl->AppendItem(root, role.GetString(ibProtocol::ibView::ibCheckTree::Title), wxNOT_FOUND, wxNOT_FOUND,
				new wxTreeItemRoleData(data->GetMetaId(), role.GetString(ibProtocol::ibView::ibCheckTree::Name)));
			m_checkCtrl->SetItemState(newItem, !role.GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly) ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED);
			m_checkCtrl->Check(newItem, role.GetBool(ibProtocol::ibView::ibCheckTree::Checked));
		}
	}
	m_checkCtrl->Thaw();
	event.Skip();
}

void ibRoleEditor::RefreshRole(const ibProtocolNode& view)
{
	// Preserve the current row across the full rebuild — capture the selected object by its id, re-select it after
	// refilling, and the check tree re-follows it; and the row at the top, so the tree a tick is drawn back into does not
	// jump.
	m_keepSelId = 0;
	if (const wxTreeItemId sel = m_roleCtrl->GetSelection(); sel.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_roleCtrl->GetItemData(sel)))
			m_keepSelId = d->GetMetaId();
	m_keepSelNode = wxTreeItemId();
	m_keepTopId = 0;
	if (const wxTreeItemId top = m_roleCtrl->GetFirstVisibleItem(); top.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_roleCtrl->GetItemData(top)))
			m_keepTopId = d->GetMetaId();
	m_keepTopNode = wxTreeItemId();

	// ONE FRAME — the rebuild empties the tree and refills it, and unfrozen that shows
	// as a collapse followed by a re-appear. Same pattern as the form's attribute tree.
	m_roleCtrl->Freeze();
	m_roleCtrl->SetEvtHandlerEnabled(false);

	ClearRole();
	FillData(view);
	if (m_keepSelNode.IsOk())
		m_roleCtrl->SelectItem(m_keepSelNode);            // an object's row — restored by its id
	else if (m_treeMETADATA.IsOk())
		m_roleCtrl->SelectItem(m_treeMETADATA);           // the configuration's row (built in InitRole, not via AppendItem)
	if (m_keepTopNode.IsOk())
		m_roleCtrl->ScrollTo(m_keepTopNode);

	m_roleCtrl->SetEvtHandlerEnabled(true);
	m_roleCtrl->Thaw();

	// …and the rights of the row selected, read again from the rows just drawn: the selection was restored with the
	// events off.
	wxTreeEvent selected(wxEVT_TREE_SEL_CHANGED, m_roleCtrl, m_roleCtrl->GetSelection());
	OnSelectedItem(selected);
}

void ibRoleEditor::AddInterfaceItem(const ibProtocolNode& row, const wxTreeItemId& hParentID)
{
	// A section's sub-sections, as the server wrote them under its row.
	for (const ibProtocolNode& commonInterface : row.Children()) {
		AddInterfaceItem(commonInterface,
			AppendItem(hParentID, commonInterface));
	}
}

void ibRoleEditor::InitRole()
{
	m_roleCtrl->GetImageList()->RemoveAll();

	m_treeMETADATA = m_roleCtrl->AddRoot(_("Configuration"));

	m_roleCtrl->SetItemBold(m_treeMETADATA);
}

void ibRoleEditor::ClearRole() {

	// The groups are whatever the rows brought last time — wiping the tree wipes them with it, so there is nothing to
	// enumerate here.
	m_roleCtrl->DeleteAllItems();
	InitRole();
}

wxTreeItemId ibRoleEditor::GroupFor(const ibProtocolNode& row)
{
	// The icon and the caption the server's row carries (ibMetaGroupCaption there) — the same one the configuration
	// tree shows.
	wxImageList* imageList = m_roleCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId group = m_roleCtrl->AppendItem(m_treeMETADATA,
		row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId(), ibProtocolNode()));
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = group;
	return group;
}

wxTreeItemId ibRoleEditor::AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row)
{
	wxImageList* imageList = m_roleCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;
	// Its rights kept with it — the rows go with the next answer, the check tree reads them when it is selected.
	const wxTreeItemId item = m_roleCtrl->AppendItem(parent, row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex,
		new ibTreeItemObject(row.GetId(), row.FindChild(ibProtocol::ibView::ibCheckTree::Roles).Clone()));
	if (row.GetId() == m_keepSelId)   // the row that was selected before this rebuild — remember it for restore
		m_keepSelNode = item;
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = item;
	return item;
}

void ibRoleEditor::FillData(const ibProtocolNode& view)
{
	const ibProtocolNode root = view.FindChild(ibProtocol::ibView::ibCheckTree::Rows);

	// The rows' pictures asked of the server at once — not a call per row drawn.
	ibProtocolPictureReceive(root);

	// The configuration's row — its rights are the configuration's own.
	wxImageList* imageList = m_roleCtrl->GetImageList();
	const wxBitmap picture = ibProtocolPicture(root, ibProtocol::ibView::ibCheckTree::Picture);
	if (picture.IsOk()) {
		const int imageIndex = imageList->Add(picture);
		m_roleCtrl->SetItemImage(m_treeMETADATA, imageIndex);
		m_roleCtrl->SetItemImage(m_treeMETADATA, imageIndex, wxTreeItemIcon_Selected);
	}
	m_roleCtrl->SetItemText(m_treeMETADATA, root.GetString(ibProtocol::ibView::ibCheckTree::Title));
	m_roleCtrl->SetItemData(m_treeMETADATA, new ibTreeItemObject(root.GetId(), root.FindChild(ibProtocol::ibView::ibCheckTree::Roles).Clone()));
	if (root.GetId() == m_keepSelId)
		m_keepSelNode = m_treeMETADATA;

	// The server's walk — every object that has a right, under its group (a group row: below 0); a section with its
	// sub-sections under it.
	for (const ibProtocolNode& row : root.Children()) {
		if (row.GetId() < 0) {
			const wxTreeItemId group = GroupFor(row);
			for (const ibProtocolNode& object : row.Children())
				AddInterfaceItem(object, AppendItem(group, object));
		}
		else {
			AddInterfaceItem(row, AppendItem(m_treeMETADATA, row));
		}
	}

	m_roleCtrl->ExpandAll();
	SetReadOnly(view.FindChild(ibProtocol::ibView::State).GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly));
}
