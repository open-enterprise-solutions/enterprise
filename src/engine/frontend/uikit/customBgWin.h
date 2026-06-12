// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/custombgwin.h
// Purpose:     wxUniv implementation of ibCustomBackgroundWindow.
// Author:      Vadim Zeitlin
// Created:     2011-10-10
// Copyright:   (c) 2011 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_CUSTOMBGWIN_H_
#define _WX_UNIV_CUSTOMBGWIN_H_

// ----------------------------------------------------------------------------
// ibCustomBackgroundWindow
// ----------------------------------------------------------------------------

template <class W>
class ibCustomBackgroundWindow : public W,
                                 public wxCustomBackgroundWindowBase
{
public:
    typedef W BaseWindowClass;

    ibCustomBackgroundWindow() = default;

protected:
    virtual void DoSetBackgroundBitmap(const wxBitmap& bmp) override
    {
        // We have support for background bitmap even at the base class level.
        BaseWindowClass::SetBackground(bmp, wxALIGN_NOT, wxTILE);
    }

    wxDECLARE_NO_COPY_TEMPLATE_CLASS(ibCustomBackgroundWindow, W);
};

#endif // _WX_UNIV_CUSTOMBGWIN_H_
