////////////////////////////////////////////////////////////////////////////
//	Description : The translation constructor (translateConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "translateConstructor.h"

#include "backend/metaData.h"
#include "backend/metaCollection/metaLanguageObject.h"
#include "backend/stringUtils.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

// What a person reads beside a box: the language's name and its code, or the code alone when that is
// all there is.
static wxString LanguageLabel(const wxString& code, const wxString& name)
{
	if (name.IsEmpty() || stringUtils::CompareString(name, code))
		return code;
	return wxString::Format(wxT("%s (%s)"), name, code);
}

ibDialogTranslateConstructor::ibDialogTranslateConstructor(wxWindow* parent, const wxString& title,
	const ibTranslateString& text, const ibMetaData* metaData, bool readOnly, int maxLength, const ibBoxEditor& boxEditor)
	: wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_original(text)
{
	// The configuration's languages, in its own order. A language of an extension is its owner's.
	std::vector<std::pair<wxString, wxString>> languages;   // code, what a person calls it
	if (metaData != nullptr) {
		ibMetaData* ownerRaw = nullptr;
		metaData->GetOwner(ownerRaw);
		const ibMetaData* owner = ownerRaw != nullptr ? ownerRaw : metaData;
		for (const auto language : owner->GetAnyArrayObject<ibValueMetaObjectLanguage>(g_metaLanguageCLSID))
			languages.emplace_back(language->GetLangCode(), language->GetSynonym());
	}
	// No configuration behind the text (a code runner's editor): the language in force is the one box
	// every text has.
	if (languages.empty())
		languages.emplace_back(ibBackendLocalization::GetUserLanguage(), wxString());

	const auto isLanguage = [&languages](const wxString& code) {
		for (const auto& language : languages)
			if (stringUtils::CompareString(language.first, code))
				return true;
		return false;
	};

	const int gap = FromDIP(6);
	const long boxStyle = wxTE_MULTILINE | (readOnly ? wxTE_READONLY : 0);

	// EXACTLY this language in its box (FindTranslate): a box filled with the substitute a reader
	// would see is stored as this language's own translation the moment OK is pressed.
	// ⭐ EVERY BOX GROWS WITH THE WINDOW, and all by the same share — as in the grid's window this one
	// replaced, where each box had a proportion of its own. A row is made growable as it is added, and
	// each grid takes as much of the height as it has rows (below), so a box in the second grid grows
	// exactly like one in the first.
	const auto addRow = [&](wxFlexGridSizer* grid, const wxString& code, const wxString& label) {
		grid->Add(new wxStaticText(this, wxID_ANY, label), wxSizerFlags().Border(wxTOP, FromDIP(3)));
		wxTextCtrl* box = new wxTextCtrl(this, wxID_ANY, m_original.FindTranslate(code),
			wxDefaultPosition, FromDIP(wxSize(360, 44)), boxStyle);
		if (maxLength > 0)
			box->SetMaxLength(maxLength);
		if (boxEditor) {
			// The box and its `...` share the one cell the box had, so the grid keeps its two columns.
			wxBoxSizer* cell = new wxBoxSizer(wxHORIZONTAL);
			cell->Add(box, wxSizerFlags(1).Expand());
			wxButton* edit = new wxButton(this, wxID_ANY, wxT("..."), wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
			edit->Bind(wxEVT_BUTTON, [this, box, label, boxEditor](wxCommandEvent&) {
				wxString boxText = box->GetValue();
				if (boxEditor(this, label, boxText))
					box->SetValue(boxText);
			});
			cell->Add(edit, wxSizerFlags().Top().Border(wxLEFT, FromDIP(3)));
			grid->Add(cell, wxSizerFlags().Expand());
		}
		else
			grid->Add(box, wxSizerFlags().Expand());
		grid->AddGrowableRow(grid->GetItemCount() / 2 - 1, 1);
		m_boxes.emplace_back(code, box);
	};

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* own = new wxFlexGridSizer(2, gap, gap);
	own->AddGrowableCol(1, 1);
	for (const auto& language : languages)
		addRow(own, language.first, LanguageLabel(language.first, language.second));
	top->Add(own, wxSizerFlags(static_cast<int>(languages.size())).Expand().Border(wxALL, gap * 2));

	// ⭐ WHAT THE TEXT HOLDS BEYOND THOSE LANGUAGES — shown apart, and kept. Rebuilding the text from the
	// configuration's languages is how a translation written for one nobody declared was lost unseen.
	wxFlexGridSizer* other = nullptr;
	wxSizerItem* otherItem = nullptr;
	int otherRows = 0;
	for (const ibBackendLocalizationEntry& entry : m_original.GetTranslations()) {
		if (isLanguage(entry.m_code))
			continue;
		if (other == nullptr) {
			top->Add(new wxStaticText(this, wxID_ANY, metaData != nullptr
					? _("Also in this text, for codes this configuration has no language for. They are kept:")
					: _("Also in this text:")),
				wxSizerFlags().Border(wxLEFT | wxRIGHT, gap * 2));
			other = new wxFlexGridSizer(2, gap, gap);
			other->AddGrowableCol(1, 1);
			otherItem = top->Add(other, wxSizerFlags().Expand().Border(wxALL, gap * 2));
		}
		addRow(other, entry.m_code, entry.m_code);
		++otherRows;
	}
	if (otherItem != nullptr)
		otherItem->SetProportion(otherRows);

	// Read-only has nothing to hand back, so it has nothing to confirm either.
	top->Add(CreateStdDialogButtonSizer(readOnly ? wxCANCEL : (wxOK | wxCANCEL)),
		wxSizerFlags().Right().Border(wxALL, gap * 2));

	SetSizerAndFit(top);
	CentreOnParent();

	// THE KEYBOARD GOES WHERE A TRANSLATION IS MISSING — that is what the window was opened to write —
	// or, when none is, to the language in force.
	Bind(wxEVT_INIT_DIALOG, [this](wxInitDialogEvent& event) {
		event.Skip();
		wxTextCtrl* focus = nullptr;
		for (const auto& box : m_boxes) {
			if (box.second->IsEmpty()) { focus = box.second; break; }
		}
		for (const auto& box : m_boxes) {
			if (focus == nullptr && stringUtils::CompareString(box.first, ibBackendLocalization::GetUserLanguage()))
				focus = box.second;
		}
		if (focus == nullptr && !m_boxes.empty())
			focus = m_boxes.front().second;
		if (focus != nullptr)
			focus->SetFocus();
	});
}

ibTranslateString ibDialogTranslateConstructor::GetTranslate() const
{
	ibBackendLocalizationEntryArray typed;
	for (const auto& box : m_boxes)
		typed.push_back(ibBackendLocalizationEntry{ box.first, box.second->GetValue() });
	return Collect(m_original, typed);
}

ibTranslateString ibDialogTranslateConstructor::Collect(const ibTranslateString& original,
	const ibBackendLocalizationEntryArray& boxes)
{
	// FROM THE TEXT THAT CAME IN, not from the boxes alone: its order stays, and a code no box was made
	// for is still in it.
	ibTranslateString collected = original;
	for (const ibBackendLocalizationEntry& box : boxes) {
		// AN EMPTY BOX IS "NOT TRANSLATED", not "translated to nothing" — see the header.
		if (box.m_data.IsEmpty())
			collected.RemoveTranslate(box.m_code);
		else
			collected.SetTranslate(box.m_code, box.m_data);
	}
	return collected;
}
