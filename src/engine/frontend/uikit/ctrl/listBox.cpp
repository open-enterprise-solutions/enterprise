// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/listbox.cpp
// Purpose:     ibListBox implementation
// Author:      Vadim Zeitlin
// Created:     30.08.00
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

#include "frontend/uikit/ctrl/listBox.h"
#include "frontend/uikit/window.h"


#if wxUSE_LISTBOX

#ifndef WX_PRECOMP
    #include <wx/log.h>

    #include <wx/dcclient.h>
    #include <wx/listbox.h>
    #include <wx/validate.h>
#endif

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"

// ----------------------------------------------------------------------------
// ibStdListboxInputHandler: handles mouse and kbd in a single or multi
// selection listbox
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdListboxInputHandler : public ibStdInputHandler
{
public:
    // if pressing the mouse button in a multiselection listbox should toggle
    // the item under mouse immediately, then specify true as the second
    // parameter (this is the standard behaviour, under GTK the item is toggled
    // only when the mouse is released in the multi selection listbox)
    ibStdListboxInputHandler(ibInputHandler *inphand,
                             bool toggleOnPressAlways = true);

    // base class methods
    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed);
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event);
    virtual bool HandleMouseMove(ibInputConsumer *consumer,
                                 const wxMouseEvent& event);

protected:
    int HitTest(const ibListBox *listbox, const wxMouseEvent& event);

    bool IsValidIndex(const ibListBox *listbox, int item);

    // init m_btnCapture and m_actionMouse
    ibControlAction SetupCapture(ibListBox *lbox,
                                 const wxMouseEvent& event,
                                 int item);

    ibRenderer *m_renderer;

    // the button which initiated the mouse capture (currently 0 or 1)
    int m_btnCapture;

    // the action to perform when the mouse moves while we capture it
    ibControlAction m_actionMouse;

    // the ctor parameter toggleOnPressAlways (see comments near it)
    bool m_toggleOnPressAlways;

    // do we track the mouse outside the window when it is captured?
    bool m_trackMouseOutside;
};

// ============================================================================
// implementation of ibListBox
// ============================================================================

wxBEGIN_EVENT_TABLE(ibListBox, ibControl)
    EVT_SIZE(ibListBox::OnSize)
wxEND_EVENT_TABLE()

// ----------------------------------------------------------------------------
// construction
// ----------------------------------------------------------------------------

void ibListBox::Init()
{
    // will be calculated later when needed
    m_lineHeight = 0;
    m_itemsPerPage = 0;
    m_maxWidth = 0;
    m_scrollRangeY = 0;
    m_maxWidthItem = -1;
    m_strings.unsorted = nullptr;

    // no items hence no current item
    m_current = -1;
    m_selAnchor = -1;
    m_currentChanged = false;

    // no need to update anything initially
    m_updateCount = 0;

    // no scrollbars to show nor update
    m_updateScrollbarX =
    m_showScrollbarX =
    m_updateScrollbarY =
    m_showScrollbarY = false;
    m_inputHandlerType = ibINP_HANDLER_LISTBOX;
}

ibListBox::ibListBox(wxWindow *parent,
                     wxWindowID id,
                     const wxPoint &pos,
                     const wxSize &size,
                     const wxArrayString& choices,
                     long style,
                     const wxValidator& validator,
                     const wxString &name)
          :wxScrollHelper(this)
{
    Init();

    Create(parent, id, pos, size, choices, style, validator, name);
}

bool ibListBox::Create(wxWindow *parent,
                       wxWindowID id,
                       const wxPoint &pos,
                       const wxSize &size,
                       const wxArrayString& choices,
                       long style,
                       const wxValidator& validator,
                       const wxString &name)
{
    wxCArrayString chs(choices);

    return Create(parent, id, pos, size, chs.GetCount(), chs.GetStrings(),
                  style, validator, name);
}

bool ibListBox::Create(wxWindow *parent,
                       wxWindowID id,
                       const wxPoint &pos,
                       const wxSize &size,
                       int n,
                       const wxString choices[],
                       long style,
                       const wxValidator& validator,
                       const wxString &name)
{
    // for compatibility accept both the new and old styles - they mean the
    // same thing for us
    if ( style & wxLB_ALWAYS_SB )
        style |= wxALWAYS_SHOW_SB;

    // if we have neither multiple nor extended flag, we must have the
    // single selection listbox
    if ( !(style & (wxLB_MULTIPLE | wxLB_EXTENDED)) )
        style |= wxLB_SINGLE;

#if wxUSE_TWO_WINDOWS
    style |=  wxVSCROLL|wxHSCROLL;
    if ((style & wxBORDER_MASK) == 0)
        style |= wxBORDER_SUNKEN;
#endif

    if ( !ibControl::Create(parent, id, pos, size, style,
                            validator, name) )
        return false;

    if ( IsSorted() )
        m_strings.sorted = new wxSortedArrayString(wxDictionaryStringSortAscending);
    else
        m_strings.unsorted = new wxArrayString;

    Set(n, choices);

    SetInitialSize(size);

    CreateInputHandler(m_inputHandlerType);

    return true;
}

ibListBox::~ibListBox()
{
    // call this just to free the client data -- and avoid leaking memory
    Clear();

    if ( IsSorted() )
        delete m_strings.sorted;
    else
        delete m_strings.unsorted;

    m_strings.sorted = nullptr;
}

// ----------------------------------------------------------------------------
// accessing strings
// ----------------------------------------------------------------------------

unsigned int ibListBox::GetCount() const
{
    return IsSorted() ? m_strings.sorted->size()
                      : m_strings.unsorted->size();
}

wxString ibListBox::GetString(unsigned int n) const
{
    // SEAM vs univ: guard like the native ports do — univ trusted callers
    if ( n >= GetCount() )
        return wxEmptyString;

    return IsSorted() ? m_strings.sorted->Item(n)
                      : m_strings.unsorted->Item(n);
}

int ibListBox::FindString(const wxString& s, bool bCase) const
{
    return IsSorted() ? m_strings.sorted->Index(s, bCase)
                      : m_strings.unsorted->Index(s, bCase);
}

// ----------------------------------------------------------------------------
// adding/inserting strings
// ----------------------------------------------------------------------------

int ibListBox::DoInsertItems(const wxArrayStringsAdapter& items,
                             unsigned int pos,
                             void **clientData,
                             wxClientDataType type)
{
    int idx = wxNOT_FOUND;

    const unsigned int numItems = items.GetCount();
    for ( unsigned int i = 0; i < numItems; ++i )
    {
        const wxString& item = items[i];
        idx = IsSorted() ? m_strings.sorted->Add(item)
                         : (m_strings.unsorted->Insert(item, pos), pos++);

        m_itemsClientData.Insert(nullptr, idx);
        AssignNewItemClientData(idx, clientData, i, type);

        // call the ibCheckListBox hook
        OnItemInserted(idx);
    }

    // the number of items has changed so we might have to show the scrollbar
    m_updateScrollbarY = true;

    // the max width also might have changed - just recalculate it instead of
    // keeping track of it here, this is probably more efficient for a typical
    // use pattern
    RefreshHorzScrollbar();

    // note that we have to refresh all the items after the ones we inserted,
    // not just these items
    RefreshFromItemToEnd(pos);

    return idx;
}

void ibListBox::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET( !IsSorted(), wxT("can't set string in sorted listbox") );

    if ( IsSorted() )
        (*m_strings.sorted)[n] = s;
    else
        (*m_strings.unsorted)[n] = s;

    if ( HasHorzScrollbar() )
    {
        // we need to update m_maxWidth as changing the string may cause the
        // horz scrollbar [dis]appear
        wxCoord width;

        GetTextExtent(s, &width, nullptr);

        // it might have increased if the new string is long
        if ( width > m_maxWidth )
        {
            m_maxWidth = width;
            m_maxWidthItem = n;
            m_updateScrollbarX = true;
        }
        // or also decreased if the old string was the longest one
        else if ( n == (unsigned int)m_maxWidthItem )
        {
            RefreshHorzScrollbar();
        }
    }

    RefreshItem(n);
}

// ----------------------------------------------------------------------------
// removing strings
// ----------------------------------------------------------------------------

void ibListBox::DoClear()
{
    if ( IsSorted() )
        m_strings.sorted->Clear();
    else
        m_strings.unsorted->Clear();

    m_itemsClientData.Clear();
    m_selections.Clear();

    m_current = -1;

    m_updateScrollbarY = true;

    RefreshHorzScrollbar();

    RefreshAll();
}

void ibListBox::DoDeleteOneItem(unsigned int n)
{
    wxCHECK_RET( IsValid(n),
                 wxT("invalid index in ibListBox::Delete") );

    // do it before removing the index as otherwise the last item will not be
    // refreshed (as GetCount() will be decremented)
    RefreshFromItemToEnd(n);

    if ( IsSorted() )
        m_strings.sorted->RemoveAt(n);
    else
        m_strings.unsorted->RemoveAt(n);

    m_itemsClientData.RemoveAt(n);

    // when the item disappears we must not keep using its index
    if ( (int)n == m_current )
    {
        m_current = -1;
    }
    else if ( (int)n < m_current )
    {
        m_current--;
    }
    //else: current item may stay

    // update the selections array: the indices of all selected items after
    // the one being deleted must change and the item itself just be removed
    int index = wxNOT_FOUND;
    unsigned int count = m_selections.GetCount();
    for ( unsigned int item = 0; item < count; item++ )
    {
        if ( m_selections[item] == (int)n )
        {
            // remember to delete it later
            index = item;
        }
        else if ( m_selections[item] > (int)n )
        {
            // to account for the index shift
            m_selections[item]--;
        }
        //else: nothing changed for this one
    }

    if ( index != wxNOT_FOUND )
    {
        m_selections.RemoveAt(index);
    }

    // the number of items has changed, hence the scrollbar may disappear
    m_updateScrollbarY = true;

    // finally, if the longest item was deleted the scrollbar may disappear
    if ( (int)n == m_maxWidthItem )
    {
        RefreshHorzScrollbar();
    }
}

// ----------------------------------------------------------------------------
// client data handling
// ----------------------------------------------------------------------------

void ibListBox::DoSetItemClientData(unsigned int n, void* clientData)
{
    m_itemsClientData[n] = clientData;
}

void *ibListBox::DoGetItemClientData(unsigned int n) const
{
    return m_itemsClientData[n];
}

// ----------------------------------------------------------------------------
// selection
// ----------------------------------------------------------------------------

void ibListBox::DoSetSelection(int n, bool select)
{
    if ( select )
    {
        if ( n == wxNOT_FOUND )
        {
            // if is wxNOT_FOUND, just deselect all like other posts
            // selecting wxNOT_FOUND is documented to deselect all items
            DeselectAll();
            return;
        }
        else if ( m_selections.Index(n) == wxNOT_FOUND )
        {
            if ( !HasMultipleSelection() )
            {
                // selecting an item in a single selection listbox deselects
                // all the others
                DeselectAll();
            }

            m_selections.Add(n);

            RefreshItem(n);
        }
        //else: already selected
    }
    else // unselect
    {
        int index = m_selections.Index(n);
        if ( index != wxNOT_FOUND )
        {
            m_selections.RemoveAt(index);

            RefreshItem(n);
        }
        //else: not selected
    }

    // sanity check: a single selection listbox can't have more than one item
    // selected
    wxASSERT_MSG( HasMultipleSelection() || (m_selections.GetCount() < 2),
                  wxT("multiple selected items in single selection lbox?") );

    if ( select )
    {
        // the newly selected item becomes the current one
        SetCurrentItem(n);
    }
}

int ibListBox::GetSelection() const
{
    wxCHECK_MSG( !HasMultipleSelection(), wxNOT_FOUND,
                 wxT("use ibListBox::GetSelections for ths listbox") );

    return m_selections.IsEmpty() ? wxNOT_FOUND : m_selections[0];
}

static int wxCMPFUNC_CONV wxCompareInts(int *n, int *m)
{
    return *n - *m;
}

int ibListBox::GetSelections(wxArrayInt& selections) const
{
    // always return sorted array to the user
    selections = m_selections;
    unsigned int count = m_selections.GetCount();

    // don't call sort on an empty array
    if ( count )
    {
        selections.Sort(wxCompareInts);
    }

    return count;
}

// ----------------------------------------------------------------------------
// refresh logic: we use delayed refreshing which allows to avoid multiple
// refreshes (and hence flicker) in case when several listbox items are
// added/deleted/changed subsequently
// ----------------------------------------------------------------------------

void ibListBox::RefreshFromItemToEnd(int from)
{
    RefreshItems(from, GetCount() - from);
}

void ibListBox::RefreshItems(int from, int count)
{
    switch ( m_updateCount )
    {
        case 0:
            m_updateFrom = from;
            m_updateCount = count;
            break;

        case -1:
            // we refresh everything anyhow
            break;

        default:
            // add these items to the others which we have to refresh
            if ( m_updateFrom < from )
            {
                count += from - m_updateFrom;
                if ( m_updateCount < count )
                    m_updateCount = count;
            }
            else // m_updateFrom >= from
            {
                int updateLast = wxMax(m_updateFrom + m_updateCount,
                                       from + count);
                m_updateFrom = from;
                m_updateCount = updateLast - m_updateFrom;
            }
    }
}

void ibListBox::RefreshItem(int n)
{
    switch ( m_updateCount )
    {
        case 0:
            // refresh this item only
            m_updateFrom = n;
            m_updateCount = 1;
            break;

        case -1:
            // we refresh everything anyhow
            break;

        default:
            // add this item to the others which we have to refresh
            if ( m_updateFrom < n )
            {
                if ( m_updateCount < n - m_updateFrom + 1 )
                    m_updateCount = n - m_updateFrom + 1;
            }
            else // n <= m_updateFrom
            {
                m_updateCount += m_updateFrom - n;
                m_updateFrom = n;
            }
    }
}

void ibListBox::RefreshAll()
{
    m_updateCount = -1;
}

void ibListBox::RefreshHorzScrollbar()
{
    m_maxWidth = 0; // recalculate it
    m_updateScrollbarX = true;
}

void ibListBox::UpdateScrollbars()
{
    wxSize size = GetClientSize();

    // is our height enough to show all items?
    unsigned int nLines = GetCount();
    wxCoord lineHeight = GetLineHeight();
    bool showScrollbarY = (int)nLines*lineHeight > size.y;

    // check the width too if required
    wxCoord charWidth, maxWidth;
    bool showScrollbarX;
    if ( HasHorzScrollbar() )
    {
        charWidth = GetCharWidth();
        maxWidth = GetMaxWidth();
        showScrollbarX = maxWidth > size.x;
    }
    else // never show it
    {
        charWidth = maxWidth = 0;
        showScrollbarX = false;
    }

    // what should be the scrollbar range now?
    int scrollRangeX = showScrollbarX
                        ? (maxWidth + charWidth - 1) / charWidth + 2 // FIXME
                        : 0;
    int scrollRangeY = showScrollbarY
                        ? nLines +
                            (size.y % lineHeight + lineHeight - 1) / lineHeight
                        : 0;

    // reset scrollbars if something changed: either the visibility status
    // or the range of a scrollbar which is shown
    if ( (showScrollbarY != m_showScrollbarY) ||
         (showScrollbarX != m_showScrollbarX) ||
         (showScrollbarY && (scrollRangeY != m_scrollRangeY)) ||
         (showScrollbarX && (scrollRangeX != m_scrollRangeX)) )
    {
        int x, y;
        GetViewStart(&x, &y);
        SetScrollbars(charWidth, lineHeight,
                      scrollRangeX, scrollRangeY,
                      x, y);

        m_showScrollbarX = showScrollbarX;
        m_showScrollbarY = showScrollbarY;

        m_scrollRangeX = scrollRangeX;
        m_scrollRangeY = scrollRangeY;
    }
}

void ibListBox::UpdateItems()
{
    // only refresh the items which must be refreshed
    if ( m_updateCount == -1 )
    {
        // refresh all
        wxLogTrace(wxT("listbox"), wxT("Refreshing all"));

        Refresh();
    }
    else
    {
        wxSize size = GetClientSize();
        wxRect rect;
        rect.width = size.x;
        rect.height = size.y;
        rect.y += m_updateFrom*GetLineHeight();
        rect.height = m_updateCount*GetLineHeight();

        // we don't need to calculate x position as we always refresh the
        // entire line(s)
        CalcScrolledPosition(0, rect.y, nullptr, &rect.y);

        wxLogTrace(wxT("listbox"), wxT("Refreshing items %d..%d (%d-%d)"),
                   m_updateFrom, m_updateFrom + m_updateCount - 1,
                   rect.GetTop(), rect.GetBottom());

        Refresh(true, &rect);
    }
}

void ibListBox::OnInternalIdle()
{
    if ( m_updateScrollbarY || m_updateScrollbarX )
    {
        UpdateScrollbars();

        m_updateScrollbarX =
        m_updateScrollbarY = false;
    }

    if ( m_currentChanged )
    {
        DoEnsureVisible(m_current);

        m_currentChanged = false;
    }

    if ( m_updateCount )
    {
        UpdateItems();

        m_updateCount = 0;
    }
    ibControl::OnInternalIdle();
}


void ibListBox::DeselectAll(int itemToLeaveSelected)
{
    // wxListBoxBase shim — the stock lboxcmn implementation
    if ( HasMultipleSelection() )
    {
        wxArrayInt selections;
        GetSelections(selections);

        size_t count = selections.GetCount();
        for ( size_t n = 0; n < count; n++ )
        {
            int item = selections[n];
            if ( item != itemToLeaveSelected )
                Deselect(item);
        }
    }
    else // single selection
    {
        int sel = GetSelection();
        if ( sel != wxNOT_FOUND && sel != itemToLeaveSelected )
            Deselect(sel);
    }
}
// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

wxBorder ibListBox::GetDefaultBorder() const
{
    return wxBORDER_SUNKEN;
}

void ibListBox::DoDraw(ibControlRenderer *renderer)
{
    // adjust the DC to account for scrolling
    wxDC& dc = renderer->GetDC();
    PrepareDC(dc);
    dc.SetFont(GetFont());

    // get the update rect
    wxRect rectUpdate = GetUpdateClientRect();

    int yTop, yBottom;
    CalcUnscrolledPosition(0, rectUpdate.GetTop(), nullptr, &yTop);
    CalcUnscrolledPosition(0, rectUpdate.GetBottom(), nullptr, &yBottom);

    // get the items which must be redrawn
    wxCoord lineHeight = GetLineHeight();
    unsigned int itemFirst = yTop / lineHeight,
                 itemLast = (yBottom + lineHeight - 1) / lineHeight,
                 itemMax = GetCount();

    if ( itemFirst >= itemMax )
        return;

    if ( itemLast > itemMax )
        itemLast = itemMax;

    // do draw them
    wxLogTrace(wxT("listbox"), wxT("Repainting items %d..%d"),
               itemFirst, itemLast);

    DoDrawRange(renderer, itemFirst, itemLast);
}

void ibListBox::DoDrawRange(ibControlRenderer *renderer,
                            int itemFirst, int itemLast)
{
    renderer->DrawItems(this, itemFirst, itemLast);
}

// ----------------------------------------------------------------------------
// size calculations
// ----------------------------------------------------------------------------

bool ibListBox::SetFont(const wxFont& font)
{
    if ( !ibControl::SetFont(font) )
        return false;

    CalcItemsPerPage();

    RefreshAll();

    return true;
}

void ibListBox::CalcItemsPerPage()
{
    m_lineHeight = GetRenderer()->GetListboxItemHeight(GetCharHeight());
    m_itemsPerPage = GetClientSize().y / m_lineHeight;
}

int ibListBox::GetItemsPerPage() const
{
    if ( !m_itemsPerPage )
    {
        wxConstCast(this, ibListBox)->CalcItemsPerPage();
    }

    return m_itemsPerPage;
}

wxCoord ibListBox::GetLineHeight() const
{
    if ( !m_lineHeight )
    {
        wxConstCast(this, ibListBox)->CalcItemsPerPage();
    }

    return m_lineHeight;
}

wxCoord ibListBox::GetMaxWidth() const
{
    if ( m_maxWidth == 0 )
    {
        ibListBox *self = wxConstCast(this, ibListBox);
        wxCoord width;
        unsigned int count = GetCount();
        for ( unsigned int n = 0; n < count; n++ )
        {
            GetTextExtent(this->GetString(n), &width, nullptr);
            if ( width > m_maxWidth )
            {
                self->m_maxWidth = width;
                self->m_maxWidthItem = n;
            }
        }
    }

    return m_maxWidth;
}

void ibListBox::OnSize(wxSizeEvent& event)
{
    // recalculate the number of items per page
    CalcItemsPerPage();

    // the scrollbars might [dis]appear
    m_updateScrollbarX =
    m_updateScrollbarY = true;

    event.Skip();
}

void ibListBox::DoSetFirstItem(int n)
{
    SetCurrentItem(n);
}

void ibListBox::DoSetSize(int x, int y,
                          int width, int height,
                          int sizeFlags)
{
    if ( GetWindowStyle() & wxLB_INT_HEIGHT )
    {
        // we must round up the height to an entire number of rows

        // the client area must contain an int number of rows, so take borders
        // into account
        wxRect rectBorders = GetRenderer()->GetBorderDimensions(GetBorder());
        wxCoord hBorders = rectBorders.y + rectBorders.height;

        wxCoord hLine = GetLineHeight();
        height = ((height - hBorders + hLine - 1) / hLine)*hLine + hBorders;
    }

    ibControl::DoSetSize(x, y, width, height, sizeFlags);
}

wxSize ibListBox::DoGetBestClientSize() const
{
    wxCoord width = 0,
            height = 0;

    unsigned int count = GetCount();
    for ( unsigned int n = 0; n < count; n++ )
    {
        wxCoord w,h;
        GetTextExtent(this->GetString(n), &w, &h);

        if ( w > width )
            width = w;
        if ( h > height )
            height = h;
    }

    // if the listbox is empty, still give it some non zero (even if
    // arbitrary) size - otherwise, leave small margin around the strings
    if ( !width )
        width = 100;
    else
        width += 3*GetCharWidth();

    if ( !height )
        height = GetCharHeight();

    // we need the height of the entire listbox, not just of one line
    height *= wxMax(count, 7);

    return wxSize(width, height);
}

// ----------------------------------------------------------------------------
// listbox actions
// ----------------------------------------------------------------------------

bool ibListBox::SendEvent(wxEventType type, int item)
{
    wxCommandEvent event(type, m_windowId);
    event.SetEventObject(this);

    // use the current item by default
    if ( item == -1 )
    {
        item = m_current;
    }

    // client data and string parameters only make sense if we have an item
    if ( item != -1 )
    {
        if ( HasClientObjectData() )
            event.SetClientObject(wxItemContainer::GetClientObject(item));
        else if ( HasClientUntypedData() )
            event.SetClientData(wxItemContainer::GetClientData(item));

        event.SetString(GetString(item));
    }

    event.SetInt(item);

    return GetEventHandler()->ProcessEvent(event);
}

void ibListBox::SetCurrentItem(int n)
{
    if ( n != m_current )
    {
        if ( m_current != -1 )
            RefreshItem(m_current);

        m_current = n;

        if ( m_current != -1 )
        {
            m_currentChanged = true;

            RefreshItem(m_current);
        }
    }
    //else: nothing to do
}

bool ibListBox::FindItem(const wxString& prefix, bool strictlyAfter)
{
    unsigned int count = GetCount();
    if ( count==0 )
    {
        // empty listbox, we can't find anything in it
        return false;
    }

    // start either from the current item or from the next one if strictlyAfter
    // is true
    int first;
    if ( strictlyAfter )
    {
        // the following line will set first correctly to 0 if there is no
        // selection (m_current == -1)
        first = m_current == (int)(count - 1) ? 0 : m_current + 1;
    }
    else // start with the current
    {
        first = m_current == -1 ? 0 : m_current;
    }

    int last = first == 0 ? count - 1 : first - 1;

    // if this is not true we'd never exit from the loop below!
    wxASSERT_MSG( first < (int)count && last < (int)count, wxT("logic error") );

    // precompute it outside the loop
    size_t len = prefix.length();

    // loop over all items in the listbox
    for ( int item = first; item != (int)last; item < (int)(count - 1) ? item++ : item = 0 )
    {
        if ( wxStrnicmp(this->GetString(item).c_str(), prefix, len) == 0 )
        {
            SetCurrentItem(item);

            if ( !(GetWindowStyle() & wxLB_MULTIPLE) )
            {
                DeselectAll(item);
                SelectAndNotify(item);

                if ( GetWindowStyle() & wxLB_EXTENDED )
                    AnchorSelection(item);
            }

            return true;
        }
    }

    // nothing found
    return false;
}

void ibListBox::EnsureVisible(int n)
{
    if ( m_updateScrollbarY )
    {
        UpdateScrollbars();

        m_updateScrollbarX =
        m_updateScrollbarY = false;
    }

    DoEnsureVisible(n);
}

void ibListBox::DoEnsureVisible(int n)
{
    if ( !m_showScrollbarY )
    {
        // nothing to do - everything is shown anyhow
        return;
    }

    int first;
    GetViewStart(nullptr, &first);
    if ( first > n )
    {
        // we need to scroll upwards, so make the current item appear on top
        // of the shown range
        Scroll(0, n);
    }
    else
    {
        int last = first + GetClientSize().y / GetLineHeight() - 1;
        if ( last < n )
        {
            // scroll down: the current item appears at the bottom of the
            // range
            Scroll(0, n - (last - first));
        }
    }
}

void ibListBox::ChangeCurrent(int diff)
{
    int current = m_current == -1 ? 0 : m_current;

    current += diff;

    int last = GetCount() - 1;
    if ( current < 0 )
        current = 0;
    else if ( current > last )
        current = last;

    SetCurrentItem(current);
}

void ibListBox::ExtendSelection(int itemTo)
{
    // if we don't have the explicit values for selection start/end, make them
    // up
    if ( m_selAnchor == -1 )
        m_selAnchor = m_current;

    if ( itemTo == -1 )
        itemTo = m_current;

    // swap the start/end of selection range if necessary
    int itemFrom = m_selAnchor;
    if ( itemFrom > itemTo )
    {
        int itemTmp = itemFrom;
        itemFrom = itemTo;
        itemTo = itemTmp;
    }

    // the selection should now include all items in the range between the
    // anchor and the specified item and only them

    int n;
    for ( n = 0; n < itemFrom; n++ )
    {
        Deselect(n);
    }

    for ( ; n <= itemTo; n++ )
    {
        SetSelection(n);
    }

    unsigned int count = GetCount();
    for ( ; n < (int)count; n++ )
    {
        Deselect(n);
    }
}

void ibListBox::DoSelect(int item, bool sel)
{
    if ( item != -1 )
    {
        // go to this item first
        SetCurrentItem(item);
    }

    // the current item is the one we want to change: either it was just
    // changed above to be the same as item or item == -1 in which we case we
    // are supposed to use the current one anyhow
    if ( m_current != -1 )
    {
        // [de]select it
        SetSelection(m_current, sel);
    }
}

void ibListBox::SelectAndNotify(int item)
{
    if ( item != -1 )
    {
        DoSelect(item);
        SendEvent(wxEVT_LISTBOX);
    }
}

void ibListBox::Activate(int item)
{
    if ( item != -1 )
    {
        SetCurrentItem(item);
        if ( !(GetWindowStyle() & wxLB_MULTIPLE) )
            DeselectAll(item);

        DoSelect(item);
        SendEvent(wxEVT_LISTBOX_DCLICK);
    }
}

// ----------------------------------------------------------------------------
// hittest
// ----------------------------------------------------------------------------

int ibListBox::DoListHitTest(const wxPoint& point) const
{
    if ( !GetClientRect().Contains(point) )
        return wxNOT_FOUND;

    int y, index;

    CalcUnscrolledPosition(0, point.y, nullptr, &y);
    index = y / GetLineHeight();

    // mouse is above the first item or below the last item
    if ( index < 0 || (unsigned int)index >= GetCount() )
        return wxNOT_FOUND;

    return index;
}

// ----------------------------------------------------------------------------
// input handling
// ----------------------------------------------------------------------------

/*
   The numArg here is the listbox item index while the strArg is used
   differently for the different actions:

   a) for ibACTION_LISTBOX_FIND it has the natural meaning: this is the string
      to find

   b) for ibACTION_LISTBOX_SELECT and ibACTION_LISTBOX_EXTENDSEL it is used
      to decide if the listbox should send the notification event (it is empty)
      or not (it is not): this allows us to reuse the same action for when the
      user is dragging the mouse when it has been released although in the
      first case no notification is sent while in the second it is sent.
 */
bool ibListBox::PerformAction(const ibControlAction& action,
                              long numArg,
                              const wxString& strArg)
{
    int item = (int)numArg;

    if ( action == ibACTION_LISTBOX_SETFOCUS )
    {
        SetCurrentItem(item);
    }
    else if ( action == ibACTION_LISTBOX_ACTIVATE )
    {
        Activate(item);
    }
    else if ( action == ibACTION_LISTBOX_TOGGLE )
    {
        if ( item == -1 )
            item = m_current;

        if ( IsSelected(item) )
        {
            DoUnselect(item);
            SendEvent(wxEVT_LISTBOX);
        }
        else
            SelectAndNotify(item);
    }
    else if ( action == ibACTION_LISTBOX_SELECT )
    {
        DeselectAll(item);

        if ( strArg.empty() )
            SelectAndNotify(item);
        else
            DoSelect(item);
    }
    else if ( action == ibACTION_LISTBOX_SELECTADD )
        DoSelect(item);
    else if ( action == ibACTION_LISTBOX_UNSELECT )
        DoUnselect(item);
    else if ( action == ibACTION_LISTBOX_MOVEDOWN )
        ChangeCurrent(1);
    else if ( action == ibACTION_LISTBOX_MOVEUP )
        ChangeCurrent(-1);
    else if ( action == ibACTION_LISTBOX_PAGEDOWN )
        ChangeCurrent(GetItemsPerPage());
    else if ( action == ibACTION_LISTBOX_PAGEUP )
        ChangeCurrent(-GetItemsPerPage());
    else if ( action == ibACTION_LISTBOX_START )
        SetCurrentItem(0);
    else if ( action == ibACTION_LISTBOX_END )
        SetCurrentItem(GetCount() - 1);
    else if ( action == ibACTION_LISTBOX_UNSELECTALL )
        DeselectAll(item);
    else if ( action == ibACTION_LISTBOX_EXTENDSEL )
        ExtendSelection(item);
    else if ( action == ibACTION_LISTBOX_FIND )
        FindNextItem(strArg);
    else if ( action == ibACTION_LISTBOX_ANCHOR )
        AnchorSelection(item == -1 ? m_current : item);
    else if ( action == ibACTION_LISTBOX_SELECTALL ||
              action == ibACTION_LISTBOX_SELTOGGLE )
        wxFAIL_MSG(wxT("unimplemented yet"));
    else
        return ibControl::PerformAction(action, numArg, strArg);

    return true;
}

/* static */
ibInputHandler *ibListBox::GetStdInputHandler(ibInputHandler *handlerDef)
{
    static ibStdListboxInputHandler s_handler(handlerDef);

    return &s_handler;
}

// ============================================================================
// implementation of ibStdListboxInputHandler
// ============================================================================

ibStdListboxInputHandler::ibStdListboxInputHandler(ibInputHandler *handler,
                                                   bool toggleOnPressAlways)
                        : ibStdInputHandler(handler)
{
    m_btnCapture = 0;
    m_toggleOnPressAlways = toggleOnPressAlways;
    m_actionMouse = ibACTION_NONE;
    m_trackMouseOutside = true;
}

int ibStdListboxInputHandler::HitTest(const ibListBox *lbox,
                                      const wxMouseEvent& event)
{
    return lbox->HitTest(event.GetPosition());
}

bool ibStdListboxInputHandler::IsValidIndex(const ibListBox *lbox, int item)
{
    return item >= 0 && (unsigned int)item < lbox->GetCount();
}

ibControlAction
ibStdListboxInputHandler::SetupCapture(ibListBox *lbox,
                                       const wxMouseEvent& event,
                                       int item)
{
    // we currently only allow selecting with the left mouse button, if we
    // do need to allow using other buttons too we might use the code
    // inside #if 0
#if 0
    m_btnCapture = event.LeftDown()
                    ? 1
                    : event.RightDown()
                        ? 3
                        : 2;
#else
    m_btnCapture = 1;
#endif // 0/1

    ibControlAction action;
    if ( lbox->HasMultipleSelection() )
    {
        if ( lbox->GetWindowStyle() & wxLB_MULTIPLE )
        {
            if ( m_toggleOnPressAlways )
            {
                // toggle the item right now
                action = ibACTION_LISTBOX_TOGGLE;
            }
            //else: later

            m_actionMouse = ibACTION_LISTBOX_SETFOCUS;
        }
        else // wxLB_EXTENDED listbox
        {
            // simple click in an extended sel listbox clears the old
            // selection and adds the clicked item to it then, ctrl-click
            // toggles an item to it and shift-click adds a range between
            // the old selection anchor and the clicked item
            if ( event.ControlDown() )
            {
                lbox->PerformAction(ibACTION_LISTBOX_ANCHOR, item);

                action = ibACTION_LISTBOX_TOGGLE;
            }
            else if ( event.ShiftDown() )
            {
                action = ibACTION_LISTBOX_EXTENDSEL;
            }
            else // simple click
            {
                lbox->PerformAction(ibACTION_LISTBOX_ANCHOR, item);

                action = ibACTION_LISTBOX_SELECT;
            }

            m_actionMouse = ibACTION_LISTBOX_EXTENDSEL;
        }
    }
    else // single selection
    {
        m_actionMouse =
        action = ibACTION_LISTBOX_SELECT;
    }

    // by default we always do track it
    m_trackMouseOutside = true;

    return action;
}

bool ibStdListboxInputHandler::HandleKey(ibInputConsumer *consumer,
                                         const wxKeyEvent& event,
                                         bool pressed)
{
    // we're only interested in the key press events
    if ( pressed && !event.AltDown() )
    {
        bool isMoveCmd = true;
        int style = consumer->GetInputWindow()->GetWindowStyle();

        ibControlAction action;
        wxString strArg;

        int keycode = event.GetKeyCode();
        switch ( keycode )
        {
            // movement
            case WXK_UP:
                action = ibACTION_LISTBOX_MOVEUP;
                break;

            case WXK_DOWN:
                action = ibACTION_LISTBOX_MOVEDOWN;
                break;

            case WXK_PAGEUP:
                action = ibACTION_LISTBOX_PAGEUP;
                break;

            case WXK_PAGEDOWN:
                action = ibACTION_LISTBOX_PAGEDOWN;
                break;

            case WXK_HOME:
                action = ibACTION_LISTBOX_START;
                break;

            case WXK_END:
                action = ibACTION_LISTBOX_END;
                break;

            // selection
            case WXK_SPACE:
                if ( style & wxLB_MULTIPLE )
                {
                    action = ibACTION_LISTBOX_TOGGLE;
                    isMoveCmd = false;
                }
                break;

            case WXK_RETURN:
                action = ibACTION_LISTBOX_ACTIVATE;
                isMoveCmd = false;
                break;

            default:
                if ( (keycode < 255) && wxIsalnum((wxChar)keycode) )
                {
                    action = ibACTION_LISTBOX_FIND;
                    strArg = (wxChar)keycode;
                }
        }

        if ( !action.IsEmpty() )
        {
            consumer->PerformAction(action, -1, strArg);

            if ( isMoveCmd )
            {
                if ( style & wxLB_SINGLE )
                {
                    // the current item is always the one selected
                    consumer->PerformAction(ibACTION_LISTBOX_SELECT);
                }
                else if ( style & wxLB_EXTENDED )
                {
                    if ( event.ShiftDown() )
                        consumer->PerformAction(ibACTION_LISTBOX_EXTENDSEL);
                    else
                    {
                        // select the item and make it the new selection anchor
                        consumer->PerformAction(ibACTION_LISTBOX_SELECT);
                        consumer->PerformAction(ibACTION_LISTBOX_ANCHOR);
                    }
                }
                //else: nothing to do for multiple selection listboxes
            }

            return true;
        }
    }

    return ibStdInputHandler::HandleKey(consumer, event, pressed);
}

bool ibStdListboxInputHandler::HandleMouse(ibInputConsumer *consumer,
                                           const wxMouseEvent& event)
{
    ibListBox *lbox = wxStaticCast(consumer->GetInputWindow(), ibListBox);
    int item = HitTest(lbox, event);
    ibControlAction action;

    // when the left mouse button is pressed, capture the mouse and track the
    // item under mouse (if the mouse leaves the window, we will still be
    // getting the mouse move messages generated by wxScrollWindow)
    if ( event.LeftDown() )
    {
        // SEAM vs univ: a click below the last item yields an out-of-range
        // index — univ passed it straight to SELECT (and into user code)
        if ( IsValidIndex(lbox, item) )
        {
            // capture the mouse to track the selected item
            lbox->CaptureMouse();

            action = SetupCapture(lbox, event, item);
        }
    }
    else if ( m_btnCapture && event.ButtonUp(m_btnCapture) )
    {
        // when the left mouse button is released, release the mouse too
        wxWindow *winCapture = wxWindow::GetCapture();
        if ( winCapture )
        {
            winCapture->ReleaseMouse();
            m_btnCapture = 0;
        }
        //else: the mouse wasn't pressed over the listbox, only released here
    }
    else if ( event.LeftDClick() && IsValidIndex(lbox, item) )
    {
        action = ibACTION_LISTBOX_ACTIVATE;
    }

    if ( !action.IsEmpty() )
    {
        lbox->PerformAction(action, item);
    }

    return ibStdInputHandler::HandleMouse(consumer, event);
}

bool ibStdListboxInputHandler::HandleMouseMove(ibInputConsumer *consumer,
                                               const wxMouseEvent& event)
{
    wxWindow *winCapture = wxWindow::GetCapture();
    if ( winCapture && (event.GetEventObject() == winCapture) )
    {
        ibListBox *lbox = wxStaticCast(consumer->GetInputWindow(), ibListBox);

        if ( !m_btnCapture || !m_trackMouseOutside )
        {
            // someone captured the mouse for us (we always set m_btnCapture
            // when we do it ourselves): in this case we only react to
            // the mouse messages when they happen inside the listbox
            if ( lbox->HitTest(event.GetPosition()) != wxHT_WINDOW_INSIDE )
                return false;
        }

        int item = HitTest(lbox, event);
        if ( !m_btnCapture )
        {
            // now that we have the mouse inside the listbox, do capture it
            // normally - but ensure that we will still ignore the outside
            // events
            SetupCapture(lbox, event, item);

            m_trackMouseOutside = false;
        }

        if ( IsValidIndex(lbox, item) )
        {
            // pass something into strArg to tell the listbox that it shouldn't
            // send the notification message: see PerformAction() above
            lbox->PerformAction(m_actionMouse, item, wxT("no"));
        }
        // else: don't pass invalid index to the listbox
    }
    else // we don't have capture any more
    {
        if ( m_btnCapture )
        {
            // if we lost capture unexpectedly (someone else took the capture
            // from us), return to a consistent state
            m_btnCapture = 0;
        }
    }

    return ibStdInputHandler::HandleMouseMove(consumer, event);
}

wxIMPLEMENT_DYNAMIC_CLASS(ibListBox, ibControl);

#endif // wxUSE_LISTBOX
