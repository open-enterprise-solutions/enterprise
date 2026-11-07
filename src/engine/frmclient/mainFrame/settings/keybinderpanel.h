#ifndef KEY_BINDER_PANEL_H
#define KEY_BINDER_PANEL_H

/////////////////////////////////////////////////////////////////////////////
// The keys page of the settings dialog: every command of the menu with its
// keys, a filter over them, and a field that takes the next key pressed.
// The page edits a copy of the commands: the caller hands the binder in
// (SetSettings) and takes it back after OK (GetSettings).
//
// A key belongs to one command: assigning a key another command holds moves
// it, since the window's accelerators could only ever fire one of the two.
/////////////////////////////////////////////////////////////////////////////

#include <wx/wx.h>
#include "gui/univ/ctrl/listCtrl.h"
#include "gui/univ/ctrl/searchCtrl.h"

#include "keybinder.h"
#include "gui/univ/ctrl/staticText.h"
#include "gui/univ/ctrl/button.h"
#include "gui/univ/ctrl/textCtrl.h"
#include "gui/univ/panel.h"

class FRMCLIENT_API ibPanelKeyBinder : public ibPanel
{
public:

	explicit ibPanelKeyBinder(wxWindow* parent);

	void SetSettings(const ibKeyBinder& keyBinder);
	ibKeyBinder GetSettings() const;

private:

	// The commands the filter lets through, one row each; a row's data is the command's index.
	void ShowCommands();
	void ShowCommandKeys(const ibKeyBinder::Command& command);

	// The command of the selected row, or nullptr.
	ibKeyBinder::Command* GetSelectedCommand();

	// The command other than `except` that holds the key, or nullptr.
	ibKeyBinder::Command* GetCommandForKey(const ibKeyBinder::Key& key, const ibKeyBinder::Command* except);

	// The buttons and the line under the key field, after a selection or a key.
	void UpdateState();

	void OnFilter(wxCommandEvent& event);
	void OnCommandSelected(wxListEvent& event);
	void OnKeyPressed(wxKeyEvent& event);
	void OnAssign(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnReset(wxCommandEvent& event);

	ibSearchCtrl* m_filterCtrl = nullptr;
	ibListCtrl* m_commandList = nullptr;
	ibStaticText* m_helpText = nullptr;
	ibTextCtrl* m_keyCtrl = nullptr;
	ibButton* m_assignButton = nullptr;
	ibStaticText* m_holderText = nullptr;
	ibButton* m_removeButton = nullptr;
	ibButton* m_resetButton = nullptr;

	std::vector<ibKeyBinder::Command> m_commands;

	// The key last pressed in the key field; a zero code when none.
	ibKeyBinder::Key m_pressedKey;
};

#endif
