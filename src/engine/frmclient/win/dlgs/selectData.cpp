#include "selectData.h"

#include <wx/imaglist.h>

bool ibDialogSelectDataType::ShowModal(int& chosen)
{
	if (m_listData->GetItemCount() == 0) {
		return false;
	}
	else if (m_listData->GetItemCount() == 1) {
		chosen = 0;
		return true;
	}

	int res = ibDialog::ShowModal();
	if (res == wxID_OK) {
		long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (lSelectedItem != wxNOT_FOUND) {
			chosen = static_cast<int>(lSelectedItem);
			return true;
		}
	}
	return false;
}

#define ICON_SIZE 16

ibDialogSelectDataType::ibDialogSelectDataType(wxWindow* parent, const wxString& caption, const wxArrayString& captions, const std::vector<wxBitmap>& pictures) :
	ibDialog(parent, wxID_ANY, caption, wxDefaultPosition, wxSize(315, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	ibDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

	m_listData = new ibListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxLC_LIST);
	m_listData->AppendColumn(wxT("type"), wxLIST_FORMAT_LEFT, 300);
	mainSizer->Add(m_listData, 1, wxALL | wxEXPAND, FromDIP(5));

	// Make an state image list containing small icons
	m_listData->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE), wxIMAGE_LIST_SMALL
	);

	// An item with no picture (none sent, none the client holds) stands by its name alone; one of another size is fitted —
	// the image list cuts a wider one into several, and the items after it would show their neighbours' pictures.
	wxImageList* imageList = m_listData->GetImageList(wxIMAGE_LIST_SMALL);
	for (size_t idx = 0; idx < captions.size(); idx++) {
		wxBitmap picture = idx < pictures.size() ? pictures[idx] : wxNullBitmap;
		if (picture.IsOk() && picture.GetSize() != wxSize(ICON_SIZE, ICON_SIZE))
			wxBitmap::Rescale(picture, wxSize(ICON_SIZE, ICON_SIZE));
		m_listData->InsertItem(static_cast<long>(idx), captions[idx], picture.IsOk() ? imageList->Add(picture) : -1);
	}

	// A click picks an item, OK chooses it — or a double click, Enter, on it (ibSingleChoiceDialog's way); the first stands
	// picked, so OK alone takes it.
	if (m_listData->GetItemCount() > 0)
		m_listData->SetItemState(0, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	m_listData->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibDialogSelectDataType::OnListItemActivated, this);

	wxBoxSizer* buttonsSizer = new wxBoxSizer(wxVERTICAL);
	m_buttonOk = new ibButton(this, wxID_OK, _("Ok"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonOk, 0, wxALL, FromDIP(5));
	m_buttonCancel = new ibButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonCancel, 0, wxALL, FromDIP(5));

	mainSizer->Add(buttonsSizer, 0, wxEXPAND, FromDIP(5));

	ibDialog::SetSizer(mainSizer);
	ibDialog::Layout();

	ibDialog::Centre(wxBOTH);
}

ibDialogSelectDataType::~ibDialogSelectDataType()
{
}

void ibDialogSelectDataType::OnListItemActivated(wxListEvent& event)
{
	EndModal(wxID_OK);
}
