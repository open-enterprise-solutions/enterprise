#include "userWnd.h"
#include "databaseLayer/databaseLayer.h"
#include "metadata/metadata.h"
#include "utils/fs/fs.h"
#include "appData.h"

#include <wx/base64.h>
#include "email/utils/wxmd5.hpp"

void CUserWnd::OnPasswordText(wxCommandEvent &event)
{
	if (m_bInitialized) {
		wxString newPassword = m_textPassword->GetValue();
		if (!newPassword.IsEmpty()) {
			md5Password = wxMD5::ComputeMd5(newPassword);
		}
		else {
			md5Password = wxEmptyString;
		}
	}
	event.Skip();
}

void CUserWnd::OnOKButtonClick(wxCommandEvent &event)
{
	if (m_textName->IsEmpty()) {
		return; 
	}

	PreparedStatement *prepStatement =
		databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s (guid, name, fullName, changed, dataSize, binaryData) VALUES(?, ?, ?, ?, ?, ?) MATCHING (guid);", IConfigMetadata::GetUsersTableName());

	if (prepStatement) {
		if (!m_userGuid.isValid()) {
			m_userGuid = Guid::newGuid();
		}
		prepStatement->SetParamString(1, m_userGuid.str());
		prepStatement->SetParamString(2, m_textName->GetValue());
		prepStatement->SetParamString(3, m_textFullName->GetValue());
		prepStatement->SetParamDate(4, wxDateTime::Now());

		CMemoryWriter writter;
		writter.w_stringZ(md5Password);

		prepStatement->SetParamNumber(5, writter.size());
		prepStatement->SetParamBlob(6, writter.pointer(), writter.size());

		prepStatement->RunQuery();
		prepStatement->Close();
	}

	event.Skip();
}

void CUserWnd::OnCancelButtonClick(wxCommandEvent &event)
{
	event.Skip();
}

bool CUserWnd::ReadUserData(const Guid &guid, bool copy)
{
	if (m_userGuid.isValid()) {
		return false;
	}
	DatabaseResultSet *resultSet = databaseLayer->RunQueryWithResults("SELECT * FROM %s WHERE guid = '%s';", IConfigMetadata::GetUsersTableName(), guid.str());
	if (resultSet->Next()) {
		m_textName->SetValue(resultSet->GetResultString("name"));
		m_textFullName->SetValue(resultSet->GetResultString("fullName"));
		wxMemoryBuffer buffer;
		resultSet->GetResultBlob("binaryData", buffer);
		CMemoryReader reader(buffer.GetData(), buffer.GetDataLen());
		reader.r_stringZ(md5Password);
		if (!md5Password.IsEmpty()) {
			m_bInitialized = false;
			m_textPassword->SetValue(
				wxT("12345678")
			);
			m_bInitialized = true;
		}
	}
	resultSet->Close();
	if (!copy) {
		m_userGuid = guid;
	}
	return true;
}

CUserWnd::CUserWnd(wxWindow * parent, wxWindowID id, const wxString & title, const wxPoint & pos, const wxSize & size, long style) :
	wxDialog(parent, id, title, pos, size, style), m_bInitialized(false)
{
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* m_mainSizer = new wxBoxSizer(wxVERTICAL);

	m_mainNotebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS);
	m_mainNotebook->SetArtProvider(new CLunaTabArt());
	m_main = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	wxBoxSizer* sizerUser = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer* m_sizerUserTop;
	m_sizerUserTop = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* m_sizerLabel = new wxBoxSizer(wxVERTICAL);

	m_staticName = new wxStaticText(m_main, wxID_ANY, wxT("Name:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticName->Wrap(-1);
	m_sizerLabel->Add(m_staticName, 0, wxALL, 9);

	m_staticFullName = new wxStaticText(m_main, wxID_ANY, wxT("Full name:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticFullName->Wrap(-1);
	m_sizerLabel->Add(m_staticFullName, 0, wxALL, 9);

	m_sizerUserTop->Add(m_sizerLabel, 0, 0, 5);

	wxBoxSizer* m_sizerText = new wxBoxSizer(wxVERTICAL);
	m_textName = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_sizerText->Add(m_textName, 0, wxALL | wxEXPAND, 5);
	m_textFullName = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	m_sizerText->Add(m_textFullName, 0, wxALL | wxEXPAND, 5);
	m_sizerUserTop->Add(m_sizerText, 1, wxEXPAND, 5);
	sizerUser->Add(m_sizerUserTop, 0, wxEXPAND, 5);

	m_staticline = new wxStaticLine(m_main, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
	sizerUser->Add(m_staticline, 0, wxALL | wxEXPAND, 5);

	wxBoxSizer* m_sizerUserBottom = new wxBoxSizer(wxHORIZONTAL);
	m_staticPassword = new wxStaticText(m_main, wxID_ANY, wxT("Password:"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticPassword->Wrap(-1);
	m_sizerUserBottom->Add(m_staticPassword, 0, wxALL, 10);
	m_textPassword = new wxTextCtrl(m_main, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	m_sizerUserBottom->Add(m_textPassword, 1, wxALL, 5);

	sizerUser->Add(m_sizerUserBottom, 1, wxEXPAND, 5);

	m_main->SetSizer(sizerUser);
	m_main->Layout();
	sizerUser->Fit(m_main);
	m_mainNotebook->AddPage(m_main, _("User"), false, wxNullBitmap);
	m_other = new wxPanel(m_mainNotebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_mainNotebook->AddPage(m_other, _("Other"), true, wxNullBitmap);

	m_mainSizer->Add(m_mainNotebook, 1, wxEXPAND | wxALL, 5);

	m_bottom = new wxStdDialogButtonSizer();
	m_bottomOK = new wxButton(this, wxID_OK);
	m_bottom->AddButton(m_bottomOK);
	m_bottomCancel = new wxButton(this, wxID_CANCEL);
	m_bottom->AddButton(m_bottomCancel);
	m_bottom->Realize();

	m_mainNotebook->SetSelection(0);
	m_mainSizer->Add(m_bottom, 0, wxEXPAND, 5);

	this->SetSizer(m_mainSizer);
	this->Layout();

	this->Centre(wxBOTH);

	// Connect Events
	m_textPassword->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(CUserWnd::OnPasswordText), NULL, this);

	m_bottomOK->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CUserWnd::OnOKButtonClick), NULL, this);
	m_bottomCancel->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CUserWnd::OnCancelButtonClick), NULL, this);

	m_bInitialized = true;
}

CUserWnd::~CUserWnd()
{
	// Disconnect Events
	m_textPassword->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(CUserWnd::OnPasswordText), NULL, this);

	m_bottomOK->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CUserWnd::OnOKButtonClick), NULL, this);
	m_bottomCancel->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(CUserWnd::OnCancelButtonClick), NULL, this);
}