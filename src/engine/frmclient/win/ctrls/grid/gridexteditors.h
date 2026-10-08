/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/grideditors.h
// Purpose:     ibGridCellEditorEvtHandler and ibGrid editors
// Author:      Michael Bedward (based on code by Julian Smart, Robin Dunn)
// Modified by: Santiago Palacios
// Created:     1/08/1999
// Copyright:   (c) Michael Bedward
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_GENERIC_GRID_EDITORS_H_
#define _WX_GENERIC_GRID_EDITORS_H_

#include <wx/defs.h>

#if wxUSE_GRID

#include <wx/scopedptr.h>

class ibGridCellEditorEvtHandler : public wxEvtHandler
{
public:
	ibGridCellEditorEvtHandler(ibGrid* grid, ibGridCellEditor* editor)
		: m_grid(grid),
		m_editor(editor),
		m_inSetFocus(false)
	{
	}

	void DismissEditor();

	void OnKillFocus(wxFocusEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnChar(wxKeyEvent& event);

	void SetInSetFocus(bool inSetFocus) { m_inSetFocus = inSetFocus; }

private:
	ibGrid* m_grid;
	ibGridCellEditor* m_editor;

	// Work around the fact that a focus kill event can be sent to
	// a combobox within a set focus event.
	bool                m_inSetFocus;

	wxDECLARE_EVENT_TABLE();
	wxDECLARE_DYNAMIC_CLASS(ibGridCellEditorEvtHandler);
	wxDECLARE_NO_COPY_CLASS(ibGridCellEditorEvtHandler);
};


#if wxUSE_TEXTCTRL

// the editor for string/text data
class ibGridCellTextEditor : public ibGridCellEditor
{
public:
	explicit ibGridCellTextEditor(size_t maxChars = 0)
		: ibGridCellEditor(),
		m_maxChars(maxChars)
	{
	}

	ibGridCellTextEditor(const ibGridCellTextEditor& other);

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;
	virtual void SetSize(const wxRect& rect) wxOVERRIDE;

	virtual bool IsAcceptedKey(wxKeyEvent& event) wxOVERRIDE;
	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;
	virtual void StartingKey(wxKeyEvent& event) wxOVERRIDE;
	virtual void HandleReturn(wxKeyEvent& event) wxOVERRIDE;

	// parameters string format is "max_width"
	virtual void SetParameters(const wxString& params) wxOVERRIDE;
#if wxUSE_VALIDATORS
	virtual void SetValidator(const wxValidator& validator);
#endif

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellTextEditor(*this);
	}

	// added GetValue so we can get the value which is in the control
	virtual wxString GetValue() const wxOVERRIDE;

protected:
	wxTextCtrl* Text() const { return (wxTextCtrl*)m_control; }

	// parts of our virtual functions reused by the derived classes
	void DoCreate(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler,
		long style = 0);
	void DoBeginEdit(const wxString& startValue);
	void DoReset(const wxString& startValue);

private:
	size_t                   m_maxChars;        // max number of chars allowed
#if wxUSE_VALIDATORS
	wxScopedPtr<wxValidator> m_validator;
#endif
	wxString                 m_value;
};

// the editor for numeric (long) data
class ibGridCellNumberEditor : public ibGridCellTextEditor
{
public:
	// allows to specify the range - if min == max == -1, no range checking is
	// done
	explicit ibGridCellNumberEditor(int min = -1, int max = -1)
		: ibGridCellTextEditor(),
		m_min(min),
		m_max(max),
		m_value(0L)
	{
	}

	ibGridCellNumberEditor(const ibGridCellNumberEditor& other)
		: ibGridCellTextEditor(other),
		m_min(other.m_min),
		m_max(other.m_max),
		m_value(other.m_value)
	{
	}

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual void SetSize(const wxRect& rect) wxOVERRIDE;

	virtual bool IsAcceptedKey(wxKeyEvent& event) wxOVERRIDE;
	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;
	virtual void StartingKey(wxKeyEvent& event) wxOVERRIDE;

	// parameters string format is "min,max"
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellNumberEditor(*this);
	}

	// added GetValue so we can get the value which is in the control
	virtual wxString GetValue() const wxOVERRIDE;

protected:
#if wxUSE_SPINCTRL
	wxSpinCtrl* Spin() const { return (wxSpinCtrl*)m_control; }
#endif

	// if HasRange(), we use wxSpinCtrl - otherwise wxTextCtrl
	bool HasRange() const
	{
#if wxUSE_SPINCTRL
		return m_min != m_max;
#else
		return false;
#endif
	}

	// string representation of our value
	wxString GetString() const
	{
		return wxString::Format(wxT("%ld"), m_value);
	}

private:
	int m_min,
		m_max;

	long m_value;
};


enum ibGridCellFloatFormat
{
	// Decimal floating point (%f)
	wxGRID_FLOAT_FORMAT_FIXED = 0x0010,

	// Scientific notation (mantise/exponent) using e character (%e)
	wxGRID_FLOAT_FORMAT_SCIENTIFIC = 0x0020,

	// Use the shorter of %e or %f (%g)
	wxGRID_FLOAT_FORMAT_COMPACT = 0x0040,

	// To use in combination with one of the above formats (%F/%E/%G)
	wxGRID_FLOAT_FORMAT_UPPER = 0x0080,

	// Format used by default.
	wxGRID_FLOAT_FORMAT_DEFAULT = wxGRID_FLOAT_FORMAT_FIXED,

	// A mask to extract format from the combination of flags.
	wxGRID_FLOAT_FORMAT_MASK = wxGRID_FLOAT_FORMAT_FIXED |
	wxGRID_FLOAT_FORMAT_SCIENTIFIC |
	wxGRID_FLOAT_FORMAT_COMPACT |
	wxGRID_FLOAT_FORMAT_UPPER
};

// the editor for floating point numbers (double) data
class ibGridCellFloatEditor : public ibGridCellTextEditor
{
public:
	explicit ibGridCellFloatEditor(int width = -1,
		int precision = -1,
		int format = wxGRID_FLOAT_FORMAT_DEFAULT)
		: ibGridCellTextEditor(),
		m_width(width),
		m_precision(precision),
		m_value(0.0),
		m_style(format)
	{
	}

	ibGridCellFloatEditor(const ibGridCellFloatEditor& other)
		: ibGridCellTextEditor(other),
		m_width(other.m_width),
		m_precision(other.m_precision),
		m_value(other.m_value),
		m_style(other.m_style),
		m_format(other.m_format)
	{
	}

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual bool IsAcceptedKey(wxKeyEvent& event) wxOVERRIDE;
	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;
	virtual void StartingKey(wxKeyEvent& event) wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellFloatEditor(*this);
	}

	// parameters string format is "width[,precision[,format]]"
	// format to choose between f|e|g|E|G (f is used by default)
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

protected:
	// string representation of our value
	wxString GetString();

private:
	int m_width,
		m_precision;
	double m_value;

	int m_style;
	wxString m_format;
};

#endif // wxUSE_TEXTCTRL

#if wxUSE_CHECKBOX

// the editor for boolean data
class ibGridCellBoolEditor : public ibGridCellEditor
{
public:
	ibGridCellBoolEditor()
		: ibGridCellEditor()
	{
	}

	ibGridCellBoolEditor(const ibGridCellBoolEditor& other)
		: ibGridCellEditor(other),
		m_value(other.m_value)
	{
	}

	virtual ibGridActivationResult
		TryActivate(int row, int col, ibGrid* grid,
			const ibGridActivationSource& actSource) wxOVERRIDE;
	virtual void DoActivate(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual void SetSize(const wxRect& rect) wxOVERRIDE;
	virtual void Show(bool show, ibGridCellAttr* attr = NULL, float scale = 1.0f) wxOVERRIDE;

	virtual bool IsAcceptedKey(wxKeyEvent& event) wxOVERRIDE;
	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;
	virtual void StartingClick() wxOVERRIDE;
	virtual void StartingKey(wxKeyEvent& event) wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellBoolEditor(*this);
	}

	// added GetValue so we can get the value which is in the control, see
	// also UseStringValues()
	virtual wxString GetValue() const wxOVERRIDE;

	// set the string values returned by GetValue() for the true and false
	// states, respectively
	static void UseStringValues(const wxString& valueTrue = wxT("1"),
		const wxString& valueFalse = wxString());

	// return true if the given string is equal to the string representation of
	// true value which we currently use
	static bool IsTrueValue(const wxString& value);

protected:
	wxCheckBox* CBox() const { return (wxCheckBox*)m_control; }

private:
	// These functions modify or use m_value.
	void SetValueFromGrid(int row, int col, ibGrid* grid);
	void SetGridFromValue(int row, int col, ibGrid* grid) const;

	wxString GetStringValue() const { return GetStringValue(m_value); }

	static
		wxString GetStringValue(bool value) { return ms_stringValues[value]; }

	bool m_value;

	static wxString ms_stringValues[2];
};

#endif // wxUSE_CHECKBOX

#if wxUSE_COMBOBOX

// the editor for string data allowing to choose from the list of strings
class ibGridCellChoiceEditor : public ibGridCellEditor
{
public:
	// if !allowOthers, user can't type a string not in choices array
	explicit ibGridCellChoiceEditor(size_t count = 0,
		const wxString choices[] = NULL,
		bool allowOthers = false);
	explicit ibGridCellChoiceEditor(const wxArrayString& choices,
		bool allowOthers = false)
		: ibGridCellEditor(),
		m_choices(choices),
		m_allowOthers(allowOthers)
	{
	}

	ibGridCellChoiceEditor(const ibGridCellChoiceEditor& other)
		: ibGridCellEditor(other),
		m_value(other.m_value),
		m_choices(other.m_choices),
		m_allowOthers(other.m_allowOthers)
	{
	}

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual void SetSize(const wxRect& rect) wxOVERRIDE;

	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;

	// parameters string format is "item1[,item2[...,itemN]]"
	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellChoiceEditor(*this);
	}

	// added GetValue so we can get the value which is in the control
	virtual wxString GetValue() const wxOVERRIDE;

protected:
	wxComboBox* Combo() const { return (wxComboBox*)m_control; }

	void OnComboCloseUp(wxCommandEvent& evt);

	wxString        m_value;
	wxArrayString   m_choices;
	bool            m_allowOthers;
};

#endif // wxUSE_COMBOBOX

#if wxUSE_COMBOBOX

class ibGridCellEnumEditor : public ibGridCellChoiceEditor
{
public:
	explicit ibGridCellEnumEditor(const wxString& choices = wxString());

	ibGridCellEnumEditor(const ibGridCellEnumEditor& other)
		: ibGridCellChoiceEditor(other),
		m_index(other.m_index)
	{
	}

	virtual ~ibGridCellEnumEditor() {}

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellEnumEditor(*this);
	}

	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

private:
	long m_index;
};

#endif // wxUSE_COMBOBOX

class ibGridCellAutoWrapStringEditor : public ibGridCellTextEditor
{
public:
	ibGridCellAutoWrapStringEditor()
		: ibGridCellTextEditor()
	{
	}

	ibGridCellAutoWrapStringEditor(const ibGridCellAutoWrapStringEditor& other)
		: ibGridCellTextEditor(other)
	{
	}

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellAutoWrapStringEditor(*this);
	}
};

#if wxUSE_DATEPICKCTRL

class ibGridCellDateEditor : public ibGridCellEditor
{
public:
	explicit ibGridCellDateEditor(const wxString& format = wxString());

	ibGridCellDateEditor(const ibGridCellDateEditor& other)
		: ibGridCellEditor(other),
		m_value(other.m_value),
		m_format(other.m_format)
	{
	}

	virtual void SetParameters(const wxString& params) wxOVERRIDE;

	virtual void Create(wxWindow* parent,
		wxWindowID id,
		wxEvtHandler* evtHandler) wxOVERRIDE;

	virtual void SetSize(const wxRect& rect) wxOVERRIDE;

	virtual void BeginEdit(int row, int col, ibGrid* grid) wxOVERRIDE;
	virtual bool EndEdit(int row, int col, const ibGrid* grid,
		const wxString& oldval, wxString* newval) wxOVERRIDE;
	virtual void ApplyEdit(int row, int col, ibGrid* grid) wxOVERRIDE;

	virtual void Reset() wxOVERRIDE;

	virtual ibGridCellEditor* Clone() const wxOVERRIDE
	{
		return new ibGridCellDateEditor(*this);
	}

	virtual wxString GetValue() const wxOVERRIDE;

protected:
	wxDatePickerCtrl* DatePicker() const;

private:
	wxDateTime m_value;
	wxString m_format;
};

#endif // wxUSE_DATEPICKCTRL

#endif // wxUSE_GRID

#endif // _WX_GENERIC_GRID_EDITORS_H_
