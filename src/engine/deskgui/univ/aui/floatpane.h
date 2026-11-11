// FORKED from wxWidgets' AUI (include/wx/aui/floatpane.h of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/aui/floatpane.h
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2005-05-17
// Copyright:   (C) Copyright 2005, Kirix Corporation, All Rights Reserved.
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_AUI_FLOATPANE_H_
#define _DESKGUI_UNIV_AUI_FLOATPANE_H_

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/defs.h"

#include "deskgui/deskgui.h"


#include "wx/weakref.h"

#include "deskgui/univ/frame.h"
#include "deskgui/univ/aui/framemanager.h"

// SEAM vs wx: THE FLOATING PANE'S FRAME IS THE THEME'S (ibFrame) - wx's was the native mini frame (wxMiniFrame), its
// caption and its close button the system's, the one window of the system's over the theme's. A tool window still, as
// the mini frame was (wxFRAME_TOOL_WINDOW): over its owner, out of the task bar and of Alt+Tab
class DESKGUI_API ibAuiFloatingFrame : public ibFrame
{
public:
    ibAuiFloatingFrame(wxWindow* parent,
                   ibAuiManager* ownerMgr,
                   const ibAuiPaneInfo& pane,
                   wxWindowID id = wxID_ANY,
                   long style = wxRESIZE_BORDER | wxSYSTEM_MENU | wxCAPTION |
                                wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT |
                                wxCLIP_CHILDREN
                   );
    virtual ~ibAuiFloatingFrame();
    void SetPaneWindow(const ibAuiPaneInfo& pane);
    ibAuiManager* GetOwnerManager() const;

    // Allow processing accelerators to the parent frame
    virtual bool IsTopNavigationDomain(NavigationKind kind) const override;

    ibAuiManager& GetAuiManager()  { return m_mgr; }

    // Override to prevent leaving dangling pointers.
    virtual bool Destroy() override;

protected:
    virtual void OnMoveStart();
    virtual void OnMoving(const wxRect& windowRect, wxDirection dir);
    virtual void OnMoveFinished();

private:
    void CleanUp();
    bool DockPane();
    void OnSize(wxSizeEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnMoveEvent(wxMoveEvent& event);
    void OnLeftDClick(wxMouseEvent& event);
    void OnIdle(wxIdleEvent& event);
    void OnActivate(wxActivateEvent& event);
    static bool isMouseDown();

#ifdef __WXMSW__
    virtual bool MSWHandleMessage(WXLRESULT* result,
                                  WXUINT message,
                                  WXWPARAM wParam,
                                  WXLPARAM lParam) override;
#endif

private:
    wxWindow* m_paneWindow;    // pane window being managed
    bool m_solidDrag;          // true if system uses solid window drag
    bool m_moving;
    wxRect m_lastRect;
    wxRect m_last2Rect;
    wxRect m_last3Rect;
    wxSize m_lastSize;
    wxDirection m_lastDirection;

    wxWeakRef<ibAuiManager> m_ownerMgr;
    ibAuiManager m_mgr;

#ifndef SWIG
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_CLASS(ibAuiFloatingFrame);
#endif // SWIG
};

#endif //_DESKGUI_UNIV_AUI_FLOATPANE_H_

