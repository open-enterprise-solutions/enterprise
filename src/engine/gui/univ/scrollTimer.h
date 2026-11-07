// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/scrtimer.h
// Purpose:     ibScrollTimer: small helper class for wxScrollArrow/Thumb
// Author:      Vadim Zeitlin
// Created:     18.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_SCROLLTIMER_H_
#define _GUI_UNIV_SCROLLTIMER_H_

#include "gui/gui.h"

// NB: this class is implemented in scrolbar.cpp

#include <wx/defs.h>

#if wxUSE_TIMER

#include <wx/timer.h>

// ----------------------------------------------------------------------------
// ibScrollTimer: the timer used when the arrow or scrollbar shaft is kept
// pressed
// ----------------------------------------------------------------------------

class GUI_API ibScrollTimer : public wxTimer
{
public:
    // default ctor
    ibScrollTimer();

    // start generating the events
    void StartAutoScroll();

    // the base class method
    virtual void Notify() override;

protected:
    // to implement in derived classes: perform the scroll action and return
    // true to continue scrolling or false to stop
    virtual bool DoNotify() = 0;

    // should we skip the next timer event?
    bool m_skipNext;
};

#endif // wxUSE_TIMER

#endif // _GUI_UNIV_SCROLLTIMER_H_
