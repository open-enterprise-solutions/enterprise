// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically): the status bar
// (src/univ/statusbr.cpp), over the port's own wxStatusBar.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/statusbr.h
// Purpose:     ibStatusBar: wxStatusBar for wxUniversal declaration
// Author:      Vadim Zeitlin
// Created:     14.10.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_STATUSBAR_H_
#define _DESKGUI_UNIV_STATUSBAR_H_

#include "deskgui/deskgui.h"

#include <wx/statusbr.h>

#if wxUSE_STATUSBAR

class ibRenderer;

// ----------------------------------------------------------------------------
// ibStatusBar: a window near the bottom of the frame used for status info
// ----------------------------------------------------------------------------

// SEAM vs univ: there wxStatusBar itself was the univ window. Here, as ibMenuBar from the native wxMenuBar, we derive
// from the PORT's wxStatusBar - the frame keeps speaking to a wxStatusBar* (SetStatusBar, SetStatusText, the menus'
// help) - made without the port's own bar: a plain window, the canon's, whose fields the theme draws, the same on every
// port. The port's bar was a look of its own on each - the native control on MSW, the generic one on GTK, macOS's - and
// the native control painted itself apart from the frame, after it, four times a resize (2026-11-11, measured; Max:
// "the assistant line runs ahead"). The methods of the port's bar that would touch its own are overridden. Not kept: the
// size grip (the theme draws none; the frame's border sizes the window), the fields' tooltips
class DESKGUI_API ibStatusBar : public wxStatusBar
{
public:
    ibStatusBar() = default;

    ibStatusBar(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                long style = wxSTB_DEFAULT_STYLE,
                const wxString& name = wxASCII_STR(wxStatusBarNameStr))
    {
        Create(parent, id, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                long style = wxSTB_DEFAULT_STYLE,
                const wxString& name = wxASCII_STR(wxStatusBarNameStr));

    // implement base class methods
    virtual void SetFieldsCount(int number = 1, const int *widths = nullptr) override;
    virtual void SetStatusWidths(int n, const int widths[]) override;
    virtual void SetStatusStyles(int n, const int styles[]) override;

    virtual bool GetFieldRect(int i, wxRect& rect) const override;
    virtual void SetMinHeight(int height) override;

    virtual int GetBorderX() const override;
    virtual int GetBorderY() const override;

#ifdef __WXMSW__
    // the window's own, not the native bar's (its parts, its grip, the place it keeps)
    virtual WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif // __WXMSW__

protected:
    // the field n in its rectangle - its frame and its text, ellipsized by the bar's style, by the theme
    // (ibRenderer::DrawStatusField)
    virtual void DrawStatusField(wxDC& dc, const wxRect& rect, int n, int flags);

    virtual void DoUpdateStatusText(int i) override;

    // tell them about our preferred height
    virtual wxSize DoGetBestSize() const override;

    // override DoSetSize() to prevent the status bar height from changing
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;

#ifdef __WXMSW__
    // a plain window's style and move, not the native bar's
    virtual WXDWORD MSWGetStyle(long flags, WXDWORD *exstyle = nullptr) const override;
    virtual void DoMoveWindow(int x, int y, int width, int height) override;
#endif // __WXMSW__

private:
    // the background and the fields, in one buffer
    void OnPaint(wxPaintEvent& event);

    // the fields' widths for the client width: the widths asked (SetStatusWidths), the theme's border between the
    // fields left out of them. SEAM vs univ: worked out when asked - the canon kept them, cast its constness off to
    // fill them, and had to be told to forget them
    wxArrayInt GetFieldWidths() const;

    // the rectangle of the field n
    wxRect DoGetFieldRect(int n) const;

    // get the (fixed) status bar height
    wxCoord GetHeight() const;

    ibRenderer *m_renderer = nullptr;

    wxDECLARE_DYNAMIC_CLASS(ibStatusBar);
    wxDECLARE_EVENT_TABLE();
};

#endif // wxUSE_STATUSBAR

#endif // _DESKGUI_UNIV_STATUSBAR_H_
