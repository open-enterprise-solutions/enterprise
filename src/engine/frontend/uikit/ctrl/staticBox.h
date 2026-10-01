// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

//////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statbox.h
// Purpose:     ibStaticBox declaration
// Author:      Vadim Zeitlin
// Created:     15.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_STATBOX_H_
#define _WX_UNIV_STATBOX_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxStaticBoxBase rides the native wxControl chain — derive
// from ibControl directly (NB: not the native wxStaticBox, so this box can't
// host a wxStaticBoxSizer; use a plain sizer inside the box area)
class FRONTEND_API ibStaticBox : public ibControl
{
public:
    ibStaticBox() = default;

    ibStaticBox(wxWindow *parent,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize)
    {
        Create(parent, wxID_ANY, label, pos, size);
    }

    ibStaticBox(wxWindow *parent, wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr))
    {
        Create(parent, id, label, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr));

    // the origin of the static box is inside the border and under the label:
    // take account of this
    virtual wxPoint GetBoxAreaOrigin() const;

    // returning true from here ensures that we act as a container window for
    // our children
    virtual bool IsStaticBox() const override { return true; }

    // the box itself never takes focus (was wxStaticBoxBase behaviour)
    virtual bool AcceptsFocus() const override { return false; }

protected:
    // draw the control
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // get the size of the border
    wxRect GetBorderGeometry() const;

private:
    wxDECLARE_DYNAMIC_CLASS(ibStaticBox);
};

#endif // _WX_UNIV_STATBOX_H_
