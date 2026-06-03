#include "generation.h"
#include "backend/metaData.h"
#include "frontend/mainFrame/mainFrame.h"

bool ibDialogGeneration::ShowModal(ibMetaID& id)
{
	const int res = wxDialog::ShowModal();
	if (res == wxID_OK) {
		const long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (lSelectedItem != wxNOT_FOUND) {
			id = m_metaDesc.GetByIdx((size_t)lSelectedItem);
			wxDialog::Destroy();
			return true;
		}

	}
	wxDialog::Destroy();
	return false;
}

#define ICON_SIZE 16

ibDialogGeneration::ibDialogGeneration(const ibMetaData* metaData, const ibMetaDescription& metaDesc) :
	wxDialog(ibFrontendDocMDIFrame::GetFrame(), wxID_ANY, _("Select generation"), wxDefaultPosition, wxSize(315, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_metaDesc(metaDesc)
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

	for (unsigned int idx = 0; idx < m_metaDesc.GetTypeCount(); idx++) {
		const ibValueMetaObject* typeCtor = metaData->FindAnyObjectByFilter(m_metaDesc.GetByIdx(idx));
		wxASSERT(typeCtor);
		wxImageList* imageList = m_listData->GetImageList(wxIMAGE_LIST_SMALL);
		long lSelectedItem = m_listData->InsertItem(m_listData->GetItemCount(), typeCtor->GetSynonym(), imageList->Add(typeCtor->GetIcon()));
		m_listData->SetItemData(lSelectedItem, typeCtor->GetMetaID());
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

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picGenerateCLSID));

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();
	wxDialog::SetIcon(dlg_icon);

	wxDialog::Centre(wxBOTH);
}

ibDialogGeneration::~ibDialogGeneration()
{
}

void ibDialogGeneration::OnListItemSelected(wxListEvent& event)
{
	EndModal(wxID_OK);
}
