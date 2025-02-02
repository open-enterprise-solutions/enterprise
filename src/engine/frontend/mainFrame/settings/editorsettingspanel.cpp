
#include "editorsettingspanel.h"

BEGIN_EVENT_TABLE(EditorSettingsPanel, wxPanel)

    EVT_RADIOBUTTON(    ID_InsertSpaces,                    OnInsertSpaces )
    EVT_RADIOBUTTON(    ID_KeepTabs,                        OnKeepTabs )
    EVT_CHECKBOX(       ID_RemoveTabsOnLoad,                OnRemoveTabsOnLoad )
    EVT_CHECKBOX(       ID_ShowLineNumbers,                 OnShowLineNumbersChanged )
    EVT_TEXT(           ID_IndentSize,                      OnIndentSizeChanged )
    EVT_CHECKBOX(       ID_MostRecentlyUsedTabSwitching,    OnMostRecentlyUsedTabSwitching )
    EVT_CHECKBOX(       ID_EnableAutoComplete,              OnEnableAutoComplete )
    EVT_CHECKBOX(       ID_ShowWhiteSpace,                  OnShowWhiteSpace )

END_EVENT_TABLE()

EditorSettingsPanel::EditorSettingsPanel( wxWindow* parent, int id, wxPoint pos, wxSize size, int style ) : wxPanel( parent, id, pos, size, style )
{
	wxFlexGridSizer* fgSizer2 = new wxFlexGridSizer( 2, 2, 0, 0 );
	fgSizer2->AddGrowableCol( 0 );
	fgSizer2->AddGrowableRow( 0 );
	fgSizer2->SetFlexibleDirection( wxHORIZONTAL );
	fgSizer2->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_NONE );

    // Setup the editor options UI.
	
	wxStaticBoxSizer* sbSizer3;
	sbSizer3 = new wxStaticBoxSizer( new wxStaticBox( this, -1, wxT("Editor") ), wxVERTICAL );
	
	wxBoxSizer* bSizer2;
	bSizer2 = new wxBoxSizer( wxHORIZONTAL );
	
	m_staticText5 = new wxStaticText( this, wxID_ANY, wxT("Indent size:"), wxDefaultPosition, wxDefaultSize, 0 );
	bSizer2->Add( m_staticText5, 0, wxALL, 5 );
	
	m_indentSizeCtrl = new wxTextCtrl( this, ID_IndentSize, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	bSizer2->Add( m_indentSizeCtrl, 0, wxALL, 5 );
	
	sbSizer3->Add( bSizer2, 1, wxEXPAND, 5 );
	
	m_useSpaces = new wxRadioButton( this, ID_InsertSpaces, wxT("Insert spaces"), wxDefaultPosition, wxDefaultSize, 0);
	sbSizer3->Add( m_useSpaces, 0, wxALL, 5 );
	
	m_useTabs = new wxRadioButton( this, ID_KeepTabs, wxT("Keep tabs"), wxDefaultPosition, wxDefaultSize, 0);
	sbSizer3->Add( m_useTabs, 0, wxALL, 5 );
	
	m_removeTabsOnLoad = new wxCheckBox( this, ID_RemoveTabsOnLoad, wxT("Convert tabs to spaces when loading a file"), wxDefaultPosition, wxDefaultSize, 0 );
	sbSizer3->Add( m_removeTabsOnLoad, 0, wxALL, 5 );
	
	m_showLineNumbers = new wxCheckBox( this, ID_ShowLineNumbers, wxT("Show line numbers in the margin"), wxDefaultPosition, wxDefaultSize, 0 );
	sbSizer3->Add( m_showLineNumbers, 0, wxALL, 5 );
    
	m_enableAutoComplete = new wxCheckBox( this, ID_EnableAutoComplete, wxT("Enable auto complete"), wxDefaultPosition, wxDefaultSize, 0 );
	sbSizer3->Add( m_enableAutoComplete, 0, wxALL, 5 );

    m_showWhiteSpace = new wxCheckBox( this, ID_ShowWhiteSpace, wxT("Show white space"), wxDefaultPosition, wxDefaultSize, 0 );
    sbSizer3->Add( m_showWhiteSpace, 0, wxALL, 5 );

    fgSizer2->Add( sbSizer3, 1, wxALL|wxEXPAND, 5 );

    // Setup the environment options UI.

	wxStaticBoxSizer* environmentSizer;
	environmentSizer = new wxStaticBoxSizer( new wxStaticBox( this, -1, wxT("Environment") ), wxVERTICAL );
		
    m_mostRecentlyUsedTabSwitching = new wxCheckBox( this, ID_MostRecentlyUsedTabSwitching, wxT("Most recently used tab switching"), wxDefaultPosition, wxDefaultSize, 0 );

    environmentSizer->Add( m_mostRecentlyUsedTabSwitching, 0, wxALL, 5);

    fgSizer2->Add( environmentSizer, 1, wxALL|wxEXPAND, 5 );

	this->SetSizer( fgSizer2 );
	this->Layout();

}

void EditorSettingsPanel::Initialize()
{
    
    m_indentSizeCtrl->SetValue(wxString::Format("%d", m_settings.GetIndentSize()));

    if (m_settings.GetUseTabs())
    {
        m_useTabs->SetValue(true);
        m_useSpaces->SetValue(false);
    }
    else
    {
        m_useSpaces->SetValue(true);
        m_useTabs->SetValue(false);
    }

    m_removeTabsOnLoad->SetValue(m_settings.GetRemoveTabsOnLoad());
    m_removeTabsOnLoad->Enable(!m_settings.GetUseTabs());

    m_showLineNumbers->SetValue(m_settings.GetShowLineNumbers());
    m_enableAutoComplete->SetValue(m_settings.GetEnableAutoComplete());
    m_showWhiteSpace->SetValue(m_settings.GetShowWhiteSpace());

    m_mostRecentlyUsedTabSwitching->SetValue(m_settings.GetMostRecentlyUsedTabSwitching());
}

void EditorSettingsPanel::SetSettings(const EditorSettings& settings)
{
    m_settings = settings;
}

const EditorSettings& EditorSettingsPanel::GetSettings() const
{
    return m_settings;
}

void EditorSettingsPanel::OnInsertSpaces(wxCommandEvent& event)
{
    m_settings.SetUseTabs(false);
    m_removeTabsOnLoad->Enable(!m_settings.GetUseTabs());
}

void EditorSettingsPanel::OnKeepTabs(wxCommandEvent& event)
{
    m_settings.SetUseTabs(true);
    m_removeTabsOnLoad->Enable(!m_settings.GetUseTabs());
}

void EditorSettingsPanel::OnRemoveTabsOnLoad(wxCommandEvent& event)
{
    m_settings.SetRemoveTabsOnLoad(m_removeTabsOnLoad->GetValue());
}

void EditorSettingsPanel::OnIndentSizeChanged(wxCommandEvent& event)
{

    long indentSize;
    
    if (m_indentSizeCtrl->GetValue().ToLong(&indentSize))
    {
        m_settings.SetIndentSize(indentSize);
    }

}

void EditorSettingsPanel::OnShowLineNumbersChanged(wxCommandEvent& event)
{
    m_settings.SetShowLineNumbers(m_showLineNumbers->GetValue());
}

void EditorSettingsPanel::OnMostRecentlyUsedTabSwitching(wxCommandEvent& event)
{
    m_settings.SetMostRecentlyUsedTabSwitching(m_mostRecentlyUsedTabSwitching->GetValue());
}

void EditorSettingsPanel::OnEnableAutoComplete(wxCommandEvent& event)
{
    m_settings.SetEnableAutoComplete(m_enableAutoComplete->GetValue());
}

void EditorSettingsPanel::OnShowWhiteSpace(wxCommandEvent& event)
{
    m_settings.SetShowWhiteSpace(m_showWhiteSpace->GetValue());
}
