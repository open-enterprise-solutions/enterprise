// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
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

#include "deskgui/univ/ctrl/staticText.h"


#if wxUSE_STATTEXT

#include <wx/stattext.h>
#include <wx/textwrapper.h>

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/validate.h>
#endif

#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibStaticText, ibControl)
    EVT_PAINT(ibStaticText::OnPaint)
    EVT_LEFT_DOWN(ibStaticText::OnLeftDown)
    EVT_KEY_DOWN(ibStaticText::OnKeyDown)
wxEND_EVENT_TABLE()

namespace {

// wxStaticTextBase's wxLabelWrapper (stattextcmn.cpp): the text broken into lines no wider than widthMax - for Wrap,
// and for the drawing of a label of wxST_WRAP
class LabelWrapper : public wxTextWrapper
{
public:
    wxString WrapText(wxWindow *win, const wxString& text, int widthMax)
    {
        m_text.clear();
        Wrap(win, text, widthMax);
        return m_text;
    }

protected:
    virtual void OnOutputLine(const wxString& line) override { m_text += line; }
    virtual void OnNewLine() override { m_text += wxT('\n'); }

private:
    wxString m_text;
};

} // anonymous namespace

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
    // painted whole as it is sized: its label is aligned in it, wrapped at its width with wxST_WRAP (ibWindow::OnSize
    // paints again only the frame)
    style |= wxFULL_REPAINT_ON_RESIZE;

    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    SetLabel(label);
    SetInitialSize(size);

    return true;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

void ibStaticText::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    const wxRect rect(GetClientSize());
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);

    if ( !m_isHyperlink || !IsEnabled() )
    {
        dc.SetFont(GetFont());
        dc.SetTextForeground(GetForegroundColour());

        // SEAM vs univ: a label of wxST_WRAP breaks at the width it is drawn in, as wx's native one does (the property
        // grid's description); the label itself stays whole, so a wider window takes it back in longer lines
        LabelWrapper wrapper;
        const wxString label = HasFlag(wxST_WRAP) ? wrapper.WrapText(this, GetLabelText(), rect.width) : GetLabelText();
        if ( !label.empty() )
            GetRenderer()->DrawLabel(dc, label, rect, GetStateFlags(), GetAlignment(), GetAccelIndex());
        return;
    }

    // a link: the same label, underlined in the theme's link colour - where Tab stands, on the accent's pale pad, as a
    // link button's (ibButton::OnPaint): no frame of its own shows the focus
    if ( GetStateFlags() & wxCONTROL_FOCUSED )
    {
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(wxBrush(wxTHEME_COLOUR(HIGHLIGHT).ChangeLightness(170)));
        dc.DrawRectangle(rect);
    }
    dc.SetFont(GetFont().Underlined());
    dc.SetTextForeground(wxTHEME_COLOUR(HYPERLINK));
    GetRenderer()->DrawLabel(dc, GetLabelText(), rect, GetStateFlags(), GetAlignment(), GetAccelIndex());
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

void ibStaticText::Wrap(int width)
{
    // SEAM vs wx: the label as drawn, GetLabelText() - GetLabel(), the window's, is empty here (DoGetBestClientSize) -
    // and back with its '&' kept as it is
    LabelWrapper wrapper;
    SetLabelText(wrapper.WrapText(this, GetLabelText(), width));
    InvalidateBestSize();
}

wxSize ibStaticText::DoGetBestClientSize() const
{
    wxInfoDC dc(wxConstCast(this, ibStaticText));
    dc.SetFont(GetFont());

    // SEAM vs univ: the label as drawn (OnPaint) - SetLabel sets the visible label alone, and
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
