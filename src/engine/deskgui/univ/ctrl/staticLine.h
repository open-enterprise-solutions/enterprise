// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statline.h
// Purpose:     ibStaticLine class for wxUniversal
// Author:      Vadim Zeitlin
// Created:     28.06.99
// Copyright:   (c) 1999 Vadim Zeitlin
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_STATICLINE_H_
#define _DESKGUI_UNIV_STATICLINE_H_

#include "deskgui/deskgui.h"

#include <wx/statline.h>      // wxLI_* styles, wxStaticLineNameStr
#include "deskgui/univ/ctrl/control.h"

class DESKGUI_API ibStaticLine : public ibControl
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
    void OnPaint(wxPaintEvent& event);

private:
    wxDECLARE_DYNAMIC_CLASS(ibStaticLine);
    wxDECLARE_EVENT_TABLE();
};

#endif // _DESKGUI_UNIV_STATICLINE_H_

