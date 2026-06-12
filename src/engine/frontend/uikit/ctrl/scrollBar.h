// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/scrolbar.h
// Purpose:     ibScrollBar for wxUniversal
// Author:      Vadim Zeitlin
// Created:     20.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_SCROLBAR_H_
#define _WX_UNIV_SCROLBAR_H_

#include "frontend/frontend.h"

class ibWindow;

class ibScrollTimer;

#include "frontend/uikit/ctrl/scrollArrow.h"
#include <wx/renderer.h>

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------

// scroll the bar
#define ibACTION_SCROLL_START       wxT("start")     // to the beginning
#define ibACTION_SCROLL_END         wxT("end")       // to the end
#define ibACTION_SCROLL_LINE_UP     wxT("lineup")    // one line up/left
#define ibACTION_SCROLL_PAGE_UP     wxT("pageup")    // one page up/left
#define ibACTION_SCROLL_LINE_DOWN   wxT("linedown")  // one line down/right
#define ibACTION_SCROLL_PAGE_DOWN   wxT("pagedown")  // one page down/right

// the scrollbar thumb may be dragged
#define ibACTION_SCROLL_THUMB_DRAG      wxT("thumbdrag")
#define ibACTION_SCROLL_THUMB_MOVE      wxT("thumbmove")
#define ibACTION_SCROLL_THUMB_RELEASE   wxT("thumbrelease")

// ----------------------------------------------------------------------------
// ibScrollBar
// ----------------------------------------------------------------------------

#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxScrollBarBase sits on the native wxControl in our build,
// so the scrollbar derives from ibControl directly (see control.h)
class FRONTEND_API ibScrollBar : public ibControl,
                                public ibControlWithArrows
{
public:
    // scrollbar elements: they correspond to wxHT_SCROLLBAR_XXX constants but
    // start from 0 which allows to use them as array indices
    enum Element
    {
        Element_Arrow_Line_1,
        Element_Arrow_Line_2,
        Element_Arrow_Page_1,
        Element_Arrow_Page_2,
        Element_Thumb,
        Element_Bar_1,
        Element_Bar_2,
        Element_Max
    };

    ibScrollBar();
    ibScrollBar(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSB_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxScrollBarNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSB_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxScrollBarNameStr));

    virtual ~ibScrollBar();

    // implement base class pure virtuals
    virtual int GetThumbPosition() const;
    virtual int GetThumbSize() const;
    virtual int GetPageSize() const;
    virtual int GetRange() const;

    virtual void SetThumbPosition(int thumbPos);
    virtual void SetScrollbar(int position, int thumbSize,
                              int range, int pageSize,
                              bool refresh = true);

    // ibScrollBar actions
    void ScrollToStart();
    void ScrollToEnd();
    bool ScrollLines(int nLines) override;
    bool ScrollPages(int nPages) override;

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = 0,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

    // scrollbars around a normal window should not receive the focus
    virtual bool AcceptsFocus() const override;

    // ibScrollBar sub elements state (combination of wxCONTROL_XXX)
    void SetState(Element which, int flags);
    int GetState(Element which) const;

    // implement ibControlWithArrows methods
    virtual ibRenderer *GetRenderer() const override { return m_renderer; }
    virtual ibWindow *GetWindow() override { return this; }
    virtual bool IsVertical() const override { return (GetWindowStyle() & wxSB_VERTICAL) != 0; }
    virtual int GetArrowState(ibScrollArrows::Arrow arrow) const override;
    virtual void SetArrowFlag(ibScrollArrows::Arrow arrow, int flag, bool set) override;
    virtual bool OnArrow(ibScrollArrows::Arrow arrow) override;
    virtual ibScrollArrows::Arrow HitTestArrow(const wxPoint& pt) const override;

    // for ibControlRenderer::DrawScrollbar() only
    const ibScrollArrows& GetArrows() const { return m_arrows; }

    // returns one of wxHT_SCROLLBAR_XXX constants
    wxHitTest HitTestBar(const wxPoint& pt) const;

    // idle processing
    virtual void OnInternalIdle() override;

protected:
    virtual wxSize DoGetBestClientSize() const override;
    virtual void DoDraw(ibControlRenderer *renderer) override;
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

    // forces update of thumb's visual appearance (does nothing if m_dirty=false)
    void UpdateThumb();

    // SetThumbPosition() helper
    void DoSetThumb(int thumbPos);

    // common part of all ctors
    void Init();

    // is this scrollbar attached to a window or a standalone control?
    bool IsStandalone() const;

    // scrollbar geometry methods:

    // gets the bounding box for a scrollbar element for the given (by default
    // - current) thumb position
    wxRect GetScrollbarRect(ibScrollBar::Element elem, int thumbPos = -1) const;

    // returns the size of the scrollbar shaft excluding the arrows
    wxCoord GetScrollbarSize() const;

    // translate the scrollbar position (in logical units) into physical
    // coordinate (in pixels) and the other way round
    wxCoord ScrollbarToPixel(int thumbPos = -1);
    int PixelToScrollbar(wxCoord coord);

    // return the starting and ending positions, in pixels, of the thumb of a
    // scrollbar with the given logical position, thumb size and range and the
    // given physical length
    static void GetScrollBarThumbSize(wxCoord length,
                                      int thumbPos,
                                      int thumbSize,
                                      int range,
                                      wxCoord *thumbStart,
                                      wxCoord *thumbEnd);

private:
    // total range of the scrollbar in logical units
    int m_range;

    // the current and previous (after last refresh - this is used for
    // repainting optimisation) size of the thumb in logical units (from 0 to
    // m_range) and its position (from 0 to m_range - m_thumbSize)
    int m_thumbSize,
        m_thumbPos,
        m_thumbPosOld;

    // the page size, i.e. the number of lines by which to scroll when page
    // up/down action is performed
    int m_pageSize;

    // the state of the sub elements
    int m_elementsState[Element_Max];

    // the dirty flag: if set, scrollbar must be updated
    bool m_dirty;

    // the object handling the arrows
    ibScrollArrows m_arrows;

    friend class ibControlRenderer; // for geometry methods
    friend class ibStdScrollBarInputHandler; // for geometry methods

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibScrollBar);
};

// ----------------------------------------------------------------------------
// Standard scrollbar input handler which can be used as a base class
// ----------------------------------------------------------------------------

class FRONTEND_API ibStdScrollBarInputHandler : public ibStdInputHandler
{
public:
    // constructor takes a renderer (used for scrollbar hit testing) and the
    // base handler to which all unhandled events are forwarded
    ibStdScrollBarInputHandler(ibRenderer *renderer,
                               ibInputHandler *inphand);

    virtual bool HandleKey(ibInputConsumer *consumer,
                           const wxKeyEvent& event,
                           bool pressed) override;
    virtual bool HandleMouse(ibInputConsumer *consumer,
                             const wxMouseEvent& event) override;
    virtual bool HandleMouseMove(ibInputConsumer *consumer, const wxMouseEvent& event) override;

    virtual ~ibStdScrollBarInputHandler();

    // this method is called by ibScrollBarTimer only and may be overridden
    //
    // return true to continue scrolling, false to stop the timer
    virtual bool OnScrollTimer(ibScrollBar *scrollbar,
                               const ibControlAction& action);

protected:
    // return true if the mouse button can be used to activate scrollbar, false
    // if not (any button under GTK+ unlike left button only which is default)
    virtual bool IsAllowedButton(int button) const
        { return button == wxMOUSE_BTN_LEFT; }

    // set or clear the specified flag on the scrollbar element corresponding
    // to m_htLast
    void SetElementState(ibScrollBar *scrollbar, int flag, bool doIt);

    // [un]highlight the scrollbar element corresponding to m_htLast
    virtual void Highlight(ibScrollBar *scrollbar, bool doIt)
        { SetElementState(scrollbar, wxCONTROL_CURRENT, doIt); }

    // [un]press the scrollbar element corresponding to m_htLast
    virtual void Press(ibScrollBar *scrollbar, bool doIt)
        { SetElementState(scrollbar, wxCONTROL_PRESSED, doIt); }

    // stop scrolling because we reached the end point
    void StopScrolling(ibScrollBar *scrollbar);

    // get the mouse coordinates in the scrollbar direction from the event
    wxCoord GetMouseCoord(const ibScrollBar *scrollbar,
                          const wxMouseEvent& event) const;

    // generate a "thumb move" action for this mouse event
    void HandleThumbMove(ibScrollBar *scrollbar, const wxMouseEvent& event);


    // the window (scrollbar) which has capture or nullptr and the flag telling if
    // the mouse is inside the element which captured it or not
    ibWindow *m_winCapture;
    bool      m_winHasMouse;
    int       m_btnCapture;  // the mouse button which has captured mouse

    // the position where we started scrolling by page
    wxPoint m_ptStartScrolling;

    // one of wxHT_SCROLLBAR_XXX value: where has the mouse been last time?
    wxHitTest m_htLast;

    // the renderer (we use it only for hit testing)
    ibRenderer *m_renderer;

    // the offset of the top/left of the scrollbar relative to the mouse to
    // keep during the thumb drag
    int m_ofsMouse;

    // the timer for generating scroll events when the mouse stays pressed on
    // a scrollbar
    ibScrollTimer *m_timerScroll;
};

#endif // _WX_UNIV_SCROLBAR_H_

