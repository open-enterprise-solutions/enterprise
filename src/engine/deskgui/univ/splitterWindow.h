// FORKED from wxWidgets: wxSplitterWindow and wxSplitterEvent (include/wx/generic/splitter.h,
// src/generic/splitter.cpp), on the library's window (wx -> ib prefixes applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/splitter.h
// Purpose:     wxSplitterWindow class
// Author:      Julian Smart
// Created:     01/02/97
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_SPLITTERWINDOW_H_
#define _DESKGUI_UNIV_SPLITTERWINDOW_H_

#include "deskgui/univ/window.h"                // base class declaration

#include <wx/containr.h>                    // wxControlContainer
#include <wx/overlay.h>
#include <wx/splitter.h>                    // wxSP_* styles, wxSplitMode, wxSPLIT_DRAG_* - wx's

class ibSplitterEvent;

wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_SPLITTER_SASH_POS_CHANGED, ibSplitterEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_SPLITTER_SASH_POS_CHANGING, ibSplitterEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_SPLITTER_SASH_POS_RESIZE, ibSplitterEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_SPLITTER_DOUBLECLICKED, ibSplitterEvent);
wxDECLARE_EXPORTED_EVENT(DESKGUI_API, ibEVT_SPLITTER_UNSPLIT, ibSplitterEvent);

// ---------------------------------------------------------------------------
// ibSplitterWindow maintains one or two panes, with
// an optional vertical or horizontal split which
// can be used with the mouse or programmatically.
// ---------------------------------------------------------------------------

// SEAM vs wx: wx's deprecated SetSashSize, a no-op, is not kept.
class DESKGUI_API ibSplitterWindow: public wxNavigationEnabled<ibWindow>
{
public:

////////////////////////////////////////////////////////////////////////////
// Public API

    // Default constructor
    ibSplitterWindow()
    {
        Init();
    }

    // Normal constructor
    ibSplitterWindow(wxWindow *parent, wxWindowID id = wxID_ANY,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxSP_3D,
                     const wxString& name = wxT("splitter"))
    {
        Init();
        Create(parent, id, pos, size, style, name);
    }

    virtual ~ibSplitterWindow();

    bool Create(wxWindow *parent, wxWindowID id = wxID_ANY,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxSP_3D,
                     const wxString& name = wxT("splitter"));

    // Gets the only or left/top pane
    wxWindow *GetWindow1() const { return m_windowOne; }

    // Gets the right/bottom pane
    wxWindow *GetWindow2() const { return m_windowTwo; }

    // Sets the split mode
    void SetSplitMode(int mode)
    {
        wxASSERT_MSG( mode == wxSPLIT_VERTICAL || mode == wxSPLIT_HORIZONTAL,
                      wxT("invalid split mode") );

        m_splitMode = (wxSplitMode)mode;
    }

    // Gets the split mode
    wxSplitMode GetSplitMode() const { return m_splitMode; }

    // Initialize with one window
    void Initialize(wxWindow *window);

    // Associates the given window with window 2, drawing the appropriate sash
    // and changing the split mode.
    // Does nothing and returns false if the window is already split.
    // A sashPosition of 0 means choose a default sash position,
    // negative sashPosition specifies the size of right/lower pane as its
    // absolute value rather than the size of left/upper pane.
    virtual bool SplitVertically(wxWindow *window1,
                                 wxWindow *window2,
                                 int sashPosition = 0)
        { return DoSplit(wxSPLIT_VERTICAL, window1, window2, sashPosition); }
    virtual bool SplitHorizontally(wxWindow *window1,
                                   wxWindow *window2,
                                   int sashPosition = 0)
        { return DoSplit(wxSPLIT_HORIZONTAL, window1, window2, sashPosition); }

    // Removes the specified (or second) window from the view
    // Doesn't actually delete the window.
    bool Unsplit(wxWindow *toRemove = nullptr);

    // Replaces one of the windows with another one (neither old nor new
    // parameter should be null)
    bool ReplaceWindow(wxWindow *winOld, wxWindow *winNew);

    // Make sure the child window sizes are updated. This is useful
    // for reducing flicker by updating the sizes before a
    // window is shown, if you know the overall size is correct.
    void UpdateSize();

    // Is the window split?
    bool IsSplit() const { return (m_windowTwo != nullptr); }

    // Sets the border size
    void SetBorderSize(int WXUNUSED(width)) { }

    // Hide or show the sash and test whether it's currently hidden.
    void SetSashInvisible(bool invisible = true);
    bool IsSashInvisible() const { return HasFlag(wxSP_NOSASH); }

    // Gets the current sash size which may be 0 if it's hidden and the default
    // sash size.
    int GetSashSize() const;
    int GetDefaultSashSize() const;

    // Gets the border size
    int GetBorderSize() const;

    // Set the sash position
    void SetSashPosition(int position, bool redraw = true);

    // Gets the sash position
    int GetSashPosition() const { return m_sashPosition; }

    // Set the sash gravity
    void SetSashGravity(double gravity);

    // Gets the sash gravity
    double GetSashGravity() const { return m_sashGravity; }

    // If this is zero, we can remove panes by dragging the sash.
    void SetMinimumPaneSize(int min);
    int GetMinimumPaneSize() const { return m_minimumPaneSize; }

    // NB: the OnXXX() functions below are for backwards compatibility only,
    //     don't use them in new code but handle the events instead!

    // called when the sash position is about to change, may return a new value
    // for the sash or -1 to prevent the change from happening at all
    virtual int OnSashPositionChanging(int newSashPosition);

    // Called when the sash position is about to be changed, return
    // false from here to prevent the change from taking place.
    // Repositions sash to minimum position if pane would be too small.
    // newSashPosition here is always positive or zero.
    virtual bool OnSashPositionChange(int newSashPosition);

    // If the sash is moved to an extreme position, a subwindow
    // is removed from the splitter window, and the app is
    // notified. The app should delete or hide the window.
    virtual void OnUnsplit(wxWindow *removed);

    // Called when the sash is double-clicked.
    // The default behaviour is to remove the sash if the
    // minimum pane size is zero.
    virtual void OnDoubleClickSash(int x, int y);

    // Do not allow ibSplitterWindow to get the focus as there is
    // no keyboard handling anyways and the child windows should
    // keep the focus
    bool AcceptsFocus() const override { return false; }

////////////////////////////////////////////////////////////////////////////
// Implementation

    // Paints the border and sash
    void OnPaint(wxPaintEvent& event);

    // Handles mouse events
    void OnMouseEvent(wxMouseEvent& ev);

    // Aborts dragging mode
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);

    // Adjusts the panes
    void OnSize(wxSizeEvent& event);

    void OnDPIChanged(wxDPIChangedEvent& event);

    // In live mode, resize child windows in idle time
    void OnInternalIdle() override;

    // Draws the sash
    virtual void DrawSash(wxDC& dc);

    // Draws the sash tracker (for whilst moving the sash)
    virtual void DrawSashTracker(int x, int y);

    // Tests for x, y over sash
    virtual bool SashHitTest(int x, int y);

    // Resizes subwindows
    virtual void SizeWindows();

#ifdef __WXMAC__
    virtual bool MacClipGrandChildren() const override { return true ; }
#endif

    // Get the sash position that was last used before Unsplit() was called.
    // Horizontal and vertical components correspond to the split in the
    // corresponding direction, and are 0 if the splitter hadn't been split in
    // this direction at all.
    wxPoint GetLastSplitPosition() const;

    // Set the default initial sash position to use when the splitter is split.
    void SetLastSplitPosition(const wxPoint& pos);

protected:
    // event handlers
#if defined(__WXMSW__) || defined(__WXMAC__)
    void OnSetCursor(wxSetCursorEvent& event);
#endif // wxMSW

    // common part of all ctors
    void Init();

    // common part of SplitVertically() and SplitHorizontally()
    bool DoSplit(wxSplitMode mode,
                 wxWindow *window1, wxWindow *window2,
                 int sashPosition);

    // adjusts sash position with respect to min. pane and window sizes
    int AdjustSashPosition(int sashPos) const;

    // get either width or height depending on the split mode
    int GetWindowSize() const;

    // convert the user specified sash position which may be > 0 (as is), < 0
    // (specifying the size of the right pane) or 0 (use default) to the real
    // position to be passed to DoSetSashPosition()
    int ConvertSashPosition(int sashPos) const;

    // set the real sash position, sashPos here must be positive
    //
    // returns true if the sash position has been changed, false otherwise
    bool DoSetSashPosition(int sashPos);

    // set the sash position and send an event about it having been changed
    void SetSashPositionAndNotify(int sashPos);

    // callbacks executed when we detect that the mouse has entered or left
    // the sash
    virtual void OnEnterSash();
    virtual void OnLeaveSash();

    // set the cursor appropriate for the current split mode
    void SetResizeCursor();

    // return the best size of the splitter equal to best sizes of its
    // subwindows
    virtual wxSize DoGetBestSize() const override;


    wxSplitMode m_splitMode;
    wxWindow*   m_windowOne;
    wxWindow*   m_windowTwo;
    int         m_dragMode;
    int         m_oldX;         // current tracker position if not live mode
    int         m_oldY;         // current tracker position if not live mode
    int         m_sashPosition; // Number of pixels from left or top
    double      m_sashGravity;
    wxSize      m_lastSize;
    int         m_requestedSashPosition;
    int         m_sashPositionCurrent; // while dragging
    wxPoint     m_ptStart;      // mouse position when dragging started
    int         m_sashStart;    // sash position when dragging started
    int         m_minimumPaneSize;
    wxPoint     m_lastSplitPosition;
    wxCursor    m_sashCursorWE;
    wxCursor    m_sashCursorNS;
    wxOverlay   m_overlay;

    // when in live mode, set this to true to resize children in idle
    bool        m_needUpdating:1;
    bool        m_permitUnsplitAlways:1;

private:
    wxDECLARE_DYNAMIC_CLASS(ibSplitterWindow);
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(ibSplitterWindow);
};

// ----------------------------------------------------------------------------
// event class and macros
// ----------------------------------------------------------------------------

// we reuse the same class for all splitter event types because this is the
// usual wxWin convention, but the three event types have different kind of
// data associated with them, so the accessors can be only used if the real
// event type matches with the one for which the accessors make sense
class DESKGUI_API ibSplitterEvent : public wxNotifyEvent
{
public:
    ibSplitterEvent(wxEventType type = wxEVT_NULL,
                    ibSplitterWindow *splitter = nullptr)
        : wxNotifyEvent(type)
    {
        SetEventObject(splitter);
        if (splitter) m_id = splitter->GetId();

        m_data.resize.oldSize = 0;
        m_data.resize.newSize = 0;
    }
    ibSplitterEvent(const ibSplitterEvent& event)
        : wxNotifyEvent(event), m_data(event.m_data)
    { }

    // SASH_POS_CHANGED methods

    // setting the sash position to -1 prevents the change from taking place at
    // all
    void SetSashPosition(int pos)
    {
        wxASSERT( GetEventType() == ibEVT_SPLITTER_SASH_POS_CHANGED
            || GetEventType() == ibEVT_SPLITTER_SASH_POS_CHANGING
            || GetEventType() == ibEVT_SPLITTER_SASH_POS_RESIZE);

        m_data.resize.pos = pos;
    }

    int GetSashPosition() const
    {
        wxASSERT( GetEventType() == ibEVT_SPLITTER_SASH_POS_CHANGED
            || GetEventType() == ibEVT_SPLITTER_SASH_POS_CHANGING
            || GetEventType() == ibEVT_SPLITTER_SASH_POS_RESIZE);

        return m_data.resize.pos;
    }

    void SetSize(int oldSize, int newSize)
    {
        wxASSERT(GetEventType() == ibEVT_SPLITTER_SASH_POS_RESIZE);

        m_data.resize.oldSize = oldSize;
        m_data.resize.newSize = newSize;
    }

    int GetOldSize() const
    {
        wxASSERT(GetEventType() == ibEVT_SPLITTER_SASH_POS_RESIZE);

        return m_data.resize.oldSize;
    }

    int GetNewSize() const
    {
        wxASSERT(GetEventType() == ibEVT_SPLITTER_SASH_POS_RESIZE);

        return m_data.resize.newSize;
    }

    // UNSPLIT event methods
    wxWindow *GetWindowBeingRemoved() const
    {
        wxASSERT( GetEventType() == ibEVT_SPLITTER_UNSPLIT );

        return m_data.win;
    }

    // DCLICK event methods
    int GetX() const
    {
        wxASSERT( GetEventType() == ibEVT_SPLITTER_DOUBLECLICKED );

        return m_data.pt.x;
    }

    int GetY() const
    {
        wxASSERT( GetEventType() == ibEVT_SPLITTER_DOUBLECLICKED );

        return m_data.pt.y;
    }

    wxNODISCARD virtual wxEvent *Clone() const override { return new ibSplitterEvent(*this); }

private:
    friend class ibSplitterWindow;

    // data for the different types of event
    union
    {
        struct
        {
            int pos;            // position for SASH_POS_* events
            int oldSize;        // window size for SASH_POS_RESIZE event
            int newSize;        // window size for SASH_POS_RESIZE event
        } resize;
        wxWindow *win;      // window being removed for UNSPLIT event
        struct
        {
            int x, y;
        } pt;               // position of double click for DCLICK event
    } m_data;

    wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(ibSplitterEvent);
};

// SEAM vs wx: no EVT_SPLITTER_* table macros - wx's (wx/splitter.h, included above for the styles) name its own
// events; this one's are bound by their types, ibEVT_SPLITTER_*
typedef void (wxEvtHandler::*ibSplitterEventFunction)(ibSplitterEvent&);

#define ibSplitterEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibSplitterEventFunction, func)

#endif // _DESKGUI_UNIV_SPLITTERWINDOW_H_
