#ifndef EDITOR_SETTINGS_H
#define EDITOR_SETTINGS_H

/////////////////////////////////////////////////////////////////////////////
// How the code editor indents and what it shows: a plain value, copied into
// every editor (Apply) and kept in the options file under
// "editor".
/////////////////////////////////////////////////////////////////////////////

#include "frmclient/frmclient.h"

class wxStyledTextCtrl;
class wxXmlNode;
class wxString;

class FRMCLIENT_API ibEditorSettings
{
public:

	// Characters per indent, at most 32.
	unsigned int GetIndentSize() const { return m_indentSize; }
	void SetIndentSize(unsigned int indentSize);

	// An indent is a tab character, not spaces.
	bool GetUseTabs() const { return m_useTabs; }
	void SetUseTabs(bool useTabs) { m_useTabs = useTabs; }

	// Tabs in a loaded file are turned into spaces.
	bool GetRemoveTabsOnLoad() const { return m_removeTabsOnLoad; }
	void SetRemoveTabsOnLoad(bool removeTabsOnLoad) { m_removeTabsOnLoad = removeTabsOnLoad; }

	bool GetShowLineNumbers() const { return m_showLineNumbers; }
	void SetShowLineNumbers(bool showLineNumbers) { m_showLineNumbers = showLineNumbers; }

	// Ctrl+Tab walks the tabs in the order they were last used, not as they stand.
	bool GetMostRecentlyUsedTabSwitching() const { return m_mostRecentlyUsedTabSwitching; }
	void SetMostRecentlyUsedTabSwitching(bool mostRecentlyUsedTabSwitching) { m_mostRecentlyUsedTabSwitching = mostRecentlyUsedTabSwitching; }

	bool GetEnableAutoComplete() const { return m_enableAutoComplete; }
	void SetEnableAutoComplete(bool enableAutoComplete) { m_enableAutoComplete = enableAutoComplete; }

	bool GetShowWhiteSpace() const { return m_showWhiteSpace; }
	void SetShowWhiteSpace(bool showWhiteSpace) { m_showWhiteSpace = showWhiteSpace; }

	// Sets a text control's indent, its tabs and whether it shows white space.
	void Apply(wxStyledTextCtrl& text) const;

	// The settings as a node named by the tag; Load reads one back, keeping
	// the default for whatever the node does not name.
	wxXmlNode* Save(const wxString& tag) const;
	void Load(wxXmlNode* root);

private:

	unsigned int m_indentSize = 4;
	bool m_useTabs = false;
	bool m_removeTabsOnLoad = false;
	bool m_showLineNumbers = true;
	bool m_mostRecentlyUsedTabSwitching = false;
	bool m_enableAutoComplete = true;
	bool m_showWhiteSpace = false;
};

#endif
