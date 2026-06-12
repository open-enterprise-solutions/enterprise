// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statline.h
// Purpose:     ibStaticLine class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     28.06.99
// Copyright:   (c) 1999 Vadim Zeitlin
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_STATLINE_H_
#define _WX_UNIV_STATLINE_H_

#include "frontend/frontend.h"

#include <wx/statline.h>      // wxLI_* styles, wxStaticLineNameStr
#include "frontend/uikit/ctrl/control.h"

class FRONTEND_API ibStaticLine : public ibControl
{
public:
    // wxStaticLineBase shim
    bool IsVertical() const { return (GetWindowStyle() & wxLI_VERTICAL) != 0; }

    // constructors and pseudo-constructors
    ibStaticLine() = default;

    ibStaticLine(wxWindow *parent,
                 const wxPoint &pos,
                 wxCoord length,
                 long style = wxLI_HORIZONTAL)
    {
        Create(parent, wxID_ANY, pos,
               style & wxLI_VERTICAL ? wxSize(wxDefaultCoord, length)
                                     : wxSize(length, wxDefaultCoord),
               style);
    }

    ibStaticLine(wxWindow *parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint &pos = wxDefaultPosition,
                 const wxSize &size = wxDefaultSize,
                 long style = wxLI_HORIZONTAL,
                 const wxString &name = wxASCII_STR(wxStaticLineNameStr) )
    {
        Create(parent, id, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize,
                long style = wxLI_HORIZONTAL,
                const wxString &name = wxASCII_STR(wxStaticLineNameStr) );

protected:
    virtual void DoDraw(ibControlRenderer *renderer) override;

private:
    wxDECLARE_DYNAMIC_CLASS(ibStaticLine);
};

#endif // _WX_UNIV_STATLINE_H_

