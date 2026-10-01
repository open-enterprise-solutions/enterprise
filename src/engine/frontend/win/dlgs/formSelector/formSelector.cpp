////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : form selector
////////////////////////////////////////////////////////////////////////////

#include "formSelector.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "frontend/mainFrame/mainFrame.h"

ibDialogSelectTypeForm::ibDialogSelectTypeForm(ibValueMetaObject* metaValue, ibValueMetaObjectFormBase* metaObject)
	: wxDialog(ibFrontendMainFrame::GetFrame(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxDIALOG_ADAPTATION_ANY_SIZER), m_metaObject(metaObject)
{
	SetTitle(metaValue->GetSynonym() + _(" form wizard"));
	SetClientSize(FromDIP(wxSize(480, 320)));
}

ibDialogSelectTypeForm::~ibDialogSelectTypeForm()
{
}

void ibDialogSelectTypeForm::CreateSelector()
{
	wxBoxSizer* bSizerMain = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer* sbSizerMain = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, _("Select form type")), wxVERTICAL);

	for (auto choice : m_listChoice) {
		wxRadioButton* radioButton = new wxRadioButton(sbSizerMain->GetStaticBox(), choice.m_value, choice.m_label, wxDefaultPosition, wxDefaultSize);
		radioButton->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnFormTypeChanged), nullptr, this);
		sbSizerMain->Add(radioButton, 1, wxALL | wxEXPAND, FromDIP(5));
	}

	wxRadioButton* radioButton = new wxRadioButton(sbSizerMain->GetStaticBox(), defaultFormType, _("Generic form"), wxDefaultPosition, wxDefaultSize);
	radioButton->Connect(wxEVT_RADIOBUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnFormTypeChanged), nullptr, this);
	sbSizerMain->Add(radioButton, 1, wxALL | wxEXPAND, FromDIP(5));

	bSizerMain->Add(sbSizerMain, 1, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* bSizerLeft = new wxBoxSizer(wxVERTICAL);

	m_staticTextName = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextName->Wrap(-1);

	bSizerLeft->Add(m_staticTextName, 0, wxALL, FromDIP(5));

	m_staticTextSynonym = new wxStaticText(this, wxID_ANY, _("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextSynonym->Wrap(-1);

	bSizerLeft->Add(m_staticTextSynonym, 0, wxALL, FromDIP(5));

	m_staticTextComment = new wxStaticText(this, wxID_ANY, _("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_staticTextComment->Wrap(-1);

	bSizerLeft->Add(m_staticTextComment, 0, wxALL, FromDIP(5));
	bSizerHeader->Add(bSizerLeft, 0, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerRight = new wxBoxSizer(wxVERTICAL);

	m_textCtrlName = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetName(), wxDefaultPosition, wxDefaultSize, 0);
	m_textCtrlName->Connect(wxEVT_TEXT, wxCommandEventHandler(ibDialogSelectTypeForm::OnTextEnter), nullptr, this);
	bSizerRight->Add(m_textCtrlName, 0, wxALL | wxEXPAND, 1);

	m_textCtrlSynonym = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetSynonym(), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlSynonym, 0, wxALL | wxEXPAND, 1);

	m_textCtrlComment = new wxTextCtrl(this, wxID_ANY, m_metaObject->GetComment(), wxDefaultPosition, wxDefaultSize, 0);
	bSizerRight->Add(m_textCtrlComment, 0, wxALL | wxEXPAND, 1);

	bSizerHeader->Add(bSizerRight, 1, wxEXPAND, FromDIP(5));
	bSizerMain->Add(bSizerHeader, 1, wxEXPAND, FromDIP(10));

	wxBoxSizer* bSizerBottom = new wxBoxSizer(wxHORIZONTAL);

	m_sdbSizerOK = new wxButton(this, wxID_ANY, _("OK"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerOK->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnButtonOk), nullptr, this);

	bSizerBottom->Add(m_sdbSizerOK, 0, wxALL, FromDIP(5));

	m_sdbSizerCancel = new wxButton(this, wxID_ANY, _("Cancel"), wxDefaultPosition, wxDefaultSize, 0);
	m_sdbSizerCancel->Connect(wxEVT_BUTTON, wxCommandEventHandler(ibDialogSelectTypeForm::OnButtonCancel), nullptr, this);
	bSizerBottom->Add(m_sdbSizerCancel, 0, wxALL, FromDIP(5));

	bSizerMain->Add(bSizerBottom, 0, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(bSizerMain);
	wxDialog::Layout();
}

void ibDialogSelectTypeForm::OnTextEnter(wxCommandEvent& event)
{
	wxString systemName =
		m_textCtrlName->GetValue();

	int pos = stringUtils::CheckCorrectName(systemName);
	if (pos > 0) {
		systemName = systemName.Left(pos);
		m_textCtrlName->SetValue(systemName);
		return;
	}

	wxString newSynonym = stringUtils::GenerateSynonym(systemName);
	m_textCtrlSynonym->SetValue(newSynonym);
	event.Skip();
}

void ibDialogSelectTypeForm::OnButtonOk(wxCommandEvent& event)
{
	m_metaObject->SetName(m_textCtrlName->GetValue());
	m_metaObject->SetSynonym(m_textCtrlSynonym->GetValue());
	m_metaObject->SetComment(m_textCtrlComment->GetValue());

	EndModal(m_choice);
	event.Skip();
}

void ibDialogSelectTypeForm::OnButtonCancel(wxCommandEvent& event)
{
	EndModal(wxNOT_FOUND);
	event.Skip();
}

#include "backend/metaData.h"

void ibDialogSelectTypeForm::OnFormTypeChanged(wxCommandEvent& event)
{
	ibMetaData* metaData = m_metaObject->GetMetaData();

	unsigned int choice = event.GetId();
	auto founded_choice = std::find_if(m_listChoice.begin(), m_listChoice.end(),
		[choice](auto& v) {return choice == v.m_value; }
	);

	m_choice = choice;

	if (founded_choice != m_listChoice.end()) {
		const wxString& newName = metaData->GetNewName(
			m_metaObject->GetClassType(),
			m_metaObject->GetParent(),
			founded_choice->m_name,
			true
		);
		m_textCtrlName->SetValue(newName);
		m_textCtrlSynonym->SetValue(
			stringUtils::GenerateSynonym(newName)
		);
	}
	else {
		const wxString& newName = metaData->GetNewName(
			m_metaObject->GetClassType(),
			m_metaObject->GetParent(),
			formDefaultName,
			true
		);
		m_textCtrlName->SetValue(newName);
		m_textCtrlSynonym->SetValue(
			stringUtils::GenerateSynonym(newName)
		);
	}

	event.Skip();
}