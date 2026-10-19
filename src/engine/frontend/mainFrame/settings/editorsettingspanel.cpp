#include "editorsettingspanel.h"

ibPanelEditorSettings::ibPanelEditorSettings(wxWindow* parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* const page = new wxBoxSizer(wxVERTICAL);

	wxStaticBoxSizer* const indent = new wxStaticBoxSizer(wxVERTICAL, this, _("Indent"));
	wxWindow* const indentBox = indent->GetStaticBox();

	wxBoxSizer* const sizeRow = new wxBoxSizer(wxHORIZONTAL);
	sizeRow->Add(new wxStaticText(indentBox, wxID_ANY, _("Indent size:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_indentSizeCtrl = new wxSpinCtrl(indentBox, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS, 1, 32, 4);
	sizeRow->Add(m_indentSizeCtrl, 0, wxALIGN_CENTER_VERTICAL);
	indent->Add(sizeRow, 0, wxALL, 5);

	m_useSpaces = new wxRadioButton(indentBox, wxID_ANY, _("Insert spaces"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	indent->Add(m_useSpaces, 0, wxALL, 5);
	m_useTabs = new wxRadioButton(indentBox, wxID_ANY, _("Keep tabs"));
	indent->Add(m_useTabs, 0, wxALL, 5);
	m_removeTabsOnLoad = new wxCheckBox(indentBox, wxID_ANY, _("Convert tabs to spaces on load"));
	indent->Add(m_removeTabsOnLoad, 0, wxALL, 5);
	page->Add(indent, 0, wxEXPAND | wxALL, 5);

	wxStaticBoxSizer* const view = new wxStaticBoxSizer(wxVERTICAL, this, _("View"));
	wxWindow* const viewBox = view->GetStaticBox();

	m_showLineNumbers = new wxCheckBox(viewBox, wxID_ANY, _("Show line numbers in margin"));
	view->Add(m_showLineNumbers, 0, wxALL, 5);
	m_showWhiteSpace = new wxCheckBox(viewBox, wxID_ANY, _("Show white space"));
	view->Add(m_showWhiteSpace, 0, wxALL, 5);
	m_enableAutoComplete = new wxCheckBox(viewBox, wxID_ANY, _("Enable auto complete"));
	view->Add(m_enableAutoComplete, 0, wxALL, 5);
	m_mostRecentlyUsedTabSwitching = new wxCheckBox(viewBox, wxID_ANY, _("Most recently used tab switching"));
	view->Add(m_mostRecentlyUsedTabSwitching, 0, wxALL, 5);
	page->Add(view, 0, wxEXPAND | wxALL, 5);

	SetSizer(page);
	SetSettings(ibEditorSettings());
}

void ibPanelEditorSettings::SetSettings(const ibEditorSettings& settings)
{
	m_indentSizeCtrl->SetValue(static_cast<int>(settings.GetIndentSize()));
	m_useTabs->SetValue(settings.GetUseTabs());
	m_useSpaces->SetValue(!settings.GetUseTabs());
	m_removeTabsOnLoad->SetValue(settings.GetRemoveTabsOnLoad());
	m_showLineNumbers->SetValue(settings.GetShowLineNumbers());
	m_showWhiteSpace->SetValue(settings.GetShowWhiteSpace());
	m_enableAutoComplete->SetValue(settings.GetEnableAutoComplete());
	m_mostRecentlyUsedTabSwitching->SetValue(settings.GetMostRecentlyUsedTabSwitching());
}

ibEditorSettings ibPanelEditorSettings::GetSettings() const
{
	ibEditorSettings settings;
	settings.SetIndentSize(m_indentSizeCtrl->GetValue());
	settings.SetUseTabs(m_useTabs->GetValue());
	settings.SetRemoveTabsOnLoad(m_removeTabsOnLoad->GetValue());
	settings.SetShowLineNumbers(m_showLineNumbers->GetValue());
	settings.SetShowWhiteSpace(m_showWhiteSpace->GetValue());
	settings.SetEnableAutoComplete(m_enableAutoComplete->GetValue());
	settings.SetMostRecentlyUsedTabSwitching(m_mostRecentlyUsedTabSwitching->GetValue());
	return settings;
}
