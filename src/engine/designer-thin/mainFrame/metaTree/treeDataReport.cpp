////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : report window
////////////////////////////////////////////////////////////////////////////

#include "treeDataReport.h"

#include <wx/sizer.h>

ibDataReportTree::ibDataReportTree(wxWindow* parent, long long tab)
	: ibMetaTreeBase(parent, tab, false)
{
	wxBoxSizer* bSizerHeader = new wxBoxSizer(wxHORIZONTAL);

	wxBoxSizer* bSizerCaption = new wxBoxSizer(wxVERTICAL);

	m_nameCaption = new wxStaticText(this, wxID_ANY, _("Name"), wxDefaultPosition, wxDefaultSize, 0);
	m_nameCaption->Wrap(-1);

	bSizerCaption->Add(m_nameCaption, 0, wxALL, FromDIP(5));

	m_synonymCaption = new wxStaticText(this, wxID_ANY, _("Synonym"), wxDefaultPosition, wxDefaultSize, 0);
	m_synonymCaption->Wrap(-1);

	bSizerCaption->Add(m_synonymCaption, 0, wxALL, FromDIP(5));

	m_commentCaption = new wxStaticText(this, wxID_ANY, _("Comment"), wxDefaultPosition, wxDefaultSize, 0);
	m_commentCaption->Wrap(-1);

	bSizerCaption->Add(m_commentCaption, 0, wxALL, FromDIP(5));

	m_defaultForm = new wxStaticText(this, wxID_ANY, _("Default form:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultForm->Wrap(-1);

	bSizerCaption->Add(m_defaultForm, 0, wxALL, FromDIP(5));

	// ⭐ THE REPORT'S OTHER DECLARATION, beside its default form: WHICH COMPOSER IS THE MAIN ONE (the desktop's).
	m_defaultComposer = new wxStaticText(this, wxID_ANY, _("Default composer:"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultComposer->Wrap(-1);

	bSizerCaption->Add(m_defaultComposer, 0, wxALL, FromDIP(5));
	bSizerHeader->Add(bSizerCaption, 0, wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizerValue = new wxBoxSizer(wxVERTICAL);

	// ⚠ A TEXT GOES BACK WHEN IT IS DONE — Enter, or the focus leaving it: each edit is a call to the server, where the
	// desktop's was a keystroke into its own object.
	m_nameValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	bSizerValue->Add(m_nameValue, 1, wxALL | wxEXPAND, 1);
	m_nameValue->Bind(wxEVT_TEXT_ENTER, &ibDataReportTree::OnEditCaptionName, this);
	m_nameValue->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { wxCommandEvent done; OnEditCaptionName(done); event.Skip(); });

	m_synonymValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	bSizerValue->Add(m_synonymValue, 1, wxALL | wxEXPAND, 1);
	m_synonymValue->Bind(wxEVT_TEXT_ENTER, &ibDataReportTree::OnEditCaptionSynonym, this);
	m_synonymValue->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { wxCommandEvent done; OnEditCaptionSynonym(done); event.Skip(); });

	m_commentValue = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	bSizerValue->Add(m_commentValue, 1, wxALL | wxEXPAND, 1);
	m_commentValue->Bind(wxEVT_TEXT_ENTER, &ibDataReportTree::OnEditCaptionComment, this);
	m_commentValue->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { wxCommandEvent done; OnEditCaptionComment(done); event.Skip(); });

	m_defaultFormValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_defaultFormValue->AppendString(_("<not selected>"));
	m_defaultFormValue->SetSelection(0);

	m_defaultFormValue->Bind(wxEVT_CHOICE, &ibDataReportTree::OnChoiceDefForm, this);

	bSizerValue->Add(m_defaultFormValue, 1, wxALL | wxEXPAND, 1);

	m_defaultComposerValue = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_defaultComposerValue->AppendString(_("<not selected>"));
	m_defaultComposerValue->SetSelection(0);

	m_defaultComposerValue->Bind(wxEVT_CHOICE, &ibDataReportTree::OnChoiceDefComposer, this);

	bSizerValue->Add(m_defaultComposerValue, 1, wxALL | wxEXPAND, 1);
	bSizerHeader->Add(bSizerValue, 1, 0, FromDIP(5));

	m_buttonModule = new wxButton(this, wxID_ANY, _("Open module"));
	m_buttonModule->Bind(wxEVT_BUTTON, &ibDataReportTree::OnButtonModuleClicked, this);

	// …the header above the base's toolbar and rows, the button below them.
	wxSizer* const bSizerMain = GetSizer();
	bSizerMain->Insert(0, bSizerHeader, 0, wxEXPAND, FromDIP(5));
	bSizerMain->Add(m_buttonModule, 0, wxALL, 0);

	Layout();
}
