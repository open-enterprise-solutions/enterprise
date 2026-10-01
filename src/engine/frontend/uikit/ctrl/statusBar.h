// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statusbr.h
// Purpose:     ibStatusBarUniv: ibStatusBar for wxUniversal declaration
// Author:      Vadim Zeitlin
// Created:     14.10.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_STATUSBR_H_
#define _WX_UNIV_STATUSBR_H_

#include "frontend/frontend.h"

class ibWindow;

#include "frontend/uikit/inputConsumer.h"
#include <wx/arrstr.h>

// ----------------------------------------------------------------------------
// ibStatusBarUniv: a window near the bottom of the frame used for status info
// ----------------------------------------------------------------------------

class FRONTEND_API ibStatusBarUniv : public wxStatusBarBase
{
public:
    ibStatusBarUniv() { Init(); }

    ibStatusBarUniv(wxWindow *parent,
                    wxWindowID id = wxID_ANY,
                    long style = wxSTB_DEFAULT_STYLE,
                    const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Init();

        (void)Create(parent, id, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                long style = wxSTB_DEFAULT_STYLE,
                const wxString& name = wxASCII_STR(wxPanelNameStr));

    // implement base class methods
    virtual void SetFieldsCount(int number = 1, const int *widths = nullptr) override;
    virtual void SetStatusWidths(int n, const int widths[]) override;

    virtual bool GetFieldRect(int i, wxRect& rect) const override;
    virtual void SetMinHeight(int height) override;

    virtual int GetBorderX() const override;
    virtual int GetBorderY() const override;

    // ibInputConsumer pure virtual
    virtual ibWindow *GetInputWindow() const override
        { return const_cast<ibStatusBar*>(this); }

protected:
    virtual void DoUpdateStatusText(int i) override;

    // recalculate the field widths
    void OnSize(wxSizeEvent& event);

    // draw the statusbar
    virtual void DoDraw(ibControlRenderer *renderer) override;

    // tell them about our preferred height
    virtual wxSize DoGetBestSize() const override;

    // override DoSetSize() to prevent the status bar height from changing
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;

    // get the (fixed) status bar height
    wxCoord GetHeight() const;

    // get the rectangle containing all the fields and the border between them
    //
    // also updates m_widthsAbs if necessary
    wxRect GetTotalFieldRect(wxCoord *borderBetweenFields);

    // get the rect for this field without ani side effects (see code)
    wxRect DoGetFieldRect(int n) const;

    // common part of all ctors
    void Init();

private:
    // the current status fields strings
    //wxArrayString m_statusText;

    // the absolute status fields widths
    wxArrayInt m_widthsAbs;

    wxDECLARE_DYNAMIC_CLASS(ibStatusBarUniv);
    wxDECLARE_EVENT_TABLE();
    WX_DECLARE_INPUT_CONSUMER()
};

#endif // _WX_UNIV_STATUSBR_H_

