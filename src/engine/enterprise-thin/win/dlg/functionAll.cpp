#include "functionAll.h"

#include <wx/imaglist.h>
#include <wx/sizer.h>

#include "protocol/protocol.h"   // ibProtocolName

#include "frmclient/win/picture.h"

#define ICON_SIZE 16

// The item a tree line opens — the schema's Item.
class ibFunctionItem : public wxTreeItemData {
	long long m_item;
public:
	explicit ibFunctionItem(long long item) : m_item(item) {}
	long long GetItem() const { return m_item; }
};

ibDialogFunctionAll::ibDialogFunctionAll(wxWindow* parent, const ibProtocolNode& shown, Open open,
	wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
	: wxDialog(parent, id, title, pos, size, style), m_open(std::move(open))
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
	m_treeCtrlElements = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_TWIST_BUTTONS);
	m_treeCtrlElements->SetDoubleBuffered(true);
	bSizer->Add(m_treeCtrlElements, 1, wxALL | wxEXPAND, FromDIP(5));

	m_treeCtrlElements->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogFunctionAll::OnTreeCtrlElementsItemActivated, this);

	m_treeCtrlElements->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE)
	);

	wxDialog::SetSizer(bSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);

	BuildTree(shown);
	wxDialog::SetFocus();
}

void ibDialogFunctionAll::BuildTree(const ibProtocolNode& shown)
{
	wxImageList* imageList = m_treeCtrlElements->GetImageList();
	wxASSERT(imageList);

	// A picture into the tree's list — or none, when it does not read.
	const auto imageOf = [imageList](const wxString& icon) {
		const wxBitmap picture = ibProtocolPicture(icon);
		return picture.IsOk() ? imageList->Add(picture) : -1;
	};

	wxTreeItemId root = m_treeCtrlElements->AddRoot(wxEmptyString);
	for (const ibProtocolNode& group : shown.FindChild(ibProtocolName::Groups).Children()) {
		const int groupImage = imageOf(group.GetString(ibProtocolName::Icon));
		wxTreeItemId groupItem = m_treeCtrlElements->AppendItem(root, group.GetString(ibProtocolName::Title), groupImage, groupImage);
		for (const ibProtocolNode& item : group.Children()) {
			const int itemImage = imageOf(item.GetString(ibProtocolName::Icon));
			m_treeCtrlElements->AppendItem(groupItem, item.GetString(ibProtocolName::Title), itemImage, itemImage,
				new ibFunctionItem(item.GetInt(ibProtocolName::Item)));
		}
	}

	m_treeCtrlElements->ExpandAll();
}

void ibDialogFunctionAll::OnTreeCtrlElementsItemActivated(wxTreeEvent& event)
{
	const wxTreeItemId& selItem = event.GetItem();
	if (!selItem.IsOk())
		return;

	// Opened — the dialog's work is done. It is modeless, and a modeless dialog's close only hides it: it is destroyed.
	const ibFunctionItem* itemData = dynamic_cast<ibFunctionItem*>(m_treeCtrlElements->GetItemData(selItem));
	if (itemData != nullptr && m_open && m_open(itemData->GetItem())) {
		Destroy();
		return;
	}

	event.Skip();
}
