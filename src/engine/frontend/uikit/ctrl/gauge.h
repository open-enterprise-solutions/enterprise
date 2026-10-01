// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/gauge.h
// Purpose:     wxUniversal ibGauge declaration
// Author:      Vadim Zeitlin
// Created:     20.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_GAUGE_H_
#define _WX_UNIV_GAUGE_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/control.h"

// ----------------------------------------------------------------------------
// ibGauge: a progress bar
// ----------------------------------------------------------------------------

class FRONTEND_API ibGauge : public ibControl
{
public:
    ibGauge() { Init(); }

    ibGauge(wxWindow *parent,
            wxWindowID id,
            int range,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxGA_HORIZONTAL,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxGaugeNameStr))
    {
        Init();

        (void)Create(parent, id, range, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                int range,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxGA_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxGaugeNameStr));

    // wxGaugeBase shims (see the SEAM note in control.h)
    virtual void SetRange(int range);
    virtual void SetValue(int pos);
    int GetRange() const { return m_rangeMax; }
    int GetValue() const { return m_gaugePos; }

    // wxUniv-specific methods

    // is it a smooth progress bar or a discrete one?
    bool IsSmooth() const { return (GetWindowStyle() & wxGA_SMOOTH) != 0; }

    // is it a vertica; progress bar or a horizontal one?
    bool IsVertical() const { return (GetWindowStyle() & wxGA_VERTICAL) != 0; }

protected:
    // common part of all ctors
    void Init();

    // return the def border for a progress bar
    virtual wxBorder GetDefaultBorder() const override;

    // return the default size
    virtual wxSize DoGetBestClientSize() const override;

    // draw the control
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // gauge state (lived in wxGaugeBase before the rebase onto ibControl)
    int m_rangeMax;
    int m_gaugePos;

    wxDECLARE_DYNAMIC_CLASS(ibGauge);
};

#endif // _WX_UNIV_GAUGE_H_
