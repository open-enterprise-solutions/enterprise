// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/scrtimer.h
// Purpose:     ibScrollTimer: small helper class for wxScrollArrow/Thumb
// Author:      Vadim Zeitlin
// Created:     18.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_SCRTIMER_H
#define _WX_UNIV_SCRTIMER_H

#include "frontend/frontend.h"

// NB: this class is implemented in scrolbar.cpp

#include <wx/defs.h>

#if wxUSE_TIMER

#include <wx/timer.h>

// ----------------------------------------------------------------------------
// ibScrollTimer: the timer used when the arrow or scrollbar shaft is kept
// pressed
// ----------------------------------------------------------------------------

class FRONTEND_API ibScrollTimer : public wxTimer
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

#endif // _WX_UNIV_SCRTIMER_H
