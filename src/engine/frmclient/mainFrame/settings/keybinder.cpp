#include <wx/wx.h>

#include "keybinder.h"
#include "settingsxml.h"

// Ctrl+Break arrives as WXK_CANCEL, which wx writes "Cancel"; a person pressed Break, and profiles say so.
static const wxString kBreakName = wxT("Break");
static const wxString kCancelName = wxT("Cancel");

void ibKeyBinder::AddCommand(const Command& command)
{
	if (GetCommandForId(command.id) == nullptr)
		m_commands.push_back(command);
}

void ibKeyBinder::AddCommandsFromMenuBar(wxMenuBar* menuBar)
{
	for (size_t i = 0; i < menuBar->GetMenuCount(); ++i)
		AddCommandsFromMenu(menuBar->GetMenuLabelText(i), menuBar->GetMenu(i));
}

void ibKeyBinder::AddCommandsFromMenu(const wxString& group, wxMenu* menu)
{
	for (wxMenuItem* item : menu->GetMenuItems()) {
		if (item->IsSubMenu()) {
			if (item->GetSubMenu() != nullptr)
				AddCommandsFromMenu(group + wxT(" / ") + item->GetItemLabelText(), item->GetSubMenu());
		}
		else if (!item->IsSeparator()) {
			Command command;
			command.id = item->GetId();
			command.name = item->GetItemLabelText();
			command.group = group;
			command.help = item->GetHelp();
			AddCommand(command);
		}
	}
}

void ibKeyBinder::SetShortcut(int id, const wxString& shortcut)
{
	Command* const command = GetCommandForId(id);
	if (command == nullptr)
		return;

	const Key key = GetTextAsKeyBinding(shortcut);
	command->keys.push_back(key);
	command->defaultKeys.push_back(key);
}

void ibKeyBinder::UpdateWindow(wxWindow* window)
{
	std::vector<wxAcceleratorEntry> entries;
	for (const Command& command : m_commands) {
		for (const Key& key : command.keys)
			entries.emplace_back(key.flags, key.code, command.id);
	}

	window->SetAcceleratorTable(entries.empty()
		? wxNullAcceleratorTable : wxAcceleratorTable(static_cast<int>(entries.size()), entries.data()));
}

void ibKeyBinder::UpdateMenuBar(wxMenuBar* menuBar)
{
	for (size_t i = 0; i < menuBar->GetMenuCount(); ++i)
		UpdateMenu(menuBar->GetMenu(i));
}

void ibKeyBinder::UpdateMenu(wxMenu* menu)
{
	for (wxMenuItem* item : menu->GetMenuItems()) {
		if (item->IsSubMenu()) {
			if (item->GetSubMenu() != nullptr)
				UpdateMenu(item->GetSubMenu());
			continue;
		}

		const Command* const command = GetCommandForId(item->GetId());
		if (command == nullptr || item->IsSeparator())
			continue;

		// The item's own label, its mnemonic kept; only what follows the tab is the binder's.
		wxString label = item->GetItemLabel().BeforeFirst(wxT('\t'));
		if (!command->keys.empty())
			label << wxT('\t') << GetKeyBindingAsText(command->keys.front());
		if (label != item->GetItemLabel())
			item->SetItemLabel(label);
	}
}

wxXmlNode* ibKeyBinder::Save(const wxString& tag) const
{
	wxXmlNode* const root = new wxXmlNode(wxXML_ELEMENT_NODE, tag);
	for (const Command& command : m_commands) {
		if (command.keys == command.defaultKeys)
			continue;

		// Written with no key at all too, when the default was taken away: "removed" is a choice.
		wxXmlNode* const node = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("command"));
		AddTextNode(node, wxT("id"), wxString::Format(wxT("%d"), command.id));
		for (const Key& key : command.keys)
			AddTextNode(node, wxT("key"), GetKeyBindingAsText(key));
		root->AddChild(node);
	}
	return root;
}

void ibKeyBinder::Load(wxXmlNode* root)
{
	for (wxXmlNode* node = root->GetChildren(); node != nullptr; node = node->GetNext()) {
		if (node->GetName() != wxT("command"))
			continue;

		long id = 0;
		bool hasId = false;
		std::vector<Key> keys;
		for (wxXmlNode* field = node->GetChildren(); field != nullptr; field = field->GetNext()) {
			if (field->GetName() == wxT("id"))
				hasId = field->GetNodeContent().ToLong(&id);
			else if (field->GetName() == wxT("key")) {
				const Key key = GetTextAsKeyBinding(field->GetNodeContent());
				if (key.code != 0)
					keys.push_back(key);
			}
		}

		Command* const command = hasId ? GetCommandForId(static_cast<int>(id)) : nullptr;
		if (command != nullptr)
			command->keys = keys;
	}
}

wxString ibKeyBinder::GetKeyBindingAsText(const Key& key)
{
	if (key.code == 0)
		return wxString();

	// Untranslated, so the profile reads the same in every language and wx parses the menu label back.
	wxString text = wxAcceleratorEntry(key.flags, key.code).ToRawString();
	if (key.code == WXK_CANCEL && text.EndsWith(kCancelName))
		text = text.Left(text.length() - kCancelName.length()) + kBreakName;
	return text;
}

ibKeyBinder::Key ibKeyBinder::GetTextAsKeyBinding(const wxString& text)
{
	wxString shortcut = text;
	shortcut.Trim().Trim(false);
	if (shortcut.Lower().EndsWith(kBreakName.Lower()))
		shortcut = shortcut.Left(shortcut.length() - kBreakName.length()) + kCancelName;

	Key key;
	wxAcceleratorEntry entry;
	if (!shortcut.IsEmpty() && entry.FromString(shortcut)) {
		key.flags = entry.GetFlags();
		key.code = entry.GetKeyCode();
	}
	return key;
}

ibKeyBinder::Command* ibKeyBinder::GetCommandForId(int id)
{
	for (Command& command : m_commands) {
		if (command.id == id)
			return &command;
	}
	return nullptr;
}
