/////////////////////////////////////////////////////////////////////////////
// Name:        fvalgred.cpp
// Purpose:     wxGridCellFormattedNumEditor implementation
// Author:      Manuel Martin
// Modified by: MM May 2017
// Created:     Oct 2011
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifdef __GNUG__
#pragma implementation "fvalgred.h"
#endif

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
  #pragma hdrstop
#endif

#if wxUSE_VALIDATORS
#if wxUSE_GRID

#include "wx/grid.h"

#include "fvalnum.h"
#include "fvalgred.h"

wxNumberValidator* wxGridCellFormattedNumEditorBase::GetValidator()
{
    //The current validator
    return wxDynamicCast(m_fcontrol->GetValidator(), wxNumberValidator);
}

bool wxGridCellFormattedNumEditorBase::KFocusAction()
{
    return  (GetValidator()->GetBehavior() &
              (wxVAL_ON_KILL_FOCUS | wxVAL_RETAIN_FOCUS))
            || !GetValidator()->GetTypingStyle().IsEmpty() ;
}

bool wxGridCellFormattedNumEditorBase::DoEndEdit(wxTextEntry* textEntry,
                                                 wxString *newval)
{
    //Use current validator
    wxNumberValidator *curValidator = GetValidator();

    //If we don't do validation here, suppose Value is valid
    m_ValRes = -1;
    //Should do validation now?
    if ( KFocusAction() )
    {   //Use ValidateQuiet(), without msg.
        m_ValRes = curValidator->ValidateQuiet();
    }

    //We are leaving the editor. So, force "normal style"
    curValidator->SetFormatted(fVALFORMAT_NORMAL);

    const wxString value = textEntry->GetValue();
    if ( m_ValRes != -1 && !m_AllowInvalid )
    {   //Store current control value
        m_fvalue = value;
        return false; //An invalid value is not allowed
    }

    //If we don't change the cell, don't update the GridTable
    if ( value == m_fvalue )
        return false;

    m_fvalue = value;

    if ( newval )
        *newval = m_fvalue;

    return true;
}

void wxGridCellFormattedNumEditorBase::DoApplyEdit(int row, int col, wxGrid* grid)
{
    grid->GetTable()->SetValue(row, col, m_fvalue);

    if ( !KFocusAction() )
        return;

    //The validator manages control colors. Here we set cell colors.
    wxFValNumColors valColors = GetValidator()->GetColors();
    if ( m_ValRes == -1 )
    {   if ( valColors.colOKForegn != wxNullColour )
            grid->SetCellTextColour(row, col, valColors.colOKForegn);
        if ( valColors.colOKBackgn != wxNullColour )
            grid->SetCellBackgroundColour(row, col, valColors.colOKBackgn);
    }
    else
    {   if ( valColors.colFaForegn != wxNullColour )
            grid->SetCellTextColour(row, col, valColors.colFaForegn);
        if ( valColors.colFaBackgn != wxNullColour )
            grid->SetCellBackgroundColour(row, col, valColors.colFaBackgn);
    }
}

void wxGridCellFormattedNumEditorBase::DoOnIdle(wxIdleEvent& event,
                                                wxTextEntry* textEntry)
{
    event.Skip();
    //Only for failed validation
    if ( m_ValRes == -1 )
        return;

    wxNumberValidator *curValidator = GetValidator();
    //So let's show the error message
    curValidator->MsgOrSound(m_fgrid);

    //Regain focus
    if ( curValidator->GetBehavior() & wxVAL_RETAIN_FOCUS )
    {   //Restore grid conditions
        m_fgrid->Show();
        m_fgrid->GoToCell(m_frow, m_fcol);
        m_fgrid->EnableCellEditControl(true);
        textEntry->ChangeValue(m_fvalue);
        textEntry->SetInsertionPoint(m_ValRes);
    }

    m_ValRes = -1;
}

/////////////////////////////////////////////////////////////////////////////

#if wxUSE_TEXTCTRL

wxGridCellFormattedNumEditor::
    wxGridCellFormattedNumEditor(const wxNumberValidator& numValidator,
                                 bool allowInvalid)
{
    m_NumValidator.Copy(numValidator);
    m_AllowInvalid = allowInvalid;
}

void wxGridCellFormattedNumEditor::Create(wxWindow* parent,
                                           wxWindowID id,
                                           wxEvtHandler* evtHandler)
{
    wxGridCellTextEditor::Create(parent, id, evtHandler);

    m_fcontrol = GetControl();
    m_fcontrol->SetValidator(m_NumValidator);

    //To show messages and retain focus
    m_fcontrol->Bind(wxEVT_IDLE, &wxGridCellFormattedNumEditor::OnIdle, this);
 }

void wxGridCellFormattedNumEditor::BeginEdit(int row, int col, wxGrid* grid)
{
    //This is a reusable control, so we must store current status because
    //we may need it at OnIdle
    m_fgrid = grid;
    m_frow = row;
    m_fcol = col;
    wxGridCellTextEditor::BeginEdit(row, col, grid);
}

bool wxGridCellFormattedNumEditor::EndEdit(int WXUNUSED(row),
                                           int WXUNUSED(col),
                                           const wxGrid* WXUNUSED(grid),
                                           const wxString& WXUNUSED(oldval),
                                           wxString *newval)
{
    return DoEndEdit(Text(), newval);
}

void wxGridCellFormattedNumEditor::ApplyEdit(int row, int col, wxGrid* grid)
{
     DoApplyEdit(row, col, grid);
}

void wxGridCellFormattedNumEditor::OnIdle(wxIdleEvent& event)
{
     DoOnIdle(event, Text());
}

#endif // wxUSE_TEXTCTRL

/////////////////////////////////////////////////////////////////////////////
#if wxUSE_COMBOBOX

wxGridCellFormattedChoiceEditor::
    wxGridCellFormattedChoiceEditor(const wxNumberValidator& numValidator,
                                    bool allowInvalid,
                                    const wxArrayString& choices)
            : wxGridCellChoiceEditor(choices, true)
{
    m_NumValidator.Copy(numValidator);
    m_AllowInvalid = allowInvalid;
}

wxGridCellFormattedChoiceEditor::
    wxGridCellFormattedChoiceEditor(const wxNumberValidator& numValidator,
                                    bool allowInvalid,
                                    size_t count,
                                    const wxString choices[])
            : wxGridCellChoiceEditor(count, choices, true)
{
    m_NumValidator.Copy(numValidator);
    m_AllowInvalid = allowInvalid;
}

void wxGridCellFormattedChoiceEditor::Create(wxWindow* parent,
                                             wxWindowID id,
                                             wxEvtHandler* evtHandler)
{
    wxGridCellChoiceEditor::Create(parent, id, evtHandler);

    m_fcontrol = GetControl();
    m_fcontrol->SetValidator(m_NumValidator);

    //To show messages and retain focus
    m_fcontrol->Bind(wxEVT_IDLE, &wxGridCellFormattedChoiceEditor::OnIdle, this);
 }

void wxGridCellFormattedChoiceEditor::BeginEdit(int row, int col, wxGrid* grid)
{
    //This is a reusable control, so we must store current status because
    //we may need it at OnIdle
    m_fgrid = grid;
    m_frow = row;
    m_fcol = col;
    wxGridCellChoiceEditor::BeginEdit(row, col, grid);
}

bool wxGridCellFormattedChoiceEditor::EndEdit(int WXUNUSED(row),
                                           int WXUNUSED(col),
                                           const wxGrid* WXUNUSED(grid),
                                           const wxString& WXUNUSED(oldval),
                                           wxString *newval)
{
    return DoEndEdit(Combo(), newval);
}

void wxGridCellFormattedChoiceEditor::ApplyEdit(int row, int col, wxGrid* grid)
{
     DoApplyEdit(row, col, grid);
}

void wxGridCellFormattedChoiceEditor::OnIdle(wxIdleEvent& event)
{
     DoOnIdle(event, Combo());
}

#endif // wxUSE_COMBOBOX

#endif // wxUSE_GRID
#endif // wxUSE_VALIDATORS
