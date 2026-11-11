// FORKED from wx's generic activity indicator (wx -> ib): a control of the library's, its ground the theme's
// (ibThemePaintDC) - wx's erased its own in the system's colour, a grey square on a report's sheet (2026-11-11)

///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/activityindicator.cpp
// Purpose:     Generic wxActivityIndicator implementation.
// Author:      Vadim Zeitlin
// Created:     2015-03-06
// Copyright:   (c) 2015 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_ACTIVITYINDICATOR_H_
#define _DESKGUI_UNIV_ACTIVITYINDICATOR_H_

#include "deskgui/deskgui.h"

#include <wx/activityindicator.h>   // wxActivityIndicatorNameStr
#include <wx/timer.h>
#include "deskgui/univ/ctrl/control.h"

class DESKGUI_API ibActivityIndicator : public ibControl
{
public:
    ibActivityIndicator() = default;

    explicit ibActivityIndicator(wxWindow* parent,
                                 wxWindowID winid = wxID_ANY,
                                 const wxPoint& pos = wxDefaultPosition,
                                 const wxSize& size = wxDefaultSize,
                                 long style = 0,
                                 const wxString& name = wxActivityIndicatorNameStr)
    {
        Create(parent, winid, pos, size, style, name);
    }

    bool Create(wxWindow* parent,
                wxWindowID winid = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxActivityIndicatorNameStr);

    void Start();
    void Stop();
    bool IsRunning() const;

protected:
    virtual wxSize DoGetBestClientSize() const override;

private:
    void OnTimer(wxTimerEvent& event);
    void OnPaint(wxPaintEvent& event);

    // the frames advance by it (Start, Stop)
    wxTimer m_timer;

    // the dot the turn starts from
    int m_frame = 0;

    wxDECLARE_DYNAMIC_CLASS_NO_COPY(ibActivityIndicator);
    wxDECLARE_EVENT_TABLE();
};

#endif // _DESKGUI_UNIV_ACTIVITYINDICATOR_H_
