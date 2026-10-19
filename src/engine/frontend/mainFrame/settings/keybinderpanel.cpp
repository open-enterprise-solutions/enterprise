#include "keybinderpanel.h"

#include <algorithm>

enum
{
	kColumnCommand,
	kColumnKeys,
	kColumnMenu,
};

static wxString JoinKeys(const std::vector<ibKeyBinder::Key>& keys)
{
	wxString text;
	for (const ibKeyBinder::Key& key : keys) {
		if (!text.IsEmpty())
			text << wxT(", ");
		text << ibKeyBinder::GetKeyBindingAsText(key);
	}
	return text;
}

ibPanelKeyBinder::ibPanelKeyBinder(wxWindow* parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer* const page = new wxBoxSizer(wxVERTICAL);

	m_filterCtrl = new wxSearchCtrl(this, wxID_ANY);
	m_filterCtrl->SetDescriptiveText(_("Find a command or a key"));
	m_filterCtrl->ShowCancelButton(true);
	page->Add(m_filterCtrl, 0, wxEXPAND | wxALL, 5);

	m_commandList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	m_commandList->InsertColumn(kColumnCommand, _("Command"), wxLIST_FORMAT_LEFT, FromDIP(170));
	m_commandList->InsertColumn(kColumnKeys, _("Keys"), wxLIST_FORMAT_LEFT, FromDIP(110));
	m_commandList->InsertColumn(kColumnMenu, _("Menu"), wxLIST_FORMAT_LEFT, FromDIP(110));
	page->Add(m_commandList, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

	m_helpText = new wxStaticText(this, wxID_ANY, wxString());
	page->Add(m_helpText, 0, wxEXPAND | wxALL, 5);

	wxBoxSizer* const keyRow = new wxBoxSizer(wxHORIZONTAL);
	keyRow->Add(new wxStaticText(this, wxID_ANY, _("Press a key:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_keyCtrl = new wxTextCtrl(this, wxID_ANY);
	keyRow->Add(m_keyCtrl, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	m_assignButton = new wxButton(this, wxID_ANY, _("Assign"));
	keyRow->Add(m_assignButton, 0, wxALIGN_CENTER_VERTICAL);
	page->Add(keyRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

	m_holderText = new wxStaticText(this, wxID_ANY, wxString());
	page->Add(m_holderText, 0, wxEXPAND | wxALL, 5);

	wxBoxSizer* const buttonRow = new wxBoxSizer(wxHORIZONTAL);
	m_removeButton = new wxButton(this, wxID_ANY, _("Remove keys"));
	buttonRow->Add(m_removeButton, 0, wxRIGHT, 5);
	m_resetButton = new wxButton(this, wxID_ANY, _("Reset to default"));
	buttonRow->Add(m_resetButton, 0);
	page->Add(buttonRow, 0, wxALL, 5);

	SetSizer(page);

	m_filterCtrl->Bind(wxEVT_TEXT, &ibPanelKeyBinder::OnFilter, this);
	m_commandList->Bind(wxEVT_LIST_ITEM_SELECTED, &ibPanelKeyBinder::OnCommandSelected, this);
	m_commandList->Bind(wxEVT_LIST_ITEM_DESELECTED, &ibPanelKeyBinder::OnCommandSelected, this);
	// The hook, not KEY_DOWN: it comes before the dialog takes Enter and Escape for its buttons.
	m_keyCtrl->Bind(wxEVT_CHAR_HOOK, &ibPanelKeyBinder::OnKeyPressed, this);
	m_assignButton->Bind(wxEVT_BUTTON, &ibPanelKeyBinder::OnAssign, this);
	m_removeButton->Bind(wxEVT_BUTTON, &ibPanelKeyBinder::OnRemove, this);
	m_resetButton->Bind(wxEVT_BUTTON, &ibPanelKeyBinder::OnReset, this);
}

void ibPanelKeyBinder::SetSettings(const ibKeyBinder& keyBinder)
{
	m_commands.clear();
	for (unsigned int i = 0; i < keyBinder.GetNumCommands(); ++i)
		m_commands.push_back(keyBinder.GetCommand(i));
	ShowCommands();
}

ibKeyBinder ibPanelKeyBinder::GetSettings() const
{
	ibKeyBinder keyBinder;
	for (const ibKeyBinder::Command& command : m_commands)
		keyBinder.AddCommand(command);
	return keyBinder;
}

void ibPanelKeyBinder::ShowCommands()
{
	const wxString filter = m_filterCtrl->GetValue().Lower();

	m_commandList->Freeze();
	m_commandList->DeleteAllItems();
	for (size_t i = 0; i < m_commands.size(); ++i) {
		const ibKeyBinder::Command& command = m_commands[i];
		const wxString keys = JoinKeys(command.keys);
		if (!filter.IsEmpty()
			&& !command.name.Lower().Contains(filter)
			&& !command.group.Lower().Contains(filter)
			&& !keys.Lower().Contains(filter))
			continue;

		const long row = m_commandList->InsertItem(m_commandList->GetItemCount(), command.name);
		m_commandList->SetItem(row, kColumnKeys, keys);
		m_commandList->SetItem(row, kColumnMenu, command.group);
		m_commandList->SetItemData(row, static_cast<long>(i));
	}
	m_commandList->Thaw();

	UpdateState();
}

void ibPanelKeyBinder::ShowCommandKeys(const ibKeyBinder::Command& command)
{
	const long row = m_commandList->FindItem(-1, static_cast<wxUIntPtr>(&command - m_commands.data()));
	if (row != wxNOT_FOUND)
		m_commandList->SetItem(row, kColumnKeys, JoinKeys(command.keys));
}

ibKeyBinder::Command* ibPanelKeyBinder::GetSelectedCommand()
{
	const long row = m_commandList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	return row != wxNOT_FOUND ? &m_commands[m_commandList->GetItemData(row)] : nullptr;
}

ibKeyBinder::Command* ibPanelKeyBinder::GetCommandForKey(const ibKeyBinder::Key& key, const ibKeyBinder::Command* except)
{
	for (ibKeyBinder::Command& command : m_commands) {
		if (&command == except)
			continue;
		for (const ibKeyBinder::Key& held : command.keys) {
			if (held == key)
				return &command;
		}
	}
	return nullptr;
}

void ibPanelKeyBinder::UpdateState()
{
	const ibKeyBinder::Command* const command = GetSelectedCommand();
	m_helpText->SetLabel(command != nullptr ? command->help : wxString());
	m_helpText->Wrap(GetClientSize().GetWidth() - FromDIP(10));

	const ibKeyBinder::Command* const holder = m_pressedKey.code != 0 ? GetCommandForKey(m_pressedKey, command) : nullptr;
	m_holderText->SetLabel(holder != nullptr
		? wxString::Format(_("Now on \"%s\" (%s); assigning moves it here."), holder->name, holder->group)
		: wxString());

	m_assignButton->Enable(command != nullptr && m_pressedKey.code != 0);
	m_removeButton->Enable(command != nullptr && !command->keys.empty());
	m_resetButton->Enable(command != nullptr && command->keys != command->defaultKeys);
	Layout();
}

void ibPanelKeyBinder::OnFilter(wxCommandEvent&)
{
	ShowCommands();
}

void ibPanelKeyBinder::OnCommandSelected(wxListEvent&)
{
	UpdateState();
}

void ibPanelKeyBinder::OnKeyPressed(wxKeyEvent& event)
{
	const int code = event.GetKeyCode();

	// Tab alone still walks the page, or the field would be a trap for the keyboard.
	if (code == WXK_TAB && !event.HasAnyModifiers()) {
		event.Skip();
		return;
	}

	// A modifier on its own is half a key: wait for the rest. (Not a switch: off the Mac RAW_CONTROL is CONTROL.)
	if (code == WXK_SHIFT || code == WXK_ALT || code == WXK_CONTROL || code == WXK_RAW_CONTROL
		|| code == WXK_WINDOWS_LEFT || code == WXK_WINDOWS_RIGHT || code == WXK_WINDOWS_MENU)
		return;

	ibKeyBinder::Key key;
	key.code = code;
	if (event.ControlDown())
		key.flags |= wxACCEL_CTRL;
	if (event.AltDown())
		key.flags |= wxACCEL_ALT;
	if (event.ShiftDown())
		key.flags |= wxACCEL_SHIFT;

	m_pressedKey = key;
	m_keyCtrl->ChangeValue(ibKeyBinder::GetKeyBindingAsText(key));
	UpdateState();
}

void ibPanelKeyBinder::OnAssign(wxCommandEvent&)
{
	ibKeyBinder::Command* const command = GetSelectedCommand();
	if (command == nullptr || m_pressedKey.code == 0)
		return;

	ibKeyBinder::Command* const holder = GetCommandForKey(m_pressedKey, command);
	if (holder != nullptr) {
		holder->keys.erase(std::remove(holder->keys.begin(), holder->keys.end(), m_pressedKey), holder->keys.end());
		ShowCommandKeys(*holder);
	}

	if (std::find(command->keys.begin(), command->keys.end(), m_pressedKey) == command->keys.end())
		command->keys.push_back(m_pressedKey);
	ShowCommandKeys(*command);
	UpdateState();
}

void ibPanelKeyBinder::OnRemove(wxCommandEvent&)
{
	ibKeyBinder::Command* const command = GetSelectedCommand();
	if (command == nullptr)
		return;

	command->keys.clear();
	ShowCommandKeys(*command);
	UpdateState();
}

void ibPanelKeyBinder::OnReset(wxCommandEvent&)
{
	ibKeyBinder::Command* const command = GetSelectedCommand();
	if (command == nullptr)
		return;

	command->keys = command->defaultKeys;
	ShowCommandKeys(*command);
	UpdateState();
}
