// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/stattext.h
// Purpose:     ibStaticText
// Author:      Vadim Zeitlin
// Created:     14.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_STATTEXT_H_
#define _WX_UNIV_STATTEXT_H_

#include "frontend/frontend.h"

#include "frontend/uikit/ctrl/control.h"

#include <wx/generic/stattextg.h>

class FRONTEND_API ibStaticText : public ibControl
{
public:
    ibStaticText() = default;

    // usual ctor
    ibStaticText(wxWindow *parent,
                 const wxString& label,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize)
    {
        Create(parent, wxID_ANY, label, pos, size, 0, wxASCII_STR(wxStaticTextNameStr));
    }

    // full form
    ibStaticText(wxWindow *parent,
                 wxWindowID id,
                 const wxString& label,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = 0,
                 const wxString &name = wxASCII_STR(wxStaticTextNameStr))
    {
        Create(parent, id, label, pos, size, style, name);
    }

    // function ctor
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString &label,
                const wxPoint &pos = wxDefaultPosition,
                const wxSize &size = wxDefaultSize,
                long style = 0,
                const wxString &name = wxASCII_STR(wxStaticTextNameStr));

    // implementation only from now on

    virtual void SetLabel(const wxString& label) override;

    virtual bool IsFocused() const override { return false; }

protected:
    // draw the control
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // SEAM vs univ: wxGenericStaticText supplied the best size before the
    // rebase onto ibControl — measure the label ourselves
    virtual wxSize DoGetBestClientSize() const override;

    virtual void WXSetVisibleLabel(const wxString& str);
    virtual wxString WXGetVisibleLabel() const;

    wxDECLARE_DYNAMIC_CLASS(ibStaticText);
};

#endif // _WX_UNIV_STATTEXT_H_
