#ifndef __FORMAT_CONSTRUCTOR_DLG_H__
#define __FORMAT_CONSTRUCTOR_DLG_H__

////////////////////////////////////////////////////////////////////////////
// The FORMAT STRING CONSTRUCTOR — three tabs over an ibFormatString.
////////////////////////////////////////////////////////////////////////////
//
// A tab per kind of value the string formats — numbers, dates, booleans — and a row per code:
// a box that says whether the code is written at all, and the control for its value. The rows are
// the fields of ibNumberFormat / ibDateFormat / ibBooleanFormat (backend/formatString.h), so
// the window knows no spelling of its own: the text under the tabs is ibFormatString::Render, and
// the sample beside it is ibFormatString::Apply — the very function Format(value, format) runs, so
// what the sample shows is what a module prints.
//
// Reached from the code editor's menu, on the literal under the caret, beside the query and the
// translation constructors.
//
////////////////////////////////////////////////////////////////////////////

#include "frontend/frontend.h"
#include "backend/formatString.h"

#include <wx/dialog.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <vector>

class FRONTEND_API ibDialogFormatConstructor : public wxDialog
{
public:

	ibDialogFormatConstructor(wxWindow* parent, const wxString& title, const ibFormatString& format, bool readOnly);

	// What the tabs say. The codes no tab has a row for come back as they came.
	ibFormatString GetFormat() const;

private:

	// A code with a whole number: written or not, and the number.
	struct ibNumberRow {
		wxCheckBox* m_on = nullptr;
		wxSpinCtrl* m_value = nullptr;
	};
	// A code with one character, picked from the ones it is usually written with.
	struct ibCharRow {
		wxCheckBox*            m_on = nullptr;
		wxChoice*              m_value = nullptr;
		std::vector<wxUniChar> m_chars;   // the character behind each item of m_value
	};
	// A code with a text.
	struct ibTextRow {
		wxCheckBox* m_on = nullptr;
		wxTextCtrl* m_value = nullptr;
	};

	void BuildNumberPage(wxNotebook* notebook);
	void BuildDatePage(wxNotebook* notebook);
	void BuildBooleanPage(wxNotebook* notebook);

	ibNumberRow AddNumberRow(wxWindow* page, wxSizer* grid, const wxString& label, ibFormatCode code,
		const std::optional<int>& value, int defaultValue, int min, int max);
	ibCharRow AddCharRow(wxWindow* page, wxSizer* grid, const wxString& label, ibFormatCode code,
		const std::optional<wxUniChar>& value, const std::vector<std::pair<wxUniChar, wxString>>& chars);
	ibTextRow AddTextRow(wxWindow* page, wxSizer* grid, const wxString& label, ibFormatCode code,
		const std::optional<wxString>& value);

	// What the controls say, before the engine has read it back (GetFormat does that).
	ibFormatString Collect() const;

	// The text and the sample, again — after every change of every control. (Not `Refresh`: that name
	// is wxWindow's, and virtual.)
	void ShowResult();
	void OnOk(wxCommandEvent& event);

	ibFormatString m_original;
	bool           m_readOnly = false;

	ibNumberRow m_digits, m_fractionDigits, m_groupSize;
	ibCharRow   m_decimalSeparator, m_groupSeparator;
	ibTextRow   m_zero;

	wxCheckBox* m_patternOn = nullptr;
	wxChoice*   m_preset = nullptr;          // item i is the preset at m_presets[i]
	wxTextCtrl* m_pattern = nullptr;
	std::vector<ibDatePreset> m_presets;
	ibTextRow   m_emptyDate;

	ibTextRow   m_true, m_false;

	wxNotebook*   m_notebook = nullptr;
	wxTextCtrl*   m_text = nullptr;          // the format string, as it will be written
	wxStaticText* m_sample = nullptr;        // what the current tab's kind of value prints as

	bool m_filling = false;                  // the controls are being set, not edited
};

#endif
