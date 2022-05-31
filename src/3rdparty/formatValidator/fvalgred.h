/////////////////////////////////////////////////////////////////////////////
// Name:        fvalgred.h
// Purpose:     wxGridCellFormattedNumEditor header
// Author:      Manuel Martin
// Modified by: MM Apr 2012
// Created:     Oct 2011
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _FVALGRED_H
#define _FVALGRED_H

#ifdef __GNUG__
#pragma interface "fvalgred.h"
#endif

#if wxUSE_VALIDATORS
#if wxUSE_GRID

class wxNumberValidator;

class wxGridCellFormattedNumEditorBase
{
public:
	wxGridCellFormattedNumEditorBase() { m_ValRes = -1; }

protected:
	bool DoEndEdit(wxTextEntry* textEntry, wxString *newval);

	void DoApplyEdit(int row, int col, wxGrid* grid);

	//Error msg
	void DoOnIdle(wxIdleEvent& event, wxTextEntry* textEntry);

	//The validator used by the control
	wxNumberValidator* GetValidator();

	//If validator does something when KillFocus
	bool KFocusAction();

	wxNumberValidator m_NumValidator;
	bool m_AllowInvalid;
	wxControl* m_fcontrol;
	wxString m_fvalue;
	int m_ValRes;
	wxGrid *m_fgrid;
	int m_frow;
	int m_fcol;
};

#if wxUSE_TEXTCTRL

class wxGridCellFormattedNumEditor : public wxGridCellTextEditor,
	wxGridCellFormattedNumEditorBase
{
public:
	//For wxTextCtrl
	wxGridCellFormattedNumEditor(const wxNumberValidator& numValidator,
		bool allowInvalid);

	virtual void Create(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler);

	virtual wxGridCellEditor *Clone() const
	{
		return new wxGridCellFormattedNumEditor(m_NumValidator, m_AllowInvalid);
	}

	virtual void BeginEdit(int row, int col, wxGrid* grid);

	virtual bool EndEdit(int row, int col, const wxGrid* grid,
		const wxString& oldval, wxString *newval);
	virtual void ApplyEdit(int row, int col, wxGrid* grid);

	void OnIdle(wxIdleEvent& event);

private:
	wxDECLARE_NO_COPY_CLASS(wxGridCellFormattedNumEditor);
};

#endif // wxUSE_TEXTCTRL

#if wxUSE_COMBOBOX

class wxGridCellFormattedChoiceEditor : public wxGridCellChoiceEditor,
	wxGridCellFormattedNumEditorBase
{
public:
	//For wxComboBox
	wxGridCellFormattedChoiceEditor(const wxNumberValidator& numValidator,
		bool allowInvalid,
		const wxArrayString& choices);

	wxGridCellFormattedChoiceEditor(const wxNumberValidator& numValidator,
		bool allowInvalid,
		size_t count = 0,
		const wxString choices[] = NULL);

	virtual void Create(wxWindow* parent, wxWindowID id, wxEvtHandler* evtHandler);

	virtual wxGridCellEditor *Clone() const
	{
		return new wxGridCellFormattedChoiceEditor(m_NumValidator,
			m_AllowInvalid);
	}

	virtual void BeginEdit(int row, int col, wxGrid* grid);

	virtual bool EndEdit(int row, int col, const wxGrid* grid,
		const wxString& oldval, wxString *newval);
	virtual void ApplyEdit(int row, int col, wxGrid* grid);

	void OnIdle(wxIdleEvent& event);

private:
	wxDECLARE_NO_COPY_CLASS(wxGridCellFormattedChoiceEditor);
};

#endif // wxUSE_COMBOBOX

#endif // wxUSE_GRID
#endif // wxUSE_VALIDATORS
#endif //_FVALGRED_H

