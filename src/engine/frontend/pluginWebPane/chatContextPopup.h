/////////////////////////////////////////////////////////////////////////////
// ibChatContextPopup — autocomplete dropdown for "@" context tokens.
//
// Companion to ibSlashCommandPopup, but anchored to '@' instead of '/'.
// Pops up below the input wxTextCtrl when the user types '@'. Lists
// matching metadata objects ("Справочник.Counterparties") and synthetic
// editor tokens ("@selection", "@file", "@open"). Up/Down navigate,
// Enter selects, Esc dismisses.
//
// Lifecycle and keyboard / focus model mirror ibSlashCommandPopup — see
// that header for the full rationale (no focus grab; host forwards key
// events from the input).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_CHAT_CONTEXT_POPUP_H_
#define _IB_CHAT_CONTEXT_POPUP_H_

#include "frontend/pluginWebPane/chatContext.h"

#include <wx/popupwin.h>
#include <wx/string.h>

#include <functional>
#include <vector>

class wxListBox;
class wxCommandEvent;

class ibChatContextPopup : public wxPopupTransientWindow {
public:
	using SelectCallback = std::function<void(const wxString& fullName)>;

	ibChatContextPopup(wxWindow* parent, SelectCallback onSelect);

	// Apply current prefix (the partial text after '@'), refresh the
	// suggestion list, and re-show below `anchor`. If no rows match,
	// dismiss instead of leaving an empty popup hanging.
	void UpdatePrefix(const wxString& prefix, wxWindow* anchor);

	void MoveSelection(int delta);
	void AcceptSelection();

private:
	wxListBox*                            m_list    = nullptr;
	SelectCallback                        m_onSelect;
	std::vector<ibChatContext::Suggestion> m_suggestions;

	void OnListDClick(wxCommandEvent& event);
};

#endif // _IB_CHAT_CONTEXT_POPUP_H_
