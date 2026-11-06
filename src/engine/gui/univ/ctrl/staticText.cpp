// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

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

#include "gui/univ/ctrl/staticText.h"


#if wxUSE_STATTEXT

#include <wx/stattext.h>

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include "gui/univ/renderer.h"
#include "gui/univ/theme.h"
#include "gui/univ/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibStaticText, ibControl)
    EVT_LEFT_DOWN(ibStaticText::OnLeftDown)
    EVT_KEY_DOWN(ibStaticText::OnKeyDown)
wxEND_EVENT_TABLE()

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
    if ( !m_isHyperlink || !IsEnabled() )
    {
        renderer->DrawLabel();
        return;
    }

    // a link: the same label, underlined in the theme's link colour
    wxDC& dc = renderer->GetDC();
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetFont(GetFont().Underlined());
    dc.SetTextForeground(wxTHEME_COLOUR(HYPERLINK));
    renderer->GetRenderer()->DrawLabel(dc, GetLabelText(), renderer->GetRect(), GetStateFlags(), GetAlignment(),
                                       GetAccelIndex());
}

// ----------------------------------------------------------------------------
// the link
// ----------------------------------------------------------------------------

void ibStaticText::SetHyperlink(bool isHyperlink)
{
    if ( m_isHyperlink == isHyperlink )
        return;
    m_isHyperlink = isHyperlink;
    // the pointer is the part people actually read: underlined text still looks like text until the cursor changes
    SetCursor(isHyperlink ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
    Refresh();
}

void ibStaticText::OnLeftDown(wxMouseEvent& event)
{
    // a disabled window is live natively (ibWindow::DoEnable): its click stops here as ibInputConsumer stops others'
    if ( !m_isHyperlink || !IsEnabled() )
    {
        event.Skip();
        return;
    }
    SetFocus();
    Click();
}

void ibStaticText::OnKeyDown(wxKeyEvent& event)
{
    // Space / Enter on a focused link does what the click does - the keyboard reaches the same action, not a shorter one
    const int key = event.GetKeyCode();
    if ( m_isHyperlink && IsEnabled() && (key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER) )
    {
        Click();
        return;
    }
    event.Skip();
}

void ibStaticText::Click()
{
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    InitCommandEvent(event);
    Command(event);
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

    // SEAM vs univ: the label as drawn (ibControlRenderer::DrawLabel) - SetLabel sets the visible label alone, and
    // GetLabel(), the window's, stayed empty: the text was measured as nothing
    wxCoord width = 0, height = 0;
    dc.GetMultiLineTextExtent(GetLabelText(), &width, &height);

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
