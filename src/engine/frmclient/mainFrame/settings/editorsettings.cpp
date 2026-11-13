#include "editorsettings.h"
#include "settingsxml.h"

#include "deskgui/univ/stc/stc.h"

void ibEditorSettings::SetIndentSize(unsigned int indentSize)
{
	m_indentSize = indentSize > 32 ? 32 : indentSize;
}

void ibEditorSettings::Apply(ibStyledTextCtrl& text) const
{
	text.SetIndent(m_indentSize);
	text.SetTabWidth(m_indentSize);

	// Indenting with tabs, Tab and Backspace move a whole indent.
	text.SetUseTabs(m_useTabs);
	text.SetTabIndents(m_useTabs);
	text.SetBackSpaceUnIndents(m_useTabs);

	text.SetViewWhiteSpace(m_showWhiteSpace ? ibSTC_WS_VISIBLEALWAYS : ibSTC_WS_INVISIBLE);
}

void ibEditorSettings::Save(ibXmlWriter& writer, const wxString& tag) const
{
	writer.WriteStartElement(tag, ibString());
	AddTextNode(writer, wxT("indentsize"), wxString::Format(wxT("%u"), m_indentSize));
	AddTextNode(writer, wxT("usetabs"), m_useTabs);
	AddTextNode(writer, wxT("removetabsonload"), m_removeTabsOnLoad);
	AddTextNode(writer, wxT("showlinenumbers"), m_showLineNumbers);
	AddTextNode(writer, wxT("mostrecentlyusedtabswitching"), m_mostRecentlyUsedTabSwitching);
	AddTextNode(writer, wxT("enableautocomplete"), m_enableAutoComplete);
	AddTextNode(writer, wxT("showwhitespace"), m_showWhiteSpace);
	writer.WriteEndElement();
}

void ibEditorSettings::Load(const ibXmlNode* root)
{
	for (const ibXmlNode* node = root->GetChildren(); node != nullptr; node = node->GetNext()) {
		const ibString& name = node->GetName();
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
