/////////////////////////////////////////////////////////////////////////////
// ibSlashCommandPopup — autocomplete dropdown for the AI Assistant input.
//
// Pops up below the input wxTextCtrl when the user types '/' at the start
// of the field. Lists available slash commands with Russian descriptions
// (e.g. "/explain — Объяснить выделенный код"). Up/Down navigate, Enter
// selects, Esc dismisses. Selection invokes a host callback that splices
// the command into the input box.
//
// Implementation notes:
//   - wxPopupTransientWindow auto-dismisses on focus loss / outside click,
//     so the host doesn't need to police that itself.
//   - Inner widget is wxListBox; cheap, keyboard-friendly, and renders
//     identically across the three desktop platforms.
//   - The popup never grabs focus from the input — we forward key events
//     into the popup from the input's wxEVT_KEY_DOWN handler. This keeps
//     typing continuous and avoids the "click eats focus → popup vanishes"
//     trap seen in custom-grabbing popups.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_SLASH_COMMAND_POPUP_H_
#define _IB_SLASH_COMMAND_POPUP_H_

#include <wx/popupwin.h>
#include <wx/string.h>

#include <functional>
#include <vector>

class wxListBox;
class wxKeyEvent;
class wxCommandEvent;

class ibSlashCommandPopup : public wxPopupTransientWindow {
public:
	struct Command {
		wxString name;         // e.g. "/explain"
		wxString op;           // editor.skill op token: "explain"
		wxString description;  // localized, displayed after em-dash
	};

	using SelectCallback = std::function<void(const wxString& command)>;

	// `parent` is the wxTextCtrl (or any window) the popup attaches to;
	// `onSelect` is invoked with the chosen command string (e.g. "/fix")
	// when the user presses Enter or double-clicks a row.
	ibSlashCommandPopup(wxWindow* parent, SelectCallback onSelect);

	// Apply the user's current prefix (e.g. "/exp") and re-show the popup
	// positioned below `anchor`. If no commands match the prefix the popup
	// auto-dismisses. `anchor` is typically the input wxTextCtrl; the
	// popup matches its width and sits flush against its bottom edge.
	void UpdatePrefix(const wxString& prefix, wxWindow* anchor);

	// Move the highlighted row by ±delta, wrapping at the ends.
	void MoveSelection(int delta);

	// Fire the selection callback with the currently highlighted command
	// and dismiss the popup. No-op if no rows are visible.
	void AcceptSelection();

	// Static catalogue of supported commands. Stable across instances —
	// the pane consults this list when matching user-entered slash text
	// to a known op token. Localized at call time via wxGetTranslation.
	static const std::vector<Command>& AllCommands();

private:
	wxListBox*       m_list    = nullptr;
	SelectCallback   m_onSelect;
	std::vector<int> m_visibleIdx;  // m_list row i → AllCommands() index

	void OnListDClick(wxCommandEvent& event);
};

#endif // _IB_SLASH_COMMAND_POPUP_H_
