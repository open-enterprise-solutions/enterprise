#ifndef KEY_BINDER_H
#define KEY_BINDER_H

/////////////////////////////////////////////////////////////////////////////
// The keys of the menu's commands. The commands come from the menu bar
// (AddCommandsFromMenuBar), their default keys from the code (SetShortcut),
// and the person's changes from the options file over them (Load); the
// binder then sets the window's accelerators and writes the keys into the
// menu labels.
//
// ⚠ A COMMAND IS SAVED UNDER ITS NUMERIC ID, and ids move when a command is
// added before another one. A profile that held every binding carried the
// old numbering forward for good — F10 landed on "Attach for debugging" once
// two start commands were added ahead of the steps (2026-09-11). So the
// profile holds only what a person CHANGED; the defaults always come from
// the code.
/////////////////////////////////////////////////////////////////////////////

#include <wx/accel.h>
#include <vector>

#include "frmclient/frmclient.h"

class wxWindow;
class wxMenu;
class wxMenuBar;
class wxXmlNode;

class FRMCLIENT_API ibKeyBinder
{
public:

	// A key with its modifiers: wxACCEL_* flags and a WXK_* or character code.
	struct Key
	{
		int flags = 0;
		int code = 0;

		bool operator==(const Key& other) const { return flags == other.flags && code == other.code; }
		bool operator!=(const Key& other) const { return !(*this == other); }
	};

	struct Command
	{
		int id = 0;
		wxString name;
		wxString group;
		wxString help;
		std::vector<Key> keys;
		// What the code binds it to (SetShortcut), beside `keys` so Save can tell what the person changed.
		std::vector<Key> defaultKeys;
	};

	void ClearCommands() { m_commands.clear(); }

	// A command already known by its id is not added twice.
	void AddCommand(const Command& command);

	// Every item of every menu of the bar, submenus too — the group is the
	// menu's label, a submenu's the path to it ("File / Recent").
	void AddCommandsFromMenuBar(wxMenuBar* menuBar);

	// A DEFAULT key of the command, written as "Ctrl+F5"; a loaded profile may change it.
	void SetShortcut(int id, const wxString& shortcut);

	unsigned int GetNumCommands() const { return static_cast<unsigned int>(m_commands.size()); }
	const Command& GetCommand(unsigned int i) const { return m_commands[i]; }

	// The keys as the window's accelerators.
	void UpdateWindow(wxWindow* window);

	// The first key of each command after its label, in every menu of the bar.
	void UpdateMenuBar(wxMenuBar* menuBar);

	// The commands whose keys differ from their defaults, as a node named by
	// the tag; Load puts them back over the defaults (a command named with no
	// key has had its keys removed).
	wxXmlNode* Save(const wxString& tag) const;
	void Load(wxXmlNode* root);

	// The key as a person reads and the options file keeps it ("Ctrl+Shift+F5"), and back.
	static wxString GetKeyBindingAsText(const Key& key);
	static Key GetTextAsKeyBinding(const wxString& text);

private:

	Command* GetCommandForId(int id);

	void AddCommandsFromMenu(const wxString& group, wxMenu* menu);
	void UpdateMenu(wxMenu* menu);

	std::vector<Command> m_commands;
};

#endif
