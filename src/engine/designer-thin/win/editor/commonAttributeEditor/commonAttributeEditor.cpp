#include "commonAttributeEditor.h"

#include "frmclient/win/picture.h"   // a row's picture, by its id

#include <vector>

#define ICON_SIZE 16

ibCommonAttributeCompositionEditor::ibCommonAttributeCompositionEditor(wxWindow* parent,
	wxWindowID winid, const ibTabDocument* document) :
	wxWindow(parent, winid, wxDefaultPosition, wxDefaultSize),
	m_document(document)
{
	m_compositionCtrl = new ibCheckTree(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_ROW_LINES | wxTR_NO_LINES | wxTR_SINGLE | wxCR_EMPTY_CHECK | wxTR_TWIST_BUTTONS);
	m_compositionCtrl->SetDoubleBuffered(true);
	m_compositionCtrl->Bind(wxEVT_CHECKTREE_CHOICE, &ibCommonAttributeCompositionEditor::OnCheckItem, this);

	m_compositionCtrl->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	InitComposition();

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(m_compositionCtrl, 1, wxEXPAND);

	m_compositionCtrl->SelectItem(m_treeMETADATA);

	wxWindow::SetSizer(mainSizer);
	wxWindow::Layout();
}

void ibCommonAttributeCompositionEditor::OnCheckItem(wxTreeEvent& event)
{
	ibTreeItemObject* data = dynamic_cast<ibTreeItemObject*>(
		m_compositionCtrl->GetItemData(event.GetItem())
		);

	if (data != nullptr && data->GetMetaId() > 0 && m_document != nullptr) {
		// THE ATTRIBUTE ITSELF MOVES, not a flag: checking creates the copy inside that
		// object, unchecking deletes it — on the server (ibCommonAttributeCompositionView::OnClientEvent).
		ibProtocolNode args;
		args.SetValue(ibProtocol::ibNode::Id, data->GetMetaId());
		args.SetValue(ibProtocol::ibView::ibCheckTree::Checked, event.GetExtraLong() != 0);
		m_document->Post(ibProtocol::ibEvent::ibKind::Toggle, args);
	}

	event.Skip();
}

void ibCommonAttributeCompositionEditor::RefreshComposition(const ibProtocolNode& view)
{
	m_keepSelId = 0;
	if (const wxTreeItemId sel = m_compositionCtrl->GetSelection(); sel.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_compositionCtrl->GetItemData(sel)))
			m_keepSelId = d->GetMetaId();
	m_keepSelNode = wxTreeItemId();
	m_keepTopId = 0;
	if (const wxTreeItemId top = m_compositionCtrl->GetFirstVisibleItem(); top.IsOk())
		if (const ibTreeItemObject* d = dynamic_cast<ibTreeItemObject*>(m_compositionCtrl->GetItemData(top)))
			m_keepTopId = d->GetMetaId();
	m_keepTopNode = wxTreeItemId();

	// ONE FRAME, not a teardown followed by a re-appear. The rebuild empties the tree
	// and refills it, and without this the user watches it collapse and grow back —
	// the same pattern the form's attribute tree uses (visualEditorAttributeTree.cpp).
	// The event handler goes with it: DeleteAllItems and SelectItem raise native
	// selection and focus events, and nothing downstream wants them mid-churn.
	m_compositionCtrl->Freeze();
	m_compositionCtrl->SetEvtHandlerEnabled(false);

	ClearComposition();
	FillData(view);
	if (m_keepSelNode.IsOk())
		m_compositionCtrl->SelectItem(m_keepSelNode);
	if (m_keepTopNode.IsOk())
		m_compositionCtrl->ScrollTo(m_keepTopNode);

	m_compositionCtrl->SetEvtHandlerEnabled(true);
	m_compositionCtrl->Thaw();
}

void ibCommonAttributeCompositionEditor::InitComposition()
{
	m_compositionCtrl->GetImageList()->RemoveAll();

	// The root wears the CONFIGURATION's icon, same as the section editor's — the server's row brings it (FillData).
	m_treeMETADATA = m_compositionCtrl->AddRoot(_("Configuration"));
	m_compositionCtrl->SetItemBold(m_treeMETADATA);
}

void ibCommonAttributeCompositionEditor::ClearComposition()
{
	m_compositionCtrl->DeleteAllItems();
	InitComposition();
}

wxTreeItemId ibCommonAttributeCompositionEditor::GroupFor(const ibProtocolNode& row)
{
	// The icon and the caption the server's row carries (ibMetaGroupCaption there) — the one the configuration tree
	// shows.
	wxImageList* imageList = m_compositionCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId group = m_compositionCtrl->AppendItem(m_treeMETADATA,
		row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = group;
	return group;
}

wxTreeItemId ibCommonAttributeCompositionEditor::AppendItem(const wxTreeItemId& parent, const ibProtocolNode& row)
{
	wxImageList* imageList = m_compositionCtrl->GetImageList();
	wxASSERT(imageList);
	const wxBitmap picture = ibProtocolPicture(row, ibProtocol::ibView::ibCheckTree::Picture);
	const int imageIndex = picture.IsOk() ? imageList->Add(picture) : wxNOT_FOUND;

	const wxTreeItemId createItem = m_compositionCtrl->AppendItem(
		parent, row.GetString(ibProtocol::ibView::ibCheckTree::Title), imageIndex, imageIndex, new ibTreeItemObject(row.GetId()));

	const bool carried = row.GetBool(ibProtocol::ibView::ibCheckTree::Checked);
	const bool editable = !row.GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly);

	m_compositionCtrl->SetItemState(createItem,
		carried
		? (editable ? ibCheckTree::CHECKED : ibCheckTree::CHECKED_DISABLED)
		: (editable ? ibCheckTree::UNCHECKED : ibCheckTree::UNCHECKED_DISABLED)
	);

	if (row.GetId() == m_keepSelId)
		m_keepSelNode = createItem;
	if (row.GetId() == m_keepTopId)
		m_keepTopNode = createItem;

	return createItem;
}

void ibCommonAttributeCompositionEditor::FillData(const ibProtocolNode& view)
{
	const ibProtocolNode root = view.FindChild(ibProtocol::ibView::ibCheckTree::Rows);

	// The rows' pictures asked of the server at once — not a call per row drawn.
	ibProtocolPictureReceive(root);

	wxImageList* imageList = m_compositionCtrl->GetImageList();
	const wxBitmap picture = ibProtocolPicture(root, ibProtocol::ibView::ibCheckTree::Picture);
	if (picture.IsOk()) {
		const int imageIndex = imageList->Add(picture);
		m_compositionCtrl->SetItemImage(m_treeMETADATA, imageIndex);
		m_compositionCtrl->SetItemImage(m_treeMETADATA, imageIndex, wxTreeItemIcon_Selected);
	}
	m_compositionCtrl->SetItemText(m_treeMETADATA, root.GetString(ibProtocol::ibView::ibCheckTree::Title));
	m_compositionCtrl->SetItemData(m_treeMETADATA, new ibTreeItemObject(root.GetId()));

	// ASKED, NOT LISTED — on the server: every metaobject that says it can carry one, under a group named by its own
	// metatype (a group row: below 0); one of a metatype with no group under the configuration's row.
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

	m_compositionCtrl->ExpandAll();
	SetReadOnly(view.FindChild(ibProtocol::ibView::State).GetBool(ibProtocol::ibView::ibCheckTree::ReadOnly));
}
