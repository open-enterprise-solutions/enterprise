// FORKED from the wx GENERIC scroll helper (src/generic/scrlwing.cpp, the part under wxHAS_GENERIC_SCROLLWIN) - see
// scrolwin.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/scrlwing.cpp
// Purpose:     wxScrolledWindow implementation
// Author:      Julian Smart
// Modified by: Vadim Zeitlin on 31.08.00: wxScrollHelper allows to implement.
//              Ron Lee on 10.4.02:  virtual size / auto scrollbars et al.
// Created:     01/02/97
// Copyright:   (c) wxWidgets team
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/scrolwin.h"

// ----------------------------------------------------------------------------
// ibScrollHelper implementation
// ----------------------------------------------------------------------------

ibScrollHelper::ibScrollHelper(wxWindow *winToScroll)
    : wxScrollHelperBase(winToScroll)
{
    m_xVisibility =
    m_yVisibility = wxSHOW_SB_DEFAULT;
    m_adjustScrollFlagReentrancy = 0;
}

void ibScrollHelper::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                                   int noUnitsX, int noUnitsY,
                                   int xPos, int yPos,
                                   bool noRefresh)
{
    wxScrollHelperBase::SetScrollbars(pixelsPerUnitX, pixelsPerUnitY,
                                      noUnitsX, noUnitsY,
                                      xPos, yPos,
                                      noRefresh);

    // the base adjusts the bars itself for another target only (see scrolwin.h)
    if ( m_targetWindow == m_win )
        AdjustScrollbars();
}

bool ibScrollHelper::IsScrollbarShown(int orient) const
{
    wxScrollbarVisibility visibility = orient == wxHORIZONTAL ? m_xVisibility
                                                              : m_yVisibility;

    return visibility != wxSHOW_SB_NEVER;
}

void ibScrollHelper::DoShowScrollbars(wxScrollbarVisibility horz,
                                      wxScrollbarVisibility vert)
{
    if ( horz != m_xVisibility || vert != m_yVisibility )
    {
        m_xVisibility = horz;
        m_yVisibility = vert;

        AdjustScrollbars();
    }
}

void
ibScrollHelper::DoAdjustScrollbar(int orient,
                                  int clientSize,
                                  int virtSize,
                                  int pixelsPerUnit,
                                  int& scrollUnits,
                                  int& scrollPosition,
                                  int& scrollLinesPerPage,
                                  wxScrollbarVisibility visibility)
{
    // scroll lines per page: if 0, no scrolling is needed
    // check if we need scrollbar in this direction at all
    if ( pixelsPerUnit == 0 || clientSize >= virtSize )
    {
        // scrolling is disabled or unnecessary
        scrollUnits =
        scrollPosition = 0;
        scrollLinesPerPage = 0;
    }
    else // might need scrolling
    {
        // Round up integer division to catch any "leftover" client space.
        scrollUnits = (virtSize + pixelsPerUnit - 1) / pixelsPerUnit;

        // Calculate the number of fully scroll units
        scrollLinesPerPage = clientSize / pixelsPerUnit;

        if ( scrollLinesPerPage >= scrollUnits )
        {
            // we're big enough to not need scrolling
            scrollUnits =
            scrollPosition = 0;
            scrollLinesPerPage = 0;
        }
        else // we do need a scrollbar
        {
            if ( scrollLinesPerPage < 1 )
                scrollLinesPerPage = 1;

            // Correct position if greater than extent of canvas minus
            // the visible portion of it or if below zero
            const int posMax = scrollUnits - scrollLinesPerPage;
            if ( scrollPosition > posMax )
                scrollPosition = posMax;
            else if ( scrollPosition < 0 )
                scrollPosition = 0;
        }
    }

    // in wxSHOW_SB_NEVER case don't show the scrollbar even if it's needed, in
    // wxSHOW_SB_ALWAYS case show the scrollbar even if it's not needed by
    // passing a special range value to SetScrollbar()
    int range;
    switch ( visibility )
    {
        case wxSHOW_SB_NEVER:
            range = 0;
            break;

        case wxSHOW_SB_ALWAYS:
            range = scrollUnits ? scrollUnits : -1;
            break;

        default:
            wxFAIL_MSG( wxS("unknown scrollbar visibility") );
            wxFALLTHROUGH;

        case wxSHOW_SB_DEFAULT:
            range = scrollUnits;
            break;

    }

    m_win->SetScrollbar(orient, scrollPosition, scrollLinesPerPage, range);
}

void ibScrollHelper::AdjustScrollbars()
{
    wxRecursionGuard guard(m_adjustScrollFlagReentrancy);
    if ( guard.IsInside() )
    {
        // don't reenter AdjustScrollbars() while another call to
        // AdjustScrollbars() is in progress because this may lead to calling
        // ScrollWindow() twice and this can really happen under MSW if
        // SetScrollbar() call below adds or removes the scrollbar which
        // changes the window size and hence results in another
        // AdjustScrollbars() call
        return;
    }

    int oldXScroll = m_xScrollPosition;
    int oldYScroll = m_yScrollPosition;

    // we may need to readjust the scrollbars several times as enabling one of
    // them reduces the area available for the window contents and so can make
    // the other scrollbar necessary now although it wasn't necessary before
    //
    // VZ: normally this loop should be over in at most 2 iterations, I don't
    //     know why do we need 5 of them
    for ( int iterationCount = 0; iterationCount < 5; iterationCount++ )
    {
        wxSize clientSize = GetTargetSize();
        const wxSize virtSize = m_targetWindow->GetVirtualSize();

        // this block of code tries to work around the following problem: the
        // window could have been just resized to have enough space to show its
        // full contents without the scrollbars, but its client size could be
        // not big enough because it does have the scrollbars right now and so
        // the scrollbars would remain even though we don't need them any more
        //
        // to prevent this from happening, check if we have enough space for
        // everything without the scrollbars and explicitly disable them then
        const wxSize availSize = GetSizeAvailableForScrollTarget(
            m_win->GetSize() - m_win->GetWindowBorderSize());
        if ( availSize != clientSize )
        {
            if ( availSize.x >= virtSize.x && availSize.y >= virtSize.y )
            {
                // this will be enough to make the scrollbars disappear below
                // and then the client size will indeed become equal to the
                // full available size
                clientSize = availSize;
            }
        }


        DoAdjustScrollbar(wxHORIZONTAL,
                          clientSize.x,
                          virtSize.x,
                          m_xScrollPixelsPerLine,
                          m_xScrollLines,
                          m_xScrollPosition,
                          m_xScrollLinesPerPage,
                          m_xVisibility);

        DoAdjustScrollbar(wxVERTICAL,
                          clientSize.y,
                          virtSize.y,
                          m_yScrollPixelsPerLine,
                          m_yScrollLines,
                          m_yScrollPosition,
                          m_yScrollLinesPerPage,
                          m_yVisibility);


        // If a scrollbar (dis)appeared as a result of this, we need to adjust
        // them again but if the client size didn't change, then we're done
        if ( GetTargetSize() == clientSize )
            break;
    }

    if (oldXScroll != m_xScrollPosition)
    {
       if (m_xScrollingEnabled)
            m_targetWindow->ScrollWindow( m_xScrollPixelsPerLine * (oldXScroll - m_xScrollPosition), 0,
                                          GetScrollRect() );
       else
            m_targetWindow->Refresh(true, GetScrollRect());
    }

    if (oldYScroll != m_yScrollPosition)
    {
        if (m_yScrollingEnabled)
            m_targetWindow->ScrollWindow( 0, m_yScrollPixelsPerLine * (oldYScroll-m_yScrollPosition),
                                          GetScrollRect() );
        else
            m_targetWindow->Refresh(true, GetScrollRect());
    }
}

void ibScrollHelper::DoScroll( int x_pos, int y_pos )
{
    if (!m_targetWindow)
        return;

    if (((x_pos == -1) || (x_pos == m_xScrollPosition)) &&
        ((y_pos == -1) || (y_pos == m_yScrollPosition))) return;

    int w = 0, h = 0;
    GetTargetSize(&w, &h);

    // compute new position:
    int new_x = m_xScrollPosition;
    int new_y = m_yScrollPosition;

    if ((x_pos != -1) && (m_xScrollPixelsPerLine))
    {
        new_x = x_pos;

        // Calculate page size i.e. number of scroll units you get on the
        // current client window
        int noPagePositions = w/m_xScrollPixelsPerLine;
        if (noPagePositions < 1) noPagePositions = 1;

        // Correct position if greater than extent of canvas minus
        // the visible portion of it or if below zero
        new_x = wxMin( m_xScrollLines-noPagePositions, new_x );
        new_x = wxMax( 0, new_x );
    }
    if ((y_pos != -1) && (m_yScrollPixelsPerLine))
    {
        new_y = y_pos;

        // Calculate page size i.e. number of scroll units you get on the
        // current client window
        int noPagePositions = h/m_yScrollPixelsPerLine;
        if (noPagePositions < 1) noPagePositions = 1;

        // Correct position if greater than extent of canvas minus
        // the visible portion of it or if below zero
        new_y = wxMin( m_yScrollLines-noPagePositions, new_y );
        new_y = wxMax( 0, new_y );
    }

    if ( new_x == m_xScrollPosition && new_y == m_yScrollPosition )
        return; // nothing to do, the position didn't change

    const bool scrollX = m_xScrollPosition != new_x;
    const bool scrollY = m_yScrollPosition != new_y;
    const bool scrollWindow =
        (scrollX && m_xScrollingEnabled) ||
        (scrollY && m_yScrollingEnabled);

    // flush all pending repaints before we change m_{x,y}ScrollPosition, as
    // otherwise invalidated area could be updated incorrectly later when
    // ScrollWindow() makes sure they're repainted before scrolling them
    if ( scrollWindow )
        m_targetWindow->Update();

    bool needsRefresh = false;

    // update the position and scroll the window now:
    if ( scrollX )
    {
        int old_x = m_xScrollPosition;
        m_xScrollPosition = new_x;
        m_win->SetScrollPos( wxHORIZONTAL, new_x );
        if ( m_xScrollingEnabled )
        {
            m_targetWindow->ScrollWindow( (old_x-new_x)*m_xScrollPixelsPerLine, 0,
                                          GetScrollRect() );
        }
        else
        {
            needsRefresh = true;
        }
    }

    if ( scrollY )
    {
        int old_y = m_yScrollPosition;
        m_yScrollPosition = new_y;
        m_win->SetScrollPos( wxVERTICAL, new_y );
        if ( m_yScrollingEnabled )
        {
            m_targetWindow->ScrollWindow( 0, (old_y-new_y)*m_yScrollPixelsPerLine,
                                          GetScrollRect() );
        }
        else
        {
            needsRefresh = true;
        }
    }

    if ( needsRefresh )
        m_targetWindow->Refresh(true, GetScrollRect());
}
