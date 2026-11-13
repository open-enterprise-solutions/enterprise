// FORKED from the wx GENERIC scroll helper (wx/generic/scrolwin.h, src/generic/scrlwing.cpp under
// wxHAS_GENERIC_SCROLLWIN: wxScrollHelper -> ibScrollHelper) and the template that puts it on a window (wx/scrolwin.h:
// wxScrolled<T> -> ibScrolled<T>). wx takes a port's own helper where the port has one: the GTK port's drives a
// GtkScrolledWindow and its GtkRange bars, and no window of the library has them - ibWindow takes wxHSCROLL / wxVSCROLL
// off the native style, its bars are the engine's (ibWindow::SetScrollbar). wxUniversal takes the generic helper on
// every port; the library takes it too, under its own name, since the wx headers choose by the port it is built on.
//
// On GTK3 the port's helper read a scrolled window's policy off the bare wxPizza (Gtk-CRITICAL, an uninitialised policy
// back), gave the engine's bars no range - nothing scrolled - and asked GTK for a window of the VIRTUAL size
// (designer-thin under Xvfb, CI, 2026-11-12). On MSW and Cocoa wx took the generic helper already: the same code.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/scrolwin.h, wx/generic/scrolwin.h
// Purpose:     wxScrolledWindow, wxScrolledControl and wxScrollHelper
// Author:      Vadim Zeitlin
// Created:     30.08.00, 2008-12-24
// Copyright:   (c) 2000 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
//              (c) 2008 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_SCROLWIN_H_
#define _DESKGUI_UNIV_SCROLWIN_H_

#include "deskgui/deskgui.h"

#include <wx/scrolwin.h>    // wxScrollHelperBase, wxScrolledT_Helper, wxCreateScrolled
#include <wx/recguard.h>

// ----------------------------------------------------------------------------
// ibScrollHelper: the generic wxScrollHelper
// ----------------------------------------------------------------------------

// SEAM vs wx: the window is not told of its helper (wxWindow::SetScrollHelper) - it takes the port's class, and nothing
// in wx reads it back (GetScrollHelper has no caller).
class DESKGUI_API ibScrollHelper : public wxScrollHelperBase
{
public:
    ibScrollHelper(wxWindow *winToScroll);

    // SEAM vs wx: the bars follow at once, for the window that scrolls itself too - as under __WXUNIVERSAL__
    // (wxScrollHelperBase::SetScrollbars); the native build waits for a size event, and none comes when only the
    // content grew: a popup's list showed no bar over more rows than fit, a text's lines went past its bars
    virtual void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false) override;

    // implement base class pure virtuals
    virtual void AdjustScrollbars() override;
    virtual bool IsScrollbarShown(int orient) const override;

protected:
    virtual void DoScroll(int x, int y) override;
    virtual void DoShowScrollbars(wxScrollbarVisibility horz,
                                  wxScrollbarVisibility vert) override;

private:
    // helper of AdjustScrollbars(): does the work for the single scrollbar
    //
    // notice that the parameters passed by non-const references are modified
    // by this function
    void DoAdjustScrollbar(int orient,
                           int clientSize,
                           int virtSize,
                           int pixelsPerUnit,
                           int& scrollUnits,
                           int& scrollPosition,
                           int& scrollLinesPerPage,
                           wxScrollbarVisibility visibility);


    wxScrollbarVisibility m_xVisibility,
                          m_yVisibility;
    wxRecursionGuardFlag m_adjustScrollFlagReentrancy;

    wxDECLARE_NO_COPY_CLASS(ibScrollHelper);
};

// ----------------------------------------------------------------------------
// ibScrolled<T>: a window of the library which knows how to scroll
// ----------------------------------------------------------------------------

// SEAM vs wx: wxScrolledT_Helper stays the base for its MSW message filter (the arrows, for scrolling); its
// FilterBestSize takes the port's helper, so its body is in DoGetBestSize below.
template<class T>
class ibScrolled : public T,
                   public ibScrollHelper,
                   private wxScrolledT_Helper
{
public:
    ibScrolled() : ibScrollHelper(this) { }
    ibScrolled(wxWindow *parent,
               wxWindowID winid = wxID_ANY,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = wxScrolledWindowStyle,
               const wxString& name = wxASCII_STR(wxPanelNameStr))
        : ibScrollHelper(this)
    {
        Create(parent, winid, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID winid,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxScrolledWindowStyle,
                const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        m_targetWindow = this;

#ifdef __WXMAC__
        this->MacSetClipChildren();
#endif

        // by default, we're scrollable in both directions (but if one of the
        // styles is specified explicitly, we shouldn't add the other one
        // automatically)
        if ( !(style & (wxHSCROLL | wxVSCROLL)) )
            style |= wxHSCROLL | wxVSCROLL;

        return wxCreateScrolled((T*)this, parent, winid, pos, size, style, name);
    }

    // SEAM vs wx: THE CHILDREN ARE PLACED AT ONCE, not deferred to the end of the size (wxMSW, DeferWindowPos). One size
    // both scrolls them - the engine's ScrollWindow moves each (ibWindow) - and lays them out, twice: a field stood in one
    // batch five times, and the system moves a deferred window's OWN children by every one of its entries, each from
    // where it stood before the batch - a form widened after a scroll put each field's buttons outside it, by the sum of
    // the steps (2026-11-13, measured: a field moved (5,5), its buttons (35,25)). On GTK3/Cocoa nothing is deferred
    virtual bool BeginRepositioningChildren() override { return false; }

#ifdef __WXMSW__
    // we need to return a special WM_GETDLGCODE value to process just the
    // arrows but let the other navigation characters through
    virtual WXLRESULT MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) override
    {
        return FilterMSWWindowProc(nMsg, T::MSWWindowProc(nMsg, wParam, lParam));
    }

    // Take into account the scroll origin.
    virtual void MSWAdjustBrushOrg(int* xOrg, int* yOrg) const override
    {
        CalcUnscrolledPosition(*xOrg, *yOrg, xOrg, yOrg);
    }
#endif // __WXMSW__

    WX_FORWARD_TO_SCROLL_HELPER()

protected:
    virtual wxSize DoGetBestSize() const override
    {
        // NB: We don't do this in WX_FORWARD_TO_SCROLL_HELPER, because not
        //     all scrollable windows should behave like this, only those that
        //     contain children controls within scrollable area
        //     (i.e., wxScrolledWindow) and other some scrollable windows may
        //     have different DoGetBestSize() implementation (e.g. wxTreeCtrl).

        wxSize best = T::DoGetBestSize();

        if ( this->GetAutoLayout() )
        {
            // Only use the content to set the window size in the direction
            // where there's no scrolling; otherwise we're going to get a huge
            // window in the direction in which scrolling is enabled
            int ppuX, ppuY;
            GetScrollPixelsPerUnit(&ppuX, &ppuY);

            // minimal size as specified: see wxScrolledT_Helper::FilterBestSize
            const wxSize minSize = this->GetMinSize();

            if ( ppuX > 0 )
                best.x = minSize.x + this->GetScrollbarSize(wxVERTICAL);

            if ( ppuY > 0 )
                best.y = minSize.y + this->GetScrollbarSize(wxHORIZONTAL);
        }

        return best;
    }

private:
    wxDECLARE_NO_COPY_CLASS(ibScrolled);
};

#endif // _DESKGUI_UNIV_SCROLWIN_H_
