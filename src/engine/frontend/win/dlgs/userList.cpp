#include "userList.h"
#include "backend/userInfo.h"   // sys_user DAO — the frontend reads/deletes users through the backend, not raw SQL
#include "frontend/win/theme/luna_toolbarart.h"
#include "backend/metadataConfiguration.h"
#include "backend/appData.h"

#define colUserName 1
#define colUserFullName 2

class ibUserListModel : public ibDataViewIndexListModel {
	wxDECLARE_NO_COPY_CLASS(ibUserListModel);
public:

	ibUserListModel() { PrepareData(); }

	void PrepareData() {
		// One backend round-trip projects every row to a Brief; the cells then read from this cache
		// instead of firing a SELECT per cell render (the old GetValueByRow hit the DB on every paint).
		m_aUsersData = ibUserInfo::ListAll();
		Reset(m_aUsersData.size());
	}

	void DeleteRow(const ibDataViewItem& item) {
		unsigned int row = GetRow(item);
		ibUserInfo::Delete(ibGuid(m_aUsersData[row].m_strUserGuid));
		m_aUsersData.erase(m_aUsersData.begin() + row);
	}

	ibGuid GetGuidByRow(const ibDataViewItem& item) const {
		return ibGuid(m_aUsersData.at(GetRow(item)).m_strUserGuid);
	}

	// Implement base class pure virtual methods.
	unsigned GetCount() const override { return m_aUsersData.size(); }

	void GetValueByRow(wxVariant& val, unsigned row, unsigned col) const override {
		if (row >= m_aUsersData.size())
			return;
		if (col == colUserName)
			val = m_aUsersData[row].m_strUserName;
		else if (col == colUserFullName)
			val = m_aUsersData[row].m_strUserFullName;
	}

	bool SetValueByRow(const wxVariant&, unsigned, unsigned) override {
		return false;
	}

private:
	std::vector<ibUserInfo::Brief> m_aUsersData;
};

enum {
	wxID_USERS_TOOL_ADD = wxID_HIGHEST + 1,
	wxID_USERS_TOOL_COPY,
	wxID_USERS_TOOL_EDIT,
	wxID_USERS_TOOL_DELETE,
};

#include "frontend/visualView/ctrl/frame.h"

ibDialogUserList::ibDialogUserList(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue dialog

	wxBoxSizer* sizerList = new wxBoxSizer(wxVERTICAL);

	m_toolbarMain = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_toolbarMain->SetArtProvider(new wxAuiLunaToolBarArt());

	m_toolAdd = m_toolbarMain->AddTool(wxID_USERS_TOOL_ADD, _("Add"), ibBackendPicture::GetPicture(g_picAddCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolCopy = m_toolbarMain->AddTool(wxID_USERS_TOOL_COPY, _("Copy"), ibBackendPicture::GetPicture(g_picCopyCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolEdit = m_toolbarMain->AddTool(wxID_USERS_TOOL_EDIT, _("Edit"), ibBackendPicture::GetPicture(g_picEditCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);
	m_toolDelete = m_toolbarMain->AddTool(wxID_USERS_TOOL_DELETE, _("Delete"), ibBackendPicture::GetPicture(g_picDeleteCLSID), wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, nullptr);

	m_toolbarMain->SetForegroundColour(wxDefaultStypeFGColour);
	m_toolbarMain->SetBackgroundColour(wxDefaultStypeBGColour);

	m_toolbarMain->Realize();
	m_toolbarMain->Connect(wxEVT_MENU, wxCommandEventHandler(ibDialogUserList::OnCommandMenu), nullptr, this);

	sizerList->Add(m_toolbarMain, 0, wxALL | wxEXPAND, 0);

	ibDataViewColumn* columnName = new ibDataViewColumn(_("Name"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), colUserName, FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);
	ibDataViewColumn* columnFullName = new ibDataViewColumn(_("Full name"), new ibDataViewTextRenderer(wxT("string"), wxDATAVIEW_CELL_INERT), colUserFullName, FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	m_dataEditor = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
	m_dataEditor->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibDialogUserList::OnItemActivated, this);
	m_dataEditor->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogUserList::OnContextMenu, this);

	m_dataEditor->Bind(wxEVT_MENU, &ibDialogUserList::OnCommandMenu, this);

	m_dataEditor->AppendColumn(columnName);
	m_dataEditor->AppendColumn(columnFullName);

	m_dataEditor->SetForegroundColour(wxDefaultStypeFGColour);
	//m_dataEditor->SetBackgroundColour(wxDefaultStypeBGColour);

	wxItemAttr attr(
		wxDefaultStypeFGColour,
		wxDefaultStypeBGColour,
		m_dataEditor->GetFont()
	);

	m_dataEditor->SetHeaderAttr(attr);

	m_dataEditor->AssociateModel(new ibUserListModel);

	sizerList->Add(m_dataEditor, 1, wxALL | wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(sizerList);
	wxDialog::Layout();
	wxDialog::Centre(wxBOTH);

	wxIcon dlg_icon;
	dlg_icon.CopyFromBitmap(ibBackendPicture::GetPicture(g_picUserListCLSID));

	wxDialog::SetIcon(dlg_icon);
	wxDialog::SetFocus();
}

ibDialogUserList::~ibDialogUserList()
{
}

#include "userItem.h"

void ibDialogUserList::OnContextMenu(ibDataViewEvent& event)
{
	wxMenu menu;

	for (unsigned int idx = 0; idx < m_toolbarMain->GetToolCount(); idx++) {
		wxAuiToolBarItem* tool = m_toolbarMain->FindToolByIndex(idx);
		if (tool) {
			menu.Append(tool->GetId(), tool->GetLabel())->SetBitmap(tool->GetBitmapBundle());
		}
	}

	ibDataViewCtrl* wnd = wxDynamicCast(
		event.GetEventObject(), ibDataViewCtrl
	);

	wxASSERT(wnd);
	wnd->PopupMenu(&menu);
}

void ibDialogUserList::OnItemActivated(ibDataViewEvent& event)
{
	ibDialogUserItem dlg(this, wxID_ANY);

	ibUserListModel* model =
		static_cast<ibUserListModel*>(m_dataEditor->GetModel());

	dlg.ReadUserData(model->GetGuidByRow(event.GetItem()));
	dlg.ShowModal();

	event.Skip();
}

void ibDialogUserList::OnCommandMenu(wxCommandEvent& event)
{
	ibDataViewItem sel = m_dataEditor->GetSelection();

	if (event.GetId() == wxID_USERS_TOOL_ADD) {
		ibDialogUserItem dlg(this, wxID_ANY);
		dlg.ShowModal();
	}

	ibUserListModel* model =
		dynamic_cast<ibUserListModel*>(m_dataEditor->GetModel());

	if (sel.IsOk()) {
		if (event.GetId() == wxID_USERS_TOOL_EDIT) {
			ibDialogUserItem dlg(this, wxID_ANY);
			dlg.ReadUserData(model->GetGuidByRow(sel));
			dlg.ShowModal();
		}

		if (event.GetId() == wxID_USERS_TOOL_COPY) {
			ibDialogUserItem dlg(this, wxID_ANY);
			dlg.ReadUserData(model->GetGuidByRow(sel), true);
			dlg.ShowModal();
		}

		if (event.GetId() == wxID_USERS_TOOL_DELETE) {
			model->DeleteRow(sel);
		}
	}

	model->PrepareData(); event.Skip();
}
