#ifndef EDITOR_SETTINGS_PANEL_H
#define EDITOR_SETTINGS_PANEL_H

#include <wx/wx.h>
#include <wx/radiobut.h>

#include "EditorSettings.h"

/**
 * This class handles the UI for modifying the editor settings page of the
 * settings dialog.
 */
class EditorSettingsPanel : public wxPanel 
{
	
public:

	EditorSettingsPanel( wxWindow* parent, int id = wxID_ANY, wxPoint pos = wxDefaultPosition, wxSize size = wxSize( 461,438 ), int style = wxTAB_TRAVERSAL );

    void Initialize();
    
    void SetSettings(const EditorSettings& settings);
    const EditorSettings& GetSettings() const;
    
    /**
     * Called when "Insert spaces" radio button is selected.
     */
    void OnInsertSpaces(wxCommandEvent& event);

    /**
     * Called when the "Keep tabs" radio button is selected.
     */
    void OnKeepTabs(wxCommandEvent& event);

    /**
     * Called when the "Convert tabs to spaces on load" check box is changed.
     */
    void OnRemoveTabsOnLoad(wxCommandEvent& event);

    /**
     * Called when the text in the indent size text control is changed.
     */
    void OnIndentSizeChanged(wxCommandEvent& event);

    /**
     * Called when the "Show line numbers in margin" check box is changed.
     */
    void OnShowLineNumbersChanged(wxCommandEvent& event);

    /**
     * Called when the "Most recently used tab switching" check box is changed.
     */
    void OnMostRecentlyUsedTabSwitching(wxCommandEvent& event);

    /**
     * Called when the "Enable auto complete" check box is changed.
     */
    void OnEnableAutoComplete(wxCommandEvent& event);

    /**
     * Called when the "Show white space" check box is changed.
     */
    void OnShowWhiteSpace(wxCommandEvent& event);
    
    DECLARE_EVENT_TABLE()

private:

    enum ID
    {
        ID_InsertSpaces      = 1,
        ID_KeepTabs,   
        ID_IndentSize,
        ID_RemoveTabsOnLoad,
        ID_ShowLineNumbers,
        ID_LoadLastProjectOnStartup,
        ID_MostRecentlyUsedTabSwitching,
        ID_EnableAutoComplete,
        ID_ShowWhiteSpace,
    };

	wxStaticText*   m_staticText5;
	wxTextCtrl*     m_indentSizeCtrl;
	wxRadioButton*  m_useTabs;
	wxRadioButton*  m_useSpaces;
	wxCheckBox*     m_removeTabsOnLoad;
    wxCheckBox*     m_showLineNumbers;
    wxCheckBox*     m_mostRecentlyUsedTabSwitching;
    wxCheckBox*     m_enableAutoComplete;
    wxCheckBox*     m_showWhiteSpace;

    EditorSettings  m_settings;
	
};

#endif