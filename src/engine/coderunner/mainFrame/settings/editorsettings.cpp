#include "editorsettings.h"

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
