#include "frontend/win/dlgs/settings/settingsAppearanceEditor.h"

#include "frontend/win/dlgs/settings/settingsStyle.h"   // ibStyleSettingsGrid — how a settings list looks
#include "frontend/win/dlgs/textWithDotsCell.h"         // the value cell: typed, or "..." for its own window
#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"   // a text parameter, every language of it
#include "frontend/win/dlgs/formatConstructor/formatConstructor.h"         // ibFormatBoxEditor — a format, a language at a time
#include "backend/backend_localization.h"               // ibTranslateString — a text read in the language in force
#include "backend/system/value/valueColour.h"            // a colour parameter, kept as a value
#include "backend/system/value/valueFont.h"              // …and a font
#include "backend/system/value/valueSpreadsheet.h"       // …and an alignment, the sheet's own enumeration
#include "backend/spreadsheetDescription.h"              // ibDefaultSpreadsheetFont() — the font a font is chosen from

#include <wx/choicdlg.h>   // wxGetSingleChoiceIndex — an alignment is one of three words
#include <wx/colordlg.h>   // wxColourDialog — a colour is chosen in the system's window
#include <wx/fontdlg.h>    // wxFontDialog — …and a font
#include <wx/dialog.h>
#include <wx/settings.h>   // wxSystemSettings — an unticked value reads grey
#include <wx/sizer.h>

namespace {

// THE TEXT A PARAMETER KEEPS, ticked or not — what its value cell edits. (Never a reference: an appearance
// reads back without a configuration — see ibAppearanceValueDescription.)
wxString KeptText(const ibAppearanceDescription& appearance, ibAppearanceParameter parameter)
{
	const ibAppearanceValueDescription* value = appearance.Find(parameter);
	// ⚠ BOTH ARMS wxString: an ibString beside a wxString is ambiguous on GCC and clang — each converts to the
	// other — and MSVC's silence about it cost the Linux and macOS builds of fd601790d their frontend.
	return value != nullptr ? ibStoredValue(value->m_value, nullptr).GetString().ToWxString() : wxString();
}

bool IsTicked(const ibAppearanceDescription& appearance, ibAppearanceParameter parameter)
{
	const ibAppearanceValueDescription* value = appearance.Find(parameter);
	return value != nullptr && value->m_use;
}

// A PARAMETER'S TEXT, written anew — ticked when there is something to apply, the way a value put beside a
// setting switches it on; emptied, the parameter is forgotten (the door's own rule, Say).
void SayText(ibAppearanceDescription& appearance, ibAppearanceParameter parameter, const wxString& raw)
{
	appearance.Say(parameter, !raw.IsEmpty(), raw.IsEmpty() ? ibValue() : ibValue(raw));
}

// WHICH PARAMETERS ARE WRITTEN AS TEXT — typed into the cell, in every language. The others are a colour, a font
// or an alignment: chosen in their own window, never typed.
bool IsTyped(ibAppearanceParameter parameter)
{
	return parameter == ibAppearanceParameter::Format || parameter == ibAppearanceParameter::Text;
}

// ONE LINE PER PARAMETER OF THE PLATFORM'S LIST — the tick, what it is called, its value.
class ibAppearanceModel : public ibDataViewVirtualListModel {
public:
	enum { kColUse = 0, kColParameter, kColValue };

	ibAppearanceModel(ibAppearanceDescription* appearance, bool readOnly)
		: ibDataViewVirtualListModel((unsigned int)ibAppearanceParameters().size()),
		  m_appearance(appearance), m_readOnly(readOnly) {
	}

	static bool IsRow(unsigned row) { return row < ibAppearanceParameters().size(); }
	static ibAppearanceParameter ParameterAt(unsigned row) { return ibAppearanceParameters()[row]; }

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		if (!IsRow(row))
			return;
		const ibAppearanceParameter parameter = ParameterAt(row);
		if (col == kColUse)
			variant = IsTicked(*m_appearance, parameter);
		else if (col == kColParameter)
			variant = ibAppearanceParameterCaption(parameter);
		else if (col == kColValue)
			// A TEXT IN SEVERAL LANGUAGES reads in the one in force, as a title does.
			variant = ibTranslateString(KeptText(*m_appearance, parameter)).GetString();
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (!IsRow(row) || m_readOnly)
			return false;
		const ibAppearanceParameter parameter = ParameterAt(row);
		const wxString kept = KeptText(*m_appearance, parameter);
		if (col == kColUse) {
			if (variant.GetBool() == IsTicked(*m_appearance, parameter))
				return false;
			// THE TICK KEEPS THE VALUE AS IT IS — a colour stays a colour; only the word "in force" changes.
			const ibAppearanceValueDescription* said = m_appearance->Find(parameter);
			m_appearance->Say(parameter, variant.GetBool(),
				said != nullptr ? ibStoredValue(said->m_value, nullptr) : ibValue());
			return true;
		}
		if (col != kColValue)
			return false;

		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		// A COLOUR, A FONT, AN ALIGNMENT ARE CHOSEN, NOT TYPED — their "..." opens their window. Emptied (the
		// cell's "×"), the parameter is forgotten.
		if (!IsTyped(parameter)) {
			if (!text.IsEmpty() || kept.IsEmpty())
				return false;
			m_appearance->Say(parameter, false, ibValue());
			return true;
		}

		// TYPED IN THE CELL — the language in force; the others stay as they were (the "..." edits them all).
		// Emptied, that language is taken out.
		ibTranslateString translated(kept);
		if (text == translated.GetString())
			return false;
		if (text.IsEmpty())
			translated.RemoveTranslate(ibBackendLocalization::GetUserLanguage());
		else
			translated.SetTranslate(text);
		SayText(*m_appearance, parameter, translated.GetRawText());
		return true;
	}

	// AN UNTICKED VALUE READS GREY — kept, not applied.
	bool GetAttrByRow(unsigned row, unsigned col, ibDataViewItemAttr& attr) const override {
		if (col == kColValue && IsRow(row) && !IsTicked(*m_appearance, ParameterAt(row))) {
			attr.SetColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			return true;
		}
		return false;
	}

	bool IsEnabledByRow(unsigned int, unsigned int col) const override {
		return !m_readOnly && col != kColParameter;
	}

private:
	ibAppearanceDescription* m_appearance;
	bool                     m_readOnly;
};

class ibDialogAppearance : public wxDialog {
public:
	ibDialogAppearance(wxWindow* parent, const ibAppearanceDescription& appearance, const ibMetaData* metaData,
		bool readOnly)
		: wxDialog(parent, wxID_ANY, _("Appearance"), wxDefaultPosition, wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  m_edited(appearance), m_metaData(metaData), m_readOnly(readOnly)
	{
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

		m_view = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(460, 180)),
			wxDV_ROW_LINES | wxDV_SINGLE);
		ibStyleSettingsGrid(m_view);
		m_model = new ibAppearanceModel(&m_edited, m_readOnly);
		m_view->AssociateModel(m_model);

		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(wxEmptyString,
			new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
			ibAppearanceModel::kColUse, FromDIP(24), wxAlignment::wxALIGN_CENTER));
		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Parameter"),
			new ibDataViewTextRenderer(), ibAppearanceModel::kColParameter, FromDIP(160), wxAlignment::wxALIGN_LEFT));
		// Typed in the cell for the language in force; the "..." opens the parameter's own window.
		m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Value"),
			new ibTextWithDotsRenderer(this, [this](wxString& text) -> bool { return EditValue(text); }),
			ibAppearanceModel::kColValue, FromDIP(260), wxAlignment::wxALIGN_LEFT));

		// A DOUBLE-CLICK (or Enter) EDITS THE CELL under the cursor — the same gesture the settings grids use.
		m_view->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
			m_view->EditItem(e.GetItem(), e.GetDataViewColumn());
			e.Skip();
		});

		sizer->Add(m_view, 1, wxEXPAND | wxALL, FromDIP(4));
		sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(6));
		SetSizerAndFit(sizer);
		CentreOnParent();
	}

	const ibAppearanceDescription& GetAppearance() const { return m_edited; }

private:
	// THE "..." OF A LINE — the window its parameter is written in. A format: every language at once, each
	// through the format string constructor, the way a column's Format property opens (ibFormatBoxEditor).
	bool EditValue(wxString& text) {
		const ibDataViewItem item = m_view->GetSelection();
		if (!item.IsOk())
			return false;
		const unsigned row = m_model->GetRow(item);
		if (!ibAppearanceModel::IsRow(row))
			return false;
		const ibAppearanceParameter parameter = ibAppearanceModel::ParameterAt(row);

		switch (parameter) {
		case ibAppearanceParameter::Format: {
			const ibTranslateString before(KeptText(m_edited, parameter));
			ibDialogTranslateConstructor dialog(this, ibAppearanceParameterCaption(parameter), before, m_metaData,
				m_readOnly, 0, ibFormatBoxEditor(m_readOnly));
			if (dialog.ShowModal() != wxID_OK)
				return false;
			const ibTranslateString after = dialog.GetTranslate();
			if (after == before)
				return false;
			SayText(m_edited, parameter, after.GetRawText());
			text = after.GetString();
			m_view->Refresh();   // the tick beside it may have changed with it
			return true;
		}
		// WHAT THE CELL SAYS instead of its value — every language, as a title is written.
		case ibAppearanceParameter::Text: {
			const ibTranslateString before(KeptText(m_edited, parameter));
			ibDialogTranslateConstructor dialog(this, ibAppearanceParameterCaption(parameter), before, m_metaData,
				m_readOnly);
			if (dialog.ShowModal() != wxID_OK)
				return false;
			const ibTranslateString after = dialog.GetTranslate();
			if (after == before)
				return false;
			SayText(m_edited, parameter, after.GetRawText());
			text = after.GetString();
			m_view->Refresh();
			return true;
		}
		// A COLOUR — the system's own colour window, standing on the colour kept.
		case ibAppearanceParameter::BackgroundColour:
		case ibAppearanceParameter::TextColour: {
			wxColourData data;
			if (const ibAppearanceValueDescription* said = m_edited.Find(parameter)) {
				ibValue kept = ibStoredValue(said->m_value, nullptr);
				ibValueColour* colour = nullptr;
				if (kept.ConvertToValue(colour) && colour != nullptr && colour->m_colour.IsOk())
					data.SetColour(colour->m_colour);
			}
			wxColourDialog dialog(this, &data);
			if (dialog.ShowModal() != wxID_OK)
				return false;
			return Choose(parameter, ibValue(new ibValueColour(dialog.GetColourData().GetColour())), text);
		}
		// A FONT — the system's own font window, standing on the font kept, else on the REPORT'S own: what is chosen
		// is read as what it changes of that one (ibCompositionFont::Of), so ticking italic says italic and nothing more.
		case ibAppearanceParameter::Font: {
			wxFontData data;
			data.SetInitialFont(ibDefaultSpreadsheetFont());
			if (const ibAppearanceValueDescription* said = m_edited.Find(parameter)) {
				ibValue kept = ibStoredValue(said->m_value, nullptr);
				ibValueFont* font = nullptr;
				if (kept.ConvertToValue(font) && font != nullptr && font->m_font.IsOk())
					data.SetInitialFont(font->m_font);
			}
			wxFontDialog dialog(this, data);
			if (dialog.ShowModal() != wxID_OK)
				return false;
			return Choose(parameter, ibValue(new ibValueFont(dialog.GetFontData().GetChosenFont())), text);
		}
		// AN ALIGNMENT — one of three words, picked in a small window of its own (the way the other settings pick
		// a theme), in the sheet's own vocabulary.
		case ibAppearanceParameter::HorizontalAlignment: {
			const ibSpreadsheetAlignmentHorz offered[] = { ibAlignmentHorz_Left, ibAlignmentHorz_Center,
				ibAlignmentHorz_Right };
			wxArrayString labels;
			for (const ibSpreadsheetAlignmentHorz one : offered)
				labels.Add(ibValue::CreateEnumObject<ibValueEnumSpreadsheetHorizontalAlignment>(one).GetString());
			const int current = labels.Index(KeptText(m_edited, parameter));
			const int chosen = wxGetSingleChoiceIndex(ibAppearanceParameterCaption(parameter), _("Appearance"),
				labels, current != wxNOT_FOUND ? current : 0, this);
			if (chosen < 0 || chosen >= static_cast<int>(labels.size()))
				return false;
			return Choose(parameter,
				ibValue::CreateEnumObject<ibValueEnumSpreadsheetHorizontalAlignment>(offered[chosen]), text);
		}
		}
		return false;
	}

	// A VALUE CHOSEN IN ITS WINDOW — said ticked, and shown as its own text.
	bool Choose(ibAppearanceParameter parameter, const ibValue& value, wxString& text) {
		m_edited.Say(parameter, true, value);
		text = KeptText(m_edited, parameter);
		m_view->Refresh();
		return true;
	}

	ibAppearanceDescription m_edited;
	const ibMetaData*       m_metaData = nullptr;
	bool                    m_readOnly = false;
	ibDataViewCtrl*         m_view = nullptr;
	ibAppearanceModel*      m_model = nullptr;
};

} // namespace

bool ibEditAppearance(wxWindow* parent, ibAppearanceDescription& appearance, const ibMetaData* metaData, bool readOnly)
{
	ibDialogAppearance dialog(parent, appearance, metaData, readOnly);
	if (dialog.ShowModal() != wxID_OK || readOnly || dialog.GetAppearance() == appearance)
		return false;
	appearance = dialog.GetAppearance();
	return true;
}

wxString ibAppearanceSummary(const ibAppearanceDescription& appearance)
{
	wxString summary;
	for (const ibAppearanceParameter parameter : ibAppearanceParameters()) {
		if (!IsTicked(appearance, parameter))
			continue;
		if (!summary.IsEmpty())
			summary += wxT(", ");   // not `;` — a format is written with those
		summary += ibAppearanceParameterCaption(parameter) + wxT(": ")
			+ ibTranslateString(KeptText(appearance, parameter)).GetString();
	}
	return summary;
}
