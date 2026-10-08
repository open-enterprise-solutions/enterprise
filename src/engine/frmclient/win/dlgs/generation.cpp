#include "generation.h"

#include "frmclient/mainFrame/mainFrame.h"
#include "protocol/protocol.h"
#include "frmclient/win/picture.h"

bool ibDialogGeneration::ShowModal(ibMetaID& id)
{
	const int res = wxDialog::ShowModal();
	if (res == wxID_OK) {
		const long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (lSelectedItem != wxNOT_FOUND) {
			id = static_cast<ibMetaID>(m_listData->GetItemData(lSelectedItem));
			return true;
		}

	}
	return false;
}

#define ICON_SIZE 16

ibDialogGeneration::ibDialogGeneration(const ibProtocolNode& request) :
	wxDialog(ibFrontendMainFrame::GetFrame(), wxID_ANY, _("Select generation"), wxDefaultPosition, wxSize(315, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

	m_listData = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxLC_LIST);
	m_listData->AppendColumn(wxT("type"), wxLIST_FORMAT_LEFT, 300);
	mainSizer->Add(m_listData, 1, wxALL | wxEXPAND, FromDIP(5));

	// Make an state image list containing small icons
	m_listData->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE), wxIMAGE_LIST_SMALL
	);

	for (const ibProtocolNode& typeCtor : request.Children()) {
		wxImageList* imageList = m_listData->GetImageList(wxIMAGE_LIST_SMALL);
		const wxBitmap icon = ibProtocolPicture(typeCtor.GetString(ibProtocolName::Picture));
		long lSelectedItem = m_listData->InsertItem(m_listData->GetItemCount(), typeCtor.GetString(ibProtocolName::Caption),
			icon.IsOk() ? imageList->Add(icon) : -1);
		m_listData->SetItemData(lSelectedItem, static_cast<long>(typeCtor.GetInt(ibProtocolName::Id)));
	}

	// Connect Events
	m_listData->Connect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(ibDialogGeneration::OnListItemSelected), nullptr, this);

	m_listData->SetDoubleBuffered(true);

	wxBoxSizer* buttonsSizer = new wxBoxSizer(wxVERTICAL);
	m_buttonOk = new wxButton(this, wxID_OK, _("Ok"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonOk, 0, wxALL, FromDIP(5));
	m_buttonCancel = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonCancel, 0, wxALL, FromDIP(5));

	mainSizer->Add(buttonsSizer, 0, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();

	const wxBitmap picture = ibProtocolPicture(request.GetString(ibProtocolName::Picture));
	if (picture.IsOk()) {
		wxIcon dlg_icon;
		dlg_icon.CopyFromBitmap(picture);
		wxDialog::SetIcon(dlg_icon);
	}

	wxDialog::Centre(wxBOTH);
}

ibDialogGeneration::~ibDialogGeneration()
{
}

void ibDialogGeneration::OnListItemSelected(wxListEvent& event)
{
	EndModal(wxID_OK);
}
