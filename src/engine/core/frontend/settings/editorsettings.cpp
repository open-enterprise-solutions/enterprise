
#include "editorsettings.h"
#include "XmlUtility.h"

#include <wx/wx.h>
#include <wx/xml/xml.h>

EditorSettings::EditorSettings()
{
    m_indentSize                    = 4;
    m_useTabs                       = false;
    m_removeTabsOnLoad              = false;
    m_showLineNumbers               = true;
    m_mostRecentlyUsedTabSwitching  = false;
    m_enableAutoComplete            = true;
    m_showWhiteSpaces               = false;
}

void EditorSettings::SetIndentSize(unsigned int indentSize)
{
    m_indentSize = wxMin(indentSize, 32);
}

void EditorSettings::SetUseTabs(bool useTabs)
{
    m_useTabs = useTabs;
}

void EditorSettings::SetRemoveTabsOnLoad(bool removeTabsOnLoad)
{
    m_removeTabsOnLoad = removeTabsOnLoad;
}

unsigned int EditorSettings::GetIndentSize() const
{
    return m_indentSize;
}

bool EditorSettings::GetUseTabs() const
{
    return m_useTabs;
}

bool EditorSettings::GetRemoveTabsOnLoad() const
{
    return m_removeTabsOnLoad;
}

bool EditorSettings::GetShowLineNumbers() const
{
    return m_showLineNumbers;
}

void EditorSettings::SetShowLineNumbers(bool showLineNumbers)
{
    m_showLineNumbers = showLineNumbers;
}

bool EditorSettings::GetMostRecentlyUsedTabSwitching() const
{
    return m_mostRecentlyUsedTabSwitching;
};

void EditorSettings::SetMostRecentlyUsedTabSwitching(bool mostRecentlyUsedTabSwitching)
{
    m_mostRecentlyUsedTabSwitching = mostRecentlyUsedTabSwitching;
}

bool EditorSettings::GetEnableAutoComplete() const
{
    return m_enableAutoComplete;
}

void EditorSettings::SetEnableAutoComplete(bool enableAutoComplete)
{
    m_enableAutoComplete = enableAutoComplete;
}

bool EditorSettings::GetShowWhiteSpace() const 
{
    return m_showWhiteSpaces;
}

void EditorSettings::SetShowWhiteSpace(bool showWhiteSpace)
{
    m_showWhiteSpaces = showWhiteSpace; 
}

wxXmlNode* EditorSettings::Save(const wxString& tag) const
{

    wxXmlNode* root = new wxXmlNode(wxXML_ELEMENT_NODE, tag);    

    root->AddChild( WriteXmlNode("indentsize", m_indentSize) );
    root->AddChild( WriteXmlNodeBool("usetabs", m_useTabs) );
    root->AddChild( WriteXmlNodeBool("removetabsonload", m_removeTabsOnLoad) );
    root->AddChild( WriteXmlNodeBool("showlinenumbers", m_showLineNumbers) );
    root->AddChild( WriteXmlNodeBool("mostrecentlyusedtabswitching", m_mostRecentlyUsedTabSwitching) );
    root->AddChild( WriteXmlNodeBool("enableautocomplete", m_enableAutoComplete) );
    root->AddChild( WriteXmlNodeBool("showwhitespace", m_showWhiteSpaces) );

    return root;

}

void EditorSettings::Load(wxXmlNode* root)
{

    wxXmlNode* node = root->GetChildren();

    while (node != NULL)
    {

        ReadXmlNode(node, "indentsize", m_indentSize)                                       ||
        ReadXmlNode(node, "usetabs", m_useTabs)                                             ||
        ReadXmlNode(node, "removetabsonload", m_removeTabsOnLoad)                           ||
        ReadXmlNode(node, "showlinenumbers", m_showLineNumbers)                             ||
        ReadXmlNode(node, "mostrecentlyusedtabswitching", m_mostRecentlyUsedTabSwitching)   ||
        ReadXmlNode(node, "enableautocomplete", m_enableAutoComplete);
        ReadXmlNode(node, "showwhitespace", m_showWhiteSpaces);

        node = node->GetNext();

    }

}