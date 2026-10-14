

#include "keybinderdialog.h"
#include "keymonitortextctrl.h"
#include "stlutility.h"

#include <map>
#include <algorithm>
#include <functional>
#include <assert.h>

BEGIN_EVENT_TABLE(ibDialogKeyBinder, wxPanel)
EVT_TREE_SEL_CHANGED(ibDialogKeyBinder::ID_CommandTree, ibDialogKeyBinder::OnCommandTreeSelectionChanged)
EVT_BUTTON(ibDialogKeyBinder::ID_Add, ibDialogKeyBinder::OnAdd)
EVT_BUTTON(ibDialogKeyBinder::ID_Remove, ibDialogKeyBinder::OnRemove)
EVT_BUTTON(ibDialogKeyBinder::ID_RemoveAll, ibDialogKeyBinder::OnRemoveAll)
EVT_LISTBOX(ibDialogKeyBinder::ID_ShortcutsList, ibDialogKeyBinder::OnShortcutsListItemSelected)
EVT_TEXT(ibDialogKeyBinder::ID_KeyMonitor, ibDialogKeyBinder::OnKeyMonitorTextChanged)
END_EVENT_TABLE()

ibDialogKeyBinder::ibDialogKeyBinder(wxWindow* parent, int id, wxPoint pos, wxSize size, int style)
	: wxPanel(parent, id, pos, size, style)
{

	wxFlexGridSizer* fgSizer1;
	fgSizer1 = new wxFlexGridSizer(2, 1, 0, 0);
	fgSizer1->AddGrowableCol(0);
	fgSizer1->AddGrowableRow(0);
	fgSizer1->SetFlexibleDirection(wxBOTH);

	wxFlexGridSizer* fgSizer2;
	fgSizer2 = new wxFlexGridSizer(1, 2, 0, 0);
	fgSizer2->AddGrowableCol(0);
	fgSizer2->AddGrowableRow(0);
	fgSizer2->SetFlexibleDirection(wxBOTH);
	fgSizer2->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_NONE);

	wxFlexGridSizer* fgSizer3;
	fgSizer3 = new wxFlexGridSizer(2, 1, 0, 0);
	fgSizer3->AddGrowableCol(0);
	fgSizer3->AddGrowableRow(1);
	fgSizer3->SetFlexibleDirection(wxBOTH);

	m_staticText1 = new wxStaticText(this, wxID_ANY, _("Commands:"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer3->Add(m_staticText1, 0, wxALL, FromDIP(5));

	m_commandTreeCtrl = new wxTreeCtrl(this, ID_CommandTree, wxDefaultPosition, wxDefaultSize, wxTR_DEFAULT_STYLE);
	fgSizer3->Add(m_commandTreeCtrl, 0, wxALL | wxEXPAND, FromDIP(5));

	m_commandTreeCtrl->SetDoubleBuffered(true);

	fgSizer2->Add(fgSizer3, 1, wxEXPAND, FromDIP(5));

	wxFlexGridSizer* fgSizer5 = new wxFlexGridSizer(8, 1, 0, 0);
	fgSizer5->AddGrowableRow(1);
	fgSizer5->SetFlexibleDirection(wxVERTICAL);

	m_staticText2 = new wxStaticText(this, wxID_ANY, _("Current Shortcuts:"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer5->Add(m_staticText2, 0, wxALL, FromDIP(5));

	m_shortcutsListCtrl = new wxListBox(this, ID_ShortcutsList, wxDefaultPosition, wxDefaultSize, 0, nullptr, 0);
	fgSizer5->Add(m_shortcutsListCtrl, 0, wxALL | wxEXPAND, FromDIP(5));

	wxBoxSizer* bSizer1 = new wxBoxSizer(wxHORIZONTAL);

	m_removeButton = new wxButton(this, ID_Remove, _("Remove"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer1->Add(m_removeButton, 0, wxALL, FromDIP(5));

	m_removeAllButton = new wxButton(this, ID_RemoveAll, _("Remove All"), wxDefaultPosition, wxDefaultSize, 0);
	bSizer1->Add(m_removeAllButton, 0, wxALL, FromDIP(5));

	fgSizer5->Add(bSizer1, 1, wxEXPAND, FromDIP(5));

	m_staticText3 = new wxStaticText(this, wxID_ANY, _("New:"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer5->Add(m_staticText3, 0, wxALL, FromDIP(5));

	m_keyMonitorCtrl = new ibTextCtrlKeyMonitor(this, ID_KeyMonitor, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	fgSizer5->Add(m_keyMonitorCtrl, 0, wxALL | wxEXPAND, FromDIP(5));

	m_staticText4 = new wxStaticText(this, wxID_ANY, _("Currently Assigned:"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer5->Add(m_staticText4, 0, wxALL, FromDIP(5));

	m_currentlyAssignedCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	fgSizer5->Add(m_currentlyAssignedCtrl, 0, wxALL | wxEXPAND, FromDIP(5));

	m_addButton = new wxButton(this, ID_Add, _("Add"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer5->Add(m_addButton, 0, wxALL | wxEXPAND, FromDIP(5));

	fgSizer2->Add(fgSizer5, 1, wxEXPAND, FromDIP(5));

	fgSizer1->Add(fgSizer2, 1, wxEXPAND, FromDIP(5));

	wxFlexGridSizer* fgSizer6 = new wxFlexGridSizer(3, 1, 0, 0);
	fgSizer6->AddGrowableCol(0);
	fgSizer6->AddGrowableRow(1);
	fgSizer6->SetFlexibleDirection(wxBOTH);

	m_staticText9 = new wxStaticText(this, wxID_ANY, _("Description:"), wxDefaultPosition, wxDefaultSize, 0);
	fgSizer6->Add(m_staticText9, 0, wxALL, FromDIP(5));

	m_descriptionCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	m_descriptionCtrl->SetMinSize(wxSize(-1, 50));

	fgSizer6->Add(m_descriptionCtrl, 1, wxALL | wxEXPAND, FromDIP(5));

	fgSizer1->Add(fgSizer6, 1, wxEXPAND, FromDIP(5));

	this->SetSizer(fgSizer1);
	this->Layout();

}

ibDialogKeyBinder::~ibDialogKeyBinder()
{
	ClearVector(m_commands);
}

void ibDialogKeyBinder::Initialize()
{
	// Add the commands to the tree. Group strings can be hierarchical —
	// AddCommandsFromMenu builds them as "Top / Sub / Subsub" so that
	// menu nesting (File → Recent → Clear) maps onto a nested tree here
	// instead of one flat group line per leaf submenu.
	wxTreeItemId root = m_commandTreeCtrl->AddRoot(_("Commands"));
	std::map<wxString, wxTreeItemId> groupNodeByPath;

	std::function<wxTreeItemId(const wxString&)> getOrCreateGroup =
		[&](const wxString& path) -> wxTreeItemId {
		auto it = groupNodeByPath.find(path);
		if (it != groupNodeByPath.end())
			return it->second;

		// Find parent path (everything before the last " / ") and the
		// leaf label (everything after). Top-level group has no parent
		// separator and parents to root.
		const wxString sep = wxT(" / ");
		const size_t cut = path.rfind(sep);
		wxTreeItemId parent;
		wxString label;
		if (cut == wxString::npos) {
			parent = root;
			label  = path;
		} else {
			parent = getOrCreateGroup(path.substr(0, cut));
			label  = path.substr(cut + sep.length());
		}
		wxTreeItemId node = m_commandTreeCtrl->AppendItem(parent, label);
		groupNodeByPath.emplace(path, node);
		return node;
	};

	for (unsigned int i = 0; i < m_commands.size(); ++i)
	{
		wxTreeItemId groupNode = getOrCreateGroup(m_commands[i]->group);

		CommandData* data = new CommandData;
		data->command = m_commands[i];

		// Remove the & characters from the label.
		wxString label = m_commands[i]->name;
		label.Replace("&", "");

		m_commandTreeCtrl->AppendItem(groupNode, label, -1, -1, data);
	}

	m_commandTreeCtrl->Expand(root);
}

void ibDialogKeyBinder::OnCommandTreeSelectionChanged(wxTreeEvent& event)
{
	m_shortcutsListCtrl->Clear();
	m_descriptionCtrl->Clear();
	m_keyMonitorCtrl->Clear();

	CommandData* data = static_cast<CommandData*>(m_commandTreeCtrl->GetItemData(event.GetItem()));

	if (data != nullptr)
	{

		ibKeyBinder::Command* command = data->command;

		for (unsigned int i = 0; i < command->keys.size(); ++i)
		{
			wxString text = ibKeyBinder::GetKeyBindingAsText(command->keys[i]);
			m_shortcutsListCtrl->Append(text);
		}

		m_descriptionCtrl->AppendText(command->help);

	}

	// Enable or disable the controls based on whether or not we have a command
	// selected.

	m_keyMonitorCtrl->Enable(data != nullptr);
	m_shortcutsListCtrl->Enable(data != nullptr);
	m_removeAllButton->Enable(data != nullptr);
	m_removeButton->Enable(false);
	m_removeAllButton->Enable(!m_shortcutsListCtrl->IsEmpty());

}

void ibDialogKeyBinder::OnAdd(wxCommandEvent& event)
{
	wxTreeItemId item = m_commandTreeCtrl->GetSelection();

	if (item.IsOk())
	{
		CommandData* data = static_cast<CommandData*>(m_commandTreeCtrl->GetItemData(item));
		assert(data != nullptr);

		if (data != nullptr)
		{
			data->command->keys.push_back(ibKeyBinder::GetTextAsKeyBinding(m_keyMonitorCtrl->GetValue()));
			m_shortcutsListCtrl->Append(m_keyMonitorCtrl->GetValue());
			m_removeAllButton->Enable(true);
		}
	}
}

void ibDialogKeyBinder::OnRemove(wxCommandEvent& event)
{

	wxTreeItemId item = m_commandTreeCtrl->GetSelection();

	if (item.IsOk())
	{

		CommandData* data = static_cast<CommandData*>(m_commandTreeCtrl->GetItemData(item));
		assert(data != nullptr);

		if (data != nullptr)
		{

			unsigned int index = m_shortcutsListCtrl->GetSelection();
			assert(index != wxNOT_FOUND);

			data->command->keys.erase(data->command->keys.begin() + index);
			m_shortcutsListCtrl->Delete(index);

			m_removeButton->Enable(false);
			m_removeAllButton->Enable(!m_shortcutsListCtrl->IsEmpty());

		}

	}
}

void ibDialogKeyBinder::OnRemoveAll(wxCommandEvent& event)
{
	wxTreeItemId item = m_commandTreeCtrl->GetSelection();

	if (item.IsOk())
	{
		CommandData* data = static_cast<CommandData*>(m_commandTreeCtrl->GetItemData(item));
		assert(data != nullptr);

		if (data != nullptr)
		{

			data->command->keys.clear();
			m_shortcutsListCtrl->Clear();

			m_removeButton->Enable(false);
			m_removeAllButton->Enable(false);

		}
	}
}

void ibDialogKeyBinder::OnShortcutsListItemSelected(wxCommandEvent& event)
{
	m_removeButton->Enable(m_shortcutsListCtrl->GetSelection() != wxNOT_FOUND);
}

void ibDialogKeyBinder::OnKeyMonitorTextChanged(wxCommandEvent& event)
{
	m_currentlyAssignedCtrl->SetValue(wxEmptyString);

	// Find what the key is currently bound to (if anything).
	ibKeyBinder::Key key = ibKeyBinder::GetTextAsKeyBinding(m_keyMonitorCtrl->GetValue());
	m_addButton->Enable(key.code != 0);

	for (unsigned int i = 0; i < m_commands.size(); ++i)
	{
		for (unsigned int j = 0; j < m_commands[i]->keys.size(); ++j)
		{
			if (m_commands[i]->keys[j].code == key.code &&
				m_commands[i]->keys[j].flags == key.flags)
			{
				m_currentlyAssignedCtrl->SetValue(m_commands[i]->name);
				return;
			}
		}
	}
}

void ibDialogKeyBinder::AddCommand(const ibKeyBinder::Command& command)
{
	m_commands.push_back(new ibKeyBinder::Command(command));
}

unsigned int ibDialogKeyBinder::GetNumCommands() const
{
	return m_commands.size();
}

const ibKeyBinder::Command& ibDialogKeyBinder::GetCommand(unsigned int i) const
{
	return *m_commands[i];
}

