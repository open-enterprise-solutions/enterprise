/////////////////////////////////////////////////////////////////////////////
// ibHelpChooserDialog — "Вибір розділу" ambiguous-name picker.
//
// 1C/BAS convention: modal dialog, three buttons (Показати / Відмінити /
// Довідка). Default action is Показати (Enter); Esc cancels.
/////////////////////////////////////////////////////////////////////////////

#include "frontend/help/helpChooserDialog.h"

#include "backend/help/helpEntry.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace {

constexpr int IDC_ShowSelected = wxID_HIGHEST + 1;
constexpr int IDC_OpenHelp     = wxID_HIGHEST + 2;

wxString CategoryPath(const ibHelpEntry& e) {
	// Render category_keys joined by '/'. Phase 3 ships keys (no
	// per-locale dictionary lookup yet) — Phase 5 will substitute
	// localized display names.
	wxString out;
	for (const wxString& seg : e.categoryKeys) {
		if (!out.IsEmpty()) out += wxT(" / ");
		out += seg;
	}
	if (out.IsEmpty()) out = e.nameLocal.IsEmpty() ? e.nameEn
	                                                  : e.nameLocal;
	return out;
}

} // namespace

ibHelpChooserDialog::ibHelpChooserDialog(
    wxWindow* parent,
    const std::vector<const ibHelpEntry*>& candidates)
    : wxDialog(parent, wxID_ANY, _("Вибір розділу"),
                wxDefaultPosition, wxSize(560, 360),
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {

	auto* label = new wxStaticText(this, wxID_ANY,
	                                 _("Виберіть розділ зі списку:"));

	m_list = new wxListBox(this, wxID_ANY);
	for (const ibHelpEntry* e : candidates) {
		if (e == nullptr) continue;
		const wxString display =
		    CategoryPath(*e) + wxT("  (") +
		    (e->nameLocal.IsEmpty() ? e->nameEn : e->nameLocal) +
		    wxT(")");
		m_list->Append(display);
		m_ids.push_back(e->id);
	}
	if (!m_ids.empty()) m_list->SetSelection(0);

	auto* btnShow   = new wxButton(this, IDC_ShowSelected, _("Показати"));
	auto* btnCancel = new wxButton(this, wxID_CANCEL,      _("Відмінити"));
	auto* btnHelp   = new wxButton(this, IDC_OpenHelp,     _("Довідка"));
	btnShow->SetDefault();

	auto* buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->AddStretchSpacer();
	buttons->Add(btnShow,   0, wxALL, 4);
	buttons->Add(btnCancel, 0, wxALL, 4);
	buttons->Add(btnHelp,   0, wxALL, 4);

	auto* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(label,  0, wxALL,             8);
	sizer->Add(m_list, 1, wxEXPAND | wxALL, 8);
	sizer->Add(buttons,0, wxEXPAND | wxALL, 4);
	SetSizer(sizer);

	Bind(wxEVT_BUTTON, &ibHelpChooserDialog::OnShow,   this, IDC_ShowSelected);
	Bind(wxEVT_BUTTON, &ibHelpChooserDialog::OnCancel, this, wxID_CANCEL);
	Bind(wxEVT_BUTTON, &ibHelpChooserDialog::OnHelp,   this, IDC_OpenHelp);
	Bind(wxEVT_LISTBOX_DCLICK,
	     [this](wxCommandEvent&) {
		     wxCommandEvent fake(wxEVT_BUTTON, IDC_ShowSelected);
		     OnShow(fake);
	     });
}

void ibHelpChooserDialog::OnShow(wxCommandEvent&) {
	const int idx = m_list ? m_list->GetSelection() : wxNOT_FOUND;
	if (idx < 0 || idx >= static_cast<int>(m_ids.size())) {
		EndModal(wxID_CANCEL);
		return;
	}
	m_selectedId = m_ids[idx];
	EndModal(wxID_OK);
}

void ibHelpChooserDialog::OnCancel(wxCommandEvent&) {
	EndModal(wxID_CANCEL);
}

void ibHelpChooserDialog::OnHelp(wxCommandEvent&) {
	m_helpRequested = true;
	EndModal(wxID_OK);
}
