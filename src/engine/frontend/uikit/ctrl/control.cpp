// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/control.cpp
// Purpose:     universal ibControl: adds handling of mnemonics
// Author:      Vadim Zeitlin
// Created:     14.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "frontend/uikit/ctrl/control.h"
#include "frontend/uikit/window.h"


#if wxUSE_CONTROLS

#include <wx/control.h>

#ifndef WX_PRECOMP
    #include <wx/app.h>
    #include <wx/dcclient.h>
#endif

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"

// ============================================================================
// implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibControl, ibWindow);

// SEAM vs univ: the event-table chain must run through ibWindow (paint pipeline),
// not the native wxControlBase branch
wxBEGIN_EVENT_TABLE(ibControl, ibWindow)
    WX_EVENT_TABLE_INPUT_CONSUMER(ibControl)
wxEND_EVENT_TABLE()

WX_FORWARD_TO_INPUT_CONSUMER(ibControl)

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void ibControl::Init()
{
    m_indexAccel = -1;
}

bool ibControl::Create(wxWindow *parent,
                       wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    if ( !ibWindow::Create(parent, id, pos, size, style, name) )
    {
        // underlying window creation failed?
        return false;
    }
#if wxUSE_VALIDATORS
    SetValidator(validator);
#endif

    return true;
}

ibControl::~ibControl()
{
#if 0 // UIKIT-REVIVE: univ accel table API (Add/Remove) is absent in the native build
    wxChar accelChar = GetAccelChar();

    if ( accelChar != wxNO_ACCEL_CHAR )
    {
        if ( ibWindow *win = wxGetTopLevelParent(this) )
        {
            wxAcceleratorEntry accelEntry(wxACCEL_ALT, (int)accelChar);
            wxAcceleratorTable *accelTable = win->GetAcceleratorTable();
            if ( accelTable && accelTable->IsOk() )
                accelTable->Remove(accelEntry);
        }
    }
#endif // wxUSE_ACCEL
}

// ----------------------------------------------------------------------------
// mnemonics handling
// ----------------------------------------------------------------------------

void ibControl::SetLabel(const wxString& label)
{
    // save original label
    ibWindow::SetLabel(label);

    UnivDoSetLabel(label);
}

void ibControl::UnivDoSetLabel(const wxString& label)
{
    wxString labelOld = m_label;
    int indexAccelOld = m_indexAccel;
#if 0 // UIKIT-REVIVE: univ accel table API (Add/Remove) is absent in the native build
    wxChar accelCharOld = GetAccelChar();
#endif // wxUSE_ACCEL

    m_indexAccel = wxControlBase::FindAccelIndex(label, &m_label);

#if 0 // UIKIT-REVIVE: univ accel table API (Add/Remove) is absent in the native build
    wxChar accelChar = GetAccelChar();

    if ( accelCharOld != accelChar )
    {
        if ( ibWindow *win = wxGetTopLevelParent(this) )
        {
            if ( wxAcceleratorTable *accelTable = win->GetAcceleratorTable() )
            {
                // remove entry only from valid accelerator table
                if ( ( accelCharOld != wxNO_ACCEL_CHAR ) && accelTable->IsOk() )
                {
                    wxAcceleratorEntry accelEntryOld(wxACCEL_ALT, (int)accelCharOld);
                    accelTable->Remove(accelEntryOld);
                }

                // accelerator table doesn't have to be valid to add
                if ( accelChar != wxNO_ACCEL_CHAR )
                {
                    wxAcceleratorEntry accelEntryNew(wxACCEL_ALT, (int)accelChar, GetId());
                    accelTable->Add(accelEntryNew);
                }
            }
        }
    }
#endif // wxUSE_ACCEL

    if ( ( m_label != labelOld ) || ( m_indexAccel != indexAccelOld ) )
    {
        Refresh();
    }
}

#endif // wxUSE_CONTROLS
