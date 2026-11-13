// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/stdrend.cpp
// Purpose:     implementation of ibThemeRenderer
// Author:      Vadim Zeitlin
// Created:     2006-09-16
// Copyright:   (c) 2006 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// for compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>


#ifndef WX_PRECOMP
    #include <wx/settings.h>
    #include <wx/brush.h>
    #include <wx/dc.h>
    #include <wx/statusbr.h>
    #include <wx/toplevel.h>
#endif //WX_PRECOMP

#include "deskgui/univ/theme/themeRenderer.h"
#include "deskgui/univ/window.h"
#include "deskgui/univ/colourScheme.h"

#include <wx/control.h>     // wxControl::Ellipsize, for DrawItemText and DrawHeaderButtonContents
#include <wx/graphics.h>    // the expander's chevron, smoothed
#include <wx/image.h>

#include <memory>

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// the margin between listbox item text and its rectangle
static const int ITEM_MARGIN = 1;

// ============================================================================
// ibThemeRenderer implementation
// ============================================================================

// ----------------------------------------------------------------------------
// ctor
// ----------------------------------------------------------------------------

ibThemeRenderer::ibThemeRenderer(const ibColourScheme *scheme)
             : m_scheme(scheme)
{
    m_penBlack = wxPen(wxSCHEME_COLOUR(scheme, SHADOW_DARK));
    m_penDarkGrey = wxPen(wxSCHEME_COLOUR(scheme, SHADOW_OUT));
    m_penLightGrey = wxPen(wxSCHEME_COLOUR(scheme, SHADOW_IN));
    m_penHighlight = wxPen(wxSCHEME_COLOUR(scheme, SHADOW_HIGHLIGHT));

    m_titlebarFont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    m_titlebarFont.SetWeight(wxFONTWEIGHT_BOLD);
}

// ----------------------------------------------------------------------------
// helper functions
// ----------------------------------------------------------------------------

void
ibThemeRenderer::DrawSolidRect(wxDC& dc, const wxColour& col, const wxRect& rect)
{
    dc.SetBrush(col);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(rect);
}

void ibThemeRenderer::DrawRect(wxDC& dc, wxRect *rect, const wxPen& pen)
{
    // draw
    dc.SetPen(pen);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(*rect);

    // adjust the rect
    rect->Inflate(-1);
}

void ibThemeRenderer::DrawShadedRect(wxDC& dc, wxRect *rect,
                                   const wxPen& pen1, const wxPen& pen2)
{
    // draw the rectangle
    dc.SetPen(pen1);
    dc.DrawLine(rect->GetLeft(), rect->GetTop(),
                rect->GetLeft(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft() + 1, rect->GetTop(),
                rect->GetRight(), rect->GetTop());
    dc.SetPen(pen2);
    dc.DrawLine(rect->GetRight(), rect->GetTop(),
                rect->GetRight(), rect->GetBottom());
    dc.DrawLine(rect->GetLeft(), rect->GetBottom(),
                rect->GetRight() + 1, rect->GetBottom());

    // adjust the rect
    rect->Inflate(-1);
}

// ----------------------------------------------------------------------------
// translate various flags into corresponding renderer constants
// ----------------------------------------------------------------------------

/* static */
void ibThemeRenderer::GetIndicatorsFromFlags(int flags,
                                           IndicatorState& state,
                                           IndicatorStatus& status)
{
    if ( flags & wxCONTROL_SELECTED )
        state = flags & wxCONTROL_DISABLED ? IndicatorState_SelectedDisabled
                                           : IndicatorState_Selected;
    else if ( flags & wxCONTROL_DISABLED )
        state = IndicatorState_Disabled;
    else if ( flags & wxCONTROL_PRESSED )
        state = IndicatorState_Pressed;
    else
        state = IndicatorState_Normal;

    status = flags & wxCONTROL_CHECKED ? IndicatorStatus_Checked
                                       : flags & wxCONTROL_UNDETERMINED
                                            ? IndicatorStatus_Undetermined
                                            : IndicatorStatus_Unchecked;
}

/* static */
ibThemeRenderer::ArrowDirection ibThemeRenderer::GetArrowDirection(wxDirection dir)
{
    switch ( dir )
    {
        case wxLEFT:
            return Arrow_Left;

        case wxRIGHT:
            return Arrow_Right;

        case wxUP:
            return Arrow_Up;

        case wxDOWN:
            return Arrow_Down;

        default:
            wxFAIL_MSG(wxT("unknown arrow direction"));
    }

    return Arrow_Max;
}

// ----------------------------------------------------------------------------
// background
// ----------------------------------------------------------------------------

void ibThemeRenderer::DrawBackground(wxDC& dc,
                                   const wxColour& col,
                                   const wxRect& rect,
                                   int WXUNUSED(flags),
                                   ibWindow *window)
{
    wxColour colBg;

    if (col.IsOk())
    {
        colBg = col;
    }
    else if (window)
    {
        colBg = m_scheme->GetBackground(window);
    }
    else
    {
        colBg = wxSCHEME_COLOUR(m_scheme, CONTROL);
    }

    DrawSolidRect(dc, colBg, rect);
}


void ibThemeRenderer::DrawButtonSurface(wxDC& dc,
                                      const wxColour& col,
                                      const wxRect& rect,
                                      int flags)
{
    DrawBackground(dc, col, rect, flags);
}

// ----------------------------------------------------------------------------
// text
// ----------------------------------------------------------------------------

void
ibThemeRenderer::DrawFocusRect(wxWindow* WXUNUSED(win), wxDC& dc, const wxRect& rect, int WXUNUSED(flags))
{
    // draw the pixels manually because the "dots" in wxPen with wxPENSTYLE_DOT style
    // may be short traits and not really dots
    //
    // note that to behave in the same manner as DrawRect(), we must exclude
    // the bottom and right borders from the rectangle
    wxCoord x1 = rect.GetLeft(),
            y1 = rect.GetTop(),
            x2 = rect.GetRight(),
            y2 = rect.GetBottom();

    dc.SetPen(m_penBlack);

    // this seems to be closer than what Windows does than wxINVERT although
    // I'm still not sure if it's correct
    dc.SetLogicalFunction(wxAND_REVERSE);

    wxCoord z;
    for ( z = x1 + 1; z < x2; z += 2 )
        dc.DrawPoint(z, rect.GetTop());

    wxCoord shift = z == x2 ? 0 : 1;
    for ( z = y1 + shift; z < y2; z += 2 )
        dc.DrawPoint(x2, z);

    shift = z == y2 ? 0 : 1;
    for ( z = x2 - shift; z > x1; z -= 2 )
        dc.DrawPoint(z, y2);

    shift = z == x1 ? 0 : 1;
    for ( z = y2 - shift; z > y1; z -= 2 )
        dc.DrawPoint(x1, z);

    dc.SetLogicalFunction(wxCOPY);
}

// SEAM vs univ: the selection of every list's and data view's item is the theme's (the delegate was the system's - the
// explorer's pale blue under the theme's white selected text, which it hid): the highlight in a focused window, paler
// in one without the focus, as ibTreeCtrl's; an item current but not selected gets the focus rect, as wx's generic
void ibThemeRenderer::DrawItemSelectionRect(wxWindow* win, wxDC& dc, const wxRect& rect, int flags)
{
    if ( !(flags & wxCONTROL_SELECTED) )
    {
        if ( (flags & wxCONTROL_CURRENT) && (flags & wxCONTROL_FOCUSED) )
            DrawFocusRect(win, dc, rect, flags);
        return;
    }

    wxColour colour = m_scheme->Get(ibColourScheme::HIGHLIGHT);
    if ( !(flags & wxCONTROL_FOCUSED) )
        colour = colour.ChangeLightness(130);

    wxDCPenChanger penChanger(dc, *wxTRANSPARENT_PEN);
    wxDCBrushChanger brushChanger(dc, wxBrush(colour));
    dc.DrawRectangle(rect);
}

// SEAM vs univ: an item's text on that selection is the theme's too (FORKED from wxRendererGeneric::DrawItemText; the
// delegate drew it through the system's theme, in its own colour for a selected item): the highlight's text on a
// selected item, the disabled text on a disabled one, else the colour the caller set on the DC
void ibThemeRenderer::DrawItemText(wxWindow* WXUNUSED(win), wxDC& dc, const wxString& text, const wxRect& rect,
                                   int align, int flags, wxEllipsizeMode ellipsizeMode)
{
    wxColour textColour;
    if ( flags & wxCONTROL_SELECTED )
        textColour = m_scheme->Get(ibColourScheme::HIGHLIGHT_TEXT);
    else if ( flags & wxCONTROL_DISABLED )
        textColour = m_scheme->Get(ibColourScheme::CONTROL_TEXT_DISABLED);

    // the default flags would consume the ampersands in the text and expand its tabs
    const wxString paintText = wxControl::Ellipsize(text, dc, ellipsizeMode, rect.GetWidth(), wxELLIPSIZE_FLAGS_NONE);

    wxDCTextColourChanger setTextFg(dc);
    if ( textColour.IsOk() )
        setTextFg.Set(textColour);
    wxDCTextBgColourChanger setTextBg(dc, wxTransparentColour);
    dc.DrawLabel(paintText, rect, align);
}

// SEAM vs univ: every column header's button is the theme's (the delegate was the system's - the explorer's white
// header, the data view's its own 3D pens): the control's face, lighter under the mouse and darker pressed, with a rule
// down its right and along its bottom; the filler past the last column (wxCONTROL_DIRTY) has none on its right, the
// control's edge is there. A header in a control without a border frames the control's outer edges too, in the same
// rule - the top of a button on its top edge, the left of one on its left edge (a column header: all of them and the
// first; the grid's row labels: the first and all of them), or that edge reads as cut off (Max, 2026-08-19: "on the left
// something ate it")
int ibThemeRenderer::DrawHeaderButton(wxWindow* win, wxDC& dc, const wxRect& rect, int flags,
                                      wxHeaderSortIconType sortArrow, wxHeaderButtonParams* params)
{
    ibColourScheme::StdColour face = ibColourScheme::CONTROL;
    if ( flags & wxCONTROL_PRESSED )
        face = ibColourScheme::CONTROL_PRESSED;
    else if ( flags & wxCONTROL_CURRENT )
        face = ibColourScheme::CONTROL_CURRENT;
    DrawSolidRect(dc, m_scheme->Get(face), rect);

    wxDCPenChanger penChanger(dc, wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
    if ( !(flags & wxCONTROL_DIRTY) )
        dc.DrawLine(rect.GetRight(), rect.GetTop(), rect.GetRight(), rect.GetBottom() + 1);
    dc.DrawLine(rect.GetLeft(), rect.GetBottom(), rect.GetRight() + 1, rect.GetBottom());

    const wxWindow* const control = win->GetParent();
    if ( control != nullptr && control->GetBorder() == wxBORDER_NONE )
    {
        if ( rect.GetTop() <= 0 )
            dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetRight() + 1, rect.GetTop());
        if ( rect.GetLeft() <= 0 )
            dc.DrawLine(rect.GetLeft(), rect.GetTop(), rect.GetLeft(), rect.GetBottom() + 1);
    }

    return DrawHeaderButtonContents(win, dc, rect, flags, sortArrow, params);
}

// SEAM vs univ: the button's contents in the theme's colours (FORKED from wxRendererGeneric::DrawHeaderButtonContents,
// a title of several lines measured whole, as the data view's header has them): the title in the window's own colour
// if it has one, else the control's text, the disabled text when disabled; the sort arrow in the dark shadow; a
// selected column underlined in the highlight
int ibThemeRenderer::DrawHeaderButtonContents(wxWindow* win, wxDC& dc, const wxRect& rect, int flags,
                                              wxHeaderSortIconType sortArrow, wxHeaderButtonParams* params)
{
    int labelWidth = 0;

    // Mark this item as selected.  For the generic version we'll just draw an
    // underline
    if ( flags & wxCONTROL_SELECTED )
    {
        const int penwidth = 3;
        int y = rect.y + rect.height + 1 - penwidth;
        wxColour c = (params && params->m_selectionColour.IsOk()) ?
            params->m_selectionColour : m_scheme->Get(ibColourScheme::HIGHLIGHT);
        wxPen pen(c, penwidth);
        pen.SetCap(wxCAP_BUTT);
        wxDCPenChanger setPen(dc, pen);
        dc.DrawLine(rect.x, y, rect.x + rect.width, y);
    }

    // Draw an up or down arrow
    int arrowSpace = 0;
    if ( sortArrow != wxHDR_SORT_ICON_NONE )
    {
        wxRect ar = rect;

        // make a rect for the arrow
        ar.SetSize(wxWindow::FromDIP(wxSize(8, 4), win));
        ar.y += (rect.height - ar.height) / 2;
        ar.x = ar.x + rect.width - 3 * ar.width / 2;
        arrowSpace = 3 * ar.width / 2; // space to preserve when drawing the label

        wxPoint triPt[3];
        if ( sortArrow & wxHDR_SORT_ICON_UP )
        {
            triPt[0].x = ar.width / 2;
            triPt[0].y = 0;
            triPt[1].x = ar.width;
            triPt[1].y = ar.height;
            triPt[2].x = 0;
            triPt[2].y = ar.height;
        }
        else
        {
            triPt[0].x = 0;
            triPt[0].y = 0;
            triPt[1].x = ar.width;
            triPt[1].y = 0;
            triPt[2].x = ar.width / 2;
            triPt[2].y = ar.height;
        }

        wxColour c = (params && params->m_arrowColour.IsOk()) ?
            params->m_arrowColour : m_scheme->Get(ibColourScheme::SHADOW_DARK);

        wxDCPenChanger setPen(dc, c);
        wxDCBrushChanger setBrush(dc, c);

        wxDCClipper clip(dc, rect);
        dc.DrawPolygon(3, triPt, ar.x, ar.y);
    }
    labelWidth += arrowSpace;

    int bmpWidth = 0;

    // draw the bitmap if there is one
    if ( params && params->m_labelBitmap.IsOk() )
    {
        int w = params->m_labelBitmap.GetLogicalWidth();
        int h = params->m_labelBitmap.GetLogicalHeight();

        const int margin = 1; // an extra pixel on either side of the bitmap

        bmpWidth = w + 2 * margin;
        labelWidth += bmpWidth;

        int x = rect.x + margin;
        const int y = rect.y + wxMax(1, (rect.height - h) / 2);

        const int extraSpace = rect.width - labelWidth;
        if ( params->m_labelText.empty() && extraSpace > 0 )
        {
            // use the alignment flags
            switch ( params->m_labelAlignment )
            {
                default:
                case wxALIGN_LEFT:
                    break;

                case wxALIGN_CENTER:
                    x += extraSpace / 2;
                    break;

                case wxALIGN_RIGHT:
                    x += extraSpace;
                    break;
            }
        }

        wxDCClipper clip(dc, rect);
        dc.DrawBitmap(params->m_labelBitmap, x, y, true);
    }

    // Draw a label if one is given
    if ( params && !params->m_labelText.empty() )
    {
        const int margin = 5;   // number of pixels to reserve on either side of the label
        labelWidth += 2 * margin;

        wxFont font = params->m_labelFont.IsOk() ?
            params->m_labelFont : win->GetFont();
        wxColour clr;
        if ( flags & wxCONTROL_DISABLED )
            clr = m_scheme->Get(ibColourScheme::CONTROL_TEXT_DISABLED);
        else if ( params->m_labelColour.IsOk() )
            clr = params->m_labelColour;
        else if ( win->UseForegroundColour() )
            clr = win->GetForegroundColour();
        else
            clr = m_scheme->Get(ibColourScheme::CONTROL_TEXT);

        wxString label(params->m_labelText);

        wxDCFontChanger setFont(dc, font);
        wxDCTextColourChanger setTextFg(dc, clr);
        wxDCTextBgModeChanger setBgMode(dc, wxBRUSHSTYLE_TRANSPARENT);

        int tw, th;
        dc.GetMultiLineTextExtent(label, &tw, &th);

        int x = rect.x + bmpWidth + margin;
        const int y = rect.y + wxMax(0, (rect.height - th) / 2);

        // truncate and add an ellipsis (...) if the text is too wide.
        const int availWidth = rect.width - labelWidth;
        if ( tw > availWidth )
        {
            label = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, availWidth, wxELLIPSIZE_FLAGS_NONE);
            tw = dc.GetTextExtent(label).x;
        }
        else // enough space, we can respect alignment
        {
            switch ( params->m_labelAlignment )
            {
                default:
                case wxALIGN_LEFT:
                    break;

                case wxALIGN_CENTER:
                    x += (availWidth - tw) / 2;
                    break;

                case wxALIGN_RIGHT:
                    x += availWidth - tw;
                    break;
            }
        }

        dc.DrawText(label, x, y);

        labelWidth += tw;
    }

    return labelWidth;
}

// SEAM vs univ: the expander of every tree, grid and outline is the theme's (the delegate was wx's generic 16x16
// box): a chevron, no box - pointing right while collapsed, down when expanded (Max, 2026-11-10, the way a modern
// tree draws it), in the sort arrow's dark shadow. SMOOTHED, a DIP and a half thick, round at its ends - drawn on
// whole pixels it read faint beside the system's glyph the old tables had (Max, 2026-11-10: "the chevrons show very
// weakly"). Into an image of its own, laid on the DC as a picture: wxMSW's GDI+ context made over a DC takes its
// device origin but not its logical one (msw/graphics.cpp), which a tree's DC may carry, and a picture is laid at
// the logical place
void ibThemeRenderer::DrawTreeItemButton(wxWindow* win, wxDC& dc, const wxRect& rect, int flags)
{
    if ( rect.width <= 0 || rect.height <= 0 )
        return;

    wxImage glyph(rect.width, rect.height);
    glyph.InitAlpha();
    memset(glyph.GetAlpha(), 0, static_cast<size_t>(rect.width) * rect.height);   // transparent, the row shows through

    const double dip = win != nullptr ? win->GetDPIScaleFactor() : 1.0;
    {
        const std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(glyph));
        if ( !gc )
            return;
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(m_scheme->Get(ibColourScheme::SHADOW_DARK))
            .Width(1.5 * dip).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));

        const double side = wxMin(rect.width, rect.height);
        const double arm = side * 0.36;   // half the chevron's span; its depth is half of that
        const double xMiddle = rect.width / 2.0;
        const double yMiddle = rect.height / 2.0;

        wxGraphicsPath path = gc->CreatePath();
        if ( flags & wxCONTROL_EXPANDED )
        {
            path.MoveToPoint(xMiddle - arm, yMiddle - arm / 2);
            path.AddLineToPoint(xMiddle, yMiddle + arm / 2);
            path.AddLineToPoint(xMiddle + arm, yMiddle - arm / 2);
        }
        else
        {
            path.MoveToPoint(xMiddle - arm / 2, yMiddle - arm);
            path.AddLineToPoint(xMiddle + arm / 2, yMiddle);
            path.AddLineToPoint(xMiddle - arm / 2, yMiddle + arm);
        }
        gc->StrokePath(path);
    }   // the context writes the image as it goes

    dc.DrawBitmap(wxBitmap(glyph), rect.x, rect.y, true);
}

wxSize ibThemeRenderer::GetExpanderSize(wxWindow* win)
{
    return win->FromDIP(wxSize(9, 9));
}

void ibThemeRenderer::DrawLabel(wxDC& dc,
                              const wxString& label,
                              const wxRect& rect,
                              int flags,
                              int alignment,
                              int indexAccel,
                              wxRect *rectBounds)
{
    // SEAM vs univ: a label is not a button - the control's own bit (wxCONTROL_SPECIAL: a menu item's submenu, a tree's
    // expanded node, a button's default) is not passed on: read as a default button's, a menu item with a submenu was
    // drawn white (2026-11-11, Max: "the submenu is drawn white")
    DrawButtonLabel(dc, label, wxNullBitmap, rect, flags & ~wxCONTROL_SPECIAL,
                    alignment, indexAccel, rectBounds);
}

void ibThemeRenderer::DrawButtonLabel(wxDC& dc,
                                    const wxString& label,
                                    const wxBitmap& image,
                                    const wxRect& rect,
                                    int flags,
                                    int alignment,
                                    int indexAccel,
                                    wxRect *rectBounds)
{
    wxDCTextColourChanger clrChanger(dc);

    wxRect rectLabel = rect;
    if ( !label.empty() && (flags & wxCONTROL_DISABLED) )
    {
        if ( flags & wxCONTROL_PRESSED )
        {
            // shift the label if a button is pressed
            rectLabel.Offset(1, 1);
        }

        // SEAM vs univ: the theme's disabled text, plain (CONTROL_TEXT_DISABLED) - the canon's was embossed, a white
        // shadow under a light grey, and on the theme's light surfaces nothing of it could be read (2026-11-11, Max:
        // "all white, nothing to see" - the menu's disabled items)
        clrChanger.Set(m_scheme->Get(ibColourScheme::CONTROL_TEXT_DISABLED));

        if ( flags & wxCONTROL_FOCUSED )
        {
            // leave enough space for the focus rect
            rectLabel.Inflate(-2);
        }
    }

    dc.DrawLabel(label, image, rectLabel, alignment, indexAccel, rectBounds);

    if ( !label.empty() && (flags & wxCONTROL_FOCUSED) )
    {
        rectLabel.Inflate(-1);

        DrawFocusRect(nullptr, dc, rectLabel);
    }
}

// ----------------------------------------------------------------------------
// borders
// ----------------------------------------------------------------------------

/*
   We implement standard-looking 3D borders which have the following appearance:

   The raised border:

   WWWWWWWWWWWWWWWWWWWWWWB
   WHHHHHHHHHHHHHHHHHHHHGB
   WH                   GB  W = white       (HILIGHT)
   WH                   GB  H = light grey  (LIGHT)
   WH                   GB  G = dark grey   (SHADOI)
   WH                   GB  B = black       (DKSHADOI)
   WH                   GB
   WH                   GB
   WGGGGGGGGGGGGGGGGGGGGGB
   BBBBBBBBBBBBBBBBBBBBBBB

   The sunken border looks like this:

   GGGGGGGGGGGGGGGGGGGGGGW
   GBBBBBBBBBBBBBBBBBBBBHW
   GB                   HW
   GB                   HW
   GB                   HW
   GB                   HW
   GB                   HW
   GB                   HW
   GHHHHHHHHHHHHHHHHHHHHHW
   WWWWWWWWWWWWWWWWWWWWWWW

   The static border (used for the controls which don't get focus) is like
   this:

   GGGGGGGGGGGGGGGGGGGGGGW
   G                     W
   G                     W
   G                     W
   G                     W
   G                     W
   G                     W
   G                     W
   WWWWWWWWWWWWWWWWWWWWWWW

   The most complicated is the double border which is a combination of special
   "anti-sunken" border and an extra border inside it:

   HHHHHHHHHHHHHHHHHHHHHHB
   HWWWWWWWWWWWWWWWWWWWWGB
   HWHHHHHHHHHHHHHHHHHHHGB
   HWH                 HGB
   HWH                 HGB
   HWH                 HGB
   HWH                 HGB
   HWHHHHHHHHHHHHHHHHHHHGB
   HGGGGGGGGGGGGGGGGGGGGGB
   BBBBBBBBBBBBBBBBBBBBBBB

   And the simple border is, well, simple:

   BBBBBBBBBBBBBBBBBBBBBBB
   B                     B
   B                     B
   B                     B
   B                     B
   B                     B
   B                     B
   B                     B
   B                     B
   BBBBBBBBBBBBBBBBBBBBBBB
*/

void ibThemeRenderer::DrawRaisedBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penHighlight, m_penBlack);
    DrawShadedRect(dc, rect, m_penLightGrey, m_penDarkGrey);
}

void ibThemeRenderer::DrawSunkenBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penDarkGrey, m_penHighlight);
    DrawShadedRect(dc, rect, m_penBlack, m_penLightGrey);
}

void ibThemeRenderer::DrawAntiSunkenBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penLightGrey, m_penBlack);
    DrawShadedRect(dc, rect, m_penHighlight, m_penDarkGrey);
}

void ibThemeRenderer::DrawBoxBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penDarkGrey, m_penHighlight);
    DrawShadedRect(dc, rect, m_penHighlight, m_penDarkGrey);
}

void ibThemeRenderer::DrawStaticBorder(wxDC& dc, wxRect *rect)
{
    DrawShadedRect(dc, rect, m_penDarkGrey, m_penHighlight);
}

void ibThemeRenderer::DrawExtraBorder(wxDC& dc, wxRect *rect)
{
    DrawRect(dc, rect, m_penLightGrey);
}

void ibThemeRenderer::DrawBorder(wxDC& dc,
                               wxBorder border,
                               const wxRect& rectTotal,
                               int WXUNUSED(flags),
                               wxRect *rectIn)
{
    wxRect rect = rectTotal;

    switch ( border )
    {
        case wxBORDER_SUNKEN:
        case wxBORDER_THEME:
            DrawSunkenBorder(dc, &rect);
            break;

        // wxBORDER_DOUBLE and wxBORDER_THEME are currently the same value.
#if 0
        case wxBORDER_DOUBLE:
            DrawAntiSunkenBorder(dc, &rect);
            DrawExtraBorder(dc, &rect);
            break;
#endif

        case wxBORDER_STATIC:
            DrawStaticBorder(dc, &rect);
            break;

        case wxBORDER_RAISED:
            DrawRaisedBorder(dc, &rect);
            break;

        case wxBORDER_SIMPLE:
            DrawRect(dc, &rect, m_penBlack);
            break;

        default:
            wxFAIL_MSG(wxT("unknown border type"));
            // fall through

        case wxBORDER_DEFAULT:
        case wxBORDER_NONE:
            break;
    }

    if ( rectIn )
        *rectIn = rect;
}

wxRect ibThemeRenderer::GetBorderDimensions(wxBorder border) const
{
    wxCoord width;
    switch ( border )
    {
        case wxBORDER_SIMPLE:
        case wxBORDER_STATIC:
            width = 1;
            break;

        case wxBORDER_RAISED:
        case wxBORDER_SUNKEN:
        case wxBORDER_THEME:
            width = 2;
            break;
#if 0
        case wxBORDER_DOUBLE:
            width = 3;
            break;
#endif
        default:
            wxFAIL_MSG(wxT("unknown border type"));
            // fall through

        case wxBORDER_DEFAULT:
        case wxBORDER_NONE:
            width = 0;
            break;
    }

    wxRect rect;
    rect.x =
    rect.y =
    rect.width =
    rect.height = width;

    return rect;
}

void ibThemeRenderer::AdjustSize(wxSize *size, const ibWindow *window)
{
    // take into account the border width
    wxRect rectBorder = GetBorderDimensions(window->GetBorder());
    size->x += rectBorder.x + rectBorder.width;
    size->y += rectBorder.y + rectBorder.height;
}

bool ibThemeRenderer::AreScrollbarsInsideBorder() const
{
    return false;
}

wxCoord ibThemeRenderer::GetListboxItemHeight(wxCoord fontHeight)
{
    return fontHeight + 2*ITEM_MARGIN;
}

void ibThemeRenderer::DrawTextBorder(wxDC& dc,
                                   wxBorder border,
                                   const wxRect& rect,
                                   int flags,
                                   wxRect *rectIn)
{
    DrawBorder(dc, border, rect, flags, rectIn);
}

// ----------------------------------------------------------------------------
// lines and boxes
// ----------------------------------------------------------------------------

void
ibThemeRenderer::DrawHorizontalLine(wxDC& dc, wxCoord y, wxCoord x1, wxCoord x2)
{
    dc.SetPen(m_penDarkGrey);
    dc.DrawLine(x1, y, x2 + 1, y);

    dc.SetPen(m_penHighlight);
    y++;
    dc.DrawLine(x1, y, x2 + 1, y);
}

void
ibThemeRenderer::DrawVerticalLine(wxDC& dc, wxCoord x, wxCoord y1, wxCoord y2)
{
    dc.SetPen(m_penDarkGrey);
    dc.DrawLine(x, y1, x, y2 + 1);

    dc.SetPen(m_penHighlight);
    x++;
    dc.DrawLine(x, y1, x, y2 + 1);
}

void ibThemeRenderer::DrawFrameWithoutLabel(wxDC& dc,
                                          const wxRect& rectFrame,
                                          const wxRect& rectLabel)
{
    // draw left, bottom and right lines entirely
    DrawVerticalLine(dc, rectFrame.GetLeft(),
                     rectFrame.GetTop(), rectFrame.GetBottom() - 2);
    DrawHorizontalLine(dc, rectFrame.GetBottom() - 1,
                       rectFrame.GetLeft(), rectFrame.GetRight());
    DrawVerticalLine(dc, rectFrame.GetRight() - 1,
                     rectFrame.GetTop(), rectFrame.GetBottom() - 1);

    // and 2 parts of the top line
    DrawHorizontalLine(dc, rectFrame.GetTop(),
                       rectFrame.GetLeft() + 1, rectLabel.GetLeft());
    DrawHorizontalLine(dc, rectFrame.GetTop(),
                       rectLabel.GetRight(), rectFrame.GetRight() - 2);
}

void ibThemeRenderer::DrawFrameWithLabel(wxDC& dc,
                                       const wxString& label,
                                       const wxRect& rectFrame,
                                       const wxRect& rectText,
                                       int flags,
                                       int alignment,
                                       int indexAccel)
{
    wxRect rectLabel;
    DrawLabel(dc, label, rectText, flags, alignment, indexAccel, &rectLabel);

    DrawFrameWithoutLabel(dc, rectFrame, rectLabel);
}

void ibThemeRenderer::DrawFrame(wxDC& dc,
                              const wxString& label,
                              const wxRect& rect,
                              int flags,
                              int alignment,
                              int indexAccel)
{
    wxCoord height = 0; // of the label
    wxRect rectFrame = rect;
    if ( !label.empty() )
    {
        // the text should touch the top border of the rect, so the frame
        // itself should be lower
        dc.GetTextExtent(label, nullptr, &height);
        rectFrame.y += height / 2;
        rectFrame.height -= height / 2;

        // we have to draw each part of the frame individually as we can't
        // erase the background beyond the label as it might contain some
        // pixmap already, so drawing everything and then overwriting part of
        // the frame with label doesn't work

        // TODO: the +5 shouldn't be hard coded
        wxRect rectText;
        rectText.x = rectFrame.x + 5;
        rectText.y = rect.y;
        rectText.width = rectFrame.width - 7; // +2 border width
        rectText.height = height;

        DrawFrameWithLabel(dc, label, rectFrame, rectText, flags,
                           alignment, indexAccel);
    }
    else // no label
    {
        DrawBoxBorder(dc, &rectFrame);
    }
}

void ibThemeRenderer::DrawItem(wxDC& dc,
                             const wxString& label,
                             const wxRect& rect,
                             int flags)
{
    wxDCTextColourChanger colChanger(dc);

    if ( flags & wxCONTROL_SELECTED )
    {
        colChanger.Set(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT_TEXT));

        const wxColour colBg = wxSCHEME_COLOUR(m_scheme, HIGHLIGHT);
        dc.SetBrush(colBg);
        dc.SetPen(colBg);
        dc.DrawRectangle(rect);
    }

    // horizontal adjustment is arbitrary; vertically the text stands in the middle of its row - a theme's row may be
    // taller than the text (Luna's is half its height again, GetListboxItemHeight), and drawn from the top all that air
    // fell under it (2026-11-13, Max: "the selection is too tall"). On GTK3/Cocoa the same
    wxRect rectText = rect;
    rectText.Deflate(2, ITEM_MARGIN);
    dc.DrawLabel(label, wxNullBitmap, rectText, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);

    if ( flags & wxCONTROL_FOCUSED )
    {
        DrawFocusRect(nullptr, dc, rect, flags);
    }
}

void ibThemeRenderer::DrawCheckItemBitmap(wxDC& dc,
                                        const wxBitmap& bitmap,
                                        const wxRect& rect,
                                        int flags)
{
    DrawCheckButton(dc, wxEmptyString, bitmap, rect, flags);
}

void ibThemeRenderer::DrawCheckItem(wxDC& dc,
                                  const wxString& label,
                                  const wxBitmap& bitmap,
                                  const wxRect& rect,
                                  int flags)
{
    wxRect rectBitmap = rect;
    rectBitmap.width = GetCheckBitmapSize().x;
    DrawCheckItemBitmap(dc, bitmap, rectBitmap, flags);

    wxRect rectLabel = rect;
    wxCoord shift = rectBitmap.width + 2*GetCheckItemMargin();
    rectLabel.x += shift;
    rectLabel.width -= shift;
    DrawItem(dc, label, rectLabel, flags);
}

// ----------------------------------------------------------------------------
// check and radio bitmaps
// ----------------------------------------------------------------------------

void ibThemeRenderer::DrawCheckButton(wxDC& dc,
                                    const wxString& label,
                                    const wxBitmap& bitmap,
                                    const wxRect& rect,
                                    int flags,
                                    wxAlignment align,
                                    int indexAccel)
{
    if (bitmap.IsOk())
        DrawCheckOrRadioButton(dc, label, bitmap, rect, flags, align, indexAccel);
    else
        DrawCheckOrRadioButton(dc, label, GetCheckBitmap(flags), rect, flags, align, indexAccel);
}

void ibThemeRenderer::DrawRadioButton(wxDC& dc,
                                    const wxString& label,
                                    const wxBitmap& bitmap,
                                    const wxRect& rect,
                                    int flags,
                                    wxAlignment align,
                                    int indexAccel)
{
    if (bitmap.IsOk())
        DrawCheckOrRadioButton(dc, label, bitmap, rect, flags, align, indexAccel);
    else
        DrawCheckOrRadioButton(dc, label, GetRadioBitmap(flags), rect, flags, align, indexAccel);

}

// SEAM vs univ: wxRendererNative's check and radio boxes are the theme's indicators - ibRenderer's delegate drew
// the generic ones, and the forks asked the native renderer; centred in the rect, as the native box is
void ibThemeRenderer::DrawCheckBox(wxWindow* WXUNUSED(win),
                                 wxDC& dc,
                                 const wxRect& rect,
                                 int flags)
{
    const wxBitmap bitmap = GetCheckBitmap(flags);
    dc.DrawBitmap(bitmap, wxRect(bitmap.GetSize()).CentreIn(rect).GetPosition(), true /* use mask */);
}

void ibThemeRenderer::DrawRadioBitmap(wxWindow* WXUNUSED(win),
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags)
{
    const wxBitmap bitmap = GetRadioBitmap(flags);
    dc.DrawBitmap(bitmap, wxRect(bitmap.GetSize()).CentreIn(rect).GetPosition(), true /* use mask */);
}

wxSize ibThemeRenderer::GetCheckBoxSize(wxWindow* WXUNUSED(win), int WXUNUSED(flags))
{
    // the indicator's size, as ibCheckBox takes it; a cell's box (wxCONTROL_CELL) is the same, it has no margins
    return GetCheckBitmapSize();
}

void ibThemeRenderer::DrawCheckOrRadioButton(wxDC& dc,
                                           const wxString& label,
                                           const wxBitmap& bitmap,
                                           const wxRect& rect,
                                           int flags,
                                           wxAlignment align,
                                           int indexAccel)
{
    // calculate the position of the bitmap and of the label
    wxCoord heightBmp = bitmap.GetHeight();
    wxCoord xBmp,
            yBmp = rect.y + (rect.height - heightBmp) / 2;

    wxRect rectLabel;
    dc.GetMultiLineTextExtent(label, nullptr, &rectLabel.height);
    rectLabel.y = rect.y + (rect.height - rectLabel.height) / 2;

    // align label vertically with the bitmap - looks nicer like this
    rectLabel.y -= (rectLabel.height - heightBmp) % 2;

    // calc horz position
    if ( align == wxALIGN_RIGHT )
    {
        xBmp = rect.GetRight() - bitmap.GetWidth();
        rectLabel.x = rect.x + 3;
        rectLabel.SetRight(xBmp);
    }
    else // normal (checkbox to the left of the text) case
    {
        xBmp = rect.x;
        rectLabel.x = xBmp + bitmap.GetWidth() + 5;
        rectLabel.SetRight(rect.GetRight());
    }

    dc.DrawBitmap(bitmap, xBmp, yBmp, true /* use mask */);

    DrawLabel(dc, label, rectLabel, flags,
              wxALIGN_LEFT | wxALIGN_TOP, indexAccel);
}

#if wxUSE_TEXTCTRL

void ibThemeRenderer::DrawTextLine(wxDC& dc,
                                 const wxString& text,
                                 const wxRect& rect,
                                 int selStart,
                                 int selEnd,
                                 int flags)
{
    if ( (selStart == -1) || !(flags & wxCONTROL_FOCUSED) )
    {
        // just draw it as is
        dc.DrawText(text, rect.x, rect.y);
    }
    else // we have selection
    {
        wxCoord width,
                x = rect.x;

        // draw the part before selection
        wxString s(text, (size_t)selStart);
        if ( !s.empty() )
        {
            dc.DrawText(s, x, rect.y);

            dc.GetTextExtent(s, &width, nullptr);
            x += width;
        }

        // draw the selection itself
        s = wxString(text.c_str() + selStart, text.c_str() + selEnd);
        if ( !s.empty() )
        {
            wxColour colFg = dc.GetTextForeground(),
                     colBg = dc.GetTextBackground();
            dc.SetTextForeground(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT_TEXT));
            dc.SetTextBackground(wxSCHEME_COLOUR(m_scheme, HIGHLIGHT));
            dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);

            dc.DrawText(s, x, rect.y);
            dc.GetTextExtent(s, &width, nullptr);
            x += width;

            dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
            dc.SetTextBackground(colBg);
            dc.SetTextForeground(colFg);
        }

        // draw the final part
        s = text.c_str() + selEnd;
        if ( !s.empty() )
        {
            dc.DrawText(s, x, rect.y);
        }
    }
}

void ibThemeRenderer::DrawLineWrapMark(wxDC& WXUNUSED(dc),
                                     const wxRect& WXUNUSED(rect))
{
    // nothing by default
}

int ibThemeRenderer::GetTextBorderWidth() const
{
    return 1;
}

wxRect
ibThemeRenderer::GetTextTotalArea(const wxRect& rect) const
{
    wxRect rectTotal = rect;
    rectTotal.Inflate(GetTextBorderWidth());
    return rectTotal;
}

wxRect ibThemeRenderer::GetTextClientArea(const wxRect& rect,
                                        wxCoord *extraSpaceBeyond) const
{
    wxRect rectText = rect;
    rectText.Deflate(GetTextBorderWidth());

    if ( extraSpaceBeyond )
        *extraSpaceBeyond = 0;

    return rectText;
}

#endif // wxUSE_TEXTCTRL

// ----------------------------------------------------------------------------
// scrollbars drawing
// ----------------------------------------------------------------------------

void ibThemeRenderer::DrawScrollbarArrow(wxDC& dc,
                                       wxDirection dir,
                                       const wxRect& rect,
                                       int flags)
{
    DrawArrow(dc, dir, rect, flags);
}

void ibThemeRenderer::DrawScrollCorner(wxDC& dc, const wxRect& rect)
{
    DrawSolidRect(dc, wxSCHEME_COLOUR(m_scheme, CONTROL), rect);
}

// ----------------------------------------------------------------------------
// status bar
// ----------------------------------------------------------------------------

#if wxUSE_STATUSBAR

wxSize ibThemeRenderer::GetStatusBarBorders() const
{
    // Rendered border may be different depending on field's style, we use
    // the largest value so that any field certainly fits into the borders
    // we return:
    wxRect raised = GetBorderDimensions(wxBORDER_RAISED);
    wxRect flat = GetBorderDimensions(wxBORDER_STATIC);
    wxASSERT_MSG( raised.x == raised.width && raised.y == raised.height &&
                  flat.x == flat.width && flat.y == flat.height,
                  wxT("this code expects uniform borders, you must override GetStatusBarBorders") );

    // take the larger of flat/raised values:
    wxSize border(wxMax(raised.x, flat.x), wxMax(raised.y, flat.y));

    return border;
}

wxCoord ibThemeRenderer::GetStatusBarBorderBetweenFields() const
{
    return 2;
}

wxSize ibThemeRenderer::GetStatusBarFieldMargins() const
{
    return wxSize(2, 2);
}

void ibThemeRenderer::DrawStatusField(wxDC& dc,
                                    const wxRect& rect,
                                    const wxString& label,
                                    int flags,
                                    int style)
{
    wxRect rectIn;

    if ( style == wxSB_RAISED )
        DrawBorder(dc, wxBORDER_RAISED, rect, flags, &rectIn);
    else if ( style != wxSB_FLAT )
        DrawBorder(dc, wxBORDER_STATIC, rect, flags, &rectIn);
    else
        rectIn = rect;

    rectIn.Deflate(GetStatusBarFieldMargins());

    wxDCClipper clipper(dc, rectIn);
    DrawLabel(dc, label, rectIn, flags, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
}

#endif // wxUSE_STATUSBAR

// SEAM vs univ (which had none - the system's tooltip showed over the theme): a tip reads as a field's content does,
// the content's plate in the border tier's frame, the theme's text in the middle - the window is sized to the text with
// even margins round it (Max, 2026-11-10: "the tooltip can be taken too")
void ibThemeRenderer::DrawToolTip(wxDC& dc, const wxRect& rect, const wxString& text)
{
    wxDCBrushChanger brushChanger(dc, wxBrush(m_scheme->Get(ibColourScheme::CONTROL)));
    wxDCPenChanger penChanger(dc, wxPen(m_scheme->Get(ibColourScheme::SHADOW_IN)));
    dc.DrawRectangle(rect);

    wxDCTextColourChanger textChanger(dc, m_scheme->Get(ibColourScheme::CONTROL_TEXT));
    dc.DrawLabel(text, rect, wxALIGN_CENTRE);
}

// ----------------------------------------------------------------------------
// top level windows
// ----------------------------------------------------------------------------

int ibThemeRenderer::HitTestFrame(const wxRect& rect, const wxPoint& pt, int flags) const
{
    wxRect client = GetFrameClientArea(rect, flags);

    if ( client.Contains(pt) )
        return wxHT_TOPLEVEL_CLIENT_AREA;

    if ( flags & wxTOPLEVEL_TITLEBAR )
    {
        wxRect client = GetFrameClientArea(rect, flags & ~wxTOPLEVEL_TITLEBAR);

        if ( flags & wxTOPLEVEL_ICON )
        {
            if ( wxRect(client.GetPosition(), GetFrameIconSize()).Contains(pt) )
                return wxHT_TOPLEVEL_ICON;
        }

        wxRect btnRect(client.GetRight() - 2 - FRAME_BUTTON_WIDTH,
                       client.GetTop() + (FRAME_TITLEBAR_HEIGHT-FRAME_BUTTON_HEIGHT)/2,
                       FRAME_BUTTON_WIDTH, FRAME_BUTTON_HEIGHT);

        if ( flags & wxTOPLEVEL_BUTTON_CLOSE )
        {
            if ( btnRect.Contains(pt) )
                return wxHT_TOPLEVEL_BUTTON_CLOSE;
            btnRect.x -= FRAME_BUTTON_WIDTH + 2;
        }
        if ( flags & wxTOPLEVEL_BUTTON_MAXIMIZE )
        {
            if ( btnRect.Contains(pt) )
                return wxHT_TOPLEVEL_BUTTON_MAXIMIZE;
            btnRect.x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_RESTORE )
        {
            if ( btnRect.Contains(pt) )
                return wxHT_TOPLEVEL_BUTTON_RESTORE;
            btnRect.x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_ICONIZE )
        {
            if ( btnRect.Contains(pt) )
                return wxHT_TOPLEVEL_BUTTON_ICONIZE;
            btnRect.x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_HELP )
        {
            if ( btnRect.Contains(pt) )
                return wxHT_TOPLEVEL_BUTTON_HELP;
            btnRect.x -= FRAME_BUTTON_WIDTH;
        }

        if ( pt.y >= client.y && pt.y < client.y + FRAME_TITLEBAR_HEIGHT )
            return wxHT_TOPLEVEL_TITLEBAR;
    }

    if ( (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED) )
    {
        // we are certainly at one of borders, let's decide which one:

        int border = 0;
        // dirty trick, relies on the way wxHT_TOPLEVEL_XXX are defined!
        if ( pt.x < client.x )
            border |= wxHT_TOPLEVEL_BORDER_W;
        else if ( pt.x >= client.width + client.x )
            border |= wxHT_TOPLEVEL_BORDER_E;
        if ( pt.y < client.y )
            border |= wxHT_TOPLEVEL_BORDER_N;
        else if ( pt.y >= client.height + client.y )
            border |= wxHT_TOPLEVEL_BORDER_S;
        return border;
    }

    return wxHT_NOWHERE;
}

void ibThemeRenderer::DrawFrameTitleBar(wxDC& dc,
                                      const wxRect& rect,
                                      const wxString& title,
                                      const wxIcon& icon,
                                      int flags,
                                      int specialButton,
                                      int specialButtonFlags)
{
    if ( (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED) )
    {
        DrawFrameBorder(dc, rect, flags);
    }
    if ( flags & wxTOPLEVEL_TITLEBAR )
    {
        DrawFrameBackground(dc, rect, flags);
        if ( flags & wxTOPLEVEL_ICON )
            DrawFrameIcon(dc, rect, icon, flags);
        DrawFrameTitle(dc, rect, title, flags);

        wxRect client = GetFrameClientArea(rect, flags & ~wxTOPLEVEL_TITLEBAR);
        wxCoord x,y;
        x = client.GetRight() - 2 - FRAME_BUTTON_WIDTH;
        y = client.GetTop() + (FRAME_TITLEBAR_HEIGHT-FRAME_BUTTON_HEIGHT)/2;

        if ( flags & wxTOPLEVEL_BUTTON_CLOSE )
        {
            DrawFrameButton(dc, x, y, wxTOPLEVEL_BUTTON_CLOSE,
                            (specialButton == wxTOPLEVEL_BUTTON_CLOSE) ?
                            specialButtonFlags : 0);
            x -= FRAME_BUTTON_WIDTH + 2;
        }
        if ( flags & wxTOPLEVEL_BUTTON_MAXIMIZE )
        {
            DrawFrameButton(dc, x, y, wxTOPLEVEL_BUTTON_MAXIMIZE,
                            (specialButton == wxTOPLEVEL_BUTTON_MAXIMIZE) ?
                            specialButtonFlags : 0);
            x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_RESTORE )
        {
            DrawFrameButton(dc, x, y, wxTOPLEVEL_BUTTON_RESTORE,
                            (specialButton == wxTOPLEVEL_BUTTON_RESTORE) ?
                            specialButtonFlags : 0);
            x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_ICONIZE )
        {
            DrawFrameButton(dc, x, y, wxTOPLEVEL_BUTTON_ICONIZE,
                            (specialButton == wxTOPLEVEL_BUTTON_ICONIZE) ?
                            specialButtonFlags : 0);
            x -= FRAME_BUTTON_WIDTH;
        }
        if ( flags & wxTOPLEVEL_BUTTON_HELP )
        {
            DrawFrameButton(dc, x, y, wxTOPLEVEL_BUTTON_HELP,
                            (specialButton == wxTOPLEVEL_BUTTON_HELP) ?
                            specialButtonFlags : 0);
        }
    }
}

void ibThemeRenderer::DrawFrameBorder(wxDC& dc, const wxRect& rect, int flags)
{
    if ( !(flags & wxTOPLEVEL_BORDER) )
        return;

    wxRect r(rect);

    DrawAntiSunkenBorder(dc, &r);
    DrawExtraBorder(dc, &r);
    if ( flags & wxTOPLEVEL_RESIZEABLE )
        DrawExtraBorder(dc, &r);
}

void ibThemeRenderer::DrawFrameBackground(wxDC& dc, const wxRect& rect, int flags)
{
    if ( !(flags & wxTOPLEVEL_TITLEBAR) )
        return;

    wxColour col = m_scheme->Get(flags & wxTOPLEVEL_ACTIVE
                                    ? ibColourScheme::TITLEBAR_ACTIVE
                                    : ibColourScheme::TITLEBAR);

    wxRect r = GetFrameClientArea(rect, flags & ~wxTOPLEVEL_TITLEBAR);
    r.height = FRAME_TITLEBAR_HEIGHT;

    DrawBackground(dc, col, r);
}

void ibThemeRenderer::DrawFrameTitle(wxDC& dc,
                                   const wxRect& rect,
                                   const wxString& title,
                                   int flags)
{
    wxColour col = m_scheme->Get(flags & wxTOPLEVEL_ACTIVE
                                    ? ibColourScheme::TITLEBAR_ACTIVE_TEXT
                                    : ibColourScheme::TITLEBAR_TEXT);
    dc.SetTextForeground(col);

    wxRect r = GetFrameClientArea(rect, flags & ~wxTOPLEVEL_TITLEBAR);
    r.height = FRAME_TITLEBAR_HEIGHT;
    if ( flags & wxTOPLEVEL_ICON )
    {
        r.x += FRAME_TITLEBAR_HEIGHT;
        r.width -= FRAME_TITLEBAR_HEIGHT + 2;
    }
    else
    {
        r.x += 1;
        r.width -= 3;
    }

    if ( flags & wxTOPLEVEL_BUTTON_CLOSE )
        r.width -= FRAME_BUTTON_WIDTH + 2;
    if ( flags & wxTOPLEVEL_BUTTON_MAXIMIZE )
        r.width -= FRAME_BUTTON_WIDTH;
    if ( flags & wxTOPLEVEL_BUTTON_RESTORE )
        r.width -= FRAME_BUTTON_WIDTH;
    if ( flags & wxTOPLEVEL_BUTTON_ICONIZE )
        r.width -= FRAME_BUTTON_WIDTH;
    if ( flags & wxTOPLEVEL_BUTTON_HELP )
        r.width -= FRAME_BUTTON_WIDTH;

    dc.SetFont(m_titlebarFont);

    wxString s;
    wxCoord textW;
    dc.GetTextExtent(title, &textW, nullptr);
    if ( textW > r.width )
    {
        // text is too big, let's shorten it and add "..." after it:
        size_t len = title.length();
        wxCoord WSoFar, letterW;

        dc.GetTextExtent(wxT("..."), &WSoFar, nullptr);
        if ( WSoFar > r.width )
        {
            // not enough space to draw anything
            return;
        }

        s.Alloc(len);
        for (size_t i = 0; i < len; i++)
        {
            dc.GetTextExtent(title[i], &letterW, nullptr);
            if ( letterW + WSoFar > r.width )
                break;
            WSoFar += letterW;
            s << title[i];
        }
        s << wxT("...");
    }
    else // no need to truncate the title
    {
        s = title;
    }

    dc.DrawLabel(s, wxNullBitmap, r, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
}

void ibThemeRenderer::DrawFrameIcon(wxDC& dc,
                                  const wxRect& rect,
                                  const wxIcon& icon,
                                  int flags)
{
    if ( icon.IsOk() )
    {
        // SEAM vs univ: in the middle of the square the title leaves it (DrawFrameTitle) - the canon drew it in the
        // corner, of a Win95 caption barely higher than the icon
        wxRect r = GetFrameClientArea(rect, flags & ~wxTOPLEVEL_TITLEBAR);
        dc.DrawIcon(icon, r.x + (FRAME_TITLEBAR_HEIGHT - icon.GetWidth()) / 2,
                    r.y + (FRAME_TITLEBAR_HEIGHT - icon.GetHeight()) / 2);
    }
}

void ibThemeRenderer::DrawFrameButton(wxDC& dc,
                                    wxCoord x, wxCoord y,
                                    int button,
                                    int flags)
{
    FrameButtonType idx;
    switch (button)
    {
        case wxTOPLEVEL_BUTTON_CLOSE:    idx = FrameButton_Close; break;
        case wxTOPLEVEL_BUTTON_MAXIMIZE: idx = FrameButton_Maximize; break;
        case wxTOPLEVEL_BUTTON_ICONIZE:  idx = FrameButton_Minimize; break;
        case wxTOPLEVEL_BUTTON_RESTORE:  idx = FrameButton_Restore; break;
        case wxTOPLEVEL_BUTTON_HELP:     idx = FrameButton_Help; break;
        default:
            wxFAIL_MSG(wxT("incorrect button specification"));
            return;
    }

    wxBitmap bmp = GetFrameButtonBitmap(idx);
    if ( !bmp.IsOk() )
        return;

    wxRect rectBtn(x, y, FRAME_BUTTON_WIDTH, FRAME_BUTTON_HEIGHT);
    if ( flags & wxCONTROL_PRESSED )
    {
        DrawSunkenBorder(dc, &rectBtn);

        rectBtn.Offset(1, 1);
    }
    else
    {
        DrawRaisedBorder(dc, &rectBtn);
    }

    DrawBackground(dc, wxSCHEME_COLOUR(m_scheme, CONTROL), rectBtn);

    wxRect rectBmp(0, 0, bmp.GetWidth(), bmp.GetHeight());
    dc.DrawBitmap(bmp, rectBmp.CentreIn(rectBtn).GetPosition(), true);
}

int ibThemeRenderer::GetFrameBorderWidth(int flags) const
{
    return flags & wxTOPLEVEL_RESIZEABLE ? 4 : 3;
}


wxRect ibThemeRenderer::GetFrameClientArea(const wxRect& rect, int flags) const
{
    wxRect r(rect);

    if ( (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED) )
    {
        r.Inflate(-GetFrameBorderWidth(flags));
    }

    if ( flags & wxTOPLEVEL_TITLEBAR )
    {
        r.y += FRAME_TITLEBAR_HEIGHT;
        r.height -= FRAME_TITLEBAR_HEIGHT;
    }

    return r;
}

wxSize
ibThemeRenderer::GetFrameTotalSize(const wxSize& clientSize, int flags) const
{
    wxSize s(clientSize);

    if ( (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED) )
    {
        s.IncBy(2*GetFrameBorderWidth(flags));
    }

    if ( flags & wxTOPLEVEL_TITLEBAR )
        s.y += FRAME_TITLEBAR_HEIGHT;

    return s;
}

wxSize ibThemeRenderer::GetFrameMinSize(int flags) const
{
    wxSize s;

    if ( (flags & wxTOPLEVEL_BORDER) && !(flags & wxTOPLEVEL_MAXIMIZED) )
    {
        s.IncBy(2*GetFrameBorderWidth(flags));
    }

    if ( flags & wxTOPLEVEL_TITLEBAR )
    {
        s.y += FRAME_TITLEBAR_HEIGHT;

        if ( flags & wxTOPLEVEL_ICON )
            s.x += FRAME_TITLEBAR_HEIGHT + 2;
        if ( flags & wxTOPLEVEL_BUTTON_CLOSE )
            s.x += FRAME_BUTTON_WIDTH + 2;
        if ( flags & wxTOPLEVEL_BUTTON_MAXIMIZE )
            s.x += FRAME_BUTTON_WIDTH;
        if ( flags & wxTOPLEVEL_BUTTON_RESTORE )
            s.x += FRAME_BUTTON_WIDTH;
        if ( flags & wxTOPLEVEL_BUTTON_ICONIZE )
            s.x += FRAME_BUTTON_WIDTH;
        if ( flags & wxTOPLEVEL_BUTTON_HELP )
            s.x += FRAME_BUTTON_WIDTH;
    }

    return s;
}

wxSize ibThemeRenderer::GetFrameIconSize() const
{
    return wxSize(16, 16);
}
