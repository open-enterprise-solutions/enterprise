////////////////////////////////////////////////////////////////////////////
//	Description : The format string constructor (formatConstructor.h)
////////////////////////////////////////////////////////////////////////////

#include "formatConstructor.h"

#include "backend/compiler/value.h"

#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>

// A row's label: what the code does, and how it is written.
static wxString RowLabel(const wxString& label, ibFormatCode code)
{
	return wxString::Format(wxT("%s (%s)"), label, ibFormatString::CodeName(code));
}

// What a person calls a date preset. The patterns are the backend's; the words are the window's.
static wxString PresetTitle(ibDatePreset preset)
{
	switch (preset) {
	case ibDatePreset::Date:         return _("Date");
	case ibDatePreset::DateTime:     return _("Date and time");
	case ibDatePreset::Time:         return _("Time");
	case ibDatePreset::HoursMinutes: return _("Hours and minutes");
	case ibDatePreset::MonthYear:    return _("Month and year");
	case ibDatePreset::Year:         return _("Year");
	case ibDatePreset::Sortable:     return _("Sortable date");
	case ibDatePreset::Custom:       break;
	}
	return _("Other");
}

ibDialogFormatConstructor::ibDialogFormatConstructor(wxWindow* parent, const wxString& title,
	const ibFormatString& format, bool readOnly)
	: wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	m_original(format), m_readOnly(readOnly)
{
	m_filling = true;
	const int gap = FromDIP(6);

	wxBoxSizer* top = new wxBoxSizer(wxVERTICAL);

	m_notebook = new wxNotebook(this, wxID_ANY);
	BuildNumberPage(m_notebook);
	BuildDatePage(m_notebook);
	BuildBooleanPage(m_notebook);
	top->Add(m_notebook, wxSizerFlags(1).Expand().Border(wxALL, gap * 2));

	// THE STRING AS IT WILL BE WRITTEN, and what it does to a value of the tab's kind.
	wxFlexGridSizer* result = new wxFlexGridSizer(2, gap, gap);
	result->AddGrowableCol(1, 1);
	result->Add(new wxStaticText(this, wxID_ANY, _("Format string:")), wxSizerFlags().CenterVertical());
	m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	result->Add(m_text, wxSizerFlags().Expand());
	result->Add(new wxStaticText(this, wxID_ANY, _("Sample:")));
	m_sample = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
	result->Add(m_sample, wxSizerFlags().Expand());
	top->Add(result, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, gap * 2));

	// A CODE NO TAB HAS A ROW FOR is kept — said, so that seeing it come back is not a surprise.
	if (!m_original.m_other.empty()) {
		wxString kept;
		for (const auto& pair : m_original.m_other)
			kept += (kept.IsEmpty() ? wxString() : wxString(wxT("; "))) + pair.first + wxT("=") + pair.second;
		top->Add(new wxStaticText(this, wxID_ANY,
			wxString::Format(_("Kept as written - codes the platform does not read: %s"), kept)),
			wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, gap * 2));
	}

	top->Add(CreateStdDialogButtonSizer(readOnly ? wxCANCEL : (wxOK | wxCANCEL)),
		wxSizerFlags().Right().Border(wxALL, gap * 2));

	SetSizerAndFit(top);
	CentreOnParent();

	Bind(wxEVT_BUTTON, &ibDialogFormatConstructor::OnOk, this, wxID_OK);
	m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
		event.Skip();
		ShowResult();
	});

	// It opens on the first tab the string says something on.
	const ibFormatString empty;
	if (m_original.m_number == empty.m_number) {
		if (!(m_original.m_date == empty.m_date))
			m_notebook->SetSelection(1);
		else if (!(m_original.m_boolean == empty.m_boolean))
			m_notebook->SetSelection(2);
	}

	m_filling = false;
	ShowResult();
}

// ---------------------------------------------------------------------------
//  The rows
// ---------------------------------------------------------------------------

ibDialogFormatConstructor::ibNumberRow ibDialogFormatConstructor::AddNumberRow(wxWindow* page, wxSizer* grid,
	const wxString& label, ibFormatCode code, const std::optional<int>& value, int defaultValue, int min, int max)
{
	ibNumberRow row;
	row.m_on = new wxCheckBox(page, wxID_ANY, RowLabel(label, code));
	row.m_on->SetValue(value.has_value());
	row.m_on->Enable(!m_readOnly);
	row.m_value = new wxSpinCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
		wxSP_ARROW_KEYS, min, max, value ? *value : defaultValue);
	row.m_value->Enable(value.has_value() && !m_readOnly);
	grid->Add(row.m_on, wxSizerFlags().CenterVertical());
	grid->Add(row.m_value);

	wxSpinCtrl* editor = row.m_value;
	row.m_on->Bind(wxEVT_CHECKBOX, [this, editor](wxCommandEvent& event) {
		editor->Enable(event.IsChecked());
		ShowResult();
	});
	editor->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ShowResult(); });
	editor->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ShowResult(); });
	return row;
}

ibDialogFormatConstructor::ibCharRow ibDialogFormatConstructor::AddCharRow(wxWindow* page, wxSizer* grid,
	const wxString& label, ibFormatCode code, const std::optional<wxUniChar>& value,
	const std::vector<std::pair<wxUniChar, wxString>>& chars)
{
	ibCharRow row;
	row.m_on = new wxCheckBox(page, wxID_ANY, RowLabel(label, code));
	row.m_on->SetValue(value.has_value());
	row.m_on->Enable(!m_readOnly);
	row.m_value = new wxChoice(page, wxID_ANY);
	for (const auto& item : chars) {
		row.m_value->Append(item.second);
		row.m_chars.push_back(item.first);
	}
	// A character written by hand that is none of the usual ones is offered as itself, not replaced.
	int selection = 0;
	if (value) {
		selection = wxNOT_FOUND;
		for (size_t i = 0; i < row.m_chars.size(); ++i)
			if (row.m_chars[i] == *value) selection = static_cast<int>(i);
		if (selection == wxNOT_FOUND) {
			selection = static_cast<int>(row.m_value->Append(wxString::Format(wxT("'%s'"), wxString(*value))));
			row.m_chars.push_back(*value);
		}
	}
	row.m_value->SetSelection(selection);
	row.m_value->Enable(value.has_value() && !m_readOnly);
	grid->Add(row.m_on, wxSizerFlags().CenterVertical());
	grid->Add(row.m_value);

	wxChoice* editor = row.m_value;
	row.m_on->Bind(wxEVT_CHECKBOX, [this, editor](wxCommandEvent& event) {
		editor->Enable(event.IsChecked());
		ShowResult();
	});
	editor->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ShowResult(); });
	return row;
}

ibDialogFormatConstructor::ibTextRow ibDialogFormatConstructor::AddTextRow(wxWindow* page, wxSizer* grid,
	const wxString& label, ibFormatCode code, const std::optional<wxString>& value)
{
	ibTextRow row;
	row.m_on = new wxCheckBox(page, wxID_ANY, RowLabel(label, code));
	row.m_on->SetValue(value.has_value());
	row.m_on->Enable(!m_readOnly);
	row.m_value = new wxTextCtrl(page, wxID_ANY, value ? *value : wxString());
	row.m_value->Enable(value.has_value() && !m_readOnly);
	grid->Add(row.m_on, wxSizerFlags().CenterVertical());
	grid->Add(row.m_value, wxSizerFlags().Expand());

	wxTextCtrl* editor = row.m_value;
	row.m_on->Bind(wxEVT_CHECKBOX, [this, editor](wxCommandEvent& event) {
		editor->Enable(event.IsChecked());
		ShowResult();
	});
	editor->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ShowResult(); });
	return row;
}

// ---------------------------------------------------------------------------
//  The tabs
// ---------------------------------------------------------------------------

void ibDialogFormatConstructor::BuildNumberPage(wxNotebook* notebook)
{
	wxPanel* page = new wxPanel(notebook);
	const int gap = FromDIP(6);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, gap, gap * 2);
	grid->AddGrowableCol(1, 1);

	const ibNumberFormat& number = m_original.m_number;
	m_fractionDigits = AddNumberRow(page, grid, _("Digits after the point, always that many"),
		ibFormatCode::NumberFractionDigits, number.m_fractionDigits, 2, 0, 30);
	m_digits = AddNumberRow(page, grid, _("Significant digits in all, trailing zeros dropped"),
		ibFormatCode::NumberDigits, number.m_digits, 15, 1, 40);
	m_decimalSeparator = AddCharRow(page, grid, _("Decimal separator"),
		ibFormatCode::NumberDecimalSeparator, number.m_decimalSeparator,
		{ { wxUniChar('.'), _("Point") }, { wxUniChar(','), _("Comma") } });
	m_groupSeparator = AddCharRow(page, grid, _("Group separator"),
		ibFormatCode::NumberGroupSeparator, number.m_groupSeparator,
		{ { wxUniChar(' '), _("Space") }, { wxUniChar(0x00A0), _("Non-breaking space") },
		  { wxUniChar(','), _("Comma") }, { wxUniChar('.'), _("Point") }, { wxUniChar('\''), _("Apostrophe") } });
	m_groupSize = AddNumberRow(page, grid, _("Digits in a group"),
		ibFormatCode::NumberGroupSize, number.m_groupSize, 3, 1, 9);
	m_zero = AddTextRow(page, grid, _("A zero prints as"), ibFormatCode::NumberZero, number.m_zero);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(grid, wxSizerFlags(1).Expand().Border(wxALL, gap * 2));
	page->SetSizer(sizer);
	notebook->AddPage(page, _("Number"));
}

void ibDialogFormatConstructor::BuildDatePage(wxNotebook* notebook)
{
	wxPanel* page = new wxPanel(notebook);
	const int gap = FromDIP(6);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, gap, gap * 2);
	grid->AddGrowableCol(1, 1);

	const ibDateFormat& date = m_original.m_date;

	// THE PATTERN: a preset, or any pattern typed — the two stay in step both ways.
	m_patternOn = new wxCheckBox(page, wxID_ANY, RowLabel(_("Pattern"), ibFormatCode::DatePattern));
	m_patternOn->SetValue(date.m_pattern.has_value());
	m_patternOn->Enable(!m_readOnly);

	const wxString pattern = date.m_pattern ? *date.m_pattern : ibFormatString::PresetPattern(ibDatePreset::Date);
	m_presets = { ibDatePreset::Date, ibDatePreset::DateTime, ibDatePreset::Time, ibDatePreset::HoursMinutes,
		ibDatePreset::MonthYear, ibDatePreset::Year, ibDatePreset::Sortable, ibDatePreset::Custom };
	m_preset = new wxChoice(page, wxID_ANY);
	for (const ibDatePreset preset : m_presets) {
		const wxString presetPattern = ibFormatString::PresetPattern(preset);
		m_preset->Append(presetPattern.IsEmpty() ? PresetTitle(preset)
			: wxString::Format(wxT("%s - %s"), PresetTitle(preset), presetPattern));
	}
	const auto presetIndex = [this](ibDatePreset preset) {
		for (size_t i = 0; i < m_presets.size(); ++i)
			if (m_presets[i] == preset) return static_cast<int>(i);
		return static_cast<int>(m_presets.size()) - 1;
	};
	m_preset->SetSelection(presetIndex(ibFormatString::PresetOf(pattern)));
	m_pattern = new wxTextCtrl(page, wxID_ANY, pattern);
	m_preset->Enable(date.m_pattern.has_value() && !m_readOnly);
	m_pattern->Enable(date.m_pattern.has_value() && !m_readOnly);

	wxBoxSizer* patternBox = new wxBoxSizer(wxVERTICAL);
	patternBox->Add(m_preset, wxSizerFlags().Expand());
	patternBox->Add(m_pattern, wxSizerFlags().Expand().Border(wxTOP, gap));
	grid->Add(m_patternOn, wxSizerFlags().Border(wxTOP, FromDIP(3)));
	grid->Add(patternBox, wxSizerFlags().Expand());

	m_patternOn->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
		m_preset->Enable(event.IsChecked());
		m_pattern->Enable(event.IsChecked());
		ShowResult();
	});
	m_preset->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
		const int selection = m_preset->GetSelection();
		if (selection != wxNOT_FOUND && m_presets[selection] != ibDatePreset::Custom)
			m_pattern->ChangeValue(ibFormatString::PresetPattern(m_presets[selection]));
		ShowResult();
	});
	m_pattern->Bind(wxEVT_TEXT, [this, presetIndex](wxCommandEvent&) {
		m_preset->SetSelection(presetIndex(ibFormatString::PresetOf(m_pattern->GetValue())));
		ShowResult();
	});

	m_emptyDate = AddTextRow(page, grid, _("An empty date prints as"), ibFormatCode::DateEmpty, date.m_empty);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(grid, wxSizerFlags().Expand().Border(wxALL, gap * 2));
	// ⚠ The one thing about this grammar nobody guesses: the month is lower case and the minutes upper.
	wxStaticText* legend = new wxStaticText(page, wxID_ANY,
		_("In a pattern: yyyy or yy - the year, mm or m - the month, dd or d - the day, HH or H - the hours, "
		  "MM or M - the minutes, SS or S - the seconds. The month is lower case, the minutes upper."));
	legend->Wrap(FromDIP(460));
	sizer->Add(legend, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxBOTTOM, gap * 2));
	page->SetSizer(sizer);
	notebook->AddPage(page, _("Date"));
}

void ibDialogFormatConstructor::BuildBooleanPage(wxNotebook* notebook)
{
	wxPanel* page = new wxPanel(notebook);
	const int gap = FromDIP(6);
	wxFlexGridSizer* grid = new wxFlexGridSizer(2, gap, gap * 2);
	grid->AddGrowableCol(1, 1);

	const ibBooleanFormat& boolean = m_original.m_boolean;
	m_true = AddTextRow(page, grid, _("True prints as"), ibFormatCode::BooleanTrue, boolean.m_true);
	m_false = AddTextRow(page, grid, _("False prints as"), ibFormatCode::BooleanFalse, boolean.m_false);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(grid, wxSizerFlags(1).Expand().Border(wxALL, gap * 2));
	page->SetSizer(sizer);
	notebook->AddPage(page, _("Boolean"));
}

// ---------------------------------------------------------------------------
//  What the tabs say
// ---------------------------------------------------------------------------

ibFormatString ibDialogFormatConstructor::Collect() const
{
	// FROM THE ORIGINAL, so the codes no tab has a row for come back as they came.
	ibFormatString format = m_original;

	const auto number = [](const ibNumberRow& row) {
		return row.m_on->IsChecked() ? std::optional<int>(row.m_value->GetValue()) : std::nullopt;
	};
	const auto character = [](const ibCharRow& row) {
		const int selection = row.m_value->GetSelection();
		return row.m_on->IsChecked() && selection != wxNOT_FOUND
			? std::optional<wxUniChar>(row.m_chars[selection]) : std::nullopt;
	};
	const auto text = [](const ibTextRow& row) {
		return row.m_on->IsChecked() ? std::optional<wxString>(row.m_value->GetValue()) : std::nullopt;
	};

	format.m_number.m_digits           = number(m_digits);
	format.m_number.m_fractionDigits   = number(m_fractionDigits);
	format.m_number.m_decimalSeparator = character(m_decimalSeparator);
	format.m_number.m_groupSeparator   = character(m_groupSeparator);
	format.m_number.m_groupSize        = number(m_groupSize);
	format.m_number.m_zero             = text(m_zero);
	format.m_date.m_pattern = m_patternOn->IsChecked() ? std::optional<wxString>(m_pattern->GetValue()) : std::nullopt;
	format.m_date.m_empty              = text(m_emptyDate);
	format.m_boolean.m_true            = text(m_true);
	format.m_boolean.m_false           = text(m_false);
	return format;
}

ibFormatString ibDialogFormatConstructor::GetFormat() const
{
	// ⭐ WHAT LEAVES THE WINDOW IS WHAT THE ENGINE READ: written, and read back by the one reader
	// there is. A text's spaces are the reader's business (trimmed; all space is a space), and the
	// window does not keep an opinion of its own about them.
	return ibFormatString::Parse(Collect().Render());
}

void ibDialogFormatConstructor::ShowResult()
{
	if (m_filling || m_text == nullptr || m_sample == nullptr)
		return;

	const ibFormatString format = GetFormat();
	m_text->ChangeValue(format.Render());

	const wxString arrow(wxUniChar(0x2192));
	const auto sample = [&](const wxString& shown, const ibValue& value) {
		return shown + wxT(" ") + arrow + wxT(" ") + format.Apply(value);
	};

	wxString line;
	switch (m_notebook->GetSelection()) {
	case 0: {
		ibValue big, negative;
		big.SetNumber(wxT("1234567.891"));
		negative.SetNumber(wxT("-42.5"));
		line = sample(wxT("1234567.891"), big) + wxT("     ")
			+ sample(wxT("-42.5"), negative) + wxT("     ")
			+ sample(wxT("0"), ibValue(0));
		break;
	}
	case 1: {
		const wxDateTime now = wxDateTime::Now();
		line = sample(now.Format(wxT("%d.%m.%Y %H:%M:%S")), ibValue(now)) + wxT("     ")
			+ sample(_("empty date"), ibValue(ibValueTypes::TYPE_DATE));
		break;
	}
	default:
		line = sample(_("True"), ibValue(true)) + wxT("     ") + sample(_("False"), ibValue(false));
		break;
	}
	m_sample->SetLabelText(line);
}

void ibDialogFormatConstructor::OnOk(wxCommandEvent& event)
{
	// `;` ENDS A PAIR AND `=` IS NOT KEPT IN ONE, so a text holding either would come back as something
	// else — said here, on the field, rather than found later in what a report prints.
	struct ibCheck { wxCheckBox* m_on; wxTextCtrl* m_value; int m_page; };
	const ibCheck checks[] = {
		{ m_zero.m_on, m_zero.m_value, 0 },
		{ m_patternOn, m_pattern, 1 },
		{ m_emptyDate.m_on, m_emptyDate.m_value, 1 },
		{ m_true.m_on, m_true.m_value, 2 },
		{ m_false.m_on, m_false.m_value, 2 },
	};
	for (const ibCheck& check : checks) {
		if (!check.m_on->IsChecked() || ibFormatString::IsWritable(check.m_value->GetValue()))
			continue;
		m_notebook->SetSelection(check.m_page);
		check.m_value->SetFocus();
		wxMessageBox(wxString::Format(_("'%s' cannot hold ';' or '=': a format string has no way to write them."),
			check.m_on->GetLabel()), GetTitle(), wxOK | wxICON_WARNING, this);
		return;
	}
	event.Skip();   // the dialog's own OK: EndModal(wxID_OK)
}
