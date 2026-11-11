// FORKED from wxWidgets' AUI (include/wx/aui/auibook.h of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

//////////////////////////////////////////////////////////////////////////////
// Name:        wx/aui/auibook.h
// Purpose:     wxaui: wx advanced user interface - notebook
// Author:      Benjamin I. Williams
// Modified by: Jens Lody
// Created:     2006-06-28
// Copyright:   (C) Copyright 2006, Kirix Corporation, All Rights Reserved.
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////



#ifndef _DESKGUI_UNIV_AUI_AUIBOOK_H_
#define _DESKGUI_UNIV_AUI_AUIBOOK_H_

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/defs.h"

#include "deskgui/deskgui.h"


#include "deskgui/univ/aui/framemanager.h"
#include "deskgui/univ/ctrl/control.h"
#include "wx/compositebookctrl.h"

#include <vector>

class ibAuiBookSerializer;
class ibAuiBookDeserializer;

class ibAuiNotebook;
class ibAuiTabFrame;


enum ibAuiNotebookOption
{
    ibAUI_NB_TOP                 = 1 << 0,
    ibAUI_NB_LEFT                = 1 << 1,  // not implemented yet
    ibAUI_NB_RIGHT               = 1 << 2,  // not implemented yet
    ibAUI_NB_BOTTOM              = 1 << 3,
    ibAUI_NB_TAB_SPLIT           = 1 << 4,
    ibAUI_NB_TAB_MOVE            = 1 << 5,
    ibAUI_NB_TAB_EXTERNAL_MOVE   = 1 << 6,
    ibAUI_NB_TAB_FIXED_WIDTH     = 1 << 7,
    ibAUI_NB_SCROLL_BUTTONS      = 1 << 8,
    ibAUI_NB_WINDOWLIST_BUTTON   = 1 << 9,
    ibAUI_NB_CLOSE_BUTTON        = 1 << 10,
    ibAUI_NB_CLOSE_ON_ACTIVE_TAB = 1 << 11,
    ibAUI_NB_CLOSE_ON_ALL_TABS   = 1 << 12,
    ibAUI_NB_MIDDLE_CLICK_CLOSE  = 1 << 13,
    ibAUI_NB_MULTILINE           = 1 << 14,
    ibAUI_NB_PIN_ON_ACTIVE_TAB   = 1 << 15,
    ibAUI_NB_UNPIN_ON_ALL_PINNED = 1 << 16,

    ibAUI_NB_DEFAULT_STYLE = ibAUI_NB_TOP |
                             ibAUI_NB_TAB_SPLIT |
                             ibAUI_NB_TAB_MOVE |
                             ibAUI_NB_SCROLL_BUTTONS |
                             ibAUI_NB_CLOSE_ON_ACTIVE_TAB |
                             ibAUI_NB_MIDDLE_CLICK_CLOSE
};

wxALLOW_COMBINING_ENUMS(ibAuiNotebookOption, wxBorder)



// aui notebook event class

class DESKGUI_API ibAuiNotebookEvent : public wxBookCtrlEvent
{
public:
    ibAuiNotebookEvent(wxEventType commandType = wxEVT_NULL,
                       int winId = 0)
          : wxBookCtrlEvent(commandType, winId)
    {
        m_dragSource = nullptr;
    }
    wxNODISCARD wxEvent *Clone() const override { return new ibAuiNotebookEvent(*this); }

    void SetDragSource(ibAuiNotebook* s) { m_dragSource = s; }
    ibAuiNotebook* GetDragSource() const { return m_dragSource; }

private:
    ibAuiNotebook* m_dragSource;

#ifndef SWIG
private:
    wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN_DEF_COPY(ibAuiNotebookEvent);
#endif
};


class DESKGUI_API ibAuiTabContainerButton
{
public:
    ibAuiTabContainerButton() = default;

    ibAuiTabContainerButton(
            int id_,
            int location_,
            ibAuiPaneButtonState curState_ = ibAUI_BUTTON_STATE_NORMAL
        ) : id(id_), curState(curState_), location(location_)
    {
    }

    int id = 0;               // button's id
    int curState = 0;         // current state (normal, hover, pressed, etc.)
    int location = 0;         // buttons location (wxLEFT, wxRIGHT, or wxCENTER)
    wxBitmapBundle bitmap;    // button's hover bitmap
    wxBitmapBundle disBitmap; // button's disabled bitmap
    wxRect rect;          // button's hit rectangle
};


// Possible tab states.
enum class ibAuiTabKind
{
    Normal, // Can be closed and dragged by user.
    Pinned, // Can be closed but can't be dragged. Can be unpinned by user.
    Locked  // Can't be closed, dragged nor unlocked by user.
};


class DESKGUI_API ibAuiNotebookPage
{
public:
    wxWindow* window = nullptr; // page's associated window
    wxString caption;     // caption displayed on the tab
    wxString tooltip;     // tooltip displayed when hovering over tab title
    wxBitmapBundle bitmap;// tab's bitmap
    ibAuiTabKind kind = ibAuiTabKind::Normal; // tab's kind

    wxRect rect;          // tab's hit rectangle (only used internally)
    bool active = false;  // true if the page is currently active

    // These fields are internal, don't use them.
    bool hover = false;   // true if mouse hovering over tab
    bool rowEnd = false;  // true if the tab is the last in the row

    // This vector contains per-page buttons, i.e. "close" and, optionally,
    // "pin" buttons. It can be empty if none are used.
    std::vector<ibAuiTabContainerButton> buttons;
};


// These legacy classes can't be just typedefs as they can be (and are)
// forward-declared in the existing code.
class ibAuiNotebookPageArray : public wxBaseArray<ibAuiNotebookPage>
{
public:
    using wxBaseArray<ibAuiNotebookPage>::wxBaseArray;
};

class ibAuiTabContainerButtonArray : public wxBaseArray<ibAuiTabContainerButton>
{
public:
    using wxBaseArray<ibAuiTabContainerButton>::wxBaseArray;
};


class DESKGUI_API ibAuiTabContainer
{
public:

    ibAuiTabContainer();
    virtual ~ibAuiTabContainer();

    void SetFlags(unsigned int flags);
    unsigned int GetFlags() const { return m_flags; }
    bool IsFlagSet(unsigned int flag) const { return (m_flags & flag) != 0; }

    bool AddPage(const ibAuiNotebookPage& info);
    bool InsertPage(const ibAuiNotebookPage& info, size_t idx);
    bool MovePage(wxWindow* page, size_t newIdx);
    bool MovePage(size_t oldIdx, size_t newIdx);
    bool RemovePage(wxWindow* page);
    void RemovePageAt(size_t idx);
    bool SetActivePage(const wxWindow* page);
    bool SetActivePage(size_t page);
    void SetNoneActive();
    int GetActivePage() const;

    // Struct containing the result of a tab hit test.
    struct HitTestResult
    {
        HitTestResult() = default;

        HitTestResult(wxWindow* window_, int pos_)
            : window(window_), pos(pos_)
        {
        }

        // Check if the result is valid.
        explicit operator bool() const { return window != nullptr; }

        // The window at the given position or null if none.
        wxWindow* window = nullptr;

        // The position of the tab in the tab control.
        int pos = wxNOT_FOUND;
    };

    // Flags allowing to customize the behaviour of TabHitTest().
    enum HitTestFlags
    {
        HitTest_Default = 0,
        HitTest_AllowAfterTab = 1
    };

    HitTestResult TabHitTest(const wxPoint& pt, int flags = HitTest_Default) const;

    ibAuiTabContainerButton* ButtonHitTest(const wxPoint& pt) const;
    wxWindow* GetWindowFromIdx(size_t idx) const;
    int GetIdxFromWindow(const wxWindow* page) const;
    size_t GetPageCount() const;
    ibAuiNotebookPage& GetPage(size_t idx);
    const ibAuiNotebookPage& GetPage(size_t idx) const;
    const ibAuiNotebookPageArray& GetPages() const;
    void SetNormalFont(const wxFont& normalFont);
    void SetSelectedFont(const wxFont& selectedFont);
    void SetMeasuringFont(const wxFont& measuringFont);
    void DoShowHide();
    void SetRect(const wxRect& rect, wxWindow* wnd = nullptr);
    void SetRowHeight(int rowHeight);

    // The height of a row the tallest of the tabs fits in, their pictures of requiredBmpSize if it is given (SEAM vs
    // wx: the tab art's, as the measures below)
    int GetBestTabCtrlSize(wxWindow* wnd, const wxSize& requiredBmpSize) const;

    void RemoveButton(int id);
    void AddButton(int id,
                   int location,
                   const wxBitmapBundle& normalBitmap = wxBitmapBundle(),
                   const wxBitmapBundle& disabledBitmap = wxBitmapBundle());

    size_t GetTabOffset() const;
    void SetTabOffset(size_t offset);

    // Is the tab visible?
    bool IsTabVisible(int tabPage, int tabOffset, wxReadOnlyDC* dc, wxWindow* wnd);

    // Make the tab visible if it wasn't already
    void MakeTabVisible(int tabPage, wxWindow* win);

    // Internal, don't use.
    void RemoveAll();

    // Backwards compatible variants of internal functions, which shouldn't be
    // used anyhow.
    bool AddPage(wxWindow* page, ibAuiNotebookPage info)
    {
        info.window = page;
        return AddPage(info);
    }

    bool InsertPage(wxWindow* page, ibAuiNotebookPage info, size_t idx)
    {
        info.window = page;
        return InsertPage(info, idx);
    }

    bool ButtonHitTest(int x, int y, ibAuiTabContainerButton** hit) const
    {
        auto* const button = ButtonHitTest(wxPoint(x, y));
        if ( hit )
            *hit = button;

        return button != nullptr;
    }

    bool TabHitTest(int x, int y, wxWindow** hit) const
    {
        auto const res = TabHitTest(wxPoint(x, y));
        if ( hit )
            *hit = res.window;

        return res.window != nullptr;
    }

    // Internal functions only, don't use.

    // Layout tabs in ibAUI_NB_MULTILINE case using either the width of the
    // given rectangle or the current width and return the extra height needed
    // for the additional rows.
    //
    // This function has an important side effect of updating rowEnd for all
    // pages -- which defines the layout.
    int LayoutMultiLineTabs(const wxRect& rect, wxWindow* wnd);
    int LayoutMultiLineTabs(wxWindow* wnd)
    {
        return LayoutMultiLineTabs(m_rect, wnd);
    }

    // Get the index of the first tab of the given kind or of a different kind,
    // returning GetPageCount() if there is no such tabs.
    int GetFirstTabOfKind(ibAuiTabKind kind) const;
    int GetFirstTabNotOfKind(ibAuiTabKind kind) const;

protected:

    virtual void Render(wxDC* dc, wxWindow* wnd);

protected:

    // the tabs' labels: a tab's, the selected one's, the one the tabs are measured in
    wxFont m_normalFont;
    wxFont m_selectedFont;
    wxFont m_measuringFont;

    // a tab's width when they are all one (ibAUI_NB_TAB_FIXED_WIDTH)
    int m_fixedTabWidth;

    // Contains pages in the display order.
    ibAuiNotebookPageArray m_pages;

    // This vector contains container-level buttons, e.g. left/right scroll
    // buttons, close button if it's not per-tab, window list button etc.
    std::vector<ibAuiTabContainerButton> m_buttons;

    wxRect m_rect;
    size_t m_tabOffset;
    unsigned int m_flags;

private:
    // Return the width that can be used for the tabs, i.e. without the space
    // reserved for the buttons.
    int GetAvailableForTabs(const wxRect& rect, wxWindow* wnd);

    // SEAM vs wx: the tab art's measures and drawing (theme/luna_tabart until 2026-11-11) - the row lays the tabs and its
    // buttons out, the theme draws them (ibRenderer::DrawTabStrip, DrawStripTab, DrawStripButton)
    void SetSizingInfo(const wxSize& tabCtrlSize, size_t tabCount, wxWindow* wnd);

    wxSize GetTabSize(wxReadOnlyDC& dc,
                      wxWindow* wnd,
                      const wxString& caption,
                      const wxBitmapBundle& bitmap,
                      int closeButtonState,
                      int* xExtent = nullptr) const;

    wxSize GetPageTabSize(wxReadOnlyDC& dc,
                          wxWindow* wnd,
                          const ibAuiNotebookPage& page,
                          int* xExtent = nullptr) const;

    // a tab in the rect of the row left to it, its rect and its close button's kept for the clicks: the width it takes
    int DrawPageTab(wxDC& dc, wxWindow* wnd, ibAuiNotebookPage& page, const wxRect& rect);

    void DrawButton(wxDC& dc,
                    wxWindow* wnd,
                    const wxRect& inRect,
                    int bitmapId,
                    int buttonState,
                    int orientation,
                    wxRect* outRect);

    // Render the buttons: part of Render(), returns the extent of the buttons
    // on the left and right side.
    void RenderButtons(wxDC& dc, wxWindow* wnd,
                       int& left_buttons_width, int& right_buttons_width);

    // Update the state of the page buttons depending on its state and flags.
    //
    // If forceActive is true, the page is always considered as active, see the
    // comment in LayoutMultiLineTabs() for the reason why this is needed.
    void UpdateButtonsState(ibAuiNotebookPage& page, bool forceActive = false);

    int m_tabRowHeight;
};



// This class is internal and can only be used outside of the library as a
// pointer to an opaque object, i.e. "ibAuiTabCtrl*" returned by ibAuiNotebook
// functions can be compared with each other and passed to ibAuiNotebook
// functions taking them, but not otherwise.

// SEAM vs wx: a control of the theme's (ibControl) - a native wxControl there
class DESKGUI_API ibAuiTabCtrl : public ibControl,
                                     public ibAuiTabContainer
{
public:
    // This constructor is only used internally by the library, applications
    // never create objects of this type.
    ibAuiTabCtrl(ibAuiNotebook* parent, wxWindowID id);

    ~ibAuiTabCtrl();

    bool IsDragging() const { return m_isDragging; }

    void SetRect(const wxRect& rect) { ibAuiTabContainer::SetRect(rect, this); }

    // Internal helpers.
    void DoShowTab(int idx);
    void DoUpdateActive();

    // Internal function taking the total tab frame area and setting the size
    // of the window to its sub-rectangle corresponding to tabs orientation.
    //
    // Also updates rowEnd for all pages in m_pages when using multiple rows.
    void DoApplyRect(const wxRect& rect, int tabCtrlHeight);

    // Another internal helper: return the hint rectangle corresponding to this
    // tab control in screen coordinates.
    wxRect GetHintScreenRect() const;

    // Get parent notebook (always valid).
    ibAuiNotebook* GetBook() const;

    // The tabs as a menu under the tab row, where the mouse is: the index of the one picked, or wxNOT_FOUND (SEAM vs
    // wx: the art's - a wxMenu; the menu is the theme's, ibMenu)
    int ShowDropDown();

protected:
    // choose the default border for this window
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    void OnPaint(wxPaintEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnLeftDown(wxMouseEvent& evt);
    void OnLeftDClick(wxMouseEvent& evt);
    void OnLeftUp(wxMouseEvent& evt);
    void OnMiddleDown(wxMouseEvent& evt);
    void OnMiddleUp(wxMouseEvent& evt);
    void OnRightDown(wxMouseEvent& evt);
    void OnRightUp(wxMouseEvent& evt);
    void OnMotion(wxMouseEvent& evt);
    void OnLeaveWindow(wxMouseEvent& evt);
    void OnSetFocus(wxFocusEvent& event);
    void OnKillFocus(wxFocusEvent& event);
    void OnChar(wxKeyEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& evt);

protected:

    wxPoint m_clickPt = wxDefaultPosition;
    wxWindow* m_clickTab = nullptr;
    bool m_isDragging = false;

    void SetHoverTab(wxWindow* wnd);

private:
    // Reset dragging-related fields above to their initial values.
    void DoEndDragging();

    // Find button with the given state, which is assumed to be set on one
    // button at most, may return nullptr if none.
    ibAuiTabContainerButton* FindButtonIn(ibAuiPaneButtonState state) const;

    ibAuiTabContainerButton* FindPressedButton() const
    {
        return FindButtonIn(ibAUI_BUTTON_STATE_PRESSED);
    }

    ibAuiTabContainerButton* FindHoverButton() const
    {
        return FindButtonIn(ibAUI_BUTTON_STATE_HOVER);
    }

    // Switch on or off the given button state for the given button and refresh
    // the window if the state has actually changed.
    //
    // Return true if the state was changed.
    bool UpdateButtonStateAndRefresh(ibAuiTabContainerButton& button,
                                     ibAuiPaneButtonState state,
                                     bool on);

    // More readable helpers for button state changes.
    bool
    SetButtonState(ibAuiTabContainerButton& button, ibAuiPaneButtonState state)
    {
        return UpdateButtonStateAndRefresh(button, state, true);
    }

    bool
    ClearButtonState(ibAuiTabContainerButton& button, ibAuiPaneButtonState state)
    {
        return UpdateButtonStateAndRefresh(button, state, false);
    }

    void OnButton(int tabIdx, int button);

#ifndef SWIG
    wxDECLARE_CLASS(ibAuiTabCtrl);
    wxDECLARE_EVENT_TABLE();
#endif

    // Rectangle corresponding to the full tab control area, including both
    // tabs (which is this window) and the page area.
    wxRect m_fullRect;
};


// Simple struct combining ibAuiTabCtrl with the position inside it.
struct ibAuiNotebookPosition
{
    ibAuiNotebookPosition() = default;

    ibAuiNotebookPosition(ibAuiTabCtrl* tabCtrl_, int tabIdx_)
        : tabCtrl(tabCtrl_), tabIdx(tabIdx_)
    {
    }

    // Check if the position is valid.
    explicit operator bool() const { return tabCtrl != nullptr; }

    ibAuiTabCtrl* tabCtrl = nullptr;
    int tabIdx = wxNOT_FOUND;
};



class DESKGUI_API ibAuiNotebook : public wxCompositeBookCtrlBase
{

public:

    ibAuiNotebook() { Init(); }

    ibAuiNotebook(wxWindow* parent,
                  wxWindowID id = wxID_ANY,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = ibAUI_NB_DEFAULT_STYLE)
    {
        Init();
        Create(parent, id, pos, size, style);
    }

    virtual ~ibAuiNotebook();

    bool Create(wxWindow* parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0);

    void SetWindowStyleFlag(long style) override;

    virtual void SetUniformBitmapSize(const wxSize& size);
    virtual void SetTabCtrlHeight(int height);

    bool AddPage(wxWindow* page,
                 const wxString& caption,
                 bool select = false,
                 const wxBitmapBundle& bitmap = wxBitmapBundle());

    bool InsertPage(size_t pageIdx,
                    wxWindow* page,
                    const wxString& caption,
                    bool select = false,
                    const wxBitmapBundle& bitmap = wxBitmapBundle());

    virtual size_t GetPageCount() const override;
    virtual wxWindow* GetPage(size_t pageIdx) const override;
    virtual int FindPage(const wxWindow* page) const override;

    // This is wxAUI-specific equivalent of FindPage(), prefer to use the other
    // function.
    int GetPageIndex(wxWindow* pageWnd) const { return FindPage(pageWnd); }

    bool SetPageText(size_t page, const wxString& text) override;
    wxString GetPageText(size_t pageIdx) const override;

    bool SetPageToolTip(size_t page, const wxString& text);
    wxString GetPageToolTip(size_t pageIdx) const;

    bool SetPageBitmap(size_t page, const wxBitmapBundle& bitmap);
    wxBitmap GetPageBitmap(size_t pageIdx) const;

    ibAuiTabKind GetPageKind(size_t pageIdx) const;
    bool SetPageKind(size_t pageIdx, ibAuiTabKind kind);

    int SetSelection(size_t newPage) override;
    int GetSelection() const override;

    // Return the tab control containing the page with the given index and its
    // visual position in it (i.e. 0 for the leading one).
    ibAuiNotebookPosition GetPagePosition(size_t page) const;

    // Return all pages in the given tab control in display order.
    std::vector<size_t> GetPagesInDisplayOrder(ibAuiTabCtrl* tabCtrl) const;


    void Split(size_t page, int direction);
    void UnsplitAll();

    const ibAuiManager& GetAuiManager() const { return m_mgr; }

    void SetManagerFlags(unsigned int flags) { m_mgr.SetFlags(flags); }

    // Sets the normal font
    void SetNormalFont(const wxFont& font);

    // Sets the selected tab font
    void SetSelectedFont(const wxFont& font);

    // Sets the measuring font
    void SetMeasuringFont(const wxFont& font);

    // Sets the tab font
    virtual bool SetFont(const wxFont& font) override;

    // Gets the tab control height
    int GetTabCtrlHeight() const;

    // Gets the height of the notebook for a given page height
    int GetHeightForPageHeight(int pageHeight);

    // Shows the window menu
    bool ShowWindowMenu();

    //wxBookCtrlBase functions

    virtual void SetPageSize (const wxSize &size) override;
    virtual int  HitTest (const wxPoint &pt, long *flags=nullptr) const override;

    virtual int GetPageImage(size_t n) const override;
    virtual bool SetPageImage(size_t n, int imageId) override;

    virtual int ChangeSelection(size_t n) override;

    virtual bool AddPage(wxWindow *page, const wxString &text, bool select,
                         int imageId) override;
    virtual bool DeleteAllPages() override;
    virtual bool InsertPage(size_t index, wxWindow *page, const wxString &text,
                            bool select, int imageId) override;

    virtual wxSize DoGetBestSize() const override;

    ibAuiTabCtrl* GetTabCtrlFromPoint(const wxPoint& pt);
    ibAuiTabCtrl* GetActiveTabCtrl();

    // Get main tab control, creating it on demand if necessary.
    ibAuiTabCtrl* GetMainTabCtrl();

    // Get all tab controls.
    std::vector<ibAuiTabCtrl*> GetAllTabCtrls();


    // Internal, don't use: use GetPagePosition() instead.
    bool FindTab(wxWindow* page, ibAuiTabCtrl** ctrl, int* idx) const;

    // Serialization support: this is used by ibAuiManager but can also be
    // called directly to save/load layout of just this notebook.
    void SaveLayout(const wxString& name, ibAuiBookSerializer& serializer) const;
    void LoadLayout(const wxString& name, ibAuiBookDeserializer& deserializer);

protected:
    // Common part of all ctors.
    void Init();

    // choose the default border for this window
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // Redo sizing after thawing
    virtual void DoThaw() override;

    // these can be overridden

    // update the height, return true if it was done or false if the new height
    // calculated by CalculateTabCtrlHeight() is the same as the old one
    virtual bool UpdateTabCtrlHeight();

    virtual int CalculateTabCtrlHeight();
    virtual wxSize CalculateNewSplitSize();

    // get next page in physical (display) order
    virtual int GetNextPage(bool forward) const override;

    // remove the page and return a pointer to it
    virtual wxWindow *DoRemovePage(size_t page) override;

    //A general selection function
    virtual int DoModifySelection(size_t n, bool events);

protected:

    void DoSizing();
    void InitNotebook(long style);
    wxWindow* GetTabFrameFromTabCtrl(wxWindow* tabCtrl);
    void RemoveEmptyTabFrames();
    void UpdateHintWindowSize();

protected:

    void OnChildFocusNotebook(wxChildFocusEvent& evt);
    void OnRender(ibAuiManagerEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnNavigationKeyNotebook(wxNavigationKeyEvent& event);
    void OnSysColourChanged(wxSysColourChangedEvent& event);
    void OnDPIChanged(wxDPIChangedEvent& event);

    // The functions below are called by ibAuiTabCtrl via ibAuiTabEventSource.
    //
    // They all take the control which generated the event (never null) and all
    // but one take the position of the tab associated with the event in this
    // control: notice that this is _not_ the same as the index of the page, in
    // general, m_tabs.GetIdxFromWindow(ibAuiTabCtrl::GetWindowFromIdx()) must
    // be used to get it.
    //
    // They all return void, except OnTabButton() which returns true if the
    // application had handled the event generated by it without skipping it,
    // in which case the button must not be handled by ibAuiTabCtrl neither.
    friend class ibAuiTabEventSource;

    void OnTabClicked(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabBeginDrag(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabDragMotion(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabEndDrag(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabCancelDrag(ibAuiTabCtrl* ctrl, int tabIdx);
    bool OnTabButton(ibAuiTabCtrl* ctrl, int tabIdx, int button_id);
    void OnTabMiddleDown(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabMiddleUp(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabRightDown(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabRightUp(ibAuiTabCtrl* ctrl, int tabIdx);
    void OnTabBgDClick(ibAuiTabCtrl* ctrl);

    // set selection to the given window (which must be non-null and be one of
    // our pages, otherwise an assert is raised)
    void SetSelectionToWindow(wxWindow *win);
    void SetSelectionToPage(const ibAuiNotebookPage& page)
    {
        SetSelectionToWindow(page.window);
    }

protected:
    class DESKGUI_API ibAuiNotebookManager : public ibAuiManager
    {
    protected:
        virtual bool CanAddPane(wxWindow* window,
                                const ibAuiPaneInfo& paneInfo) const override;
    };

    ibAuiNotebookManager m_mgr;

    // Contains all pages in the insertion order.
    ibAuiTabContainer m_tabs;

    // Current page index in m_tabs or wxNOT_FOUND if none.
    int m_curPage;

    int m_tabIdCounter;
    wxWindow* m_dummyWnd;

    wxSize m_requestedBmpSize;
    int m_requestedTabCtrlHeight;
    wxFont m_selectedFont;
    wxFont m_normalFont;
    wxFont m_measuringFont;
    int m_tabCtrlHeight;

    int m_lastDropMovePos = -1;
    unsigned int m_flags;

private:
    // Create a new tab frame, containing a new ibAuiTabCtrl.
    ibAuiTabFrame* CreateTabFrame(wxSize size = wxSize());

    // Inserts the page at the given position into the given tab control.
    void InsertPageAt(ibAuiNotebookPage& info,
                      size_t page_idx,
                      ibAuiTabCtrl* tabctrl,
                      int tab_page_idx, // Can be -1 to append.
                      bool select);

    struct TabInfo;

    TabInfo FindTab(wxWindow* page) const;

    // Return the index at which the given page should be inserted in this tab
    // control if it's dropped at the given (in screen coordinates) point or
    // wxNOT_FOUND if dropping it is not allowed.
    int GetDropIndex(const ibAuiNotebookPage& srcPage,
                     ibAuiTabCtrl* tabCtrl,
                     const wxPoint& ptScreen) const;

#ifndef SWIG
    wxDECLARE_CLASS(ibAuiNotebook);
    wxDECLARE_EVENT_TABLE();
#endif
};




// wx event machinery

#ifndef SWIG

wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_PAGE_CLOSE, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_PAGE_CHANGED, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_PAGE_CHANGING, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_PAGE_CLOSED, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_BUTTON, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_BEGIN_DRAG, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_END_DRAG, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_DRAG_MOTION, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_ALLOW_DND, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_TAB_RIGHT_UP, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_DRAG_DONE, ibAuiNotebookEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_AUINOTEBOOK_BG_DCLICK, ibAuiNotebookEvent);

typedef void (wxEvtHandler::*ibAuiNotebookEventFunction)(ibAuiNotebookEvent&);

#define ibAuiNotebookEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibAuiNotebookEventFunction, func)

#define EVT_AUINOTEBOOK_PAGE_CLOSE(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_PAGE_CLOSE, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_PAGE_CLOSED(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_PAGE_CLOSED, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_PAGE_CHANGED(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_PAGE_CHANGED, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_PAGE_CHANGING(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_PAGE_CHANGING, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_BUTTON(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_BUTTON, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_BEGIN_DRAG(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_BEGIN_DRAG, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_END_DRAG(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_END_DRAG, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_DRAG_MOTION(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_DRAG_MOTION, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_ALLOW_DND(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_ALLOW_DND, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_DRAG_DONE(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_DRAG_DONE, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_TAB_MIDDLE_DOWN(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_TAB_MIDDLE_UP(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_TAB_RIGHT_DOWN(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_TAB_RIGHT_UP(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_TAB_RIGHT_UP, winid, ibAuiNotebookEventHandler(fn))
#define EVT_AUINOTEBOOK_BG_DCLICK(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUINOTEBOOK_BG_DCLICK, winid, ibAuiNotebookEventHandler(fn))
#else

// wxpython/swig event work
%constant wxEventType ibEVT_AUINOTEBOOK_PAGE_CLOSE;
%constant wxEventType ibEVT_AUINOTEBOOK_PAGE_CLOSED;
%constant wxEventType ibEVT_AUINOTEBOOK_PAGE_CHANGED;
%constant wxEventType ibEVT_AUINOTEBOOK_PAGE_CHANGING;
%constant wxEventType ibEVT_AUINOTEBOOK_BUTTON;
%constant wxEventType ibEVT_AUINOTEBOOK_BEGIN_DRAG;
%constant wxEventType ibEVT_AUINOTEBOOK_END_DRAG;
%constant wxEventType ibEVT_AUINOTEBOOK_DRAG_MOTION;
%constant wxEventType ibEVT_AUINOTEBOOK_ALLOW_DND;
%constant wxEventType ibEVT_AUINOTEBOOK_DRAG_DONE;
%constant wxEventType ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN;
%constant wxEventType ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP;
%constant wxEventType ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN;
%constant wxEventType ibEVT_AUINOTEBOOK_TAB_RIGHT_UP;
%constant wxEventType ibEVT_AUINOTEBOOK_BG_DCLICK;

%pythoncode {
    EVT_AUINOTEBOOK_PAGE_CLOSE = wx.PyEventBinder( ibEVT_AUINOTEBOOK_PAGE_CLOSE, 1 )
    EVT_AUINOTEBOOK_PAGE_CLOSED = wx.PyEventBinder( ibEVT_AUINOTEBOOK_PAGE_CLOSED, 1 )
    EVT_AUINOTEBOOK_PAGE_CHANGED = wx.PyEventBinder( ibEVT_AUINOTEBOOK_PAGE_CHANGED, 1 )
    EVT_AUINOTEBOOK_PAGE_CHANGING = wx.PyEventBinder( ibEVT_AUINOTEBOOK_PAGE_CHANGING, 1 )
    EVT_AUINOTEBOOK_BUTTON = wx.PyEventBinder( ibEVT_AUINOTEBOOK_BUTTON, 1 )
    EVT_AUINOTEBOOK_BEGIN_DRAG = wx.PyEventBinder( ibEVT_AUINOTEBOOK_BEGIN_DRAG, 1 )
    EVT_AUINOTEBOOK_END_DRAG = wx.PyEventBinder( ibEVT_AUINOTEBOOK_END_DRAG, 1 )
    EVT_AUINOTEBOOK_DRAG_MOTION = wx.PyEventBinder( ibEVT_AUINOTEBOOK_DRAG_MOTION, 1 )
    EVT_AUINOTEBOOK_ALLOW_DND = wx.PyEventBinder( ibEVT_AUINOTEBOOK_ALLOW_DND, 1 )
    EVT_AUINOTEBOOK_DRAG_DONE = wx.PyEventBinder( ibEVT_AUINOTEBOOK_DRAG_DONE, 1 )
    EVT__AUINOTEBOOK_TAB_MIDDLE_DOWN = wx.PyEventBinder( ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN, 1 )
    EVT__AUINOTEBOOK_TAB_MIDDLE_UP = wx.PyEventBinder( ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP, 1 )
    EVT__AUINOTEBOOK_TAB_RIGHT_DOWN = wx.PyEventBinder( ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN, 1 )
    EVT__AUINOTEBOOK_TAB_RIGHT_UP = wx.PyEventBinder( ibEVT_AUINOTEBOOK_TAB_RIGHT_UP, 1 )
    EVT_AUINOTEBOOK_BG_DCLICK = wx.PyEventBinder( ibEVT_AUINOTEBOOK_BG_DCLICK, 1 )
}
#endif


// old wxEVT_COMMAND_* constants
#define ibEVT_COMMAND_AUINOTEBOOK_PAGE_CLOSE        ibEVT_AUINOTEBOOK_PAGE_CLOSE
#define ibEVT_COMMAND_AUINOTEBOOK_PAGE_CLOSED       ibEVT_AUINOTEBOOK_PAGE_CLOSED
#define ibEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGED      ibEVT_AUINOTEBOOK_PAGE_CHANGED
#define ibEVT_COMMAND_AUINOTEBOOK_PAGE_CHANGING     ibEVT_AUINOTEBOOK_PAGE_CHANGING
#define ibEVT_COMMAND_AUINOTEBOOK_BUTTON            ibEVT_AUINOTEBOOK_BUTTON
#define ibEVT_COMMAND_AUINOTEBOOK_BEGIN_DRAG        ibEVT_AUINOTEBOOK_BEGIN_DRAG
#define ibEVT_COMMAND_AUINOTEBOOK_END_DRAG          ibEVT_AUINOTEBOOK_END_DRAG
#define ibEVT_COMMAND_AUINOTEBOOK_DRAG_MOTION       ibEVT_AUINOTEBOOK_DRAG_MOTION
#define ibEVT_COMMAND_AUINOTEBOOK_ALLOW_DND         ibEVT_AUINOTEBOOK_ALLOW_DND
#define ibEVT_COMMAND_AUINOTEBOOK_DRAG_DONE         ibEVT_AUINOTEBOOK_DRAG_DONE
#define ibEVT_COMMAND_AUINOTEBOOK_TAB_MIDDLE_DOWN   ibEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN
#define ibEVT_COMMAND_AUINOTEBOOK_TAB_MIDDLE_UP     ibEVT_AUINOTEBOOK_TAB_MIDDLE_UP
#define ibEVT_COMMAND_AUINOTEBOOK_TAB_RIGHT_DOWN    ibEVT_AUINOTEBOOK_TAB_RIGHT_DOWN
#define ibEVT_COMMAND_AUINOTEBOOK_TAB_RIGHT_UP      ibEVT_AUINOTEBOOK_TAB_RIGHT_UP
#define ibEVT_COMMAND_AUINOTEBOOK_BG_DCLICK         ibEVT_AUINOTEBOOK_BG_DCLICK
#define ibEVT_COMMAND_AUINOTEBOOK_CANCEL_DRAG       ibEVT_AUINOTEBOOK_CANCEL_DRAG

#endif  // _DESKGUI_UNIV_AUI_AUIBOOK_H_
