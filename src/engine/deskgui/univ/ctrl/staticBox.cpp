// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/statbox.cpp
// Purpose:     ibStaticBox implementation
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

#include "deskgui/univ/ctrl/staticBox.h"


#if wxUSE_STATBOX

#ifndef WX_PRECOMP
    #include <wx/dc.h>
    #include <wx/log.h>
    #include <wx/statbox.h>
    #include <wx/validate.h>
#endif

#include "deskgui/univ/renderer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibStaticBox, ibControl);

// ============================================================================
// implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibStaticBox, ibControl)
    EVT_PAINT(ibStaticBox::OnPaint)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// ibStaticBox
// ----------------------------------------------------------------------------

bool ibStaticBox::Create(wxWindow *parent,
                         wxWindowID id,
                         const wxString &label,
                         const wxPoint &pos,
                         const wxSize &size,
                         long style,
                         const wxString &name)
{
    // FIXME refresh just the right/bottom parts affected in OnSize
    style |= wxFULL_REPAINT_ON_RESIZE;

    if ( !ibControl::Create(parent, id, pos, size, style, wxDefaultValidator, name) )
        return false;

    SetLabel(label);

    return true;
}

void ibStaticBox::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    ibThemePaintDC dc(this);

    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());
    dc.SetTextBackground(GetBackgroundColour());

    GetRenderer()->DrawFrame(dc, GetLabelText(), wxRect(GetClientSize()), GetStateFlags(), GetAlignment(),
                             GetAccelIndex());
}

// ----------------------------------------------------------------------------
// geometry
// ----------------------------------------------------------------------------

wxRect ibStaticBox::GetBorderGeometry() const
{
    // FIXME should use the renderer here
    wxRect rect;
    rect.width =
    rect.x = GetCharWidth() / 2 + 1;
    rect.y = GetCharHeight() + 1;
    rect.height = rect.y / 2;

    return rect;
}

wxPoint ibStaticBox::GetBoxAreaOrigin() const
{
    wxPoint pt = ibControl::GetClientAreaOrigin();
    wxRect rect = GetBorderGeometry();
    pt.x += rect.x;
    pt.y += rect.y;

    return pt;
}

// wxStaticBoxBase's (src/common/statboxcmn.cpp); this box has no label window,
// and its label is the one it draws (GetLabelText)
void ibStaticBox::GetBordersForSizer(int *borderTop, int *borderOther) const
{
    const int BORDER = FromDIP(5); // FIXME: hardcoded value

    *borderTop = GetLabelText().empty() ? BORDER : GetCharHeight();

    *borderOther = BORDER;
}

void ibStaticBox::WXDestroyWithoutChildren()
{
    // Notice that we must make a copy of the list as it will be changed by
    // Reparent() calls in the loop.
    const wxWindowList children = GetChildren();
    wxWindow* const parent = GetParent();
    for ( const auto& child : children )
    {
        child->Reparent(parent);
    }

    delete this;
}

// wxStaticBox's (src/msw/statbox.cpp)
wxSize ibStaticBox::DoGetBestSize() const
{
    wxSize best;

    // Calculate the size needed by the label
    int cx = GetCharWidth(), cy = GetCharHeight();

    int wBox;
    GetTextExtent(GetLabelText(), &wBox, &cy);

    wBox += 3*cx;
    int hBox = cy + 8; // EDIT_HEIGHT_FROM_CHAR_HEIGHT (msw/private.h)

    // If there is a sizer then the base best size is the sizer's minimum
    if (GetSizer() != nullptr)
    {
        wxSize cm(GetSizer()->CalcMin());
        best = ClientToWindowSize(cm);
        // adjust for a long label if needed
        best.x = wxMax(best.x, wBox);
    }
    // otherwise the best size falls back to the label size
    else
    {
        best = wxSize(wBox, hBox);
    }
    return best;
}

// ----------------------------------------------------------------------------
// ibStaticBoxSizer: wxStaticBoxSizer (src/common/sizer.cpp)
// ----------------------------------------------------------------------------

ibStaticBoxSizer::ibStaticBoxSizer( ibStaticBox *box, int orient )
    : wxBoxSizer( orient ),
      m_staticBox( box )
{
    wxASSERT_MSG( box, wxT("ibStaticBoxSizer needs a static box") );

    // do this so that our Detach() is called if the static box is destroyed
    // before we are
    m_staticBox->SetContainingSizer(this);
}

ibStaticBoxSizer::ibStaticBoxSizer(int orient, wxWindow *win, const wxString& s)
                : wxBoxSizer(orient),
                  m_staticBox(new ibStaticBox(win, wxID_ANY, s))
{
    // same as above
    m_staticBox->SetContainingSizer(this);
}

ibStaticBoxSizer::~ibStaticBoxSizer()
{
    // As an exception to the general rule that sizers own other sizers that
    // they contain but not the windows managed by them, this sizer does own
    // the static box associated with it (which is not very logical but
    // convenient in practice and, most importantly, can't be changed any more
    // because of compatibility). However we definitely should not destroy the
    // children of this static box when we're being destroyed, as this would be
    // unexpected and break the existing code which worked with the windows
    // created as siblings of the static box instead of its children in the
    // previous wxWidgets versions, so ensure they are left alive.

    if ( m_staticBox )
        m_staticBox->WXDestroyWithoutChildren();
}

bool ibStaticBoxSizer::CheckForNonBoxChildren(wxSizer* sizer) const
{
    for ( const wxSizerItem* item: sizer->GetChildren() )
    {
        if ( wxWindow* const win = item->GetWindow() )
        {
            if ( CheckIfNonBoxChild(win) )
                return true;
        }
        else if ( wxSizer* const subsizer = item->GetSizer() )
        {
            if ( CheckForNonBoxChildren(subsizer) )
                return true;
        }
    }

    return false;
}

bool ibStaticBoxSizer::CheckIfNonBoxChild(wxWindow* win) const
{
    if ( m_staticBox->IsDescendant(win) )
        return false;

    // Warn if the window is not a child of the static box, which it really
    // should be, even if we still support using the box parent as parent
    // too for compatibility.
    wxLogDebug("Element %s of ibStaticBoxSizer should be created "
               "as child of its ibStaticBox and not of %s.",
               wxDumpWindow(win),
               wxDumpWindow(win->GetParent()));

    return true;
}

wxSizerItem* ibStaticBoxSizer::DoInsert(size_t index, wxSizerItem* item)
{
    if ( wxWindow* const win = item->GetWindow() )
    {
        // We can check immediately if we have any non-box children and
        // it's better to do it here, for example to allow putting a
        // breakpoint on wxLogDebug() message above and immediately seeing
        // where the item is inserted from, if it's not clear otherwise.
        if ( CheckIfNonBoxChild(win) )
            m_hasNonBoxChildren = true;
    }

    return wxBoxSizer::DoInsert(index, item);
}

void ibStaticBoxSizer::RepositionChildren(const wxSize& minSize)
{
    int top_border, other_border;
    m_staticBox->GetBordersForSizer(&top_border, &other_border);

    m_staticBox->SetSize( m_position.x, m_position.y, m_size.x, m_size.y );

    wxSize old_size( m_size );
    m_size.x -= 2*other_border;
    m_size.y -= top_border + other_border;

    wxPoint old_pos( m_position );

    // If we didn't have any sibling children so far, but we don't have any
    // real children either, chances are that they could have been added, so
    // check for this (but if we do have real children, don't bother doing
    // anything as this would result in extra overhead for every re-layout).
    if ( !m_hasNonBoxChildren && m_staticBox->GetChildren().empty() )
        m_hasNonBoxChildren = CheckForNonBoxChildren(this);

    if ( !m_hasNonBoxChildren )
    {
        // SEAM vs wx: the box is ours on every platform - no GTK wxPizza, no
        // Cocoa content view - so wx's MSW branch is everybody's: if the
        // ibStaticBox has children, then these windows must be placed by the
        // base class version called below using coordinates relative to the
        // top-left corner of the staticbox, keeping in count the borders
        m_position.x = other_border;
        m_position.y = top_border;
    }
    else
    {
        // the windows contained in the staticbox have been created as siblings of the
        // staticbox (this is the "old" way of staticbox contents creation); in this
        // case we need to position them with coordinates relative to our common parent
        m_position.x += other_border;
        m_position.y += top_border;

        // Also check for the non-supported scenario in which both windows
        // using box as their parent and the ones using its parent are used:
        // this won't work correctly because the latter ones won't use the
        // required offset and so won't be laid out correctly, see the code
        // dealing with m_position in RepositionChildren() below.
        if ( !m_staticBox->GetChildren().empty() )
        {
            wxASSERT_MSG
            (
                !m_hasNonBoxChildren,
                "All items of ibStaticBoxSizer must use the same parent.\n"
                "\n"
                "It is strongly recommended to use the associated static box "
                "as the parent for all of them, but if not, all items must "
                "use the containing window as parent.\n"
                "\n"
                "Mixing items using different parents inside the same sizer "
                "is NOT supported."
            );

            // One assert is enough, so reset it to avoid giving it again.
            m_hasNonBoxChildren = false;
        }
    }

    wxBoxSizer::RepositionChildren(minSize);

    m_position = old_pos;
    m_size = old_size;
}

wxSize ibStaticBoxSizer::CalcMin()
{
    int top_border, other_border;
    m_staticBox->GetBordersForSizer(&top_border, &other_border);

    wxSize ret( wxBoxSizer::CalcMin() );
    ret.x += 2*other_border;

    // ensure that we're wide enough to show the static box label (there is no
    // need to check for the static box best size in vertical direction though)
    const int boxWidth = m_staticBox->GetBestSize().x;
    if ( ret.x < boxWidth )
        ret.x = boxWidth;

    ret.y += other_border + top_border;

    return ret;
}

void ibStaticBoxSizer::ShowItems( bool show )
{
    m_staticBox->Show( show );
    wxBoxSizer::ShowItems( show );
}

bool ibStaticBoxSizer::AreAnyItemsShown() const
{
    // We don't need to check the status of our child items: if the box is
    // shown, this sizer should be considered shown even if all its elements
    // are hidden (or, more prosaically, there are no elements at all). And,
    // conversely, if the box is hidden then all our items, which are its
    // children, are hidden too.
    return m_staticBox->IsShown();
}

bool ibStaticBoxSizer::Detach( wxWindowBase *window )
{
    // avoid deleting m_staticBox in our dtor if it's being detached from the
    // sizer (which can happen because it's being already destroyed for
    // example)
    if ( window == m_staticBox )
    {
        m_staticBox = nullptr;
        return true;
    }

    return wxSizer::Detach( window );
}

#if 0
void ibStaticBox::DoSetClientSize(int width, int height)
{
    wxRect rect = GetBorderGeometry();

    ibControl::DoSetClientSize(width + rect.x + rect.width,
                               height + rect.y + rect.height);
}

void ibStaticBox::DoGetClientSize(int *width, int *height) const
{
    ibControl::DoGetClientSize(width, height);

    wxRect rect = GetBorderGeometry();
    if ( width )
        *width -= rect.x + rect.width;
    if ( height )
        *height -= rect.y + rect.height;
}

#endif // 0

#endif // wxUSE_STATBOX

