#include "frontend/win/dlgs/settings/savedSettings.h"

#include "backend/composition/dataComposer.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — the platform's own way to speak

#include <wx/sizer.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>

///////////////////////////////////////////////////////////////////////////////

bool ibDialogSavedSettings::Show(wxWindow* parent, ibDataComposer& composer, Mode mode,
                                 ibSettingsCategory category, const ibGuid& objectKey,
                                 const ibMetaData* metaData)
{
	if (!objectKey.isValid())
		return false;   // nothing for a shelf to be about

	wxWindow* over = parent != nullptr ? parent
		: ((wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr);

	ibDialogSavedSettings dialog(over, composer, mode, category, objectKey, metaData);
	dialog.ShowModal();
	// ⭐ WHAT CHANGED, not what the person pressed to leave. The main act closes the window and the
	// housekeeping does not — so "did anything happen" is a fact the window keeps, and Close after a
	// rename still reports true, because the rename did happen.
	return dialog.m_changed;
}

void ibDialogSavedSettings::ApplyDefault(ibDataComposer& composer, ibSettingsCategory category,
                                         const ibGuid& objectKey, const ibMetaData* metaData)
{
	if (!objectKey.isValid())
		return;

	switch (ibRestoreDefaultComposerSettings(category, objectKey, composer, metaData)) {
	case ibDefaultSettingsOutcome::Missing:
		// ⚠ SAID OUT LOUD (Max, 2026-08-26: *"and if it cannot find that setting afterwards, it
		// complains"*). The person marked something to come back on open and it did not: what they
		// then see is the author's settings, which looks exactly like the mark being ignored.
		ibValueSystemFunction::Message(
			_("The settings marked to be restored on open could not be found"),
			ibStatusMessage::ibStatusMessage_Warning);
		break;
	case ibDefaultSettingsOutcome::Restored:
	case ibDefaultSettingsOutcome::None:
		break;   // nothing to say: it worked, or nobody asked for anything
	}
}

ibDialogSavedSettings::ibDialogSavedSettings(wxWindow* parent, ibDataComposer& composer, Mode mode,
                                             ibSettingsCategory category, const ibGuid& objectKey,
                                             const ibMetaData* metaData)
	// ⚠ NO FromDIP IN THIS LIST. It is a method of the window being constructed — calling it before
	// the base has run asks a wxWindow that does not exist yet. The size is scaled below instead.
	: wxDialog(parent, wxID_ANY,
	           mode == Mode::Save ? _("Save settings") : _("Restore settings"),
	           wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_composer(composer), m_mode(mode), m_category(category), m_objectKey(objectKey), m_metaData(metaData)
{
	SetSize(FromDIP(wxSize(440, 320)));

	wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);

	// ONE COLUMN, NO HEADER — the entries are names, and a header over a single column of names says
	// nothing the window's own title has not said already.
	m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	m_list->InsertColumn(0, wxEmptyString, wxLIST_FORMAT_LEFT, FromDIP(290));

	wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);
	body->Add(m_list, 1, wxEXPAND | wxALL, FromDIP(6));

	wxBoxSizer* buttons = new wxBoxSizer(wxVERTICAL);

	// THE ACT THE WINDOW IS FOR, first and named after itself.
	m_btnMain = new wxButton(this, wxID_ANY, m_mode == Mode::Save ? _("Save") : _("Restore"));
	buttons->Add(m_btnMain, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	// …and, when saving, somewhere to put it that is not on the shelf yet.
	if (m_mode == Mode::Save) {
		// ⚠ ASCII "..." IN A LITERAL, not the "…" character. This file has no BOM and MSVC reads a
		// BOM-less source in the system codepage, so a UTF-8 ellipsis reaches the button as mojibake
		// ("NewвЂ¦" on screen). Every other button in the tree spells it with three dots for the same
		// reason — comments may hold anything, literals may not.
		m_btnNew = new wxButton(this, wxID_ANY, _("New..."));
		buttons->Add(m_btnNew, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
	}

	// ⭐ THE MARK IS OFFERED IN BOTH MODES (Max, 2026-08-26: *"beside it there is a button, set as
	// the main one"*) — saving a setting and declaring it the one to come back on open is one
	// thought, and having to reopen the other window to finish it splits it in two.
	//
	// Its caption states the ACT, not the state, because a person reads a button to find out what
	// pressing it does; WHICH entry currently carries the mark is said by the bold row.
	m_btnDefault = new wxButton(this, wxID_ANY, _("Restore on open"));
	buttons->Add(m_btnDefault, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

	m_btnRename = new wxButton(this, wxID_ANY, _("Rename..."));
	m_btnRemove = new wxButton(this, wxID_ANY, _("Delete"));
	buttons->Add(m_btnRename, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
	buttons->Add(m_btnRemove, 0, wxEXPAND | wxBOTTOM, FromDIP(4));

	body->Add(buttons, 0, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, FromDIP(6));

	outer->Add(body, 1, wxEXPAND);
	outer->Add(CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, FromDIP(6));
	SetSizer(outer);

	m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED,  &ibDialogSavedSettings::OnActivated, this);
	m_list->Bind(wxEVT_LIST_ITEM_SELECTED,   &ibDialogSavedSettings::OnSelectionChanged, this);
	m_list->Bind(wxEVT_LIST_ITEM_DESELECTED, &ibDialogSavedSettings::OnSelectionChanged, this);
	m_btnMain   ->Bind(wxEVT_BUTTON, &ibDialogSavedSettings::OnMainAct, this);
	if (m_btnNew != nullptr)
		m_btnNew->Bind(wxEVT_BUTTON, &ibDialogSavedSettings::OnNew, this);
	m_btnDefault->Bind(wxEVT_BUTTON, &ibDialogSavedSettings::OnDefault, this);
	m_btnRename ->Bind(wxEVT_BUTTON, &ibDialogSavedSettings::OnRename,  this);
	m_btnRemove ->Bind(wxEVT_BUTTON, &ibDialogSavedSettings::OnRemove,  this);

	Reload();
	CentreOnParent();
}

void ibDialogSavedSettings::Reload()
{
	// WHAT IS SELECTED SURVIVES A RELOAD — by ID, not by row: a rename re-sorts nothing, but a save
	// does (newest first), and a person watching their selection jump to another entry has been told
	// a lie about what they are about to press.
	const int was = Selected();
	const ibGuid keepId = was != wxNOT_FOUND && static_cast<size_t>(was) < m_entries.size()
		? m_entries[was].m_id : wxNullGuid;

	m_entries   = ibListComposerSettings(m_category, m_objectKey);
	m_defaultId = ibGetDefaultComposerSettings(m_objectKey);

	m_list->DeleteAllItems();
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const long row = m_list->InsertItem(static_cast<long>(i), m_entries[i].m_name);
		// ⭐ THE DEFAULT IS BOLD, and that is the whole indicator (Max, 2026-08-26). A mark in the
		// text ("(default)") would be part of the name people read and rename around; the weight of
		// the row is not.
		if (m_defaultId.isValid() && m_entries[i].m_id == m_defaultId) {
			wxFont font = m_list->GetFont();
			font.SetWeight(wxFONTWEIGHT_BOLD);
			m_list->SetItemFont(row, font);
		}
		if (m_entries[i].m_id == keepId)
			m_list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
	}

	UpdateButtons();
}

int ibDialogSavedSettings::Selected() const
{
	return static_cast<int>(m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED));
}

void ibDialogSavedSettings::UpdateButtons()
{
	// GREYED, NOT HIDDEN, while nothing is selected: the buttons are the window's vocabulary, and a
	// vocabulary that appears and disappears has to be re-read every time. "New…" stays live — it is
	// the one act that needs no entry, and on an empty shelf it is the only way forward.
	const bool any = Selected() != wxNOT_FOUND;
	m_btnMain   ->Enable(any);
	m_btnDefault->Enable(any);
	m_btnRename ->Enable(any);
	m_btnRemove ->Enable(any);
}

void ibDialogSavedSettings::OnSelectionChanged(wxListEvent& event)
{
	UpdateButtons();
	event.Skip();
}

bool ibDialogSavedSettings::SaveInto(const ibGuid& id, const wxString& name)
{
	// ⭐ THE ID DECIDES: empty = a new entry with a new identity; an existing one = that entry is
	// rewritten and KEEPS its identity, so the default mark goes on pointing at the same thing.
	if (!ibSaveComposerSettings(m_category, m_objectKey, id, name, m_composer).isValid()) {
		wxMessageBox(_("The settings could not be saved."), _("Save settings"), wxOK | wxICON_ERROR, this);
		return false;
	}
	m_changed = true;
	return true;
}

bool ibDialogSavedSettings::ActOnSelected()
{
	const int at = Selected();
	if (at == wxNOT_FOUND || static_cast<size_t>(at) >= m_entries.size())
		return false;

	if (m_mode == Mode::Save) {
		// PICKING AN EXISTING ENTRY IS SAYING "PUT IT HERE" — it was chosen from the list of what is
		// there, so there is nothing left to confirm.
		return SaveInto(m_entries[at].m_id, m_entries[at].m_name);
	}

	if (!ibRestoreComposerSettings(m_category, m_objectKey, m_entries[at].m_id, m_composer, m_metaData)) {
		// A REFUSAL IS SPOKEN. The row may hold a value whose type this configuration no longer has —
		// the read says so and the row stays; the person is told rather than left pressing an entry
		// that does nothing.
		wxMessageBox(wxString::Format(_("The settings named \"%s\" could not be restored."), m_entries[at].m_name),
			_("Saved settings"), wxOK | wxICON_ERROR, this);
		return false;
	}

	m_changed = true;
	return true;
}

void ibDialogSavedSettings::OnActivated(wxListEvent& WXUNUSED(event))
{
	// A DOUBLE CLICK IS THE WINDOW'S OWN ACT — restore it, or save into it, and leave.
	if (ActOnSelected())
		EndModal(wxID_OK);
}

void ibDialogSavedSettings::OnMainAct(wxCommandEvent& WXUNUSED(event))
{
	if (ActOnSelected())
		EndModal(wxID_OK);
}

void ibDialogSavedSettings::OnNew(wxCommandEvent& WXUNUSED(event))
{
	const wxString name = wxGetTextFromUser(_("Settings name"), _("Save settings"), wxEmptyString, this);
	if (name.IsEmpty())
		return;   // cancelled, or named nothing — a nameless shelf entry is hard to pick back

	// A NAME ALREADY ON THE SHELF is a replacement, and it is asked about rather than done —
	// otherwise two entries wear one caption and only their ids tell them apart.
	for (const ibComposerSettingsEntry& entry : m_entries)
		if (entry.m_name.IsSameAs(name, false)) {
			if (wxMessageBox(wxString::Format(_("Settings named \"%s\" already exist. Replace them?"), name),
					_("Save settings"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
				return;
			if (SaveInto(entry.m_id, name))
				EndModal(wxID_OK);
			return;
		}

	if (SaveInto(wxNullGuid, name))
		EndModal(wxID_OK);
}

void ibDialogSavedSettings::OnDefault(wxCommandEvent& WXUNUSED(event))
{
	const int at = Selected();
	if (at == wxNOT_FOUND || static_cast<size_t>(at) >= m_entries.size())
		return;

	// ⭐ THE SAME GESTURE UNDOES ITSELF: pressing it on the entry that already carries the mark
	// clears it, so "open it the way the author left it" needs no button of its own.
	const bool already = m_entries[at].m_id == m_defaultId;
	if (!ibSetDefaultComposerSettings(m_objectKey, already ? wxNullGuid : m_entries[at].m_id)) {
		wxMessageBox(_("The default settings could not be changed."), _("Saved settings"), wxOK | wxICON_ERROR, this);
		return;
	}

	m_changed = true;
	Reload();   // …and the bold row moves, which is the only feedback this needs
}

void ibDialogSavedSettings::OnRename(wxCommandEvent& WXUNUSED(event))
{
	const int at = Selected();
	if (at == wxNOT_FOUND || static_cast<size_t>(at) >= m_entries.size())
		return;

	const wxString renamed = wxGetTextFromUser(_("New name"), _("Rename settings"), m_entries[at].m_name, this);
	if (renamed.IsEmpty() || renamed == m_entries[at].m_name)
		return;

	// ⚠ RENAMING IS NOT RESTORING. The caption lives in the node, so this rewrites the node and never
	// touches the composer — a person renaming the entry they are NOT using must not have it applied
	// to them as a side effect.
	if (!ibRenameComposerSettings(m_category, m_objectKey, m_entries[at].m_id, renamed)) {
		wxMessageBox(_("The settings could not be renamed."), _("Rename settings"), wxOK | wxICON_ERROR, this);
		return;
	}

	m_changed = true;
	Reload();
}

void ibDialogSavedSettings::OnRemove(wxCommandEvent& WXUNUSED(event))
{
	const int at = Selected();
	if (at == wxNOT_FOUND || static_cast<size_t>(at) >= m_entries.size())
		return;

	if (wxMessageBox(wxString::Format(_("Delete the settings named \"%s\"?"), m_entries[at].m_name),
			_("Delete settings"), wxYES_NO | wxICON_QUESTION, this) != wxYES)
		return;

	if (!ibRemoveComposerSettings(m_category, m_objectKey, m_entries[at].m_id)) {
		wxMessageBox(_("The settings could not be deleted."), _("Delete settings"), wxOK | wxICON_ERROR, this);
		return;
	}

	m_changed = true;
	Reload();
}
