// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/bmpbuttn.cpp
// Purpose:     ibBitmapButton implementation
// Author:      Vadim Zeitlin
// Created:     25.08.00
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

#include "gui/univ/ctrl/bmpButton.h"


#if wxUSE_BMPBUTTON

#include <wx/bmpbuttn.h>

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/validate.h>
#endif

wxIMPLEMENT_DYNAMIC_CLASS(ibBitmapButton, ibButton);

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ibBitmapButton
// ----------------------------------------------------------------------------

bool ibBitmapButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxBitmapBundle& bitmap,
                            const wxPoint &pos,
                            const wxSize &size,
                            long style,
                            const wxValidator& validator,
                            const wxString &name)
{
    // we add wxBU_EXACTFIT because the bitmap buttons are not the standard
    // ones and so shouldn't be forced to be of the standard size which is
    // typically too big for them
    return ibButton::Create(parent, id, bitmap, wxEmptyString,
                            pos, size, style | wxBU_EXACTFIT, validator, name);
}

#endif // wxUSE_BMPBUTTON
