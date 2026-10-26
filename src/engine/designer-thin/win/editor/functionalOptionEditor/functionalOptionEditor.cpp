#include "functionalOptionEditor.h"

#include "frmclient/win/picture.h"   // a row's picture, by its id

#include <functional>

#define ICON_SIZE 16

ibFunctionalOptionEditor::ibFunctionalOptionEditor(wxWindow* parent,
	wxWindowID winid, const ibTabDocument* document) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize),
	m_document(document)
{
	m_membersCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_membersCtrl->SetDoubleBuffered(true);
	m_membersCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibFunctionalOptionEditor::OnCheckItem, this);

	m_membersCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitMembers();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_membersCtrl, 1, wxEXPAND);

	m_membersCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibFunctionalOptionEditor::OnCheckItem(wxTreeEvent& event)
{
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_membersCtrl->GetItemData(event.GetItem())
		);

	if (data != nullptr && data->GetMetaId() > 0 && m_document != nullptr) {
		// Written into the member — it keeps the options it belongs to — on the server
		// (ibFunctionalOptionMembersView::OnClientEvent).
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibNode::Id, data->GetMetaId());
		args.SetValue(ibProtocol::ibView::ibCheckTree::Checked, event.GetExtraLong() != 0);
		m_document->Post(ibProtocol::ibEvent::ibKind::Toggle, args);
	}

	event.Skip();
}

void ibFunctionalOptionEditor::RefreshMembers(const ibProtocolNode& view)
{
	m_keepSelId = 0;
	if (const wxTreeItemId sel = m_membersCtrl->GetSelection(); sel.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_membersCtrl->GetItemData(sel)))
			m_keepSelId = d->GetMetaId();
	m_keepSelNode = wxTreeItemId();
	m_keepTopId = 0;
	if (const wxTreeItemId top = m_membersCtrl->GetFirstVisibleItem(); top.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_membersCtrl->GetItemData(top)))
			m_keepTopId = d->GetMetaId();
	m_keepTopNode = wxTreeItemId();

	// The objects opened, by their ids — the fill opens the groups only.
	m_keepExpandedIds.clear();
	const std::function<void(const wxTreeItemId&)> keepExpanded = [this, &keepExpanded](const wxTreeItemId& item) {
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = m_membersCtrl->GetFirstChild(item, cookie); child.IsOk();
			child = m_membersCtrl->GetNextChild(item, cookie)) {
			const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_membersCtrl->GetItemData(child));
			if (d != nullptr && d->GetMetaId() > 0 && m_membersCtrl->IsExpanded(child))
				m_keepExpandedIds.insert(d->GetMetaId());
			keepExpanded(child);
		}
	};
	if (m_treeMETADATA.IsOk())
		keepExpanded(m_treeMETADATA);

	// ONE FRAME, not a teardown followed by a re-appear — see commonAttributeEditor.h.
	m_membersCtrl->Freeze();
	m_membersCtrl->SetEvtHandlerEnabled(false);

	ClearMembers();
	FillData(view);
	if (m_keepSelNode.IsOk())
		m_membersCtrl->SelectItem(m_keepSelNode);
	if (m_keepTopNode.IsOk())
		m_membersCtrl->ScrollTo(m_keepTopNode);

	m_membersCtrl->SetEvtHandlerEnabled(true);
	m_membersCtrl->Thaw();
}

void ibFunctionalOptionEditor::InitMembers()
{
	m_groups.clear();
	m_membersCtrl->GetImageList()->RemoveAll();

	// The root wears the CONFIGURATION's icon, same as the section and common attribute editors' — the server's row
	// brings it (FillData).
	m_treeMETADATA = m_membersCtrl->AddRoot(_("Configuration"));
	m_membersCtrl->SetItemBold(m_treeMETADATA);
}

void ibFunctionalOptionEditor::ClearMembers()
{
	m_membersCtrl->DeleteAllItems();
	InitMembers();
}

wxTreeItemId ibFunctionalOptionEditor::GroupFor(const ibProtocolNode& row)
{
	// The icon and the caption the server's row carries (ibMetaGroupCaption there) — the one the configuration tree,
	// the role editor and the section editor show.
	wxImageList* imageList = m_membersCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId group = m_membersCtrl->AppendItem(m_treeMETADATA,
		row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = group;

	m_groups.push_back(group);
	return group;
}

wxTreeItemId ibFunctionalOptionEditor::AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row)
{
	wxImageList* imageList = m_membersCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId createItem = m_membersCtrl->AppendItem(
		parent, row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));

	const bool member = row.GetBool(ibProtocol::ibView::ibCheckTree::Checked);
	const bool editable = !row.GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly);

	m_membersCtrl->SetItemState(createItem,
		member
		? (editable ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED)
		: (editable ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED)
	);

	if (row.GetId() == m_keepSelId)
		m_keepSelNode = createItem;
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = createItem;

	return createItem;
}

void ibFunctionalOptionEditor::AppendChildren(const wxTreeItemId& parent, const ibProtocolNode& row)
{
	for (const ibProtocolNode& child : row.Children())
		AppendChildren(AppendItem(parent, child), child);

	// An object opened before the rebuild opens again, now that what is under it is there.
	if (m_keepExpandedIds.count(row.GetId()) != 0)
		m_membersCtrl->Expand(parent);
}

void ibFunctionalOptionEditor::FillData(const ibProtocolNode& view)
{
	const ibProtocolNode root = view.FindChild(ibProtocol::ibView::ibCheckTree::Rows);

	// The rows' pictures asked of the server at once — not a call per row drawn.
	ibProtocolPictureReceive(root);

	wxImageList* imageList = m_membersCtrl->GetImageList();
	const wxBitmap picture = ibProtocolPicture(root, ibProtocol::ibView::ibCheckTree::Picture);
	if (picture.IsOk()) {
		const int imageIndex = imageList->Add(picture);
		m_membersCtrl->SetItemImage(m_treeMETADATA, imageIndex);
		m_membersCtrl->SetItemImage(m_treeMETADATA, imageIndex, wxTreeItemIcon_Selected);
	}
	m_membersCtrl->SetItemText(m_treeMETADATA, root.GetString(ibProtocol::ibView::ibCheckTree::Title));
	m_membersCtrl->SetItemData(m_treeMETADATA, new ibTreeItemObject(root.GetId()));

	// ASKED, NOT LISTED — on the server: every object that says it may belong, under a group named by its own metatype
	// (a group row: below 0), with the fields and tables inside it that say the same.
	for (const ibProtocolNode& row : root.Children()) {
		if (row.GetId() < 0) {
			const wxTreeItemId group = GroupFor(row);
			for (const ibProtocolNode& object : row.Children())
				AppendChildren(AppendItem(group, object), object);
		}
		else {
			AppendChildren(AppendItem(m_treeMETADATA, row), row);
		}
	}

	// The groups open, the objects closed: every field of every object at once would bury the objects.
	m_membersCtrl->Expand(m_treeMETADATA);
	for (const wxTreeItemId& group : m_groups)
		m_membersCtrl->Expand(group);

	SetReadOnly(view.FindChild(ibProtocol::ibView::State).GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly));
}
