////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form selector
////////////////////////////////////////////////////////////////////////////

#include "formSelector.h"
#include "metadata/metaObjects/metaFormObject.h"
#include "metadata/objects/baseObject.h"
#include "frontend/mainFrame.h"

CSelectTypeForm::CSelectTypeForm(IMetaObjectValue *metaValue, IMetaFormObject *metaObject)
	: wxDialog(CMainFrame::Get(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(480, 320), wxDEFAULT_DIALOG_STYLE | wxDIALOG_ADAPTATION_ANY_SIZER), m_metaObject(metaObject)
{
	SetTitle(_(metaValue->GetClassName() + " form wizard"));
}

CSelectTypeForm::~CSelectTypeForm()
{
}

void CSelectTypeForm::CreateSelector()
{
	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbSizerMain = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, wxT("Select form type")), wxVERTICAL);

	for (auto choice : m_aChoices) {
		wxRadioButton *radioButton = new wxRadioButton(sbSizerMain->GetStaticBox(), choice.m_choice, choice.m_name, wxDefaultPosition, wxDefaultSize);
		radioButton->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(CSelectTypeForm::OnFormTypeChanged), NULL, this);
		sbSizerMain->Add(radioButton, 1, wxALL | wxEXPAND, 5);
	}

	wxRadioButton *radioButton = new wxRadioButton(sbSizerMain->GetStaticBox(), defaultFormType, _("Generic form"), wxDefaultPosition, wxDefaultSize);
	radioButton->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(CSelectTypeForm::OnFormTypeChanged), NULL, this);
	sbSizerMain->Add(radioButton, 1, wxALL | wxEXPAND, 5);

	bSizerMain->Add(sbSizerMain, 1, wxEXPAND, 5);

	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizerLeft = new wxBoxSizer(wxVERTICAL);

	m_staticTextName = new wxStaticText(this, wxID_ANY, wxT("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextName->Wrap(-1);

	bSizerLeft->Add(m_staticTextName, 0, wxALL, 5);

	m_staticTextSynonym = new wxStaticText(this, wxID_ANY, wxT("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextSynonym->Wrap(-1);

	bSizerLeft->Add(m_staticTextSynonym, 0, wxALL, 5);

	m_staticTextComment = new wxStaticText(this, wxID_ANY, wxT("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextComment->Wrap(-1);

	bSizerLeft->Add(m_staticTextComment, 0, wxALL, 5);
	bSizerHeader->Add(bSizerLeft, 0, wxEXPAND, 5);

	wxBoxSizer* bSizerRight = new wxBoxSizer(wxVERTICAL);

	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetName(), wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlName->Connect(wxEVT_TEXT, wxCommandEventHandler(CSelectTypeForm::OnTextEnter), NULL, this);
	bSizerRight->Add(m_textCtrlName, 0, wxALL | wxEXPAND, 1);

	m_textCtrlSynonym = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetSynonym(), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlSynonym, 0, wxALL | wxEXPAND, 1);

	m_textCtrlComment = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetComment(), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlComment, 0, wxALL | wxEXPAND, 1);

	bSizerHeader->Add(bSizerRight, 1, wxEXPAND, 5);
	bSizerMain->Add(bSizerHeader, 1, wxEXPAND, 10);

	wxBoxSizer* bSizerBottom = new wxBoxSizer(wxHORIZONTAL);

	m_sdbSizerOK = new wxButton(this, wxID_ANY, wxT("OK"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerOK->Connect(wxEVT_BUTTON, wxCommandEventHandler(CSelectTypeForm::OnButtonOk), NULL, this);

	bSizerBottom->Add(m_sdbSizerOK, 0, wxALL, 5);

	m_sdbSizerCancel = new wxButton(this, wxID_ANY, wxT("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerCancel->Connect(wxEVT_BUTTON, wxCommandEventHandler(CSelectTypeForm::OnButtonCancel), NULL, this);
	bSizerBottom->Add(m_sdbSizerCancel, 0, wxALL, 5);

	bSizerMain->Add(bSizerBottom, 0, wxEXPAND, 5);

	this->SetSizer(bSizerMain);
	this->Layout();
}

#include "utils/stringUtils.h"

void CSelectTypeForm::OnTextEnter(wxCommandEvent &event)
{
	wxString systemName = 
		m_textCtrlName->GetValue(); 

	int pos = StringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_textCtrlName->SetValue(systemName);
		return;
	}

	wxString newSynonym = StringUtils::GenerateSynonym(systemName);
	m_textCtrlSynonym->SetValue(newSynonym);
	event.Skip();
}

void CSelectTypeForm::OnButtonOk(wxCommandEvent &event)
{
	m_metaObject->SetName(m_textCtrlName->GetValue());
	m_metaObject->SetSynonym(m_textCtrlSynonym->GetValue());
	m_metaObject->SetComment(m_textCtrlComment->GetValue());

	EndModal(m_choice);
	event.Skip();
}

void CSelectTypeForm::OnButtonCancel(wxCommandEvent & event)
{
	EndModal(wxID_CANCEL);
	event.Skip();
}

#include "metadata/metadata.h"

void CSelectTypeForm::OnFormTypeChanged(wxCommandEvent &event)
{
	m_choice = event.GetId();

	if ((m_choice - 1) < m_aChoices.size()) {
		auto choiceData = m_aChoices.at(m_choice - 1);
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxString newName = metaData->GetNewName(
			m_metaObject->GetClsid(),
			m_metaObject->GetParent(),
			choiceData.m_name,
			true
		);
		m_textCtrlName->SetValue(newName);
		wxString newSynonym = StringUtils::GenerateSynonym(newName);
		m_textCtrlSynonym->SetValue(newSynonym);
	}
	else {
		IMetadata *metaData = m_metaObject->GetMetadata();
		wxString newName = metaData->GetNewName(
			m_metaObject->GetClsid(),
			m_metaObject->GetParent(),
			formDefaultName,
			true
		);
		m_textCtrlName->SetValue(newName);
		wxString newSynonym = StringUtils::GenerateSynonym(newName);
		m_textCtrlSynonym->SetValue(newSynonym);
	}

	event.Skip();
}