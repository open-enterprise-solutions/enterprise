#include "selectData.h"

#include "backend/metaData.h"
#include "frontend/mainFrame/mainFrame.h"

bool CDialogSelectDataType::ShowModal(class_identifier_t& clsid)
{
	if (m_listData->GetItemCount() == 0) {
		return false; 
	}
	else if (m_listData->GetItemCount() == 1) {
		long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_DONTCARE);
		if (lSelectedItem != wxNOT_FOUND) {
			clsid = m_clsids.at(lSelectedItem);
			return true;
		}
		return true;
	}

	int res = wxDialog::ShowModal();
	if (res == wxID_OK) {
		long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (lSelectedItem != wxNOT_FOUND) {
			clsid = m_clsids.at(lSelectedItem);
			return true;
		}
	}
	return false;
}

#define ICON_SIZE 16

CDialogSelectDataType::CDialogSelectDataType(IMetaData* metaData, std::set<class_identifier_t>& clsids) :
	wxDialog(CDocMDIFrame::GetFrame(), wxID_ANY, _("Select data type"), wxDefaultPosition, wxSize(315, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

	m_listData = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_NO_HEADER | wxLC_SINGLE_SEL | wxLC_LIST);
	m_listData->AppendColumn(wxT("type"), wxLIST_FORMAT_LEFT, 300);
	mainSizer->Add(m_listData, 1, wxALL | wxEXPAND, 5);

	// Make an state image list containing small icons
	m_listData->AssignImageList(
		new wxImageList(ICON_SIZE, ICON_SIZE), wxIMAGE_LIST_SMALL
	);

	for (auto clsid : clsids) {
		if (metaData->IsRegisterCtor(clsid)) {
			IAbstractTypeCtor* typeCtor = metaData->GetAvailableCtor(clsid);
			wxASSERT(typeCtor);
			wxImageList* imageList = m_listData->GetImageList(wxIMAGE_LIST_SMALL);
			long lSelectedItem = m_listData->InsertItem(m_listData->GetItemCount(), typeCtor->GetClassName(), imageList->Add(typeCtor->GetClassIcon()));
			m_clsids.insert_or_assign(lSelectedItem, clsid);
		}
	}

	// Connect Events
	m_listData->Connect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(CDialogSelectDataType::OnListItemSelected), nullptr, this);

	m_listData->SetDoubleBuffered(true);

	wxBoxSizer* buttonsSizer = new wxBoxSizer(wxVERTICAL);
	m_buttonOk = new wxButton(this, wxID_OK, _("Ok"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonOk, 0, wxALL, 5);
	m_buttonCancel = new wxButton(this, wxID_CANCEL, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	buttonsSizer->Add(m_buttonCancel, 0, wxALL, 5);

	mainSizer->Add(buttonsSizer, 0, wxEXPAND, 5);

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();

	wxDialog::Centre(wxBOTH);
}

CDialogSelectDataType::~CDialogSelectDataType()
{
}

void CDialogSelectDataType::OnListItemSelected(wxListEvent& event)
{
	EndModal(wxID_OK);
}
