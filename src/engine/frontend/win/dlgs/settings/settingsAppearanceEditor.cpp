#include "frontend/win/dlgs/settings/settingsAppearanceEditor.h"

#include "frontend/win/dlgs/settings/settingsStyle.h"   // ibStyleSettingsGrid — how a settings list looks
#include "frontend/win/dlgs/textWithDotsCell.h"         // the value cell: typed, or "..." for its own window
#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"   // a text parameter, every language of it
#include "frontend/win/dlgs/formatConstructor/formatConstructor.h"         // ibFormatBoxEditor — a format, a language at a time
#include "backend/backend_localization.h"               // ibTranslateString — a text read in the language in force

#include <wx/dialog.h>
#include <wx/settings.h>   // wxSystemSettings — an unticked value reads grey
#include <wx/sizer.h>

namespace {

// THE TEXT A PARAMETER KEEPS, ticked or not — what its value cell edits. (Never a reference: an appearance
// reads back without a configuration — see ibAppearanceValueDescription.)
wxString KeptText(const ibAppearanceDescription& appearance, ibAppearanceParameter parameter)
{
	const ibAppearanceValueDescription* value = appearance.Find(parameter);
	return value != nullptr ? ibStoredValue(value->m_value, nullptr).GetString() : wxString();
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
			m_appearance->Say(parameter, variant.GetBool(), kept.IsEmpty() ? ibValue() : ibValue(kept));
			return true;
		}
		if (col != kColValue)
			return false;

		// TYPED IN THE CELL — the language in force; the others stay as they were (the "..." edits them all).
		// Emptied, that language is taken out.
		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
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
		}
		return false;
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
