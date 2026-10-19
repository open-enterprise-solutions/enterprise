#include "interfaceEditor.h"

#include "frmclient/win/picture.h"   // a row's picture, by its id

#include <vector>

#define ICON_SIZE 16

namespace {

// The pictures of `row` and the rows under it — asked of the server at once, not a call per row drawn.
void PicturesOf(const ibProtocolNode& row, std::vector<wxString>& pictures)
{
	pictures.push_back(row.GetString(ibProtocol::ibView::ibCheckTree::Picture));
	for (const ibProtocolNode& child : row.Children())
		PicturesOf(child, pictures);
}

}

ibInterfaceEditor::ibInterfaceEditor(wxWindow* parent,
	wxWindowID winid, const ibTabDocument* document) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize), m_document(document)
{
	m_interfaceCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_interfaceCtrl->SetDoubleBuffered(true);
	m_interfaceCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibInterfaceEditor::OnCheckItem, this);

	//set image list
	m_interfaceCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitInterface();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_interfaceCtrl, 1, wxEXPAND);

	m_interfaceCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibInterfaceEditor::OnCheckItem(wxTreeEvent& event)
{
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_interfaceCtrl->GetItemData(event.GetItem())
		);
	if (data != nullptr && data->GetMetaId() > 0 && m_document != nullptr) {
		// In the section or out of it — the server's (ibInterfaceCompositionView::OnClientEvent), and drawn back.
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibNode::Id, data->GetMetaId());
		args.SetValue(ibProtocol::ibView::ibCheckTree::Checked, event.GetExtraLong() != 0);
		m_document->Post(ibProtocol::ibEvent::ibKind::Toggle, args);
	}

	event.Skip();
}

void ibInterfaceEditor::RefreshInterface(const ibProtocolNode& view)
{
	// Preserve the current row across the full rebuild — capture the selected object by its id, re-select it after
	// refilling; and the row at the top, so the tree a tick is drawn back into does not jump.
	m_keepSelId = 0;
	if (const wxTreeItemId sel = m_interfaceCtrl->GetSelection(); sel.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_interfaceCtrl->GetItemData(sel)))
			m_keepSelId = d->GetMetaId();
	m_keepSelNode = wxTreeItemId();
	m_keepTopId = 0;
	if (const wxTreeItemId top = m_interfaceCtrl->GetFirstVisibleItem(); top.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_interfaceCtrl->GetItemData(top)))
			m_keepTopId = d->GetMetaId();
	m_keepTopNode = wxTreeItemId();

	// ONE FRAME — same reason as the form's attribute tree: the rebuild tears the tree
	// down and refills it, and un-frozen that is visible as a collapse followed by a
	// re-appear. Events are muted for the churn (DeleteAllItems / SelectItem raise
	// native selection and focus events nobody wants mid-rebuild).
	m_interfaceCtrl->Freeze();
	m_interfaceCtrl->SetEvtHandlerEnabled(false);

	ClearInterface();
	FillData(view);
	if (m_keepSelNode.IsOk())
		m_interfaceCtrl->SelectItem(m_keepSelNode);       // an object's row — restored by its id
	if (m_keepTopNode.IsOk())
		m_interfaceCtrl->ScrollTo(m_keepTopNode);

	m_interfaceCtrl->SetEvtHandlerEnabled(true);
	m_interfaceCtrl->Thaw();
}

void ibInterfaceEditor::InitInterface()
{
	m_interfaceCtrl->GetImageList()->RemoveAll();

	m_treeMETADATA = m_interfaceCtrl->AddRoot(_("Configuration"));

	m_interfaceCtrl->SetItemBold(m_treeMETADATA);
}

void ibInterfaceEditor::ClearInterface() {

	// Nothing to enumerate: the groups are whatever the rows brought last time. Wiping the tree wipes them with it.
	m_interfaceCtrl->DeleteAllItems();
	InitInterface();
}

wxTreeItemId ibInterfaceEditor::GroupFor(const ibProtocolNode& row)
{
	// The icon and the caption the server's row carries — the type registry's and the group's own answer there
	// (ibMetaGroupCaption), the same the configuration tree shows.
	wxImageList* imageList = m_interfaceCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row.GetString(ibProtocol::ibView::ibCheckTree::Picture));
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId group = m_interfaceCtrl->AppendItem(m_treeMETADATA,
		row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = group;
	return group;
}

wxTreeItemId ibInterfaceEditor::AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row)
{
	wxImageList* imageList = m_interfaceCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row.GetString(ibProtocol::ibView::ibCheckTree::Picture));
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;
	// Bare NAME — the item already sits UNDER its type group, as on the desktop's.
	wxTreeItemId createItem = m_interfaceCtrl->AppendItem(parent, row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));
	const bool editable = !row.GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly);
	m_interfaceCtrl->SetItemState(createItem,
		row.GetBool(ibProtocol::ibView::ibCheckTree::Checked) ?
		editable ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED :
		editable ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED
	);
	if (row.GetId() == m_keepSelId)   // the row that was selected before this rebuild — remember it for restore
		m_keepSelNode = createItem;
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = createItem;

	return createItem;
}

void ibInterfaceEditor::FillData(const ibProtocolNode& view)
{
	const ibProtocolNode root = view.FindChild(ibProtocol::ibView::ibCheckTree::Rows);

	std::vector<wxString> pictures;
	PicturesOf(root, pictures);
	ibProtocolPictureReceive(pictures);

	// The configuration's row — its name and its picture, as the server's row has them.
	wxImageList* imageList = m_interfaceCtrl->GetImageList();
	const wxBitmap picture = ibProtocolPicture(root.GetString(ibProtocol::ibView::ibCheckTree::Picture));
	if (picture.IsOk()) {
		const int imageIndex = imageList->Add(picture);
		m_interfaceCtrl->SetItemImage(m_treeMETADATA, imageIndex);
		m_interfaceCtrl->SetItemImage(m_treeMETADATA, imageIndex, wxTreeItemIcon_Selected);
	}
	m_interfaceCtrl->SetItemText(m_treeMETADATA, root.GetString(ibProtocol::ibView::ibCheckTree::Title));
	m_interfaceCtrl->SetItemData(m_treeMETADATA, new ibTreeItemObject(root.GetId()));

	// The server's walk — every object that may be in a section, under its group (a group row: below 0); an object of a
	// metatype with no group stands under the configuration's row, as on the desktop's.
	for (const ibProtocolNode& row : root.Children()) {
		if (row.GetId() < 0) {
			const wxTreeItemId group = GroupFor(row);
			for (const ibProtocolNode& object : row.Children())
				AppendItem(group, object);
		}
		else {
			AppendItem(m_treeMETADATA, row);
		}
	}

	m_interfaceCtrl->ExpandAll();
	SetReadOnly(view.FindChild(ibProtocol::ibView::State).GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly));
}
