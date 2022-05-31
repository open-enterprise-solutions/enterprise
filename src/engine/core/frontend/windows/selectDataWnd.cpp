#include "selectDataWnd.h"
#include "metadata/metadata.h"
#include "frontend/mainFrame.h"

bool CSelectDataTypeWnd::ShowModal(CLASS_ID& clsid)
{
	int res = wxDialog::ShowModal();
	if (res == wxID_OK) {
		long lSelectedItem = m_listData->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (lSelectedItem != wxNOT_FOUND) {
			auto foundedIt = m_clsids.begin();
			std::advance(foundedIt, (size_t)lSelectedItem);
			clsid = *foundedIt;
			return true;
		}
	}
	return false;
}

#define ICON_SIZE 16

CSelectDataTypeWnd::CSelectDataTypeWnd(IMetadata* metaData, std::set<CLASS_ID>& clsids) :
	wxDialog(CMainFrame::Get(), wxID_ANY, _("Select data type"), wxDefaultPosition, wxSize(315, 300), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), m_clsids(clsids)
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
		IObjectValueAbstract* singleObject = metaData->GetAvailableObject(clsid);
		wxASSERT(singleObject);
		wxImageList* imageList = m_listData->GetImageList(wxIMAGE_LIST_SMALL);
		long lSelectedItem = m_listData->InsertItem(m_listData->GetItemCount(), singleObject->GetClassName(), imageList->Add(singleObject->GetClassIcon()));
		m_listData->SetItemData(lSelectedItem, clsid);
	}

	// Connect Events
	m_listData->Connect(wxEVT_COMMAND_LIST_ITEM_SELECTED, wxListEventHandler(CSelectDataTypeWnd::OnListItemSelected), NULL, this);

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

CSelectDataTypeWnd::~CSelectDataTypeWnd()
{
}

void CSelectDataTypeWnd::OnListItemSelected(wxListEvent& event)
{
	EndModal(wxID_OK);
}
