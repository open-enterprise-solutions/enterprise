#include "editorsettings.h"
#include "settingsxml.h"

#include <wx/stc/stc.h>

void ibEditorSettings::SetIndentSize(unsigned int indentSize)
{
	m_indentSize = indentSize > 32 ? 32 : indentSize;
}

void ibEditorSettings::Apply(wxStyledTextCtrl& text) const
{
	text.SetIndent(m_indentSize);
	text.SetTabWidth(m_indentSize);

	// Indenting with tabs, Tab and Backspace move a whole indent.
	text.SetUseTabs(m_useTabs);
	text.SetTabIndents(m_useTabs);
	text.SetBackSpaceUnIndents(m_useTabs);

	text.SetViewWhiteSpace(m_showWhiteSpace ? wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
}

wxXmlNode* ibEditorSettings::Save(const wxString& tag) const
{
	wxXmlNode* const root = new wxXmlNode(wxXML_ELEMENT_NODE, tag);
	AddTextNode(root, wxT("indentsize"), wxString::Format(wxT("%u"), m_indentSize));
	AddTextNode(root, wxT("usetabs"), m_useTabs);
	AddTextNode(root, wxT("removetabsonload"), m_removeTabsOnLoad);
	AddTextNode(root, wxT("showlinenumbers"), m_showLineNumbers);
	AddTextNode(root, wxT("mostrecentlyusedtabswitching"), m_mostRecentlyUsedTabSwitching);
	AddTextNode(root, wxT("enableautocomplete"), m_enableAutoComplete);
	AddTextNode(root, wxT("showwhitespace"), m_showWhiteSpace);
	return root;
}

void ibEditorSettings::Load(wxXmlNode* root)
{
	for (wxXmlNode* node = root->GetChildren(); node != nullptr; node = node->GetNext()) {
		const wxString& name = node->GetName();
		if (name == wxT("indentsize")) {
			unsigned int indentSize = 0;
			if (node->GetNodeContent().ToUInt(&indentSize))
				SetIndentSize(indentSize);
		}
		else if (name == wxT("usetabs"))
			m_useTabs = IsTrueNode(node);
		else if (name == wxT("removetabsonload"))
			m_removeTabsOnLoad = IsTrueNode(node);
		else if (name == wxT("showlinenumbers"))
			m_showLineNumbers = IsTrueNode(node);
		else if (name == wxT("mostrecentlyusedtabswitching"))
			m_mostRecentlyUsedTabSwitching = IsTrueNode(node);
		else if (name == wxT("enableautocomplete"))
			m_enableAutoComplete = IsTrueNode(node);
		else if (name == wxT("showwhitespace"))
			m_showWhiteSpace = IsTrueNode(node);
	}
}
