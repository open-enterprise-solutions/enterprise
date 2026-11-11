// FORKED from wxWidgets' AUI (src/aui/tabmdi.cpp of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        src/aui/tabmdi.cpp
// Purpose:     Generic MDI (Multiple Document Interface) classes
// Author:      Hans Van Leemputten
// Modified by: Benjamin I. Williams / Kirix Corporation
// Created:     29/07/2002
// Copyright:   (c) Hans Van Leemputten
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#if wxUSE_MDI

#include "deskgui/univ/aui/tabmdi.h"

#ifndef WX_PRECOMP
    #include "wx/panel.h"
    #include "wx/menu.h"
    #include "wx/intl.h"
    #include "wx/log.h"
    #include "wx/settings.h"
#endif //WX_PRECOMP

#include "wx/stockitem.h"
#include "wx/wupdlock.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/theme.h"

enum MDI_MENU_ID
{
    wxWINDOWCLOSE = 4001,
    wxWINDOWCLOSEALL,
    wxWINDOWNEXT,
    wxWINDOWPREV
};

//-----------------------------------------------------------------------------
// ibAuiMDIParentFrame
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibAuiMDIParentFrame, ibFrame);

wxBEGIN_EVENT_TABLE(ibAuiMDIParentFrame, ibFrame)
    EVT_CLOSE(ibAuiMDIParentFrame::OnClose)
#if wxUSE_MENUS
    EVT_MENU (wxID_ANY, ibAuiMDIParentFrame::DoHandleMenu)
    EVT_UPDATE_UI (wxID_ANY, ibAuiMDIParentFrame::DoHandleUpdateUI)
#endif
wxEND_EVENT_TABLE()

ibAuiMDIParentFrame::ibAuiMDIParentFrame()
{
    Init();
}

ibAuiMDIParentFrame::ibAuiMDIParentFrame(wxWindow *parent,
                                         wxWindowID id,
                                         const wxString& title,
                                         const wxPoint& pos,
                                         const wxSize& size,
                                         long style,
                                         const wxString& name)
{
    Init();
    (void)Create(parent, id, title, pos, size, style, name);
}

ibAuiMDIParentFrame::~ibAuiMDIParentFrame()
{
    // Avoid having GetActiveChild() called after m_pClientWindow is destroyed
    SendDestroyEvent();
    // Make sure the client window is destructed before the menu bars are!
    wxDELETE(m_pClientWindow);

#if wxUSE_MENUS
    wxDELETE(m_pMyMenuBar);
    RemoveWindowMenu(GetMenuBar());
    wxDELETE(m_pWindowMenu);
#endif // wxUSE_MENUS
}

bool ibAuiMDIParentFrame::Create(wxWindow *parent,
                                 wxWindowID id,
                                 const wxString& title,
                                 const wxPoint& pos,
                                 const wxSize& size,
                                 long style,
                                 const wxString& name)
{
#if wxUSE_MENUS
    // this style can be used to prevent a window from having the standard MDI
    // "Window" menu
    if (!(style & wxFRAME_NO_WINDOW_MENU))
    {
        // SEAM vs wx: a menu of the theme's menubar is the theme's (ibMenuBar)
        m_pWindowMenu = new ibMenu;
        m_pWindowMenu->Append(wxWINDOWCLOSE,    _("Cl&ose"));
        m_pWindowMenu->Append(wxWINDOWCLOSEALL, _("Close All"));
        m_pWindowMenu->AppendSeparator();
        m_pWindowMenu->Append(wxWINDOWNEXT,     _("&Next"));
        m_pWindowMenu->Append(wxWINDOWPREV,     _("&Previous"));
    }
#endif // wxUSE_MENUS

    if ( !ibFrame::Create(parent, id, title, pos, size, style, name) )
        return false;

    m_pClientWindow = OnCreateClient();
    return m_pClientWindow != nullptr;
}


ibAuiNotebook* ibAuiMDIParentFrame::GetNotebook() const
{
    return static_cast<ibAuiNotebook*>(m_pClientWindow);
}



#if wxUSE_MENUS
void ibAuiMDIParentFrame::SetWindowMenu(wxMenu* pMenu)
{
    // Replace the window menu from the currently loaded menu bar.
    wxMenuBar *pMenuBar = GetMenuBar();

    if (m_pWindowMenu)
    {
        RemoveWindowMenu(pMenuBar);
        wxDELETE(m_pWindowMenu);
    }

    if (pMenu)
    {
        m_pWindowMenu = pMenu;
        AddWindowMenu(pMenuBar);
    }
}

void ibAuiMDIParentFrame::SetMenuBar(wxMenuBar* pMenuBar)
{
    // Remove the Window menu from the old menu bar
    RemoveWindowMenu(GetMenuBar());

    // Add the Window menu to the new menu bar.
    AddWindowMenu(pMenuBar);

    ibFrame::SetMenuBar(pMenuBar);
    //m_pMyMenuBar = GetMenuBar();
}
#endif // wxUSE_MENUS

void ibAuiMDIParentFrame::SetChildMenuBar(ibAuiMDIChildFrame* pChild)
{
#if wxUSE_MENUS
    if (!pChild)
    {
        // No Child, set Our menu bar back.
        if (m_pMyMenuBar)
            SetMenuBar(m_pMyMenuBar);
        else
            SetMenuBar(GetMenuBar());

        // Make sure we know our menu bar is in use
        m_pMyMenuBar = nullptr;
    }
    else
    {
        if (pChild->GetMenuBar() == nullptr)
            return;

        // Do we need to save the current bar?
        if (m_pMyMenuBar == nullptr)
            m_pMyMenuBar = GetMenuBar();

        SetMenuBar(pChild->GetMenuBar());
    }
#endif // wxUSE_MENUS
}

bool ibAuiMDIParentFrame::ProcessEvent(wxEvent& event)
{
    // stops the same event being processed repeatedly
    if (m_pLastEvt == &event)
        return false;
    m_pLastEvt = &event;

    // let the active child (if any) process the event first.
    bool res = false;
    ibAuiMDIChildFrame* pActiveChild = GetActiveChild();
    if (pActiveChild &&
        event.IsCommandEvent() &&
        event.GetEventObject() != m_pClientWindow &&
           !(event.GetEventType() == wxEVT_ACTIVATE ||
             event.GetEventType() == wxEVT_SET_FOCUS ||
             event.GetEventType() == wxEVT_KILL_FOCUS ||
             event.GetEventType() == wxEVT_CHILD_FOCUS ||
             event.GetEventType() == wxEVT_COMMAND_SET_FOCUS ||
             event.GetEventType() == wxEVT_COMMAND_KILL_FOCUS )
       )
    {
        res = pActiveChild->GetEventHandler()->ProcessEvent(event);
    }

    if (!res)
    {
        // if the event was not handled this frame will handle it,
        // which is why we need the protection code at the beginning
        // of this method
        res = wxEvtHandler::ProcessEvent(event);
    }

    m_pLastEvt = nullptr;

    return res;
}

void ibAuiMDIParentFrame::OnClose(wxCloseEvent& event)
{
    if (!CloseAll())
        event.Veto();
    else
        event.Skip();
}

bool ibAuiMDIParentFrame::CloseAll()
{
    ibAuiMDIChildFrame* pActiveChild;
    while ((pActiveChild = GetActiveChild()) != nullptr)
    {
        if (!pActiveChild->Close())
        {
            // it refused to close, don't close the remaining ones either
            return false;
        }
    }

    return true;
}

ibAuiMDIChildFrame *ibAuiMDIParentFrame::GetActiveChild() const
{
    // We can be called before the client window is created, so check for its
    // existence.
    ibAuiMDIClientWindow* const client = GetClientWindow();
    return client ? client->GetActiveChild() : nullptr;
}

void ibAuiMDIParentFrame::SetActiveChild(ibAuiMDIChildFrame* pChildFrame)
{
    ibAuiMDIClientWindow* const client = GetClientWindow();
    if (client && client->GetActiveChild() != pChildFrame)
    {
        client->SetActiveChild(pChildFrame);
    }
}

ibAuiMDIClientWindow *ibAuiMDIParentFrame::GetClientWindow() const
{
    return m_pClientWindow;
}

ibAuiMDIClientWindow *ibAuiMDIParentFrame::OnCreateClient()
{
    return new ibAuiMDIClientWindow( this );
}

void ibAuiMDIParentFrame::ActivateNext()
{
    if (m_pClientWindow && m_pClientWindow->GetSelection() != wxNOT_FOUND)
    {
        size_t active = m_pClientWindow->GetSelection() + 1;
        if (active >= m_pClientWindow->GetPageCount())
            active = 0;

        m_pClientWindow->SetSelection(active);
    }
}

void ibAuiMDIParentFrame::ActivatePrevious()
{
    if (m_pClientWindow && m_pClientWindow->GetSelection() != wxNOT_FOUND)
    {
        int active = m_pClientWindow->GetSelection() - 1;
        if (active < 0)
            active = m_pClientWindow->GetPageCount() - 1;

        m_pClientWindow->SetSelection(active);
    }
}

void ibAuiMDIParentFrame::Init()
{
    m_pLastEvt = nullptr;
    m_pClientWindow = nullptr;
#if wxUSE_MENUS
    m_pWindowMenu = nullptr;
    m_pMyMenuBar = nullptr;
#endif // wxUSE_MENUS
}

#if wxUSE_MENUS
void ibAuiMDIParentFrame::RemoveWindowMenu(wxMenuBar* pMenuBar)
{
    if (pMenuBar && m_pWindowMenu)
    {
        // Remove old window menu
        int pos = pMenuBar->FindMenu(_("&Window"));
        if (pos != wxNOT_FOUND)
        {
            // DBG:: We're going to delete the wrong menu!!!
            wxASSERT(m_pWindowMenu == pMenuBar->GetMenu(pos));
            pMenuBar->Remove(pos);
        }
    }
}

void ibAuiMDIParentFrame::AddWindowMenu(wxMenuBar *pMenuBar)
{
    if (pMenuBar && m_pWindowMenu)
    {
        int pos = pMenuBar->FindMenu(wxGetStockLabel(wxID_HELP,wxSTOCK_NOFLAGS));
        if (pos == wxNOT_FOUND)
            pMenuBar->Append(m_pWindowMenu, _("&Window"));
        else
            pMenuBar->Insert(pos, m_pWindowMenu, _("&Window"));
    }
}

void ibAuiMDIParentFrame::DoHandleMenu(wxCommandEvent& event)
{
    switch (event.GetId())
    {
        case wxWINDOWCLOSE:
        {
            ibAuiMDIChildFrame* pActiveChild = GetActiveChild();
            if (pActiveChild)
                pActiveChild->Close();
            break;
        }
        case wxWINDOWCLOSEALL:
        {
            CloseAll();
            break;
        }
        case wxWINDOWNEXT:
            ActivateNext();
            break;
        case wxWINDOWPREV:
            ActivatePrevious();
            break;
        default:
            event.Skip();
    }
}

void ibAuiMDIParentFrame::DoHandleUpdateUI(wxUpdateUIEvent& event)
{
    switch (event.GetId())
    {
        case wxWINDOWCLOSE:
        case wxWINDOWCLOSEALL:
        {
            ibAuiMDIClientWindow* client_window = GetClientWindow();
            wxCHECK_RET(client_window, wxS("Missing MDI Client Window"));
            size_t pages = client_window->GetPageCount();
            event.Enable(pages >= 1);
            break;
        }

        case wxWINDOWNEXT:
        case wxWINDOWPREV:
        {
            ibAuiMDIClientWindow* client_window = GetClientWindow();
            wxCHECK_RET(client_window, wxS("Missing MDI Client Window"));
            size_t pages = client_window->GetPageCount();
            event.Enable(pages >= 2);
            break;
        }

        default:
            event.Skip();
    }
}
#endif // wxUSE_MENUS

void ibAuiMDIParentFrame::DoGetClientSize(int* width, int* height) const
{
    ibFrame::DoGetClientSize(width, height);
}

void ibAuiMDIParentFrame::Tile(wxOrientation orient)
{
    ibAuiMDIClientWindow* client_window = GetClientWindow();
    wxASSERT_MSG(client_window, wxT("Missing MDI Client Window"));

    int cur_idx = client_window->GetSelection();
    if (cur_idx == -1)
        return;

    if (orient == wxVERTICAL)
    {
        client_window->Split(cur_idx, wxLEFT);
    }
    else if (orient == wxHORIZONTAL)
    {
        client_window->Split(cur_idx, wxTOP);
    }
}


//-----------------------------------------------------------------------------
// ibAuiMDIChildFrame
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibAuiMDIChildFrame, wxFrame);

wxBEGIN_EVENT_TABLE(ibAuiMDIChildFrame, wxFrame)
    EVT_MENU_HIGHLIGHT_ALL(ibAuiMDIChildFrame::OnMenuHighlight)
wxEND_EVENT_TABLE()

ibAuiMDIChildFrame::ibAuiMDIChildFrame()
{
    Init();
}

ibAuiMDIChildFrame::ibAuiMDIChildFrame(ibAuiMDIParentFrame *parent,
                                       wxWindowID id,
                                       const wxString& title,
                                       const wxPoint& WXUNUSED(pos),
                                       const wxSize& size,
                                       long style,
                                       const wxString& name)
{
    Init();

    // There are two ways to create a tabbed mdi child frame without
    // making it the active document.  Either Show(false) can be called
    // before Create() (as is customary on some ports with wxFrame-type
    // windows), or wxMINIMIZE can be passed in the style flags.  Note that
    // ibAuiMDIChildFrame is not really derived from wxFrame, as wxMDIChildFrame
    // is, but those are the expected semantics.  No style flag is passed
    // onto the panel underneath.
    if (style & wxMINIMIZE)
        m_activateOnCreate = false;

    Create(parent, id, title, wxDefaultPosition, size, 0, name);
}

ibAuiMDIChildFrame::~ibAuiMDIChildFrame()
{
    ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
    if (pParentFrame)
    {
        if (pParentFrame->GetActiveChild() == this)
        {
            pParentFrame->SetActiveChild(nullptr);
            pParentFrame->SetChildMenuBar(nullptr);
        }
        ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
        wxASSERT(pClientWindow);
        int idx = pClientWindow->GetPageIndex(this);
        if (idx != wxNOT_FOUND)
        {
            pClientWindow->RemovePage(idx);
        }
    }

#if wxUSE_MENUS
    wxDELETE(m_pMenuBar);
#endif // wxUSE_MENUS
}

bool ibAuiMDIChildFrame::Create(ibAuiMDIParentFrame* parent,
                                wxWindowID id,
                                const wxString& title,
                                const wxPoint& WXUNUSED(pos),
                                const wxSize& size,
                                long style,
                                const wxString& name)
{
    ibAuiMDIClientWindow* pClientWindow = parent->GetClientWindow();
    wxASSERT_MSG((pClientWindow != nullptr), wxT("Missing MDI client window."));

    // see comment in constructor
    if (style & wxMINIMIZE)
        m_activateOnCreate = false;

    // create the window hidden to prevent flicker
    wxWindow::Show(false);
    wxWindow::Create(pClientWindow,
                    id,
                    wxDefaultPosition,
                    size,
                    wxNO_BORDER, name);

    SetMDIParentFrame(parent);

    m_title = title;

    // SEAM vs wx: no page yet - the child joins the notebook when it is shown (Show), built: a page added here stood
    // empty in the window while its form was made in it

    return true;
}

bool ibAuiMDIChildFrame::Destroy()
{
    ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
    wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

    ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
    wxASSERT_MSG(pClientWindow, wxT("Missing MDI Client Window"));

    if (pParentFrame->GetActiveChild() == this)
    {
        // deactivate ourself
        wxActivateEvent event(wxEVT_ACTIVATE, false, GetId());
        event.SetEventObject(this);
        GetEventHandler()->ProcessEvent(event);

        pParentFrame->SetChildMenuBar(nullptr);
    }

    pClientWindow->RemovePage(pClientWindow->FindPage(this));

    // This is a child window, so we need to delete it immediately instead of
    // postponing it until idle time as we do with real TLWs.
    delete this;

    return true;
}

#if wxUSE_MENUS
void ibAuiMDIChildFrame::SetMenuBar(wxMenuBar *menu_bar)
{
    wxMenuBar *pOldMenuBar = m_pMenuBar;
    m_pMenuBar = menu_bar;

    if (m_pMenuBar)
    {
        ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
        wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

        m_pMenuBar->SetParent(pParentFrame);
        if (pParentFrame->GetActiveChild() == this)
        {
            // replace current menu bars
            if (pOldMenuBar)
                pParentFrame->SetChildMenuBar(nullptr);
            pParentFrame->SetChildMenuBar(this);
        }
    }
}

wxMenuBar *ibAuiMDIChildFrame::GetMenuBar() const
{
    return m_pMenuBar;
}
#endif // wxUSE_MENUS

void ibAuiMDIChildFrame::SetTitle(const wxString& title)
{
    m_title = title;

    ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
    wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

    ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
    if (pClientWindow != nullptr)
    {
        size_t pos;
        for (pos = 0; pos < pClientWindow->GetPageCount(); pos++)
        {
            if (pClientWindow->GetPage(pos) == this)
            {
                pClientWindow->SetPageText(pos, m_title);
                break;
            }
        }
    }
}

void ibAuiMDIChildFrame::SetIcons(const wxIconBundle& icons)
{
    // SEAM vs wx: kept (wxTDIChildFrame keeps none) - the page made when the child is shown takes its bitmap from them
    m_icons = icons;

    ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
    wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

    ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
    if (pClientWindow != nullptr)
    {
        int idx = pClientWindow->GetPageIndex(this);

        if (idx != -1)
        {
            pClientWindow->SetPageBitmap((size_t)idx, GetPageBitmap());
        }
    }
}

wxBitmap ibAuiMDIChildFrame::GetPageBitmap() const
{
    const wxSize sizeIcon(wxSystemSettings::GetMetric(wxSYS_SMALLICON_X, this),
                          wxSystemSettings::GetMetric(wxSYS_SMALLICON_Y, this));
    wxBitmap bmp;
    if ( m_icons.IsOk() )
        bmp.CopyFromIcon(m_icons.GetIcon(sizeIcon));
    return bmp;
}

void ibAuiMDIChildFrame::Activate()
{
    ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
    wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

    ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();

    if (pClientWindow != nullptr)
    {
        size_t pos;
        for (pos = 0; pos < pClientWindow->GetPageCount(); pos++)
        {
            if (pClientWindow->GetPage(pos) == this)
            {
                pClientWindow->SetSelection(pos);
                break;
            }
        }
    }
}

void ibAuiMDIChildFrame::OnMenuHighlight(wxMenuEvent& event)
{
#if wxUSE_STATUSBAR
    if (m_pMDIParentFrame)
    {
        // we don't have any help text for this item,
        // but may be the MDI frame does?
        m_pMDIParentFrame->OnMenuHighlight(event);
    }
#else
    wxUnusedVar(event);
#endif // wxUSE_STATUSBAR
}

void ibAuiMDIChildFrame::SetMDIParentFrame(ibAuiMDIParentFrame* parentFrame)
{
    m_pMDIParentFrame = parentFrame;
}

ibAuiMDIParentFrame* ibAuiMDIChildFrame::GetMDIParentFrame() const
{
    return m_pMDIParentFrame;
}

void ibAuiMDIChildFrame::Init()
{
    m_activateOnCreate = true;
    m_pMDIParentFrame = nullptr;
#if wxUSE_MENUS
    m_pMenuBar = nullptr;
#endif // wxUSE_MENUS
}

bool ibAuiMDIChildFrame::Show(bool show)
{
    m_activateOnCreate = show;

    if ( show )
    {
        // This is not a real TLW, so it won't get a size event when it's
        // really "mapped", i.e. appears on the screen for the first time.
        // Instead, its size had been already set when it was created and we
        // didn't have any opportunity to lay it out since then, i.e. since
        // before its children were created. Do it now to allow the same code
        // that would work with a "real" wxMDIChildFrame to also work with this
        // class.
        DoLayout();

        // SEAM vs wx: the child joins the notebook here, shown and laid out with all it holds, under the bitmap of its
        // icons (SetIcons) - wx added it as it was made (Create)
        ibAuiMDIClientWindow* const pClientWindow = GetMDIParentFrame()->GetClientWindow();
        if ( pClientWindow->GetPageIndex(this) == wxNOT_FOUND )
        {
            pClientWindow->AddPage(this, m_title, m_activateOnCreate, GetPageBitmap());

            // Check that the parent notion of the active child coincides with our one.
            // This is less obvious that it seems because we must honour
            // m_activateOnCreate flag but only if it's not the first child because
            // this one becomes active unconditionally.
            wxASSERT_MSG
            (
                (m_activateOnCreate || pClientWindow->GetPageCount() == 1)
                    == (GetMDIParentFrame()->GetActiveChild() == this),
                wxS("Logic error: child [not] activated when it should [not] have been.")
            );
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
// ibAuiMDIClientWindow
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibAuiMDIClientWindow, ibAuiNotebook);

wxBEGIN_EVENT_TABLE(ibAuiMDIClientWindow, ibAuiNotebook)
    EVT_AUINOTEBOOK_PAGE_CHANGED(wxID_ANY, ibAuiMDIClientWindow::OnPageChanged)
    EVT_AUINOTEBOOK_PAGE_CLOSE(wxID_ANY, ibAuiMDIClientWindow::OnPageClose)
wxEND_EVENT_TABLE()

ibAuiMDIClientWindow::ibAuiMDIClientWindow()
{
}

ibAuiMDIClientWindow::ibAuiMDIClientWindow(ibAuiMDIParentFrame* parent, long style)
{
    CreateClient(parent, style);
}

bool ibAuiMDIClientWindow::CreateClient(ibAuiMDIParentFrame* parent, long style)
{
    SetWindowStyleFlag(style);

    if (!ibAuiNotebook::Create(parent,
                               wxID_ANY,
                               wxPoint(0,0),
                               parent->FromDIP(wxSize(100, 100)),
                               ibAUI_NB_DEFAULT_STYLE | wxNO_BORDER))
    {
        return false;
    }

    // SEAM vs wx: the workspace's colour is the theme's (ibColourScheme::FRAME, the canon's wxSYS_COLOUR_APPWORKSPACE), not
    // the system's - a client of the theme painted its own over it (2026-11-11)
    SetOwnBackgroundColour(wxTHEME_COLOUR(FRAME));

    return true;
}

ibAuiMDIChildFrame* ibAuiMDIClientWindow::GetActiveChild()
{
    const int sel = GetSelection();
    if ( sel == wxNOT_FOUND || sel >= (int)GetPageCount() )
        return nullptr;

    return wxStaticCast(GetPage(sel), ibAuiMDIChildFrame);
}

int ibAuiMDIClientWindow::DoModifySelection(size_t n, bool events)
{
    // SEAM vs wx: frozen through the change of the page shown - the client's own freeze (its DoModifySelection),
    // moved in with it: the tab and the page it shows come up as one
    wxWindowUpdateLocker freeze(this);
    return ibAuiNotebook::DoModifySelection(n, events);
}

void ibAuiMDIClientWindow::PageChanged(int old_selection, int new_selection)
{
    // don't do anything if the page doesn't actually change
    if (old_selection == new_selection)
        return;

    /*
    // don't do anything if the new page is already active
    if (new_selection != -1)
    {
        ibAuiMDIChildFrame* child = (ibAuiMDIChildFrame*)GetPage(new_selection);
        if (child->GetMDIParentFrame()->GetActiveChild() == child)
            return;
    }*/


    // notify old active child that it has been deactivated
    if ((old_selection != -1) && (old_selection < (int)GetPageCount()))
    {
        ibAuiMDIChildFrame* old_child = (ibAuiMDIChildFrame*)GetPage(old_selection);
        wxASSERT_MSG(old_child, wxT("ibAuiMDIClientWindow::PageChanged - null page pointer"));

        wxActivateEvent event(wxEVT_ACTIVATE, false, old_child->GetId());
        event.SetEventObject(old_child);
        old_child->GetEventHandler()->ProcessEvent(event);
    }

    // notify new active child that it has been activated
    if (new_selection != -1)
    {
        ibAuiMDIChildFrame* active_child = (ibAuiMDIChildFrame*)GetPage(new_selection);
        wxASSERT_MSG(active_child, wxT("ibAuiMDIClientWindow::PageChanged - null page pointer"));

        wxActivateEvent event(wxEVT_ACTIVATE, true, active_child->GetId());
        event.SetEventObject(active_child);
        active_child->GetEventHandler()->ProcessEvent(event);

        if (active_child->GetMDIParentFrame())
        {
            active_child->GetMDIParentFrame()->SetActiveChild(active_child);
            active_child->GetMDIParentFrame()->SetChildMenuBar(active_child);
        }
    }


}

void ibAuiMDIClientWindow::OnPageClose(ibAuiNotebookEvent& evt)
{
    ibAuiMDIChildFrame* wnd;
    wnd = static_cast<ibAuiMDIChildFrame*>(GetPage(evt.GetSelection()));

    wnd->Close();

    // regardless of the result of wnd->Close(), we've
    // already taken care of the close operations, so
    // suppress further processing
    evt.Veto();
}

void ibAuiMDIClientWindow::OnPageChanged(ibAuiNotebookEvent& evt)
{
    PageChanged(evt.GetOldSelection(), evt.GetSelection());
}

#endif // wxUSE_MDI
