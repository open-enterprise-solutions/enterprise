// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/stattext.cpp
// Purpose:     ibStaticText
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

#include "frontend/uikit/ctrl/staticText.h"


#if wxUSE_STATTEXT

#include <wx/stattext.h>

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/theme.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

bool ibStaticText::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxString &label,
                          const wxPoint &pos,
                          const wxSize &size,
                          long style,
                          const wxString &name)
{
    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    SetLabel(label);
    SetInitialSize(size);

    return true;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibStaticText::DoDraw(ibControlRenderer *renderer)
{
    renderer->DrawLabel();
}

void ibStaticText::SetLabel(const wxString& str)
{
    // SEAM vs univ: no wxStaticTextBase ellipsizing support — show as is
    WXSetVisibleLabel(str);
    InvalidateBestSize();
}

wxSize ibStaticText::DoGetBestClientSize() const
{
    wxInfoDC dc(wxConstCast(this, ibStaticText));
    dc.SetFont(GetFont());

    wxCoord width = 0, height = 0;
    dc.GetMultiLineTextExtent(GetLabel(), &width, &height);

    // an empty label still reserves one line of height, so a sizer slot
    // sized before the first SetLabel() call doesn't collapse to zero
    if ( height == 0 )
        height = dc.GetCharHeight();

    return wxSize(width, height);
}

void ibStaticText::WXSetVisibleLabel(const wxString& str)
{
    UnivDoSetLabel(str);
}

wxString ibStaticText::WXGetVisibleLabel() const
{
    return ibControl::GetLabel();
}

/*
   FIXME: UpdateLabel() should be called on size events to allow correct
          dynamic ellipsizing of the label
*/

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticText, ibControl);

#endif // wxUSE_STATTEXT
