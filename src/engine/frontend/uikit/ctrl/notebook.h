// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/notebook.h
// Purpose:     universal version of ibNotebook
// Author:      Vadim Zeitlin
// Created:     01.02.01
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_NOTEBOOK_H_
#define _WX_UNIV_NOTEBOOK_H_

#include "frontend/frontend.h"

#include <wx/arrstr.h>

class ibSpinButton;

// ----------------------------------------------------------------------------
// the actions supported by this control
// ----------------------------------------------------------------------------

// change the page: to the next/previous/given one
#define ibACTION_NOTEBOOK_NEXT      wxT("nexttab")
#define ibACTION_NOTEBOOK_PREV      wxT("prevtab")
#define ibACTION_NOTEBOOK_GOTO      wxT("gototab")

// ----------------------------------------------------------------------------
// ibNotebook
// ----------------------------------------------------------------------------

#include <wx/notebook.h>      // wxNotebookBase API, wxNB_* styles
#include "frontend/uikit/ctrl/control.h"

// SEAM vs univ: wxNotebookBase rides the native wxControl chain; the book
// page bookkeeping (wxBookCtrlBase) is reimplemented via shims below
class FRONTEND_API ibNotebook : public ibControl, public wxWithImages
{
public:
    // ---- wxBookCtrlBase shims (see the SEAM note above) ------------------
    enum { SetSelection_SendEvent = 1 };

    size_t GetPageCount() const { return m_pages.size(); }
    wxWindow *GetPage(size_t n) const {
        wxCHECK_MSG( n < m_pages.size(), nullptr, wxT("invalid notebook page") );
        return m_pages[n];
    }

    bool AddPage(wxWindow *page, const wxString& text,
                 bool bSelect = false, int imageId = NO_IMAGE) {
        return InsertPage(GetPageCount(), page, text, bSelect, imageId);
    }

    bool SendPageChangingEvent(int nPage) {
        wxBookCtrlEvent event(wxEVT_NOTEBOOK_PAGE_CHANGING, GetId());
        event.SetSelection(nPage);
        event.SetOldSelection(m_selection);
        event.SetEventObject(this);
        return !GetEventHandler()->ProcessEvent(event) || event.IsAllowed();
    }

    void SendPageChangedEvent(int nPageOld, int nPageNew = -1) {
        wxBookCtrlEvent event(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId());
        event.SetSelection(nPageNew == -1 ? m_selection : nPageNew);
        event.SetOldSelection(nPageOld);
        event.SetEventObject(this);
        GetEventHandler()->ProcessEvent(event);
    }
    wxDirection GetTabOrientation() const {
        const long style = GetWindowStyle();
        if ( style & wxNB_BOTTOM ) return wxBOTTOM;
        if ( style & wxNB_LEFT )   return wxLEFT;
        if ( style & wxNB_RIGHT )  return wxRIGHT;
        return wxTOP;
    }

    int GetNextPage(bool forward) const {
        const int count = (int)GetPageCount();
        if ( count == 0 )
            return wxNOT_FOUND;
        int page = m_selection == wxNOT_FOUND ? 0
                 : (m_selection + (forward ? 1 : count - 1)) % count;
        return page;
    }
    // ----------------------------------------------------------------------
    // ctors and such
    // --------------

    ibNotebook() { Init(); }

    ibNotebook(wxWindow *parent,
               wxWindowID id,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxASCII_STR(wxNotebookNameStr))
    {
        Init();

        (void)Create(parent, id, pos, size, style, name);
    }

    // quasi ctor
    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxNotebookNameStr));

    // dtor
    virtual ~ibNotebook();

    // implement wxNotebookBase pure virtuals
    // --------------------------------------

    virtual int SetSelection(size_t nPage)  { return DoSetSelection(nPage, SetSelection_SendEvent); }

    // changes selected page without sending events
    int ChangeSelection(size_t nPage)  { return DoSetSelection(nPage); }

    virtual bool SetPageText(size_t nPage, const wxString& strText) ;
    virtual wxString GetPageText(size_t nPage) const ;

    virtual int GetPageImage(size_t nPage) const ;
    virtual bool SetPageImage(size_t nPage, int nImage) ;

    virtual void SetPageSize(const wxSize& size) ;
    virtual void SetPadding(const wxSize& padding) ;
    virtual void SetTabSize(const wxSize& sz) ;

    virtual wxSize CalcSizeFromPage(const wxSize& sizePage) const ;

    virtual bool DeleteAllPages() ;

    virtual bool InsertPage(size_t nPage,
                            wxNotebookPage *pPage,
                            const wxString& strText,
                            bool bSelect = false,
                            int imageId = NO_IMAGE) ;

    // style tests
    // -----------

    // return true if all tabs have the same width
    bool FixedSizeTabs() const { return HasFlag(wxNB_FIXEDWIDTH); }

    // return true if the notebook has tabs at the sidesand not at the top (or
    // bottom) as usual
    bool IsVertical() const;

    // hit testing
    // -----------

    virtual int HitTest(const wxPoint& pt, long *flags = nullptr) const ;

    // input handling
    // --------------

    virtual bool PerformAction(const ibControlAction& action,
                               long numArg = 0l,
                               const wxString& strArg = wxEmptyString) override;

    static ibInputHandler *GetStdInputHandler(ibInputHandler *handlerDef);
    virtual ibInputHandler *DoGetStdInputHandler(ibInputHandler *handlerDef) override
    {
        return GetStdInputHandler(handlerDef);
    }

    // refresh the currently selected tab
    void RefreshCurrent();

    // get the tab rect
    wxRect GetTabRect(size_t page) const ;

protected:
    virtual wxNotebookPage *DoRemovePage(size_t nPage) ;

    // drawing
    virtual void DoDraw(ibControlRenderer *renderer) override;
    void DoDrawTab(wxDC& dc, const wxRect& rect, size_t n);

    // resizing
    virtual void DoMoveWindow(int x, int y, int width, int height) override;
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) override;

    int DoSetSelection(size_t nPage, int flags = 0) ;

    // common part of all ctors
    void Init();

    // resize the tab to fit its title (and icon if any)
    void ResizeTab(int page);

    // recalculate the geometry of the notebook completely
    void Relayout();

    // is the spin button currently shown?
    bool HasSpinBtn() const;

    // calculate last (fully) visible tab: updates m_lastVisible
    void CalcLastVisibleTab();

    // show or hide the spin control for tabs scrolling depending on whether it
    // is needed or not
    void UpdateSpinBtn();

    // position the spin button
    void PositionSpinBtn();

    // refresh the given tab only
    void RefreshTab(int page, bool forceSelected = false);

    // refresh all tabs
    void RefreshAllTabs();

    // get the rectangle containing all tabs
    wxRect GetAllTabsRect() const;

    // get the part occupied by the tabs - slightly smaller than
    // GetAllTabsRect() because the tabs may be indented from it
    wxRect GetTabsPart() const;

    // calculate the tab size (without padding)
    wxSize CalcTabSize(int page) const;

    // get the (cached) size of a tab
    void GetTabSize(int page, wxCoord *w, wxCoord *h) const;

    // get the (cached) width of the tab
    wxCoord GetTabWidth(int page) const
        { return FixedSizeTabs() ? m_widthMax : m_widths[page]; }

    // return true if the tab has an associated image
    bool HasImage(int page) const
        { return HasImageList() && m_images[page] != -1; }

    // get the part of the notebook reserved for the pages (slightly larger
    // than GetPageRect() as we draw a border and leave marginin between)
    wxRect GetPagePart() const;

    // get the page rect in our client coords
    wxRect GetPageRect() const ;

    // get our client size from the page size
    wxSize GetSizeForPage(const wxSize& size) const;

    // scroll the tabs so that the first page shown becomes the given one
    void ScrollTo(size_t page);

    // scroll the tabs so that the first page shown becomes the given one
    void ScrollLastTo(size_t page);

    // the pages titles
    wxArrayString m_titles;

    // the spin button to change the pages
    ibSpinButton *m_spinbtn;

    // the offset of the first page shown (may be changed with m_spinbtn)
    wxCoord m_offset;

    // the first and last currently visible tabs: the name is not completely
    // accurate as m_lastVisible is, in fact, the first tab which is *not*
    // visible: so the visible tabs are those with indexes such that
    // m_firstVisible <= n < m_lastVisible
    size_t m_firstVisible,
           m_lastVisible;

    // the last fully visible item, usually just m_lastVisible - 1 but may be
    // different from it
    size_t m_lastFullyVisible;

    // the height of tabs in a normal notebook or the width of tabs in a
    // notebook with tabs on a side
    wxCoord m_heightTab;

    // the biggest height (or width) of a notebook tab (used only if
    // FixedSizeTabs()) or -1 if not calculated yet
    wxCoord m_widthMax;

    // the cached widths (or heights) of tabs
    wxArrayInt m_widths;

    // the icon indices
    wxArrayInt m_images;

    // the accel indexes for labels
    wxArrayInt m_accels;

    // the padding
    wxSize m_sizePad;

    // wxBookCtrlBase state (lived in the lost base)
    wxVector<wxWindow*> m_pages;
    int m_selection = wxNOT_FOUND;

    wxDECLARE_DYNAMIC_CLASS(ibNotebook);
};

#endif // _WX_UNIV_NOTEBOOK_H_

