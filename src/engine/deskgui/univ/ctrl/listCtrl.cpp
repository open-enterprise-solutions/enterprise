// FORKED from the wx GENERIC list control (wxGenericListCtrl -> ibListCtrl,
// mechanical ib prefixes). The generic list is already custom-drawn - the
// port is the usual deskgui seam work: ibControl base, theme colours.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/listctrl.cpp
// Purpose:     generic implementation of wxListCtrl
// Author:      Robert Roebling
//              Vadim Zeitlin (virtual list control support)
// Copyright:   (c) 1998 Robert Roebling
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// TODO
//
//   1. we need to implement searching/sorting for virtual controls somehow
//   2. when changing selection the lines are refreshed twice


// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/listCtrl.h"

#if wxUSE_LISTCTRL

#ifndef WX_PRECOMP
    #include "wx/scrolwin.h"
    #include "wx/timer.h"
    #include "wx/settings.h"
    #include "wx/dynarray.h"
    #include "wx/dcclient.h"
    #include "wx/dcscreen.h"
    #include "wx/math.h"
    #include "wx/settings.h"
    #include "wx/sizer.h"
#endif

#include "wx/imaglist.h"
#include "wx/renderer.h"
#include "wx/dcbuffer.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"  // the list's colours: the theme's, as ibTreeCtrl's
#include "wx/selstore.h"

#include "wx/generic/private/drawbitmap.h"
#include "wx/generic/private/widthcalc.h"

#include "wx/private/access.h"

#ifdef __WXMAC__
    #include "wx/osx/private.h"
#endif

#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
    #include "wx/msw/wrapwin.h"
#endif

#include <memory>

// ----------------------------------------------------------------------------
// the private classes: FORKED from wx/generic/private/listctrl.h
// ----------------------------------------------------------------------------

// ============================================================================
// private classes
// ============================================================================

//-----------------------------------------------------------------------------
//  ibColWidthInfo (internal)
//-----------------------------------------------------------------------------

struct ibColWidthInfo
{
    int     nMaxWidth;
    bool    bNeedsUpdate;   //  only set to true when an item whose
                            //  width == nMaxWidth is removed

    ibColWidthInfo(int w = 0, bool needsUpdate = false)
    {
        nMaxWidth = w;
        bNeedsUpdate = needsUpdate;
    }
};

using ColWidthArray = std::vector<ibColWidthInfo>;

//-----------------------------------------------------------------------------
//  ibListItemData (internal)
//-----------------------------------------------------------------------------

class ibListItemData
{
public:
    ibListItemData(ibListMainWindow *owner);
    ibListItemData(const ibListItemData&) = delete;
    ibListItemData(ibListItemData&&);
    ibListItemData& operator=(const ibListItemData&) = delete;
    ibListItemData& operator=(ibListItemData&&);
    ~ibListItemData();

    void SetItem( const wxListItem &info );
    void SetImage( int image ) { m_image = image; }
    void SetData( wxUIntPtr data ) { m_data = data; }
    void SetPosition( int x, int y );
    void SetSize( int width, int height );

    bool HasText() const { return !m_text.empty(); }
    const wxString& GetText() const { return m_text; }
    void SetText(const wxString& text) { m_text = text; }

    // we can't use empty string for measuring the string width/height, so
    // always return something
    wxString GetTextForMeasuring() const
    {
        wxString s = GetText();
        if ( s.empty() )
            s = wxT('H');

        return s;
    }

    bool IsHit( int x, int y ) const;

    int GetX() const;
    int GetY() const;
    int GetWidth() const;
    int GetHeight() const;

    int GetImage() const { return m_image; }
    bool HasImage() const { return GetImage() != -1; }

    void GetItem( wxListItem &info ) const;

    void SetAttr(wxItemAttr *attr) { m_attr = attr; }
    wxItemAttr *GetAttr() const { return m_attr; }

public:
    // the item image or -1
    int m_image = -1;

    // user data associated with the item
    wxUIntPtr m_data = 0;

    // the item coordinates are not used in report mode; instead this pointer is
    // null and the owner window is used to retrieve the item position and size
    wxRect *m_rect = nullptr;

    // the list ctrl we are in
    ibListMainWindow *m_owner;

    // custom attributes or nullptr
    wxItemAttr *m_attr = nullptr;

protected:
    wxString m_text;
};

//-----------------------------------------------------------------------------
//  ibListHeaderData (internal)
//-----------------------------------------------------------------------------

class ibListHeaderData : public wxObject
{
public:
    ibListHeaderData();
    ibListHeaderData( const wxListItem &info );
    void SetItem( const wxListItem &item );
    void SetPosition( int x, int y );
    void SetWidth( int w );
    void SetState( int state );
    void SetFormat( int format );
    void SetHeight( int h );
    bool HasImage() const;

    bool HasText() const { return !m_text.empty(); }
    const wxString& GetText() const { return m_text; }
    void SetText(const wxString& text) { m_text = text; }

    void GetItem( wxListItem &item ) const;

    bool IsHit( int x, int y ) const;
    int GetImage() const;
    int GetWidth() const;
    int GetFormat() const;
    int GetState() const;

protected:
    long      m_mask;
    int       m_image;
    wxString  m_text;
    int       m_format;
    int       m_width;
    int       m_xpos,
              m_ypos;
    int       m_height;
    int       m_state;

private:
    void Init();
};

//-----------------------------------------------------------------------------
//  ibListLineData (internal)
//-----------------------------------------------------------------------------

class ibListLineData
{
public:
    // the list of subitems: only may have more than one item in report mode
    std::vector<ibListItemData> m_items;

    // this is not used in report view
    struct GeometryInfo
    {
        // total item rect
        wxRect m_rectAll;

        // label only
        wxRect m_rectLabel;

        // icon only
        wxRect m_rectIcon;

        // the part to be highlighted
        wxRect m_rectHighlight;

        // extend all our rects to be centered inside the one of given width
        void ExtendWidth(wxCoord w)
        {
            wxASSERT_MSG( m_rectAll.width <= w,
                            wxT("width can only be increased") );

            int delta = (w - m_rectAll.width) / 2;
            m_rectAll.width = w;
            m_rectLabel.x += delta;
            m_rectIcon.x += delta;
            m_rectHighlight.x += delta;
        }
    };

    std::unique_ptr<GeometryInfo> m_gi;

    // is this item selected? [NB: not used in virtual mode]
    bool m_highlighted;

    bool m_checked;

    // back pointer to the list ctrl
    ibListMainWindow *m_owner;

public:
    ibListLineData(ibListMainWindow *owner);
    ibListLineData(const ibListLineData&) = delete;
    ibListLineData(ibListLineData&& other) = default;

    ibListLineData& operator=(const ibListLineData&) = delete;
    ibListLineData& operator=(ibListLineData&&) = default;

    ~ibListLineData() = default;

    // called by the owner when it toggles report view
    void SetReportView(bool inReportView)
    {
        // we only need m_gi when we're not in report view so update as needed
        m_gi.reset( inReportView ? nullptr : new GeometryInfo );
    }

    // are we in report mode?
    inline bool InReportView() const;

    // are we in virtual report mode?
    inline bool IsVirtual() const;

    // these 2 methods shouldn't be called for report view controls, in that
    // case we determine our position/size ourselves

    // calculate the size of the line
    void CalculateSize( wxReadOnlyDC *dc, int spacing );

    // remember the position this line appears at
    void SetPosition( int x, int y, int spacing );

    // wxListCtrl API

    void SetImage( int image ) { SetImage(0, image); }
    int GetImage() const { return GetImage(0); }
    void SetImage( int index, int image );
    int GetImage( int index ) const;

    void Check(bool check) { m_checked = check; }
    bool IsChecked() { return m_checked; }

    bool HasImage() const { return GetImage() != -1; }
    bool HasText() const { return !GetText(0).empty(); }

    void SetItem( int index, const wxListItem &info );
    void GetItem( int index, wxListItem &info ) const;

    wxString GetText(int index) const;
    void SetText( int index, const wxString& s );

    wxItemAttr *GetAttr() const;
    void SetAttr(wxItemAttr *attr);

    // return true if the highlighting really changed
    bool Highlight( bool on );

    void ReverseHighlight();

    bool IsHighlighted() const
    {
        wxASSERT_MSG( !IsVirtual(), wxT("unexpected call to IsHighlighted") );

        return m_highlighted;
    }

    // draw the line on the given DC in icon/list mode
    void Draw( wxDC *dc, const wxPoint& origin, bool current );

    // the same in report mode: it needs more parameters as we don't store
    // everything in the item in report mode
    void DrawInReportMode( wxDC *dc,
                           const wxRect& rect,
                           bool highlighted,
                           bool current,
                           bool checked );

private:
    // set the line to contain num items (only can be > 1 in report mode)
    void InitItems( int num );

    // get the mode (i.e. style)  of the list control
    inline int GetMode() const;

    // Apply this item attributes to the given DC: set the text font and colour
    // and also erase the background appropriately.
    void ApplyAttributes(wxDC *dc,
                         const wxRect& rectHL,
                         bool highlighted,
                         bool current);

    // draw the text on the DC with the correct justification; also add an
    // ellipsis if the text is too large to fit in the current width
    void DrawTextFormatted(wxDC *dc,
                           const wxString &text,
                           int col,
                           int x,
                           int yMid,    // this is middle, not top, of the text
                           int width);
};

//-----------------------------------------------------------------------------
//  ibListHeaderWindow (internal)
//-----------------------------------------------------------------------------

class ibListHeaderWindow : public ibWindow
{
protected:
    ibListMainWindow  *m_owner;
    const wxCursor    *m_currentCursor;
    wxCursor          *m_resizeCursor;
    bool               m_isDragging;

    // column being resized or -1
    int m_column;

    // divider line position in logical (unscrolled) coords
    int m_currentX;

    // minimal position beyond which the divider line
    // can't be dragged in logical coords
    int m_minX;

public:
    ibListHeaderWindow();

    // We provide only Create(), not the ctor, because we need to create the
    // C++ object before creating the window, see the explanations in
    // CreateOrDestroyHeaderWindowAsNeeded()
    bool Create( wxWindow *win,
                 wxWindowID id,
                 ibListMainWindow *owner,
                 const wxPoint &pos = wxDefaultPosition,
                 const wxSize &size = wxDefaultSize,
                 long style = 0,
                 const wxString &name = wxT("wxlistctrlcolumntitles") );

    virtual ~ibListHeaderWindow();

    // We never need focus as we don't have any keyboard interface.
    virtual bool AcceptsFocus() const override { return false; }

    void DrawCurrent();
    void AdjustDC( wxDC& dc );

    void OnPaint( wxPaintEvent &event );
    void OnMouse( wxMouseEvent &event );
    void OnSysColourChanged( wxSysColourChangedEvent &event );

    // needs refresh
    bool m_dirty;

    // Update main window's column later
    bool m_sendSetColumnWidth;
    int m_colToSend;
    int m_widthToSend;

    bool m_sortAsc;
    int m_sortCol;

    virtual wxWindow *GetMainWindowOfCompositeControl() override { return GetParent(); }

    virtual void OnInternalIdle() override;

private:
    // common part of all ctors
    void Init();

    // generate and process the list event of the given type, return true if
    // it wasn't vetoed, i.e. if we should proceed
    bool SendListEvent(wxEventType type, const wxPoint& pos);

    wxDECLARE_EVENT_TABLE();
};

//-----------------------------------------------------------------------------
// ibListRenameTimer (internal)
//-----------------------------------------------------------------------------

class ibListRenameTimer: public wxTimer
{
private:
    ibListMainWindow *m_owner;

public:
    ibListRenameTimer( ibListMainWindow *owner );
    void Notify() override;
};

//-----------------------------------------------------------------------------
// ibListFindTimer (internal)
//-----------------------------------------------------------------------------

class ibListFindTimer: public wxTimer
{
public:
    // reset the current prefix after half a second of inactivity
    enum { DELAY = 500 };

    ibListFindTimer( ibListMainWindow *owner )
        : m_owner(owner)
    {
    }

    virtual void Notify() override;

private:
    ibListMainWindow *m_owner;
};

//-----------------------------------------------------------------------------
// ibListTextCtrlWrapper: wraps a ibTextCtrl to make it work for inline editing
//-----------------------------------------------------------------------------

class ibListTextCtrlWrapper : public wxEvtHandler
{
public:
    // NB: text must be a valid object but not Create()d yet
    ibListTextCtrlWrapper(ibListMainWindow *owner,
                          ibTextCtrl *text,
                          size_t itemEdit);

    ibTextCtrl *GetText() const { return m_text; }

    // Check if the given key event should stop editing and return true if it
    // does or false otherwise.
    bool CheckForEndEditKey(const wxKeyEvent& event);

    // Different reasons for calling EndEdit():
    //
    // It was called because:
    enum EndReason
    {
        End_Accept,     // user has accepted the changes.
        End_Discard,    // user has cancelled editing.
        End_Destroy     // the entire control is being destroyed.
    };

    void EndEdit(EndReason reason);

protected:
    void OnChar( wxKeyEvent &event );
    void OnKeyUp( wxKeyEvent &event );
    void OnKillFocus( wxFocusEvent &event );

    bool AcceptChanges();
    void Finish( bool setfocus );

private:
    ibListMainWindow   *m_owner;
    ibTextCtrl         *m_text;
    wxString            m_startValue;
    size_t              m_itemEdited;
    bool                m_aboutToFinish;

    wxDECLARE_EVENT_TABLE();
};

//-----------------------------------------------------------------------------
//  ibListMainWindow (internal)
//-----------------------------------------------------------------------------

class ibListMainWindow : public ibWindow
{
public:
    ibListMainWindow();
    ibListMainWindow( wxWindow *parent,
                      wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size );

    virtual ~ibListMainWindow();

    // called by the main control when its mode changes
    void SetReportView(bool inReportView);

    // helper to simplify testing for wxLC_XXX flags
    bool HasFlag(int flag) const { return m_parent->HasFlag(flag); }

    // return true if this is a virtual list control
    bool IsVirtual() const { return HasFlag(wxLC_VIRTUAL); }

    // return true if the control is in report mode
    bool InReportView() const { return HasFlag(wxLC_REPORT); }

    // return true if we are in single selection mode, false if multi sel
    bool IsSingleSel() const { return HasFlag(wxLC_SINGLE_SEL); }

    // do we have a header window?
    bool HasHeader() const
        { return InReportView() && !HasFlag(wxLC_NO_HEADER); }

    void HighlightAll( bool on );

    // all these functions only do something if the line is currently visible

    // Make sure that _line_ is the only item highlighted in the control.
    // _oldLine_ is the old focused item.
    void HighlightOnly( size_t line, size_t oldLine = (size_t)-1 );

    // In multiple selection mode, instead of sending one notification per item
    // (which is too slow if a lot of items are selected) we send only one notification
    // for all of them which is the wxMSW behaviour. Currently done for virtual
    // list controls and for deselection only.
    enum SendEvent { SendEvent_None, SendEvent_Normal };

    // change the line "selected" state, return true if it really changed
    bool HighlightLine( size_t line, bool highlight = true,
                        SendEvent sendEvent = SendEvent_Normal );

    // as HighlightLine() but do it for the range of lines: this is incredibly
    // more efficient for virtual list controls!
    //
    // NB: unlike HighlightLine() this one does refresh the lines on screen
    void HighlightLines( size_t lineFrom, size_t lineTo, bool on = true,
                         SendEvent sendEvent = SendEvent_Normal );

    // toggle the line state and refresh it
    void ReverseHighlight( size_t line )
        { HighlightLine(line, !IsHighlighted(line)); RefreshLine(line); }

    // return true if the line is highlighted
    bool IsHighlighted(size_t line) const;

    // refresh one or several lines at once
    void RefreshLine( size_t line );
    void RefreshLines( size_t lineFrom, size_t lineTo );

    // refresh all selected items
    void RefreshSelected();

    // refresh all lines below the given one: the difference with
    // RefreshLines() is that the index here might not be a valid one (happens
    // when the last line is deleted)
    void RefreshAfter( size_t lineFrom );

    // the methods which are forwarded to ibListLineData itself in list/icon
    // modes but are here because the lines don't store their positions in the
    // report mode

    // Let the screen readers see the shown items: they are drawn by us and so
    // are invisible to them otherwise.
    void UpdateAccessibleItems();

    // The item announced to the screen readers by the last call to the
    // function above or -1 if there was no current item then.
    long m_lastAccessibleCurrent = -1;

    // get the bound rect for the entire line
    wxRect GetLineRect(size_t line) const;

    // get the bound rect of the label
    wxRect GetLineLabelRect(size_t line) const;

    // get the bound rect of the items icon (only may be called if we do have
    // an icon!)
    wxRect GetLineIconRect(size_t line) const;

    // get the rect to be highlighted when the item has focus
    wxRect GetLineHighlightRect(size_t line) const;

    // get the size of the total line rect
    wxSize GetLineSize(size_t line) const
        { return GetLineRect(line).GetSize(); }

    // return the hit code for the corresponding position (in this line)
    long HitTestLine(size_t line, int x, int y) const;

    // bring the selected item into view, scrolling to it if necessary
    void MoveToItem(size_t item);

    bool ScrollList( int WXUNUSED(dx), int dy );

    // bring the current item into view
    void MoveToFocus() { MoveToItem(m_current); }

    // start editing the label of the given item
    ibTextCtrl *EditLabel(long item,
                          wxClassInfo* textControlClass = wxCLASSINFO(ibTextCtrl));

    bool EndEditLabel(bool cancel);

    ibTextCtrl *GetEditControl() const
    {
        return m_textctrlWrapper ? m_textctrlWrapper->GetText() : nullptr;
    }

    void ResetTextControl(ibTextCtrl *text)
    {
        delete text;
        m_textctrlWrapper = nullptr;
    }

    void OnRenameTimer();
    bool OnRenameAccept(size_t itemEdit, const wxString& value);
    void OnRenameCancelled(size_t itemEdit);

    void OnFindTimer();
    // set whether or not to ring the find bell
    // (does nothing on MSW - bell is always rung)
    void EnableBellOnNoMatch( bool on );

    void OnMouse( wxMouseEvent &event );

    // called to switch the selection from the current item to newCurrent,
    void OnArrowChar( size_t newCurrent, const wxKeyEvent& event );

    void OnCharHook( wxKeyEvent &event );
    void OnChar( wxKeyEvent &event );
    void OnKeyDown( wxKeyEvent &event );
    void OnKeyUp( wxKeyEvent &event );
    void OnSetFocus( wxFocusEvent &event );
    void OnKillFocus( wxFocusEvent &event );
    void OnScroll( wxScrollWinEvent& event );

    void OnPaint( wxPaintEvent &event );

    void OnSysColourChanged( wxSysColourChangedEvent &event );

    void OnChildFocus(wxChildFocusEvent& event);

    void DrawImage( int index, wxDC *dc, int x, int y );
    void GetImageSize( int &width, int &height ) const;

    void SetImages( wxWithImages *images, const int which );
    void SetItemSpacing( int spacing, bool isSmall = false );
    int GetItemSpacing( bool isSmall = false );

    void SetColumn( int col, const wxListItem &item );
    void SetColumnWidth( int col, int width );
    void GetColumn( int col, wxListItem &item ) const;
    int GetColumnWidth( int col ) const;
    int GetColumnCount() const { return m_columns.size(); }

    // returns the sum of the heights of all columns
    int GetHeaderWidth() const;

    int GetCountPerPage() const;

    void SetItem( wxListItem &item );
    void GetItem( wxListItem &item ) const;
    void SetItemState( long item, long state, long stateMask );
    void SetItemStateAll( long state, long stateMask );
    int GetItemState( long item, long stateMask ) const;
    bool GetItemRect( long item, wxRect &rect ) const
    {
        return GetSubItemRect(item, wxLIST_GETSUBITEMRECT_WHOLEITEM, rect);
    }
    bool GetSubItemRect( long item, long subItem, wxRect& rect,
                         int code = wxLIST_RECT_BOUNDS ) const;
    wxRect GetViewRect() const;
    bool GetItemPosition( long item, wxPoint& pos ) const;
    int GetSelectedItemCount() const;

    bool HasCheckBoxes() const;
    bool EnableCheckBoxes(bool enable = true);
    bool IsItemChecked(long item) const;
    void CheckItem(long item, bool check);

    wxString GetItemText(long item, int col = 0) const
    {
        wxListItem info;
        info.m_mask = wxLIST_MASK_TEXT;
        info.m_itemId = item;
        info.m_col = col;
        GetItem( info );
        return info.m_text;
    }

    void SetItemText(long item, const wxString& value)
    {
        wxListItem info;
        info.m_mask = wxLIST_MASK_TEXT;
        info.m_itemId = item;
        info.m_text = value;
        SetItem( info );
    }

    wxWithImages* GetSmallImages() const
        { return m_small_images; }


    // set the scrollbars and update the positions of the items
    void RecalculatePositions();

    // do the same thing and also call Refresh()
    void RecalculatePositionsAndRefresh();

    long GetNextItem( long item, int geometry, int state ) const;
    void DeleteItem( long index );
    void DeleteAllItems();
    void DeleteColumn( int col );
    void DeleteEverything();
    void EnsureVisible( long index );
    long FindItem( long start, const wxString& str, bool partial = false );
    long FindItem( long start, wxUIntPtr data);
    long FindItem( const wxPoint& pt );
    long HitTest( int x, int y, int &flags ) const;
    void InsertItem( wxListItem &item );
    long InsertColumn( long col, const wxListItem &item );
    int GetItemWidthWithImage(wxListItem * item);
    void SortItems( wxListCtrlCompare fn, wxIntPtr data );

    size_t GetItemCount() const;
    bool IsEmpty() const { return GetItemCount() == 0; }
    void SetItemCount(long count);

    // change the current (== focused) item, send a notification event
    void ChangeCurrent(size_t current);
    void ResetCurrent() { ChangeCurrent((size_t)-1); }
    bool HasCurrent() const { return m_current != (size_t)-1; }

    // send out a wxListEvent
    void SendNotify( size_t line,
                     wxEventType command,
                     const wxPoint& point = wxDefaultPosition );

    // override base class virtual to reset m_lineHeight when the font changes
    virtual bool SetFont(const wxFont& font) override
    {
        if ( !ibWindow::SetFont(font) )
            return false;

        m_lineHeight = 0;

        return true;
    }

    // SEAM vs wx: the lines shown are reckoned anew where the window moves - after the scroll position changed. Reset
    // in OnScroll only, before it, the scroll helper's Update() cached the old range again, and the strip scrolled in
    // at the top was drawn by it: an empty row a wheel notch, scrolling up
    virtual void ScrollWindow(int dx, int dy, const wxRect *rect = nullptr) override;

    void ExtendRulesAndAlternateColour(bool extend)
    {
        m_extendRulesAndAlternateColour = extend;
    }


    // these are for ibListLineData usage only

    // get the backpointer to the list ctrl
    ibListCtrl *GetListCtrl() const
    {
        return wxStaticCast(GetParent(), ibListCtrl);
    }

    // get the height of all lines (assuming they all do have the same height)
    wxCoord GetLineHeight() const;

    // get the y position of the given line (only for report view)
    wxCoord GetLineY(size_t line) const;

    // get the brush to use for the item highlighting
    wxBrush *GetHighlightBrush() const
    {
        return m_hasFocus ? m_highlightBrush : m_highlightUnfocusedBrush;
    }

    bool HasFocus() const override
    {
        return m_hasFocus;
    }

    void UpdateSelectionCount(bool selected)
    {
        wxASSERT_MSG( !IsVirtual(), "Can be called for non virtual lists only" );

        if ( IsSingleSel() )
            return;

        selected ? ++m_selCount : --m_selCount;
    }

    void DrawInReportModeOnBlank ( wxDC *dc,
                                   const wxRect& rect,
                                   int lineNumber );

protected:
    // the array of all line objects for a non virtual list control (for the
    // virtual list control we only ever use m_lines[0])
    std::vector<ibListLineData> m_lines;

    // the list of column objects
    std::vector<ibListHeaderData> m_columns;

    // currently focused item or -1
    size_t               m_current;

    // the number of lines per page
    int                  m_linesPerPage;

    // this flag is set when something which should result in the window
    // redrawing happens (i.e. an item was added or deleted, or its appearance
    // changed) and OnPaint() doesn't redraw the window while it is set which
    // allows to minimize the number of repaintings when a lot of items are
    // being added. The real repainting occurs only after the next OnIdle()
    // call
    bool                 m_dirty;

    wxColour            *m_highlightColour;
    wxWithImages        *m_small_images;
    wxWithImages        *m_normal_images;

    int                  m_small_spacing;
    int                  m_normal_spacing;
    bool                 m_hasFocus;

    bool                 m_lastOnSame;
    wxTimer             *m_renameTimer;

    // incremental search data
    wxString             m_findPrefix;
    wxTimer             *m_findTimer;
    // This flag is set to 0 if the bell is disabled, 1 if it is enabled and -1
    // if it is globally enabled but has been temporarily disabled because we
    // had already beeped for this particular search.
    int                  m_findBell;

    bool                 m_isCreated;
    int                  m_dragCount;
    wxPoint              m_dragStart;
    ColWidthArray        m_aColWidths;

    // for double click logic
    size_t m_lineLastClicked,
           m_lineBeforeLastClicked,
           m_lineSelectSingleOnUp;

    // Multiple selection extends from the anchor. Not used in single-selection mode.
    size_t m_anchor;

    bool m_hasCheckBoxes;

protected:
    wxWindow *GetMainWindowOfCompositeControl() override { return GetParent(); }

    // the total count of items selected in a non virtual list control with
    // multiple selections (always 0 otherwise)
    size_t m_selCount;

    // the total count of items in a virtual list control
    size_t m_countVirt;

    // the object maintaining the items selection state, only used in virtual
    // controls
    wxSelectionStore m_selStore;

    // common part of all ctors
    void Init();

    // get the line data for the given index
    ibListLineData *GetLine(size_t n) const
    {
        ibListMainWindow *self = wxConstCast(this, ibListMainWindow);
        if ( IsVirtual() )
        {
            self->CacheLineData(n);
            n = 0;
        }
        else
        {
            wxCHECK_MSG( n < m_lines.size(), nullptr, wxT("invalid line index") );
        }

        return &self->m_lines[n];
    }

    // get a dummy line which can be used for geometry calculations and such:
    // you must use GetLine() if you want to really draw the line
    ibListLineData *GetDummyLine() const;

    // cache the line data of the n-th line in m_lines[0]
    void CacheLineData(size_t line);

    // get the range of visible lines
    void GetVisibleLinesRange(size_t *from, size_t *to);

    // force us to recalculate the range of visible lines
    void ResetVisibleLinesRange() { m_lineFrom = (size_t)-1; }

    // find the first item starting with the given prefix after the given item
    size_t PrefixFindItem(size_t item, const wxString& prefix) const;

    // get the colour to be used for drawing the rules
    wxColour GetRuleColour() const
    {
        return wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    }

private:
    // initialize the current item if needed
    void UpdateCurrent();

    // change the current (== focused) item, without sending any event
    // return true if m_current really changed.
    bool ChangeCurrentWithoutEvent(size_t current);

    // Trying to activate the current item from keyboard is only possible
    // if it is actually selected. We don't send wxEVT_LIST_ITEM_ACTIVATED
    // event if it is not, and wxEVT_LIST_KEY_DOWN event should carry -1
    // in this case, as the wxMSW implementation does.
    bool ShouldSendEventForCurrent() const
    {
        return HasCurrent() && IsHighlighted(m_current);
    }

    // For multiple selection mode.
    // Change the selected range from [anchor, oldCurrent] to [anchor, newCurrent]
    // without generating unnecessary wxEVT_LIST_ITEM_{DE}SELECTED events.
    void ExtendSelection(size_t oldCurrent, size_t newCurrent);

    // delete all items but don't refresh: called from dtor
    void DoDeleteAllItems();

    // Compute the minimal width needed to fully display the column header.
    int ComputeMinHeaderWidth(const ibListHeaderData* header) const;

    // Check if the given point is inside the checkbox of this item.
    //
    // Always returns false if there are no checkboxes.
    bool IsInsideCheckBox(long item, int x, int y) const;

    // the height of one line using the current font
    wxCoord m_lineHeight;

    // the total header width or 0 if not calculated yet
    wxCoord m_headerWidth;

    // the first and last lines being shown on screen right now (inclusive),
    // both may be -1 if they must be calculated so never access them directly:
    // use GetVisibleLinesRange() above instead
    size_t m_lineFrom,
           m_lineTo;

    // the brushes to use for item highlighting when we do/don't have focus
    wxBrush *m_highlightBrush,
            *m_highlightUnfocusedBrush;

    // wrapper around the text control currently used for in place editing or
    // nullptr if no item is being edited
    ibListTextCtrlWrapper *m_textctrlWrapper;

    // tells whether or not to paint empty rows with alternate colour and draw
    // rulers on empty rows
    bool m_extendRulesAndAlternateColour;

    wxDECLARE_EVENT_TABLE();

    friend class ibListCtrl;
    friend class ibListCtrlMaxWidthCalculator;
};


// NOTE: If using the wxListBox visual attributes works everywhere then this can
// be removed, as well as the #else case below.
#define _USE_VISATTR 0


// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

static const int SCROLL_UNIT_X = 15;

// the spacing between the lines (in report mode)
static const int LINE_SPACING = 0;

// extra margins around the text label
static const int EXTRA_WIDTH = 6;
static const int EXTRA_HEIGHT = 6;

// margin between the window and the items
static const int EXTRA_BORDER_X = 2;
static const int EXTRA_BORDER_Y = 2;

static const int ICON_OFFSET_X = 2;

// offset for the header window
static const int HEADER_OFFSET_X = 0;
static const int HEADER_OFFSET_Y = 0;

// margin between rows of icons in [small] icon view
static const int MARGIN_BETWEEN_ROWS = 6;

// when autosizing the columns, add some slack
static const int AUTOSIZE_COL_MARGIN = 10;

// the space between the image and the text in the report mode
static const int IMAGE_MARGIN_IN_REPORT_MODE = 5;

// the space between the image and the text in the report mode in header
static const int HEADER_IMAGE_MARGIN_IN_REPORT_MODE = 2;

// space after a checkbox
static const int MARGIN_AROUND_CHECKBOX = 5;

// ----------------------------------------------------------------------------
// ibListItemData
// ----------------------------------------------------------------------------

ibListItemData::ibListItemData(ibListItemData&& other)
              : m_image(other.m_image),
                m_data(other.m_data),
                m_owner(other.m_owner),
                m_text(std::move(other.m_text))
{
    // Take ownership of the pointers from the other object and reset them.
    std::swap(m_attr, other.m_attr);
    std::swap(m_rect, other.m_rect);
}

ibListItemData& ibListItemData::operator=(ibListItemData&& other)
{
    m_image = other.m_image;
    m_data = other.m_data;
    m_owner = other.m_owner;
    m_text = std::move(other.m_text);

    // Swap them to let our pointers be deleted by the other object if necessary.
    std::swap(m_attr, other.m_attr);
    std::swap(m_rect, other.m_rect);

    return *this;
}

ibListItemData::~ibListItemData()
{
    // in the virtual list control the attributes are managed by the main
    // program, so don't delete them
    if ( !m_owner->IsVirtual() )
        delete m_attr;

    delete m_rect;
}

ibListItemData::ibListItemData(ibListMainWindow *owner)
{
    m_owner = owner;

    if ( !owner->InReportView() )
        m_rect = new wxRect;
}

// Check if the item is visible
bool ibListCtrl::IsVisible(long item) const
{
    wxRect itemRect;
    GetItemRect( item, itemRect );
    const wxRect clientRect = GetClientRect();
    bool visible = clientRect.Intersects( itemRect );
    if ( visible && m_headerWin )
    {
        wxRect headerRect = m_headerWin->GetClientRect();
        // take into account the +1 added in GetSubItemRect()
        headerRect.height++;
        visible = itemRect.GetBottom() > headerRect.GetBottom();
    }
    return visible;
}

void ibListItemData::SetItem( const wxListItem &info )
{
    if ( info.m_mask & wxLIST_MASK_TEXT )
        SetText(info.m_text);
    if ( info.m_mask & wxLIST_MASK_IMAGE )
        m_image = info.m_image;
    if ( info.m_mask & wxLIST_MASK_DATA )
        m_data = info.m_data;

    if ( info.HasAttributes() )
    {
        if ( m_attr )
            m_attr->AssignFrom(*info.GetAttributes());
        else
            m_attr = new wxItemAttr(*info.GetAttributes());
    }

    if ( m_rect )
    {
        m_rect->x =
        m_rect->y =
        m_rect->height = 0;
        m_rect->width = info.m_width;
    }
}

void ibListItemData::SetPosition( int x, int y )
{
    wxCHECK_RET( m_rect, wxT("unexpected SetPosition() call") );

    m_rect->x = x;
    m_rect->y = y;
}

void ibListItemData::SetSize( int width, int height )
{
    wxCHECK_RET( m_rect, wxT("unexpected SetSize() call") );

    if ( width != -1 )
        m_rect->width = width;
    if ( height != -1 )
        m_rect->height = height;
}

bool ibListItemData::IsHit( int x, int y ) const
{
    wxCHECK_MSG( m_rect, false, wxT("can't be called in this mode") );

    return wxRect(GetX(), GetY(), GetWidth(), GetHeight()).Contains(x, y);
}

int ibListItemData::GetX() const
{
    wxCHECK_MSG( m_rect, 0, wxT("can't be called in this mode") );

    return m_rect->x;
}

int ibListItemData::GetY() const
{
    wxCHECK_MSG( m_rect, 0, wxT("can't be called in this mode") );

    return m_rect->y;
}

int ibListItemData::GetWidth() const
{
    wxCHECK_MSG( m_rect, 0, wxT("can't be called in this mode") );

    return m_rect->width;
}

int ibListItemData::GetHeight() const
{
    wxCHECK_MSG( m_rect, 0, wxT("can't be called in this mode") );

    return m_rect->height;
}

void ibListItemData::GetItem( wxListItem &info ) const
{
    long mask = info.m_mask;
    if ( !mask )
        // by default, get everything for backwards compatibility
        mask = -1;

    if ( mask & wxLIST_MASK_TEXT )
        info.m_text = m_text;
    if ( mask & wxLIST_MASK_IMAGE )
        info.m_image = m_image;
    if ( mask & wxLIST_MASK_DATA )
        info.m_data = m_data;

    if ( m_attr )
    {
        if ( m_attr->HasTextColour() )
            info.SetTextColour(m_attr->GetTextColour());
        if ( m_attr->HasBackgroundColour() )
            info.SetBackgroundColour(m_attr->GetBackgroundColour());
        if ( m_attr->HasFont() )
            info.SetFont(m_attr->GetFont());
    }
}

//-----------------------------------------------------------------------------
//  ibListHeaderData
//-----------------------------------------------------------------------------

void ibListHeaderData::Init()
{
    m_mask = 0;
    m_image = -1;
    m_format = 0;
    m_width = 0;
    m_xpos = 0;
    m_ypos = 0;
    m_height = 0;
    m_state = 0;
}

ibListHeaderData::ibListHeaderData()
{
    Init();
}

ibListHeaderData::ibListHeaderData( const wxListItem &item )
{
    Init();

    SetItem( item );

    // Always give some initial width to the new columns (it's still possible
    // to set the width to 0 explicitly, however).
    if ( !(m_mask & wxLIST_MASK_WIDTH) )
        SetWidth(wxLIST_DEFAULT_COL_WIDTH);
}

void ibListHeaderData::SetItem( const wxListItem &item )
{
    m_mask = item.m_mask;

    if ( m_mask & wxLIST_MASK_TEXT )
        m_text = item.m_text;

    if ( m_mask & wxLIST_MASK_IMAGE )
        m_image = item.m_image;

    if ( m_mask & wxLIST_MASK_FORMAT )
        m_format = item.m_format;

    if ( m_mask & wxLIST_MASK_WIDTH )
        SetWidth(item.m_width);

    if ( m_mask & wxLIST_MASK_STATE )
        SetState(item.m_state);
}

void ibListHeaderData::SetPosition( int x, int y )
{
    m_xpos = x;
    m_ypos = y;
}

void ibListHeaderData::SetHeight( int h )
{
    m_height = h;
}

void ibListHeaderData::SetWidth( int w )
{
    m_width = w < 0 ? wxLIST_DEFAULT_COL_WIDTH : w;
}

void ibListHeaderData::SetState( int flag )
{
    m_state = flag;
}

void ibListHeaderData::SetFormat( int format )
{
    m_format = format;
}

bool ibListHeaderData::HasImage() const
{
    return m_image != -1;
}

bool ibListHeaderData::IsHit( int x, int y ) const
{
    return ((x >= m_xpos) && (x <= m_xpos+m_width) && (y >= m_ypos) && (y <= m_ypos+m_height));
}

void ibListHeaderData::GetItem( wxListItem& item ) const
{
    long mask = item.m_mask;
    if ( !mask )
    {
        // by default, get everything for backwards compatibility
        mask = -1;
    }

    if ( mask & wxLIST_MASK_STATE )
        item.m_state = m_state;
    if ( mask & wxLIST_MASK_TEXT )
        item.m_text = m_text;
    if ( mask & wxLIST_MASK_IMAGE )
        item.m_image = m_image;
    if ( mask & wxLIST_MASK_WIDTH )
        item.m_width = m_width;
    if ( mask & wxLIST_MASK_FORMAT )
        item.m_format = m_format;
}

int ibListHeaderData::GetImage() const
{
    return m_image;
}

int ibListHeaderData::GetWidth() const
{
    return m_width;
}

int ibListHeaderData::GetFormat() const
{
    return m_format;
}

int ibListHeaderData::GetState() const
{
    return m_state;
}

//-----------------------------------------------------------------------------
//  ibListLineData
//-----------------------------------------------------------------------------

inline int ibListLineData::GetMode() const
{
    return m_owner->GetListCtrl()->GetWindowStyleFlag() & wxLC_MASK_TYPE;
}

inline bool ibListLineData::InReportView() const
{
    return m_owner->HasFlag(wxLC_REPORT);
}

inline bool ibListLineData::IsVirtual() const
{
    return m_owner->IsVirtual();
}

ibListLineData::ibListLineData( ibListMainWindow *owner )
{
    m_owner = owner;

    SetReportView(InReportView());

    m_highlighted = false;
    m_checked = false;

    InitItems( GetMode() == wxLC_REPORT ? m_owner->GetColumnCount() : 1 );
}

void ibListLineData::CalculateSize( wxReadOnlyDC *dc, int spacing )
{
    wxCHECK_RET( !m_items.empty(), wxT("no subitems at all??") );

    ibListItemData* const item = &m_items[0];

    wxString s;
    wxCoord lw, lh;

    switch ( GetMode() )
    {
        case wxLC_ICON:
        case wxLC_SMALL_ICON:
            m_gi->m_rectAll.width = spacing;

            s = item->GetText();

            if ( s.empty() )
            {
                lh =
                m_gi->m_rectLabel.width =
                m_gi->m_rectLabel.height = 0;
            }
            else // has label
            {
                dc->GetTextExtent( s, &lw, &lh );
                lw += EXTRA_WIDTH;
                lh += EXTRA_HEIGHT;

                m_gi->m_rectAll.height = spacing + lh;
                if (lw > spacing)
                    m_gi->m_rectAll.width = lw;

                m_gi->m_rectLabel.width = lw;
                m_gi->m_rectLabel.height = lh;
            }

            if (item->HasImage())
            {
                int w, h;
                m_owner->GetImageSize( w, h );
                m_gi->m_rectIcon.width = w + 8;
                m_gi->m_rectIcon.height = h + 8;

                if ( m_gi->m_rectIcon.width > m_gi->m_rectAll.width )
                    m_gi->m_rectAll.width = m_gi->m_rectIcon.width;
                if ( m_gi->m_rectIcon.height + lh > m_gi->m_rectAll.height - 4 )
                    m_gi->m_rectAll.height = m_gi->m_rectIcon.height + lh + 4;
            }

            if ( item->HasText() )
            {
                m_gi->m_rectHighlight.width = m_gi->m_rectLabel.width;
                m_gi->m_rectHighlight.height = m_gi->m_rectLabel.height;
            }
            else // no text, highlight the icon
            {
                m_gi->m_rectHighlight.width = m_gi->m_rectIcon.width;
                m_gi->m_rectHighlight.height = m_gi->m_rectIcon.height;
            }
            break;

        case wxLC_LIST:
            s = item->GetTextForMeasuring();

            dc->GetTextExtent( s, &lw, &lh );
            lw += EXTRA_WIDTH;
            lh += EXTRA_HEIGHT;

            m_gi->m_rectLabel.width = lw;
            m_gi->m_rectLabel.height = lh;

            m_gi->m_rectAll.width = lw;
            m_gi->m_rectAll.height = lh;

            if (item->HasImage())
            {
                int w, h;
                m_owner->GetImageSize( w, h );
                m_gi->m_rectIcon.width = w;
                m_gi->m_rectIcon.height = h;

                m_gi->m_rectAll.width += 4 + w;
                if (h > m_gi->m_rectAll.height)
                    m_gi->m_rectAll.height = h;
            }

            m_gi->m_rectHighlight.width = m_gi->m_rectAll.width;
            m_gi->m_rectHighlight.height = m_gi->m_rectAll.height;
            break;

        case wxLC_REPORT:
            wxFAIL_MSG( wxT("unexpected call to SetSize") );
            break;

        default:
            wxFAIL_MSG( wxT("unknown mode") );
            break;
    }
}

void ibListLineData::SetPosition( int x, int y, int WXUNUSED(spacing) )
{
    wxCHECK_RET( !m_items.empty(), wxT("no subitems at all??") );

    ibListItemData* const item = &m_items[0];

    switch ( GetMode() )
    {
        case wxLC_ICON:
        case wxLC_SMALL_ICON:
            m_gi->m_rectAll.x = x;
            m_gi->m_rectAll.y = y;

            if ( item->HasImage() )
            {
                m_gi->m_rectIcon.x = m_gi->m_rectAll.x + 4 +
                    (m_gi->m_rectAll.width - m_gi->m_rectIcon.width) / 2;
                m_gi->m_rectIcon.y = m_gi->m_rectAll.y + 4;
            }

            if ( item->HasText() )
            {
                m_gi->m_rectLabel.x = m_gi->m_rectAll.x + (EXTRA_WIDTH/2) +
                    (m_gi->m_rectAll.width - m_gi->m_rectLabel.width) / 2;
                m_gi->m_rectLabel.y = m_gi->m_rectAll.y + m_gi->m_rectAll.height +
                    (EXTRA_HEIGHT/2) - m_gi->m_rectLabel.height;
                m_gi->m_rectHighlight.x = m_gi->m_rectLabel.x - (EXTRA_WIDTH/2);
                m_gi->m_rectHighlight.y = m_gi->m_rectLabel.y - (EXTRA_HEIGHT/2);
            }
            else // no text, highlight the icon
            {
                m_gi->m_rectHighlight.x = m_gi->m_rectIcon.x - 4;
                m_gi->m_rectHighlight.y = m_gi->m_rectIcon.y - 4;
            }
            break;

        case wxLC_LIST:
            m_gi->m_rectAll.x = x;
            m_gi->m_rectAll.y = y;

            m_gi->m_rectHighlight.x = m_gi->m_rectAll.x;
            m_gi->m_rectHighlight.y = m_gi->m_rectAll.y;
            m_gi->m_rectLabel.y = m_gi->m_rectAll.y + 2;

            if (item->HasImage())
            {
                m_gi->m_rectIcon.x = m_gi->m_rectAll.x + 2;
                m_gi->m_rectIcon.y = m_gi->m_rectAll.y + 2;
                m_gi->m_rectLabel.x = m_gi->m_rectAll.x + 4 + (EXTRA_WIDTH/2) + m_gi->m_rectIcon.width;
            }
            else
            {
                m_gi->m_rectLabel.x = m_gi->m_rectAll.x + (EXTRA_WIDTH/2);
            }
            break;

        case wxLC_REPORT:
            wxFAIL_MSG( wxT("unexpected call to SetPosition") );
            break;

        default:
            wxFAIL_MSG( wxT("unknown mode") );
            break;
    }
}

void ibListLineData::InitItems( int num )
{
    for (int i = 0; i < num; i++)
        m_items.emplace_back( m_owner );
}

void ibListLineData::SetItem( int index, const wxListItem &info )
{
    m_items.at(index).SetItem( info );
}

void ibListLineData::GetItem( int index, wxListItem &info ) const
{
    m_items.at(index).GetItem( info );
}

wxString ibListLineData::GetText(int index) const
{
    return m_items.at(index).GetText();
}

void ibListLineData::SetText( int index, const wxString& s )
{
    m_items.at(index).SetText(s);
}

void ibListLineData::SetImage( int index, int image )
{
    m_items.at(index).SetImage(image);
}

int ibListLineData::GetImage( int index ) const
{
    return m_items.at(index).GetImage();
}

wxItemAttr *ibListLineData::GetAttr() const
{
    return m_items.at(0).GetAttr();
}

void ibListLineData::SetAttr(wxItemAttr *attr)
{
    m_items.at(0).SetAttr(attr);
}

void ibListLineData::ApplyAttributes(wxDC *dc,
                                     const wxRect& rectHL,
                                     bool highlighted,
                                     bool current)
{
    const wxItemAttr * const attr = GetAttr();

    wxWindow * const listctrl = m_owner->GetParent();

    const bool hasFocus = listctrl->HasFocus();

    // fg colour

    // don't use foreground colour for drawing highlighted items - this might
    // make them completely invisible (and there is no way to do bit
    // arithmetics on wxColour, unfortunately)
    wxColour colText;
    if ( highlighted )
    {
#ifdef __WXMAC__
        if ( hasFocus )
            colText = *wxWHITE;
        else
            colText = *wxBLACK;
#else
        colText = wxTHEME_COLOUR(HIGHLIGHT_TEXT); // SEAM: theme, not wxSYS
#endif
    }
    else if ( attr && attr->HasTextColour() )
        colText = attr->GetTextColour();
    else
        colText = listctrl->GetForegroundColour();

    dc->SetTextForeground(colText);

    // font
    wxFont font;
    if ( attr && attr->HasFont() )
        font = attr->GetFont();
    else
        font = listctrl->GetFont();

    dc->SetFont(font);

    // background
    if ( highlighted )
    {
        // SEAM: the theme's selection (ibThemeRenderer::DrawItemSelectionRect), not the native look - its pale blue
        // hid the theme's white selected text
        int flags = wxCONTROL_SELECTED;
        if ( hasFocus )
            flags |= wxCONTROL_FOCUSED;
        if (current)
           flags |= wxCONTROL_CURRENT;
        ibThemeEngine::Get()->GetRenderer()->DrawItemSelectionRect( m_owner, *dc, rectHL, flags );
    }
    else if ( attr && attr->HasBackgroundColour() )
    {
        // Draw the background using the items custom background colour.
        dc->SetBrush(attr->GetBackgroundColour());
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(rectHL);
    }

    // just for debugging to better see where the items are
#if 0
    dc->SetPen(*wxRED_PEN);
    dc->SetBrush(*wxTRANSPARENT_BRUSH);
    dc->DrawRectangle( m_gi->m_rectAll );
    dc->SetPen(*wxGREEN_PEN);
    dc->DrawRectangle( m_gi->m_rectIcon );
#endif
}

void ibListLineData::Draw(wxDC *dc, const wxPoint& origin, bool current)
{
    wxCHECK_RET( !m_items.empty(), wxT("no subitems at all??") );

    wxRect rect(m_gi->m_rectHighlight);
    rect.Offset(origin);

    ApplyAttributes(dc, rect, IsHighlighted(), current);

    ibListItemData* const item = &m_items[0];
    if (item->HasImage())
    {
        // centre the image inside our rectangle, this looks nicer when items
        // ae aligned in a row
        const wxRect& rectIcon = m_gi->m_rectIcon;
        m_owner->DrawImage(
            item->GetImage(), dc, rectIcon.x + origin.x, rectIcon.y + origin.y);
    }

    if (item->HasText())
    {
        wxRect rectLabel = m_gi->m_rectLabel;
        rectLabel.Offset(origin);

        wxDCClipper clipper(*dc, rectLabel);
        dc->DrawText(item->GetText(), rectLabel.x, rectLabel.y);
    }
}

void ibListLineData::DrawInReportMode( wxDC *dc,
                                       const wxRect& rect,
                                       bool highlighted,
                                       bool current,
                                       bool checked )
{
    // TODO: later we should support setting different attributes for
    //       different columns - to do it, just add "col" argument to
    //       GetAttr() and move these lines into the loop below

    // Note: GetSubItemRect() needs to be modified if the layout here changes.

    ApplyAttributes(dc, rect, highlighted, current);

    wxCoord x = rect.x;
    wxCoord yMid = rect.y + rect.height/2;

    if ( m_owner->HasCheckBoxes() )
    {
        // SEAM vs wx: the window's renderer draws the box - the theme's, as ibCheckBox's; wx asked the native one
        wxSize cbSize = m_owner->GetRenderer()->GetCheckBoxSize(m_owner);
        int yOffset = (rect.height - cbSize.GetHeight()) / 2;
        wxRect rr(wxPoint(x, rect.y + yOffset), cbSize);
        rr.x += MARGIN_AROUND_CHECKBOX;

        int flags = 0;
        if (checked)
            flags |= wxCONTROL_CHECKED;
        m_owner->GetRenderer()->DrawCheckBox(m_owner, *dc, rr, flags);

        x += cbSize.GetWidth() + (2 * MARGIN_AROUND_CHECKBOX);
    }

    x += ICON_OFFSET_X;

    size_t col = 0;
    for ( const auto& item : m_items )
    {
        int width = m_owner->GetColumnWidth(col);
        if (col == 0 && m_owner->HasCheckBoxes())
            width -= x;
        int xOld = x;
        x += width;

        width -= 8;
        const int wText = width;
        wxDCClipper clipper(*dc, xOld, rect.y, wText, rect.height);

        if ( item.HasImage() )
        {
            int ix, iy;
            m_owner->GetImageSize( ix, iy );
            m_owner->DrawImage( item.GetImage(), dc, xOld, yMid - iy/2 );

            ix += IMAGE_MARGIN_IN_REPORT_MODE;

            xOld += ix;
            width -= ix;
        }

        if ( item.HasText() )
            DrawTextFormatted(dc, item.GetText(), col, xOld, yMid, width);

        ++col;
    }
}

void ibListLineData::DrawTextFormatted(wxDC *dc,
                                       const wxString& textOrig,
                                       int col,
                                       int x,
                                       int yMid,
                                       int width)
{
    // we don't support displaying multiple lines currently (and neither does
    // wxMSW FWIW) so just merge all the lines
    wxString text(textOrig);
    text.Replace(wxT("\n"), wxT(" "));

    wxCoord w, h;
    dc->GetTextExtent(text, &w, &h);

    const wxCoord y = yMid - (h + 1)/2;

    wxDCClipper clipper(*dc, x, y, width, h);

    // determine if the string can fit inside the current width
    if (w <= width)
    {
        // it can, draw it using the items alignment
        wxListItem item;
        m_owner->GetColumn(col, item);
        switch ( item.GetAlign() )
        {
            case wxLIST_FORMAT_LEFT:
                // nothing to do
                break;

            case wxLIST_FORMAT_RIGHT:
                x += width - w;
                break;

            case wxLIST_FORMAT_CENTER:
                x += (width - w) / 2;
                break;

            default:
                wxFAIL_MSG( wxT("unknown list item format") );
                break;
        }

        dc->DrawText(text, x, y);
    }
    else // otherwise, truncate and add an ellipsis if possible
    {
        // determine the base width
        wxString ellipsis(wxT("..."));
        wxCoord base_w;
        dc->GetTextExtent(ellipsis, &base_w, &h);

        // continue until we have enough space or only one character left
        wxCoord w_c, h_c;
        size_t len = text.length();
        wxString drawntext = text.Left(len);
        while (len > 1)
        {
            dc->GetTextExtent(drawntext.Last(), &w_c, &h_c);
            drawntext.RemoveLast();
            len--;
            w -= w_c;
            if (w + base_w <= width)
                break;
        }

        // if still not enough space, remove ellipsis characters
        while (ellipsis.length() > 0 && w + base_w > width)
        {
            ellipsis = ellipsis.Left(ellipsis.length() - 1);
            dc->GetTextExtent(ellipsis, &base_w, &h);
        }

        // now draw the text
        dc->DrawText(drawntext, x, y);
        dc->DrawText(ellipsis, x + w, y);
    }
}

bool ibListLineData::Highlight( bool on )
{
    wxCHECK_MSG( !IsVirtual(), false, wxT("unexpected call to Highlight") );

    if ( on == m_highlighted )
        return false;

    m_highlighted = on;
    m_owner->UpdateSelectionCount(on);

    return true;
}

void ibListLineData::ReverseHighlight( void )
{
    Highlight(!IsHighlighted());
}

//-----------------------------------------------------------------------------
//  ibListHeaderWindow
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibListHeaderWindow,ibWindow)
    EVT_PAINT         (ibListHeaderWindow::OnPaint)
    EVT_MOUSE_EVENTS  (ibListHeaderWindow::OnMouse)
    EVT_SYS_COLOUR_CHANGED(ibListHeaderWindow::OnSysColourChanged)
wxEND_EVENT_TABLE()

void ibListHeaderWindow::Init()
{
    m_currentCursor = nullptr;
    m_isDragging = false;
    m_dirty = false;
    m_sendSetColumnWidth = false;
}

ibListHeaderWindow::ibListHeaderWindow()
{
    Init();

    m_owner = nullptr;
    m_resizeCursor = nullptr;

    m_sortAsc = true;
    m_sortCol = -1;
}

bool ibListHeaderWindow::Create( wxWindow *win,
                                 wxWindowID id,
                                 ibListMainWindow *owner,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString &name )
{
    if ( !ibWindow::Create(win, id, pos, size, style, name) )
        return false;

    Init();

    m_owner = owner;
    m_resizeCursor = new wxCursor( wxCURSOR_SIZEWE );

    // SEAM vs wx: no colours of the system's set on the header - its buttons and their titles are the theme's
    // (ibThemeRenderer::DrawHeaderButton, OnPaint)
#if _USE_VISATTR
    wxVisualAttributes attr = wxPanel::GetClassDefaultAttributes();
    if (!m_hasFont)
        SetOwnFont( attr.font );
#else
    if (!m_hasFont)
        SetOwnFont( wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT ));
#endif

    return true;
}

ibListHeaderWindow::~ibListHeaderWindow()
{
    delete m_resizeCursor;
}

#ifdef __WXUNIVERSAL__
#include "wx/univ/renderer.h"
#include "wx/univ/theme.h"
#endif

// shift the DC origin to match the position of the main window horz
// scrollbar: this allows us to always use logical coords
void ibListHeaderWindow::AdjustDC(wxDC& dc)
{
    ibListCtrl *parent = m_owner->GetListCtrl();

    int xpix;
    parent->GetScrollPixelsPerUnit( &xpix, nullptr );

    int view_start;
    parent->GetViewStart( &view_start, nullptr );


    int org_x = 0;
    int org_y = 0;
    dc.GetDeviceOrigin( &org_x, &org_y );

    // account for the horz scrollbar offset
#if defined(__WXGTK__) && !defined(__WXGTK3__)
    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        // Maybe we just have to check for m_signX
        // in the DC, but I leave the #ifdef __WXGTK__
        // for now
        dc.SetDeviceOrigin( org_x + (view_start * xpix), org_y );
    }
    else
#endif
        dc.SetDeviceOrigin( org_x - (view_start * xpix), org_y );
}

void ibListHeaderWindow::OnPaint( wxPaintEvent &WXUNUSED(event) )
{
    ibListCtrl *parent = m_owner->GetListCtrl();

    // SEAM vs wx: buffered - straight on the paint DC each button went over
    // the last on screen, and a column dragged wider flickered at every step
    ibThemePaintDC dc( this );
    ibRenderer* const renderer = ibThemeEngine::Get()->GetRenderer();

    AdjustDC( dc );

    // width and height of the entire header window
    int w, h;
    GetClientSize( &w, &h );
    parent->CalcUnscrolledPosition(w, 0, &w, nullptr);

    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetTextForeground(wxTHEME_COLOUR(CONTROL_TEXT));

    int x = HEADER_OFFSET_X;
    int numColumns = m_owner->GetColumnCount();
    wxListItem item;
    for ( int i = 0; i < numColumns && x < w; i++ )
    {
        m_owner->GetColumn( i, item );
        int wCol = item.m_width;

        int cw = wCol;
        int ch = h;

        int flags = 0;
        if (!m_parent->IsEnabled())
            flags |= wxCONTROL_DISABLED;

// NB: The code below is not really Mac-specific, but since we are close
// to 2.8 release and I don't have time to test on other platforms, I
// defined this only for wxMac. If this behaviour is desired on
// other platforms, please go ahead and revise or remove the #ifdef.
#ifdef __WXMAC__
        if ( !m_owner->IsVirtual() && (item.m_mask & wxLIST_MASK_STATE) &&
                (item.m_state & wxLIST_STATE_SELECTED) )
            flags |= wxCONTROL_SELECTED;
#endif

        wxHeaderSortIconType sortArrow = wxHDR_SORT_ICON_NONE;
        if ( i == m_sortCol )
        {
            if ( m_sortAsc )
                sortArrow = wxHDR_SORT_ICON_UP;
            else
                sortArrow = wxHDR_SORT_ICON_DOWN;
        }

        if (i == 0)
           flags |= wxCONTROL_SPECIAL; // mark as first column

        renderer->DrawHeaderButton
                                (
                                    this,
                                    dc,
                                    wxRect(x, HEADER_OFFSET_Y, cw, ch),
                                    flags,
                                    sortArrow
                                );

        // see if we have enough space for the column label

        // for this we need the width of the text
        wxCoord wLabel;
        wxCoord hLabel;
        dc.GetTextExtent(item.GetText(), &wLabel, &hLabel);
        wLabel += 2 * EXTRA_WIDTH;

        // and the width of the icon, if any
        int ix = 0, iy = 0;    // init them just to suppress the compiler warnings
        const int image = item.m_image;
        wxWithImages *smallImages;
        if ( image != -1 )
        {
            smallImages = m_owner->GetSmallImages();
            if ( smallImages )
            {
                smallImages->GetImageLogicalSize(this, ix, iy);
                wLabel += ix + HEADER_IMAGE_MARGIN_IN_REPORT_MODE;
            }
        }
        else
        {
            smallImages = nullptr;
        }

        // ignore alignment if there is not enough space anyhow
        int xAligned;
        switch ( wLabel < cw ? item.GetAlign() : wxLIST_FORMAT_LEFT )
        {
            default:
                wxFAIL_MSG( wxT("unknown list item format") );
                wxFALLTHROUGH;

            case wxLIST_FORMAT_LEFT:
                xAligned = x;
                break;

            case wxLIST_FORMAT_RIGHT:
                xAligned = x + cw - wLabel;
                break;

            case wxLIST_FORMAT_CENTER:
                xAligned = x + (cw - wLabel) / 2;
                break;
        }

        // draw the text and image clipping them so that they
        // don't overwrite the column boundary
        wxDCClipper clipper(dc, x, HEADER_OFFSET_Y, cw, h);

        // if we have an image, draw it on the right of the label
        if ( smallImages )
        {
            wxDrawImageBitmap
            (
                this, *smallImages, image,
                dc,
                xAligned + wLabel - ix - HEADER_IMAGE_MARGIN_IN_REPORT_MODE,
                HEADER_OFFSET_Y + (h - iy)/2
            );
        }

        dc.DrawText( item.GetText(),
                     xAligned + EXTRA_WIDTH, (h - hLabel) / 2 );

        x += wCol;
    }

    // Fill in what's missing to the right of the columns, otherwise we will
    // leave an unpainted area when columns are removed (and it looks better)
    if ( x < w )
    {
        renderer->DrawHeaderButton
                                (
                                    this,
                                    dc,
                                    wxRect(x, HEADER_OFFSET_Y, w - x, h),
                                    wxCONTROL_DIRTY // mark as last column
                                );
    }
}

void ibListHeaderWindow::OnInternalIdle()
{
    ibWindow::OnInternalIdle();

    if (m_sendSetColumnWidth)
    {
        m_owner->SetColumnWidth( m_colToSend, m_widthToSend );
        m_sendSetColumnWidth = false;

        // SEAM vs wx: lay the rows out and refresh them with the header now. The control's idle, which does it for
        // every other change (ibListCtrl::OnInternalIdle), came before this one - a parent's idle runs before its
        // children's - so the width being dragged waited for the next message, and each paint met the rows still
        // dirty: they stood still until the mouse was let go
        m_owner->RecalculatePositionsAndRefresh();
    }
}

void ibListHeaderWindow::DrawCurrent()
{
    m_sendSetColumnWidth = true;
    m_colToSend = m_column;
    m_widthToSend = m_currentX - m_minX;
}

void ibListHeaderWindow::OnMouse( wxMouseEvent &event )
{
    ibListCtrl *parent = m_owner->GetListCtrl();

    // we want to work with logical coords
    int x;
    parent->CalcUnscrolledPosition(event.GetX(), 0, &x, nullptr);

    if (m_isDragging)
    {
        SendListEvent(wxEVT_LIST_COL_DRAGGING, event.GetPosition());

        // we don't draw the line beyond our window, but we allow dragging it
        // there
        int w = 0;
        GetClientSize( &w, nullptr );
        parent->CalcUnscrolledPosition(w, 0, &w, nullptr);
        w -= 6;

        // erase the line if it was drawn
        if ( m_currentX < w )
            DrawCurrent();

        if (event.ButtonUp())
        {
            ReleaseMouse();
            m_isDragging = false;
            m_dirty = true;
            m_owner->SetColumnWidth( m_column, m_currentX - m_minX );
            SendListEvent(wxEVT_LIST_COL_END_DRAG, event.GetPosition());
        }
        else
        {
            if (x > m_minX + 7)
                m_currentX = x;
            else
                m_currentX = m_minX + 7;

            // draw in the new location
            if ( m_currentX < w )
                DrawCurrent();
        }
    }
    else // not dragging
    {
        m_minX = 0;
        bool hit_border = false;

        // end of the current column
        int xpos = 0;

        // find the column where this event occurred
        int col,
            countCol = m_owner->GetColumnCount();
        for (col = 0; col < countCol; col++)
        {
            xpos += m_owner->GetColumnWidth( col );
            m_column = col;

            if ( abs(x-xpos) < 3 )
            {
                // near the column border
                hit_border = true;
                break;
            }

            if ( x < xpos )
            {
                // inside the column
                break;
            }

            m_minX = xpos;
        }

        if ( col == countCol )
            m_column = -1;

        if (event.LeftDown() || event.RightUp())
        {
            if (hit_border && event.LeftDown())
            {
                if ( SendListEvent(wxEVT_LIST_COL_BEGIN_DRAG,
                                   event.GetPosition()) )
                {
                    m_isDragging = true;
                    m_currentX = x;
                    CaptureMouse();
                    DrawCurrent();
                }
                //else: column resizing was vetoed by the user code
            }
            else // click on a column
            {
                // record the selected state of the columns
                if (event.LeftDown())
                {
                    for (int i=0; i < m_owner->GetColumnCount(); i++)
                    {
                        wxListItem colItem;
                        m_owner->GetColumn(i, colItem);
                        long state = colItem.GetState();
                        if (i == m_column)
                            colItem.SetState(state | wxLIST_STATE_SELECTED);
                        else
                            colItem.SetState(state & ~wxLIST_STATE_SELECTED);
                        m_owner->SetColumn(i, colItem);
                    }
                }

                SendListEvent( event.LeftDown()
                                    ? wxEVT_LIST_COL_CLICK
                                    : wxEVT_LIST_COL_RIGHT_CLICK,
                                event.GetPosition());
            }
        }
        else if ( event.LeftDClick() && hit_border )
        {
            // Autosize the column when the divider is clicked: if there are
            // any items, fit the columns to its contents, otherwise just fit
            // it to its label width.
            parent->SetColumnWidth(m_column,
                                   parent->IsEmpty() ? wxLIST_AUTOSIZE_USEHEADER
                                                     : wxLIST_AUTOSIZE);
        }
        else if (event.Moving())
        {
            bool setCursor;
            if (hit_border)
            {
                setCursor = m_currentCursor == wxSTANDARD_CURSOR;
                m_currentCursor = m_resizeCursor;
            }
            else
            {
                setCursor = m_currentCursor != wxSTANDARD_CURSOR;
                m_currentCursor = wxSTANDARD_CURSOR;
            }

            if ( setCursor )
                SetCursor(*m_currentCursor);
        }
    }
}

void ibListHeaderWindow::OnSysColourChanged(wxSysColourChangedEvent &event)
{
    SetOwnForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
    SetOwnBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

    Refresh();

    event.Skip();
}

bool ibListHeaderWindow::SendListEvent(wxEventType type, const wxPoint& pos)
{
    wxWindow *parent = GetParent();
    wxListEvent le( type, parent->GetId() );
    le.SetEventObject( parent );
    le.m_pointDrag = pos;
    le.m_col = m_column;
    return !parent->GetEventHandler()->ProcessEvent( le ) || le.IsAllowed();
}

//-----------------------------------------------------------------------------
// ibListRenameTimer (internal)
//-----------------------------------------------------------------------------

ibListRenameTimer::ibListRenameTimer( ibListMainWindow *owner )
{
    m_owner = owner;
}

void ibListRenameTimer::Notify()
{
    m_owner->OnRenameTimer();
}

//-----------------------------------------------------------------------------
// ibListFindTimer (internal)
//-----------------------------------------------------------------------------

void ibListFindTimer::Notify()
{
    m_owner->OnFindTimer();
}

//-----------------------------------------------------------------------------
// ibListTextCtrlWrapper (internal)
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibListTextCtrlWrapper, wxEvtHandler)
    EVT_CHAR           (ibListTextCtrlWrapper::OnChar)
    EVT_KEY_UP         (ibListTextCtrlWrapper::OnKeyUp)
    EVT_KILL_FOCUS     (ibListTextCtrlWrapper::OnKillFocus)
wxEND_EVENT_TABLE()

ibListTextCtrlWrapper::ibListTextCtrlWrapper(ibListMainWindow *owner,
                                             ibTextCtrl *text,
                                             size_t itemEdit)
              : m_startValue(owner->GetItemText(itemEdit)),
                m_itemEdited(itemEdit)
{
    m_owner = owner;
    m_text = text;
    m_aboutToFinish = false;

    ibListCtrl *parent = m_owner->GetListCtrl();

    wxRect rectLabel = owner->GetLineLabelRect(itemEdit);

    parent->CalcScrolledPosition(rectLabel.x, rectLabel.y,
                                  &rectLabel.x, &rectLabel.y);

    m_text->Create(owner, wxID_ANY, m_startValue,
                   wxPoint(rectLabel.x-4,rectLabel.y-4),
                   wxSize(rectLabel.width+11,rectLabel.height+8));
    m_text->SetFocus();

    m_text->PushEventHandler(this);
}

void ibListTextCtrlWrapper::EndEdit(EndReason reason)
{
    if( m_aboutToFinish )
    {
        // We already called Finish which cannot be called
        // more than once.
        return;
    }

    m_aboutToFinish = true;

    switch ( reason )
    {
        case End_Accept:
            // Notify the owner about the changes
            AcceptChanges();

            // Even if vetoed, close the control (consistent with MSW)
            Finish( true );
            break;

        case End_Discard:
            m_owner->OnRenameCancelled(m_itemEdited);

            Finish( true );
            break;

        case End_Destroy:
            // Don't generate any notifications for the control being destroyed
            // and don't set focus to it either.
            Finish(false);
            break;
    }
}

void ibListTextCtrlWrapper::Finish( bool setfocus )
{
    m_text->RemoveEventHandler(this);
    m_owner->ResetTextControl( m_text );

    wxPendingDelete.Append( this );

    if (setfocus)
        m_owner->SetFocus();
}

bool ibListTextCtrlWrapper::AcceptChanges()
{
    const wxString value = m_text->GetValue();

    // notice that we should always call OnRenameAccept() to generate the "end
    // label editing" event, even if the user hasn't really changed anything
    if ( !m_owner->OnRenameAccept(m_itemEdited, value) )
    {
        // vetoed by the user
        return false;
    }

    // accepted, do rename the item (unless nothing changed)
    if ( value != m_startValue )
        m_owner->SetItemText(m_itemEdited, value);

    return true;
}

void ibListTextCtrlWrapper::OnChar( wxKeyEvent &event )
{
    if ( !CheckForEndEditKey(event) )
        event.Skip();
}

bool ibListTextCtrlWrapper::CheckForEndEditKey(const wxKeyEvent& event)
{
    switch ( event.m_keyCode )
    {
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            EndEdit( End_Accept );
            break;

        case WXK_ESCAPE:
            EndEdit( End_Discard );
            break;

        default:
            return false;
    }

    return true;
}

void ibListTextCtrlWrapper::OnKeyUp( wxKeyEvent &event )
{
    if ( !m_aboutToFinish )
    {
        // auto-grow the textctrl:
        wxSize parentSize = m_owner->GetSize();
        wxPoint myPos = m_text->GetPosition();
        wxSize mySize = m_text->GetSize();
        int sx, sy;
        m_text->GetTextExtent(m_text->GetValue() + wxT("MM"), &sx, &sy);
        if (myPos.x + sx > parentSize.x)
            sx = parentSize.x - myPos.x;
       if (mySize.x > sx)
            sx = mySize.x;
       m_text->SetSize(sx, wxDefaultCoord);
    }

    event.Skip();
}

void ibListTextCtrlWrapper::OnKillFocus( wxFocusEvent &event )
{
    if ( !m_aboutToFinish )
    {
        m_aboutToFinish = true;
        if ( !AcceptChanges() )
            m_owner->OnRenameCancelled( m_itemEdited );

        Finish( false );
    }

    // We must let the native text control handle focus
    event.Skip();
}

//-----------------------------------------------------------------------------
//  ibListMainWindow
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(ibListMainWindow, ibWindow)
  EVT_PAINT          (ibListMainWindow::OnPaint)
  EVT_MOUSE_EVENTS   (ibListMainWindow::OnMouse)
  EVT_CHAR_HOOK      (ibListMainWindow::OnCharHook)
  EVT_CHAR           (ibListMainWindow::OnChar)
  EVT_KEY_DOWN       (ibListMainWindow::OnKeyDown)
  EVT_KEY_UP         (ibListMainWindow::OnKeyUp)
  EVT_SET_FOCUS      (ibListMainWindow::OnSetFocus)
  EVT_KILL_FOCUS     (ibListMainWindow::OnKillFocus)
  EVT_SCROLLWIN      (ibListMainWindow::OnScroll)
  EVT_CHILD_FOCUS    (ibListMainWindow::OnChildFocus)
  EVT_SYS_COLOUR_CHANGED(ibListMainWindow::OnSysColourChanged)
wxEND_EVENT_TABLE()

void ibListMainWindow::Init()
{
    m_dirty = true;
    m_selCount =
    m_countVirt = 0;
    m_lineFrom =
    m_lineTo = (size_t)-1;
    m_linesPerPage = 0;

    m_headerWidth =
    m_lineHeight = 0;

    m_small_images = nullptr;
    m_normal_images = nullptr;

    m_small_spacing = 30;
    m_normal_spacing = 40;

    m_hasFocus = false;
    m_dragCount = 0;
    m_isCreated = false;

    m_lastOnSame = false;
    m_renameTimer = new ibListRenameTimer( this );
    m_findTimer = nullptr;
    m_findBell = 0;  // default is to not ring bell at all
    m_textctrlWrapper = nullptr;

    m_current =
    m_lineLastClicked =
    m_lineSelectSingleOnUp =
    m_lineBeforeLastClicked =
    m_anchor  = (size_t)-1;

    m_hasCheckBoxes = false;
    m_extendRulesAndAlternateColour = false;
}

ibListMainWindow::ibListMainWindow()
{
    Init();

    m_highlightBrush =
    m_highlightUnfocusedBrush = nullptr;
}

ibListMainWindow::ibListMainWindow( wxWindow *parent,
                                    wxWindowID id,
                                    const wxPoint& pos,
                                    const wxSize& size )
                : ibWindow( parent, id, pos, size,
                            wxWANTS_CHARS | wxBORDER_NONE )
{
    Init();

    // SEAM: the theme's selection, as ibTreeCtrl's - not the system's
    m_highlightBrush = new wxBrush(wxTHEME_COLOUR(HIGHLIGHT), wxBRUSHSTYLE_SOLID);
    m_highlightUnfocusedBrush = new wxBrush(wxTHEME_COLOUR(HIGHLIGHT).ChangeLightness(130), wxBRUSHSTYLE_SOLID);

    wxVisualAttributes attr = ibListCtrl::GetClassDefaultAttributes();
    SetOwnForegroundColour( attr.colFg );
    SetOwnBackgroundColour( attr.colBg );
    if (!m_hasFont)
        SetOwnFont( attr.font );
}

ibListMainWindow::~ibListMainWindow()
{
    if ( m_textctrlWrapper )
        m_textctrlWrapper->EndEdit(ibListTextCtrlWrapper::End_Destroy);

    DoDeleteAllItems();

    delete m_highlightBrush;
    delete m_highlightUnfocusedBrush;
    delete m_renameTimer;
    delete m_findTimer;
}

void ibListMainWindow::SetReportView(bool inReportView)
{
    for ( auto& line : m_lines )
    {
        line.SetReportView(inReportView);
    }
}

void ibListMainWindow::CacheLineData(size_t line)
{
    ibListCtrl *listctrl = GetListCtrl();

    ibListLineData *ld = GetDummyLine();

    size_t countCol = GetColumnCount();
    for ( size_t col = 0; col < countCol; col++ )
    {
        ld->SetText(col, listctrl->OnGetItemText(line, col));
        ld->SetImage(col, listctrl->OnGetItemColumnImage(line, col));
    }

    if ( HasCheckBoxes() )
    {
        ld->Check(listctrl->OnGetItemIsChecked(line));
    }

    ld->SetAttr(listctrl->OnGetItemAttr(line));
}

ibListLineData *ibListMainWindow::GetDummyLine() const
{
    wxASSERT_MSG( !IsEmpty(), wxT("invalid line index") );
    wxASSERT_MSG( IsVirtual(), wxT("GetDummyLine() shouldn't be called") );

    ibListMainWindow *self = wxConstCast(this, ibListMainWindow);

    // we need to recreate the dummy line if the number of columns in the
    // control changed as it would have the incorrect number of fields
    // otherwise
    if ( !m_lines.empty() &&
            m_lines[0].m_items.size() != (size_t)GetColumnCount() )
    {
        self->m_lines.clear();
    }

    if ( m_lines.empty() )
    {
        self->m_lines.emplace_back(self);

        // don't waste extra memory -- there never going to be anything
        // else/more in this array
        self->m_lines.shrink_to_fit();
    }

    return &self->m_lines[0];
}

// ----------------------------------------------------------------------------
// line geometry (report mode only)
// ----------------------------------------------------------------------------

wxCoord ibListMainWindow::GetLineHeight() const
{
    // we cache the line height as calling GetTextExtent() is slow
    if ( !m_lineHeight )
    {
        ibListMainWindow *self = wxConstCast(this, ibListMainWindow);

        wxInfoDC dc( self );

        wxCoord y;
        dc.GetTextExtent(wxT("H"), nullptr, &y);

        if ( m_small_images && m_small_images->GetImageCount() )
        {
            int iw = 0, ih = 0;
            m_small_images->GetImageLogicalSize(this, iw, ih);
            y = wxMax(y, ih);
        }

        y += EXTRA_HEIGHT;
        self->m_lineHeight = y + LINE_SPACING;
    }

    return m_lineHeight;
}

wxCoord ibListMainWindow::GetLineY(size_t line) const
{
    wxASSERT_MSG( InReportView(), wxT("only works in report mode") );

    return LINE_SPACING + line * GetLineHeight();
}

wxRect ibListMainWindow::GetLineRect(size_t line) const
{
    if ( !InReportView() )
        return GetLine(line)->m_gi->m_rectAll;

    wxRect rect;
    rect.x = HEADER_OFFSET_X;
    rect.y = GetLineY(line);
    rect.width = GetHeaderWidth();
    rect.height = GetLineHeight();

    return rect;
}

wxRect ibListMainWindow::GetLineLabelRect(size_t line) const
{
    if ( !InReportView() )
        return GetLine(line)->m_gi->m_rectLabel;

    int image_x = 0;
    ibListLineData *data = GetLine(line);
    if ( !data->m_items.empty() )
    {
        ibListItemData *item = &data->m_items[0];
        if ( item->HasImage() )
        {
            int ix, iy;
            GetImageSize( ix, iy );
            image_x = 3 + ix + IMAGE_MARGIN_IN_REPORT_MODE;
        }
    }

    wxRect rect;
    rect.x = image_x + HEADER_OFFSET_X;
    rect.y = GetLineY(line);
    rect.width = GetColumnWidth(0) - image_x;
    rect.height = GetLineHeight();

    return rect;
}

wxRect ibListMainWindow::GetLineIconRect(size_t line) const
{
    if ( !InReportView() )
        return GetLine(line)->m_gi->m_rectIcon;

    wxASSERT_MSG( GetLine(line)->HasImage(), wxT("should have an image") );

    wxRect rect = GetLineRect(line);
    rect.x += ICON_OFFSET_X;

    if ( HasCheckBoxes() )
    {
        // SEAM vs wx: the box the line draws - the window's renderer's (DrawInReportMode)
        wxSize cbSize = GetRenderer()->GetCheckBoxSize(const_cast<ibListMainWindow*>(this));
        rect.x += cbSize.GetWidth() + (2 * MARGIN_AROUND_CHECKBOX);
    }

    // use full height of the line, same as win32 listctrl
    int ix, iy;
    GetImageSize(ix, iy);
    rect.width = ix;

    return rect;
}

wxRect ibListMainWindow::GetLineHighlightRect(size_t line) const
{
    return InReportView() ? GetLineRect(line)
                          : GetLine(line)->m_gi->m_rectHighlight;
}

long ibListMainWindow::HitTestLine(size_t line, int x, int y) const
{
    wxASSERT_MSG( line < GetItemCount(), wxT("invalid line in HitTestLine") );

    if ( IsInsideCheckBox(line, x, y) )
        return wxLIST_HITTEST_ONITEMSTATEICON;

    ibListLineData *ld = GetLine(line);

    if ( ld->HasImage() && GetLineIconRect(line).Contains(x, y) )
        return wxLIST_HITTEST_ONITEMICON;

    // VS: Testing for "ld->HasText() || InReportView()" instead of
    //     "ld->HasText()" is needed to make empty lines in report view
    //     possible
    if ( ld->HasText() || InReportView() )
    {
        wxRect rect = InReportView() ? GetLineRect(line)
                                     : GetLineLabelRect(line);

        if ( rect.Contains(x, y) )
            return wxLIST_HITTEST_ONITEMLABEL;
    }

    return 0;
}

// ----------------------------------------------------------------------------
// highlight (selection) handling
// ----------------------------------------------------------------------------

bool ibListMainWindow::IsHighlighted(size_t line) const
{
    if ( IsVirtual() )
    {
        return m_selStore.IsSelected(line);
    }
    else // !virtual
    {
        ibListLineData *ld = GetLine(line);
        wxCHECK_MSG( ld, false, wxT("invalid index in IsHighlighted") );

        return ld->IsHighlighted();
    }
}

void ibListMainWindow::HighlightLines( size_t lineFrom,
                                       size_t lineTo,
                                       bool highlight,
                                       SendEvent sendEvent )
{
    // It is safe to swap the bounds here if they are not in order.
    if ( lineFrom > lineTo )
        wxSwap(lineFrom, lineTo);

    if ( IsVirtual() )
    {
        wxArrayInt linesChanged;
        if ( !m_selStore.SelectRange(lineFrom, lineTo, highlight,
                                     &linesChanged) )
        {
            // meny items changed state, refresh everything
            RefreshLines(lineFrom, lineTo);
        }
        else // only a few items changed state, refresh only them
        {
            size_t count = linesChanged.GetCount();
            for ( size_t n = 0; n < count; n++ )
            {
                RefreshLine(linesChanged[n]);
            }
        }
    }
    else // iterate over all items in non report view
    {
        for ( size_t line = lineFrom; line <= lineTo; line++ )
        {
            if ( HighlightLine(line, highlight, sendEvent) )
                RefreshLine(line);
        }
    }
}

bool ibListMainWindow::HighlightLine( size_t line, bool highlight, SendEvent sendEvent )
{
    bool changed;

    if ( IsVirtual() )
    {
        changed = m_selStore.SelectItem(line, highlight);
    }
    else // !virtual
    {
        ibListLineData *ld = GetLine(line);
        wxCHECK_MSG( ld, false, wxT("invalid index in HighlightLine") );

        changed = ld->Highlight(highlight);
    }

    if ( changed && sendEvent )
    {
        SendNotify( line, highlight ? wxEVT_LIST_ITEM_SELECTED
                                    : wxEVT_LIST_ITEM_DESELECTED );
    }

    return changed;
}

void ibListMainWindow::RefreshLine( size_t line )
{
    if ( InReportView() )
    {
        size_t visibleFrom, visibleTo;
        GetVisibleLinesRange(&visibleFrom, &visibleTo);

        if ( line < visibleFrom || line > visibleTo )
            return;
    }

    wxRect rect = GetLineRect(line);

    GetListCtrl()->CalcScrolledPosition( rect.x, rect.y, &rect.x, &rect.y );
    RefreshRect( rect );
}

void ibListMainWindow::RefreshLines( size_t lineFrom, size_t lineTo )
{
    // we suppose that they are ordered by caller
    wxASSERT_MSG( lineFrom <= lineTo, wxT("indices in disorder") );

    wxASSERT_MSG( lineTo < GetItemCount(), wxT("invalid line range") );

    if ( InReportView() )
    {
        size_t visibleFrom, visibleTo;
        GetVisibleLinesRange(&visibleFrom, &visibleTo);

        if ( lineFrom > visibleTo || lineTo < visibleFrom )
        {
            // None of these lines are currently visible at all, don't bother
            // doing anything.
            return;
        }

        if ( lineFrom < visibleFrom )
            lineFrom = visibleFrom;
        if ( lineTo > visibleTo )
            lineTo = visibleTo;

        wxRect rect;
        rect.x = 0;
        rect.y = GetLineY(lineFrom);
        rect.width = GetClientSize().x;
        rect.height = GetLineY(lineTo) - rect.y + GetLineHeight();

        GetListCtrl()->CalcScrolledPosition( rect.x, rect.y, &rect.x, &rect.y );
        RefreshRect( rect );
    }
    else // !report
    {
        // TODO: this should be optimized...
        for ( size_t line = lineFrom; line <= lineTo; line++ )
        {
            RefreshLine(line);
        }
    }
}

void ibListMainWindow::RefreshAfter( size_t lineFrom )
{
    if ( InReportView() )
    {
        // Note that we don't compare lineFrom with the last visible line
        // because we refresh the entire rectangle below it anyhow, so it
        // doesn't matter if it's bigger than it. And we must still refresh
        // even if lineFrom is invalid because it may have been (just) deleted
        // when we're called from DeleteItem().
        size_t visibleFrom;
        GetVisibleLinesRange(&visibleFrom, nullptr);

        if ( lineFrom < visibleFrom )
            lineFrom = visibleFrom;

        wxRect rect;
        rect.x = 0;
        rect.y = GetLineY(lineFrom);
        GetListCtrl()->CalcScrolledPosition( rect.x, rect.y, &rect.x, &rect.y );

        wxSize size = GetClientSize();
        rect.width = size.x;

        // refresh till the bottom of the window
        rect.height = size.y - rect.y;

        RefreshRect( rect );
    }
    else // !report
    {
        // TODO: how to do it more efficiently?
        m_dirty = true;
    }
}

void ibListMainWindow::RefreshSelected()
{
    if ( IsEmpty() )
        return;

    size_t from, to;
    if ( InReportView() )
    {
        GetVisibleLinesRange(&from, &to);
    }
    else // !virtual
    {
        from = 0;
        to = GetItemCount() - 1;
    }

    if ( HasCurrent() && m_current >= from && m_current <= to )
        RefreshLine(m_current);

    for ( size_t line = from; line <= to; line++ )
    {
        // NB: the test works as expected even if m_current == -1
        if ( line != m_current && IsHighlighted(line) )
            RefreshLine(line);
    }
}

void ibListMainWindow::UpdateAccessibleItems()
{
    wxPrivate::AccessibleRows rows;

    const size_t count = GetItemCount();
    if ( count )
    {
        const bool inReportView = InReportView();

        // Only the shown items are exposed: the control may have thousands of
        // them and the elements of the ones which are not shown would be
        // useless anyhow, as their rectangles would be outside of it.
        size_t visibleFrom, visibleTo;
        if ( inReportView )
        {
            GetVisibleLinesRange(&visibleFrom, &visibleTo);
        }
        else // There is no simple way to find the visible items in this case.
        {
            visibleFrom = 0;
            visibleTo = count - 1;
        }

        const wxRect rectClient(GetClientSize());
        const int numCols = inReportView ? GetColumnCount() : 1;

        for ( size_t line = visibleFrom; line <= visibleTo; ++line )
        {
            wxRect rect;
            GetItemRect(line, rect);
            if ( !inReportView && !rect.Intersects(rectClient) )
                continue;

            wxPrivate::AccessibleRow row(line, rect);
            row.selected = IsHighlighted(line);

            for ( int col = 0; col < numCols; ++col )
            {
                // Only the report view has real columns, in the other ones the
                // single value is the item label.
                wxRect rectCell;
                if ( inReportView )
                {
                    GetSubItemRect(line, col, rectCell);
                }
                else
                {
                    rectCell = GetLineLabelRect(line);
                    GetListCtrl()->CalcScrolledPosition(rectCell.x, rectCell.y,
                                                        &rectCell.x, &rectCell.y);
                }

                row.cells.emplace_back(GetItemText(line, col), rectCell);
            }

            rows.push_back(row);
        }
    }

    wxPrivate::SetAccessibleTable(this, rows, count);

    // Announce the current item if it has changed, as the screen readers have
    // no other way of knowing that the user has moved to another one.
    const long current = HasCurrent() ? (long)m_current : -1;
    if ( current != m_lastAccessibleCurrent )
    {
        m_lastAccessibleCurrent = current;

        wxPrivate::SetAccessibleCurrentRow(this, current);
    }
}

void ibListMainWindow::OnPaint( wxPaintEvent &WXUNUSED(event) )
{
    // SEAM vs wx: ibWindow sets wxBG_STYLE_PAINT (no erase pass), so this
    // OnPaint owns the background too - buffered: erased and then drawn on the
    // paint DC, the list flickered at every new selection and column width
    wxAutoBufferedPaintDC dc( this );

    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if ( IsEmpty() )
    {
        // Remove the accessible elements for the previously shown items.
        UpdateAccessibleItems();

        // nothing to draw or not the moment to draw it
        return;
    }

    if ( m_dirty )
    {
        // Calling Refresh() from inside OnPaint() doesn't work under macOS, so
        // don't do it immediately...
        RecalculatePositions();

        // ... but schedule it for later.
        CallAfter(&wxWindow::Refresh, true, (const wxRect*)nullptr);

        // Don't bother redoing the relayout again the next time nor redrawing
        // now, as we'll be refresh soon anyhow.
        m_dirty = false;
        return;
    }

    const wxPoint origin(GetListCtrl()->CalcScrolledPosition(wxPoint(0, 0)));

    if ( InReportView() )
    {
        int lineHeight = GetLineHeight();

        size_t visibleFrom, visibleTo;
        const size_t linesPerPage = (unsigned int) m_linesPerPage;
        GetVisibleLinesRange(&visibleFrom, &visibleTo);

        // We may need to iterate beyond visibleTo if we want to draw striped
        // background across the entire window.
        size_t visibleEnd;
        wxColour colAlt;
        if ( m_extendRulesAndAlternateColour )
        {
            colAlt = GetListCtrl()->GetAlternateRowColour();
            visibleEnd = wxMax(linesPerPage, visibleTo);
        }
        else
        {
            visibleEnd = visibleTo;
        }

        // tell the caller cache to cache the data
        if ( IsVirtual() )
        {
            wxListEvent evCache(wxEVT_LIST_CACHE_HINT,
                                GetParent()->GetId());
            evCache.SetEventObject( GetParent() );
            evCache.m_oldItemIndex = visibleFrom;
            evCache.m_item.m_itemId =
            evCache.m_itemIndex = visibleTo;
            GetParent()->GetEventHandler()->ProcessEvent( evCache );
        }

        wxRect rectLine(GetLineRect(visibleFrom));
        rectLine.Offset(origin);

        for (size_t line = visibleFrom;
            line <= visibleEnd;
            line++, rectLine.y += rectLine.height)
        {
            if ( !IsExposed(rectLine) )
            {
                // don't redraw unaffected lines to avoid flicker
                continue;
            }

            if ( line > visibleTo )
            {
                // We only iterate beyond visibleTo when we have to draw the
                // odd rows background, so do this if needed.
                if ( line % 2 )
                {
                    dc.SetBrush(colAlt);
                    dc.SetPen(*wxTRANSPARENT_PEN);
                    dc.DrawRectangle(rectLine);
                }

                // But don't do anything else, as there is no valid item.
                continue;
            }

            GetLine(line)->DrawInReportMode( &dc,
                                             rectLine,
                                             IsHighlighted(line),
                                             line == m_current,
                                             IsItemChecked(line) );
        }

        if ( HasFlag(wxLC_HRULES) )
        {
            wxPen pen(GetRuleColour(), 1, wxPENSTYLE_SOLID);
            wxSize clientSize = GetClientSize();

            size_t i = visibleFrom;
            if (i == 0) i = 1; // Don't draw the first one

            int y = (i - visibleFrom) * lineHeight;
            for ( ; i <= visibleEnd; i++, y += lineHeight )
            {
                dc.SetPen(pen);
                dc.SetBrush( *wxTRANSPARENT_BRUSH );
                dc.DrawLine(0, y, clientSize.x, y);
            }

            // Draw last horizontal rule
            if ( visibleEnd == GetItemCount() - 1 )
            {
                dc.SetPen( pen );
                dc.SetBrush( *wxTRANSPARENT_BRUSH );
                dc.DrawLine(0, y, clientSize.x, y);
            }
        }

        // Draw vertical rules if required
        if ( HasFlag(wxLC_VRULES) &&
                (m_extendRulesAndAlternateColour || !IsEmpty()) )
        {
            wxPen pen(GetRuleColour(), 1, wxPENSTYLE_SOLID);
            wxRect firstItemRect, lastItemRect;

            GetItemRect(visibleFrom, firstItemRect);
            GetItemRect(visibleTo, lastItemRect);
            int x = rectLine.x;
            dc.SetPen(pen);
            dc.SetBrush(* wxTRANSPARENT_BRUSH);

            int clientHeight, clientWidth;
            GetSize( &clientWidth, &clientHeight );

            for (int col = 0; col < GetColumnCount(); col++)
            {
                int colWidth = GetColumnWidth(col);
                x += colWidth;
                int x_pos = x;
                if (col < GetColumnCount()-1) x_pos -= 2;

                int ruleHeight = m_extendRulesAndAlternateColour && visibleEnd > visibleTo
                                    ? clientHeight
                                    : lastItemRect.GetBottom() + 1;

                dc.DrawLine(x_pos, firstItemRect.GetY() - 1, x_pos, ruleHeight);
            }
        }
    }
    else // !report
    {
        size_t count = GetItemCount();
        for ( size_t i = 0; i < count; i++ )
        {
            GetLine(i)->Draw( &dc, origin, i == m_current );
        }
    }

    // DrawFocusRect() is unusable under Mac, it draws outside of the highlight
    // rectangle somehow and so leaves traces when the item is not selected any
    // more, see #12229.
#ifndef __WXMAC__
    if ( HasCurrent() )
    {
        int flags = 0;
        if ( IsHighlighted(m_current) )
            flags |= wxCONTROL_SELECTED;

        wxRect rect(GetLineHighlightRect(m_current));
        rect.Offset(origin);

        ibThemeEngine::Get()->GetRenderer()->DrawFocusRect(this, dc, rect, flags);
    }
#endif // !__WXMAC__

    // The items we've just drawn may be different from the previous ones, e.g.
    // because the control was scrolled, so refresh the elements using them.
    UpdateAccessibleItems();
}

void ibListMainWindow::OnSysColourChanged( wxSysColourChangedEvent &event )
{
    // SEAM: the theme's colours (GetClassDefaultAttributes), as at the creation
    const wxVisualAttributes attr = ibListCtrl::GetClassDefaultAttributes();
    SetOwnForegroundColour(attr.colFg);
    SetOwnBackgroundColour(attr.colBg);

    if ( m_highlightBrush )
    {
        m_highlightBrush->SetColour(wxTHEME_COLOUR(HIGHLIGHT));
    }

    if ( m_highlightUnfocusedBrush )
    {
        m_highlightUnfocusedBrush->SetColour(wxTHEME_COLOUR(HIGHLIGHT).ChangeLightness(130));
    }

    Refresh();

    event.Skip();
}

void ibListMainWindow::HighlightAll( bool on )
{
    if ( IsSingleSel() )
    {
        wxASSERT_MSG( !on, wxT("can't do this in a single selection control") );

        // we just have one item to turn off
        if ( HasCurrent() && IsHighlighted(m_current) )
        {
            HighlightLine(m_current, false);
            RefreshLine(m_current);
        }
    }
    else // multi selection
    {
        if ( !IsEmpty() )
            HighlightLines(0, GetItemCount() - 1, on);
    }
}

void ibListMainWindow::HighlightOnly( size_t line, size_t oldLine )
{
    const unsigned selCount = GetSelectedItemCount();

    if ( selCount == 1 && IsHighlighted(line) )
    {
        return; // Nothing changed.
    }

    if ( oldLine != (size_t)-1 )
    {
        IsHighlighted(oldLine) ? ReverseHighlight(oldLine)
                               : RefreshLine(oldLine); // refresh the old focus to remove it
    }

    // Deselect the remaining items we may have when using multiple selection
    // (for single selection the only selected item is always the highlighted
    // one, so we would have already returned above).
    if ( selCount != 0 )
    {
        // Deselecting many items at once will generate wxEVT_XXX_DESELECTED event
        // for each one of them. although this may be inefficient if the number of
        // deselected items is too much, we keep doing this (for non-virtual list
        // controls) for backward compatibility concerns. For virtual listctrl (in
        // multi-selection mode), wxMSW sends only a notification to indicate that
        // something has been deselected. Notice that to be fully compatible with
        // wxMSW behaviour, _line_ shouldn't be deselected if it was selected.

        const SendEvent sendEvent = IsVirtual() ? SendEvent_None : SendEvent_Normal;

        size_t lineFrom = 0,
               lineTo   = GetItemCount() - 1;

        if ( line > lineFrom && line < lineTo )
        {
            HighlightLines(lineFrom, line - 1, false, sendEvent);
            HighlightLines(line + 1, lineTo, false, sendEvent);
        }
        else // _line_ is equal to lineFrom or lineTo
        {
            line == lineFrom ? ++lineFrom : --lineTo;
            HighlightLines(lineFrom, lineTo, false, sendEvent);
        }

        // If we didn't send the event for individual items above, send it for all of them now.
        if ( sendEvent == SendEvent_None )
            SendNotify((size_t)-1, wxEVT_LIST_ITEM_DESELECTED);
    }

    // _line_ should be the only selected item.
    HighlightLine(line);
    // refresh the new focus to add it.
    RefreshLine(line);
}

void ibListMainWindow::OnChildFocus(wxChildFocusEvent& WXUNUSED(event))
{
    // Do nothing here.  This prevents the default handler in wxScrolledWindow
    // from needlessly scrolling the window when the edit control is
    // dismissed.  See ticket #9563.
}

void ibListMainWindow::SendNotify( size_t line,
                                   wxEventType command,
                                   const wxPoint& point )
{
    ibListCtrl* const listctrl = GetListCtrl();

    wxListEvent le( command, listctrl->GetId() );
    le.SetEventObject( listctrl );

    le.m_item.m_itemId =
    le.m_itemIndex = line;

    // set only for events which have position
    if ( point != wxDefaultPosition )
    {
        // the position should be relative to the parent window, not
        // this one for compatibility with MSW and common sense: the
        // user code doesn't know anything at all about this window,
        // so why should it get positions relative to it?
        le.m_pointDrag = GetPosition() + point;
    }

    // provide information about the (first column of the) item in the event if
    // we have a valid item and any columns at all
    if ( line != (size_t)-1 && listctrl->GetColumnCount() )
    {
        GetLine(line)->GetItem( 0, le.m_item );
    }

    listctrl->GetEventHandler()->ProcessEvent( le );
}

bool ibListMainWindow::ChangeCurrentWithoutEvent(size_t current)
{
    if ( current == m_current )
    {
        return false; // Nothing changed!
    }

    m_current = current;

    // as the current item changed, we shouldn't start editing it when the
    // "slow click" timer expires as the click happened on another item
    if ( m_renameTimer->IsRunning() )
        m_renameTimer->Stop();

    return true;
}

void ibListMainWindow::ChangeCurrent(size_t current)
{
    if ( ChangeCurrentWithoutEvent(current) )
        SendNotify(current, wxEVT_LIST_ITEM_FOCUSED);
}

ibTextCtrl *ibListMainWindow::EditLabel(long item, wxClassInfo* textControlClass)
{
    // Calling this function for control without wxLC_EDIT_LABELS flag set
    // is not portable. i.e. on wxMSW this function cannot edit labels if
    // the flag is not already set on the control.
    wxASSERT_MSG( HasFlag(wxLC_EDIT_LABELS),
                 "should only be called if wxLC_EDIT_LABELS flag is set");

    wxCHECK_MSG( (item >= 0) && ((size_t)item < GetItemCount()), nullptr,
                 wxT("wrong index in ibListCtrl::EditLabel()") );

    wxASSERT_MSG( textControlClass->IsKindOf(wxCLASSINFO(ibTextCtrl)),
                 wxT("EditLabel() needs a text control") );

    size_t itemEdit = (size_t)item;

    wxListEvent le( wxEVT_LIST_BEGIN_LABEL_EDIT, GetParent()->GetId() );
    le.SetEventObject( GetParent() );
    le.m_item.m_itemId =
    le.m_itemIndex = item;
    ibListLineData *data = GetLine(itemEdit);
    wxCHECK_MSG( data, nullptr, wxT("invalid index in EditLabel()") );
    data->GetItem( 0, le.m_item );

    // See comment in EditLabel() (src/msw/listctrl.cpp) for why we create the
    // text control before sending the wxEVT_LIST_BEGIN_LABEL_EDIT event.
    ibTextCtrl * const text = (ibTextCtrl *)textControlClass->CreateObject();
    m_textctrlWrapper = new ibListTextCtrlWrapper(this, text, item);

    if ( GetParent()->GetEventHandler()->ProcessEvent( le ) && !le.IsAllowed() )
    {
        m_textctrlWrapper->EndEdit(ibListTextCtrlWrapper::End_Destroy);
        // vetoed by user code
        return nullptr;
    }

    if ( m_dirty )
    {
        // Ensure the display is updated before we start editing.
        Update();
    }

    return m_textctrlWrapper->GetText();
}

bool ibListMainWindow::EndEditLabel(bool cancel)
{
    if (!m_textctrlWrapper)
    {
        return false;
    }

    m_textctrlWrapper->EndEdit(cancel ? ibListTextCtrlWrapper::End_Discard : ibListTextCtrlWrapper::End_Accept);
    return true;
}

void ibListMainWindow::OnRenameTimer()
{
    wxCHECK_RET( HasCurrent(), wxT("unexpected rename timer") );

    EditLabel( m_current );
}

bool ibListMainWindow::OnRenameAccept(size_t itemEdit, const wxString& value)
{
    wxListEvent le( wxEVT_LIST_END_LABEL_EDIT, GetParent()->GetId() );
    le.SetEventObject( GetParent() );
    le.m_item.m_itemId =
    le.m_itemIndex = itemEdit;

    ibListLineData *data = GetLine(itemEdit);

    wxCHECK_MSG( data, false, wxT("invalid index in OnRenameAccept()") );

    data->GetItem( 0, le.m_item );
    le.m_item.m_text = value;

    GetParent()->GetEventHandler()->ProcessEvent( le );

    if ( !le.IsAllowed() )
        return false;

    m_dirty = true;

    return true;
}

void ibListMainWindow::OnRenameCancelled(size_t itemEdit)
{
    // let owner know that the edit was cancelled
    wxListEvent le( wxEVT_LIST_END_LABEL_EDIT, GetParent()->GetId() );

    le.SetEditCanceled(true);

    le.SetEventObject( GetParent() );
    le.m_item.m_itemId =
    le.m_itemIndex = itemEdit;

    ibListLineData *data = GetLine(itemEdit);
    wxCHECK_RET( data, wxT("invalid index in OnRenameCancelled()") );

    data->GetItem( 0, le.m_item );
    GetEventHandler()->ProcessEvent( le );
}

void ibListMainWindow::OnFindTimer()
{
    m_findPrefix.clear();
    if ( m_findBell == -1 )
        m_findBell = 1;
}

void ibListMainWindow::EnableBellOnNoMatch( bool on )
{
    m_findBell = on;
}

void ibListMainWindow::OnMouse( wxMouseEvent &event )
{
#ifdef __WXMAC__
    // On wxMac we can't depend on the EVT_KILL_FOCUS event to properly
    // shutdown the edit control when the mouse is clicked elsewhere on the
    // listctrl because the order of events is different (or something like
    // that), so explicitly end the edit if it is active.
    if ( event.LeftDown() && m_textctrlWrapper )
        m_textctrlWrapper->EndEdit(ibListTextCtrlWrapper::End_Accept);
#endif // __WXMAC__

    if ( event.LeftDown() )
    {
        // Ensure we skip the event to let the system set focus to this window.
        event.Skip();
    }

    // Pretend that the event happened in wxListCtrl itself.
    wxMouseEvent me(event);
    me.SetEventObject( GetParent() );
    me.SetId(GetParent()->GetId());
    if ( GetParent()->GetEventHandler()->ProcessEvent( me ))
        return;

    if (event.GetEventType() == wxEVT_MOUSEWHEEL)
    {
        // let the base class handle mouse wheel events.
        event.Skip();
        return;
    }

    if ( !HasCurrent() || IsEmpty() )
    {
        if (event.RightDown())
        {
            SendNotify( (size_t)-1, wxEVT_LIST_ITEM_RIGHT_CLICK, event.GetPosition() );

            wxContextMenuEvent evtCtx(wxEVT_CONTEXT_MENU,
                                      GetParent()->GetId(),
                                      ClientToScreen(event.GetPosition()));
            evtCtx.SetEventObject(GetParent());
            GetParent()->GetEventHandler()->ProcessEvent(evtCtx);
        }

        if ( IsEmpty() )
            return;

        // Continue processing...
    }

    if (m_dirty)
        return;

    if ( !(event.Dragging() || event.ButtonDown() || event.LeftUp() ||
        event.ButtonDClick()) )
        return;

    int x = event.GetX();
    int y = event.GetY();
    GetListCtrl()->CalcUnscrolledPosition( x, y, &x, &y );

    // where did we hit it (if we did)?
    long hitResult = 0;

    size_t count = GetItemCount(),
           current;

    if ( InReportView() )
    {
        current = y / GetLineHeight();
        if ( current < count )
            hitResult = HitTestLine(current, x, y);
    }
    else // !report
    {
        // TODO: optimize it too! this is less simple than for report view but
        //       enumerating all items is still not a way to do it!!
        for ( current = 0; current < count; current++ )
        {
            hitResult = HitTestLine(current, x, y);
            if ( hitResult )
                break;
        }
    }

    // Update drag events counter first as we must do it even if the mouse is
    // not on any item right now as we must keep count in case we started
    // dragging from the empty control area but continued to do it over a valid
    // item -- in this situation we must not start dragging this item.
    if (event.Dragging())
        m_dragCount++;
    else
        m_dragCount = 0;

    // The only mouse events that can be generated without any valid item are
    // wxEVT_LIST_ITEM_DESELECTED for virtual lists, and
    // wxEVT_LIST_ITEM_RIGHT_CLICK as it can be useful to have a global
    // popup menu for the list control itself which should be shown even when
    // the user clicks outside of any item.
    if ( !hitResult )
    {
        // outside of any item
        if (event.RightDown())
        {
            wxContextMenuEvent evtCtx(
                wxEVT_CONTEXT_MENU,
                GetParent()->GetId(),
                ClientToScreen(event.GetPosition()));
            evtCtx.SetEventObject(GetParent());
            GetParent()->GetEventHandler()->ProcessEvent(evtCtx);
        }
        else if (event.LeftDown())
        {
            // reset the selection in multi-selection mode only when Ctrl and
            // Shift keys are not pressed to mimic MSW behaviour
            if ( IsSingleSel() || !(event.ControlDown() || event.ShiftDown()) )
            {
                // reset the selection and bail out
                HighlightAll(false);
                // generate a DESELECTED event for
                // virtual multi-selection lists
                if ( IsVirtual() && !IsSingleSel() )
                    SendNotify( m_lineLastClicked, wxEVT_LIST_ITEM_DESELECTED );
            }
        }

        return;
    }

    if ( event.Dragging() )
    {
        if (m_dragCount == 1)
        {
            // we have to report the raw, physical coords as we want to be
            // able to call HitTest(event.m_pointDrag) from the user code to
            // get the item being dragged
            m_dragStart = event.GetPosition();
        }

        if (m_dragCount != 3)
            return;

        int command = event.RightIsDown() ? wxEVT_LIST_BEGIN_RDRAG
                                          : wxEVT_LIST_BEGIN_DRAG;

        SendNotify( m_lineLastClicked, command, m_dragStart );

        return;
    }

    bool forceClick = false;
    if (event.ButtonDClick())
    {
        if ( m_renameTimer->IsRunning() )
            m_renameTimer->Stop();

        m_lastOnSame = false;

        if ( current == m_lineLastClicked )
        {
            SendNotify( current, wxEVT_LIST_ITEM_ACTIVATED );

            return;
        }
        else
        {
            // The first click was on another item, so don't interpret this as
            // a double click, but as a simple click instead
            forceClick = true;
        }
    }

    if (event.LeftUp())
    {
        if (m_lineSelectSingleOnUp != (size_t)-1)
        {
            // select single line
            HighlightOnly(m_lineSelectSingleOnUp);
        }

        if (m_lastOnSame)
        {
            if ((current == m_current) &&
                (hitResult == wxLIST_HITTEST_ONITEMLABEL) &&
                HasFlag(wxLC_EDIT_LABELS) )
            {
                if ( !InReportView() ||
                        GetLineLabelRect(current).Contains(x, y) )
                {
                    int dclick = wxSystemSettings::GetMetric(wxSYS_DCLICK_MSEC);
                    m_renameTimer->Start(dclick > 0 ? dclick : 250, true);
                }
            }

            m_lastOnSame = false;
        }

        if ( GetSelectedItemCount() == 1 || event.CmdDown() )
        {
            // In multiple selection mode, the anchor is set to the first selected
            // item or can be changed to m_current if Ctrl key is down, as is the
            // case under wxMSW.
            m_anchor = m_current;
        }

        m_lineSelectSingleOnUp = (size_t)-1;
    }
    else
    {
        // This is necessary, because after a DnD operation in
        // from and to ourself, the up event is swallowed by the
        // DnD code. So on next non-up event (which means here and
        // now) m_lineSelectSingleOnUp should be reset.
        m_lineSelectSingleOnUp = (size_t)-1;
    }
    if (event.RightDown())
    {
        m_lineBeforeLastClicked = m_lineLastClicked;
        m_lineLastClicked = current;

        // If the item is already selected, do not update the selection.
        // Multi-selections should not be cleared if a selected item is clicked.
        if (!IsHighlighted(current))
        {
            size_t oldCurrent = m_current;
            ChangeCurrent(current);
            HighlightOnly(m_current, oldCurrent);
        }

        SendNotify( current, wxEVT_LIST_ITEM_RIGHT_CLICK, event.GetPosition() );

        // Allow generation of context menu event
        event.Skip();
    }
    else if (event.MiddleDown())
    {
        SendNotify( current, wxEVT_LIST_ITEM_MIDDLE_CLICK );
    }
    else if ( event.LeftDown() || forceClick )
    {
        m_lineBeforeLastClicked = m_lineLastClicked;
        m_lineLastClicked = current;

        size_t oldCurrent = m_current;
        bool oldWasSelected = HasCurrent() && IsHighlighted(m_current);

        bool cmdModifierDown = event.CmdDown();
        if ( IsSingleSel() || !(cmdModifierDown || event.ShiftDown()) )
        {
            if (IsInsideCheckBox(current, x, y))
            {
                CheckItem(current, !IsItemChecked(current));
            }
            else if (IsSingleSel() || !IsHighlighted(current))
            {
                ChangeCurrent(current);
                HighlightOnly(m_current, oldWasSelected ? oldCurrent : (size_t)-1);
            }
            else // multi sel & current is highlighted & no mod keys
            {
                m_lineSelectSingleOnUp = current;
                ChangeCurrent(current); // change focus
            }
        }
        else // multi sel & either ctrl or shift is down
        {
            if (cmdModifierDown)
            {
                ChangeCurrent(current);

                ReverseHighlight(m_current);
            }
            else if (event.ShiftDown())
            {
                ChangeCurrent(current);

                if ( oldCurrent == (size_t)-1 )
                {
                    // Highlight m_current only if there is no previous selection.
                    HighlightLine(m_current);
                }
                else if ( oldCurrent != current && m_anchor != (size_t)-1 )
                {
                    ExtendSelection(oldCurrent, current);
                }
            }
            else // !ctrl, !shift
            {
                // test in the enclosing if should make it impossible
                wxFAIL_MSG( wxT("how did we get here?") );
            }
        }

        if (m_current != oldCurrent && oldCurrent != (size_t)-1)
            RefreshLine( oldCurrent );

        // Set the flag telling us whether the next click on this item should
        // start editing its label. This should happen if we clicked on the
        // current item and it was already selected, i.e. if this click was not
        // done to select it.
        //
        // It should not happen if this was a double click (forceClick is true)
        // nor if we hadn't had the focus before as then this click was used to
        // give focus to the control.
        m_lastOnSame = (m_current == oldCurrent) && oldWasSelected &&
                            !forceClick && HasFocus();
    }
}

void ibListMainWindow::MoveToItem(size_t item)
{
    if ( item == (size_t)-1 )
        return;

    wxRect rect = GetLineRect(item);

    int client_w, client_h;
    GetClientSize( &client_w, &client_h );

    const int hLine = GetLineHeight();

    int view_x = SCROLL_UNIT_X * GetListCtrl()->GetScrollPos( wxHORIZONTAL );
    int view_y = hLine * GetListCtrl()->GetScrollPos( wxVERTICAL );

    if ( InReportView() )
    {
        // the next we need the range of lines shown it might be different,
        // so recalculate it
        ResetVisibleLinesRange();

        if (rect.y < view_y)
            GetListCtrl()->Scroll( -1, rect.y / hLine );
        if (rect.y + rect.height + 5 > view_y + client_h)
            GetListCtrl()->Scroll( -1, (rect.y + rect.height - client_h + hLine) / hLine );

#if defined(__WXMAC__) || defined(__WXUNIVERSAL__)
        // At least on Mac the visible lines value will get reset inside of
        // Scroll *before* it actually scrolls the window because of the
        // Update() that happens there, so it will still have the wrong value.
        // So let's reset it again and wait for it to be recalculated in the
        // next paint event.  I would expect this problem to show up in wxGTK
        // too but couldn't duplicate it there.  Perhaps the order of events
        // is different...  --Robin
        // Same in wxUniv/X11
        ResetVisibleLinesRange();
#endif
    }
    else // !report
    {
        int sx = -1,
            sy = -1;

        if (rect.x-view_x < 5)
            sx = (rect.x - 5) / SCROLL_UNIT_X;
        if (rect.x + rect.width - 5 > view_x + client_w)
            sx = (rect.x + rect.width - client_w + SCROLL_UNIT_X) / SCROLL_UNIT_X;

        if (rect.y-view_y < 5)
            sy = (rect.y - 5) / hLine;
        if (rect.y + rect.height - 5 > view_y + client_h)
            sy = (rect.y + rect.height - client_h + hLine) / hLine;

        GetListCtrl()->Scroll(sx, sy);
    }
}

bool ibListMainWindow::ScrollList(int WXUNUSED(dx), int dy)
{
    if ( !InReportView() )
    {
        // TODO: this should work in all views but is not implemented now
        return false;
    }

    size_t top, bottom;
    GetVisibleLinesRange(&top, &bottom);

    if ( bottom == (size_t)-1 )
        return 0;

    ResetVisibleLinesRange();

    int hLine = GetLineHeight();

    GetListCtrl()->Scroll(-1, top + dy / hLine);

#if defined(__WXMAC__) || defined(__WXUNIVERSAL__)
    // see comment in MoveToItem() for why we do this
    ResetVisibleLinesRange();
#endif

    return true;
}

// ----------------------------------------------------------------------------
// keyboard handling
// ----------------------------------------------------------------------------

// Helper function which handles items selection correctly and efficiently. and
// simply, mimic the wxMSW behaviour. And The benefit of this function is that it
// ensures that wxEVT_LIST_ITEM_{DE}SELECTED events are only generated for items
// freshly (de)selected (i.e. items that have really changed state). Useful for
// multi-selection mode when selecting using mouse or arrows with Shift key down.
void ibListMainWindow::ExtendSelection(size_t oldCurrent, size_t newCurrent)
{
    // Refresh the old/new focus to remove/add it
    RefreshLine(oldCurrent);
    RefreshLine(newCurrent);

    // Given a selection [anchor, old], to change/extend it to new (i.e. the
    // selection becomes [anchor, new]) we discriminate three possible cases:
    //
    // Case 1) new < old <= anchor || anchor <= old < new
    // i.e. oldCurrent between anchor and newCurrent, in which case we:
    // - Highlight everything between anchor and newCurrent (inclusive).
    //
    // Case 2) old < new <= anchor || anchor <= new < old
    // i.e. newCurrent between anchor and oldCurrent, in which case we:
    // - Unhighlight everything between oldCurrent and newCurrent (exclusive).
    //
    // Case 3) old < anchor < new || new < anchor < old
    // i.e. anchor between oldCurrent and newCurrent, in which case we
    // - Highlight everything between anchor and newCurrent (inclusive).
    // - Unhighlight everything between anchor (exclusive) and oldCurrent.

    size_t lineFrom, lineTo;

    if ( (newCurrent < oldCurrent && oldCurrent <= m_anchor) ||
         (newCurrent > oldCurrent && oldCurrent >= m_anchor) )
    {
        lineFrom = m_anchor;
        lineTo   = newCurrent;

        HighlightLines(lineFrom, lineTo);
    }
    else if ( (oldCurrent < newCurrent && newCurrent <= m_anchor) ||
              (oldCurrent > newCurrent && newCurrent >= m_anchor) )
    {
        lineFrom = oldCurrent;
        lineTo   = newCurrent;

        // Exclude newCurrent from being deselected
        (lineTo < lineFrom) ? ++lineTo : --lineTo;

        HighlightLines(lineFrom, lineTo, false);

        // For virtual listctrl (in multi-selection mode), wxMSW sends only
        // a notification to indicate that something has been deselected.
        if ( IsVirtual() )
            SendNotify((size_t)-1, wxEVT_LIST_ITEM_DESELECTED);
    }
    else if ( (oldCurrent < m_anchor && m_anchor < newCurrent) ||
              (newCurrent < m_anchor && m_anchor < oldCurrent) )
    {
        lineFrom = m_anchor;
        lineTo   = oldCurrent;

        // Exclude anchor from being deselected
        (lineTo < lineFrom) ? --lineFrom : ++lineFrom;

        HighlightLines(lineFrom, lineTo, false);

        // See above.
        if ( IsVirtual() )
            SendNotify((size_t)-1, wxEVT_LIST_ITEM_DESELECTED);

        lineFrom = m_anchor;
        lineTo   = newCurrent;

        HighlightLines(lineFrom, lineTo);
    }
}

void ibListMainWindow::OnArrowChar(size_t newCurrent, const wxKeyEvent& event)
{
    wxCHECK_RET( newCurrent < (size_t)GetItemCount(),
                 wxT("invalid item index in OnArrowChar()") );

    size_t oldCurrent = m_current;

    ChangeCurrent(newCurrent);

    // in single selection we just ignore Shift as we can't select several
    // items anyhow
    if ( event.ShiftDown() && !IsSingleSel() )
    {
        ExtendSelection(oldCurrent, newCurrent);
    }
    else // !shift
    {
        // all previously selected items are unselected unless ctrl is held in
        // a multi-selection control. in single selection mode we must always
        // have a selected item. note that we use the real Ctrl key here and
        // not Cmd under macOS, where Cmd-arrows have a different meaning.
        if ( !event.RawControlDown() || IsSingleSel() )
        {
            HighlightOnly(m_current, oldCurrent);

            // Update anchor
            m_anchor = m_current;
        }
        else
        {
            // refresh the old/new focus to remove/add it
            RefreshLine(oldCurrent);
            RefreshLine(m_current);
        }
    }

    MoveToFocus();
}

void ibListMainWindow::OnKeyDown( wxKeyEvent &event )
{
    wxWindow *parent = GetParent();

    // propagate the key event upwards
    wxKeyEvent ke(event);
    ke.SetEventObject( parent );
    ke.SetId(GetParent()->GetId());
    if (parent->GetEventHandler()->ProcessEvent( ke ))
        return;

    // send a list event
    wxListEvent le( wxEVT_LIST_KEY_DOWN, parent->GetId() );
    const size_t current = ShouldSendEventForCurrent() ? m_current : (size_t)-1;
    le.m_item.m_itemId =
    le.m_itemIndex = current;
    if ( current != (size_t)-1 )
        GetLine(current)->GetItem( 0, le.m_item );
    le.m_code = event.GetKeyCode();
    le.SetEventObject( parent );
    if (parent->GetEventHandler()->ProcessEvent( le ))
        return;

    event.Skip();
}

void ibListMainWindow::OnKeyUp( wxKeyEvent &event )
{
    wxWindow *parent = GetParent();

    // propagate the key event upwards
    wxKeyEvent ke(event);
    ke.SetEventObject( parent );
    ke.SetId(GetParent()->GetId());
    if (parent->GetEventHandler()->ProcessEvent( ke ))
        return;

    event.Skip();
}

void ibListMainWindow::OnCharHook( wxKeyEvent &event )
{
    if ( m_textctrlWrapper )
    {
        // When an in-place editor is active we should ensure that it always
        // gets the key events that are special to it.
        if ( m_textctrlWrapper->CheckForEndEditKey(event) )
        {
            // Skip the call to wxEvent::Skip() below.
            return;
        }
    }

    event.Skip();
}

void ibListMainWindow::OnChar( wxKeyEvent &event )
{
    wxWindow *parent = GetParent();

    // propagate the char event upwards
    wxKeyEvent ke(event);
    ke.SetEventObject( parent );
    ke.SetId(GetParent()->GetId());
    if (parent->GetEventHandler()->ProcessEvent( ke ))
        return;

    if ( HandleAsNavigationKey(event) )
        return;

    // no item -> nothing to do
    if (!HasCurrent())
    {
        event.Skip();
        return;
    }

    // don't use m_linesPerPage directly as it might not be computed yet
    const int pageSize = GetCountPerPage();
    wxCHECK_RET( pageSize, wxT("should have non zero page size") );

    if (GetLayoutDirection() == wxLayout_RightToLeft)
    {
        if (event.GetKeyCode() == WXK_RIGHT)
            event.m_keyCode = WXK_LEFT;
        else if (event.GetKeyCode() == WXK_LEFT)
            event.m_keyCode = WXK_RIGHT;
    }

    int keyCode = event.GetKeyCode();

#ifdef __WXOSX__
    // Cmd-Up and Cmd-Down are the standard shortcuts for going to the first and
    // the last item under macOS, where the keyboards often don't have Home and
    // End keys at all, so handle them in the same way as those keys.
    if ( event.CmdDown() && (keyCode == WXK_UP || keyCode == WXK_DOWN) )
    {
        if ( !IsEmpty() )
            OnArrowChar(keyCode == WXK_UP ? 0 : GetItemCount() - 1, event);

        return;
    }
#endif // __WXOSX__

    switch ( keyCode )
    {
        case WXK_UP:
            if ( m_current > 0 )
                OnArrowChar( m_current - 1, event );
            break;

        case WXK_DOWN:
            if ( m_current < (size_t)GetItemCount() - 1 )
                OnArrowChar( m_current + 1, event );
            break;

        case WXK_END:
            if (!IsEmpty())
                OnArrowChar( GetItemCount() - 1, event );
            break;

        case WXK_HOME:
            if (!IsEmpty())
                OnArrowChar( 0, event );
            break;

        case WXK_PAGEUP:
            {
                int steps = InReportView() ? pageSize - 1
                                           : m_current % pageSize;

                int index = m_current - steps;
                if (index < 0)
                    index = 0;

                OnArrowChar( index, event );
            }
            break;

        case WXK_PAGEDOWN:
            {
                int steps = InReportView()
                                ? pageSize - 1
                                : pageSize - (m_current % pageSize) - 1;

                size_t index = m_current + steps;
                size_t count = GetItemCount();
                if ( index >= count )
                    index = count - 1;

                OnArrowChar( index, event );
            }
            break;

        case WXK_LEFT:
            if ( !InReportView() )
            {
                int index = m_current - pageSize;
                if (index < 0)
                    index = 0;

                OnArrowChar( index, event );
            }
            break;

        case WXK_RIGHT:
            if ( !InReportView() )
            {
                size_t index = m_current + pageSize;

                size_t count = GetItemCount();
                if ( index >= count )
                    index = count - 1;

                OnArrowChar( index, event );
            }
            break;

        case WXK_SPACE:
            if ( IsSingleSel() )
            {
                if ( event.ControlDown() )
                {
                    ReverseHighlight(m_current);
                }
                else if ( ShouldSendEventForCurrent() ) // normal space press
                {
                    SendNotify( m_current, wxEVT_LIST_ITEM_ACTIVATED );
                }
            }
            else // multiple selection
            {
                ReverseHighlight(m_current);
            }
            break;

        case WXK_RETURN:
        case WXK_EXECUTE:
            if ( event.HasModifiers() || !ShouldSendEventForCurrent() )
            {
                event.Skip();
                break;
            }

            SendNotify( m_current, wxEVT_LIST_ITEM_ACTIVATED );
            break;

        default:
            if ( !event.HasModifiers() &&
                 ((keyCode >= '0' && keyCode <= '9') ||
                  (keyCode >= 'a' && keyCode <= 'z') ||
                  (keyCode >= 'A' && keyCode <= 'Z') ||
                  (keyCode == '_') ||
                  (keyCode == '+') ||
                  (keyCode == '*') ||
                  (keyCode == '-')))
            {
                // find the next item starting with the given prefix
                wxChar ch = (wxChar)keyCode;
                size_t item;

                // if the same character is typed multiple times then go to the
                // next entry starting with that character instead of searching
                // for an item starting with multiple copies of this character,
                // this is more useful and is how it works under Windows.
                if ( m_findPrefix.length() == 1 && m_findPrefix[0] == ch )
                {
                    item = PrefixFindItem(m_current, ch);
                }
                else
                {
                    const wxString newPrefix(m_findPrefix + ch);
                    item = PrefixFindItem(m_current, newPrefix);
                    if ( item != (size_t)-1 )
                        m_findPrefix = newPrefix;
                }

                // also start the timer to reset the current prefix if the user
                // doesn't press any more alnum keys soon -- we wouldn't want
                // to use this prefix for a new item search
                if ( !m_findTimer )
                {
                    m_findTimer = new ibListFindTimer( this );
                }

                // Notice that we should start the timer even if we didn't find
                // anything to make sure we reset the search state later.
                m_findTimer->StartOnce(ibListFindTimer::DELAY);

                // restart timer even when there's no match so bell get's reset
                if ( item != (size_t)-1 )
                {
                    // Select the found item and go to it.
                    HighlightAll(false);
                    SetItemState(item,
                                 wxLIST_STATE_FOCUSED | wxLIST_STATE_SELECTED,
                                 wxLIST_STATE_FOCUSED | wxLIST_STATE_SELECTED);
                    EnsureVisible(item);

                    // Reset the bell flag if it had been temporarily disabled
                    // before.
                    if ( m_findBell == -1 )
                        m_findBell = 1;
                }
                else // No such item
                {
                    // Signal it with a bell if enabled.
                    if ( m_findBell == 1 )
                    {
                        ::wxBell();

                        // Disable it for the next unsuccessful match, we only
                        // beep once, this is usually enough and continuing to
                        // do it would be annoying.
                        m_findBell = -1;
                    }
                }
            }
            else
            {
                event.Skip();
            }
    }
}

// ----------------------------------------------------------------------------
// focus handling
// ----------------------------------------------------------------------------

void ibListMainWindow::OnSetFocus( wxFocusEvent &WXUNUSED(event) )
{
    if ( GetParent() )
    {
        wxFocusEvent event( wxEVT_SET_FOCUS, GetParent()->GetId() );
        event.SetEventObject( GetParent() );
        if ( GetParent()->GetEventHandler()->ProcessEvent( event) )
            return;
    }

    // wxGTK sends us EVT_SET_FOCUS events even if we had never got
    // EVT_KILL_FOCUS before which means that we finish by redrawing the items
    // which are already drawn correctly resulting in horrible flicker - avoid
    // it
    if ( !m_hasFocus )
    {
        m_hasFocus = true;

        UpdateCurrent();
        RefreshSelected();
    }
}

void ibListMainWindow::OnKillFocus( wxFocusEvent &WXUNUSED(event) )
{
    if ( GetParent() )
    {
        wxFocusEvent event( wxEVT_KILL_FOCUS, GetParent()->GetId() );
        event.SetEventObject( GetParent() );
        if ( GetParent()->GetEventHandler()->ProcessEvent( event) )
            return;
    }

    m_hasFocus = false;
    RefreshSelected();
}

void ibListMainWindow::DrawImage( int index, wxDC *dc, int x, int y )
{
    if ( HasFlag(wxLC_ICON) && m_normal_images )
    {
        wxDrawImageBitmap(this, *m_normal_images, index, *dc, x, y);
    }
    else if ( HasFlag(wxLC_SMALL_ICON | wxLC_LIST | wxLC_REPORT) && m_small_images )
    {
        wxDrawImageBitmap(this, *m_small_images, index, *dc, x, y);
    }
}

void ibListMainWindow::GetImageSize( int &width, int &height ) const
{
    if ( HasFlag(wxLC_ICON) && m_normal_images )
    {
        m_normal_images->GetImageLogicalSize(this, width, height);
    }
    else if ( HasFlag(wxLC_SMALL_ICON | wxLC_LIST | wxLC_REPORT) && m_small_images )
    {
        m_small_images->GetImageLogicalSize(this, width, height);
    }
    else
    {
        width =
        height = 0;
    }
}


void ibListMainWindow::SetImages( wxWithImages *images, const int which )
{
    m_dirty = true;

    // calc the spacing from the icon size
    int width = 0;

    if ((images) && (images->HasImages()) )
    {
        int height;
        images->GetImageLogicalSize(this, width, height);
    }

    if (which == wxIMAGE_LIST_NORMAL)
    {
        m_normal_images = images;
        m_normal_spacing = width + 8;
    }

    if (which == wxIMAGE_LIST_SMALL)
    {
        m_small_images = images;
        m_small_spacing = width + 14;
        m_lineHeight = 0;  // ensure that the line height will be recalc'd
    }
}

void ibListMainWindow::SetItemSpacing( int spacing, bool isSmall )
{
    m_dirty = true;
    if (isSmall)
        m_small_spacing = spacing;
    else
        m_normal_spacing = spacing;
}

int ibListMainWindow::GetItemSpacing( bool isSmall )
{
    return isSmall ? m_small_spacing : m_normal_spacing;
}

// ----------------------------------------------------------------------------
// columns
// ----------------------------------------------------------------------------

int
ibListMainWindow::ComputeMinHeaderWidth(const ibListHeaderData* column) const
{
    wxInfoDC dc(const_cast<ibListMainWindow*>(this));

    int width = dc.GetTextExtent(column->GetText()).x + AUTOSIZE_COL_MARGIN;

    width += 2*EXTRA_WIDTH;

    // check for column header's image availability
    const int image = column->GetImage();
    if ( image != -1 )
    {
        if ( m_small_images )
        {
            int ix = 0, iy = 0;
            m_small_images->GetImageLogicalSize(this, ix, iy);
            width += ix + HEADER_IMAGE_MARGIN_IN_REPORT_MODE;
        }
    }

    return width;
}

void ibListMainWindow::SetColumn( int col, const wxListItem &item )
{
    wxCHECK_RET( col >= 0 && col < (int)m_columns.size(),
                 "invalid column index in SetColumn" );

    ibListHeaderData* const column = &m_columns[col];
    column->SetItem( item );

    if ( item.m_width == wxLIST_AUTOSIZE_USEHEADER )
        column->SetWidth(ComputeMinHeaderWidth(column));

    ibListHeaderWindow *headerWin = GetListCtrl()->m_headerWin;
    if ( headerWin )
        headerWin->m_dirty = true;

    m_dirty = true;

    // invalidate it as it has to be recalculated
    m_headerWidth = 0;
}

class ibListCtrlMaxWidthCalculator : public wxMaxWidthCalculatorBase
{
public:
    ibListCtrlMaxWidthCalculator(ibListMainWindow *listmain, unsigned int column)
        : wxMaxWidthCalculatorBase(column),
          m_listmain(listmain)
    {
    }

    virtual void UpdateWithRow(int row) override
    {
        ibListLineData *line = m_listmain->GetLine( row );

        wxListItem item;
        line->m_items.at(GetColumn()).GetItem(item);

        UpdateWithWidth(m_listmain->GetItemWidthWithImage(&item));
    }

private:
    ibListMainWindow* const m_listmain;
};


void ibListMainWindow::SetColumnWidth( int col, int width )
{
    wxCHECK_RET( col >= 0 && col < GetColumnCount(),
                 wxT("invalid column index") );

    wxCHECK_RET( InReportView(),
                 wxT("SetColumnWidth() can only be called in report mode.") );

    m_dirty = true;

    ibListHeaderWindow *headerWin = GetListCtrl()->m_headerWin;
    if ( headerWin )
        headerWin->m_dirty = true;

    wxCHECK_RET( col >= 0 && col < (int)m_columns.size(),
                 "invalid column index in SetColumnWidth" );

    ibListHeaderData* const column = &m_columns[col];

    if ( width == wxLIST_AUTOSIZE_USEHEADER || width == wxLIST_AUTOSIZE )
    {
        ibListCtrlMaxWidthCalculator calculator(this, col);

        calculator.UpdateWithWidth(AUTOSIZE_COL_MARGIN);

        if ( width == wxLIST_AUTOSIZE_USEHEADER )
            calculator.UpdateWithWidth(ComputeMinHeaderWidth(column));

        //  if the cached column width isn't valid then recalculate it
        ibColWidthInfo& widthInfo = m_aColWidths[col];
        if ( widthInfo.bNeedsUpdate )
        {
            size_t first_visible, last_visible;
            GetVisibleLinesRange(&first_visible, &last_visible);

            calculator.ComputeBestColumnWidth(GetItemCount(),
                                              first_visible, last_visible);
            widthInfo.nMaxWidth = calculator.GetMaxWidth();
            widthInfo.bNeedsUpdate = false;
        }
        else
        {
            calculator.UpdateWithWidth(widthInfo.nMaxWidth);
        }

        width = calculator.GetMaxWidth() + AUTOSIZE_COL_MARGIN;

        if ( col == 0 && HasCheckBoxes() )
        {
            // also account for the space needed by the checkbox
            // SEAM vs wx: the box the line draws - the window's renderer's (DrawInReportMode)
            width += GetRenderer()->GetCheckBoxSize(this).x
                        + 2*MARGIN_AROUND_CHECKBOX;
        }

        // expand the last column to fit the client size
        // only for AUTOSIZE_USEHEADER to mimic MSW behaviour
        if ( (width == wxLIST_AUTOSIZE_USEHEADER) && (col == GetColumnCount() - 1) )
        {
            int margin = GetClientSize().GetX();
            for ( int i = 0; i < col && margin > 0; ++i )
                margin -= m_columns.at(i).GetWidth();

            if ( margin > width )
                width = margin;
        }
    }

    column->SetWidth( width );

    // invalidate it as it has to be recalculated
    m_headerWidth = 0;
}

int ibListMainWindow::GetHeaderWidth() const
{
    if ( !m_headerWidth )
    {
        ibListMainWindow *self = wxConstCast(this, ibListMainWindow);

        size_t count = GetColumnCount();
        for ( size_t col = 0; col < count; col++ )
        {
            self->m_headerWidth += GetColumnWidth(col);
        }
    }

    return m_headerWidth;
}

void ibListMainWindow::GetColumn( int col, wxListItem &item ) const
{
    wxCHECK_RET( col >= 0 && col < (int)m_columns.size(),
                 "invalid column index in GetColumn" );

    m_columns[col].GetItem( item );
}

int ibListMainWindow::GetColumnWidth( int col ) const
{
    wxCHECK_MSG( col >= 0 && col < (int)m_columns.size(), 0,
                 "invalid column index in GetColumnWidth" );

    return m_columns[col].GetWidth();
}

// ----------------------------------------------------------------------------
// item state
// ----------------------------------------------------------------------------

void ibListMainWindow::SetItem( wxListItem &item )
{
    long id = item.m_itemId;
    wxCHECK_RET( id >= 0 && (size_t)id < GetItemCount(),
                 wxT("invalid item index in SetItem") );

    if ( !IsVirtual() )
    {
        ibListLineData *line = GetLine((size_t)id);
        line->SetItem( item.m_col, item );

        // Set item state if user wants
        if ( item.m_mask & wxLIST_MASK_STATE )
            SetItemState( item.m_itemId, item.m_state, item.m_state );

        if (InReportView())
        {
            //  update the Max Width Cache if needed
            int width = GetItemWidthWithImage(&item);

            ibColWidthInfo& widthInfo = m_aColWidths.at(item.m_col);
            if ( width > widthInfo.nMaxWidth )
            {
                widthInfo.nMaxWidth = width;
                widthInfo.bNeedsUpdate = true;
            }
        }
    }

    // update the item on screen unless we're going to update everything soon
    // anyhow
    if ( !m_dirty )
    {
        wxRect rectItem;
        GetItemRect(id, rectItem);
        RefreshRect(rectItem);
    }
}

void ibListMainWindow::SetItemStateAll(long state, long stateMask)
{
    if ( IsEmpty() )
        return;

    // first deal with selection
    if ( stateMask & wxLIST_STATE_SELECTED )
    {
        // set/clear select state
        if ( IsVirtual() )
        {
            // optimized version for virtual listctrl.
            m_selStore.SelectRange(0, GetItemCount() - 1, state == wxLIST_STATE_SELECTED);
            Refresh();
        }
        else if ( state & wxLIST_STATE_SELECTED )
        {
            const long count = GetItemCount();
            for( long i = 0; i <  count; i++ )
            {
                SetItemState( i, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED );
            }

        }
        else
        {
            // clear for non virtual (somewhat optimized by using GetNextItem())
            long i = -1;
            while ( (i = GetNextItem(i, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1 )
            {
                SetItemState( i, 0, wxLIST_STATE_SELECTED );
            }
        }
    }

    if ( HasCurrent() && (state == 0) && (stateMask & wxLIST_STATE_FOCUSED) )
    {
        // unfocus all: only one item can be focussed, so clearing focus for
        // all items is simply clearing focus of the focussed item.
        SetItemState(m_current, state, stateMask);
    }
    //(setting focus to all items makes no sense, so it is not handled here.)
}

void ibListMainWindow::SetItemState( long litem, long state, long stateMask )
{
    if ( litem == -1 )
    {
        SetItemStateAll(state, stateMask);
        return;
    }

    wxCHECK_RET( litem >= 0 && (size_t)litem < GetItemCount(),
                 wxT("invalid list ctrl item index in SetItem") );

    size_t oldCurrent = m_current;
    size_t item = (size_t)litem;    // safe because of the check above

    // do we need to change the focus?
    if ( stateMask & wxLIST_STATE_FOCUSED )
    {
        if ( state & wxLIST_STATE_FOCUSED )
        {
            // don't do anything if this item is already focused
            if ( item != m_current )
            {
                ChangeCurrent(item);

                if ( oldCurrent != (size_t)-1 )
                {
                    if ( IsSingleSel() )
                    {
                        HighlightLine(oldCurrent, false);
                    }

                    RefreshLine(oldCurrent);
                }

                RefreshLine( m_current );
            }
        }
        else // unfocus
        {
            // don't do anything if this item is not focused
            if ( item == m_current )
            {
                ResetCurrent();

                if ( IsSingleSel() )
                {
                    // we must unselect the old current item as well or we
                    // might end up with more than one selected item in a
                    // single selection control
                    HighlightLine(oldCurrent, false);
                }

                RefreshLine( oldCurrent );
            }
        }
    }

    // do we need to change the selection state?
    if ( stateMask & wxLIST_STATE_SELECTED )
    {
        bool on = (state & wxLIST_STATE_SELECTED) != 0;

        if ( IsSingleSel() )
        {
            if ( on )
            {
                // selecting the item also makes it the focused one in the
                // single sel mode
                if ( m_current != item )
                {
                    ChangeCurrent(item);

                    if ( oldCurrent != (size_t)-1 )
                    {
                        HighlightLine( oldCurrent, false );
                        RefreshLine( oldCurrent );
                    }
                }
            }
            else // off
            {
                // only the current item may be selected anyhow
                if ( item != m_current )
                    return;
            }
        }

        if ( HighlightLine(item, on) )
        {
            RefreshLine(item);
        }
    }
}

int ibListMainWindow::GetItemState( long item, long stateMask ) const
{
    wxCHECK_MSG( item >= 0 && (size_t)item < GetItemCount(), 0,
                 wxT("invalid list ctrl item index in GetItemState()") );

    int ret = wxLIST_STATE_DONTCARE;

    if ( stateMask & wxLIST_STATE_FOCUSED )
    {
        if ( (size_t)item == m_current )
            ret |= wxLIST_STATE_FOCUSED;
    }

    if ( stateMask & wxLIST_STATE_SELECTED )
    {
        if ( IsHighlighted(item) )
            ret |= wxLIST_STATE_SELECTED;
    }

    return ret;
}

void ibListMainWindow::GetItem( wxListItem &item ) const
{
    wxCHECK_RET( item.m_itemId >= 0 && (size_t)item.m_itemId < GetItemCount(),
                 wxT("invalid item index in GetItem") );

    ibListLineData *line = GetLine((size_t)item.m_itemId);
    line->GetItem( item.m_col, item );

    // Get item state if user wants it
    if ( item.m_mask & wxLIST_MASK_STATE )
        item.m_state = GetItemState( item.m_itemId, wxLIST_STATE_SELECTED |
                                                 wxLIST_STATE_FOCUSED );
}

// ----------------------------------------------------------------------------
// item count
// ----------------------------------------------------------------------------

size_t ibListMainWindow::GetItemCount() const
{
    return IsVirtual() ? m_countVirt : m_lines.size();
}

void ibListMainWindow::SetItemCount(long count)
{
    // Update the current item if it's not valid any longer (notice that this
    // invalidates it completely if the control is becoming empty, which is the
    // right thing to do).
    if ( HasCurrent() && m_current >= (size_t)count )
        ChangeCurrent(count - 1);

    // And do the same thing for the multiple selection anchor.
    if ( m_anchor != (size_t)-1 && m_anchor >= (size_t)count )
        m_anchor = count - 1;

    m_selStore.SetItemCount(count);
    m_countVirt = count;

    ResetVisibleLinesRange();

    // scrollbars must be reset
    m_dirty = true;
}

int ibListMainWindow::GetSelectedItemCount() const
{
    // deal with the quick case first
    if ( IsSingleSel() )
        return HasCurrent() ? IsHighlighted(m_current) : false;

    // virtual controls remmebers all its selections itself
    if ( IsVirtual() )
        return m_selStore.GetSelectedCount();

    return m_selCount;
}

// ----------------------------------------------------------------------------
// item position/size
// ----------------------------------------------------------------------------

wxRect ibListMainWindow::GetViewRect() const
{
    wxASSERT_MSG( !HasFlag(wxLC_LIST), "not implemented for list view" );

    // we need to find the longest/tallest label
    wxCoord xMax = 0, yMax = 0;
    const int count = GetItemCount();
    if ( count )
    {
        for ( int i = 0; i < count; i++ )
        {
            // we need logical, not physical, coordinates here, so use
            // GetLineRect() instead of GetItemRect()
            wxRect r = GetLineRect(i);

            wxCoord x = r.GetRight(),
                    y = r.GetBottom();

            if ( x > xMax )
                xMax = x;
            if ( y > yMax )
                yMax = y;
        }
    }

    // some fudge needed to make it look prettier
    xMax += 2 * EXTRA_BORDER_X;
    yMax += 2 * EXTRA_BORDER_Y;

    // account for the scrollbars if necessary
    const wxSize sizeAll = GetClientSize();
    const ibListCtrl* const listctrl = GetListCtrl();
    if ( xMax > sizeAll.x )
        yMax += listctrl->GetScrollbarSize(wxHORIZONTAL);
    if ( yMax > sizeAll.y )
        xMax += listctrl->GetScrollbarSize(wxVERTICAL);

    return wxRect(0, 0, xMax, yMax);
}

bool
ibListMainWindow::GetSubItemRect(long item, long subItem, wxRect& rect,
                                 int code) const
{
    wxCHECK_MSG( subItem == wxLIST_GETSUBITEMRECT_WHOLEITEM || InReportView(),
                 false,
                 wxT("GetSubItemRect only meaningful in report view") );
    wxCHECK_MSG( item >= 0 && (size_t)item < GetItemCount(), false,
                 wxT("invalid item in GetSubItemRect") );

    // ensure that we're laid out, otherwise we could return nonsense
    if ( m_dirty )
    {
        wxConstCast(this, ibListMainWindow)->RecalculatePositions();
    }

    rect = GetLineRect((size_t)item);

    // Adjust rect to specified column
    if ( subItem != wxLIST_GETSUBITEMRECT_WHOLEITEM )
    {
        wxCHECK_MSG( subItem >= 0 && subItem < GetColumnCount(), false,
                     wxT("invalid subItem in GetSubItemRect") );

        for (int i = 0; i < subItem; i++)
        {
            rect.x += GetColumnWidth(i);
        }
        rect.width = GetColumnWidth(subItem);

        switch ( code )
        {
            case wxLIST_RECT_BOUNDS:
                // Nothing to do.
                break;

            case wxLIST_RECT_ICON:
            case wxLIST_RECT_LABEL:
                // Note: this needs to be kept in sync with DrawInReportMode().
                {
                    rect.x += ICON_OFFSET_X;
                    rect.width -= ICON_OFFSET_X;

                    ibListLineData* const line = GetLine(item);
                    if ( subItem == 0 && line->HasImage() )
                    {
                        int ix, iy;
                        GetImageSize(ix, iy);

                        if ( code == wxLIST_RECT_ICON )
                        {
                            rect.y += (rect.height - iy) / 2;
                            rect.width = ix;
                            rect.height = iy;
                        }
                        else // wxLIST_RECT_LABEL
                        {
                            // this includes the margin between icon and label (IMAGE_MARGIN_IN_REPORT_MODE)
                            rect.x += ix;
                            rect.width -= ix;
                        }
                    }
                    else // No icon
                    {
                        if ( code == wxLIST_RECT_ICON )
                            rect = wxRect();
                        //else: label rect is the same as the full one
                    }
                }
                break;

            default:
                wxFAIL_MSG(wxS("Unknown rectangle requested"));
                return false;
        }
    }

    GetListCtrl()->CalcScrolledPosition(rect.x, rect.y, &rect.x, &rect.y);

    return true;
}

bool ibListMainWindow::GetItemPosition(long item, wxPoint& pos) const
{
    wxRect rect;
    GetItemRect(item, rect);

    pos.x = rect.x;
    pos.y = rect.y;

    return true;
}

// ----------------------------------------------------------------------------
// checkboxes
// ----------------------------------------------------------------------------

bool ibListMainWindow::HasCheckBoxes() const
{
    return m_hasCheckBoxes;
}

bool ibListMainWindow::EnableCheckBoxes(bool enable)
{
    m_hasCheckBoxes = enable;

    m_dirty = true;
    m_headerWidth = 0;
    Refresh();

    return true;
}

void ibListMainWindow::CheckItem(long item, bool state)
{
    wxCHECK_RET( HasCheckBoxes(), "checkboxes are disabled" );

    if ( !IsVirtual() )
    {
        ibListLineData* line = GetLine((size_t)item);

        // Don't send any events if nothing changes, for consistency with
        // wxMSW, where the native control doesn't send them in this case.
        if ( line->IsChecked() == state )
            return;

        line->Check(state);

        RefreshLine(item);
    }

    SendNotify(item, state ? wxEVT_LIST_ITEM_CHECKED
        : wxEVT_LIST_ITEM_UNCHECKED);
}

bool ibListMainWindow::IsItemChecked(long item) const
{
    if ( !HasCheckBoxes() )
        return false;

    if ( !IsVirtual() )
    {
        ibListLineData* line = GetLine((size_t)item);
        return line->IsChecked();
    }
    else
    {
        ibListCtrl* listctrl = GetListCtrl();
        return listctrl->OnGetItemIsChecked(item);
    }
}

bool ibListMainWindow::IsInsideCheckBox(long item, int x, int y) const
{
    if ( HasCheckBoxes() )
    {
        wxRect lineRect = GetLineRect(item);
        // SEAM vs wx: the box the line draws - the window's renderer's (DrawInReportMode)
        wxSize cbSize = GetRenderer()->GetCheckBoxSize(const_cast<ibListMainWindow*>(this));
        int yOffset = (lineRect.height - cbSize.GetHeight()) / 2;
        wxRect rr(wxPoint(lineRect.x + MARGIN_AROUND_CHECKBOX, lineRect.y + yOffset), cbSize);

        return rr.Contains(wxPoint(x, y));
    }

    return false;
}

// ----------------------------------------------------------------------------
// geometry calculation
// ----------------------------------------------------------------------------

void ibListMainWindow::RecalculatePositions()
{
    const int lineHeight = GetLineHeight();

    wxInfoDC dc( this );

    const size_t count = GetItemCount();

    int iconSpacing;
    if ( HasFlag(wxLC_ICON) && m_normal_images )
        iconSpacing = m_normal_spacing;
    else if ( HasFlag(wxLC_SMALL_ICON) && m_small_images )
        iconSpacing = m_small_spacing;
    else
        iconSpacing = 0;

    // Note that we do not call GetClientSize() here but
    // GetSize() and subtract the border size for sunken
    // borders manually. This is technically incorrect,
    // but we need to know the client area's size WITHOUT
    // scrollbars here. Since we don't know if there are
    // any scrollbars, we use GetSize() instead. Another
    // solution would be to call SetScrollbars() here to
    // remove the scrollbars and call GetClientSize() then,
    // but this might result in flicker and - worse - will
    // reset the scrollbars to 0 which is not good at all
    // if you resize a dialog/window, but don't want to
    // reset the window scrolling. RR.
    // Furthermore, we actually do NOT subtract the border
    // width as 2 pixels is just the extra space which we
    // need around the actual content in the window. Other-
    // wise the text would e.g. touch the upper border. RR.
    int clientWidth,
        clientHeight;
    GetSize( &clientWidth, &clientHeight );

    if ( InReportView() )
    {
        // all lines have the same height and we scroll one line per step
        int entireHeight = count * lineHeight + LINE_SPACING;

        m_linesPerPage = clientHeight / lineHeight;

        ResetVisibleLinesRange();

        GetListCtrl()->SetScrollbars( SCROLL_UNIT_X, lineHeight,
                       GetHeaderWidth() / SCROLL_UNIT_X,
                       (entireHeight + lineHeight - 1) / lineHeight,
                       GetListCtrl()->GetScrollPos(wxHORIZONTAL),
                       GetListCtrl()->GetScrollPos(wxVERTICAL),
                       true );
    }
    else // !report
    {
        // we have 3 different layout strategies: either layout all items
        // horizontally/vertically (wxLC_ALIGN_XXX styles explicitly given) or
        // to arrange them in top to bottom, left to right (don't ask me why
        // not the other way round...) order
        if ( HasFlag(wxLC_ALIGN_LEFT | wxLC_ALIGN_TOP) )
        {
            int x = EXTRA_BORDER_X;
            int y = EXTRA_BORDER_Y;

            wxCoord widthMax = 0;

            size_t i;
            for ( i = 0; i < count; i++ )
            {
                ibListLineData *line = GetLine(i);
                line->CalculateSize( &dc, iconSpacing );
                line->SetPosition( x, y, iconSpacing );

                wxSize sizeLine = GetLineSize(i);

                if ( HasFlag(wxLC_ALIGN_TOP) )
                {
                    if ( sizeLine.x > widthMax )
                        widthMax = sizeLine.x;

                    y += sizeLine.y;
                }
                else // wxLC_ALIGN_LEFT
                {
                    x += sizeLine.x + MARGIN_BETWEEN_ROWS;
                }
            }

            if ( HasFlag(wxLC_ALIGN_TOP) )
            {
                // traverse the items again and tweak their sizes so that they are
                // all the same in a row
                for ( i = 0; i < count; i++ )
                {
                    ibListLineData *line = GetLine(i);
                    line->m_gi->ExtendWidth(widthMax);
                }
            }

            GetListCtrl()->SetScrollbars
            (
                SCROLL_UNIT_X,
                lineHeight,
                (x + SCROLL_UNIT_X) / SCROLL_UNIT_X,
                (y + lineHeight) / lineHeight,
                GetListCtrl()->GetScrollPos( wxHORIZONTAL ),
                GetListCtrl()->GetScrollPos( wxVERTICAL ),
                true
            );
        }
        else // "flowed" arrangement, the most complicated case
        {
            // at first we try without any scrollbars, if the items don't fit into
            // the window, we recalculate after subtracting the space taken by the
            // scrollbar

            int entireWidth = 0;

            for (int tries = 0; tries < 2; tries++)
            {
                entireWidth = 2 * EXTRA_BORDER_X;

                if (tries == 1)
                {
                    // Now we have decided that the items do not fit into the
                    // client area, so we need a scrollbar
                    entireWidth += SCROLL_UNIT_X;
                }

                int x = EXTRA_BORDER_X;
                int y = EXTRA_BORDER_Y;

                // Note that "row" here is vertical, i.e. what is called
                // "column" in many other places in wxWidgets.
                int maxWidthInThisRow = 0;

                m_linesPerPage = 0;
                int currentlyVisibleLines = 0;

                for (size_t i = 0; i < count; i++)
                {
                    currentlyVisibleLines++;
                    ibListLineData *line = GetLine( i );
                    line->CalculateSize( &dc, iconSpacing );
                    line->SetPosition( x, y, iconSpacing );

                    wxSize sizeLine = GetLineSize( i );

                    if ( maxWidthInThisRow < sizeLine.x )
                        maxWidthInThisRow = sizeLine.x;

                    y += sizeLine.y;
                    if (currentlyVisibleLines > m_linesPerPage)
                        m_linesPerPage = currentlyVisibleLines;

                    // Have we reached the end of the row either because no
                    // more items would fit or because there are simply no more
                    // items?
                    if ( y + sizeLine.y >= clientHeight
                            || i == count - 1)
                    {
                        // Adjust all items in this row to have the same
                        // width to ensure that they all align horizontally in
                        // icon view.
                        if ( HasFlag(wxLC_ICON) || HasFlag(wxLC_SMALL_ICON) )
                        {
                            size_t firstRowLine = i - currentlyVisibleLines + 1;
                            for (size_t j = firstRowLine; j <= i; j++)
                            {
                                GetLine(j)->m_gi->ExtendWidth(maxWidthInThisRow);
                            }
                        }

                        currentlyVisibleLines = 0;
                        y = EXTRA_BORDER_Y;
                        maxWidthInThisRow += MARGIN_BETWEEN_ROWS;
                        x += maxWidthInThisRow;
                        entireWidth += maxWidthInThisRow;
                        maxWidthInThisRow = 0;
                    }

                    if ( (tries == 0) &&
                            (entireWidth + SCROLL_UNIT_X > clientWidth) )
                    {
                        clientHeight -=
                            GetListCtrl()->GetScrollbarSize(wxHORIZONTAL);
                        m_linesPerPage = 0;
                        break;
                    }

                    if ( i == count - 1 )
                        tries = 1;  // Everything fits, no second try required.
                }
            }

            GetListCtrl()->SetScrollbars
            (
                SCROLL_UNIT_X,
                lineHeight,
                (entireWidth + SCROLL_UNIT_X) / SCROLL_UNIT_X,
                0,
                GetListCtrl()->GetScrollPos( wxHORIZONTAL ),
                0,
                true
            );
        }
    }
}

void ibListMainWindow::RecalculatePositionsAndRefresh()
{
    RecalculatePositions();

    // Whether the window was dirty or not before, it won't be any longer after
    // the next refresh.
    m_dirty = false;
    Refresh();

    ibListHeaderWindow *headerWin = GetListCtrl()->m_headerWin;
    if ( headerWin && headerWin->m_dirty )
    {
        headerWin->m_dirty = false;
        headerWin->Refresh();
    }
}

void ibListMainWindow::UpdateCurrent()
{
    if ( !HasCurrent() && !IsEmpty() )
    {
        // Initialise m_current to the first item without sending any
        // wxEVT_LIST_ITEM_FOCUSED event (typicaly when the control gains focus)
        // and this is to allow changing the focused item using the arrow keys.
        // which is the behaviour found in the wxMSW port.
        ChangeCurrentWithoutEvent(0);
    }
}

long ibListMainWindow::GetNextItem( long item,
                                    int WXUNUSED(geometry),
                                    int state ) const
{
    long ret = item,
         max = GetItemCount();
    wxCHECK_MSG( (ret == -1) || (ret < max), -1,
                 wxT("invalid listctrl index in GetNextItem()") );

    // notice that we start with the next item (or the first one if item == -1)
    // and this is intentional to allow writing a simple loop to iterate over
    // all selected items
    ret++;
    if ( ret == max )
        // this is not an error because the index was OK initially,
        // just no such item
        return -1;

    if ( !state )
        // any will do
        return (size_t)ret;

    size_t count = GetItemCount();
    for ( size_t line = (size_t)ret; line < count; line++ )
    {
        if ( (state & wxLIST_STATE_FOCUSED) && (line == m_current) )
            return line;

        if ( (state & wxLIST_STATE_SELECTED) && IsHighlighted(line) )
            return line;
    }

    return -1;
}

// ----------------------------------------------------------------------------
// deleting stuff
// ----------------------------------------------------------------------------

void ibListMainWindow::DeleteItem( long lindex )
{
    size_t count = GetItemCount();

    wxCHECK_RET( (lindex >= 0) && ((size_t)lindex < count),
                 wxT("invalid item index in DeleteItem") );

    size_t index = (size_t)lindex;

    // we don't need to adjust the index for the previous items
    if ( HasCurrent() && m_current >= index )
    {
        // if the current item is being deleted, we want the next one to
        // become selected - unless there is no next one - so don't adjust
        // m_current in this case
        if ( m_current != index || m_current == count - 1 )
            m_current--;
    }

    if ( InReportView() )
    {
        //  mark the Column Max Width cache as dirty if the items in the line
        //  we're deleting contain the Max Column Width
        ibListLineData * const line = GetLine(index);
        wxListItem      item;

        size_t i = 0;
        for ( const auto& it : line->m_items )
        {
            it.GetItem(item);

            int itemWidth;
            itemWidth = GetItemWidthWithImage(&item);

            ibColWidthInfo& widthInfo = m_aColWidths.at(i++);
            if ( itemWidth >= widthInfo.nMaxWidth )
                widthInfo.bNeedsUpdate = true;
        }

        ResetVisibleLinesRange();
    }

    SendNotify( index, wxEVT_LIST_DELETE_ITEM, wxDefaultPosition );

    if ( IsVirtual() )
    {
        m_countVirt--;
        m_selStore.OnItemDelete(index);
    }
    else
    {
        auto const iter =  m_lines.begin() + index;
        if ( iter->IsHighlighted() )
            UpdateSelectionCount(false);
        m_lines.erase(iter);
    }

    // we need to refresh the (vert) scrollbar as the number of items changed
    m_dirty = true;

    RefreshAfter(index);

    // This might be a wxGTK bug, but when deleting the last item in a control
    // with many items, the vertical scroll position may change so that the new
    // last item is not visible any longer, which is very annoying from the
    // user point of view. Ensure that whatever happens, this item is visible.
    if ( count > 1 && m_current != (size_t)-1 )
        EnsureVisible(m_current);
}

void ibListMainWindow::DeleteColumn( int col )
{
    wxCHECK_RET( col >= 0 && col < (int)m_columns.size(),
                 "invalid column index in DeleteColumn()" );

    m_dirty = true;
    m_columns.erase( m_columns.begin() + col );

    if ( !IsVirtual() )
    {
        // update all the items
        for ( auto& line : m_lines )
        {
            // In the following atypical but possible scenario it can be
            // legal to call DeleteColumn() but the items may not have any
            // values for it:
            //  1. In report view, insert a second column.
            //  2. Still in report view, add an item with 2 values.
            //  3. Switch to an icon (or list) view.
            //  4. Add an item -- necessarily with 1 value only.
            //  5. Switch back to report view.
            //  6. Call DeleteColumn().
            // So we need to check for this as otherwise we would simply crash
            // if this happens.
            if ( line.m_items.size() <= static_cast<unsigned>(col) )
                continue;

            line.m_items.erase(line.m_items.begin() + col);
        }
    }

    if ( InReportView() )   //  we only cache max widths when in Report View
    {
        m_aColWidths.erase(m_aColWidths.begin() + col);
    }

    // invalidate it as it has to be recalculated
    m_headerWidth = 0;
}

void ibListMainWindow::DoDeleteAllItems()
{
    // We will need to update all columns if any items are inserted again.
    if ( InReportView() )
    {
        for ( auto& widthInfo : m_aColWidths )
            widthInfo.bNeedsUpdate = true;
    }

    if ( IsEmpty() )
        // nothing to do - in particular, don't send the event
        return;

    ResetCurrent();

    // to make the deletion of all items faster, we don't send the
    // notifications for each item deletion in this case but only one event
    // for all of them: this is compatible with wxMSW and documented in
    // DeleteAllItems() description

    wxListEvent event( wxEVT_LIST_DELETE_ALL_ITEMS, GetParent()->GetId() );
    event.SetEventObject( GetParent() );
    GetParent()->GetEventHandler()->ProcessEvent( event );

    if ( IsVirtual() )
    {
        m_countVirt = 0;
        m_selStore.Clear();
    }
    else
    {
        m_selCount = 0;
    }

    if ( InReportView() )
        ResetVisibleLinesRange();

    m_lines.clear();
}

void ibListMainWindow::DeleteAllItems()
{
    DoDeleteAllItems();

    RecalculatePositionsAndRefresh();
}

void ibListMainWindow::DeleteEverything()
{
    m_columns.clear();
    m_aColWidths.clear();

    DeleteAllItems();
}

// ----------------------------------------------------------------------------
// scanning for an item
// ----------------------------------------------------------------------------

void ibListMainWindow::EnsureVisible( long index )
{
    wxCHECK_RET( index >= 0 && (size_t)index < GetItemCount(),
                 wxT("invalid index in EnsureVisible") );

    // We have to call this here because the label in question might just have
    // been added and its position is not known yet
    if ( m_dirty )
        RecalculatePositions();

    MoveToItem((size_t)index);
}

long ibListMainWindow::FindItem(long start, const wxString& str, bool partial )
{
    if (str.empty())
        return wxNOT_FOUND;

    long pos = start;
    wxString str_upper = str.Upper();
    if (pos < 0)
        pos = 0;

    size_t count = GetItemCount();
    for ( size_t i = (size_t)pos; i < count; i++ )
    {
        ibListLineData *line = GetLine(i);
        wxString line_upper = line->GetText(0).Upper();
        if (!partial)
        {
            if (line_upper == str_upper )
                return i;
        }
        else
        {
            if (line_upper.find(str_upper) == 0)
                return i;
        }
    }

    return wxNOT_FOUND;
}

long ibListMainWindow::FindItem(long start, wxUIntPtr data)
{
    long pos = start;
    if (pos < 0)
        pos = 0;

    size_t count = GetItemCount();
    for (size_t i = (size_t)pos; i < count; i++)
    {
        ibListLineData *line = GetLine(i);
        wxListItem item;
        line->GetItem( 0, item );
        if (item.m_data == data)
            return i;
    }

    return wxNOT_FOUND;
}

long ibListMainWindow::FindItem( const wxPoint& pt )
{
    size_t topItem;
    GetVisibleLinesRange( &topItem, nullptr );

    wxPoint p;
    GetItemPosition( GetItemCount() - 1, p );
    if ( p.y == 0 )
        return topItem;

    long id = (long)floor( pt.y * double(GetItemCount() - topItem - 1) / p.y + topItem );
    if ( id >= 0 && id < (long)GetItemCount() )
        return id;

    return wxNOT_FOUND;
}

long ibListMainWindow::HitTest( int x, int y, int &flags ) const
{
    GetListCtrl()->CalcUnscrolledPosition( x, y, &x, &y );

    size_t count = GetItemCount();

    if ( InReportView() )
    {
        size_t current = y / GetLineHeight();
        if ( current < count )
        {
            flags = HitTestLine(current, x, y);
            if ( flags )
                return current;
        }
    }
    else // !report
    {
        // TODO: optimize it too! this is less simple than for report view but
        //       enumerating all items is still not a way to do it!!
        for ( size_t current = 0; current < count; current++ )
        {
            flags = HitTestLine(current, x, y);
            if ( flags )
                return current;
        }
    }

    return wxNOT_FOUND;
}

// ----------------------------------------------------------------------------
// adding stuff
// ----------------------------------------------------------------------------

void ibListMainWindow::InsertItem( wxListItem &item )
{
    wxASSERT_MSG( !IsVirtual(), wxT("can't be used with virtual control") );

    int count = GetItemCount();
    wxCHECK_RET( item.m_itemId >= 0, wxT("invalid item index") );

    if (item.m_itemId > count)
        item.m_itemId = count;

    size_t id = item.m_itemId;

    m_dirty = true;

    if ( InReportView() )
    {
        ResetVisibleLinesRange();

        const unsigned col = item.GetColumn();
        wxCHECK_RET( col < m_aColWidths.size(), "invalid item column" );

        // calculate the width of the item and adjust the max column width
        ibColWidthInfo& widthInfo = m_aColWidths[col];
        int width = GetItemWidthWithImage(&item);
        item.SetWidth(width);
        if (width > widthInfo.nMaxWidth)
        {
            widthInfo.nMaxWidth = width;
            widthInfo.bNeedsUpdate = true;
        }
    }

    ibListLineData line(this);

    line.SetItem( item.m_col, item );
    if ( item.m_mask & wxLIST_MASK_IMAGE )
    {
        // Reset the buffered height if it's not big enough for the new image.
        int image = item.GetImage();
        if ( m_small_images && image != -1 && InReportView() )
        {
            int imageWidth, imageHeight;
            m_small_images->GetImageLogicalSize(this, imageWidth, imageHeight);

            if ( imageHeight > m_lineHeight )
                m_lineHeight = 0;
        }
    }

    m_lines.insert( m_lines.begin() + id, std::move(line) );

    m_dirty = true;

    // If an item is selected at or below the point of insertion, we need to
    // increment the member variables because the current row's index has gone
    // up by one
    if ( HasCurrent() && m_current >= id )
        m_current++;

    SendNotify(id, wxEVT_LIST_INSERT_ITEM);

    RefreshLines(id, GetItemCount() - 1);
}

long ibListMainWindow::InsertColumn( long col, const wxListItem &item )
{
    long idx = -1;

    m_dirty = true;
    if ( InReportView() )
    {
        ibListHeaderData column( item );
        if (item.m_width == wxLIST_AUTOSIZE_USEHEADER)
            column.SetWidth(ComputeMinHeaderWidth(&column));

        ibColWidthInfo colWidthInfo(0, IsVirtual());

        bool insert = (col >= 0) && ((size_t)col < m_columns.size());
        if ( insert )
        {
            m_columns.insert( m_columns.begin() + col, column );
            m_aColWidths.insert( m_aColWidths.begin() + col, colWidthInfo );
            idx = col;
        }
        else
        {
            idx = m_aColWidths.size();
            m_columns.push_back( column );
            m_aColWidths.push_back( colWidthInfo );
        }

        if ( !IsVirtual() )
        {
            // update all the items
            for ( auto& line : m_lines )
            {
                auto& items = line.m_items;
                if ( insert )
                    items.emplace(items.begin() + col, this);
                else
                    items.emplace_back(this);
            }
        }

        // invalidate it as it has to be recalculated
        m_headerWidth = 0;
    }
    return idx;
}

int ibListMainWindow::GetItemWidthWithImage(wxListItem * item)
{
    int width = 0;
    wxInfoDC dc(this);

    if (item->GetImage() != -1)
    {
        int ix, iy;
        GetImageSize( ix, iy );
        width += ix + IMAGE_MARGIN_IN_REPORT_MODE;
    }

    if (!item->GetText().empty())
    {
        wxCoord w;
        dc.GetTextExtent( item->GetText(), &w, nullptr );
        width += w;
    }

    return width;
}

// ----------------------------------------------------------------------------
// sorting
// ----------------------------------------------------------------------------

struct ibListLineComparator
{
    ibListLineComparator(wxListCtrlCompare& f, wxIntPtr data)
        : m_f(f),
          m_data(data)
    {
    }

    bool operator()(const ibListLineData& line1,
                    const ibListLineData& line2) const
    {
        wxListItem item;
        line1.GetItem( 0, item );
        wxUIntPtr data1 = item.m_data;
        line2.GetItem( 0, item );
        wxUIntPtr data2 = item.m_data;
        return m_f(data1, data2, m_data) < 0;
    }

    const wxListCtrlCompare m_f;
    const wxIntPtr          m_data;
};

void ibListMainWindow::SortItems( wxListCtrlCompare fn, wxIntPtr data )
{
    // selections won't make sense any more after sorting the items so reset
    // them
    HighlightAll(false);
    ResetCurrent();

    std::sort(m_lines.begin(), m_lines.end(), ibListLineComparator(fn, data));

    m_dirty = true;
}

// ----------------------------------------------------------------------------
// scrolling
// ----------------------------------------------------------------------------

void ibListMainWindow::OnScroll(wxScrollWinEvent& event)
{
    // update our idea of which lines are shown when we redraw the window the
    // next time
    ResetVisibleLinesRange();

    if ( event.GetOrientation() == wxHORIZONTAL && HasHeader() )
    {
        ibListCtrl* lc = GetListCtrl();
        wxCHECK_RET( lc, wxT("no listctrl window?") );

        if (lc->m_headerWin) // when we use wxLC_NO_HEADER, m_headerWin==nullptr
        {
            lc->m_headerWin->Refresh();
            lc->m_headerWin->Update();
        }
    }
}

void ibListMainWindow::ScrollWindow(int dx, int dy, const wxRect *rect)
{
    ResetVisibleLinesRange();

    ibWindow::ScrollWindow(dx, dy, rect);
}

int ibListMainWindow::GetCountPerPage() const
{
    // The number of lines per page may be out of date if we're dirty, so
    // recompute it in this case.
    if ( m_dirty )
        wxConstCast(this, ibListMainWindow)->RecalculatePositions();

    if ( !m_linesPerPage )
    {
        wxConstCast(this, ibListMainWindow)->
            m_linesPerPage = GetClientSize().y / GetLineHeight();
    }

    return m_linesPerPage;
}

void ibListMainWindow::GetVisibleLinesRange(size_t *from, size_t *to)
{
    wxASSERT_MSG( InReportView(), wxT("this is for report mode only") );

    if ( m_lineFrom == (size_t)-1 )
    {
        size_t count = GetItemCount();
        if ( count )
        {
            m_lineFrom = GetListCtrl()->GetScrollPos(wxVERTICAL);

            // this may happen if SetScrollbars() hadn't been called yet
            if ( m_lineFrom >= count )
                m_lineFrom = count - 1;

            // we redraw one extra line but this is needed to make the redrawing
            // logic work when there is a fractional number of lines on screen
            m_lineTo = m_lineFrom + m_linesPerPage;
            if ( m_lineTo >= count )
                m_lineTo = count - 1;
        }
        else // empty control
        {
            m_lineFrom = 0;
            m_lineTo = (size_t)-1;
        }
    }

    wxASSERT_MSG( IsEmpty() ||
                  (m_lineFrom <= m_lineTo && m_lineTo < GetItemCount()),
                  wxT("GetVisibleLinesRange() returns incorrect result") );

    if ( from )
        *from = m_lineFrom;
    if ( to )
        *to = m_lineTo;
}

size_t
ibListMainWindow::PrefixFindItem(size_t idParent,
                                 const wxString& prefixOrig) const
{
    // if no items then just return
    if ( idParent == (size_t)-1 )
        return idParent;

    // match is case insensitive as this is more convenient to the user: having
    // to press Shift-letter to go to the item starting with a capital letter
    // would be too bothersome
    wxString prefix = prefixOrig.Lower();

    // determine the starting point: we shouldn't take the current item (this
    // allows to switch between two items starting with the same letter just by
    // pressing it) but we shouldn't jump to the next one if the user is
    // continuing to type as otherwise he might easily skip the item he wanted
    size_t itemid = idParent;
    if ( prefix.length() == 1 )
    {
        itemid += 1;
    }

    // look for the item starting with the given prefix after it
    while ( ( itemid < (size_t)GetItemCount() ) &&
            !GetLine(itemid)->GetText(0).Lower().StartsWith(prefix) )
    {
        itemid += 1;
    }

    // if we haven't found anything...
    if ( !( itemid < (size_t)GetItemCount() ) )
    {
        // ... wrap to the beginning
        itemid = 0;

        // and try all the items (stop when we get to the one we started from)
        while ( ( itemid < (size_t)GetItemCount() ) && itemid != idParent &&
                    !GetLine(itemid)->GetText(0).Lower().StartsWith(prefix) )
        {
            itemid += 1;
        }
        // If we haven't found the item, id will be (size_t)-1, as per
        // documentation
        if ( !( itemid < (size_t)GetItemCount() ) ||
             ( ( itemid == idParent ) &&
               !GetLine(itemid)->GetText(0).Lower().StartsWith(prefix) ) )
        {
            itemid = (size_t)-1;
        }
    }

    return itemid;
}

// -------------------------------------------------------------------------------------
// ibListCtrl
// -------------------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibListCtrl, ibControl);

wxBEGIN_EVENT_TABLE(ibListCtrl,ibListCtrlBase)
  EVT_SIZE(ibListCtrl::OnSize)
  EVT_SCROLLWIN(ibListCtrl::OnScroll)
wxEND_EVENT_TABLE()

void ibListCtrl::Init()
{
    m_mainWin = nullptr;
    m_headerWin = nullptr;
}

ibListCtrl::~ibListCtrl()
{
    // Don't wait until the base class does it because our subwindows expect
    // their parent window to be a wxListCtrl, but this won't be the case any
    // more when we get to the base class dtor (it will be only a wxWindow).
    DestroyChildren();
}

void ibListCtrl::CreateOrDestroyHeaderWindowAsNeeded()
{
    bool needs_header = HasHeader();
    bool has_header = (m_headerWin != nullptr);

    if (needs_header == has_header)
        return;

    if (needs_header)
    {
        // Notice that we must initialize m_headerWin first, and create the
        // real window only later, so that the test in the beginning of the
        // function blocks repeated creation of the header as it could happen
        // before via wxNavigationEnabled::AddChild() -> ToggleWindowStyle() ->
        // SetWindowStyleFlag().
        m_headerWin = new ibListHeaderWindow();
        m_headerWin->Create
                      (
                        this, wxID_ANY, m_mainWin,
                        wxPoint(0,0),
                        wxSize
                        (
                          GetClientSize().x,
                          ibThemeEngine::Get()->GetRenderer()->GetHeaderButtonHeight(this)
                        ),
                        wxTAB_TRAVERSAL
                      );

#if defined( __WXMAC__ )
        static wxFont font( wxOSX_SYSTEM_FONT_SMALL );
        m_headerWin->SetFont( font );
#endif

        GetSizer()->Prepend( m_headerWin, 0, wxGROW );
    }
    else
    {
        GetSizer()->Detach( m_headerWin );

        wxDELETE(m_headerWin);
    }
}

bool ibListCtrl::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxPoint &pos,
                        const wxSize &size,
                        long style,
                        const wxValidator &validator,
                        const wxString &name)
{
    Init();

    // just like in other ports, an assert will fail if the user doesn't give any type style:
    wxASSERT_MSG( (style & wxLC_MASK_TYPE),
                  wxT("wxListCtrl style should have exactly one mode bit set") );

    if ( !ibListCtrlBase::Create( parent, id, pos, size,
                                  style | wxVSCROLL | wxHSCROLL,
                                  validator, name ) )
        return false;

    m_mainWin = new ibListMainWindow(this, wxID_ANY, wxPoint(0, 0), size);

    SetTargetWindow( m_mainWin );

    // We use the cursor keys for moving the selection, not scrolling, so call
    // this method to ensure wxScrollHelperEvtHandler doesn't catch all
    // keyboard events forwarded to us from ibListMainWindow.
    DisableKeyboardScrolling();

    wxBoxSizer *sizer = new wxBoxSizer( wxVERTICAL );
    sizer->Add( m_mainWin, 1, wxGROW );
    SetSizer( sizer );

    CreateOrDestroyHeaderWindowAsNeeded();

    SetInitialSize(size);

    return true;
}

void ibListCtrl::ExtendRulesAndAlternateColour(bool state)
{
    wxCHECK_RET( m_mainWin, "can't be called before creation" );

    wxASSERT_MSG( InReportView(), "can only be called in report mode" );

    m_mainWin->ExtendRulesAndAlternateColour(state);
    m_mainWin->Refresh();
}

wxBorder ibListCtrl::GetDefaultBorder() const
{
    return wxBORDER_THEME;
}

#if defined(__WXMSW__) && !defined(__WXUNIVERSAL__)
WXLRESULT ibListCtrl::MSWWindowProc(WXUINT nMsg,
                                       WXWPARAM wParam,
                                       WXLPARAM lParam)
{
    WXLRESULT rc = ibListCtrlBase::MSWWindowProc(nMsg, wParam, lParam);

    // we need to process arrows ourselves for scrolling
    if ( nMsg == WM_GETDLGCODE )
    {
        rc |= DLGC_WANTARROWS;
    }

    return rc;
}
#endif // __WXMSW__

wxSize ibListCtrl::GetSizeAvailableForScrollTarget(const wxSize& size)
{
    wxSize newsize = size;
    if (m_headerWin)
       newsize.y -= m_headerWin->GetSize().y;

    return newsize;
}

void ibListCtrl::OnScroll(wxScrollWinEvent& event)
{
    // update our idea of which lines are shown when we redraw
    // the window the next time
    m_mainWin->ResetVisibleLinesRange();

    if ( event.GetOrientation() == wxHORIZONTAL && HasHeader() )
    {
        m_headerWin->Refresh();
        m_headerWin->Update();
    }

    // Let the window be scrolled as usual by the default handler.
    event.Skip();
}

bool ibListCtrl::HasCheckBoxes() const
{
    if (!InReportView())
        return false;

    return m_mainWin->HasCheckBoxes();
}

bool ibListCtrl::EnableCheckBoxes(bool enable)
{
    if (!InReportView())
        return false;

    return m_mainWin->EnableCheckBoxes(enable);
}

void ibListCtrl::CheckItem(long item, bool state)
{
    if (InReportView())
        m_mainWin->CheckItem(item, state);
}

bool ibListCtrl::IsItemChecked(long item) const
{
    if (!InReportView())
        return false;

    return m_mainWin->IsItemChecked(item);
}

void ibListCtrl::ShowSortIndicator(int idx, bool ascending)
{
    if ( m_headerWin &&
                (idx != m_headerWin->m_sortCol ||
                 (idx != -1 && ascending != m_headerWin->m_sortAsc)) )
    {
        m_headerWin->m_sortCol = idx;
        m_headerWin->m_sortAsc = ascending;
        m_headerWin->Refresh();
    }
}

int ibListCtrl::GetSortIndicator() const
{
    if ( m_headerWin )
        return m_headerWin->m_sortCol;

    return -1;
}

bool ibListCtrl::IsAscendingSortIndicator() const
{
    if ( m_headerWin )
        return m_headerWin->m_sortAsc;

    return true;
}

void ibListCtrl::SetSingleStyle( long style, bool add )
{
    wxASSERT_MSG( !(style & wxLC_VIRTUAL),
                  wxT("wxLC_VIRTUAL can't be [un]set") );

    long flag = GetWindowStyle();

    if (add)
    {
        if (style & wxLC_MASK_TYPE)
            flag &= ~(wxLC_MASK_TYPE | wxLC_VIRTUAL);
        if (style & wxLC_MASK_ALIGN)
            flag &= ~wxLC_MASK_ALIGN;
        if (style & wxLC_MASK_SORT)
            flag &= ~wxLC_MASK_SORT;
    }

    if (add)
        flag |= style;
    else
        flag &= ~style;

    // some styles can be set without recreating everything (as happens in
    // SetWindowStyleFlag() which calls ibListMainWindow::DeleteEverything())
    if ( !(style & ~(wxLC_HRULES | wxLC_VRULES)) )
    {
        Refresh();
        wxWindow::SetWindowStyleFlag(flag);
    }
    else
    {
        SetWindowStyleFlag( flag );
    }
}

void ibListCtrl::SetWindowStyleFlag( long flag )
{
    // we add wxHSCROLL and wxVSCROLL in ctor unconditionally and it never
    // makes sense to remove them as we'll always add scrollbars anyhow when
    // needed
    flag |= wxHSCROLL | wxVSCROLL;

    const bool wasInReportView = HasFlag(wxLC_REPORT);

    // update the window style first so that the header is created or destroyed
    // corresponding to the new style
    wxWindow::SetWindowStyleFlag( flag );

    if (m_mainWin)
    {
        const bool inReportView = (flag & wxLC_REPORT) != 0;
        if ( inReportView != wasInReportView )
        {
            // we need to notify the main window about this change as it must
            // update its data structures
            m_mainWin->SetReportView(inReportView);
        }

        // m_mainWin->DeleteEverything();  wxMSW doesn't do that

        CreateOrDestroyHeaderWindowAsNeeded();

        GetSizer()->Layout();
    }
}

bool ibListCtrl::GetColumn(int col, wxListItem &item) const
{
    m_mainWin->GetColumn( col, item );
    return true;
}

bool ibListCtrl::SetColumn( int col, const wxListItem& item )
{
    m_mainWin->SetColumn( col, item );
    return true;
}

int ibListCtrl::GetColumnWidth( int col ) const
{
    return m_mainWin->GetColumnWidth( col );
}

bool ibListCtrl::SetColumnWidth( int col, int width )
{
    m_mainWin->SetColumnWidth( col, width );
    return true;
}

// Column ordering functions
int ibListCtrl::GetColumnOrder(int col) const
{
    // TODO: Implement this on generic port
    return col;
}

int ibListCtrl::GetColumnIndexFromOrder(int order) const
{
    // TODO: Implement this on generic port
    return order;
}

wxArrayInt ibListCtrl::GetColumnsOrder() const
{
    // TODO: Implement this on generic port
    return wxArrayInt();
}

bool ibListCtrl::SetColumnsOrder(const wxArrayInt& WXUNUSED(orders))
{
    // TODO: Implement this on generic port
    return false;
}

int ibListCtrl::GetCountPerPage() const
{
  return m_mainWin->GetCountPerPage();  // different from Windows ?
}

bool ibListCtrl::GetItem( wxListItem &info ) const
{
    m_mainWin->GetItem( info );
    return true;
}

bool ibListCtrl::SetItem( wxListItem &info )
{
    m_mainWin->SetItem( info );
    return true;
}

bool ibListCtrl::SetItem( long index, int col, const wxString& label, int imageId )
{
    wxListItem info;
    info.m_text = label;
    info.m_mask = wxLIST_MASK_TEXT;
    info.m_itemId = index;
    info.m_col = col;
    if ( imageId > -1 )
    {
        info.m_image = imageId;
        info.m_mask |= wxLIST_MASK_IMAGE;
    }

    m_mainWin->SetItem(info);
    return true;
}

int ibListCtrl::GetItemState( long item, long stateMask ) const
{
    return m_mainWin->GetItemState( item, stateMask );
}

bool ibListCtrl::SetItemState( long item, long state, long stateMask )
{
    m_mainWin->SetItemState( item, state, stateMask );
    return true;
}

bool
ibListCtrl::SetItemImage( long item, int image, int WXUNUSED(selImage) )
{
    return SetItemColumnImage(item, 0, image);
}

bool
ibListCtrl::SetItemColumnImage( long item, long column, int image )
{
    wxListItem info;
    info.m_image = image;
    info.m_mask = wxLIST_MASK_IMAGE;
    info.m_itemId = item;
    info.m_col = column;
    m_mainWin->SetItem( info );
    return true;
}

wxString ibListCtrl::GetItemText( long item, int col ) const
{
    return m_mainWin->GetItemText(item, col);
}

void ibListCtrl::SetItemText( long item, const wxString& str )
{
    m_mainWin->SetItemText(item, str);
}

wxUIntPtr ibListCtrl::GetItemData( long item ) const
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_DATA;
    info.m_itemId = item;
    m_mainWin->GetItem( info );
    return info.m_data;
}

bool ibListCtrl::SetItemPtrData( long item, wxUIntPtr data )
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_DATA;
    info.m_itemId = item;
    info.m_data = data;
    m_mainWin->SetItem( info );
    return true;
}

wxRect ibListCtrl::GetViewRect() const
{
    return m_mainWin->GetViewRect();
}

bool ibListCtrl::GetItemRect(long item, wxRect& rect, int code) const
{
    return GetSubItemRect(item, wxLIST_GETSUBITEMRECT_WHOLEITEM, rect, code);
}

bool ibListCtrl::GetSubItemRect(long item,
                                       long subItem,
                                       wxRect& rect,
                                       int code) const
{
    if ( !m_mainWin->GetSubItemRect( item, subItem, rect, code ) )
        return false;

    if ( m_mainWin->HasHeader() )
        rect.y += m_headerWin->GetSize().y + 1;

    return true;
}

bool ibListCtrl::GetItemPosition( long item, wxPoint& pos ) const
{
    m_mainWin->GetItemPosition( item, pos );
    return true;
}

bool ibListCtrl::SetItemPosition( long WXUNUSED(item), const wxPoint& WXUNUSED(pos) )
{
    return false;
}

int ibListCtrl::GetItemCount() const
{
    return m_mainWin->GetItemCount();
}

int ibListCtrl::GetColumnCount() const
{
    // wxLC_LIST is special as we want to return 1 for it, for compatibility
    // with the native wxMSW version and not the real number of columns, which
    // is 0. For the other non-wxLC_REPORT modes returning 0 is fine, however,
    // as wxMSW does it too.
    return HasFlag(wxLC_LIST) ? 1 : m_mainWin->GetColumnCount();
}

void ibListCtrl::SetItemSpacing( int spacing, bool isSmall )
{
    m_mainWin->SetItemSpacing( spacing, isSmall );
}

wxSize ibListCtrl::GetItemSpacing() const
{
    const int spacing = m_mainWin->GetItemSpacing(HasFlag(wxLC_SMALL_ICON));

    return wxSize(spacing, spacing);
}

void ibListCtrl::SetItemTextColour( long item, const wxColour &col )
{
    wxListItem info;
    info.m_itemId = item;
    info.SetTextColour( col );
    m_mainWin->SetItem( info );
}

wxColour ibListCtrl::GetItemTextColour( long item ) const
{
    wxListItem info;
    info.m_itemId = item;
    m_mainWin->GetItem( info );
    return info.GetTextColour();
}

void ibListCtrl::SetItemBackgroundColour( long item, const wxColour &col )
{
    wxListItem info;
    info.m_itemId = item;
    info.SetBackgroundColour( col );
    m_mainWin->SetItem( info );
}

wxColour ibListCtrl::GetItemBackgroundColour( long item ) const
{
    wxListItem info;
    info.m_itemId = item;
    m_mainWin->GetItem( info );
    return info.GetBackgroundColour();
}

void ibListCtrl::SetItemFont( long item, const wxFont &f )
{
    wxListItem info;
    info.m_itemId = item;
    info.SetFont( f );
    m_mainWin->SetItem( info );
}

wxFont ibListCtrl::GetItemFont( long item ) const
{
    wxListItem info;
    info.m_itemId = item;
    m_mainWin->GetItem( info );
    return info.GetFont();
}

int ibListCtrl::GetSelectedItemCount() const
{
    return m_mainWin->GetSelectedItemCount();
}

wxColour ibListCtrl::GetTextColour() const
{
    return GetForegroundColour();
}

void ibListCtrl::SetTextColour(const wxColour& col)
{
    SetForegroundColour(col);
}

long ibListCtrl::GetTopItem() const
{
    size_t top;
    m_mainWin->GetVisibleLinesRange(&top, nullptr);
    return (long)top;
}

long ibListCtrl::GetNextItem( long item, int geom, int state ) const
{
    return m_mainWin->GetNextItem( item, geom, state );
}

void ibListCtrl::DoUpdateImages(int which )
{
    m_mainWin->SetImages( GetImages(which), which );
}

bool ibListCtrl::Arrange( int WXUNUSED(flag) )
{
    return 0;
}

bool ibListCtrl::DeleteItem( long item )
{
    m_mainWin->DeleteItem( item );
    return true;
}

bool ibListCtrl::DeleteAllItems()
{
    m_mainWin->DeleteAllItems();
    return true;
}

bool ibListCtrl::DeleteAllColumns()
{
    size_t count = m_mainWin->m_columns.size();
    for ( size_t n = 0; n < count; n++ )
        DeleteColumn( 0 );
    return true;
}

void ibListCtrl::ClearAll()
{
    m_mainWin->DeleteEverything();
}

bool ibListCtrl::DeleteColumn( int col )
{
    m_mainWin->DeleteColumn( col );

    // if we don't have the header any longer, we need to relayout the window
    // if ( !GetColumnCount() )


    // Ensure that the non-existent columns are really removed from display.
    Refresh();

    return true;
}

ibTextCtrl *ibListCtrl::EditLabel(long item,
                                         wxClassInfo* textControlClass)
{
    return m_mainWin->EditLabel( item, textControlClass );
}

bool ibListCtrl::EndEditLabel(bool cancel)
{
    return m_mainWin->EndEditLabel(cancel);
}

ibTextCtrl *ibListCtrl::GetEditControl() const
{
    return m_mainWin->GetEditControl();
}

bool ibListCtrl::EnsureVisible( long item )
{
    m_mainWin->EnsureVisible( item );
    return true;
}

long ibListCtrl::FindItem( long start, const wxString& str, bool partial )
{
    return m_mainWin->FindItem( start, str, partial );
}

long ibListCtrl::FindItem( long start, wxUIntPtr data )
{
    return m_mainWin->FindItem( start, data );
}

long ibListCtrl::FindItem( long WXUNUSED(start), const wxPoint& pt,
                           int WXUNUSED(direction))
{
    return m_mainWin->FindItem( pt );
}

long ibListCtrl::HitTest(const wxPoint& point, int& flags, long *col) const
{
    if ( col )
    {
        *col = -1;
        if ( InReportView() )
        {
            const wxPoint unscrolled = CalcUnscrolledPosition( point );

            for ( int c = 0, wsum = 0, cols = GetColumnCount();
                  c < cols;
                  ++c )
            {
                wsum += GetColumnWidth(c);
                if ( wsum > unscrolled.x )
                {
                    *col = c;
                    break;
                }
            }
        }
    }

    return m_mainWin->HitTest( (int)point.x, (int)point.y, flags );
}

long ibListCtrl::InsertItem( wxListItem& info )
{
    m_mainWin->InsertItem( info );
    return info.m_itemId;
}

long ibListCtrl::InsertItem( long index, const wxString &label )
{
    wxListItem info;
    info.m_text = label;
    info.m_mask = wxLIST_MASK_TEXT;
    info.m_itemId = index;
    return InsertItem( info );
}

long ibListCtrl::InsertItem( long index, int imageIndex )
{
    wxListItem info;
    info.m_mask = wxLIST_MASK_IMAGE;
    info.m_image = imageIndex;
    info.m_itemId = index;
    return InsertItem( info );
}

long ibListCtrl::InsertItem( long index, const wxString &label, int imageIndex )
{
    wxListItem info;
    info.m_text = label;
    info.m_image = imageIndex;
    info.m_mask = wxLIST_MASK_TEXT;
    if (imageIndex > -1)
        info.m_mask |= wxLIST_MASK_IMAGE;
    info.m_itemId = index;
    return InsertItem( info );
}

long ibListCtrl::DoInsertColumn( long col, const wxListItem &item )
{
    wxCHECK_MSG( InReportView(), -1, wxT("can't add column in non report mode") );

    long idx = m_mainWin->InsertColumn( col, item );

    // NOTE: if wxLC_NO_HEADER was given, then we are in report view mode but
    //       still have m_headerWin==nullptr
    if (m_headerWin)
        m_headerWin->Refresh();

    return idx;
}

bool ibListCtrl::ScrollList( int dx, int dy )
{
    return m_mainWin->ScrollList(dx, dy);
}

// Sort items.
// fn is a function which takes 3 long arguments: item1, item2, data.
// item1 is the long data associated with a first item (NOT the index).
// item2 is the long data associated with a second item (NOT the index).
// data is the same value as passed to SortItems.
// The return value is a negative number if the first item should precede the second
// item, a positive number of the second item should precede the first,
// or zero if the two items are equivalent.
// data is arbitrary data to be passed to the sort function.

bool ibListCtrl::SortItems( wxListCtrlCompare fn, wxIntPtr data )
{
    m_mainWin->SortItems( fn, data );
    return true;
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------

void ibListCtrl::OnSize(wxSizeEvent& event)
{
    if (!m_mainWin) return;

    // We need to override OnSize so that our scrolled
    // window a) does call Layout() to use sizers for
    // positioning the controls but b) does not query
    // the sizer for their size and use that for setting
    // the scrollable area as set that ourselves by
    // calling SetScrollbar() further down.

    Layout();

    m_mainWin->RecalculatePositionsAndRefresh();

    AdjustScrollbars();

    event.Skip();
}

void ibListCtrl::OnInternalIdle()
{
    wxWindow::OnInternalIdle();

    if (m_mainWin->m_dirty)
        m_mainWin->RecalculatePositionsAndRefresh();
}

// ----------------------------------------------------------------------------
// font/colours
// ----------------------------------------------------------------------------

bool ibListCtrl::SetBackgroundColour( const wxColour &colour )
{
    if ( !wxWindow::SetBackgroundColour( colour ) )
        return false;

    if (m_mainWin)
    {
        m_mainWin->SetBackgroundColour( colour );
        m_mainWin->m_dirty = true;
    }

    return true;
}

bool ibListCtrl::SetForegroundColour( const wxColour &colour )
{
    if ( !wxWindow::SetForegroundColour( colour ) )
        return false;

    if (m_mainWin)
    {
        m_mainWin->SetForegroundColour( colour );
        m_mainWin->m_dirty = true;
    }

    return true;
}

bool ibListCtrl::SetFont( const wxFont &font )
{
    if (!BaseType::SetFont(font))
        return false;

    if (m_mainWin)
    {
        m_mainWin->SetFont( font );
        m_mainWin->m_dirty = true;
    }

    if (m_headerWin)
    {
        m_headerWin->SetFont( font );
        // CalculateAndSetHeaderHeight();
    }

    Refresh();

    return true;
}

// static
wxVisualAttributes
ibListCtrl::GetClassDefaultAttributes(wxWindowVariant variant)
{
#if _USE_VISATTR
    // Use the same color scheme as wxListBox
    return wxListBox::GetClassDefaultAttributes(variant);
#else
    wxUnusedVar(variant);
    wxVisualAttributes attr;
    attr.colFg = wxTHEME_COLOUR(CONTROL_TEXT); // SEAM: theme, as ibTreeCtrl's
    attr.colBg = wxTHEME_COLOUR(WINDOW); // SEAM: theme, as ibTreeCtrl's
    attr.font  = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    return attr;
#endif
}

// ----------------------------------------------------------------------------
// methods forwarded to m_mainWin
// ----------------------------------------------------------------------------

#if wxUSE_DRAG_AND_DROP

void ibListCtrl::SetDropTarget( wxDropTarget *dropTarget )
{
    m_mainWin->SetDropTarget( dropTarget );
}

wxDropTarget *ibListCtrl::GetDropTarget() const
{
    return m_mainWin->GetDropTarget();
}

#endif

bool ibListCtrl::SetCursor( const wxCursor &cursor )
{
    return m_mainWin ? m_mainWin->wxWindow::SetCursor(cursor) : false;
}

wxColour ibListCtrl::GetBackgroundColour() const
{
    return m_mainWin ? m_mainWin->GetBackgroundColour() : wxColour();
}

wxColour ibListCtrl::GetForegroundColour() const
{
    return m_mainWin ? m_mainWin->GetForegroundColour() : wxColour();
}

wxSize ibListCtrl::DoGetBestClientSize() const
{
    // The base class version can compute the best size in report view only.
    wxSize sizeBest = ibListCtrlBase::DoGetBestClientSize();

    if ( !InReportView() )
    {
        // Ensure that our minimal width is at least big enough to show all our
        // items. This is important for wxListbook to size itself correctly.

        // Remember the offset of the first item: this corresponds to the
        // margins around the item so we will add it to the minimal size below
        // to ensure that we have equal margins on all sides.
        wxPoint ofs;

        // We can iterate over all items as there shouldn't be too many of them
        // in non-report view. If it ever becomes a problem, we could examine
        // just the first few items probably, the determination of the best
        // size is less important if we will need scrollbars anyhow.
        for ( int n = 0; n < GetItemCount(); n++ )
        {
            const wxRect itemRect = m_mainWin->GetLineRect(n);
            if ( !n )
            {
                // Remember the position of the first item as all the rest are
                // offset by at least this number of pixels too.
                ofs = itemRect.GetPosition();
            }

            sizeBest.IncTo(itemRect.GetSize());
        }

        sizeBest.IncBy(2*ofs);


        // If we have the scrollbars we need to account for them too. And to
        // make sure the scrollbars status is up to date we need to call this
        // function to set them.
        m_mainWin->RecalculatePositions();

        // Unfortunately we can't use wxWindow::HasScrollbar() here as we need
        // to use m_mainWin client/virtual size for determination of whether we
        // use scrollbars and not the size of this window itself. Maybe that
        // function should be extended to work correctly in the case when our
        // scrollbars manage a different window from this one but currently it
        // doesn't work.
        const wxSize sizeClient = m_mainWin->GetClientSize();
        const wxSize sizeVirt = m_mainWin->GetVirtualSize();

        if ( sizeVirt.x > sizeClient.x /* HasScrollbar(wxHORIZONTAL) */ )
            sizeBest.y += GetScrollbarSize(wxHORIZONTAL);

        if ( sizeVirt.y > sizeClient.y /* HasScrollbar(wxVERTICAL) */ )
            sizeBest.x += GetScrollbarSize(wxVERTICAL);
    }

    return sizeBest;
}

// ----------------------------------------------------------------------------
// virtual list control support
// ----------------------------------------------------------------------------

void ibListCtrl::SetItemCount(long count)
{
    wxASSERT_MSG( IsVirtual(), wxT("this is for virtual controls only") );

    m_mainWin->SetItemCount(count);
}

void ibListCtrl::RefreshItem(long item)
{
    m_mainWin->RefreshLine(item);
}

void ibListCtrl::RefreshItems(long itemFrom, long itemTo)
{
    m_mainWin->RefreshLines(itemFrom, itemTo);
}

void ibListCtrl::EnableBellOnNoMatch( bool on )
{
    m_mainWin->EnableBellOnNoMatch(on);
}

// Generic wxListCtrl is more or less a container for two other
// windows which drawings are done upon. These are namely
// 'm_headerWin' and 'm_mainWin'.
// Here we override 'virtual wxWindow::Refresh()' to mimic the
// behaviour wxListCtrl has under wxMSW.
//
void ibListCtrl::Refresh(bool eraseBackground, const wxRect *rect)
{
    if (!rect)
    {
        // The easy case, no rectangle specified.
        if (m_headerWin)
            m_headerWin->Refresh(eraseBackground);

        if (m_mainWin)
            m_mainWin->Refresh(eraseBackground);
    }
    else
    {
        // Refresh the header window
        if (m_headerWin)
        {
            wxRect rectHeader = m_headerWin->GetRect();
            rectHeader.Intersect(*rect);
            if (rectHeader.GetWidth() && rectHeader.GetHeight())
            {
                int x, y;
                m_headerWin->GetPosition(&x, &y);
                rectHeader.Offset(-x, -y);
                m_headerWin->Refresh(eraseBackground, &rectHeader);
            }
        }

        // Refresh the main window
        if (m_mainWin)
        {
            wxRect rectMain = m_mainWin->GetRect();
            rectMain.Intersect(*rect);
            if (rectMain.GetWidth() && rectMain.GetHeight())
            {
                int x, y;
                m_mainWin->GetPosition(&x, &y);
                rectMain.Offset(-x, -y);
                m_mainWin->Refresh(eraseBackground, &rectMain);
            }
        }
    }
}

void ibListCtrl::Update()
{
    if ( m_mainWin )
    {
        if ( m_mainWin->m_dirty )
            m_mainWin->RecalculatePositionsAndRefresh();

        m_mainWin->Update();
    }

    if ( m_headerWin )
        m_headerWin->Update();
}


// ----------------------------------------------------------------------------
// ibListCtrlBase: FORKED from src/common/listctrlcmn.cpp
// ibListCtrlBase implementation
// ----------------------------------------------------------------------------

long
ibListCtrlBase::AppendColumn(const wxString& heading,
                             wxListColumnFormat format,
                             int width)
{
    return InsertColumn(GetColumnCount(), heading, format, width);
}

long
ibListCtrlBase::InsertColumn(long col,
                             const wxString& heading,
                             int format,
                             int width)
{
    wxListItem item;
    item.m_mask = wxLIST_MASK_TEXT | wxLIST_MASK_FORMAT;
    item.m_text = heading;
    if ( width >= 0
            || width == wxLIST_AUTOSIZE
                || width == wxLIST_AUTOSIZE_USEHEADER )
    {
        item.m_mask |= wxLIST_MASK_WIDTH;
        item.m_width = width;
    }
    item.m_format = format;

    return InsertColumn(col, item);
}

long ibListCtrlBase::InsertColumn(long col, const wxListItem& info)
{
    long rc = DoInsertColumn(col, info);
    if ( rc != -1 )
    {
        // As our best size calculation depends on the column headers,
        // invalidate the previously cached best size when a column is added.
        InvalidateBestSize();
    }

    return rc;
}

wxSize ibListCtrlBase::DoGetBestClientSize() const
{
    // There is no obvious way to determine the best size in icon and list
    // modes so just don't do it for now.
    if ( !InReportView() )
        return ibControl::DoGetBestClientSize();

    int totalWidth;
    wxInfoDC dc(const_cast<ibListCtrlBase*>(this));

    // In report mode, we use only the column headers, not items, to determine
    // the best width. The reason for this is that it's easier (we can't just
    // iterate over all items, especially not in a virtual control, so we'd
    // have to do something relatively complicated such as checking the size of
    // some items in the beginning and the end only) and also because the
    // columns are usually static while the list contents is dynamic so it
    // usually doesn't make much sense to adjust the control size to it anyhow.
    // And finally, scrollbars can always be used with the items while the
    // headers are just truncated if there is not enough place for them.
    const int columns = GetColumnCount();
    if ( HasFlag(wxLC_NO_HEADER) || !columns )
    {
        // Use some arbitrary width.
        totalWidth = 50*dc.GetCharWidth();
    }
    else // We do have columns, use them to determine the best width.
    {
        totalWidth = 0;
        for ( int col = 0; col < columns; col++ )
        {
            totalWidth += GetColumnWidth(col);
        }
    }

    // Use some arbitrary height, there is no good way to determine it.
    return wxSize(totalWidth, 10*dc.GetCharHeight());
}

void ibListCtrlBase::SetAlternateRowColour(const wxColour& colour)
{
    wxASSERT(HasFlag(wxLC_VIRTUAL));
    m_alternateRowColour.SetBackgroundColour(colour);
}

void ibListCtrlBase::EnableAlternateRowColours(bool enable)
{
    if ( enable )
    {
        // This code is copied from wxDataViewMainWindow::OnPaint()

        // Determine the alternate rows colour automatically from the
        // background colour.
        const wxColour bgColour = GetBackgroundColour();

        // Depending on the background, alternate row colour should be a bit
        // darker or brighter.
        int alpha = bgColour.GetRGB() > 0x808080 ? 97 : 110;
        SetAlternateRowColour(bgColour.ChangeLightness(alpha));
    }
    else // Disable striping by setting invalid alternative colour.
    {
        SetAlternateRowColour(wxColour());
    }
}

wxItemAttr *ibListCtrlBase::OnGetItemAttr(long item) const
{
    return (m_alternateRowColour.GetBackgroundColour().IsOk() && (item % 2))
        ? wxConstCast(&m_alternateRowColour, wxItemAttr)
        : nullptr; // no attributes by default
}

wxString ibListCtrlBase::OnGetItemText(long WXUNUSED(item), long WXUNUSED(col)) const
{
    // this is a pure virtual function, in fact - which is not really pure
    // because the controls which are not virtual don't need to implement it
    wxFAIL_MSG("wxListCtrl::OnGetItemText not supposed to be called");

    return wxEmptyString;
}

bool ibListCtrlBase::OnGetItemIsChecked(long WXUNUSED(item)) const
{
    // this is a pure virtual function, in fact - which is not really pure
    // because the controls which are not virtual don't need to implement it
    wxFAIL_MSG("wxListCtrl::OnGetItemIsChecked not supposed to be called");

    return false;
}

int ibListCtrlBase::OnGetItemImage(long WXUNUSED(item)) const
{
    wxCHECK_MSG(!GetImageList(wxIMAGE_LIST_SMALL),
                -1,
                "List control has an image list, OnGetItemImage or OnGetItemColumnImage should be overridden.");
    return -1;
}

int ibListCtrlBase::OnGetItemColumnImage(long item, long column) const
{
    if ( !column )
        return OnGetItemImage(item);

    return -1;
}

wxItemAttr* ibListCtrlBase::OnGetItemColumnAttr(long item, long WXUNUSED(column)) const
{
    return OnGetItemAttr(item);
}

// ----------------------------------------------------------------------------
// Images support
// ----------------------------------------------------------------------------

void ibListCtrlBase::SetNormalImages(const wxVector<wxBitmapBundle>& images)
{
    m_imagesNormal.SetImages(images);

    DoUpdateImages(wxIMAGE_LIST_NORMAL);
}

void ibListCtrlBase::SetSmallImages(const wxVector<wxBitmapBundle>& images)
{
    m_imagesSmall.SetImages(images);

    DoUpdateImages(wxIMAGE_LIST_SMALL);
}

wxWithImages* ibListCtrlBase::GetImages(int which)
{
    if ( which == wxIMAGE_LIST_NORMAL )
    {
        return &m_imagesNormal;
    }
    else if ( which == wxIMAGE_LIST_SMALL )
    {
        return &m_imagesSmall;
    }
    else if ( which == wxIMAGE_LIST_STATE )
    {
        return &m_imagesState;
    }
    return nullptr;
}

const wxWithImages* ibListCtrlBase::GetImages(int which) const
{
    return const_cast<ibListCtrlBase*>(this)->GetImages(which);
}

wxImageList* ibListCtrlBase::GetUpdatedImageList(int which)
{
    wxWithImages* const images = GetImages(which);
    return images ? images->GetUpdatedImageListFor(this) : nullptr;
}

wxImageList *ibListCtrlBase::GetImageList(int which) const
{
    const wxWithImages* const images = GetImages(which);
    return images ? images->GetImageList() : nullptr;
}

void ibListCtrlBase::SetImageList(wxImageList *imageList, int which)
{
    if ( which == wxIMAGE_LIST_NORMAL )
    {
        m_imagesNormal.SetImageList(imageList);
    }
    else if ( which == wxIMAGE_LIST_SMALL )
    {
        m_imagesSmall.SetImageList(imageList);
    }
    else if ( which == wxIMAGE_LIST_STATE )
    {
        m_imagesState.SetImageList(imageList);
    }
    else
    {
        wxFAIL_MSG("unknown image list");
        return;
    }

    // Actually update the images shown in the control.
    DoUpdateImages(which);
}

void ibListCtrlBase::AssignImageList(wxImageList *imageList, int which)
{
    SetImageList(imageList, which);
    if ( which == wxIMAGE_LIST_NORMAL )
        m_imagesNormal.TakeOwnership();
    else if ( which == wxIMAGE_LIST_SMALL )
        m_imagesSmall.TakeOwnership();
    else if ( which == wxIMAGE_LIST_STATE )
        m_imagesState.TakeOwnership();
}

#endif // wxUSE_LISTCTRL
