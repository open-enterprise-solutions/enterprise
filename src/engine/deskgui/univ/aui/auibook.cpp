// FORKED from wxWidgets' AUI (src/aui/auibook.cpp of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/aui/auibook.cpp
// Purpose:     wxaui: wx advanced user interface - notebook
// Author:      Benjamin I. Williams
// Modified by: Jens Lody
// Created:     2006-06-28
// Copyright:   (C) Copyright 2006, Kirix Corporation, All Rights Reserved
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"



#include "deskgui/univ/aui/auibook.h"

#ifndef WX_PRECOMP
    #include "wx/settings.h"
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/frame.h"
#endif

#include "deskgui/univ/aui/serializer.h"
#include "deskgui/univ/aui/tabmdi.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/menu.h"
#include "wx/wupdlock.h"

#include "wx/dcbuffer.h" // just for wxALWAYS_NATIVE_DOUBLE_BUFFER

#include <algorithm>
#include <stack>
#include <unordered_set>

wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_PAGE_CLOSE, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_PAGE_CLOSED, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_PAGE_CHANGING, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_PAGE_CHANGED, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_BUTTON, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_BEGIN_DRAG, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_END_DRAG, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_CANCEL_DRAG, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_DRAG_MOTION, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_ALLOW_DND, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_BG_DCLICK, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_DRAG_DONE, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_TAB_RIGHT_UP, ibAuiNotebookEvent);
wxDEFINE_EVENT(ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, ibAuiNotebookEvent);

wxIMPLEMENT_CLASS(ibAuiNotebook, wxBookCtrlBase);
wxIMPLEMENT_CLASS(ibAuiTabCtrl, ibControl);
wxIMPLEMENT_DYNAMIC_CLASS(ibAuiNotebookEvent, wxBookCtrlEvent);


// Local helper functions
namespace
{

// Build the vector of buttons for the page depending on the notebook flags.
std::vector<ibAuiTabContainerButton> MakePageButtons(unsigned int flags)
{
    std::vector<ibAuiTabContainerButton> buttons;

    // Pin/unpin button can be only shown if one of the styles enabling it is
    // used and depends on the current tab state, so make it hidden by default.
    if (flags & (ibAUI_NB_PIN_ON_ACTIVE_TAB | ibAUI_NB_UNPIN_ON_ALL_PINNED))
        buttons.push_back({ibAUI_BUTTON_PIN, wxRIGHT, ibAUI_BUTTON_STATE_HIDDEN});

    // Close button is hidden by default, it will be shown depending on the
    // exact style used and, for ibAUI_NB_CLOSE_ON_ACTIVE_TAB, on whether the
    // tab is current.
    if (flags & (ibAUI_NB_CLOSE_ON_ALL_TABS | ibAUI_NB_CLOSE_ON_ACTIVE_TAB))
        buttons.push_back({ibAUI_BUTTON_CLOSE, wxRIGHT, ibAUI_BUTTON_STATE_HIDDEN});

    return buttons;
}

} // anonymous namespace


// ibAuiTabEventSource
//
// This class is the "attorney" in "attorney-client" idiom used to allow
// ibAuiTabCtrl to call some (but not all) ibAuiNotebook private functions.
class ibAuiTabEventSource
{
private:
    static void TabClicked(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabClicked(ctrl, tabIdx);
    }

    static void TabBeginDrag(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabBeginDrag(ctrl, tabIdx);
    }

    static void TabDragMotion(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabDragMotion(ctrl, tabIdx);
    }

    static void TabEndDrag(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabEndDrag(ctrl, tabIdx);
    }

    static void TabCancelDrag(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabCancelDrag(ctrl, tabIdx);
    }

    static bool TabButton(ibAuiTabCtrl* ctrl, int tabIdx, int button_id)
    {
        return ctrl->GetBook()->OnTabButton(ctrl, tabIdx, button_id);
    }

    static void TabMiddleDown(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabMiddleDown(ctrl, tabIdx);
    }

    static void TabMiddleUp(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabMiddleUp(ctrl, tabIdx);
    }

    static void TabRightDown(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabRightDown(ctrl, tabIdx);
    }

    static void TabRightUp(ibAuiTabCtrl* ctrl, int tabIdx)
    {
        ctrl->GetBook()->OnTabRightUp(ctrl, tabIdx);
    }

    static void TabBgDClick(ibAuiTabCtrl* ctrl)
    {
        ctrl->GetBook()->OnTabBgDClick(ctrl);
    }

    friend class ibAuiTabCtrl;
};

// -- ibAuiTabContainer class implementation --


// ibAuiTabContainer is a class which contains information about each
// tab.  It also can render an entire tab control to a specified DC.
// It's not a window class itself, because this code will be used by
// the ibAuiManager, where it is disadvantageous to have separate
// windows for each tab control in the case of "docked tabs"

// A derived class, ibAuiTabCtrl, is an actual wxWindow-derived window
// which can be used as a tab control in the normal sense.


ibAuiTabContainer::ibAuiTabContainer()
{
    m_tabOffset = 0;
    m_flags = 0;

    m_normalFont = *wxNORMAL_FONT;
    m_selectedFont = *wxNORMAL_FONT;
    m_selectedFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_measuringFont = m_selectedFont;
    m_fixedTabWidth = wxWindow::FromDIP(100, nullptr);

    AddButton(ibAUI_BUTTON_LEFT, wxLEFT);
    AddButton(ibAUI_BUTTON_RIGHT, wxRIGHT);
    AddButton(ibAUI_BUTTON_WINDOWLIST, wxRIGHT);
    AddButton(ibAUI_BUTTON_CLOSE, wxRIGHT);
}

ibAuiTabContainer::~ibAuiTabContainer() = default;

void ibAuiTabContainer::SetFlags(unsigned int flags)
{
    // check for new close button settings
    RemoveButton(ibAUI_BUTTON_LEFT);
    RemoveButton(ibAUI_BUTTON_RIGHT);
    RemoveButton(ibAUI_BUTTON_WINDOWLIST);
    RemoveButton(ibAUI_BUTTON_CLOSE);


    if ((flags & ibAUI_NB_SCROLL_BUTTONS) && !(flags & ibAUI_NB_MULTILINE))
    {
        AddButton(ibAUI_BUTTON_LEFT, wxLEFT);
        AddButton(ibAUI_BUTTON_RIGHT, wxRIGHT);
    }

    if (flags & ibAUI_NB_WINDOWLIST_BUTTON)
    {
        AddButton(ibAUI_BUTTON_WINDOWLIST, wxRIGHT);
    }

    if (flags & ibAUI_NB_CLOSE_BUTTON)
    {
        AddButton(ibAUI_BUTTON_CLOSE, wxRIGHT);
    }

    // Also synchronize the tabs buttons with the new settings if any of the
    // flags affecting them changed.
    const auto flagsAffectingButtons =
        ibAUI_NB_PIN_ON_ACTIVE_TAB |
        ibAUI_NB_UNPIN_ON_ALL_PINNED |
        ibAUI_NB_CLOSE_ON_ALL_TABS |
        ibAUI_NB_CLOSE_ON_ACTIVE_TAB;
    if ((m_flags & flagsAffectingButtons) != (flags & flagsAffectingButtons))
    {
        for (auto& page : m_pages)
        {
            page.buttons = MakePageButtons(flags);
        }
    }

    m_flags = flags;
}

void ibAuiTabContainer::SetNormalFont(const wxFont& font)
{
    m_normalFont = font;
}

void ibAuiTabContainer::SetSelectedFont(const wxFont& font)
{
    m_selectedFont = font;
}

void ibAuiTabContainer::SetMeasuringFont(const wxFont& font)
{
    m_measuringFont = font;
}

void ibAuiTabContainer::SetRect(const wxRect& rect, wxWindow* wnd)
{
    m_rect = rect;

    SetSizingInfo(rect.GetSize(), m_pages.GetCount(), wnd);
}

void ibAuiTabContainer::SetRowHeight(int rowHeight)
{
    m_tabRowHeight = rowHeight;
}

bool ibAuiTabContainer::AddPage(const ibAuiNotebookPage& info)
{
    return InsertPage(info, m_pages.GetCount());
}

bool ibAuiTabContainer::InsertPage(const ibAuiNotebookPage& info,
                                   size_t idx)
{
    const auto it = m_pages.insert(m_pages.begin() + idx, info);

    it->buttons = MakePageButtons(m_flags);

    // a fixed width of a tab follows how many there are
    SetSizingInfo(m_rect.GetSize(), m_pages.GetCount(), info.window);

    return true;
}

bool ibAuiTabContainer::MovePage(wxWindow* page,
                                 size_t new_idx)
{
    int idx = GetIdxFromWindow(page);
    if (idx == -1)
        return false;

    return MovePage(static_cast<size_t>(idx), new_idx);
}

bool ibAuiTabContainer::MovePage(size_t old_idx, size_t new_idx)
{
    const auto b = m_pages.begin();
    if (old_idx < new_idx)
        std::rotate(b + old_idx, b + old_idx + 1, b + new_idx + 1);
    else if (old_idx > new_idx)
        std::rotate(b + new_idx, b + old_idx, b + old_idx + 1);
    else // nothing to do
        return false;

    return true;
}

bool ibAuiTabContainer::RemovePage(wxWindow* page)
{
    int idx = GetIdxFromWindow(page);
    if (idx == -1)
        return false;

    RemovePageAt(idx);

    return true;
}

void ibAuiTabContainer::RemovePageAt(size_t idx)
{
    wxCHECK_RET( idx < m_pages.GetCount(), "invalid page index" );

    wxWindow* const wnd = m_pages[idx].window;

    m_pages.RemoveAt(idx);

    // a fixed width of a tab follows how many there are
    SetSizingInfo(m_rect.GetSize(), m_pages.GetCount(), wnd);
}

void ibAuiTabContainer::RemoveAll()
{
    m_pages.Clear();
}

bool ibAuiTabContainer::SetActivePage(const wxWindow* wnd)
{
    bool found = false;

    for ( auto& page : m_pages )
    {
        if (page.window == wnd)
        {
            page.active = true;
            found = true;
        }
        else
        {
            page.active = false;
        }
    }

    return found;
}

void ibAuiTabContainer::SetNoneActive()
{
    for ( auto& page : m_pages )
    {
        page.active = false;
    }
}

bool ibAuiTabContainer::SetActivePage(size_t page)
{
    if (page >= m_pages.GetCount())
        return false;

    return SetActivePage(m_pages.Item(page).window);
}

int ibAuiTabContainer::GetActivePage() const
{
    size_t i, page_count = m_pages.GetCount();
    for (i = 0; i < page_count; ++i)
    {
        ibAuiNotebookPage& page = m_pages.Item(i);
        if (page.active)
            return i;
    }

    return -1;
}

wxWindow* ibAuiTabContainer::GetWindowFromIdx(size_t idx) const
{
    if (idx >= m_pages.GetCount())
        return nullptr;

    return m_pages[idx].window;
}

int ibAuiTabContainer::GetIdxFromWindow(const wxWindow* wnd) const
{
    const size_t page_count = m_pages.GetCount();
    for ( size_t i = 0; i < page_count; ++i )
    {
        ibAuiNotebookPage& page = m_pages.Item(i);
        if (page.window == wnd)
            return i;
    }
    return wxNOT_FOUND;
}

ibAuiNotebookPage& ibAuiTabContainer::GetPage(size_t idx)
{
    wxASSERT_MSG(idx < m_pages.GetCount(), wxT("Invalid Page index"));

    return m_pages[idx];
}

const ibAuiNotebookPage& ibAuiTabContainer::GetPage(size_t idx) const
{
    wxASSERT_MSG(idx < m_pages.GetCount(), wxT("Invalid Page index"));

    return m_pages[idx];
}

const ibAuiNotebookPageArray& ibAuiTabContainer::GetPages() const
{
    return m_pages;
}

size_t ibAuiTabContainer::GetPageCount() const
{
    return m_pages.GetCount();
}

void ibAuiTabContainer::AddButton(int id,
                                  int location,
                                  const wxBitmapBundle& WXUNUSED(normalBitmap),
                                  const wxBitmapBundle& WXUNUSED(disabledBitmap))
{
    // We ignore the bitmaps as they are never used currently.
    m_buttons.push_back({id, location});
}

void ibAuiTabContainer::RemoveButton(int id)
{
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it)
    {
        if (it->id == id)
        {
            m_buttons.erase(it);
            return;
        }
    }
}



size_t ibAuiTabContainer::GetTabOffset() const
{
    return m_tabOffset;
}

void ibAuiTabContainer::SetTabOffset(size_t offset)
{
    // It doesn't make sense to set the offset when multiple lines of tabs are
    // used as it is not used in this case.
    wxASSERT( !IsFlagSet(ibAUI_NB_MULTILINE) );

    m_tabOffset = offset;
}


void
ibAuiTabContainer::UpdateButtonsState(ibAuiNotebookPage& page, bool forceActive)
{
    const bool isActive = forceActive || page.active;
    for (auto& button : page.buttons)
    {
        bool shown = false;
        switch ( page.kind )
        {
            case ibAuiTabKind::Normal:
            case ibAuiTabKind::Pinned:
                switch ( button.id )
                {
                    case ibAUI_BUTTON_CLOSE:
                        if (IsFlagSet(ibAUI_NB_CLOSE_ON_ALL_TABS))
                        {
                            shown = true;
                        }
                        else if (IsFlagSet(ibAUI_NB_CLOSE_ON_ACTIVE_TAB))
                        {
                            if (isActive)
                                shown = true;
                        }
                        break;

                    case ibAUI_BUTTON_PIN:
                        if (IsFlagSet(ibAUI_NB_PIN_ON_ACTIVE_TAB))
                        {
                            if (isActive)
                            {
                                shown = true;

                                if ( page.kind == ibAuiTabKind::Pinned )
                                    button.curState |= ibAUI_BUTTON_STATE_CHECKED;
                                else
                                    button.curState &= ~ibAUI_BUTTON_STATE_CHECKED;
                            }
                        }

                        // This is not an "else" of the above "if" as when both
                        // styles are used, we show "pin" button only on the
                        // active tab, but also show "unpin" on all the pinned
                        // tabs.
                        if (IsFlagSet(ibAUI_NB_UNPIN_ON_ALL_PINNED))
                        {
                            if ( page.kind == ibAuiTabKind::Pinned )
                            {
                                shown = true;

                                button.curState |= ibAUI_BUTTON_STATE_CHECKED;
                            }
                        }
                        break;
                }
                break;

            case ibAuiTabKind::Locked:
                // A locked page can't be closed or pinned, leave the button
                // in its default hidden state.
                break;
        }

        // Leave the other flags unchanged, this doesn't matter when hiding
        // the button but does when showing it as it could be pressed or
        // under the mouse.
        if ( shown )
            button.curState &= ~ibAUI_BUTTON_STATE_HIDDEN;
        else
            button.curState |= ibAUI_BUTTON_STATE_HIDDEN;
    }
}

// Combined border between the tabs and the buttons in DIPs.
static const int ibAUI_BUTTONS_BORDER = 2;

// The square of a button, the row's or a tab's, in DIPs: its glyph is the theme's (ibRenderer::DrawStripButton).
static const int ibAUI_BUTTON_SIZE = 16;

// The room before the first tab where no button stands, in DIPs.
static const int ibAUI_TABS_INDENT = 5;

// The widest label kept in one line, in DIPs: a longer one goes in two (ibAuiTabLabelLines).
static const int ibAUI_TAB_LABEL_WIDTH = 220;

// THE LINES OF A TAB'S LABEL - one while it fits the width, two of about half its length when it does not: a
// document's caption is long (its kind, its number, its date), and in one line its tab took the row (2026-11-11, Max:
// "it is very wide"; "half of its length, not a fixed width"). Broken at the space that leaves the wider of the two
// lines the narrowest, nothing cut - the tab is as wide as that line; a label with no space stays in one line
static wxArrayString ibAuiTabLabelLines(const wxReadOnlyDC& dc, const wxString& caption, int maxWidth)
{
    wxArrayString lines;
    size_t breakAt = wxString::npos;
    int narrowest = dc.GetTextExtent(caption).x;
    if ( narrowest > maxWidth )
    {
        for ( size_t space = caption.find(wxT(' ')); space != wxString::npos; space = caption.find(wxT(' '), space + 1) )
        {
            const int wider = wxMax(dc.GetTextExtent(caption.Left(space)).x, dc.GetTextExtent(caption.Mid(space + 1)).x);
            if ( wider < narrowest )
            {
                narrowest = wider;
                breakAt = space;
            }
        }
    }

    if ( breakAt == wxString::npos )
    {
        lines.Add(caption);
        return lines;
    }

    lines.Add(caption.Left(breakAt));
    lines.Add(caption.Mid(breakAt + 1));
    return lines;
}

void ibAuiTabContainer::SetSizingInfo(const wxSize& tabCtrlSize, size_t tabCount, wxWindow* wnd)
{
    int totalWidth = tabCtrlSize.x - wxWindow::FromDIP(ibAUI_TABS_INDENT, wnd) - wxWindow::FromDIP(4, wnd);

    if (IsFlagSet(ibAUI_NB_CLOSE_BUTTON))
        totalWidth -= wxWindow::FromDIP(ibAUI_BUTTON_SIZE, wnd);
    if (IsFlagSet(ibAUI_NB_WINDOWLIST_BUTTON))
        totalWidth -= wxWindow::FromDIP(ibAUI_BUTTON_SIZE, wnd);

    m_fixedTabWidth = wxWindow::FromDIP(100, wnd);
    if (tabCount > 0)
        m_fixedTabWidth = totalWidth / (int)tabCount;

    m_fixedTabWidth = wxMax(m_fixedTabWidth, wxWindow::FromDIP(100, wnd));

    if (m_fixedTabWidth > totalWidth / 2)
        m_fixedTabWidth = totalWidth / 2;

    m_fixedTabWidth = wxMin(m_fixedTabWidth, wxWindow::FromDIP(220, wnd));
}

wxSize ibAuiTabContainer::GetTabSize(wxReadOnlyDC& dc,
                                     wxWindow* wnd,
                                     const wxString& caption,
                                     const wxBitmapBundle& bitmap,
                                     int closeButtonState,
                                     int* xExtent) const
{
    dc.SetFont(m_measuringFont);

    // the widest of the label's lines (ibAuiTabLabelLines); the height of any label in as many lines, not of this one:
    // one row for a short caption and a tall one
    const wxArrayString lines = ibAuiTabLabelLines(dc, caption, wnd->FromDIP(ibAUI_TAB_LABEL_WIDTH));
    int width = 0;
    for ( const wxString& line : lines )
        width = wxMax(width, dc.GetTextExtent(line).x);
    int height = dc.GetTextExtent(wxT("ABCDEFXj")).y * static_cast<int>(lines.size());

    // the close button and its padding
    if (!(closeButtonState & ibAUI_BUTTON_STATE_HIDDEN))
        width += wnd->FromDIP(ibAUI_BUTTON_SIZE) + wnd->FromDIP(3);

    // the picture, in logical dimensions of this window, and its padding on the right
    if (bitmap.IsOk())
    {
        const wxSize bitmapSize = bitmap.GetPreferredLogicalSizeFor(wnd);
        width += bitmapSize.x + wnd->FromDIP(3);
        height = wxMax(height, bitmapSize.y);
    }

    const wxSize padding = wnd->FromDIP(wxSize(16, 10));
    width += padding.x;
    height += padding.y;

    if (IsFlagSet(ibAUI_NB_TAB_FIXED_WIDTH))
        width = m_fixedTabWidth;

    if (xExtent)
        *xExtent = width;

    return wxSize(width, height);
}

wxSize ibAuiTabContainer::GetPageTabSize(wxReadOnlyDC& dc,
                                         wxWindow* wnd,
                                         const ibAuiNotebookPage& page,
                                         int* xExtent) const
{
    // the close button is the only one a tab shows (SEAM vs wx: a pin is never drawn)
    int closeState = ibAUI_BUTTON_STATE_HIDDEN;
    for ( const auto& button : page.buttons )
    {
        if ( button.id == ibAUI_BUTTON_CLOSE )
            closeState = button.curState;
    }

    return GetTabSize(dc, wnd, page.caption, page.bitmap, closeState, xExtent);
}

int ibAuiTabContainer::GetBestTabCtrlSize(wxWindow* wnd, const wxSize& requiredBmpSize) const
{
    wxInfoDC dc(wnd);

    // a picture size given is every tab's: the row does not grow as a tab with a picture is added among ones without
    wxBitmapBundle requiredBmp;
    if (requiredBmpSize.IsFullySpecified())
        requiredBmp = wxBitmap(requiredBmpSize.x, requiredBmpSize.y);

    // by the lines of their labels, not by their letters (GetTabSize): one height for a short caption and a tall one,
    // a line more while a tab's label takes two
    int maxHeight = 0;
    for ( const auto& page : m_pages )
    {
        const wxSize size = GetTabSize(dc, wnd, page.caption, requiredBmp.IsOk() ? requiredBmp : page.bitmap,
                                       ibAUI_BUTTON_STATE_HIDDEN);
        maxHeight = wxMax(maxHeight, size.y);
    }

    return maxHeight + 2;
}

int ibAuiTabContainer::GetAvailableForTabs(const wxRect& rect,
                                           wxWindow* wnd)
{
    /*
        Note that this function always ignores LEFT/RIGHT buttons because
        either it's called from LayoutMultiLineTabs() in which case the buttons
        are never used (tabs are multiline XOR scrollable) or it is called from
        RenderButtons() to determine the maximum available width for the tabs,
        which corresponds to the situation when these buttons are hidden.
     */

    size_t i;
    const size_t button_count = m_buttons.size();

    int right_buttons_width = 0;

    // measure the buttons on the right side
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxRIGHT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;
        if (button.id == ibAUI_BUTTON_RIGHT) // See the block comment above.
            continue;

        right_buttons_width += wnd->FromDIP(ibAUI_BUTTON_SIZE);
    }


    int left_buttons_width = 0;

    // measure the buttons on the left side

    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxLEFT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;
        if (button.id == ibAUI_BUTTON_LEFT) // See the block comment above.
            continue;

        left_buttons_width += wnd->FromDIP(ibAUI_BUTTON_SIZE);
    }

    if (left_buttons_width == 0)
        left_buttons_width = wnd->FromDIP(ibAUI_TABS_INDENT);

    return rect.width - left_buttons_width - right_buttons_width - wnd->FromDIP(ibAUI_BUTTONS_BORDER);
}

int ibAuiTabContainer::LayoutMultiLineTabs(const wxRect& rect, wxWindow* wnd)
{
    wxInfoDC dc(wnd);

    const int availableWidth = GetAvailableForTabs(rect, wnd);

    int extraHeight = 0;

    int widthRow = 0;
    bool firstTabInRow = true;
    bool* lastRowEnd = nullptr; // Pointer to the flag in the last tab.
    for ( auto& page : m_pages )
    {
        // When using ibAUI_NB_{CLOSE,PIN}_ON_ACTIVE_TAB, we need to reserve
        // enough space in each row to show the corresponding button on the
        // active tab if it's in this row to avoid redoing the layout when the
        // selection changes (as this would be very confusing). So we
        // always measure the first tab in the row as if it had the button(s).
        // This relies on the extra increment in the width being constant for
        // all tabs, but this seems to be a safe enough assumption.
        //
        // Note that for non-ibAUI_NB_CLOSE_ON_ACTIVE_TAB cases, it doesn't
        // matter, as "Close" is either never shown at all or always shown
        // regardless of the page active status.
        //
        // Also note that we don't need to update the buttons state for the
        // locked pages as they never change, so the layout doesn't depend on
        // whether they're active or not and if the entire row consists of only
        // locked tabs, we never have to add any extra space.
        if ( page.kind != ibAuiTabKind::Locked )
        {
            UpdateButtonsState(page, firstTabInRow);
            firstTabInRow = false;
        }

        const auto size = GetPageTabSize(dc, wnd, page);

        widthRow += size.x;

        // Reset it by default, it will be set during the next loop
        // iteration or after the loop if it's the last tab in the row.
        page.rowEnd = false;

        // Start a new row if this tab doesn't fit into the current one.
        if ( widthRow > availableWidth )
        {
            widthRow = size.x;

            // We assume that all tabs have the same height: if they don't,
            // things are not going to look well no matter what.
            extraHeight += size.y;

            firstTabInRow = true;

            if ( lastRowEnd )
                *lastRowEnd = true;
        }

        lastRowEnd = &page.rowEnd;
    }

    if ( lastRowEnd )
        *lastRowEnd = true;

    return extraHeight;
}

void ibAuiTabContainer::RenderButtons(wxDC& dc, wxWindow* wnd,
                                      int& left_buttons_width,
                                      int& right_buttons_width)
{
    size_t i;
    const size_t page_count = m_pages.GetCount();
    const size_t button_count = m_buttons.size();

    // ensure we show as many tabs as possible
    while (m_tabOffset > 0 && IsTabVisible(page_count-1, m_tabOffset-1, &dc, wnd))
        --m_tabOffset;

    // find out if size of tabs is larger than can be
    // afforded on screen
    int total_width = 0;
    int visible_width = 0;
    for (i = 0; i < page_count; ++i)
    {
        ibAuiNotebookPage& page = m_pages.Item(i);

        UpdateButtonsState(page);

        int x_extent = 0;
        const wxSize size = GetPageTabSize(dc, wnd, page, &x_extent);

        if (i+1 < page_count)
            total_width += x_extent;
        else
            total_width += size.x;

        if (i >= m_tabOffset)
        {
            if (i+1 < page_count)
                visible_width += x_extent;
            else
                visible_width += size.x;
        }
    }

    const int availableWidth = GetAvailableForTabs(m_rect, wnd);

    if (total_width > availableWidth || m_tabOffset != 0)
    {
        // show left/right buttons
        for (i = 0; i < button_count; ++i)
        {
            ibAuiTabContainerButton& button = m_buttons.at(i);
            if (button.id == ibAUI_BUTTON_LEFT ||
                button.id == ibAUI_BUTTON_RIGHT)
            {
                button.curState &= ~ibAUI_BUTTON_STATE_HIDDEN;
            }
        }
    }
    else
    {
        // hide left/right buttons
        for (i = 0; i < button_count; ++i)
        {
            ibAuiTabContainerButton& button = m_buttons.at(i);
            if (button.id == ibAUI_BUTTON_LEFT ||
                button.id == ibAUI_BUTTON_RIGHT)
            {
                button.curState |= ibAUI_BUTTON_STATE_HIDDEN;
            }
        }
    }

    // determine whether various buttons should be enabled
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(i);
        if (button.id == ibAUI_BUTTON_LEFT)
        {
            if (m_tabOffset == 0)
                button.curState |= ibAUI_BUTTON_STATE_DISABLED;
            else
                button.curState &= ~ibAUI_BUTTON_STATE_DISABLED;
        }
        else if (button.id == ibAUI_BUTTON_RIGHT)
        {
            int button_width = 0;
            for ( const auto& b : m_buttons )
                button_width += b.rect.GetWidth();

            if (visible_width < m_rect.GetWidth() - button_width)
                button.curState |= ibAUI_BUTTON_STATE_DISABLED;
            else
                button.curState &= ~ibAUI_BUTTON_STATE_DISABLED;
        }
        else if (button.id == ibAUI_BUTTON_CLOSE)
        {
            button.curState &= ~ibAUI_BUTTON_STATE_DISABLED;

            // Disable "Close" button if the current page is locked, as such
            // pages can't be closed.
            for (const auto& page : m_pages)
            {
                if (page.active)
                {
                    switch ( page.kind )
                    {
                        case ibAuiTabKind::Normal:
                        case ibAuiTabKind::Pinned:
                            break;

                        case ibAuiTabKind::Locked:
                            button.curState |= ibAUI_BUTTON_STATE_DISABLED;
                            break;
                    }
                    break;
                }
            }
        }
    }

    right_buttons_width = 0;

    // draw the buttons on the right side
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxRIGHT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;

        wxRect button_rect = m_rect;
        button_rect.SetY(1);
        button_rect.SetWidth(m_rect.width - right_buttons_width);

        DrawButton(dc,
                   wnd,
                   button_rect,
                   button.id,
                   button.curState,
                   wxRIGHT,
                   &button.rect);

        right_buttons_width += button.rect.GetWidth();
    }



    left_buttons_width = 0;

    // draw the buttons on the left side

    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxLEFT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;

        wxRect button_rect(left_buttons_width, 1, 1000, m_rect.height);

        DrawButton(dc,
                   wnd,
                   button_rect,
                   button.id,
                   button.curState,
                   wxLEFT,
                   &button.rect);

        left_buttons_width += button.rect.GetWidth();
    }

    if (left_buttons_width == 0)
        left_buttons_width = wnd->FromDIP(ibAUI_TABS_INDENT);


    // update the tab buttons visibility
    // make sure tab button entries which aren't used are marked as hidden
    for (i = page_count; i < m_pages.GetCount(); ++i)
    {
        for (auto& button : m_pages[i].buttons)
            button.curState |= ibAUI_BUTTON_STATE_HIDDEN;
    }

    // buttons before the tab offset must be set to hidden
    for (i = 0; i < m_tabOffset; ++i)
    {
        for (auto& button : m_pages[i].buttons)
            button.curState |= ibAUI_BUTTON_STATE_HIDDEN;
    }
}

void ibAuiTabContainer::DrawButton(wxDC& dc,
                                   wxWindow* wnd,
                                   const wxRect& inRect,
                                   int bitmapId,
                                   int buttonState,
                                   int orientation,
                                   wxRect* outRect)
{
    // at the start or the end of the room given, in the middle of it down
    const int size = wnd->FromDIP(ibAUI_BUTTON_SIZE);
    *outRect = wxRect(orientation == wxLEFT ? inRect.x : inRect.x + inRect.width - size,
                      (inRect.y + inRect.height) / 2 - size / 2,
                      size,
                      size);

    ibThemeEngine::Get()->GetRenderer()->DrawStripButton(dc, *outRect, bitmapId, ibAuiGetControlFlags(buttonState));
}

int ibAuiTabContainer::DrawPageTab(wxDC& dc, wxWindow* wnd, ibAuiNotebookPage& page, const wxRect& inRect)
{
    ibRenderer* const renderer = ibThemeEngine::Get()->GetRenderer();
    const bool bottom = IsFlagSet(ibAUI_NB_BOTTOM);

    // the close button is the only one a tab shows (SEAM vs wx: a pin is never drawn)
    ibAuiTabContainerButton* closeButton = nullptr;
    for ( auto& button : page.buttons )
    {
        if ( button.id == ibAUI_BUTTON_CLOSE )
            closeButton = &button;
    }
    const int closeState = closeButton ? closeButton->curState : ibAUI_BUTTON_STATE_HIDDEN;

    int tabWidth = 0;
    GetPageTabSize(dc, wnd, page, &tabWidth);

    // the tab stands on the row's foot, 3 pixels under its top, cut at its end
    const wxRect tabRect(inRect.x, inRect.y + 3, tabWidth, inRect.height - 3);
    const int clipWidth = wxMin(tabRect.width, inRect.GetRight() + 1 - tabRect.x);
    wxDCClipper clip(dc, wxRect(tabRect.x, tabRect.y, clipWidth + 1, tabRect.height - 3));

    renderer->DrawStripTab(dc, tabRect, bottom ? wxBOTTOM : wxTOP, page.active ? wxCONTROL_SELECTED : 0);

    // the picture and the label are centred down on the face: off the strip's top (the row over the pages), off the
    // shelf (under them)
    const int middle = bottom ? tabRect.y + (tabRect.height - 6 + 1) / 2 : tabRect.y + 2 + (tabRect.height - 6) / 2;

    wxRect bitmapRect;
    int textX = tabRect.x + wnd->FromDIP(8);
    if (page.bitmap.IsOk())
    {
        const wxBitmap bitmap = page.bitmap.GetBitmapFor(wnd);
        const wxSize bitmapSize = bitmap.GetLogicalSize();
        bitmapRect = wxRect(wxPoint(textX, middle - bitmapSize.y / 2), bitmapSize);
        dc.DrawBitmap(bitmap, bitmapRect.GetPosition(), true);

        textX = bitmapRect.GetRight() + 1 + wnd->FromDIP(3);
    }

    // the close button at the end, flat until the mouse is over it; it is clicked down the tab's height
    int closeWidth = 0;
    if (closeButton && !(closeState & ibAUI_BUTTON_STATE_HIDDEN))
    {
        const int size = wnd->FromDIP(ibAUI_BUTTON_SIZE);
        const wxRect glyph(tabRect.GetRight() + 1 - size - wnd->FromDIP(1),
                           (bottom ? 1 : tabRect.y - 1) + tabRect.height / 2 - size / 2, size, size);
        renderer->DrawStripButton(dc, glyph, ibAUI_BUTTON_CLOSE, ibAuiGetControlFlags(closeState) | wxCONTROL_FLAT);

        closeButton->rect = wxRect(glyph.x, glyph.y, size, tabRect.height);
        closeWidth = size;
    }

    // the label in its lines (ibAuiTabLabelLines, broken in the font it was measured in), each cut to the room left, in
    // the theme's text - the selected tab's is weightier, its font bold (Max, 2026-11-10: by default everything black)
    const wxString caption = page.caption.empty() ? wxString(wxT("Xj")) : page.caption;
    dc.SetFont(m_measuringFont);
    const wxArrayString lines = ibAuiTabLabelLines(dc, page.caption, wnd->FromDIP(ibAUI_TAB_LABEL_WIDTH));
    const int lineCount = static_cast<int>(lines.size());
    dc.SetFont(m_selectedFont);
    wxSize selectedExtent(0, dc.GetTextExtent(caption).y * lineCount);
    for ( const wxString& line : lines )
        selectedExtent.x = wxMax(selectedExtent.x, dc.GetTextExtent(line).x);
    dc.SetFont(page.active ? m_selectedFont : m_normalFont);
    const int textHeight = dc.GetTextExtent(caption).y;

    const int textWidth = tabRect.width - (textX - tabRect.x) - closeWidth;
    const int textY = middle - textHeight * lineCount / 2 - 1;
    dc.SetTextForeground(wxTHEME_COLOUR(CONTROL_TEXT));
    for ( int n = 0; n < lineCount; n++ )
        dc.DrawText(wxControl::Ellipsize(lines[n], dc, wxELLIPSIZE_END, textWidth, wxELLIPSIZE_FLAGS_NONE),
                    textX, textY + textHeight * n);

    // the focus round the picture and the label of the selected tab, the row focused
    if (page.active && wnd->FindFocus() == wnd)
    {
        wxRect focusRect = page.caption.empty() ? wxRect() : wxRect(wxPoint(textX, textY), selectedExtent);
        focusRect.Union(bitmapRect);
        focusRect.Inflate(2, 2);
        renderer->DrawFocusRect(wnd, dc, focusRect, 0);
    }

    page.rect = tabRect;
    return tabWidth;
}


// Render() renders the tab catalog to the specified DC
// It is a virtual function and can be overridden to
// provide custom drawing capabilities
void ibAuiTabContainer::Render(wxDC* pdc, wxWindow* wnd)
{
    if (m_rect.IsEmpty())
        return;

    size_t i;
    size_t page_count = m_pages.GetCount();

    wxDC& dc = *pdc;

    // draw background
    ibThemeEngine::Get()->GetRenderer()->DrawTabStrip(dc, m_rect, IsFlagSet(ibAUI_NB_BOTTOM) ? wxBOTTOM : wxTOP);

    // draw buttons
    int left_buttons_width = 0;
    int right_buttons_width = 0;
    RenderButtons(dc, wnd, left_buttons_width, right_buttons_width);

    int offset = left_buttons_width;

    // draw the tabs

    size_t active = (size_t)-1;
    wxRect active_rect;

    wxRect rect = m_rect;
    rect.y = 0;

    if (IsFlagSet(ibAUI_NB_MULTILINE) && page_count)
    {
        // We assume vertical size of all tabs is the same, so it doesn't
        // matter which one we use for measuring.
        const ibAuiNotebookPage& page = m_pages.Item(0);

        const auto size = GetPageTabSize(dc, wnd, page);

        rect.height = size.y;
    }
    else
    {
        rect.height = m_tabRowHeight;
    }

    // Note that this must be consistent with GetAvailableForTabs().
    const int rightBorder = m_rect.width - right_buttons_width - wnd->FromDIP(ibAUI_BUTTONS_BORDER);

    for (i = m_tabOffset; i < page_count; ++i)
    {
        ibAuiNotebookPage& page = m_pages.Item(i);

        UpdateButtonsState(page);

        // Check if this tab is at least partially visible when using a single
        // row (otherwise all rows are visible).
        if (!IsFlagSet(ibAUI_NB_MULTILINE) && offset >= rightBorder)
        {
            // This (and, hence, all the subsequent) tab(s) would be completely
            // hidden, stop drawing.
            break;
        }

        rect.x = offset;
        rect.width = rightBorder - offset;

        offset += DrawPageTab(dc, wnd, page, rect);

        if (page.active)
        {
            active = i;
            active_rect = rect;
        }

        // Start a new row if necessary when using multiple rows.
        if (IsFlagSet(ibAUI_NB_MULTILINE) && page.rowEnd)
        {
            offset = left_buttons_width;

            rect.y += rect.height;
        }
    }


    // make sure to deactivate buttons which are off the screen to the right
    for (++i; i < m_pages.GetCount(); ++i)
    {
        for (auto& button : m_pages[i].buttons)
            button.curState |= ibAUI_BUTTON_STATE_HIDDEN;
    }


    // draw the active tab again so it stands in the foreground
    if (active >= m_tabOffset && active < m_pages.GetCount())
    {
        DrawPageTab(dc, wnd, m_pages.Item(active), active_rect);
    }
}

// Is the tab visible?
bool ibAuiTabContainer::IsTabVisible(int tabPage, int tabOffset, wxReadOnlyDC* dc, wxWindow* wnd)
{
    if (!dc || !dc->IsOk())
        return false;

    // All tabs are always visible when multiple lines are used.
    if (IsFlagSet(ibAUI_NB_MULTILINE))
        return true;

    size_t i;
    size_t page_count = m_pages.GetCount();
    size_t button_count = m_buttons.size();

    // First check if both buttons are disabled - if so, there's no need to
    // check further for visibility.
    int arrowButtonVisibleCount = 0;
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(i);
        if (button.id == ibAUI_BUTTON_LEFT ||
            button.id == ibAUI_BUTTON_RIGHT)
        {
            if ((button.curState & ibAUI_BUTTON_STATE_HIDDEN) == 0)
                arrowButtonVisibleCount ++;
        }
    }

    // Tab must be visible
    if (arrowButtonVisibleCount == 0)
        return true;

    // If tab is less than the given offset, it must be invisible by definition
    if (tabPage < tabOffset)
        return false;

    // draw buttons
    int left_buttons_width = 0;
    int right_buttons_width = 0;

    // calculate size of the buttons on the right side
    int offset = m_rect.x + m_rect.width;
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxRIGHT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;

        offset -= button.rect.GetWidth();
        right_buttons_width += button.rect.GetWidth();
    }

    offset = 0;

    // calculate size of the buttons on the left side
    for (i = 0; i < button_count; ++i)
    {
        ibAuiTabContainerButton& button = m_buttons.at(button_count - i - 1);

        if (button.location != wxLEFT)
            continue;
        if (button.curState & ibAUI_BUTTON_STATE_HIDDEN)
            continue;

        offset += button.rect.GetWidth();
        left_buttons_width += button.rect.GetWidth();
    }

    offset = left_buttons_width;

    if (offset == 0)
        offset += wnd->FromDIP(ibAUI_TABS_INDENT);

    wxRect rect = m_rect;

    // See if the given page is visible at the given tab offset (effectively scroll position)
    for (i = tabOffset; i < page_count; ++i)
    {
        ibAuiNotebookPage& page = m_pages.Item(i);

        rect.width = m_rect.width - right_buttons_width - offset - wnd->FromDIP(ibAUI_BUTTONS_BORDER);

        if (rect.width <= 0)
            return false; // haven't found the tab, and we've run out of space, so return false

        UpdateButtonsState(page);

        int x_extent = 0;
        GetPageTabSize(*dc, wnd, page, &x_extent);

        offset += x_extent;

        if (i == (size_t) tabPage)
        {
            // If not all of the tab is visible, and supposing there's space to display it all,
            // we could do better so we return false.
            if (((m_rect.width - right_buttons_width - offset - wnd->FromDIP(ibAUI_BUTTONS_BORDER)) <= 0) && ((m_rect.width - right_buttons_width - left_buttons_width) > x_extent))
                return false;
            else
                return true;
        }
    }

    // Shouldn't really get here, but if it does, assume the tab is visible to prevent
    // further looping in calling code.
    return true;
}

// Make the tab visible if it wasn't already
void ibAuiTabContainer::MakeTabVisible(int tabPage, wxWindow* win)
{
    wxInfoDC dc(win);
    if (!IsTabVisible(tabPage, GetTabOffset(), & dc, win))
    {
        int i;
        for (i = 0; i < (int) m_pages.GetCount(); i++)
        {
            if (IsTabVisible(tabPage, i, & dc, win))
            {
                SetTabOffset(i);
                win->Refresh();
                return;
            }
        }
    }
}

// TabHitTest() tests if a tab was hit, returning the struct containing the
// window that was hit together with its position or null pointer otherwise.
ibAuiTabContainer::HitTestResult
ibAuiTabContainer::TabHitTest(const wxPoint& pt, int flags) const
{
    if (!m_rect.Contains(pt))
        return {};

    const ibAuiTabContainerButton* const btn = ButtonHitTest(pt);
    if (btn && !(btn->curState & ibAUI_BUTTON_STATE_DISABLED))
    {
        for ( const auto& button : m_buttons )
        {
            if ( btn == &button )
                return {};
        }
    }

    const size_t page_count = m_pages.GetCount();
    for (size_t i = m_tabOffset; i < page_count; ++i)
    {
        ibAuiNotebookPage& page = m_pages.Item(i);
        if (page.rect.Contains(pt))
        {
            return { page.window, static_cast<int>(i) };
        }

        // Also optionally check if the point lies over the blank space after
        // the last tab in the row if this should be allowed.
        if ((flags & HitTest_AllowAfterTab) && page.rowEnd)
        {
            if (pt.x >= page.rect.x &&
                    pt.y >= page.rect.y &&
                        pt.y < page.rect.y + page.rect.height)
            {
                return { page.window, static_cast<int>(i) };
            }
        }
    }

    return {};
}

// ButtonHitTest() tests if a button was hit. The function returns
// the button if one was hit and null pointer otherwise
ibAuiTabContainerButton*
ibAuiTabContainer::ButtonHitTest(const wxPoint& pt) const
{
    if (!m_rect.Contains(pt))
        return nullptr;

    for ( auto& button : m_buttons )
    {
        if (button.rect.Contains(pt) &&
            !(button.curState & ibAUI_BUTTON_STATE_HIDDEN ))
        {
            return const_cast<ibAuiTabContainerButton*>(&button);
        }
    }

    for ( const auto& page : m_pages )
    {
        for ( const auto& button : page.buttons )
        {
            if (button.rect.Contains(pt) &&
                !(button.curState & (ibAUI_BUTTON_STATE_HIDDEN |
                                       ibAUI_BUTTON_STATE_DISABLED)))
            {
                return const_cast<ibAuiTabContainerButton*>(&button);
            }
        }
    }

    return nullptr;
}



// the utility function ShowWnd() is the same as show,
// except it handles ibAuiMDIChildFrame windows as well,
// as the Show() method on this class is "unplugged"
static void ShowWnd(wxWindow* wnd, bool show)
{
#if wxUSE_MDI
    if (wxDynamicCast(wnd, ibAuiMDIChildFrame))
    {
        ibAuiMDIChildFrame* cf = (ibAuiMDIChildFrame*)wnd;
        cf->wxWindow::Show(show);
    }
    else
#endif
    {
        wnd->Show(show);
    }
}


// DoShowHide() this function shows the active window, then
// hides all of the other windows (in that order)
void ibAuiTabContainer::DoShowHide()
{
    const ibAuiNotebookPageArray& pages = GetPages();

    // show new active page first
    for ( const auto& page : pages )
    {
        if (page.active)
        {
            ShowWnd(page.window, true);
            break;
        }
    }

    // hide all other pages
    for ( const auto& page : pages )
    {
        if (!page.active)
            ShowWnd(page.window, false);
    }
}


int ibAuiTabContainer::GetFirstTabOfKind(ibAuiTabKind kind) const
{
    int pos = 0;
    for ( const auto& page : m_pages )
    {
        if ( page.kind == kind )
            break;

        pos++;
    }

    return pos;
}

int ibAuiTabContainer::GetFirstTabNotOfKind(ibAuiTabKind kind) const
{
    int pos = 0;
    for ( const auto& page : m_pages )
    {
        if ( page.kind != kind )
            break;

        pos++;
    }

    return pos;
}


// -- ibAuiTabCtrl class implementation --



wxBEGIN_EVENT_TABLE(ibAuiTabCtrl, ibControl)
    EVT_PAINT(ibAuiTabCtrl::OnPaint)
    EVT_SIZE(ibAuiTabCtrl::OnSize)
    EVT_LEFT_DOWN(ibAuiTabCtrl::OnLeftDown)
    EVT_LEFT_DCLICK(ibAuiTabCtrl::OnLeftDClick)
    EVT_LEFT_UP(ibAuiTabCtrl::OnLeftUp)
    EVT_MIDDLE_DOWN(ibAuiTabCtrl::OnMiddleDown)
    EVT_MIDDLE_UP(ibAuiTabCtrl::OnMiddleUp)
    EVT_RIGHT_DOWN(ibAuiTabCtrl::OnRightDown)
    EVT_RIGHT_UP(ibAuiTabCtrl::OnRightUp)
    EVT_MOTION(ibAuiTabCtrl::OnMotion)
    EVT_LEAVE_WINDOW(ibAuiTabCtrl::OnLeaveWindow)
    EVT_SET_FOCUS(ibAuiTabCtrl::OnSetFocus)
    EVT_KILL_FOCUS(ibAuiTabCtrl::OnKillFocus)
    EVT_CHAR(ibAuiTabCtrl::OnChar)
    EVT_MOUSE_CAPTURE_LOST(ibAuiTabCtrl::OnCaptureLost)
wxEND_EVENT_TABLE()


ibAuiTabCtrl::ibAuiTabCtrl(ibAuiNotebook* parent, wxWindowID id)
            : ibControl(parent, id,
                        wxDefaultPosition,
                        wxDefaultSize,
                        wxNO_BORDER | wxWANTS_CHARS,
                        wxDefaultValidator,
                        wxT("ibAuiTabCtrl"))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
}

ibAuiTabCtrl::~ibAuiTabCtrl() = default;

// Our parent is always ibAuiNotebook, by construction, so the cast is safe.
ibAuiNotebook* ibAuiTabCtrl::GetBook() const
{
    return static_cast<ibAuiNotebook*>(GetParent());
}

void ibAuiTabCtrl::DoShowTab(int idx)
{
    DoShowHide();
    MakeTabVisible(idx, this);
}

void ibAuiTabCtrl::DoUpdateActive()
{
    DoShowHide();
    Refresh();
}

void ibAuiTabCtrl::DoEndDragging()
{
    m_clickPt = wxDefaultPosition;
    m_isDragging = false;
    m_clickTab = nullptr;
}

void ibAuiTabCtrl::DoApplyRect(const wxRect& rect, int tabCtrlHeight)
{
    // Save the full rectangle for GetHintScreenRect().
    m_fullRect = rect;

    // Save the height of a single tab row before possibly changing it below in
    // multi-line case.
    SetRowHeight(tabCtrlHeight);

    if (IsFlagSet(ibAUI_NB_MULTILINE))
    {
        tabCtrlHeight += LayoutMultiLineTabs(rect, this);
    }

    if (IsFlagSet(ibAUI_NB_BOTTOM))
    {
        SetSize(rect.x, rect.y + rect.height - tabCtrlHeight,
                rect.width, tabCtrlHeight);
        SetRect(wxRect(0, 0, rect.width, tabCtrlHeight));
    }
    else //TODO: if (IsFlagSet(ibAUI_NB_TOP))
    {
        SetSize(rect.x, rect.y, rect.width, tabCtrlHeight);
        SetRect(wxRect(0, 0, rect.width, tabCtrlHeight));
    }
    // TODO: else if (IsFlagSet(ibAUI_NB_LEFT)){}
    // TODO: else if (IsFlagSet(ibAUI_NB_RIGHT)){}

    // SEAM vs wx: refreshed, not painted at once (Update) - the row is drawn when the window paints, after the change
    // that laid it out, not in its middle: the notebook's thaw lays it out (DoThaw), and a change frozen round painted
    // its tabs as it thawed, half done (a tab closed, the selection not yet moved)
    Refresh();
}

wxRect ibAuiTabCtrl::GetHintScreenRect() const
{
    wxRect rect = m_fullRect;
    GetParent()->ClientToScreen(&rect.x, &rect.y);
    return rect;
}

void ibAuiTabCtrl::OnPaint(wxPaintEvent&)
{
    // SEAM vs wx: on the theme's background (ibThemePaintDC) - a row of no tabs was left unpainted
    ibThemePaintDC dc(this);

    if (GetPageCount() > 0)
        Render(&dc, this);
}

int ibAuiTabCtrl::ShowDropDown()
{
    ibMenu menuPopup;

    constexpr int MENU_ID_BASE = 1000;
    int id = MENU_ID_BASE;

    for ( const auto& page : m_pages )
    {
        // Preserve ampersands possibly present in the caption string by
        // escaping them before passing the caption to the menu item.
        wxString caption = wxControl::EscapeMnemonics(page.caption);

        // if there is no caption, make it a space.  This will prevent
        // an assert in the menu code.
        if (caption.IsEmpty())
            caption = wxT(" ");

        wxMenuItem* const item = menuPopup.Append(id++, caption);
        if (page.bitmap.IsOk())
            item->SetBitmap(page.bitmap);
    }

    // find out where to put the popup menu of window items
    wxPoint pt = ScreenToClient(::wxGetMousePosition());

    // find out the screen coordinate at the bottom of the tab ctrl
    const wxRect cli_rect = GetClientRect();
    pt.y = cli_rect.y + cli_rect.height;

    const int command = GetPopupMenuSelectionFromUser(menuPopup, pt);

    if (command >= MENU_ID_BASE)
        return command - MENU_ID_BASE;

    return wxNOT_FOUND;
}

void ibAuiTabCtrl::OnSize(wxSizeEvent& evt)
{
    wxSize s = evt.GetSize();
    wxRect r(0, 0, s.GetWidth(), s.GetHeight());
    SetRect(r);
}

void ibAuiTabCtrl::OnLeftDown(wxMouseEvent& evt)
{
    CaptureMouse();

    // Reset any previous values first.
    DoEndDragging();

    const wxPoint pos = evt.GetPosition();
    ibAuiTabContainerButton* const buttonUnderMouse = ButtonHitTest(pos);

    if ( auto const tabInfo = TabHitTest(pos) )
    {
        int new_selection = tabInfo.pos;

        // ibAuiNotebooks always want to receive this event
        // even if the tab is already active, because they may
        // have multiple tab controls
        if ((new_selection != GetActivePage()) && !buttonUnderMouse)
        {
            ibAuiTabEventSource::TabClicked(this, new_selection);
        }

        m_clickPt.x = evt.m_x;
        m_clickPt.y = evt.m_y;
        m_clickTab = tabInfo.window;
    }

    auto* const pressedButton = FindPressedButton();
    if ( buttonUnderMouse != pressedButton )
    {
        if ( pressedButton )
            ClearButtonState(*pressedButton, ibAUI_BUTTON_STATE_PRESSED);

        if ( buttonUnderMouse )
            SetButtonState(*buttonUnderMouse, ibAUI_BUTTON_STATE_PRESSED);
    }
    //else: No button state to change.
}

void ibAuiTabCtrl::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    if (m_isDragging)
    {
        const auto clickTab = m_clickTab;

        DoEndDragging();

        ibAuiTabEventSource::TabCancelDrag(this, GetIdxFromWindow(clickTab));
    }

    auto* const pressedButton = FindPressedButton();
    if ( pressedButton )
        ClearButtonState(*pressedButton, ibAUI_BUTTON_STATE_PRESSED);

    auto* const hoverButton = FindHoverButton();
    if ( hoverButton )
        ClearButtonState(*hoverButton, ibAUI_BUTTON_STATE_HOVER);
}

void ibAuiTabCtrl::OnLeftUp(wxMouseEvent& evt)
{
    if (GetCapture() == this)
        ReleaseMouse();

    if (m_isDragging)
    {
        const auto clickTab = m_clickTab;

        DoEndDragging();

        ibAuiTabEventSource::TabEndDrag(this, GetIdxFromWindow(clickTab));

        return;
    }

    if (auto* const pressedButton = FindPressedButton())
    {
        ClearButtonState(*pressedButton, ibAUI_BUTTON_STATE_PRESSED);

        // make sure we're still clicking the button
        const ibAuiTabContainerButton* const
            button = ButtonHitTest(evt.GetPosition());
        if (!button || button->curState & ibAUI_BUTTON_STATE_DISABLED)
            return;

        if (button != pressedButton)
            return;

        if (!(pressedButton->curState & ibAUI_BUTTON_STATE_DISABLED))
        {
            OnButton(GetIdxFromWindow(m_clickTab), pressedButton->id);
        }
    }

    DoEndDragging();
}

void ibAuiTabCtrl::OnMiddleUp(wxMouseEvent& evt)
{
    auto const tabInfo = TabHitTest(evt.GetPosition());
    if (!tabInfo)
        return;

    ibAuiTabEventSource::TabMiddleUp(this, tabInfo.pos);
}

void ibAuiTabCtrl::OnMiddleDown(wxMouseEvent& evt)
{
    auto const tabInfo = TabHitTest(evt.GetPosition());
    if (!tabInfo)
        return;

    ibAuiTabEventSource::TabMiddleDown(this, tabInfo.pos);
}

void ibAuiTabCtrl::OnRightUp(wxMouseEvent& evt)
{
    auto const tabInfo = TabHitTest(evt.GetPosition());
    if (!tabInfo)
        return;

    ibAuiTabEventSource::TabRightUp(this, tabInfo.pos);
}

void ibAuiTabCtrl::OnRightDown(wxMouseEvent& evt)
{
    auto const tabInfo = TabHitTest(evt.GetPosition());
    if (!tabInfo)
        return;

    ibAuiTabEventSource::TabRightDown(this, tabInfo.pos);
}

void ibAuiTabCtrl::OnLeftDClick(wxMouseEvent& evt)
{
    wxPoint pos = evt.GetPosition();

    if ( const ibAuiTabContainerButton* const button = ButtonHitTest(pos) )
    {
        if ( !(button->curState & ibAUI_BUTTON_STATE_DISABLED) &&
             (button->id == ibAUI_BUTTON_LEFT ||
              button->id == ibAUI_BUTTON_RIGHT) )
        {
            OnButton(GetIdxFromWindow(m_clickTab), button->id);
        }

        return;
    }

    if ( !TabHitTest(pos) )
    {
        ibAuiTabEventSource::TabBgDClick(this);
    }
}

void ibAuiTabCtrl::OnMotion(wxMouseEvent& evt)
{
    wxPoint pos = evt.GetPosition();

    // Don't highlight any buttons while dragging the tab itself.
    if ( !m_isDragging )
    {
        // check if the mouse is hovering above a button and, if so, if it's the
        // same one as before or a different one
        auto* const hoverButton = FindHoverButton();
        ibAuiTabContainerButton* const button = ButtonHitTest(pos);
        if ( button != hoverButton )
        {
            if ( hoverButton )
                ClearButtonState(*hoverButton, ibAUI_BUTTON_STATE_HOVER);

            if ( button && !(button->curState & ibAUI_BUTTON_STATE_DISABLED) )
                SetButtonState(*button, ibAUI_BUTTON_STATE_HOVER);
        }

        // Don't do anything else if we're hovering over a button, even a
        // disabled one.
        if ( button )
            return;

        // Also skip the rest if we're moving the mouse while a button is
        // pressed.
        if ( FindPressedButton() )
            return;
    }

    bool hovering = false;
    if (evt.Moving())
    {
        if ( auto const tabInfo = TabHitTest(pos) )
        {
            hovering = true;

            SetHoverTab(tabInfo.window);

#if wxUSE_TOOLTIPS
            wxString tooltip(m_pages[tabInfo.pos].tooltip);

            // If the text changes, set it else, keep old, to avoid
            // 'moving tooltip' effect
            if (GetToolTipText() != tooltip)
                SetToolTip(tooltip);
#endif // wxUSE_TOOLTIPS
        }
    }

    if (!hovering)
    {
        SetHoverTab(nullptr);

#if wxUSE_TOOLTIPS
        UnsetToolTip();
#endif // wxUSE_TOOLTIPS
    }

    if (!evt.LeftIsDown() || m_clickPt == wxDefaultPosition)
        return;

    if (m_isDragging)
    {
        ibAuiTabEventSource::TabDragMotion(this, GetIdxFromWindow(m_clickTab));
        return;
    }


    if (wxSystemSettings::ExceedsDragThreshold(m_clickPt, pos, this))
    {
        const int idx = GetIdxFromWindow(m_clickTab);
        if ( idx != wxNOT_FOUND )
        {
            switch ( GetPage(idx).kind )
            {
                case ibAuiTabKind::Normal:
                case ibAuiTabKind::Pinned:
                    break;

                case ibAuiTabKind::Locked:
                    // Don't allow dragging locked tabs.
                    return;
            }
        }

        ibAuiTabEventSource::TabBeginDrag(this, idx);

        m_isDragging = true;
    }
}

void ibAuiTabCtrl::OnLeaveWindow(wxMouseEvent& WXUNUSED(event))
{
    auto* const hoverButton = FindHoverButton();
    if (hoverButton)
    {
        ClearButtonState(*hoverButton, ibAUI_BUTTON_STATE_HOVER);
    }

    SetHoverTab(nullptr);
}

ibAuiTabContainerButton*
ibAuiTabCtrl::FindButtonIn(ibAuiPaneButtonState state) const
{
    for ( const auto& button : m_buttons )
    {
        if ( button.curState & state )
            return const_cast<ibAuiTabContainerButton*>(&button);
    }

    for ( const auto& page : m_pages )
    {
        for ( const auto& button : page.buttons )
        {
            if ( button.curState & state )
                return const_cast<ibAuiTabContainerButton*>(&button);
        }
    }

    return nullptr;
}

bool
ibAuiTabCtrl::UpdateButtonStateAndRefresh(ibAuiTabContainerButton& button,
                                          ibAuiPaneButtonState state,
                                          bool on)
{
    int newState = button.curState;
    if ( on )
        newState |= state;
    else
        newState &= ~state;

    if ( newState == button.curState )
        return false;

    button.curState = newState;

    Refresh();
    Update();

    return true;
}

void ibAuiTabCtrl::OnButton(int tabIdx, int button)
{
    // Let ibAuiNotebook generate the event for this button click and perform
    // the default action for the buttons handled at its level, such as closing
    // or pinning the tab.
    //
    // It only returns true the application handled the event without skipping
    // it, which means that it handled the button completely on its own.
    if (ibAuiTabEventSource::TabButton(this, tabIdx, button))
        return;

    // For mostly historical (but also convenience) reasons, some buttons are
    // handled here instead of in ibAuiNotebook.
    if (button == ibAUI_BUTTON_LEFT || button == ibAUI_BUTTON_RIGHT)
    {
        if (button == ibAUI_BUTTON_LEFT)
        {
            if (GetTabOffset() > 0)
            {
                SetTabOffset(GetTabOffset()-1);
                Refresh();
                Update();
            }
        }
        else
        {
            SetTabOffset(GetTabOffset()+1);
            Refresh();
            Update();
        }
    }
    else if (button == ibAUI_BUTTON_WINDOWLIST)
    {
        int idx = ShowDropDown();

        if (idx != -1)
        {
            ibAuiTabEventSource::TabClicked(this, idx);
        }
    }
}

void ibAuiTabCtrl::OnSetFocus(wxFocusEvent& WXUNUSED(event))
{
    Refresh();
}

void ibAuiTabCtrl::OnKillFocus(wxFocusEvent& WXUNUSED(event))
{
    Refresh();
}

void ibAuiTabCtrl::OnChar(wxKeyEvent& event)
{
    if (GetActivePage() == -1)
    {
        event.Skip();
        return;
    }

    // We can't leave tab processing to the system; on Windows, tabs and keys
    // get eaten by the system and not processed properly if we specify both
    // wxTAB_TRAVERSAL and wxWANTS_CHARS. And if we specify just wxTAB_TRAVERSAL,
    // we don't key arrow key events.

    int key = event.GetKeyCode();

    if (key == WXK_NUMPAD_PAGEUP)
        key = WXK_PAGEUP;
    if (key == WXK_NUMPAD_PAGEDOWN)
        key = WXK_PAGEDOWN;
    if (key == WXK_NUMPAD_HOME)
        key = WXK_HOME;
    if (key == WXK_NUMPAD_END)
        key = WXK_END;
    if (key == WXK_NUMPAD_LEFT)
        key = WXK_LEFT;
    if (key == WXK_NUMPAD_RIGHT)
        key = WXK_RIGHT;

    if (key == WXK_TAB || key == WXK_PAGEUP || key == WXK_PAGEDOWN)
    {
        bool bCtrlDown = event.ControlDown();
        bool bShiftDown = event.ShiftDown();

        bool bForward = (key == WXK_TAB && !bShiftDown) || (key == WXK_PAGEDOWN);
        bool bWindowChange = (key == WXK_PAGEUP) || (key == WXK_PAGEDOWN) || bCtrlDown;
        bool bFromTab = (key == WXK_TAB);

        if (bFromTab && !bWindowChange)
        {
            // Handle ordinary tabs via Navigate. This is needed at least for wxGTK to tab properly.
            Navigate(bForward ? wxNavigationKeyEvent::IsForward : wxNavigationKeyEvent::IsBackward);
            return;
        }

        ibAuiNotebook* nb = wxDynamicCast(GetParent(), ibAuiNotebook);
        if (!nb)
        {
            event.Skip();
            return;
        }

        wxNavigationKeyEvent keyEvent;
        keyEvent.SetDirection(bForward);
        keyEvent.SetWindowChange(bWindowChange);
        keyEvent.SetFromTab(bFromTab);
        keyEvent.SetEventObject(nb);

        if (!nb->ProcessWindowEvent(keyEvent))
        {
            // Not processed? Do an explicit tab into the page.
            wxWindow* win = GetWindowFromIdx(GetActivePage());
            if (win)
                win->SetFocus();
        }
        return;
    }

    if (m_pages.GetCount() < 2)
    {
        event.Skip();
        return;
    }

    int newPage = -1;

    int forwardKey, backwardKey;
    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        forwardKey = WXK_LEFT;
        backwardKey = WXK_RIGHT;
    }
    else
     {
        forwardKey = WXK_RIGHT;
        backwardKey = WXK_LEFT;
    }

    if (key == forwardKey)
    {
        if (m_pages.GetCount() > 1)
        {
            if (GetActivePage() == -1)
                newPage = 0;
            else if (GetActivePage() < (int) (m_pages.GetCount() - 1))
                newPage = GetActivePage() + 1;
        }
    }
    else if (key == backwardKey)
    {
        if (m_pages.GetCount() > 1)
        {
            if (GetActivePage() == -1)
                newPage = (int) (m_pages.GetCount() - 1);
            else if (GetActivePage() > 0)
                newPage = GetActivePage() - 1;
        }
    }
    else if (key == WXK_HOME)
    {
        newPage = 0;
    }
    else if (key == WXK_END)
    {
        newPage = (int) (m_pages.GetCount() - 1);
    }
    else
        event.Skip();

    if (newPage != -1)
    {
        ibAuiTabEventSource::TabClicked(this, newPage);
    }
    else
        event.Skip();
}

// ibAuiTabFrame is an interesting case.  It's important that all child pages
// of the multi-notebook control are all actually children of that control
// (and not grandchildren).  ibAuiTabFrame facilitates this.  There is one
// instance of ibAuiTabFrame for each tab control inside the multi-notebook.
// It's important to know that ibAuiTabFrame is not a real window, but it merely
// used to capture the dimensions/positioning of the internal tab control and
// it's managed page windows

class ibAuiTabFrame : public wxWindow
{
public:

    ibAuiTabFrame(ibAuiTabCtrl* tabs, const wxSize& size, int tabCtrlHeight) :
        m_rect(size),
        m_tabs(tabs),
        m_tabCtrlHeight(tabCtrlHeight)
    {
    }

    ~ibAuiTabFrame()
    {
        // use pending delete because sometimes during
        // window closing, refreshes are pending
        if (!wxPendingDelete.Member(m_tabs))
            wxPendingDelete.Append(m_tabs);
    }

    void SetTabCtrlHeight(int h)
    {
        m_tabCtrlHeight = h;
    }

    // As we don't have a valid HWND, base class implementations of these
    // functions don't work for this window, so override them to forward to the
    // real window.
    wxSize GetDPI() const override
    {
        return m_tabs->GetDPI();
    }

    wxLayoutDirection GetLayoutDirection() const override
    {
        return m_tabs->GetLayoutDirection();
    }

protected:
    void DoSetSize(int x, int y,
                   int width, int height,
                   int WXUNUSED(sizeFlags = wxSIZE_AUTO)) override
    {
        m_rect = wxRect(x, y, width, height);
        DoSizing();
    }

    void DoGetClientSize(int* x, int* y) const override
    {
        *x = m_rect.width;
        *y = m_rect.height;
    }

public:
    bool Show( bool WXUNUSED(show = true) ) override { return false; }

    void DoSizing()
    {
        if (m_tabs->IsFrozen() || m_tabs->GetParent()->IsFrozen())
            return;

        m_tabs->DoApplyRect(m_rect, m_tabCtrlHeight);
        m_tab_rect = m_tabs->GetRect();

        const ibAuiNotebookPageArray& pages = m_tabs->GetPages();

        // SEAM vs wx: no border round a page - the art's was none
        for ( auto& page : pages )
        {
            int height = m_rect.height - m_tab_rect.height;
            if ( height < 0 )
            {
                // avoid passing negative height to wxWindow::SetSize(), this
                // results in assert failures/GTK+ warnings
                height = 0;
            }
            int width = m_rect.width;
            if (width < 0)
                width = 0;

            if (m_tabs->IsFlagSet(ibAUI_NB_BOTTOM))
            {
                page.window->SetSize(m_rect.x,
                                     m_rect.y,
                                     width,
                                     height);
            }
            else //TODO: if (IsFlagSet(ibAUI_NB_TOP))
            {
                page.window->SetSize(m_rect.x,
                                     m_rect.y + m_tab_rect.height,
                                     width,
                                     height);
            }
            // TODO: else if (IsFlagSet(ibAUI_NB_LEFT)){}
            // TODO: else if (IsFlagSet(ibAUI_NB_RIGHT)){}
        }
    }

protected:
    void DoGetSize(int* x, int* y) const override
    {
        if (x)
            *x = m_rect.GetWidth();
        if (y)
            *y = m_rect.GetHeight();
    }

public:
    void Update() override
    {
        // does nothing
    }

    wxRect m_rect;
    wxRect m_tab_rect;
    ibAuiTabCtrl* const m_tabs;
    int m_tabCtrlHeight = 0;

    wxDECLARE_CLASS(ibAuiTabFrame);
};

wxIMPLEMENT_CLASS(ibAuiTabFrame, wxWindow);

const int ibAuiBaseTabCtrlId = 5380;


// -- ibAuiNotebook class implementation --

// More convenient version of FindTab(): returns all the results instead of
// requiring output parameters for returning some of them.
//
// Note that TabInfo returned by FindTab() is normally always valid.
struct ibAuiNotebook::TabInfo : ibAuiNotebookPosition
{
    TabInfo() = default;

    TabInfo(ibAuiTabCtrl* tabCtrl_, int tabIdx_, ibAuiNotebookPage* info)
        : ibAuiNotebookPosition{tabCtrl_, tabIdx_}, pageInfo(info)
    {
    }

    // Information about the page or nullptr if not found.
    ibAuiNotebookPage* pageInfo = nullptr;
};

wxBEGIN_EVENT_TABLE(ibAuiNotebook, wxBookCtrlBase)
    EVT_SIZE(ibAuiNotebook::OnSize)
    EVT_CHILD_FOCUS(ibAuiNotebook::OnChildFocusNotebook)
    EVT_NAVIGATION_KEY(ibAuiNotebook::OnNavigationKeyNotebook)
    EVT_SYS_COLOUR_CHANGED(ibAuiNotebook::OnSysColourChanged)
    EVT_DPI_CHANGED(ibAuiNotebook::OnDPIChanged)
wxEND_EVENT_TABLE()

namespace
{

// ibAuiNotebook always adds a special dummy pane with this name.
constexpr const char* const DUMMY_PANE_NAME = "dummy";

// Check if this is a dummy pane.
bool IsDummyPane(const ibAuiPaneInfo& pane)
{
    return pane.name == DUMMY_PANE_NAME;
}

} // anonymous namespace

bool ibAuiNotebook::ibAuiNotebookManager::CanAddPane(
    wxWindow* window, const ibAuiPaneInfo& paneInfo) const
{
    if ( !ibAuiManager::CanAddPane(window, paneInfo) )
        return false;

    // ibAuiNotebook creates a hidden dummy pane before any real tab frames.
    if ( IsDummyPane(paneInfo) && m_panes.empty() )
        return true;

    wxCHECK_MSG(wxDynamicCast(window, ibAuiTabFrame), false,
                wxT("Can't add non-tab panes to ibAuiNotebook's manager"));

    return true;
}

void ibAuiNotebook::OnSysColourChanged(wxSysColourChangedEvent &event)
{
    event.Skip(true);

    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;
        ibAuiTabFrame* tab_frame = (ibAuiTabFrame*)pane.window;
        ibAuiTabCtrl* tabctrl = tab_frame->m_tabs;
        tabctrl->Refresh();
    }
    Refresh();
}

void ibAuiNotebook::OnDPIChanged(wxDPIChangedEvent& event)
{
    UpdateTabCtrlHeight();
    event.Skip();
}

void ibAuiNotebook::Init()
{
    m_curPage = wxNOT_FOUND;
    m_tabIdCounter = ibAuiBaseTabCtrlId;
    m_dummyWnd = nullptr;
    m_requestedBmpSize = wxDefaultSize;
    m_requestedTabCtrlHeight = -1;
}

bool ibAuiNotebook::Create(wxWindow* parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style)
{
    if (!wxControl::Create(parent, id, pos, size, style))
        return false;

    InitNotebook(style);

    return true;
}

// InitNotebook() contains common initialization
// code called by all constructors
void ibAuiNotebook::InitNotebook(long style)
{
    SetName(wxT("ibAuiNotebook"));
    m_curPage = wxNOT_FOUND;
    m_tabIdCounter = ibAuiBaseTabCtrlId;
    m_dummyWnd = nullptr;
    m_flags = (unsigned int)style;
    m_tabCtrlHeight = FromDIP(20);

    m_normalFont = *wxNORMAL_FONT;
    m_selectedFont = *wxNORMAL_FONT;
    m_selectedFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_measuringFont = m_selectedFont;

    // the row's height in these fonts (SEAM vs wx: setting the art did it)
    UpdateTabCtrlHeight();

    m_dummyWnd = new wxWindow(this, wxID_ANY, wxPoint(0,0), wxSize(0,0));
    m_dummyWnd->SetSize(FromDIP(wxSize(200, 200)));
    m_dummyWnd->Show(false);

    m_mgr.SetManagedWindow(this);
    // SEAM vs wx: the sash between the book's groups moves on release, as the main window's (ibFrontendMainFrame) - a
    // hint follows the mouse; a live drag laid the groups out on every move and left pieces behind (2026-11-12, Max).
    // On GTK3/Cocoa the same - the docking is the engine's there too
    m_mgr.SetFlags(ibAUI_MGR_DEFAULT & ~ibAUI_MGR_LIVE_RESIZE);
    m_mgr.SetDockSizeConstraint(1.0, 1.0); // no dock size constraint

    m_mgr.AddPane(m_dummyWnd,
              ibAuiPaneInfo().Name(DUMMY_PANE_NAME).Bottom().CaptionVisible(false).Show(false));

    m_mgr.Update();
}

ibAuiTabFrame* ibAuiNotebook::CreateTabFrame(wxSize size)
{
    auto* const tabs = new ibAuiTabCtrl(this, m_tabIdCounter++);
    tabs->SetFlags(m_flags);
    tabs->SetNormalFont(m_normalFont);
    tabs->SetSelectedFont(m_selectedFont);
    tabs->SetMeasuringFont(m_measuringFont);

    return new ibAuiTabFrame(tabs, size, m_tabCtrlHeight);
}

ibAuiNotebook::~ibAuiNotebook()
{
    // Indicate we're deleting pages
    SendDestroyEvent();

    while ( GetPageCount() > 0 )
        DeletePage(0);

    m_mgr.UnInit();
}

// SetTabCtrlHeight() is the highest-level override of the
// tab height.  A call to this function effectively enforces a
// specified tab ctrl height, overriding all other considerations,
// such as text or bitmap height.  It overrides any call to
// SetUniformBitmapSize().  Specifying a height of -1 reverts
// any previous call and returns to the default behaviour

void ibAuiNotebook::SetTabCtrlHeight(int height)
{
    m_requestedTabCtrlHeight = height;

    // if window is already initialized, recalculate the tab height
    if (m_dummyWnd)
    {
        UpdateTabCtrlHeight();
    }
}


// SetUniformBitmapSize() ensures that all tabs will have
// the same height, even if some tabs don't have bitmaps
// Passing wxDefaultSize to this function will instruct
// the control to use dynamic tab height-- so when a tab
// with a large bitmap is added, the tab ctrl's height will
// automatically increase to accommodate the bitmap

void ibAuiNotebook::SetUniformBitmapSize(const wxSize& size)
{
    m_requestedBmpSize = size;

    // if window is already initialized, recalculate the tab height
    if (m_dummyWnd)
    {
        UpdateTabCtrlHeight();
    }
}

// UpdateTabCtrlHeight() does the actual tab resizing. It's meant
// to be used internally
bool ibAuiNotebook::UpdateTabCtrlHeight()
{
    // get the tab ctrl height we will use
    int height = CalculateTabCtrlHeight();

    // if the tab control height needs to change, update
    // all of our tab controls with the new height
    if (m_tabCtrlHeight == height)
        return false;

    m_tabCtrlHeight = height;

    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;
        ibAuiTabFrame* tab_frame = (ibAuiTabFrame*)pane.window;
        ibAuiTabCtrl* tabctrl = tab_frame->m_tabs;
        tab_frame->SetTabCtrlHeight(m_tabCtrlHeight);
        // SEAM vs wx: the fonts the height was measured in - the art's copy carried them
        tabctrl->SetNormalFont(m_normalFont);
        tabctrl->SetSelectedFont(m_selectedFont);
        tabctrl->SetMeasuringFont(m_measuringFont);
        tab_frame->DoSizing();
    }

    return true;
}

void ibAuiNotebook::UpdateHintWindowSize()
{
    wxSize size = CalculateNewSplitSize();

    // the placeholder hint window should be set to this size
    ibAuiPaneInfo& info = m_mgr.GetPane(DUMMY_PANE_NAME);
    if (info.IsOk())
    {
        info.MinSize(size);
        info.BestSize(size);
        m_dummyWnd->SetSize(size);
    }
}


// calculates the size of the new split
wxSize ibAuiNotebook::CalculateNewSplitSize()
{
    return m_mgr.CalculateNewSplitSize();
}

int ibAuiNotebook::CalculateTabCtrlHeight()
{
    // if a fixed tab ctrl height is specified,
    // just return that instead of calculating a
    // tab height
    if (m_requestedTabCtrlHeight != -1)
        return m_requestedTabCtrlHeight;

    // find out new best tab height
    return m_tabs.GetBestTabCtrlSize(this, m_requestedBmpSize);
}


void ibAuiNotebook::SetWindowStyleFlag(long style)
{
    wxControl::SetWindowStyleFlag(style);

    m_flags = (unsigned int)style;

    // if the control is already initialized
    if (m_mgr.GetManagedWindow() == (wxWindow*)this)
    {
        // let all of the tab children know about the new style
        for ( const auto& pane : m_mgr.GetAllPanes() )
        {
            if ( IsDummyPane(pane) )
                continue;
            ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
            ibAuiTabCtrl* tabctrl = tabframe->m_tabs;
            tabctrl->SetFlags(m_flags);
            tabframe->DoSizing();
            tabctrl->Refresh();   // SEAM vs wx: not painted at once (ibAuiTabCtrl::DoApplyRect)
        }
    }
}


bool ibAuiNotebook::AddPage(wxWindow* page,
                            const wxString& caption,
                            bool select,
                            const wxBitmapBundle& bitmap)
{
    return InsertPage(GetPageCount(), page, caption, select, bitmap);
}

bool ibAuiNotebook::InsertPage(size_t page_idx,
                               wxWindow* page,
                               const wxString& caption,
                               bool select,
                               const wxBitmapBundle& bitmap)
{
    // Note that index may be equal to GetPageCount() here to append the page.
    wxCHECK_MSG(page_idx <= GetPageCount(), false, wxT("invalid page index"));

    wxCHECK_MSG(page, false, wxT("page pointer must be non-null"));

    wxCHECK_MSG(page->GetParent() == this, false,
                wxT("page must be a child of the notebook"));

    ibAuiNotebookPage info;
    info.window = page;
    info.caption = caption;
    info.bitmap = bitmap;
    info.active = false;

    ibAuiTabCtrl* active_tabctrl = GetActiveTabCtrl();

    // The usual case is appending a new page, when we can just add it page at
    // the end of the currently active tab control.
    int tab_page_idx = -1;

    // But when inserting, we may need to do it at a different place.
    if ( page_idx != m_tabs.GetPageCount() )
    {
        const auto tabInfo = FindTab(m_tabs.GetWindowFromIdx(page_idx));
        if ( tabInfo.tabCtrl == active_tabctrl )
        {
            // Use this index for insertion, as it's in the correct control.
            tab_page_idx = tabInfo.tabIdx;
        }
        //else: Do nothing, we'll append it to the active tab control.
    }

    InsertPageAt(info, page_idx, active_tabctrl, tab_page_idx, select);

    return true;
}

void ibAuiNotebook::InsertPageAt(ibAuiNotebookPage& info,
                                 size_t page_idx,
                                 ibAuiTabCtrl* tabctrl,
                                 int tab_page_idx,
                                 bool select)
{
    wxWindow* const page = info.window;

    page->Reparent(this);

    // if there are currently no tabs, the first added
    // tab must be selected, even if "select" is false
    if (m_tabs.GetPageCount() == 0)
    {
        select = true;
    }

    m_tabs.InsertPage(info, page_idx);

    if ( tab_page_idx == -1 )
        tab_page_idx = tabctrl->GetPageCount();
    tabctrl->InsertPage(info, tab_page_idx);

    // Note that we don't need to call DoSizing() if the height has changed, as
    // it's already called from UpdateTabCtrlHeight() itself in this case.
    if ( !UpdateTabCtrlHeight() )
        DoSizing();

    tabctrl->DoShowHide();

    // adjust selected index
    if(m_curPage >= (int) page_idx)
        m_curPage++;

    if (select)
    {
        SetSelectionToWindow(page);
    }
}

int ibAuiNotebook::GetNextPage(bool forward) const
{
    if ( m_curPage == wxNOT_FOUND )
        return wxNOT_FOUND;

    const auto tabInfo = FindTab(m_tabs.GetWindowFromIdx(m_curPage));
    if ( !tabInfo )
        return wxNOT_FOUND;

    // Find the next or previous position in the active tab control, with
    // wraparound.
    const int lastPos = tabInfo.tabCtrl->GetPageCount() - 1;
    const int nextPos = forward
        ? tabInfo.tabIdx < lastPos ? tabInfo.tabIdx + 1 : 0
        : tabInfo.tabIdx > 0 ? tabInfo.tabIdx - 1 : lastPos;

    // Now find the corresponding page index.
    return m_tabs.GetIdxFromWindow(tabInfo.tabCtrl->GetWindowFromIdx(nextPos));
}

wxWindow* ibAuiNotebook::DoRemovePage(size_t page_idx)
{
    wxCHECK_MSG(page_idx < GetPageCount(), nullptr, "invalid page index");

    // Lock the window for changes to avoid flicker when
    // removing the active page (there is a noticeable
    // flicker from the active tab is closed and until a
    // new one is selected) - this is noticeable on MSW
    wxWindowUpdateLocker locker { this };

    // save active window pointer
    wxWindow* active_wnd = nullptr;
    if (m_curPage >= 0)
        active_wnd = m_tabs.GetWindowFromIdx(m_curPage);
    const int old_curpage = m_curPage;

    // save pointer of window being deleted
    wxWindow* wnd = m_tabs.GetWindowFromIdx(page_idx);
    wxWindow* new_active = nullptr;

    ShowWnd(wnd, false);

    // find out which onscreen tab ctrl owns this tab
    const auto tabInfo = FindTab(wnd);
    if ( !tabInfo )
        return nullptr;

    ibAuiTabCtrl* const ctrl = tabInfo.tabCtrl;
    int ctrl_idx = tabInfo.tabIdx;

    bool is_curpage = (m_curPage == (int)page_idx);
    bool is_active_in_split = ctrl->GetPage(ctrl_idx).active;


    // remove the tab from main catalog
    m_tabs.RemovePageAt(page_idx);

    // remove the tab from the onscreen tab ctrl
    ctrl->RemovePageAt(ctrl_idx);

    if (is_active_in_split)
    {
        const int ctrl_new_page_count = (int)ctrl->GetPageCount();

        if (ctrl_idx >= ctrl_new_page_count)
            ctrl_idx = ctrl_new_page_count-1;

        if (ctrl_idx >= 0 && ctrl_idx < ctrl_new_page_count)
        {
            // set new page as active in the tab split
            ctrl->SetActivePage(ctrl_idx);

            // if the page deleted was the current page for the
            // entire tab control, then record the window
            // pointer of the new active page for activation
            if (is_curpage)
            {
                new_active = ctrl->GetWindowFromIdx(ctrl_idx);
            }
            else // not deleting the globally active page
            {
                // so don't change the current one
                new_active = active_wnd;

                // but we still need to show the new active page in this
                // control, otherwise it wouldn't be updated
                ctrl->DoShowTab(ctrl_idx);
            }
        }
    }
    else
    {
        // we are not deleting the active page, so keep it the same
        new_active = active_wnd;
    }


    if (!new_active)
    {
        // we haven't yet found a new page to active,
        // so select the next page from the main tab
        // catalogue

        if (page_idx < m_tabs.GetPageCount())
        {
            new_active = m_tabs.GetPage(page_idx).window;
        }

        if (!new_active && m_tabs.GetPageCount() > 0)
        {
            new_active = m_tabs.GetPage(0).window;
        }
    }


    RemoveEmptyTabFrames();

    // the row as tall as the tabs left need, as a tab put in made it taller (InsertPageAt): the one whose label took two
    // lines closed, the row is one line again (2026-11-11, Max) - the frames laid out when the lock above ends (DoThaw)
    if ( !m_isBeingDeleted )
        UpdateTabCtrlHeight();

    m_curPage = wxNOT_FOUND;

    // set new active pane unless we're being destroyed anyhow
    if ( !m_isBeingDeleted )
    {
        if ( new_active )
        {
            SetSelectionToWindow(new_active);
        }
        else if ( old_curpage != wxNOT_FOUND )
        {
            // Still send this event when not calling SetSelectionToWindow() as
            // the selection did change.
            ibAuiNotebookEvent evt(ibEVT_AUINOTEBOOK_PAGE_CHANGED, m_windowId);
            evt.SetSelection(wxNOT_FOUND);
            evt.SetOldSelection(old_curpage);
            evt.SetEventObject(this);
            (void)ProcessWindowEvent(evt);
        }
    }

    return wnd;
}

int ibAuiNotebook::FindPage(const wxWindow* page) const
{
    return m_tabs.GetIdxFromWindow(page);
}


// SetPageText() changes the tab caption of the specified page
bool ibAuiNotebook::SetPageText(size_t page_idx, const wxString& text)
{
    wxCHECK_MSG(page_idx < GetPageCount(), false, "invalid page index");

    // update our own tab catalog
    ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    page_info.caption = text;

    // update what's on screen
    if ( const auto tabInfo = FindTab(page_info.window) )
    {
        tabInfo.pageInfo->caption = text;
        tabInfo.tabCtrl->Refresh();   // SEAM vs wx: not painted at once (ibAuiTabCtrl::DoApplyRect)
    }

    // a caption taking a line more, or one less, makes the row as tall as its tabs need (GetBestTabCtrlSize)
    UpdateTabCtrlHeight();

    return true;
}

// returns the page caption
wxString ibAuiNotebook::GetPageText(size_t page_idx) const
{
    wxCHECK_MSG(page_idx < GetPageCount(), wxString(), "invalid page index");

    // update our own tab catalog
    const ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    return page_info.caption;
}

bool ibAuiNotebook::SetPageToolTip(size_t page_idx, const wxString& text)
{
    wxCHECK_MSG(page_idx < GetPageCount(), false, "invalid page index");

    // update our own tab catalog
    ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    page_info.tooltip = text;

    const auto tabInfo = FindTab(page_info.window);
    if ( !tabInfo )
        return false;

    tabInfo.pageInfo->tooltip = text;

    // NB: we don't update the tooltip if it is already being displayed, it
    //     typically never happens, no need to code that
    return true;
}

wxString ibAuiNotebook::GetPageToolTip(size_t page_idx) const
{
    wxCHECK_MSG(page_idx < GetPageCount(), wxString(), "invalid page index");

    const ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    return page_info.tooltip;
}

bool ibAuiNotebook::SetPageBitmap(size_t page_idx, const wxBitmapBundle& bitmap)
{
    wxCHECK_MSG(page_idx < GetPageCount(), false, "invalid page index");

    // update our own tab catalog
    ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    page_info.bitmap = bitmap;

    // tab height might have changed
    UpdateTabCtrlHeight();

    // update what's on screen
    const auto tabInfo = FindTab(page_info.window);
    if ( !tabInfo )
        return false;

    tabInfo.pageInfo->bitmap = bitmap;
    tabInfo.tabCtrl->Refresh();   // SEAM vs wx: not painted at once (ibAuiTabCtrl::DoApplyRect)

    return true;
}

// returns the page bitmap
wxBitmap ibAuiNotebook::GetPageBitmap(size_t page_idx) const
{
    wxCHECK_MSG(page_idx < GetPageCount(), wxBitmap(), "invalid page index");

    // update our own tab catalog
    const ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    return page_info.bitmap.GetBitmap(page_info.bitmap.GetDefaultSize());
}

ibAuiTabKind ibAuiNotebook::GetPageKind(size_t page_idx) const
{
    wxCHECK_MSG(page_idx < GetPageCount(), ibAuiTabKind::Normal, "invalid page index");

    return m_tabs.GetPage(page_idx).kind;
}

bool ibAuiNotebook::SetPageKind(size_t page_idx, ibAuiTabKind kind)
{
    wxCHECK_MSG(page_idx < GetPageCount(), false, "invalid page index");

    ibAuiNotebookPage& page_info = m_tabs.GetPage(page_idx);
    if ( page_info.kind == kind )
        return false;

    const auto tabInfo = FindTab(page_info.window);
    if ( !tabInfo )
        return false;

    auto& tabCtrl = *tabInfo.tabCtrl;

    // Update the tab position as changing its kind moves it: if it becomes
    // locked/pinned, it needs to be moved to the end of the group of the tabs
    // of this kind and if it becomes normal, it needs to be moved to the
    // beginning of the normal tabs (and not the end, to minimize the distance
    // by which it moves).
    size_t newIdx = 0;
    switch ( kind )
    {
        case ibAuiTabKind::Normal:
            newIdx = tabCtrl.GetFirstTabOfKind(ibAuiTabKind::Normal);

            // There must be at least the locked/pinned tab which is changing
            // its kind now before the first normal one.
            wxASSERT( newIdx > 0 );

            // Move it before the first normal tab or before the end.
            newIdx--;
            break;

        case ibAuiTabKind::Pinned:
            // When a normal tab becomes pinned, it should be moved to the end
            // of the pinned tabs, but when a locked tab becomes pinned, it
            // makes more sense to put it at the beginning of this group, for
            // symmetry with what happens when a pinned tab becomes normal.
            switch ( tabInfo.pageInfo->kind )
            {
                case ibAuiTabKind::Normal:
                    newIdx = tabCtrl.GetFirstTabOfKind(ibAuiTabKind::Normal);
                    break;

                case ibAuiTabKind::Pinned:
                    // This tab is switching to pinned state, so it couldn't
                    // have already been pinned before.
                    wxFAIL_MSG("unreachable");
                    break;

                case ibAuiTabKind::Locked:
                    newIdx = tabCtrl.GetFirstTabNotOfKind(ibAuiTabKind::Locked);

                    // There must be at least one locked tab, which is
                    // switching to pinned state right now.
                    wxASSERT( newIdx > 0 );

                    newIdx--;
                    break;
            }
            break;

        case ibAuiTabKind::Locked:
            newIdx = tabCtrl.GetFirstTabNotOfKind(ibAuiTabKind::Locked);
            break;
    }

    page_info.kind = kind;

    // And also update kind in this copy of the page info before moving it.
    tabInfo.pageInfo->kind = kind;

    tabCtrl.MovePage(tabInfo.tabIdx, newIdx);
    tabCtrl.Refresh();

    return true;
}

// GetSelection() returns the index of the currently active page
int ibAuiNotebook::GetSelection() const
{
    return m_curPage;
}

// SetSelection() sets the currently active page
int ibAuiNotebook::SetSelection(size_t new_page)
{
    return DoModifySelection(new_page, true);
}

void ibAuiNotebook::SetSelectionToWindow(wxWindow *win)
{
    const int idx = m_tabs.GetIdxFromWindow(win);
    wxCHECK_RET( idx != wxNOT_FOUND, wxT("invalid notebook page") );


    // since a tab was clicked, let the parent know that we received
    // the focus, even if we will assign that focus immediately
    // to the child tab in the SetSelection call below
    // (the child focus event will also let ibAuiManager, if any,
    // know that the notebook control has been activated)

    wxWindow* parent = GetParent();
    if (parent)
    {
        wxChildFocusEvent eventFocus(this);
        parent->ProcessWindowEvent(eventFocus);
    }


    SetSelection(idx);
}

// GetPageCount() returns the total number of
// pages managed by the multi-notebook
size_t ibAuiNotebook::GetPageCount() const
{
    return m_tabs.GetPageCount();
}

// GetPage() returns the wxWindow pointer of the
// specified page
wxWindow* ibAuiNotebook::GetPage(size_t page_idx) const
{
    wxASSERT(page_idx < m_tabs.GetPageCount());

    return m_tabs.GetWindowFromIdx(page_idx);
}

// DoSizing() performs all sizing operations in each tab control
void ibAuiNotebook::DoSizing()
{
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        tabframe->DoSizing();
    }
}

// GetActiveTabCtrl() returns the active tab control.  It is
// called to determine which control gets new windows being added
ibAuiTabCtrl* ibAuiNotebook::GetActiveTabCtrl()
{
    if (m_curPage >= 0 && m_curPage < (int)m_tabs.GetPageCount())
    {
        // find the tab ctrl with the current page
        return FindTab(m_tabs.GetPage(m_curPage).window).tabCtrl;
    }

    // no current page, just return the main tab control
    return GetMainTabCtrl();
}

ibAuiTabCtrl* ibAuiNotebook::GetMainTabCtrl()
{
    ibAuiTabCtrl* tabMain = nullptr;
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        if ( pane.dock_direction == ibAUI_DOCK_CENTER )
        {
            wxASSERT_MSG( !tabMain, "Multiple main tab controls?" );

            tabMain = static_cast<ibAuiTabFrame*>(pane.window)->m_tabs;
        }
    }

    if ( !tabMain )
    {
        ibAuiTabFrame* tabframe = CreateTabFrame();
        m_mgr.AddPane(tabframe,
                      ibAuiPaneInfo().Center().CaptionVisible(false));
        m_mgr.Update();

        tabMain = tabframe->m_tabs;
    }

    return tabMain;
}

std::vector<ibAuiTabCtrl*> ibAuiNotebook::GetAllTabCtrls()
{
    std::vector<ibAuiTabCtrl*> tabCtrls;
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        tabCtrls.push_back(tabframe->m_tabs);
    }

    if ( tabCtrls.empty() )
    {
        tabCtrls.push_back(GetMainTabCtrl());
    }

    return tabCtrls;
}

ibAuiNotebook::TabInfo ibAuiNotebook::FindTab(wxWindow* wnd) const
{
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        ibAuiTabCtrl* const tabCtrl = tabframe->m_tabs;

        const size_t page_count = tabCtrl->GetPageCount();
        for ( size_t i = 0; i < page_count; ++i )
        {
            ibAuiNotebookPage& page = tabCtrl->GetPage(i);
            if ( page.window == wnd )
                return { tabCtrl, static_cast<int>(i), &page };
        }
    }

    wxFAIL_MSG( "Window unexpectedly not found in any tab control" );

    return {};
}

// This overload is deprecated and kept only for compatibility.
bool ibAuiNotebook::FindTab(wxWindow* page, ibAuiTabCtrl** ctrl, int* idx) const
{
    const auto tabInfo = FindTab(page);
    if ( !tabInfo )
        return false;

    *ctrl = tabInfo.tabCtrl;
    *idx = tabInfo.tabIdx;

    return true;
}

int
ibAuiNotebook::GetDropIndex(const ibAuiNotebookPage& page_info,
                            ibAuiTabCtrl* dest_tabs,
                            const wxPoint& mouse_screen_pt) const
{
    int insert_idx = -1;

    wxPoint pt = dest_tabs->ScreenToClient(mouse_screen_pt);
    if (auto const targetInfo = dest_tabs->TabHitTest(pt))
    {
        insert_idx = targetInfo.pos;

        // Check that we don't try to insert a tab between tabs of
        // inappropriate kind, kinds must always remain in a (non-strict)
        // decreasing order.
        if ( insert_idx > 0 )
        {
            if ( dest_tabs->GetPage(insert_idx - 1).kind < page_info.kind )
                return wxNOT_FOUND;
        }

        if ( page_info.kind < dest_tabs->GetPage(insert_idx).kind )
            return wxNOT_FOUND;
    }
    else // Append the tab if there is no existing tab under it.
    {
        insert_idx = dest_tabs->GetPageCount();

        // When appending we need just a single test.
        if ( dest_tabs->GetPage(insert_idx - 1).kind < page_info.kind )
            return wxNOT_FOUND;
    }

    return insert_idx;
}

ibAuiNotebookPosition ibAuiNotebook::GetPagePosition(size_t page) const
{
    return FindTab(GetPage(page));
}

std::vector<size_t>
ibAuiNotebook::GetPagesInDisplayOrder(ibAuiTabCtrl* tabCtrl) const
{
    wxCHECK_MSG( tabCtrl, std::vector<size_t>(), "invalid tab control" );

    const size_t count = tabCtrl->GetPageCount();
    std::vector<size_t> pages(count);
    for ( size_t i = 0; i < count; ++i )
    {
        pages[i] = m_tabs.GetIdxFromWindow(tabCtrl->GetWindowFromIdx(i));
    }

    return pages;
}

void ibAuiNotebook::Split(size_t page, int direction)
{
    // get the page's window pointer
    wxWindow* wnd = GetPage(page);
    if (!wnd)
        return;

    // notebooks with 1 or less pages can't be split
    if (GetPageCount() < 2)
        return;

    // find out which tab control the page currently belongs to
    const auto srcTabInfo = FindTab(wnd);
    if ( !srcTabInfo )
        return;

    ibAuiTabCtrl* const src_tabs = srcTabInfo.tabCtrl;

    // create a new tab frame
    ibAuiTabFrame* new_tabs = CreateTabFrame(CalculateNewSplitSize());
    ibAuiTabCtrl* const dest_tabs = new_tabs->m_tabs;

    m_mgr.SplitPane(GetTabFrameFromTabCtrl(src_tabs), new_tabs, direction);

    // remove the page from the source tabs
    ibAuiNotebookPage page_info = *srcTabInfo.pageInfo;
    page_info.active = false;
    src_tabs->RemovePageAt(srcTabInfo.tabIdx);
    if (src_tabs->GetPageCount() > 0)
    {
        src_tabs->SetActivePage((size_t)0);
        src_tabs->DoUpdateActive();
    }


    // add the page to the destination tabs
    dest_tabs->InsertPage(page_info, 0);

    if (src_tabs->GetPageCount() == 0)
    {
        RemoveEmptyTabFrames();
    }

    DoSizing();
    dest_tabs->DoUpdateActive();

    // force the set selection function reset the selection
    m_curPage = wxNOT_FOUND;

    // set the active page to the one we just split off
    SetSelectionToPage(page_info);

    UpdateHintWindowSize();
}

void ibAuiNotebook::UnsplitAll()
{
    auto* const tabMain = GetMainTabCtrl();

    bool changed = false;
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        auto* const tab = static_cast<ibAuiTabFrame*>(pane.window)->m_tabs;

        // Don't move tabs from the main tab control to itself.
        if ( tab == tabMain )
            continue;

        while ( tab->GetPageCount() )
        {
            ibAuiNotebookPage info = tab->GetPage(0);
            info.active = false;

            tab->RemovePageAt(0);

            tabMain->AddPage(info);

            changed = true;
        }
    }

    if ( changed )
    {
        // We need to update the selection if the current page was in another
        // tab control before, so force a selection change to ensure that the
        // right page is shown.
        m_curPage = wxNOT_FOUND;
        int sel = tabMain->GetActivePage();
        if ( sel == wxNOT_FOUND )
        {
            // Not sure if this can actually happen, but fall back to the first
            // page if it does (note that we know that the main tab control is
            // not empty as we must have added a page to it above for "changed"
            // to be true).
            sel = 0;
        }

        ChangeSelection(sel);

        RemoveEmptyTabFrames();

        DoSizing();

        UpdateHintWindowSize();
    }
}

void ibAuiNotebook::OnSize(wxSizeEvent& evt)
{
    UpdateHintWindowSize();

    evt.Skip();
}

void ibAuiNotebook::OnTabClicked(ibAuiTabCtrl* ctrl, int tabIdx)
{
    wxWindow* const wnd = ctrl->GetWindowFromIdx(tabIdx);
    wxASSERT(wnd != nullptr);

    // clicking again on the tab shown gives the row of tabs the focus (SEAM vs wx: here, not in DoModifySelection)
    if (m_tabs.GetIdxFromWindow(wnd) == m_curPage)
    {
        if (FindFocus() != ctrl)
            ctrl->SetFocus();
        return;
    }

    // Note that this will generate ibEVT_AUINOTEBOOK_PAGE_CHANG{ING,ED} events.
    SetSelectionToWindow(wnd);
}

void ibAuiNotebook::OnTabBgDClick(ibAuiTabCtrl* ctrl)
{
    // select the tab ctrl which received the db click
    const int selection = ctrl->GetActivePage();
    if ( selection != wxNOT_FOUND )
    {
        if ( wxWindow* const wnd = ctrl->GetWindowFromIdx(selection) )
            SetSelectionToWindow(wnd);
    }

    // notify owner that the tabbar background has been double-clicked
    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_BG_DCLICK, m_windowId);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void ibAuiNotebook::OnTabBeginDrag(ibAuiTabCtrl* ctrl, int tabIdx)
{
    m_lastDropMovePos = -1;

    wxWindow* const wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_BEGIN_DRAG, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetOldSelection(e.GetSelection());
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void ibAuiNotebook::OnTabDragMotion(ibAuiTabCtrl* src_tabs, int src_idx)
{
    wxWindow* const wnd = src_tabs->GetWindowFromIdx(src_idx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_DRAG_MOTION, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetOldSelection(e.GetSelection());
    e.SetEventObject(this);
    ProcessWindowEvent(e);

    wxPoint screen_pt = ::wxGetMousePosition();
    wxPoint client_pt = ScreenToClient(screen_pt);

    ibAuiTabCtrl* const dest_tabs = GetTabCtrlFromPoint(client_pt);

    if (dest_tabs == src_tabs)
    {
        src_tabs->SetCursor(wxCursor(wxCURSOR_ARROW));

        // always hide the hint for inner-tabctrl drag
        m_mgr.HideHint();

        // if tab moving is not allowed, leave
        if (!(m_flags & ibAUI_NB_TAB_MOVE))
        {
            return;
        }

        wxPoint pt = dest_tabs->ScreenToClient(screen_pt);

        // When using multiple rows, allow dragging the tab to the space after
        // the last tab of the row too.
        int flags = ibAuiTabContainer::HitTest_Default;
        if (dest_tabs->IsFlagSet(ibAUI_NB_MULTILINE))
            flags |= ibAuiTabContainer::HitTest_AllowAfterTab;

        // this is an inner-tab drag/reposition
        if (auto const destTabInfo = dest_tabs->TabHitTest(pt, flags))
        {
            wxCHECK_RET( src_idx != -1, "Invalid source tab?" );

            int dest_idx = destTabInfo.pos;

            if ( src_idx == dest_idx )
            {
                // Reset the last drop position, the mouse has moved away from
                // the other tab, so now the user should be able to drag this
                // one there again.
                m_lastDropMovePos = -1;
                return;
            }

            const auto& src_page = src_tabs->GetPage(src_idx);

            // A tab can only be moved inside the group of tabs of the same
            // kind, as otherwise the tabs of the same kind wouldn't be grouped
            // together any longer.
            if ( dest_tabs->GetPage(dest_idx).kind != src_page.kind )
                return;

            // When dragging a smaller tab over the larger one, after moving
            // the tab into the new position, the same point can be now over a
            // different tab, which would result in another move of this tab as
            // soon as the mouse moves even by a single pixel. And then, of
            // course, it would move again, and so on. To avoid this, we need
            // to check if the mouse has moved from the tab over which it was
            // when the last drop occurred.
            if ( dest_idx == m_lastDropMovePos )
                return;

            if (dest_tabs->MovePage(src_page.window, dest_idx))
            {
                // Update the layout when using multiline tabs as it can change
                // depending on the tab order.
                if (dest_tabs->IsFlagSet(ibAUI_NB_MULTILINE))
                    dest_tabs->LayoutMultiLineTabs(dest_tabs);

                dest_tabs->SetActivePage((size_t)dest_idx);
                dest_tabs->DoUpdateActive();
            }

            m_lastDropMovePos = dest_tabs->TabHitTest(pt).pos;
        }

        return;
    }


    // if external drag is allowed, check if the tab is being dragged
    // over a different ibAuiNotebook control
    if (m_flags & ibAUI_NB_TAB_EXTERNAL_MOVE)
    {
        wxWindow* tab_ctrl = ::wxFindWindowAtPoint(screen_pt);

        // if we aren't over any window, stop here
        if (!tab_ctrl)
            return;

        // make sure we are not over the hint window
        if (!wxDynamicCast(tab_ctrl, wxFrame))
        {
            ibAuiTabCtrl* other_tabs = nullptr;
            while (tab_ctrl)
            {
                other_tabs = wxDynamicCast(tab_ctrl, ibAuiTabCtrl);
                if (other_tabs)
                    break;
                tab_ctrl = tab_ctrl->GetParent();
            }

            if (other_tabs)
            {
                ibAuiNotebook* nb = (ibAuiNotebook*)tab_ctrl->GetParent();

                if (nb != this)
                {
                    m_mgr.UpdateHint(other_tabs->GetHintScreenRect());
                    return;
                }
            }
        }
        else
        {
            if (!dest_tabs)
            {
                // we are either over a hint window, or not over a tab
                // window, and there is no where to drag to, so exit
                return;
            }
        }
    }


    // if there are less than two panes, split can't happen, so leave
    if (m_tabs.GetPageCount() < 2)
        return;

    // if tab moving is not allowed, leave
    if (!(m_flags & ibAUI_NB_TAB_SPLIT))
        return;


    if (src_tabs)
    {
        src_tabs->SetCursor(wxCursor(wxCURSOR_SIZING));
    }


    wxRect hintRect;
    if (dest_tabs)
    {
        if (src_tabs)
        {
            const auto& src_page = src_tabs->GetPage(src_idx);
            if (GetDropIndex(src_page, dest_tabs, screen_pt) == wxNOT_FOUND)
            {
                m_mgr.HideHint();
                return;
            }
        }

        hintRect = dest_tabs->GetHintScreenRect();
    }
    else
    {
        hintRect = m_mgr.CalculateHintRect(m_dummyWnd, client_pt);
    }

    m_mgr.UpdateHint(hintRect);
}



void ibAuiNotebook::OnTabEndDrag(ibAuiTabCtrl* src_tabs, int src_idx)
{
    wxWindow* src_page = src_tabs->GetWindowFromIdx(src_idx);

    m_mgr.HideHint();

    src_tabs->SetCursor(wxCursor(wxCURSOR_ARROW));

    // get the mouse position, which will be used to determine the drop point
    wxPoint mouse_screen_pt = ::wxGetMousePosition();
    wxPoint mouse_client_pt = ScreenToClient(mouse_screen_pt);



    // check for an external move
    if (m_flags & ibAUI_NB_TAB_EXTERNAL_MOVE)
    {
        wxWindow* tab_ctrl = ::wxFindWindowAtPoint(mouse_screen_pt);

        while (tab_ctrl)
        {
            if (wxDynamicCast(tab_ctrl, ibAuiTabCtrl))
                break;
            tab_ctrl = tab_ctrl->GetParent();
        }

        if (tab_ctrl)
        {
            ibAuiNotebook* nb = (ibAuiNotebook*)tab_ctrl->GetParent();

            if (nb != this)
            {
                // find out from the destination control
                // if it's ok to drop this tab here
                ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_ALLOW_DND, m_windowId);
                e.SetSelection(src_idx);
                e.SetOldSelection(e.GetSelection());
                e.SetEventObject(this);
                e.SetDragSource(this);
                e.Veto(); // dropping must be explicitly approved by control owner

                nb->ProcessWindowEvent(e);

                if (!e.IsAllowed())
                {
                    // no answer or negative answer
                    m_mgr.HideHint();
                    return;
                }

                // drop was allowed

                // Check that it's not an impossible parent relationship
                wxWindow* p = nb;
                while (p && !p->IsTopLevel())
                {
                    if (p == src_page)
                    {
                        return;
                    }
                    p = p->GetParent();
                }

                // get main index of the page
                int main_idx = m_tabs.GetIdxFromWindow(src_page);
                wxCHECK_RET( main_idx != wxNOT_FOUND, wxT("no source page?") );


                // make a copy of the page info
                ibAuiNotebookPage page_info = m_tabs.GetPage(main_idx);
                page_info.hover = false;

                // remove the page from the source notebook
                RemovePage(main_idx);


                // found out the insert idx
                ibAuiTabCtrl* dest_tabs = (ibAuiTabCtrl*)tab_ctrl;
                wxPoint pt = dest_tabs->ScreenToClient(mouse_screen_pt);

                int insert_idx = -1;
                if (auto const targetInfo = dest_tabs->TabHitTest(pt))
                {
                    insert_idx = targetInfo.pos;
                }


                // add the page to the new notebook: note that we always append
                // it to the end in logical order, as it's not clear what
                // should its position be (the alternative would be to insert
                // it before the target page, but it's not really clear if this
                // is really what we want)
                nb->InsertPageAt(page_info, nb->GetPageCount(),
                                 dest_tabs, insert_idx,
                                 true /* select */);

                // notify owner that the tab has been dragged
                ibAuiNotebookEvent e2(ibEVT_AUINOTEBOOK_DRAG_DONE, m_windowId);
                e2.SetSelection(src_idx);
                e2.SetOldSelection(src_idx);
                e2.SetEventObject(this);
                ProcessWindowEvent(e2);

                return;
            }
        }
    }




    // only perform a tab split if it's allowed
    ibAuiTabCtrl* dest_tabs = nullptr;

    if ((m_flags & ibAUI_NB_TAB_SPLIT) && m_tabs.GetPageCount() >= 2)
    {
        ibAuiNotebookPage page_info = src_tabs->GetPage(src_idx);

        // If the pointer is in an existing tab frame, do a tab insert
        wxWindow* hit_wnd = ::wxFindWindowAtPoint(mouse_screen_pt);
        ibAuiTabFrame* tab_frame = (ibAuiTabFrame*)GetTabFrameFromTabCtrl(hit_wnd);
        int insert_idx = -1;
        if (tab_frame)
        {
            dest_tabs = tab_frame->m_tabs;

            if (dest_tabs == src_tabs)
                return;

            insert_idx = GetDropIndex(page_info, dest_tabs, mouse_screen_pt);
            if ( insert_idx == wxNOT_FOUND )
                return;
        }
        else
        {
            wxRect rect = m_mgr.CalculateHintRect(m_dummyWnd,
                                                  mouse_client_pt);
            if (rect.IsEmpty())
            {
                // there is no suitable drop location here, exit out
                return;
            }

            // If there is no tabframe at all, create one
            ibAuiTabFrame* new_tabs = CreateTabFrame(CalculateNewSplitSize());
            m_mgr.SplitPane(GetTabFrameFromTabCtrl(src_tabs),
                            new_tabs,
                            wxBOTTOM,
                            mouse_client_pt);

            dest_tabs = new_tabs->m_tabs;
            insert_idx = 0;
        }



        // remove the page from the source tabs
        page_info.active = false;
        src_tabs->RemovePage(page_info.window);
        if (src_tabs->GetPageCount() > 0)
        {
            src_tabs->SetActivePage((size_t)0);
            src_tabs->DoUpdateActive();
        }



        // add the page to the destination tabs
        dest_tabs->InsertPage(page_info, insert_idx);

        if (src_tabs->GetPageCount() == 0)
        {
            RemoveEmptyTabFrames();
        }

        DoSizing();
        dest_tabs->DoUpdateActive();

        // force the set selection function reset the selection
        m_curPage = wxNOT_FOUND;

        // set the active page to the one we just split off
        SetSelectionToPage(page_info);

        UpdateHintWindowSize();
    }

    // notify owner that the tab has been dragged
    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_DRAG_DONE, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(src_page));
    e.SetOldSelection(e.GetSelection());
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}



void ibAuiNotebook::OnTabCancelDrag(ibAuiTabCtrl* ctrl, int tabIdx)
{
    m_mgr.HideHint();

    ctrl->SetCursor(wxCursor(wxCURSOR_ARROW));

    wxWindow* const wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_CANCEL_DRAG, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

ibAuiTabCtrl* ibAuiNotebook::GetTabCtrlFromPoint(const wxPoint& pt)
{
    // if we've just removed the last tab from the source
    // tab set, the remove the tab control completely
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        if (tabframe->m_tab_rect.Contains(pt))
            return tabframe->m_tabs;
    }

    return nullptr;
}

wxWindow* ibAuiNotebook::GetTabFrameFromTabCtrl(wxWindow* tab_ctrl)
{
    // if we've just removed the last tab from the source
    // tab set, the remove the tab control completely
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        if (tabframe->m_tabs == tab_ctrl)
        {
            return tabframe;
        }
    }

    return nullptr;
}

void ibAuiNotebook::RemoveEmptyTabFrames()
{
    // if we've just removed the last tab from the source
    // tab set, then remove the tab control completely

    // Make a copy of all panes because the original is modified when detaching
    // the pane in the loop.
    ibAuiPaneInfoArray all_panes = m_mgr.GetAllPanes();
    for ( const auto& pane : all_panes )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tab_frame = (ibAuiTabFrame*)pane.window;
        if (tab_frame->m_tabs->GetPageCount() == 0)
        {
            m_mgr.DetachPane(tab_frame);

            delete tab_frame;
        }
    }


    // check to see if there is still a center pane;
    // if there isn't, make a frame the center pane
    wxWindow* first_good = nullptr;
    bool center_found = false;
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;
        if (pane.dock_direction == ibAUI_DOCK_CENTRE)
            center_found = true;
        if (!first_good)
            first_good = pane.window;
    }

    if (!center_found && first_good)
    {
        m_mgr.GetPane(first_good).Centre();
    }

    if (!m_isBeingDeleted)
        m_mgr.Update();
}

void ibAuiNotebook::OnChildFocusNotebook(wxChildFocusEvent& evt)
{
    evt.Skip();

    // if we're dragging a tab, don't change the current selection.
    // This code prevents a bug that used to happen when the hint window
    // was hidden.  In the bug, the focus would return to the notebook
    // child, which would then enter this handler and call
    // SetSelection, which is not desired turn tab dragging.

    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;
        ibAuiTabFrame* tabframe = (ibAuiTabFrame*)pane.window;
        if (tabframe->m_tabs->IsDragging())
            return;
    }


    const auto findPageFromWindow = [this](wxWindow* win) -> wxWindow*
    {
        while ( win )
        {
            // pages have the notebook as the parent, so stop when we reach one
            // (and also stop in the impossible case of no parent at all)
            wxWindow* const parent = win->GetParent();
            if ( !parent )
                return nullptr;

            if ( parent == this )
                return win;

            win = parent;
        }

        return nullptr;
    };

    // Prefer the actual current focus: this event can be delayed and refer to a
    // page whose handler has already selected another page.
    wxWindow* win = findPageFromWindow(wxWindow::FindFocus());

    // But if we couldn't find the page containing the focus, use the window
    // that generated the event.
    if ( !win )
        win = findPageFromWindow(evt.GetWindow());

    if ( !win )
        return;

    // change the tab selection to this page
    int idx = m_tabs.GetIdxFromWindow(win);
    if (idx != -1 && idx != m_curPage)
    {
        SetSelection(idx);
    }
}

void ibAuiNotebook::OnNavigationKeyNotebook(wxNavigationKeyEvent& event)
{
    if ( event.IsWindowChange() ) {
        // change pages
        // FIXME: the problem with this is that if we have a split notebook,
        // we selection may go all over the place.
        AdvanceSelection(event.GetDirection());
    }
    else {
        // we get this event in 3 cases
        //
        // a) one of our pages might have generated it because the user TABbed
        // out from it in which case we should propagate the event upwards and
        // our parent will take care of setting the focus to prev/next sibling
        //
        // or
        //
        // b) the parent panel wants to give the focus to us so that we
        // forward it to our selected page. We can't deal with this in
        // OnSetFocus() because we don't know which direction the focus came
        // from in this case and so can't choose between setting the focus to
        // first or last panel child
        //
        // or
        //
        // c) we ourselves (see MSWTranslateMessage) generated the event
        //
        wxWindow * const parent = GetParent();

        // the wxObject* casts are required to avoid MinGW GCC 2.95.3 ICE
        const bool isFromParent = event.GetEventObject() == (wxObject*) parent;
        const bool isFromSelf = event.GetEventObject() == (wxObject*) this;

        if ( isFromParent || isFromSelf )
        {
            // no, it doesn't come from child, case (b) or (c): forward to a
            // page but only if direction is backwards (TAB) or from ourselves,
            if ( GetSelection() != wxNOT_FOUND &&
                    (!event.GetDirection() || isFromSelf) )
            {
                // so that the page knows that the event comes from it's parent
                // and is being propagated downwards
                event.SetEventObject(this);

                wxWindow *page = GetPage(GetSelection());
                if ( !page->ProcessWindowEvent(event) )
                {
                    page->SetFocus();
                }
                //else: page manages focus inside it itself
            }
            else // otherwise set the focus to the notebook itself
            {
                SetFocus();
            }
        }
        else
        {
            // it comes from our child, case (a), pass to the parent, but only
            // if the direction is forwards. Otherwise set the focus to the
            // notebook itself. The notebook is always the 'first' control of a
            // page.
            if ( !event.GetDirection() )
            {
                SetFocus();
            }
            else if ( parent )
            {
                event.SetCurrentFocus(this);
                parent->ProcessWindowEvent(event);
            }
        }
    }
}

bool ibAuiNotebook::OnTabButton(ibAuiTabCtrl* tabs, int tabIdx, int button_id)
{
    int selection = tabIdx;
    if (selection == -1 && button_id == ibAUI_BUTTON_CLOSE)
    {
        // if the close button is to the right, use the active
        // page selection to determine which page to close
        selection = tabs->GetActivePage();
    }

    // Notify the application about the button click.
    {
        ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_BUTTON, m_windowId);
        e.SetSelection(selection != -1
            ? m_tabs.GetIdxFromWindow(tabs->GetWindowFromIdx(selection))
            : wxNOT_FOUND
        );
        e.SetInt(button_id);
        e.SetEventObject(this);

        // Note that if the application handles this event without skipping
        // it, we don't do anything else, neither here nor in ibAuiTabCtrl:
        // this is compatible with the previous versions, in which handling
        // this event prevented our own handler, executed after the application
        // one, from running at all.
        if (ProcessWindowEvent(e))
            return true;
    }

    if (button_id == ibAUI_BUTTON_CLOSE)
    {
        if (selection != -1)
        {
            wxWindow* close_wnd = tabs->GetWindowFromIdx(selection);

            // ask owner if it's ok to close the tab
            ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_PAGE_CLOSE, m_windowId);
            const int idx = m_tabs.GetIdxFromWindow(close_wnd);
            e.SetSelection(idx);
            e.SetOldSelection(selection);
            e.SetEventObject(this);
            ProcessWindowEvent(e);
            if (!e.IsAllowed())
                return false;


#if wxUSE_MDI
            if (wxDynamicCast(close_wnd, ibAuiMDIChildFrame))
            {
                close_wnd->Close();
            }
            else
#endif
            {
                int main_idx = m_tabs.GetIdxFromWindow(close_wnd);
                wxCHECK_MSG( main_idx != wxNOT_FOUND, false,
                             wxT("no page to delete?") );

                DeletePage(main_idx);
            }

            // notify owner that the tab has been closed
            ibAuiNotebookEvent e2(ibEVT_AUINOTEBOOK_PAGE_CLOSED, m_windowId);
            e2.SetSelection(idx);
            e2.SetEventObject(this);
            ProcessWindowEvent(e2);
        }
    }
    else if (button_id == ibAUI_BUTTON_PIN)
    {
        // Note that we don't send any event specific to pinning the tab, the
        // generic button event above is the only notification for it, but such
        // event could be always added later if necessary.
        wxWindow* const wnd = tabs->GetWindowFromIdx(tabIdx);

        const auto idx = m_tabs.GetIdxFromWindow(wnd);

        ibAuiTabKind newKind = ibAuiTabKind::Locked;
        switch ( GetPageKind(idx) )
        {
            case ibAuiTabKind::Normal:
                newKind = ibAuiTabKind::Pinned;
                break;

            case ibAuiTabKind::Pinned:
                newKind = ibAuiTabKind::Normal;
                break;

            case ibAuiTabKind::Locked:
                // Locked tabs can't be pinned or unpinned.
                break;
        }

        wxCHECK_MSG(newKind != ibAuiTabKind::Locked, false,
                    "locked pages shouldn't have pin button");

        SetPageKind(idx, newKind);
    }

    return false;
}


void ibAuiNotebook::OnTabMiddleDown(ibAuiTabCtrl* ctrl, int tabIdx)
{
    wxWindow* const wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void ibAuiNotebook::OnTabMiddleUp(ibAuiTabCtrl* ctrl, int tabIdx)
{
    // if the ibAUI_NB_MIDDLE_CLICK_CLOSE is specified, middle
    // click should act like a tab close action.  However, first
    // give the owner an opportunity to handle the middle up event
    // for custom action

    wxWindow* const wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetEventObject(this);
    if (ProcessWindowEvent(e))
        return;
    if (!e.IsAllowed())
        return;

    // check if we are supposed to close on middle-up
    if ((m_flags & ibAUI_NB_MIDDLE_CLICK_CLOSE) == 0)
        return;

    // simulate the user pressing the close button on the tab
    OnTabButton(ctrl, tabIdx, ibAUI_BUTTON_CLOSE);
}

void ibAuiNotebook::OnTabRightDown(ibAuiTabCtrl* ctrl, int tabIdx)
{
    // Generate corresponding ibEVT_AUI event.
    wxWindow* wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void ibAuiNotebook::OnTabRightUp(ibAuiTabCtrl* ctrl, int tabIdx)
{
    // Generate corresponding ibEVT_AUI event.
    wxWindow* wnd = ctrl->GetWindowFromIdx(tabIdx);

    ibAuiNotebookEvent e(ibEVT_AUINOTEBOOK_TAB_RIGHT_UP, m_windowId);
    e.SetSelection(m_tabs.GetIdxFromWindow(wnd));
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

// Sets the normal font
void ibAuiNotebook::SetNormalFont(const wxFont& font)
{
    m_normalFont = font;
    m_tabs.SetNormalFont(font);
}

// Sets the selected tab font
void ibAuiNotebook::SetSelectedFont(const wxFont& font)
{
    m_selectedFont = font;
    m_tabs.SetSelectedFont(font);
}

// Sets the measuring font
void ibAuiNotebook::SetMeasuringFont(const wxFont& font)
{
    m_measuringFont = font;
    m_tabs.SetMeasuringFont(font);
}

// Sets the tab font
bool ibAuiNotebook::SetFont(const wxFont& font)
{
    wxControl::SetFont(font);

    wxFont normalFont(font);
    wxFont selectedFont(normalFont);
    selectedFont.SetWeight(wxFONTWEIGHT_BOLD);

    SetNormalFont(normalFont);
    SetSelectedFont(selectedFont);
    SetMeasuringFont(selectedFont);

    return true;
}

// Gets the tab control height
int ibAuiNotebook::GetTabCtrlHeight() const
{
    return m_tabCtrlHeight;
}

// Gets the height of the notebook for a given page height
int ibAuiNotebook::GetHeightForPageHeight(int pageHeight)
{
    UpdateTabCtrlHeight();

    int tabCtrlHeight = GetTabCtrlHeight();
    int decorHeight = 2;
    return tabCtrlHeight + pageHeight + decorHeight;
}

// Shows the window menu
bool ibAuiNotebook::ShowWindowMenu()
{
    ibAuiTabCtrl* tabCtrl = GetActiveTabCtrl();

    int idx = tabCtrl->ShowDropDown();

    if (idx != -1)
    {
        OnTabClicked(tabCtrl, idx);

        return true;
    }
    else
        return false;
}

void ibAuiNotebook::DoThaw()
{
    DoSizing();

    wxBookCtrlBase::DoThaw();
}

void ibAuiNotebook::SetPageSize (const wxSize& WXUNUSED(size))
{
    wxFAIL_MSG("Not implemented for ibAuiNotebook");
}

int ibAuiNotebook::HitTest (const wxPoint &pt, long *flags) const
{
    wxWindow *w = nullptr;
    long position = wxBK_HITTEST_NOWHERE;
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabFrame* tabframe = (ibAuiTabFrame*) pane.window;
        if (tabframe->m_tab_rect.Contains(pt))
        {
            wxPoint tabpos = tabframe->m_tabs->ScreenToClient(ClientToScreen(pt));
            w = tabframe->m_tabs->TabHitTest(tabpos).window;
            if (w)
                position = wxBK_HITTEST_ONITEM;
            break;
        }
        else if (tabframe->m_rect.Contains(pt))
        {
            w = tabframe->m_tabs->GetWindowFromIdx(tabframe->m_tabs->GetActivePage());
            if (w)
                position = wxBK_HITTEST_ONPAGE;
            break;
        }
    }

    if (flags)
        *flags = position;
    return w ? GetPageIndex(w) : wxNOT_FOUND;
}

int ibAuiNotebook::GetPageImage(size_t WXUNUSED(n)) const
{
    wxFAIL_MSG("Not implemented for ibAuiNotebook");
    return -1;
}

bool ibAuiNotebook::SetPageImage(size_t n, int imageId)
{
    return SetPageBitmap(n, GetBitmapBundle(imageId));
}

int ibAuiNotebook::ChangeSelection(size_t n)
{
    return DoModifySelection(n, false);
}

bool ibAuiNotebook::AddPage(wxWindow *page, const wxString &text, bool select,
                            int imageId)
{
    return AddPage(page, text, select, GetBitmapBundle(imageId));
}

bool ibAuiNotebook::DeleteAllPages()
{
    size_t count = GetPageCount();
    for(size_t i = 0; i < count; i++)
    {
        DeletePage(0);
    }
    return true;
}

bool ibAuiNotebook::InsertPage(size_t index, wxWindow *page,
                               const wxString &text, bool select,
                               int imageId)
{
    return InsertPage(index, page, text, select, GetBitmapBundle(imageId));
}

namespace
{

// Helper class to calculate the best size of a ibAuiNotebook
class ibAuiLayoutObject
{
public:
    enum DockDir
    {
        DockDir_Center,
        DockDir_Left,
        DockDir_Right,
        DockDir_Vertical,   // Merge elements from here vertically
        DockDir_Top,
        DockDir_Bottom,
        DockDir_None
    };

    ibAuiLayoutObject(const wxSize &size, const ibAuiPaneInfo &pInfo)
        : m_size(size)
    {
        m_pInfo = &pInfo;
        /*
            To speed up the sorting of the panes, the direction is mapped to a
            useful increasing value. This avoids complicated comparison of the
            enum values during the sort. The size calculation is done from the
            inner to the outermost direction. Therefore CENTER < LEFT/RIGHT <
            TOP/BOTTOM (It doesn't matter it LEFT or RIGHT is done first, as
            both extend the best size horizontally; the same applies for
            TOP/BOTTOM in vertical direction)
         */
        switch ( pInfo.dock_direction )
        {
            case ibAUI_DOCK_CENTER: m_dir = DockDir_Center; break;
            case ibAUI_DOCK_LEFT:   m_dir = DockDir_Left; break;
            case ibAUI_DOCK_RIGHT:  m_dir = DockDir_Right; break;
            case ibAUI_DOCK_TOP:    m_dir = DockDir_Top; break;
            case ibAUI_DOCK_BOTTOM: m_dir = DockDir_Bottom; break;
            default:                m_dir = DockDir_None;
        }
    }
    void MergeLayout(const ibAuiLayoutObject &lo2)
    {
        if ( this == &lo2 )
            return;

        bool mergeHorizontal;
        if ( m_pInfo->dock_layer != lo2.m_pInfo->dock_layer || m_dir != lo2.m_dir )
            mergeHorizontal = lo2.m_dir < DockDir_Vertical;
        else if ( m_pInfo->dock_row != lo2.m_pInfo->dock_row )
            mergeHorizontal = true;
        else
            mergeHorizontal = lo2.m_dir >= DockDir_Vertical;

        if ( mergeHorizontal )
        {
            m_size.x += lo2.m_size.x;
            if ( lo2.m_size.y > m_size.y )
                m_size.y = lo2.m_size.y;
        }
        else
        {
            if ( lo2.m_size.x > m_size.x )
                m_size.x = lo2.m_size.x;
            m_size.y += lo2.m_size.y;
        }
    }

    wxSize m_size;
    const ibAuiPaneInfo *m_pInfo;
    DockDir m_dir;

    /*
        As the calculation is done from the inner to the outermost pane, the
        panes are sorted in the following order: layer, direction, row,
        position.
     */
    bool operator<(const ibAuiLayoutObject& lo2) const
    {
        int diff = m_pInfo->dock_layer - lo2.m_pInfo->dock_layer;
        if ( diff )
            return diff < 0;
        diff = m_dir - lo2.m_dir;
        if ( diff )
            return diff < 0;
        diff = m_pInfo->dock_row - lo2.m_pInfo->dock_row;
        if ( diff )
            return diff < 0;
        return m_pInfo->dock_pos < lo2.m_pInfo->dock_pos;
    }
};

} // anonymous namespace

wxSize ibAuiNotebook::DoGetBestSize() const
{
    /*
        The best size of the ibAuiNotebook is a combination of all panes inside
        the object. To be able to efficiently  calculate the dimensions (i.e.
        without iterating over the panes multiple times) the panes need to be
        processed in a specific order. Therefore we need to collect them in the
        following variable which is sorted later on.
     */
    wxVector<ibAuiLayoutObject> layouts;
    const int tabHeight = GetTabCtrlHeight();
    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) || pane.IsFloating() )
            continue;

        const ibAuiTabFrame* tabframe = (ibAuiTabFrame*) pane.window;
        const ibAuiNotebookPageArray &pages = tabframe->m_tabs->GetPages();

        wxSize bestPageSize;
        for ( size_t pIdx = 0; pIdx < pages.GetCount(); pIdx++ )
            bestPageSize.IncTo(pages[pIdx].window->GetBestSize());

        bestPageSize.y += tabHeight;
        // Store the current pane with its largest window dimensions
        layouts.push_back(ibAuiLayoutObject(bestPageSize, pane));
    }

    if ( layouts.empty() )
        return wxSize(0, 0);

    wxVectorSort(layouts);

    /*
        The sizes of the panes are merged here. As the center pane is always at
        position 0 all sizes are merged there. As panes can be stacked using
        the dock_pos property, different positions are merged at the first
        (i.e. dock_pos = 0) element before being merged with the center pane.
     */
    size_t pos = 0;
    for ( size_t n = 1; n < layouts.size(); n++ )
    {
        if ( layouts[n].m_pInfo->dock_layer == layouts[pos].m_pInfo->dock_layer &&
             layouts[n].m_dir == layouts[pos].m_dir &&
             layouts[n].m_pInfo->dock_row == layouts[pos].m_pInfo->dock_row )
        {
            layouts[pos].MergeLayout(layouts[n]);
        }
        else
        {
            layouts[0].MergeLayout(layouts[pos]);
            pos = n;
        }
    }
    layouts[0].MergeLayout(layouts[pos]);

    return layouts[0].m_size;
}

int ibAuiNotebook::DoModifySelection(size_t n, bool events)
{
    wxWindow* wnd = m_tabs.GetWindowFromIdx(n);
    if (!wnd)
        return m_curPage;

    // don't change the page unless necessary
    // SEAM vs wx: and nothing else - the focus a click again on the tab gives it is the click's (OnTabClicked): given
    // here, a program choosing the page shown (a tab closed, ibAuiDocChildFrame::Destroy) took the focus out of the
    // page into the row of tabs
    if ((int)n == m_curPage)
        return m_curPage;

    bool vetoed = false;

    ibAuiNotebookEvent evt(ibEVT_AUINOTEBOOK_PAGE_CHANGING, m_windowId);

    if(events)
    {
        evt.SetSelection(n);
        evt.SetOldSelection(m_curPage);
        evt.SetEventObject(this);
        ProcessWindowEvent(evt);
        vetoed = !evt.IsAllowed();
    }

    if (!vetoed)
    {
        int old_curpage = m_curPage;
        m_curPage = n;

        if ( const auto tabInfo = FindTab(wnd) )
        {
            ibAuiTabCtrl* const ctrl = tabInfo.tabCtrl;

            ctrl->SetActivePage(tabInfo.tabIdx);
            DoSizing();
            ctrl->DoShowTab(tabInfo.tabIdx);

            // set fonts
            for ( const auto& pane : m_mgr.GetAllPanes() )
            {
                if ( IsDummyPane(pane) )
                    continue;
                ibAuiTabCtrl* tabctrl = ((ibAuiTabFrame*)pane.window)->m_tabs;
                if (tabctrl != ctrl)
                    tabctrl->SetSelectedFont(m_normalFont);
                else
                    tabctrl->SetSelectedFont(m_selectedFont);
                tabctrl->Refresh();
            }

            // Set the focus to the page if we're not currently focused on the tab.
            // This is Firefox-like behaviour.
            if (wnd->IsShownOnScreen() && FindFocus() != ctrl)
                wnd->SetFocus();

            // program allows the page change
            if(events)
            {
                evt.SetEventType(ibEVT_AUINOTEBOOK_PAGE_CHANGED);
                (void)ProcessWindowEvent(evt);
            }

            return old_curpage;
        }
    }

    return m_curPage;
}

void ibAuiTabCtrl::SetHoverTab(wxWindow* wnd)
{
    bool hoverChanged = false;

    for ( auto& page : m_pages )
    {
        bool oldHover = page.hover;
        page.hover = (page.window == wnd);
        if ( oldHover != page.hover )
            hoverChanged = true;
    }

    if ( hoverChanged )
    {
        Refresh();
        Update();
    }
}

// ----------------------------------------------------------------------------
// Layout serialization
// ----------------------------------------------------------------------------

void
ibAuiNotebook::SaveLayout(const wxString& name,
                          ibAuiBookSerializer& serializer) const
{
    serializer.BeforeSaveNotebook(name);

    for ( const auto& pane : m_mgr.GetAllPanes() )
    {
        if ( IsDummyPane(pane) )
            continue;

        ibAuiTabLayoutInfo tab;
        m_mgr.CopyDockLayoutFrom(tab, pane);

        const ibAuiTabCtrl* const
            tabCtrl = static_cast<ibAuiTabFrame*>(pane.window)->m_tabs;

        auto* const activePage =
            tabCtrl->GetWindowFromIdx(tabCtrl->GetActivePage());
        tab.active = m_tabs.GetIdxFromWindow(activePage);

        // As an optimization, don't bother with saving the pages order for the
        // main control if it hasn't been changed from the default.
        bool mustSavePages = false;
        if ( tab.dock_direction != ibAUI_DOCK_CENTER )
        {
            // The non-main tab controls can't possibly have all the pages, so
            // we always have to save the indices of the ones they contain.
            mustSavePages = true;
        }
        else if ( tabCtrl->GetPageCount() != GetPageCount() )
        {
            // If the main control doesn't have all the pages, we need to save
            // them for it as well.
            mustSavePages = true;
        }

        std::vector<int> pages;
        int n = 0;
        for ( const auto& page : tabCtrl->GetPages() )
        {
            // This is inefficient for many pages, we should consider using a
            // hash map indexed by the window pointer if this proves to be a
            // problem in practice.
            const int idx = m_tabs.GetIdxFromWindow(page.window);
            if ( idx != n++ )
            {
                // And if the pages are not in order, we must save them too.
                mustSavePages = true;
            }

            pages.push_back(idx);

            // Also remember if this page is pinned.
            switch ( page.kind )
            {
                case ibAuiTabKind::Normal:
                    // Nothing special to do.
                    break;

                case ibAuiTabKind::Pinned:
                    tab.pinned.push_back(idx);
                    break;

                case ibAuiTabKind::Locked:
                    // We don't store locked pages because their status can't
                    // be changed by the user, so it would be pointless to save
                    // and restore them.
                    break;
            }
        }

        // But if none of the conditions above is true, we can avoid saving
        // them, which also allows us not to remove and re-add them when
        // restoring later, see LoadLayout() below.
        if ( mustSavePages )
            tab.pages = std::move(pages);

        serializer.SaveNotebookTabControl(tab);
    }

    serializer.AfterSaveNotebook();
}

void
ibAuiNotebook::LoadLayout(const wxString& name,
                          ibAuiBookDeserializer& deserializer)
{
    const auto tabs = deserializer.LoadNotebookTabs(name);

    if ( tabs.empty() )
        return;

    // Remove any existing tabs before adding new ones.
    UnsplitAll();

    // Get the only remaining tab control.
    ibAuiTabCtrl* const tabMain = GetMainTabCtrl();

    // If we don't have anything saved and the pages are in the default order
    // we may not have to do anything at all.
    bool useExistingPages = false;

    // Reset the state of all pinned pages to normal, as this state will be
    // restored from the deserialized data below.
    //
    // Note that we do not do this for the locked pages, as their state can't
    // be changed by the user and so it's not necessarily to save nor restore
    // it.
    for ( auto& page : m_tabs.GetPages() )
    {
        if ( page.kind == ibAuiTabKind::Pinned )
            const_cast<ibAuiNotebookPage&>(page).kind = ibAuiTabKind::Normal;
    }

    // This set will contain all the pages that should be pinned.
    std::unordered_set<int> pinned;

    // Remember the active page in the main tab control if we change it.
    const wxWindow* activeInMainTab = nullptr;

    // Keep track of pages we've already added to some tab control: even if the
    // deserialized data is somehow incorrect and duplicates the page indices,
    // we don't want to try to have the same page in more than one tab control.
    std::unordered_set<int> addedPages;
    const int pageCount = m_tabs.GetPageCount();

    for ( const auto& tab : tabs )
    {
        ibAuiTabCtrl* tabCtrl = nullptr;

        // We'll deal with the pinned pages outside of this loop below, after
        // all pages are in their correct places, for now just remember them.
        pinned.insert(tab.pinned.begin(), tab.pinned.end());

        // The pages pointer will be set to point to either tab.pages or
        // pageDefault in which case it will also be filled.
        std::vector<int> pagesDefault;
        const std::vector<int>* pages = nullptr;

        // We don't need to create a new pane for the main, central tab control,
        // but we still need to order its tabs correctly.
        if ( tab.dock_direction == ibAUI_DOCK_CENTER )
        {
            // Special case: if the pages vector is empty check if we're using
            // the default order, in which case we can avoid doing anything if
            // the pages are already in the same order (which is a common case).
            if ( tab.pages.empty() )
            {
                useExistingPages = true;

                for ( int i = 0; i < pageCount; ++i )
                {
                    if ( tabMain->GetWindowFromIdx(i) != m_tabs.GetWindowFromIdx(i) )
                    {
                        useExistingPages = false;
                        break;
                    }
                }

                if ( useExistingPages )
                {
                    // All pages are in the main tab in the default order
                    // already, so we don't have anything to do except
                    // restoring the active page -- which is in this case the
                    // same as selection (tab indices == notebook indices).
                    SetSelection(tab.active);
                    break;
                }

                // The pages had been reordered, restore the default order when
                // re-adding them below.
                for ( int i = 0; i < pageCount; ++i )
                    pagesDefault.push_back(i);

                pages = &pagesDefault;
            }
            else
            {
                // Just use the specified pages.
                pages = &tab.pages;
            }

            // In any case, we must remove all pages currently in this tab as
            // they will be re-added.
            tabMain->RemoveAll();

            tabCtrl = tabMain;
        }
        else // Non-central tab control.
        {
            // In this case, we must have some pages, but if we somehow don't,
            // just skip adding the tab control entirely.
            if ( tab.pages.empty() )
                continue;

            // Re-add using the saved layout.
            ibAuiPaneInfo pane;
            m_mgr.CopyDockLayoutTo(tab, pane);

            // Our panes never show caption.
            pane.CaptionVisible(false);

            ibAuiTabFrame* tabframe = CreateTabFrame();
            m_mgr.AddPane(tabframe, pane);

            tabCtrl = tabframe->m_tabs;
            pages = &tab.pages;
        }

        // In any case, add the pages that this tab control had before to it.
        //
        // There is one extra complication: we must ensure that any locked
        // pages still come first, even if the deserializer positioned them
        // wrongly, because this is an invariant of ibAuiNotebook and has to be
        // respected. So instead of adding the pages directly, collect them in
        // this vector first.
        std::vector<int> pagesToAdd;
        pagesToAdd.reserve(pages->size());

        int lockedCount = 0;
        for ( auto page : *pages )
        {
            // We just silently ignore invalid or duplicate page indices
            // here, because it doesn't seem right for them to result in a
            // fatal error and any warnings would be just annoying.
            if ( page >= pageCount )
                continue;

            if ( !addedPages.insert(page).second )
                continue;

            if ( m_tabs.GetPage(page).kind == ibAuiTabKind::Locked )
            {
                // Locked pages must always come first, so insert them at the
                // beginning (but keep their original order).
                pagesToAdd.insert(pagesToAdd.begin() + lockedCount++, page);
            }
            else
            {
                pagesToAdd.push_back(page);
            }
        }

        // In the degenerate case when all pages are invalid, pagesToAdd may be
        // empty here, but we can still execute the rest of the code below, it
        // just won't do anything.

        const wxWindow* activeWindow = nullptr;
        for ( auto page : pagesToAdd )
        {
            const auto& info = m_tabs.GetPage(page);

            if ( page == tab.active )
                activeWindow = info.window;

            tabCtrl->AddPage(info);
        }

        if ( !activeWindow )
        {
            // We must have some active page, so make the first one active if
            // the deserialized data didn't define a valid active page.
            activeWindow = tabCtrl->GetWindowFromIdx(0);
        }

        tabCtrl->SetActivePage(activeWindow);

        if ( tabCtrl == tabMain )
        {
            // Remember it to set the selection to it below.
            activeInMainTab = activeWindow;
        }
    }

    // Check if there were any existing pages not added to any tab control.
    if ( !useExistingPages && wxSsize(addedPages) < pageCount )
    {
        // Use a stack here to remove the pages from the end below.
        std::stack<int> toRemove;

        for ( int i = 0; i < pageCount; ++i )
        {
            if ( addedPages.count(i) == 0 )
            {
                ibAuiTabCtrl* tabCtrl = tabMain;
                int tabIndex = wxNOT_FOUND;
                if ( deserializer.HandleOrphanedPage(*this, i,
                                                     &tabCtrl, &tabIndex) )
                {
                    // Try not to crash even if the deserializer implements
                    // HandleOrphanedPage() incorrectly.
                    if ( !tabCtrl )
                    {
                        wxFAIL_MSG
                        (
                            "HandleOrphanedPage() can't return null tab control"
                        );

                        tabCtrl = tabMain;
                    }

                    const int tabCount = tabCtrl->GetPageCount();
                    if ( tabIndex != wxNOT_FOUND && tabIndex >= tabCount )
                    {
                        wxFAIL_MSG
                        (
                            "HandleOrphanedPage() must return valid tab index"
                        );

                        tabIndex = wxNOT_FOUND;
                    }

                    if ( tabIndex == wxNOT_FOUND )
                        tabIndex = tabCount;

                    tabCtrl->InsertPage(m_tabs.GetPage(i), tabIndex);
                }
                else // Remove this page.
                {
                    toRemove.push(i);
                }
            }
        }

        while ( !toRemove.empty() )
        {
            // Note that we shouldn't call DoRemovePage() because it supposes
            // that the page is in some tab control and does nothing if this is
            // not the case.
            m_tabs.RemovePageAt(toRemove.top());

            toRemove.pop();
        }

        RemoveEmptyTabFrames();
    }

    // Now deal with the pinned pages: we must ensure that their positions are
    // consistent, i.e. there are no non-pinned pages before a pinned one in
    // any tab control.
    if ( !pinned.empty() )
    {
        for ( auto tabCtrl : GetAllTabCtrls() )
        {
            bool stop = false;
            for ( auto n : GetPagesInDisplayOrder(tabCtrl) )
            {
                auto& page = m_tabs.GetPage(n);
                switch ( page.kind )
                {
                    case ibAuiTabKind::Normal:
                        if ( pinned.count(n) )
                        {
                            // Make this page pinned. We don't need to call
                            // SetPageKind() for this, as it's already in the
                            // right place, just update its kind directly.
                            page.kind = ibAuiTabKind::Pinned;

                            int idx = tabCtrl->GetIdxFromWindow(page.window);
                            if ( idx == wxNOT_FOUND )
                            {
                                // This would be a logic error in this code.
                                wxFAIL_MSG("page not found in tab control");
                                break;
                            }

                            tabCtrl->GetPage(idx).kind = ibAuiTabKind::Pinned;
                        }
                        else
                        {
                            // There can be no pinned pages after the first
                            // normal one, so there is no point in continuing:
                            // even if any other pages were marked as pinned in
                            // the deserialized data, we would just ignore them
                            // anyhow.
                            stop = true;
                        }
                        break;

                    case ibAuiTabKind::Pinned:
                        // This would indicate a logic error in this function,
                        // as we unpinned all the pages at the start of it.
                        wxFAIL_MSG("all pages should be unpinned");
                        break;

                    case ibAuiTabKind::Locked:
                        // We can't change the kind of a locked page, so either
                        // it is before the pinned pages or data is invalid,
                        // leave it as is in any case.
                        break;
                }

                if ( stop )
                    break;
            }
        }
    }

    // Update the active pages visibility in all tab controls after adding and
    // removing all the pages.
    for ( auto tabCtrl : GetAllTabCtrls() )
    {
        tabCtrl->DoUpdateActive();
    }

    // We don't save information about the currently focused tab control, so
    // always make the main one active after loading the layout by setting the
    // selected page to the page active in it (if there is no such page, it
    // means we're reusing the existing pages and so don't need to do anything).
    if ( activeInMainTab )
        m_curPage = m_tabs.GetIdxFromWindow(activeInMainTab);

    UpdateHintWindowSize();

    m_mgr.Update();
}

