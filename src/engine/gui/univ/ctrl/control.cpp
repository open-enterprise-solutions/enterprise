// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

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

#include "gui/univ/ctrl/control.h"
#include "gui/univ/window.h"


#if wxUSE_CONTROLS

#include <wx/control.h>
#include <wx/arrstr.h>      // wxSplit — a title's lines

#ifndef WX_PRECOMP
    #include <wx/app.h>
    #include <wx/dcclient.h>
#endif

#include "gui/univ/renderer.h"
#include "gui/univ/inputHandler.h"
#include "gui/univ/theme.h"

// ============================================================================
// implementation
// ============================================================================

wxIMPLEMENT_DYNAMIC_CLASS(ibControl, ibWindow);

// SEAM vs univ: the event-table chain must run through ibWindow (paint pipeline),
// not the native wxControlBase branch
wxBEGIN_EVENT_TABLE(ibControl, ibWindow)
    WX_EVENT_TABLE_INPUT_CONSUMER(ibControl)
    EVT_NC_PAINT(ibControl::OnNcPaint)
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
#if 0 // GUI-REVIVE: univ accel table API (Add/Remove) is absent in the native build
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
#if 0 // GUI-REVIVE: univ accel table API (Add/Remove) is absent in the native build
    wxChar accelCharOld = GetAccelChar();
#endif // wxUSE_ACCEL

    m_indexAccel = wxControlBase::FindAccelIndex(label, &m_label);

#if 0 // GUI-REVIVE: univ accel table API (Add/Remove) is absent in the native build
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

// ----------------------------------------------------------------------------
// the title
// ----------------------------------------------------------------------------

// The gap between the title and the frame - one for every control, so the fields after a column of titles start at
// one x whatever they are (the form's text field, check box and static text kept 4, 4 and 6).
static int TitleGap(const wxWindow& window)
{
    return window.FromDIP(4);
}

bool ibControl::SetFont(const wxFont& font)
{
    if ( !ibWindow::SetFont(font) )
        return false;

    if ( !m_title.empty() )
        OnTitleChanged();
    return true;
}

void ibControl::SetTitle(const wxString& title)
{
    if ( m_title == title )
        return;
    m_title = title;
    OnTitleChanged();
}

void ibControl::SetTitlePosition(wxDirection side)
{
    if ( m_titlePosition == side )
        return;
    m_titlePosition = side;
    OnTitleChanged();
}

// Measured line by line - a title with explicit '\n' gets the height of all its lines - with the window's own text
// measure, which takes no client DC: one would ask the client origin, which asks this column (GetBorderRect).
wxSize ibControl::GetTitleExtent() const
{
    wxSize extent;
    if ( m_title.empty() )
        return extent;
    for ( const wxString& line : wxSplit(m_title, wxT('\n'), 0) )
    {
        const wxSize size = GetTextExtent(line.empty() ? wxString(wxT(" ")) : line);
        extent.x = wxMax(extent.x, size.x);
        extent.y += size.y;
    }
    return extent;
}

void ibControl::SetTitleWidth(int width)
{
    if ( m_titleWidth == width )
        return;
    m_titleWidth = width;
    if ( !m_title.empty() )
        OnTitleChanged();
}

int ibControl::GetTitleWidth() const
{
    if ( m_title.empty() )
        return 0;
    return (m_titleWidth != wxDefaultCoord ? m_titleWidth : GetTitleExtent().x) + TitleGap(*this);
}

wxRect ibControl::GetBorderRect() const
{
    wxRect rect = ibWindow::GetBorderRect();
    const int column = GetTitleWidth();
    if ( m_titlePosition == wxLEFT )
        rect.x += column;
    rect.width -= column;
    return rect;
}

void ibControl::OnTitleChanged()
{
    InvalidateBestSize();

    // the client area moved within the window: what lays itself out in it (the scrollbars, a text's lines) is told,
    // and the window - its non-client part, where the title is, included - is drawn anew
    SendSizeEvent();
    wxWindow::Refresh();
}

void ibControl::OnNcPaint(wxNcPaintEvent& event)
{
    // the border is ibWindow's
    event.Skip();

    if ( m_title.empty() || !m_renderer )
        return;

    const int column = GetTitleWidth();
    const wxSize size = GetSize();
    const wxRect rectColumn(m_titlePosition == wxLEFT ? 0 : size.x - column, 0, column, size.y);
    const wxRect rectTitle(m_titlePosition == wxLEFT ? 0 : rectColumn.x + TitleGap(*this), 0,
                           column - TitleGap(*this), size.y);

    wxWindowDC dc(this);

    // the column stands on what the control stands on - its parent's surface, not its own face
    wxWindow* const parent = GetParent();
    m_renderer->DrawBackground(dc, parent != nullptr ? parent->GetBackgroundColour() : GetBackgroundColour(),
                               rectColumn, 0);

    // a line longer than its column ends with an ellipsis
    wxString title;
    for ( const wxString& line : wxSplit(m_title, wxT('\n'), 0) )
    {
        if ( !title.empty() )
            title += wxT('\n');
        title += wxControl::Ellipsize(line, dc, wxELLIPSIZE_END, rectTitle.width, wxELLIPSIZE_FLAGS_EXPAND_TABS);
    }

    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    // only the disabled state reaches it: a title is not focused, pressed nor hovered - the control is
    m_renderer->DrawLabel(dc, title, rectTitle, GetStateFlags() & wxCONTROL_DISABLED,
                          wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
}

#endif // wxUSE_CONTROLS
