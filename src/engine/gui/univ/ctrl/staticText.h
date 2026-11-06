// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/gui.md,
// the porting workflow).

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

#include "gui/gui.h"

#include "gui/univ/ctrl/control.h"

#include <wx/generic/stattextg.h>

class GUI_API ibStaticText : public ibControl
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

    // SEAM vs univ: a text takes no focus - wxStaticTextBase said so, lost with the rebase onto ibControl; a link does:
    // an action only the mouse reaches is one a keyboard user does not have
    virtual bool AcceptsFocus() const override { return m_isHyperlink; }

    // Drawn as a HYPERLINK: underlined in the theme's link colour, a hand over it; a click - or Space / Enter on its
    // focus - sends wxEVT_BUTTON, as a button does. Whoever owns it decides what opening means.
    void SetHyperlink(bool isHyperlink = true);
    bool IsHyperlink() const { return m_isHyperlink; }

protected:
    // draw the control
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // SEAM vs univ: wxGenericStaticText supplied the best size before the
    // rebase onto ibControl — measure the label ourselves
    virtual wxSize DoGetBestClientSize() const override;

    virtual void WXSetVisibleLabel(const wxString& str);
    virtual wxString WXGetVisibleLabel() const;

    void OnLeftDown(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

private:
    // the link clicked: wxEVT_BUTTON
    void Click();

    bool m_isHyperlink = false;

    wxDECLARE_DYNAMIC_CLASS(ibStaticText);
    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_UNIV_STATTEXT_H_
