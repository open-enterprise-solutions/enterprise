
#include "editorsettings.h"
#include "xmlutility.h"

#include <wx/wx.h>
#include <wx/xml/xml.h>

ibEditorSettings::ibEditorSettings()
{
    m_indentSize                    = 4;
    m_useTabs                       = false;
    m_removeTabsOnLoad              = false;
    m_showLineNumbers               = true;
    m_mostRecentlyUsedTabSwitching  = false;
    m_enableAutoComplete            = true;
    m_showWhiteSpaces               = false;
}

void ibEditorSettings::SetIndentSize(unsigned int indentSize)
{
    m_indentSize = wxMin(indentSize, 32);
}

void ibEditorSettings::SetUseTabs(bool useTabs)
{
    m_useTabs = useTabs;
}

void ibEditorSettings::SetRemoveTabsOnLoad(bool removeTabsOnLoad)
{
    m_removeTabsOnLoad = removeTabsOnLoad;
}

unsigned int ibEditorSettings::GetIndentSize() const
{
    return m_indentSize;
}

bool ibEditorSettings::GetUseTabs() const
{
    return m_useTabs;
}

bool ibEditorSettings::GetRemoveTabsOnLoad() const
{
    return m_removeTabsOnLoad;
}

bool ibEditorSettings::GetShowLineNumbers() const
{
    return m_showLineNumbers;
}

void ibEditorSettings::SetShowLineNumbers(bool showLineNumbers)
{
    m_showLineNumbers = showLineNumbers;
}

bool ibEditorSettings::GetMostRecentlyUsedTabSwitching() const
{
    return m_mostRecentlyUsedTabSwitching;
};

void ibEditorSettings::SetMostRecentlyUsedTabSwitching(bool mostRecentlyUsedTabSwitching)
{
    m_mostRecentlyUsedTabSwitching = mostRecentlyUsedTabSwitching;
}

bool ibEditorSettings::GetEnableAutoComplete() const
{
    return m_enableAutoComplete;
}

void ibEditorSettings::SetEnableAutoComplete(bool enableAutoComplete)
{
    m_enableAutoComplete = enableAutoComplete;
}

bool ibEditorSettings::GetShowWhiteSpace() const 
{
    return m_showWhiteSpaces;
}

void ibEditorSettings::SetShowWhiteSpace(bool showWhiteSpace)
{
    m_showWhiteSpaces = showWhiteSpace; 
}

wxXmlNode* ibEditorSettings::Save(const wxString& tag) const
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

void ibEditorSettings::Load(wxXmlNode* root)
{

    wxXmlNode* node = root->GetChildren();

    while (node != nullptr)
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