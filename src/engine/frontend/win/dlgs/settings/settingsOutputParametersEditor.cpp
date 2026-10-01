#include "frontend/win/dlgs/settings/settingsOutputParametersEditor.h"

#include "frontend/win/dlgs/settings/settingsStyle.h"   // ibStyleSettingsGrid — how a settings list looks
#include "frontend/win/dlgs/textWithDotsCell.h"         // the value cell: typed, or "..." for its own list or window
#include "frontend/win/dlgs/translateConstructor/translateConstructor.h"   // a title, every language of it
#include "backend/composition/compositionTheme.h"       // ibCompositionThemes — the palettes a theme is picked from
#include "backend/backend_localization.h"               // ibTranslateString — a title read in the language in force

#include <wx/choicdlg.h>   // wxGetSingleChoiceIndex — the window a theme or a word is picked in
#include <wx/settings.h>   // wxSystemSettings — an unticked value reads grey
#include <wx/sizer.h>

#include <utility>   // std::move — libstdc++ does not drag it in the way MSVC does
#include <vector>

namespace {

// THE VALUE A PARAMETER KEEPS, ticked or not. (Never a reference: these read back without a configuration.)
ibValue Kept(const ibOutputParametersDescription& parameters, ibOutputParameter parameter)
{
	const ibParameterValueDescription<ibOutputParameter>* value = parameters.Find(parameter);
	return value != nullptr ? ibStoredValue(value->m_value, nullptr) : ibValue();
}

bool IsTicked(const ibOutputParametersDescription& parameters, ibOutputParameter parameter)
{
	return parameters.Says(parameter);
}

// WHAT A LINE'S VALUE READS AS — a theme by its name, a mode by its word, a title in the language in force. A
// mode nobody set reads `Auto`, which is what it is; a theme nobody set reads nothing, because what stands
// there is the storey above's and this page is not the place it is decided.
wxString Presentation(const ibOutputParametersDescription& parameters, ibOutputParameter parameter)
{
	const ibValue kept = Kept(parameters, parameter);
	if (parameter == ibOutputParameter::Theme)
		return kept.IsEmpty() ? wxString() : ibCompositionThemeById(kept.GetString()).Caption();
	if (ibOutputParameterShows(parameter))
		return ibShowModeCaption(kept.IsEmpty() ? ibShowMode::Auto : static_cast<ibShowMode>(kept.GetInteger()));
	return ibTranslateString(kept.GetString()).GetString();
}

// A WORD SAID ANEW — ticked when there is something to apply, the way a value put beside a setting switches it
// on; emptied, the parameter is forgotten (the door's own rule, Say).
void SayValue(ibOutputParametersDescription& parameters, ibOutputParameter parameter, const ibValue& value)
{
	parameters.Say(parameter, !value.IsEmpty(), value);
}

} // namespace

// ONE LINE PER PARAMETER OF THE STOREY'S LIST — the tick, what it is called, its value.
class ibOutputParametersModel : public ibDataViewVirtualListModel {
public:
	enum { kColUse = 0, kColParameter, kColValue };

	explicit ibOutputParametersModel(ibOutputParametersEditor* editor)
		: ibDataViewVirtualListModel(RowCount(editor)), m_editor(editor) {
	}

	static unsigned RowCount(const ibOutputParametersEditor* editor) {
		return editor->m_parameters != nullptr ? (unsigned)ibOutputParameters(editor->m_scope).size() : 0;
	}
	bool IsRow(unsigned row) const {
		return m_editor->m_parameters != nullptr && row < ibOutputParameters(m_editor->m_scope).size();
	}
	ibOutputParameter ParameterAt(unsigned row) const { return ibOutputParameters(m_editor->m_scope)[row]; }

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		if (!IsRow(row))
			return;
		const ibOutputParameter parameter = ParameterAt(row);
		if (col == kColUse)
			variant = IsTicked(*m_editor->m_parameters, parameter);
		else if (col == kColParameter)
			variant = ibOutputParameterCaption(parameter);
		else if (col == kColValue)
			variant = Presentation(*m_editor->m_parameters, parameter);
	}

	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (!IsRow(row) || m_editor->m_readOnly)
			return false;
		ibOutputParametersDescription& parameters = *m_editor->m_parameters;
		const ibOutputParameter parameter = ParameterAt(row);
		const ibValue kept = Kept(parameters, parameter);
		if (col == kColUse) {
			if (variant.GetBool() == IsTicked(parameters, parameter))
				return false;
			parameters.Say(parameter, variant.GetBool(), kept);
			m_editor->Changed();
			return true;
		}
		if (col != kColValue)
			return false;

		wxString text = variant.GetString();
		text.Trim(true).Trim(false);
		if (text == Presentation(parameters, parameter))
			return false;

		if (parameter == ibOutputParameter::Title) {
			// TYPED IN THE CELL — the language in force; the others stay as they were (the "..." edits them all).
			// Emptied, that language is taken out.
			ibTranslateString translated(kept.GetString());
			if (text.IsEmpty())
				translated.RemoveTranslate(ibBackendLocalization::GetUserLanguage());
			else
				translated.SetTranslate(text);
			const wxString raw = translated.GetRawText();
			SayValue(parameters, parameter, raw.IsEmpty() ? ibValue() : ibValue(raw));
			m_editor->Changed();
			return true;
		}

		// A WORD OF A LIST, typed by its name — the "..." offers the list itself. Emptied, the word is forgotten.
		if (text.IsEmpty()) {
			SayValue(parameters, parameter, ibValue());
			m_editor->Changed();
			return true;
		}
		if (parameter == ibOutputParameter::Theme) {
			for (const ibCompositionTheme* theme : ibCompositionThemes())
				if (text.IsSameAs(theme->Caption(), false)) {
					SayValue(parameters, parameter, ibValue(wxString(theme->m_id)));
					m_editor->Changed();
					return true;
				}
			return false;
		}
		if (ibOutputParameterShows(parameter)) {
			for (const ibShowMode mode : { ibShowMode::Auto, ibShowMode::Show, ibShowMode::Hide })
				if (text.IsSameAs(ibShowModeCaption(mode), false)) {
					SayValue(parameters, parameter, ibValue(static_cast<int>(mode)));
					m_editor->Changed();
					return true;
				}
		}
		return false;
	}

	// AN UNTICKED VALUE READS GREY — kept, not applied.
	bool GetAttrByRow(unsigned row, unsigned col, ibDataViewItemAttr& attr) const override {
		if (col == kColValue && IsRow(row) && !IsTicked(*m_editor->m_parameters, ParameterAt(row))) {
			attr.SetColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
			return true;
		}
		return false;
	}

	bool IsEnabledByRow(unsigned int, unsigned int col) const override {
		return !m_editor->m_readOnly && col != kColParameter;
	}

private:
	ibOutputParametersEditor* m_editor;
};

ibOutputParametersEditor::ibOutputParametersEditor(wxWindow* parent, ibOutputParametersDescription* parameters,
	ibOutputParameterScope scope, MetaOf metaOf)
	: wxPanel(parent, wxID_ANY), m_parameters(parameters), m_scope(scope), m_metaOf(std::move(metaOf))
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_view = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_view);
	m_model = new ibOutputParametersModel(this);
	m_view->AssociateModel(m_model);

	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(wxEmptyString,
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		ibOutputParametersModel::kColUse, FromDIP(24), wxAlignment::wxALIGN_CENTER));
	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Parameter"),
		new ibDataViewTextRenderer(), ibOutputParametersModel::kColParameter, FromDIP(240), wxAlignment::wxALIGN_LEFT));
	// Typed in the cell; the "..." opens the parameter's own list or window.
	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(_("Value"),
		new ibTextWithDotsRenderer(this, [this](wxString& text) -> bool { return EditValue(text); }),
		ibOutputParametersModel::kColValue, FromDIP(320), wxAlignment::wxALIGN_LEFT));

	// A DOUBLE-CLICK (or Enter) EDITS THE CELL under the cursor — the same gesture the settings grids use.
	m_view->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		m_view->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});

	sizer->Add(m_view, 1, wxEXPAND | wxALL, FromDIP(4));
	SetSizer(sizer);
}

void ibOutputParametersEditor::SetParameters(ibOutputParametersDescription* parameters, ibOutputParameterScope scope)
{
	m_parameters = parameters;
	m_scope      = scope;
	if (m_model != nullptr)
		m_model->Reset(ibOutputParametersModel::RowCount(this));
}

void ibOutputParametersEditor::SetReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	if (m_view != nullptr)
		m_view->Refresh();
}

void ibOutputParametersEditor::Changed()
{
	if (m_view != nullptr)
		m_view->Refresh();   // the tick beside a value may have changed with it
	if (m_changed)
		m_changed();
}

bool ibOutputParametersEditor::EditValue(wxString& text)
{
	if (m_readOnly || m_parameters == nullptr)
		return false;
	const ibDataViewItem item = m_view->GetSelection();
	if (!item.IsOk())
		return false;
	const unsigned row = m_model->GetRow(item);
	if (!m_model->IsRow(row))
		return false;
	const ibOutputParameter parameter = m_model->ParameterAt(row);

	if (parameter == ibOutputParameter::Title) {
		const ibTranslateString before(Kept(*m_parameters, parameter).GetString());
		ibDialogTranslateConstructor dialog(this, ibOutputParameterCaption(parameter), before,
			m_metaOf ? m_metaOf() : nullptr, m_readOnly);
		if (dialog.ShowModal() != wxID_OK)
			return false;
		const ibTranslateString after = dialog.GetTranslate();
		if (after == before)
			return false;
		const wxString raw = after.GetRawText();
		SayValue(*m_parameters, parameter, raw.IsEmpty() ? ibValue() : ibValue(raw));
		text = after.GetString();
		Changed();
		return true;
	}

	// A LIST TO PICK FROM — the platform's palettes, or the three words for whether something is shown — in a
	// small window of its own, the way the query constructor asks for a field (Max, 2026-09-30: "you press the
	// three dots and a window with the themes opens").
	wxArrayString labels;
	std::vector<ibValue> offered;
	if (parameter == ibOutputParameter::Theme) {
		for (const ibCompositionTheme* theme : ibCompositionThemes()) {
			labels.Add(theme->Caption());
			offered.push_back(ibValue(wxString(theme->m_id)));
		}
	}
	else if (ibOutputParameterShows(parameter)) {
		for (const ibShowMode mode : { ibShowMode::Auto, ibShowMode::Show, ibShowMode::Hide }) {
			labels.Add(ibShowModeCaption(mode));
			offered.push_back(ibValue(static_cast<int>(mode)));
		}
	}
	if (offered.empty())
		return false;
	const int current = labels.Index(Presentation(*m_parameters, parameter));
	const int chosen = wxGetSingleChoiceIndex(ibOutputParameterCaption(parameter), _("Other settings"), labels,
		current != wxNOT_FOUND ? current : 0, this);
	if (chosen < 0 || chosen >= (int)offered.size())
		return false;
	SayValue(*m_parameters, parameter, offered[(size_t)chosen]);
	text = Presentation(*m_parameters, parameter);
	Changed();
	return true;
}
