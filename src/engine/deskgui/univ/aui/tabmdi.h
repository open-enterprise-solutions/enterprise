// FORKED from wxWidgets' AUI (include/wx/aui/tabmdi.h of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/aui/tabmdi.h
// Purpose:     Generic MDI (Multiple Document Interface) classes
// Author:      Hans Van Leemputten
// Modified by: Benjamin I. Williams / Kirix Corporation
// Created:     29/07/2002
// Copyright:   (c) Hans Van Leemputten
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_AUI_TABMDI_H_
#define _DESKGUI_UNIV_AUI_TABMDI_H_

#include "wx/defs.h"

#include "deskgui/deskgui.h"

#if wxUSE_MDI

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/frame.h"
#include "deskgui/univ/frame.h"
#include "wx/panel.h"
#include "wx/notebook.h"
#include "wx/icon.h"
#include "wx/mdi.h"
#include "deskgui/univ/aui/auibook.h"

//-----------------------------------------------------------------------------
// classes
//-----------------------------------------------------------------------------

class DESKGUI_API ibAuiMDIParentFrame;
class DESKGUI_API ibAuiMDIClientWindow;
class DESKGUI_API ibAuiMDIChildFrame;

//-----------------------------------------------------------------------------
// ibAuiMDIParentFrame
//-----------------------------------------------------------------------------

// SEAM vs wx: the theme's frame - its decorations and its menubar drawn by the theme (ibFrame)
class DESKGUI_API ibAuiMDIParentFrame : public ibFrame
{
public:
    ibAuiMDIParentFrame();
    ibAuiMDIParentFrame(wxWindow *parent,
                        wxWindowID winid,
                        const wxString& title,
                        const wxPoint& pos = wxDefaultPosition,
                        const wxSize& size = wxDefaultSize,
                        long style = wxDEFAULT_FRAME_STYLE | wxVSCROLL | wxHSCROLL,
                        const wxString& name = wxASCII_STR(wxFrameNameStr));

    ~ibAuiMDIParentFrame();

    bool Create(wxWindow *parent,
                wxWindowID winid,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE | wxVSCROLL | wxHSCROLL,
                const wxString& name = wxASCII_STR(wxFrameNameStr) );

    ibAuiNotebook* GetNotebook() const;

#if wxUSE_MENUS
    wxMenu* GetWindowMenu() const { return m_pWindowMenu; }
    void SetWindowMenu(wxMenu* pMenu);

    virtual void SetMenuBar(wxMenuBar *pMenuBar) override;
#endif // wxUSE_MENUS

    void SetChildMenuBar(ibAuiMDIChildFrame *pChild);

    ibAuiMDIChildFrame *GetActiveChild() const;
    void SetActiveChild(ibAuiMDIChildFrame* pChildFrame);

    ibAuiMDIClientWindow *GetClientWindow() const;
    virtual ibAuiMDIClientWindow *OnCreateClient();

    virtual void Cascade() { /* Has no effect */ }
    virtual void Tile(wxOrientation orient = wxHORIZONTAL);
    virtual void ArrangeIcons() { /* Has no effect */ }
    virtual void ActivateNext();
    virtual void ActivatePrevious();

protected:
    ibAuiMDIClientWindow*   m_pClientWindow;
    wxEvent*                m_pLastEvt;

#if wxUSE_MENUS
    wxMenu              *m_pWindowMenu;
    wxMenuBar           *m_pMyMenuBar;
#endif // wxUSE_MENUS

protected:
    void Init();

#if wxUSE_MENUS
    void RemoveWindowMenu(wxMenuBar *pMenuBar);
    void AddWindowMenu(wxMenuBar *pMenuBar);

    void DoHandleMenu(wxCommandEvent &event);
    void DoHandleUpdateUI(wxUpdateUIEvent &event);
#endif // wxUSE_MENUS

    virtual bool ProcessEvent(wxEvent& event) override;

    virtual void DoGetClientSize(int *width, int *height) const override;

private:
    void OnClose(wxCloseEvent& event);

    // close all children, return false if any of them vetoed it
    bool CloseAll();

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibAuiMDIParentFrame);
};

//-----------------------------------------------------------------------------
// ibAuiMDIChildFrame
//-----------------------------------------------------------------------------

class DESKGUI_API ibAuiMDIChildFrame : public wxTDIChildFrame
{
public:
    ibAuiMDIChildFrame();
    ibAuiMDIChildFrame(ibAuiMDIParentFrame *parent,
                       wxWindowID winid,
                       const wxString& title,
                       const wxPoint& pos = wxDefaultPosition,
                       const wxSize& size = wxDefaultSize,
                       long style = wxDEFAULT_FRAME_STYLE,
                       const wxString& name = wxASCII_STR(wxFrameNameStr));

    virtual ~ibAuiMDIChildFrame();
    bool Create(ibAuiMDIParentFrame *parent,
                wxWindowID winid,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr));

#if wxUSE_MENUS
    virtual void SetMenuBar(wxMenuBar *menuBar) override;
    virtual wxMenuBar *GetMenuBar() const override;
#endif // wxUSE_MENUS

    virtual void SetTitle(const wxString& title) override;

    virtual void SetIcons(const wxIconBundle& icons) override;

    virtual void Activate() override;
    virtual bool Destroy() override;

    virtual bool Show(bool show = true) override;

    void OnMenuHighlight(wxMenuEvent& evt);

    void SetMDIParentFrame(ibAuiMDIParentFrame* parent);
    ibAuiMDIParentFrame* GetMDIParentFrame() const;

protected:
    void Init();

    // the bitmap of its page: of its icons (SetIcons), at the small icon's size
    wxBitmap GetPageBitmap() const;

public:
    // This function needs to be called when a size change is confirmed,
    // we needed this function to prevent anybody from the outside
    // changing the panel... it messes the UI layout when we would allow it.
    void ApplyMDIChildFrameRect();

protected:
    ibAuiMDIParentFrame* m_pMDIParentFrame;
    bool m_activateOnCreate;

#if wxUSE_MENUS
    wxMenuBar* m_pMenuBar;
#endif // wxUSE_MENUS



private:
    wxDECLARE_DYNAMIC_CLASS(ibAuiMDIChildFrame);
    wxDECLARE_EVENT_TABLE();

    friend class ibAuiMDIClientWindow;
};

//-----------------------------------------------------------------------------
// ibAuiMDIClientWindow
//-----------------------------------------------------------------------------

class DESKGUI_API ibAuiMDIClientWindow : public ibAuiNotebook
{
public:
    ibAuiMDIClientWindow();
    ibAuiMDIClientWindow(ibAuiMDIParentFrame *parent, long style = 0);

    virtual bool CreateClient(ibAuiMDIParentFrame *parent,
                              long style = wxVSCROLL | wxHSCROLL);

    virtual ibAuiMDIChildFrame* GetActiveChild();
    virtual void SetActiveChild(ibAuiMDIChildFrame* pChildFrame)
    {
        SetSelection(GetPageIndex(pChildFrame));
    }

protected:
    virtual int DoModifySelection(size_t n, bool events) override;

    void PageChanged(int oldSelection, int newSelection);
    void OnPageClose(ibAuiNotebookEvent& evt);
    void OnPageChanged(ibAuiNotebookEvent& evt);

private:
    wxDECLARE_DYNAMIC_CLASS(ibAuiMDIClientWindow);
    wxDECLARE_EVENT_TABLE();
};
#endif // wxUSE_MDI

#endif // _DESKGUI_UNIV_AUI_TABMDI_H_
