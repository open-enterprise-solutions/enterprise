// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/scrarrow.h
// Purpose:     ibScrollArrows class
// Author:      Vadim Zeitlin
// Created:     22.01.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_SCRARROW_H_
#define _WX_UNIV_SCRARROW_H_

#include "frontend/frontend.h"

#if wxUSE_SCROLLBAR
// ----------------------------------------------------------------------------
// ibScrollArrows is not a control but just a class containing the common
// functionality of scroll arrows, whether part of scrollbars, spin ctrls or
// anything else.
//
// To customize its behaviour, ibScrollArrows doesn't use any virtual methods
// but instead a callback pointer to a ibControlWithArrows object which is used
// for all control-dependent stuff. Thus, to use ibScrollArrows, you just need
// to derive from the ibControlWithArrows interface and implement its methods.
// ----------------------------------------------------------------------------

class ibControlWithArrows;
class wxDC;
class wxMouseEvent;
class wxRect;
class ibRenderer;

// ----------------------------------------------------------------------------
// ibScrollArrows: an abstraction of scrollbar arrow
// ----------------------------------------------------------------------------

class FRONTEND_API ibScrollArrows
{
public:
    enum Arrow
    {
        Arrow_None = -1,
        Arrow_First,        // left or top
        Arrow_Second,       // right or bottom
        Arrow_Max
    };

    // ctor requires a back pointer to ibControlWithArrows
    ibScrollArrows(ibControlWithArrows *control);

    // draws the arrow on the given DC in the given rectangle, uses
    // ibControlWithArrows::GetArrowState() to get its current state
    void DrawArrow(Arrow arrow, wxDC& dc, const wxRect& rect,
                   bool scrollbarLike = false) const;

    // process a mouse move, enter or leave event, possibly calling
    // ibControlWithArrows::SetArrowState() if
    // ibControlWithArrows::HitTestArrow() says that the mouse has left/entered
    // an arrow
    bool HandleMouseMove(const wxMouseEvent& event) const;

    // process a mouse click event
    bool HandleMouse(const wxMouseEvent& event) const;

    // dtor
    ~ibScrollArrows();

private:
    // set or clear the wxCONTROL_CURRENT flag for the arrow
    void UpdateCurrentFlag(Arrow arrow, Arrow arrowCur) const;

    // the main control
    ibControlWithArrows *m_control;

    // the data for the mouse capture
    struct wxScrollArrowCaptureData *m_captureData;
};

// ----------------------------------------------------------------------------
// ibControlWithArrows: interface implemented by controls using ibScrollArrows
// ----------------------------------------------------------------------------

class ibWindow;

class FRONTEND_API ibControlWithArrows
{
public:
    virtual ~ibControlWithArrows() = default;

    // get the renderer to use for drawing the arrows
    virtual ibRenderer *GetRenderer() const = 0;

    // get the controls window (used for mouse capturing)
    virtual ibWindow *GetWindow() = 0;

    // get the orientation of the arrows (vertical or horizontal)
    virtual bool IsVertical() const = 0;

    // get the state of this arrow as combination of wxCONTROL_XXX flags
    virtual int GetArrowState(ibScrollArrows::Arrow arrow) const = 0;

    // set or clear the specified flag in the arrow state: this function is
    // responsible for refreshing the control
    virtual void SetArrowFlag(ibScrollArrows::Arrow arrow,
                              int flag, bool set = true) = 0;

    // hit testing: return on which arrow the point is (or Arrow_None)
    virtual ibScrollArrows::Arrow HitTestArrow(const wxPoint& pt) const = 0;

    // called when the arrow is pressed, return true to continue scrolling and
    // false to stop it
    virtual bool OnArrow(ibScrollArrows::Arrow arrow) = 0;
};
#endif // wxUSE_SCROLLBAR

#endif // _WX_UNIV_SCRARROW_H_
