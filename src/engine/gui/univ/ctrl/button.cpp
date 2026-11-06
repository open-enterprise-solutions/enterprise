// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/button.cpp
// Purpose:     ibButton
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

#include "gui/univ/ctrl/button.h"
#include "gui/univ/window.h"


#if wxUSE_BUTTON

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/dcscreen.h>
    #include <wx/button.h>
    #include <wx/validate.h>
    #include <wx/settings.h>
#endif

#include <wx/arrstr.h>   // wxSplit, wxArrayString - a tile's label in lines
#include <wx/control.h>  // wxControl::Ellipsize - a tile's last line that does not fit
#include <wx/stockitem.h>

#include "gui/univ/renderer.h"
#include "gui/univ/theme.h"
#include "gui/univ/colourScheme.h"

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// default margins around the image
static const wxCoord DEFAULT_BTN_MARGIN_X = 0;  // We should give space for the border, at least.
static const wxCoord DEFAULT_BTN_MARGIN_Y = 0;

namespace {

// A label broken into lines no wider than `width` at its spaces - a tile's caption in its tile, not cut at its edge. A
// word wider than the line stands on a line of its own and is cut there, as a label that does not wrap is.
wxArrayString WrappedLines(const wxWindow& window, const wxString& label, int width)
{
    wxArrayString lines;
    wxString line;
    for (const wxString& word : wxSplit(label, wxT(' '), 0)) {
        const wxString longer = line.empty() ? word : line + wxT(' ') + word;
        if (!line.empty() && window.GetTextExtent(longer).x > width) {
            lines.push_back(line);
            line = word;
        }
        else {
            line = longer;
        }
    }
    if (!line.empty() || lines.empty())
        lines.push_back(line);
    return lines;
}

} // namespace

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

void ibButton::Init()
{
    m_isPressed =
    m_isDefault = false;

    m_marginBmpX = DEFAULT_BTN_MARGIN_X + 2;
    m_marginBmpY = DEFAULT_BTN_MARGIN_Y + 2;
}

bool ibButton::Create(wxWindow *parent,
                      wxWindowID id,
                      const wxBitmapBundle& bitmap,
                      const wxString &lbl,
                      const wxPoint &pos,
                      const wxSize &size,
                      long style,
                      const wxValidator& validator,
                      const wxString &name)
{
    wxString label(lbl);
    if (label.empty() && wxIsStockID(id))
        label = wxGetStockLabel(id);

    long ctrl_style = style & ~wxBU_ALIGN_MASK;
    ctrl_style = ctrl_style & ~wxALIGN_MASK;

    if((style & wxBU_RIGHT) == wxBU_RIGHT)
        ctrl_style |= wxALIGN_RIGHT;
    else if((style & wxBU_LEFT) == wxBU_LEFT)
        ctrl_style |= wxALIGN_LEFT;
    else
        ctrl_style |= wxALIGN_CENTRE_HORIZONTAL;

    if((style & wxBU_TOP) == wxBU_TOP)
        ctrl_style |= wxALIGN_TOP;
    else if((style & wxBU_BOTTOM) == wxBU_BOTTOM)
        ctrl_style |= wxALIGN_BOTTOM;
    else
        ctrl_style |= wxALIGN_CENTRE_VERTICAL;

    if ( !ibControl::Create(parent, id, pos, size, ctrl_style, validator, name) )
        return false;

    SetLabel(label);

    if ( bitmap.IsOk() )
        DoSetBitmap(bitmap, wxAnyButtonBase::State_Normal);

    SetInitialSize(size);

    CreateInputHandler(ibINP_HANDLER_BUTTON);

    return true;
}

ibButton::~ibButton()
{
}

// ----------------------------------------------------------------------------
// size management
// ----------------------------------------------------------------------------

/* static */
wxSize ibButton::GetDefaultSize(wxWindow* WXUNUSED(win))
{
    static wxSize s_sizeBtn;

    if ( s_sizeBtn.x == 0 )
    {
        wxScreenDC dc;

        s_sizeBtn.x = dc.GetCharWidth()*10 + 2;

        // SEAM vs univ: the canon used charHeight*11/10 + 2, which came out
        // SMALLER than a text field of the same font (line + 2*(border+pad)
        // = line + 6) — buttons and fields on one form row must be equally
        // tall, so the row height is the maximum of the two formulas
        const wxCoord hChar = dc.GetCharHeight();
        s_sizeBtn.y = wxMax(hChar*11/10 + 2, hChar + 6);
    }

    return s_sizeBtn;
}

wxSize ibButton::GetNoteExtent() const
{
    if (m_note.empty() || m_bitmapPosition != wxTOP || HasFlag(wxBU_NOTEXT))
        return wxSize();
    const wxFont noteFont = NoteFont();
    wxCoord nw = 0, nh = 0;
    GetTextExtent(m_note, &nw, &nh, nullptr, nullptr, &noteFont);
    return wxSize(nw, nh);
}

wxSize ibButton::DoGetBestClientSize() const
{
    const wxString label = HasFlag(wxBU_NOTEXT) ? wxString() : GetLabelText();
    const wxBitmap bmp = DoGetBitmap(wxAnyButtonBase::State_Normal);
    const int bmpW = bmp.IsOk() ? bmp.GetLogicalWidth() : 0;

    // Stacked - a tile: the bitmap over the label, the note under them; as wide as its widest line, the label
    // unwrapped, and as tall as it is at that width. A sizer that gives it a width asks its height there
    // (GetMinSizeFromKnownDirection).
    if ( m_bitmapPosition == wxTOP || m_bitmapPosition == wxBOTTOM )
    {
        const wxCoord tw = label.empty() ? 0 : GetTextExtent(label).x;
        return wxSize(wxMax(wxMax(tw, bmpW), GetNoteExtent().x) + 2 * FromDIP(14),
                      DoGetBestClientHeight(wxDefaultCoord));
    }

    wxInfoDC dc(wxConstCast(this, ibButton));
    wxCoord width = 0, height = 0;
    if ( !label.empty() )
        dc.GetMultiLineTextExtent(label, &width, &height);

    if ( bmp.IsOk() )
    {
        // allocate extra space for the bitmap, and the gap between it and the label
        wxCoord heightBmp = bmp.GetLogicalHeight() + 2*m_marginBmpY;
        if ( height < heightBmp )
            height = heightBmp;

        width += bmpW + 2*m_marginBmpX + (label.empty() ? 0 : FromDIP(6));
    }

    // SEAM vs univ (sizing rules per the design directive):
    //  - the HEIGHT of every default-sized button is the standard one — the
    //    uniform control-row height;
    //  - the WIDTH follows the label (plus breathing room); the standard
    //    width applies only when there is nothing to measure
    if ( !(GetWindowStyle() & wxBU_EXACTFIT) )
    {
        const wxSize szDef = GetDefaultSize();

        if ( width > 0 )
            width += 2*dc.GetCharWidth();   // label padding
        else
            width = szDef.x;                // empty button: standard width

        if ( height < szDef.y )
            height = szDef.y;
    }

    return wxSize(width, height);
}

// A tile's height at a client width: its label wrapped into that width, as it is drawn; with no width, the label on
// one line. A button laid out in a row has no height a width changes: wxDefaultCoord, its best size's.
int ibButton::DoGetBestClientHeight(int width) const
{
    if ( m_bitmapPosition != wxTOP && m_bitmapPosition != wxBOTTOM )
        return wxDefaultCoord;
    const wxString label = HasFlag(wxBU_NOTEXT) ? wxString() : GetLabelText();
    const wxCoord th = label.empty() ? GetCharHeight() : GetTextExtent(label).y;
    const wxBitmap bmp = DoGetBitmap(wxAnyButtonBase::State_Normal);
    const int bmpH = bmp.IsOk() ? bmp.GetLogicalHeight() : 0;
    const int bmpGap = (bmp.IsOk() && !label.empty()) ? FromDIP(6) : 0;
    const wxSize note = GetNoteExtent();
    const int lines = (m_bitmapPosition == wxTOP && !label.empty() && width > 0)
        ? static_cast<int>(WrappedLines(*this, label, width - 2 * FromDIP(6)).size()) : 1;
    return bmpH + bmpGap + th * lines + (note.y == 0 ? 0 : FromDIP(2) + note.y) + 2 * FromDIP(4);
}

// A sizer that has decided the width first asks the height there. A vertical box sizer squeezing a tile below its
// best width gets the height of its caption wrapped; a grid sizer asks once, at its widest item's minimum width - a
// tile whose MinimumSize is narrower than its caption is wrapped there - and lays its cells out without asking again.
// In a row (a box or a wrap sizer laid across) a tile is not asked and keeps its best size. A cell squeezed further
// gets no more height, and DoDraw ends the line that does not fit with an ellipsis. Its own minimum stays a floor.
wxSize ibButton::GetMinSizeFromKnownDirection(int direction, int size, int WXUNUSED(availableOtherDir))
{
    if ( direction != wxHORIZONTAL || size <= 0 || m_bitmapPosition != wxTOP )
        return wxDefaultSize;
    wxSize wanted(wxMin(GetBestSize().x, size), GetBestHeight(size));
    wanted.IncTo(GetMinSize());
    return wanted;
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

// The pieces - the bitmap, the label (its lines, in a tile), the note - laid out here and each drawn by the theme, which
// gives it the state's look (a disabled label greyed, the default button's white); the face under them is the theme's
// too (ibAnyButton::DoDrawBackground).
void ibButton::DoDraw(ibControlRenderer *renderer)
{
    wxDC& dc = renderer->GetDC();
    const wxRect rect = renderer->GetRect();
    ibRenderer* const theme = renderer->GetRenderer();
    const int flags = GetStateFlags();

    // …a hyperlink's label underlined, in the theme's link colour.
    const wxFont face = m_isHyperlink ? GetFont().Underlined() : GetFont();
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetFont(face);
    dc.SetTextForeground(m_isHyperlink && !(flags & wxCONTROL_DISABLED) ? wxTHEME_COLOUR(HYPERLINK)
                                                                         : GetForegroundColour());

    const wxString label = HasFlag(wxBU_NOTEXT) ? wxString() : GetLabelText();
    const wxBitmap bmp = BitmapForCurrentState();
    const int bmpW = bmp.IsOk() ? bmp.GetLogicalWidth() : 0;
    const int bmpH = bmp.IsOk() ? bmp.GetLogicalHeight() : 0;
    const int gap = (bmp.IsOk() && !label.empty()) ? FromDIP(6) : 0;

    if ( m_bitmapPosition == wxLEFT || m_bitmapPosition == wxRIGHT )
    {
        wxCoord tw = 0, th = 0;
        if ( !label.empty() )
            dc.GetMultiLineTextExtent(label, &tw, &th);

        // the bitmap and the label stand together where the button's alignment puts them (wxBU_LEFT / wxBU_RIGHT)
        const int contentW = bmpW + gap + tw;
        const int align = GetAlignment();
        const int x = (align & wxALIGN_RIGHT) ? rect.GetRight() + 1 - m_marginBmpX - contentW
                    : (align & wxALIGN_CENTRE_HORIZONTAL) ? rect.x + (rect.width - contentW) / 2
                    : rect.x + m_marginBmpX;
        const int xBitmap = m_bitmapPosition == wxLEFT ? x : x + tw + gap;
        const int xLabel = m_bitmapPosition == wxLEFT ? x + bmpW + gap : x;

        if ( bmp.IsOk() )
            theme->DrawButtonLabel(dc, wxString(), bmp, wxRect(xBitmap, rect.y, bmpW, rect.height), flags, wxALIGN_CENTRE);
        if ( !label.empty() )
            theme->DrawButtonLabel(dc, label, wxNullBitmap, wxRect(xLabel, rect.y, tw, rect.height), flags,
                                   wxALIGN_CENTRE, GetAccelIndex());
        return;
    }

    // Stacked: the note, when there is one, under the label in a smaller face - a tile's second line.
    const wxCoord th = label.empty() ? 0 : dc.GetTextExtent(label).y;
    const wxSize note = GetNoteExtent();
    const int noteGap = note.y == 0 ? 0 : FromDIP(2);
    // The label over the note wraps into the width it has - a tile is as wide as its cell, not as its caption.
    const int lineWidth = rect.width - 2 * FromDIP(6);
    wxArrayString lines = (m_bitmapPosition == wxTOP && !label.empty())
        ? WrappedLines(*this, label, lineWidth) : wxArrayString();
    // No more lines than the height has room for: a tile squeezed below its best height - a grid narrower than its
    // cells - ends its last line with an ellipsis rather than writing over its note.
    const int room = rect.height - 2 * FromDIP(4) - bmpH - gap - noteGap - note.y;
    const size_t fit = static_cast<size_t>(wxMax(1, th > 0 ? room / th : 1));
    if ( lines.size() > fit )
    {
        wxString rest = lines[fit - 1];
        for (size_t more = fit; more < lines.size(); ++more)
            rest += wxT(' ') + lines[more];
        lines.resize(fit);
        lines[fit - 1] = wxControl::Ellipsize(rest, dc, wxELLIPSIZE_END, lineWidth, wxELLIPSIZE_FLAGS_EXPAND_TABS);   // an & is drawn, not a mnemonic
    }
    const int labelH = lines.empty() ? th : th * static_cast<int>(lines.size());
    const int contentH = bmpH + gap + labelH + noteGap + note.y;
    const int y = rect.y + wxMax(FromDIP(4), (rect.height - contentH) / 2);   // a tile lower than its content keeps its picture's top

    if ( m_bitmapPosition == wxTOP )
    {
        if ( bmp.IsOk() )
            theme->DrawButtonLabel(dc, wxString(), bmp, wxRect(rect.x, y, rect.width, bmpH), flags, wxALIGN_CENTRE);
        int lineY = y + bmpH + gap;
        for (const wxString& line : lines)
        {
            theme->DrawButtonLabel(dc, line, wxNullBitmap, wxRect(rect.x, lineY, rect.width, th), flags, wxALIGN_CENTRE);
            lineY += th;
        }
        if ( note.y > 0 )
        {
            dc.SetFont(NoteFont());
            theme->DrawButtonLabel(dc, m_note, wxNullBitmap, wxRect(rect.x, lineY + noteGap, rect.width, note.y), flags,
                                   wxALIGN_CENTRE);
            dc.SetFont(face);
        }
    }
    else
    {
        if ( !label.empty() )
            theme->DrawButtonLabel(dc, label, wxNullBitmap, wxRect(rect.x, y, rect.width, th), flags, wxALIGN_CENTRE,
                                   GetAccelIndex());
        if ( bmp.IsOk() )
            theme->DrawButtonLabel(dc, wxString(), bmp, wxRect(rect.x, y + th + gap, rect.width, bmpH), flags,
                                   wxALIGN_CENTRE);
    }
}

// ----------------------------------------------------------------------------
// input processing
// ----------------------------------------------------------------------------

void ibButton::Click()
{
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    InitCommandEvent(event);
    Command(event);
}

// ----------------------------------------------------------------------------
// misc
// ----------------------------------------------------------------------------

void ibButton::SetBitmap(const wxBitmapBundle& bitmap, wxDirection dir)
{
    DoSetBitmap(bitmap, wxAnyButtonBase::State_Normal);
    SetBitmapPosition(dir);
}

void ibButton::SetBitmapPosition(wxDirection dir)
{
    if ( m_bitmapPosition == dir )
        return;
    m_bitmapPosition = dir;
    InvalidateBestSize();
    Refresh();
}

void ibButton::SetNote(const wxString& note)
{
    if ( m_note == note )
        return;
    m_note = note;
    InvalidateBestSize();
    Refresh();
}

void ibButton::SetHyperlink(bool isHyperlink)
{
    if ( m_isHyperlink == isHyperlink )
        return;
    m_isHyperlink = isHyperlink;

    // No frame: the border is this window's own (the NC pass draws it), so it goes with the style - at the wx level
    // only, as ibWindow::Create sets it: the native host never draws one.
    wxWindowBase::SetWindowStyleFlag((GetWindowStyleFlag() & ~wxBORDER_MASK)
                                     | (isHyperlink ? wxBORDER_NONE : GetDefaultBorder()));
    SetCursor(isHyperlink ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
    InvalidateBestSize();
    Refresh();
}

wxBitmap ibButton::DoGetBitmap(State which) const
{
    const wxBitmapBundle& bitmap = m_bitmaps[which];
    return bitmap.IsOk() ? bitmap.GetBitmapFor(this) : wxBitmap();
}

void ibButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
    m_bitmaps[which] = bitmap;
    InvalidateBestSize();
    Refresh();
}

void ibButton::DoSetBitmapMargins(wxCoord x, wxCoord y)
{
    m_marginBmpX = x + 2;
    m_marginBmpY = y + 2;

    InvalidateBestSize();
    Refresh();
}

wxBitmap ibButton::BitmapForCurrentState() const
{
    const int flags = GetStateFlags();
    const State which = (flags & wxCONTROL_DISABLED) ? wxAnyButtonBase::State_Disabled
                      : (flags & wxCONTROL_PRESSED)  ? wxAnyButtonBase::State_Pressed
                      : (flags & wxCONTROL_CURRENT)  ? wxAnyButtonBase::State_Current
                      : (flags & wxCONTROL_FOCUSED)  ? wxAnyButtonBase::State_Focused
                      : wxAnyButtonBase::State_Normal;
    if ( m_bitmaps[which].IsOk() )
        return DoGetBitmap(which);

    // the normal one stands for a state given none; a disabled button greys it
    const wxBitmap normal = DoGetBitmap(wxAnyButtonBase::State_Normal);
    return normal.IsOk() && which == wxAnyButtonBase::State_Disabled ? normal.ConvertToDisabled() : normal;
}

ibWindow *ibButton::SetDefault()
{
    // SEAM vs univ: no native default-button bookkeeping — the flag is ours
    m_isDefault = true;
    Refresh();
    return this;
}

wxIMPLEMENT_DYNAMIC_CLASS(ibButton, ibControl);

#endif // wxUSE_BUTTON
