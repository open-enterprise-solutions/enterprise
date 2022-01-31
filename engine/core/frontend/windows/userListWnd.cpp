#include "userListWnd.h"
#include "databaseLayer/databaseLayer.h"
#include "metadata/metadata.h"
#include "appData.h"

#define colUserName 1
#define colUserFullName 2

class CUserListModel : public wxDataViewIndexListModel {
	wxDECLARE_NO_COPY_CLASS(CUserListModel);
public:

	CUserListModel() {
		PrepareData();
	}

	void PrepareData() {
		m_aUsersData.clear();
		DatabaseResultSet *resultSet = 
			databaseLayer->RunQueryWithResults("SELECT guid FROM %s;", IConfigMetadata::GetUsersTableName());
		while (resultSet->Next()) {
			m_aUsersData.push_back(
				resultSet->GetResultString("guid")
			);
		}
		resultSet->Close();
		Reset(m_aUsersData.size());
	}

	void DeleteRow(const wxDataViewItem &item) {
		unsigned int row = GetRow(item);
		databaseLayer->RunQuery("DELETE FROM %s WHERE guid = '%s';", IConfigMetadata::GetUsersTableName(), m_aUsersData[row].str());
		m_aUsersData.erase(m_aUsersData.begin() + row);
	}

	Guid GetGuidByRow(const wxDataViewItem &item) const {
		return m_aUsersData.at(GetRow(item));
	}

	// Implement base class pure virtual methods.
	unsigned GetColumnCount() const override { return 2; }
	wxString GetColumnType(unsigned) const override { return wxT("string"); }
	unsigned GetCount() const override { return m_aUsersData.size(); }

	void GetValueByRow(wxVariant& val, unsigned row, unsigned col) const override {
		DatabaseResultSet *resultSet =
			databaseLayer->RunQueryWithResults("SELECT name, fullName FROM %s WHERE guid = '%s';", IConfigMetadata::GetUsersTableName(), m_aUsersData[row].str());
		if (resultSet->Next()) {
			if (col == colUserName) {
				val = resultSet->GetResultString("name");
			}
			else if (col == colUserFullName) {
				val = resultSet->GetResultString("fullName");
			}
		}
		resultSet->Close();
	}

	bool SetValueByRow(const wxVariant&, unsigned, unsigned) override {
		return false;
	}

private:
	std::vector<Guid> m_aUsersData;
};

enum {
	wxID_USERS_TOOL_ADD = wxID_HIGHEST + 1,
	wxID_USERS_TOOL_COPY,
	wxID_USERS_TOOL_EDIT,
	wxID_USERS_TOOL_DELETE,
};

CUsersListWnd::CUsersListWnd(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : wxDialog(parent, id, title, pos, size, style)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* sizerList = new wxBoxSizer(wxVERTICAL);

	m_auiToolBarUsers = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_TB_HORZ_TEXT);
	m_auiToolBarUsers->SetArtProvider(new wxAuiGenericToolBarArt());

	m_toolAdd = m_auiToolBarUsers->AddTool(wxID_USERS_TOOL_ADD, _("add"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);
	m_toolCopy = m_auiToolBarUsers->AddTool(wxID_USERS_TOOL_COPY, _("copy"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);
	m_toolEdit = m_auiToolBarUsers->AddTool(wxID_USERS_TOOL_EDIT, _("edit"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);
	m_toolDelete = m_auiToolBarUsers->AddTool(wxID_USERS_TOOL_DELETE, _("delete"), wxNullBitmap, wxNullBitmap, wxITEM_NORMAL, wxEmptyString, wxEmptyString, NULL);

	m_auiToolBarUsers->Realize();
	m_auiToolBarUsers->Connect(wxEVT_MENU, wxCommandEventHandler(CUsersListWnd::OnCommandMenu), NULL, this);

	sizerList->Add(m_auiToolBarUsers, 0, wxALL | wxEXPAND, 0);

	wxDataViewColumn *columnName = new wxDataViewColumn(_("Name"), new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT), colUserName, FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);
	wxDataViewColumn *columnFullName = new wxDataViewColumn(_("Full name"), new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_INERT), colUserFullName, FromDIP(200), wxALIGN_LEFT,
		wxDATAVIEW_COL_SORTABLE);

	m_dataViewUsers = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0);
	m_dataViewUsers->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &CUsersListWnd::OnItemActivated, this);
	m_dataViewUsers->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &CUsersListWnd::OnContextMenu, this);

	m_dataViewUsers->Bind(wxEVT_MENU, &CUsersListWnd::OnCommandMenu, this);

	m_dataViewUsers->AppendColumn(columnName);
	m_dataViewUsers->AppendColumn(columnFullName);

	m_dataViewUsers->AssociateModel(new CUserListModel);

	sizerList->Add(m_dataViewUsers, 1, wxALL | wxEXPAND, 5);

	this->SetSizer(sizerList);
	this->Layout();
	this->Centre(wxBOTH);
}

CUsersListWnd::~CUsersListWnd()
{
}

#include "userWnd.h"

void CUsersListWnd::OnContextMenu(wxDataViewEvent &event)
{
	wxMenu menu;

	for (unsigned int idx = 0; idx < m_auiToolBarUsers->GetToolCount(); idx++) {
		wxAuiToolBarItem *tool = m_auiToolBarUsers->FindToolByIndex(idx);
		if (tool) {
			menu.Append(tool->GetId(), tool->GetLabel());
		}
	}

	wxDataViewCtrl *wnd = wxDynamicCast(
		event.GetEventObject(), wxDataViewCtrl
	);

	wxASSERT(wnd);
	wnd->PopupMenu(&menu);
}

void CUsersListWnd::OnItemActivated(wxDataViewEvent &event)
{
	CUserWnd *userWnd = new CUserWnd(this, wxID_ANY);
	CUserListModel *model =
		dynamic_cast<CUserListModel *>(m_dataViewUsers->GetModel());
	userWnd->ReadUserData(model->GetGuidByRow(event.GetItem()));
	userWnd->ShowModal();
	event.Skip();
}

void CUsersListWnd::OnCommandMenu(wxCommandEvent &event)
{
	wxDataViewItem sel = m_dataViewUsers->GetSelection();

	if (event.GetId() == wxID_USERS_TOOL_ADD) {
		CUserWnd *userWnd = new CUserWnd(this, wxID_ANY);
		userWnd->ShowModal();
	}

	CUserListModel *model =
		dynamic_cast<CUserListModel *>(m_dataViewUsers->GetModel());

	if (sel.IsOk()) {
		if (event.GetId() == wxID_USERS_TOOL_EDIT) {
			CUserWnd *userWnd = new CUserWnd(this, wxID_ANY);
			userWnd->ReadUserData(model->GetGuidByRow(sel));
			userWnd->ShowModal();
		}

		if (event.GetId() == wxID_USERS_TOOL_COPY) {
			CUserWnd *userWnd = new CUserWnd(this, wxID_ANY);
			userWnd->ReadUserData(model->GetGuidByRow(sel), true);
			userWnd->ShowModal();
		}

		if (event.GetId() == wxID_USERS_TOOL_DELETE) {
			model->DeleteRow(sel);
		}
	}

	model->PrepareData(); event.Skip();
}
