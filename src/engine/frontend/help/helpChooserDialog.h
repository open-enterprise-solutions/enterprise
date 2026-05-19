/////////////////////////////////////////////////////////////////////////////
// "Вибір розділу" — ambiguous-name chooser dialog.
//
// Modal wxDialog that lists 2+ candidate entries (returned by
// ResolveByName) and asks the user to pick one. Mirrors the 1C / BAS
// chooser layout exactly: list, three buttons (Показати / Відмінити /
// Довідка). Default action (Enter) is Показати; Esc cancels.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_CHOOSER_DIALOG_H_
#define _IB_HELP_CHOOSER_DIALOG_H_

#include <wx/dialog.h>
#include <wx/listbox.h>

#include <vector>

struct ibHelpEntry;

class ibHelpChooserDialog : public wxDialog {
public:
	ibHelpChooserDialog(wxWindow* parent,
	                    const std::vector<const ibHelpEntry*>& candidates);

	// Available after ShowModal returns wxID_OK. Empty string on cancel
	// or when the user clicked Довідка (the Help button opens the
	// "About the syntax helper" entry rather than selecting from the
	// candidate list).
	wxString GetSelectedId() const { return m_selectedId; }

	// True when the user clicked "Довідка" (Help). Host frame should
	// open the help-on-the-helper entry rather than a candidate.
	bool HelpRequested() const { return m_helpRequested; }

private:
	wxListBox* m_list = nullptr;
	std::vector<wxString> m_ids;

	wxString m_selectedId;
	bool     m_helpRequested = false;

	void OnShow(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnHelp(wxCommandEvent& event);
};

#endif // _IB_HELP_CHOOSER_DIALOG_H_
