// FORKED from wxWidgets' AUI (src/aui/framemanager.cpp of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        src/aui/framemanager.cpp
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2005-05-17
// Copyright:   (C) Copyright 2005-2006, Kirix Corporation, All Rights Reserved
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/wxprec.h"



#include "deskgui/univ/aui/framemanager.h"
#include "deskgui/univ/aui/floatpane.h"
#include "deskgui/univ/aui/tabmdi.h"
#include "deskgui/univ/aui/auibar.h"
#include "deskgui/univ/aui/auibook.h"
#include "deskgui/univ/aui/serializer.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"
#include "wx/mdi.h"
#include "wx/wupdlock.h"
#include "wx/dcbuffer.h"
#include "wx/renderer.h"   // wxCONTROL_* - a button's state as the theme reads it (ibAuiGetControlFlags)

#include "wx/private/tlwdrag.h"


#ifndef WX_PRECOMP
    #include "wx/panel.h"
    #include "wx/settings.h"
    #include "wx/app.h"
    #include "wx/dcclient.h"
    #include "wx/dcmemory.h"
    #include "wx/toolbar.h"
    #include "wx/image.h"
    #include "wx/statusbr.h"
#endif

WX_CHECK_BUILD_OPTIONS("wxAUI")

ibAuiPaneInfo ibAuiNullPaneInfo;
ibAuiDockInfo ibAuiNullDockInfo;
wxDEFINE_EVENT( ibEVT_AUI_PANE_BUTTON, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_PANE_CLOSE, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_PANE_MINIMIZE, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_PANE_MAXIMIZE, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_PANE_RESTORE, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_PANE_ACTIVATED, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_RENDER, ibAuiManagerEvent );
wxDEFINE_EVENT( ibEVT_AUI_FIND_MANAGER, ibAuiManagerEvent );

#ifdef __WXMSW__
    #include "wx/msw/wrapwin.h"
    #include "wx/msw/private.h"
#endif

#include "wx/dcgraph.h"

#include "wx/generic/private/drawresize.h"

#include <map>
#include <memory>
#include <unordered_map>

#ifdef wxHAS_TLW_DRAG_SESSION

// ----------------------------------------------------------------------------
// ibAuiPaneDragHandler: forwards drag session events to ibAuiManager
// ----------------------------------------------------------------------------

// This is used when dragging a pane using the system drag support, i.e. under
// Wayland, where the application can't move the floating frame itself.
class ibAuiPaneDragHandler : public wxTLWDragHandler
{
public:
    // The manager must be valid for the duration of the drag, which is
    // guaranteed as it owns the session owning this object.
    ibAuiPaneDragHandler(ibAuiManager* mgr,
                         wxWindow* paneWindow,
                         const wxPoint& offset)
        : m_mgr(mgr),
          m_paneWindow(paneWindow),
          m_offset(offset)
    {
    }

    // Let the caller override our detection of drag start: this should be used
    // when we already know that the user is dragging.
    void SetDragStarted()
    {
        m_dragReallyStarted = true;
    }

    void OnDragOver(wxWindow* win, const wxPoint& pt) override
    {
        if ( IsReallyDragging(pt) )
            m_mgr->OnPaneDragMove(m_paneWindow, win, pt, m_offset);
    }

    void OnDragDrop(wxWindow* win, const wxPoint& pt) override
    {
        // Don't do anything if the pane wasn't really dragged anywhere.
        if ( IsReallyDragging(pt) )
            m_mgr->OnPaneDragDrop(m_paneWindow, win, pt, m_offset);

        m_mgr->OnPaneDragEnd(m_paneWindow);
    }

    void OnDragCancel() override
    {
        m_mgr->OnPaneDragEnd(m_paneWindow);
    }

private:
    // Return true if the pointer has moved far enough from the position where
    // the drag had started for this to be a drag and not just a click.
    bool IsReallyDragging(const wxPoint& pt)
    {
        if ( m_dragReallyStarted )
            return true;

        if ( m_startPos == wxDefaultPosition )
        {
            m_startPos = pt;
            return false;
        }

        wxWindow* const win = m_mgr->GetManagedWindow();
        if ( !wxSystemSettings::ExceedsDragThreshold(m_startPos, pt, win) )
            return false;

        m_dragReallyStarted = true;

        return true;
    }

    ibAuiManager* const m_mgr;
    wxWindow* const m_paneWindow;
    const wxPoint m_offset;

    wxPoint m_startPos = wxDefaultPosition;
    bool m_dragReallyStarted = false;

    wxDECLARE_NO_COPY_CLASS(ibAuiPaneDragHandler);
};

#endif // wxHAS_TLW_DRAG_SESSION

wxIMPLEMENT_DYNAMIC_CLASS(ibAuiManagerEvent, wxEvent);
wxIMPLEMENT_CLASS(ibAuiManager, wxEvtHandler);


// -- local constants and helper functions --
namespace
{

// Index of the outermost layer used for all toolbars.
constexpr int auiToolBarLayer = 10;

// Default proportion which is "infinitely" greater than anything else.
constexpr int maxDockProportion = 100000;


wxBitmap ibCreateVenetianBlindsBitmap(wxByte r, wxByte g, wxByte b, wxByte a)
{
    const unsigned char c = wxSystemSettings::GetAppearance().IsDark() ? 220 : 5;

    unsigned char data[] = { r,g,b, r,g,b, c,c,c, c,c,c };
    unsigned char alpha[] = { a, a, 200, 200 };

    wxImage img(1,4,data,true);
    img.SetAlpha(alpha,true);
    return wxBitmap(img);
}

// CopyDocksAndPanes() - this utility function creates copies of
// the dock and pane info.  ibAuiDockInfo's usually contain pointers
// to ibAuiPaneInfo classes, thus this function is necessary to reliably
// reconstruct that relationship in the new dock info and pane info arrays

void
CopyDocksAndPanes(ibAuiDockInfoArray& dest_docks,
                  ibAuiPaneInfoArray& dest_panes,
                  const ibAuiDockInfoArray& src_docks,
                  const ibAuiPaneInfoArray& src_panes)
{
    dest_docks = src_docks;
    dest_panes = src_panes;
    int j, k, pc1, pc2;
    for ( auto& dock : dest_docks )
    {
        for (j = 0, pc1 = dock.panes.GetCount(); j < pc1; ++j)
            for (k = 0, pc2 = src_panes.GetCount(); k < pc2; ++k)
                if (dock.panes.Item(j) == &src_panes.Item(k))
                    dock.panes.Item(j) = &dest_panes.Item(k);
    }
}

// GetMaxLayer() is an internal function which returns
// the highest layer inside the specified dock
int GetMaxLayer(const ibAuiDockInfoArray& docks, int dock_direction)
{
    int max_layer = 0;
    for ( const auto& dock : docks )
    {
        if (dock.dock_direction == dock_direction &&
            dock.dock_layer > max_layer && !dock.fixed)
                max_layer = dock.dock_layer;
    }
    return max_layer;
}


// GetMaxRow() is an internal function which returns
// the highest layer inside the specified dock
int GetMaxRow(const ibAuiPaneInfoArray& panes, int direction, int layer)
{
    int max_row = 0;
    for ( const auto& pane : panes )
    {
        if (pane.dock_direction == direction &&
            pane.dock_layer == layer &&
            pane.dock_row > max_row)
                max_row = pane.dock_row;
    }
    return max_row;
}



// DoInsertDockLayer() is an internal function that inserts a new dock
// layer by incrementing all existing dock layer values by one
void
DoInsertDockLayer(ibAuiPaneInfoArray& panes,
                  int dock_direction,
                  int dock_layer)
{
    for ( auto& pane : panes )
    {
        if (!pane.IsFloating() &&
            pane.dock_direction == dock_direction &&
            pane.dock_layer >= dock_layer)
                pane.dock_layer++;
    }
}

// DoInsertDockLayer() is an internal function that inserts a new dock
// row by incrementing all existing dock row values by one
void
DoInsertDockRow(ibAuiPaneInfoArray& panes,
                int dock_direction,
                int dock_layer,
                int dock_row)
{
    for ( auto& pane : panes )
    {
        if (!pane.IsFloating() &&
            pane.dock_direction == dock_direction &&
            pane.dock_layer == dock_layer &&
            pane.dock_row >= dock_row)
                pane.dock_row++;
    }
}

// DoInsertDockLayer() is an internal function that inserts a space for
// another dock pane by incrementing all existing dock row values by one
void
DoInsertPane(ibAuiPaneInfoArray& panes,
             int dock_direction,
             int dock_layer,
             int dock_row,
             int dock_pos)
{
    for ( auto& pane : panes )
    {
        if (!pane.IsFloating() &&
            pane.dock_direction == dock_direction &&
            pane.dock_layer == dock_layer &&
            pane.dock_row == dock_row &&
            pane.dock_pos >= dock_pos)
                pane.dock_pos++;
    }
}

// Flags for FindDocks()
enum class FindDocksFlags
{
    // No special flags.
    None = 0,

    // Stop after the first found dock, returned array has 0 or 1 elements.
    OnlyFirst = 1,

    // Reverse the order of the returned docks, useful for right/bottom docks.
    ReverseOrder = 2
};

// FindDocks() is an internal function that returns a list of docks which meet
// the specified conditions in the parameters and returns a sorted array
// (sorted by layer and then row)
ibAuiDockInfoPtrArray
FindDocks(ibAuiDockInfoArray& docks,
          int dock_direction,
          int dock_layer,
          int dock_row,
          FindDocksFlags flags = FindDocksFlags::None)
{
    // discover the maximum dock layer and the max row
    int max_row = 0, max_layer = 0;
    for ( const auto& d : docks )
    {
        max_row = wxMax(max_row, d.dock_row);
        max_layer = wxMax(max_layer, d.dock_layer);
    }

    // if no dock layer was specified, search all dock layers
    int begin_layer, end_layer;
    if (dock_layer == -1)
    {
        begin_layer = 0;
        end_layer = max_layer;
    }
    else
    {
        begin_layer = dock_layer;
        end_layer = dock_layer;
    }

    // if no dock row was specified, search all dock rows
    int begin_row, end_row;
    if (dock_row == -1)
    {
        begin_row = 0;
        end_row = max_row;
    }
    else
    {
        begin_row = dock_row;
        end_row = dock_row;
    }

    ibAuiDockInfoPtrArray arr;

    for (int layer = begin_layer; layer <= end_layer; ++layer)
    {
        for (int row = begin_row; row <= end_row; ++row)
        {
            for ( auto& d : docks )
            {
                if (dock_direction == -1 || dock_direction == d.dock_direction)
                {
                    if (d.dock_layer == layer && d.dock_row == row)
                    {
                        switch ( flags )
                        {
                            case FindDocksFlags::None:
                                arr.Add(&d);
                                break;

                            case FindDocksFlags::OnlyFirst:
                                arr.Add(&d);
                                return arr;

                            case FindDocksFlags::ReverseOrder:
                                // Inserting into an array is inefficient, but
                                // we won't have more than a few docks here, so
                                // it doesn't matter, and it keeps the code
                                // simpler.
                                arr.Insert(&d, 0);
                                break;
                        }
                    }
                }
            }
        }
    }

    return arr;
}

// FindPaneInDock() looks up a specified window pointer inside a dock.
// If found, the corresponding ibAuiPaneInfo pointer is returned, otherwise nullptr.
ibAuiPaneInfo* FindPaneInDock(const ibAuiDockInfo& dock, wxWindow* window)
{
    for ( const auto& p : dock.panes )
    {
        if (p->window == window)
            return p;
    }
    return nullptr;
}

// RemovePaneFromDocks() removes a pane window from all docks
// with a possible exception specified by parameter "ex_cept"
void
RemovePaneFromDocks(ibAuiDockInfoArray& docks,
                    ibAuiPaneInfo& pane,
                    ibAuiDockInfo* ex_cept  = nullptr)
{
    for ( auto& d : docks )
    {
        if (&d == ex_cept)
            continue;
        ibAuiPaneInfo* pi = FindPaneInDock(d, pane.window);
        if (pi)
            d.panes.Remove(pi);
    }
}

/*
// This function works fine, and may be used in the future

// RenumberDockRows() takes a dock and assigns sequential numbers
// to existing rows.  Basically it takes out the gaps; so if a
// dock has rows with numbers 0,2,5, they will become 0,1,2
void RenumberDockRows(ibAuiDockInfoPtrArray& docks)
{
    int i, dock_count;
    for (i = 0, dock_count = docks.GetCount(); i < dock_count; ++i)
    {
        ibAuiDockInfo& dock = *docks.Item(i);
        dock.dock_row = i;

        int j, pane_count;
        for (j = 0, pane_count = dock.panes.GetCount(); j < pane_count; ++j)
            dock.panes.Item(j)->dock_row = i;
    }
}
*/

// SEAM vs wx: the window a top-level window's panes are laid out in (ibAuiManager::SetManagedWindow) - an ib window,
// placed at the client area's origin as one is (ibWindow::Create, DoSetSize). wxGTK places a child from the native
// window's corner, and the port's window stood there: its toolbars over the title bar, the menubar over its top, its
// bottom short of the frame (the designer on GTK3, CI's look, 2026-11-12). On MSW and Cocoa wx places the port's
// window there itself (AdjustForParentClientOrigin) - the same place; its paint and erase are AUI's (Bind) before ibWindow's
class ibAuiManagedWindow : public ibWindow
{
public:
    explicit ibAuiManagedWindow(wxWindow* owner)
    {
        Create(owner, wxID_ANY, wxDefaultPosition, owner->GetClientSize(), wxBORDER_NONE);
    }
};

} // anonymous namespace

int ibAuiGetControlFlags(int state)
{
    if (state & ibAUI_BUTTON_STATE_DISABLED)
        return wxCONTROL_DISABLED;

    int flags = 0;
    if (state & ibAUI_BUTTON_STATE_PRESSED)
        flags |= wxCONTROL_PRESSED;
    if (state & ibAUI_BUTTON_STATE_HOVER)
        flags |= wxCONTROL_CURRENT;
    if (state & ibAUI_BUTTON_STATE_CHECKED)
        flags |= wxCONTROL_CHECKED;
    return flags;
}


// ----------------------------------------------------------------------------
// ibAuiMinDock: dock showing panes that can be minimized.
// ----------------------------------------------------------------------------

// This class is currently implemented as ibAuiToolBar, but this could change
// in the future, we don't expose this fact in the public API.
class ibAuiMinDock : public ibAuiToolBar
{
public:
    // Style is the combination of ibAUI_MIN_DOCK_XXX values.
    ibAuiMinDock(ibAuiManager& mgr,
                 ibAuiManagerDock direction,
                 unsigned int style)
        : m_mgr(mgr),
          m_style(style)
    {
        long flags = 0;
        if ( m_style & ibAUI_MIN_DOCK_TEXT )
            flags |= ibAUI_TB_TEXT;
        if ( m_style & ibAUI_MIN_DOCK_ROTATE_ICON_WITH_TEXT )
            flags |= ibAUI_TB_ROTATE_ICON_WITH_TEXT;

        switch ( direction )
        {
            case ibAUI_DOCK_TOP:
            case ibAUI_DOCK_BOTTOM:
                flags |= ibAUI_TB_HORIZONTAL;
                if ( flags & ibAUI_TB_TEXT )
                    flags |= ibAUI_TB_HORZ_LAYOUT;
                break;

            case ibAUI_DOCK_LEFT:
                flags |= ibAUI_TB_VERTICAL;
                if ( flags & ibAUI_TB_TEXT )
                    flags |= ibAUI_TB_VERT_LAYOUT_UP;
                break;

            case ibAUI_DOCK_RIGHT:
                flags |= ibAUI_TB_VERTICAL;
                if ( flags & ibAUI_TB_TEXT )
                    flags |= ibAUI_TB_VERT_LAYOUT_DOWN;
                break;

            default:
                wxFAIL_MSG("Invalid direction for minimized dock");
        }

        Create(mgr.GetManagedWindow(), wxID_ANY,
                       wxDefaultPosition, wxDefaultSize,
                       flags | ibAUI_TB_PLAIN_BACKGROUND);

        SetFont(GetFont().MakeSmaller());

        Bind(wxEVT_MENU, &ibAuiMinDock::OnToolClicked, this);
    }

    void AddPane(ibAuiPaneInfo& paneInfo)
    {
        wxBitmapBundle icon;
        if ( m_style & ibAUI_MIN_DOCK_ICONS )
        {
            icon = paneInfo.iconMin;
            if ( !icon.IsOk() )
                icon = paneInfo.icon;
        }

        wxString text, tooltip;
        if ( m_style & ibAUI_MIN_DOCK_TEXT )
        {
            // Don't set tool tip in this case, it would be redundant.
            text = paneInfo.caption;
        }
        else // Showing only icons.
        {
            // Don't set text in this case, it would be unused anyhow.
            tooltip = paneInfo.caption;
        }

        auto* const item = AddTool(wxID_ANY, text, icon, tooltip);
        m_panes[item->GetId()] = &paneInfo;

        m_needsRealize = true;
    }

    // Returns false if the dock has become empty after removing this pane.
    bool RemovePane(ibAuiPaneInfo& paneInfo)
    {
        for (auto const& kv : m_panes)
        {
            if ( kv.second == &paneInfo )
            {
                DeleteTool(kv.first);
                m_panes.erase(kv.first);

                m_needsRealize = true;

                return !m_panes.empty();
            }
        }

        wxFAIL_MSG("Pane unexpectedly not found in minimized dock");

        return true;
    }

    bool RealizeIfNeeded()
    {
        if ( !m_needsRealize )
            return false;

        Realize();

        m_needsRealize = false;

        return true;
    }

private:
    void OnToolClicked(wxCommandEvent& event)
    {
        auto* const pane = m_panes[event.GetId()];
        wxCHECK_RET(pane, "No window associated with minimized pane tool?");

        if ( pane->IsShown() )
        {
            m_mgr.MinimizePane(*pane);
        }
        else
        {
            pane->Show();
        }

        m_mgr.Update();
    }

    ibAuiManager& m_mgr;
    const unsigned int m_style;

    std::unordered_map<int, ibAuiPaneInfo*> m_panes;

    bool m_needsRealize = false;
};

// SetActivePane() sets the active pane, as well as cycles through
// every other pane and makes sure that all others' active flags
// are turned off
void ibAuiManager::SetActivePane(wxWindow* active_pane)
{
    ibAuiPaneInfo* active_paneinfo = nullptr;
    for ( auto& pane : m_panes )
    {
        pane.state &= ~ibAuiPaneInfo::optionActive;
        if (pane.window == active_pane)
        {
            pane.state |= ibAuiPaneInfo::optionActive;
            active_paneinfo = &pane;
        }
    }

    // send the 'activated' event after all panes have been updated
    if ( active_paneinfo )
    {
        ibAuiManagerEvent evt(ibEVT_AUI_PANE_ACTIVATED);
        evt.SetManager(this);
        evt.SetPane(active_paneinfo);
        ProcessMgrEvent(evt);
    }
}


// this function is used to sort panes by dock position
static int PaneSortFunc(ibAuiPaneInfo** p1, ibAuiPaneInfo** p2)
{
    return ((*p1)->dock_pos < (*p2)->dock_pos) ? -1 : 1;
}


bool ibAuiPaneInfo::IsValid() const
{
    if ( dock_direction == ibAUI_DOCK_CENTRE &&
        !(dock_layer == 0 && dock_row == 0 && dock_pos == 0) )
    {
        wxFAIL_MSG("Center pane must have dock layer, row and pos set to 0");

        return false;
    }

    // Should this RTTI and function call be rewritten as
    // sending a new event type to allow other window types
    // to check the pane settings?
    ibAuiToolBar* toolbar = wxDynamicCast(window, ibAuiToolBar);
    if ( toolbar && !toolbar->IsPaneValid(*this) )
    {
        wxFAIL_MSG("toolbar style and pane docking flags are incompatible");

        return false;
    }

    return true;
}

// -- ibAuiManager class implementation --


ibAuiManager::ibAuiManager(wxWindow* managed_wnd, unsigned int flags)
{
    m_action = actionNone;
    m_actionWindow = nullptr;
    m_hoverButton = nullptr;
    m_flags = flags;
    m_hasMaximized = false;
    m_frame = nullptr;
    m_dockConstraintX = 0.3;
    m_dockConstraintY = 0.3;
    m_hintFadeMax = 64;

    m_reserved = nullptr;
    m_currentDragItem = -1;

    if (managed_wnd)
    {
        SetManagedWindow(managed_wnd);
    }
}

ibAuiManager::~ibAuiManager()
{
    UnInit();

    // NOTE: It's possible that the windows have already been destroyed by the
    // time this dtor is called, so this loop can result in memory access via
    // invalid pointers, resulting in a crash.  So it will be disabled while
    // waiting for a better solution.
#if 0
    for ( size_t i = 0; i < m_panes.size(); i++ )
    {
        ibAuiPaneInfo& pinfo = m_panes[i];
        if (pinfo.window && !pinfo.window->GetParent())
            delete pinfo.window;
    }
#endif
}

int ibAuiManager::GetActionPartIndex() const
{
    int n = 0;
    for ( const auto& uiPart : m_uiParts )
    {
        if ( &uiPart == m_actionPart )
            return n;

        ++n;
    }

    return wxNOT_FOUND;
}

void ibAuiManager::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    m_frame->Refresh();
    event.Skip(true);
}

// We don't need to scale the positions and sizes on DPI change if they use
// DPI-independent pixels.
#ifndef wxHAS_DPI_INDEPENDENT_PIXELS

void ibAuiManager::OnDPIChanged(wxDPIChangedEvent& event)
{
    event.Skip();

    for( ibAuiPaneInfo& pinfo : GetAllPanes() )
    {
        pinfo.min_size = event.Scale(pinfo.min_size);
        pinfo.best_size = event.Scale(pinfo.best_size);

        // The position is in pixels only for the panes in fixed docks, for
        // all the others it's just the index of the pane in its dock, which
        // must not be scaled.
        const auto docks = FindDocks(m_docks,
                                     pinfo.dock_direction,
                                     pinfo.dock_layer,
                                     pinfo.dock_row,
                                     FindDocksFlags::OnlyFirst);
        const bool scalePos = !docks.IsEmpty() && docks[0]->fixed;

        // Note that the position is along the dock while the size is across
        // it, i.e. for vertical docks the position is vertical but the size
        // is horizontal, and vice versa for the horizontal ones.
        switch ( pinfo.dock_direction )
        {
            case ibAUI_DOCK_LEFT:
            case ibAUI_DOCK_RIGHT:
            case ibAUI_DOCK_CENTER:
                if ( scalePos )
                    pinfo.dock_pos = event.ScaleY(pinfo.dock_pos);
                pinfo.dock_size = event.ScaleX(pinfo.dock_size);
                break;

            case ibAUI_DOCK_TOP:
            case ibAUI_DOCK_BOTTOM:
                if ( scalePos )
                    pinfo.dock_pos = event.ScaleX(pinfo.dock_pos);
                pinfo.dock_size = event.ScaleY(pinfo.dock_size);
                break;
        }
    }

    // Force recreating the docks after updating the panes.
    m_docks.clear();

    Update();
}

#endif // !wxHAS_DPI_INDEPENDENT_PIXELS

// creates a floating frame for the windows
ibAuiFloatingFrame* ibAuiManager::CreateFloatingFrame(wxWindow* parent,
                                                      const ibAuiPaneInfo& paneInfo)
{
    return new ibAuiFloatingFrame(parent, this, paneInfo);
}

bool ibAuiManager::CanDockPanel(const ibAuiPaneInfo & WXUNUSED(p))
{
    // if a key modifier is pressed while dragging the frame,
    // don't dock the window
    return !(wxGetKeyState(WXK_CONTROL) || wxGetKeyState(WXK_ALT));
}

bool ibAuiManager::CanAddPane(wxWindow* window,
                              const ibAuiPaneInfo& WXUNUSED(paneInfo)) const
{
    wxCHECK_MSG(window, false, wxT("null window ptrs are not allowed"));

    return true;
}

// GetPane() looks up a ibAuiPaneInfo structure based
// on the supplied window pointer.  Upon failure, GetPane()
// returns an empty ibAuiPaneInfo, a condition which can be checked
// by calling ibAuiPaneInfo::IsOk().
//
// The pane info's structure may then be modified.  Once a pane's
// info is modified, ibAuiManager::Update() must be called to
// realize the changes in the UI.

ibAuiPaneInfo& ibAuiManager::GetPane(wxWindow* window)
{
    for ( auto& p : m_panes )
    {
        if (p.window == window)
            return p;
    }
    return ibAuiNullPaneInfo;
}

// this version of GetPane() looks up a pane based on a
// 'pane name', see above comment for more info
ibAuiPaneInfo& ibAuiManager::GetPane(const wxString& name)
{
    for ( auto& p : m_panes )
    {
        if (p.name == name)
            return p;
    }
    return ibAuiNullPaneInfo;
}

// HitTest() is an internal function which determines
// which UI item the specified coordinates are over
// (x,y) specify a position in client coordinates
ibAuiDockUIPart* ibAuiManager::HitTest(int x, int y)
{
    ibAuiDockUIPart* result = nullptr;

    for ( auto& uiPart : m_uiParts )
    {
        ibAuiDockUIPart* item = &uiPart;

        // we are not interested in typeDock, because this space
        // isn't used to draw anything, just for measurements;
        // besides, the entire dock area is covered with other
        // rectangles, which we are interested in.
        if (item->type == ibAuiDockUIPart::typeDock)
            continue;

        // if we already have a hit on a more specific item, we are not
        // interested in a pane hit.  If, however, we don't already have
        // a hit, returning a pane hit is necessary for some operations
        if ((item->type == ibAuiDockUIPart::typePane ||
            item->type == ibAuiDockUIPart::typePaneBorder) && result)
            continue;

        // if the point is inside the rectangle, we have a hit
        if (item->rect.Contains(x,y))
            result = item;
    }

    return result;
}


// SetFlags() and GetFlags() allow the owner to set various
// options which are global to ibAuiManager
void ibAuiManager::SetFlags(unsigned int flags)
{
    // set the new flags
    m_flags = flags;
}

unsigned int ibAuiManager::GetFlags() const
{
    return m_flags;
}

/* static */ bool ibAuiManager::AlwaysUsesLiveResize(const wxWindow* WXUNUSED(window))
{
    return false;
}

bool ibAuiManager::HasLiveResize() const
{
    return (GetFlags() & ibAUI_MGR_LIVE_RESIZE) == ibAUI_MGR_LIVE_RESIZE;
}

// don't use these anymore as they are deprecated
// use Set/GetManagedFrame() instead
void ibAuiManager::SetFrame(wxFrame* frame)
{
    SetManagedWindow((wxWindow*)frame);
}

wxFrame* ibAuiManager::GetFrame() const
{
    return (wxFrame*)m_frame;
}


// this function will return the aui manager for a given
// window.  The |window| parameter should be any child window
// or grand-child window (and so on) of the frame/window
// managed by ibAuiManager.  The |window| parameter does not
// need to be managed by the manager itself.
ibAuiManager* ibAuiManager::GetManager(wxWindow* window)
{
    ibAuiManagerEvent evt(ibEVT_AUI_FIND_MANAGER);
    evt.SetManager(nullptr);
    evt.ResumePropagation(wxEVENT_PROPAGATE_MAX);
    if (!window->GetEventHandler()->ProcessEvent(evt))
        return nullptr;

    return evt.GetManager();
}


// SetManagedWindow() is usually called once when the frame
// manager class is being initialized.  "frame" specifies
// the frame which should be managed by the frame manager
void ibAuiManager::SetManagedWindow(wxWindow* wnd)
{
    wxASSERT_MSG(wnd, wxT("specified window must be non-null"));

    UnInit();

    // SEAM vs wx: A TOP-LEVEL WINDOW'S PANES ARE LAID OUT IN A WINDOW INSIDE IT, filling its client area - not in the
    // top-level window itself. The theme's top-level window (ibTopLevelWindow) is its decorations and its client area
    // in one native window, the client area's origin past the title bar and the menubar: drawn over the client area
    // through the native window (wxOverlay - the docking hint, the sash dragged), a rectangle stood that origin up and
    // to the left, and the strip of the client area below it was never covered (2026-11-11, Max: "it finds the area
    // wrong"). The window inside begins where it is drawn; the panes are its children (AddPane)
    wxWindow* const owner = wnd;
    if ( owner->IsTopLevel() )
    {
        wnd = new ibAuiManagedWindow(owner);

        wxBoxSizer* const sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(wnd, wxSizerFlags(1).Expand());
        owner->SetSizer(sizer);
    }

    m_frame = wnd;
    m_frame->Bind(ibEVT_AUI_PANE_BUTTON, &ibAuiManager::OnPaneButton, this);
    m_frame->Bind(ibEVT_AUI_RENDER, &ibAuiManager::OnRender, this);
    m_frame->Bind(wxEVT_DESTROY, &ibAuiManager::OnDestroy, this);
    m_frame->Bind(wxEVT_PAINT, &ibAuiManager::OnPaint, this);
    m_frame->Bind(wxEVT_ERASE_BACKGROUND, &ibAuiManager::OnEraseBackground, this);
    m_frame->Bind(wxEVT_SIZE, &ibAuiManager::OnSize, this);
    m_frame->Bind(wxEVT_SET_CURSOR, &ibAuiManager::OnSetCursor, this);
    m_frame->Bind(wxEVT_LEFT_DOWN, &ibAuiManager::OnLeftDown, this);
    m_frame->Bind(wxEVT_LEFT_UP, &ibAuiManager::OnLeftUp, this);
    m_frame->Bind(wxEVT_MOTION, &ibAuiManager::OnMotion, this);
    m_frame->Bind(wxEVT_LEAVE_WINDOW, &ibAuiManager::OnLeaveWindow, this);
    m_frame->Bind(wxEVT_MOUSE_CAPTURE_LOST, &ibAuiManager::OnCaptureLost, this);
    m_frame->Bind(wxEVT_CHILD_FOCUS, &ibAuiManager::OnChildFocus, this);
    m_frame->Bind(ibEVT_AUI_FIND_MANAGER, &ibAuiManager::OnFindManager, this);
    m_frame->Bind(wxEVT_SYS_COLOUR_CHANGED, &ibAuiManager::OnSysColourChanged, this);
#ifndef wxHAS_DPI_INDEPENDENT_PIXELS
    m_frame->Bind(wxEVT_DPI_CHANGED, &ibAuiManager::OnDPIChanged, this);
#endif // !wxHAS_DPI_INDEPENDENT_PIXELS

#if wxUSE_MDI
    // if the owner is going to manage an MDI parent frame,
    // we need to add the MDI client window as the default
    // center pane

    if (wxDynamicCast(owner, wxMDIParentFrame))
    {
        wxMDIParentFrame* mdi_frame = (wxMDIParentFrame*)owner;
        wxWindow* client_window = mdi_frame->GetClientWindow();

        wxASSERT_MSG(client_window, wxT("Client window is null!"));

        AddPane(client_window,
                ibAuiPaneInfo().Name(wxT("mdiclient")).
                CenterPane().PaneBorder(false));
    }
    else if (wxDynamicCast(owner, ibAuiMDIParentFrame))
    {
        ibAuiMDIParentFrame* mdi_frame = (ibAuiMDIParentFrame*)owner;
        ibAuiMDIClientWindow* client_window = mdi_frame->GetClientWindow();
        wxASSERT_MSG(client_window, wxT("Client window is null!"));

        AddPane(client_window,
                ibAuiPaneInfo().Name(wxT("mdiclient")).
                CenterPane().PaneBorder(false));
    }

#endif
}


// UnInit() is called automatically by ibAuiManager itself when either the
// manager itself or its associated frame is destroyed, but can also be called
// explicitly, so make it safe to call it multiple times and just do nothing
// during any calls but the first one.
void ibAuiManager::UnInit()
{
    if (m_frame)
    {
        m_frame->Unbind(ibEVT_AUI_PANE_BUTTON, &ibAuiManager::OnPaneButton, this);
        m_frame->Unbind(ibEVT_AUI_RENDER, &ibAuiManager::OnRender, this);
        m_frame->Unbind(wxEVT_DESTROY, &ibAuiManager::OnDestroy, this);
        m_frame->Unbind(wxEVT_PAINT, &ibAuiManager::OnPaint, this);
        m_frame->Unbind(wxEVT_ERASE_BACKGROUND, &ibAuiManager::OnEraseBackground, this);
        m_frame->Unbind(wxEVT_SIZE, &ibAuiManager::OnSize, this);
        m_frame->Unbind(wxEVT_SET_CURSOR, &ibAuiManager::OnSetCursor, this);
        m_frame->Unbind(wxEVT_LEFT_DOWN, &ibAuiManager::OnLeftDown, this);
        m_frame->Unbind(wxEVT_LEFT_UP, &ibAuiManager::OnLeftUp, this);
        m_frame->Unbind(wxEVT_MOTION, &ibAuiManager::OnMotion, this);
        m_frame->Unbind(wxEVT_LEAVE_WINDOW, &ibAuiManager::OnLeaveWindow, this);
        m_frame->Unbind(wxEVT_MOUSE_CAPTURE_LOST, &ibAuiManager::OnCaptureLost, this);
        m_frame->Unbind(wxEVT_CHILD_FOCUS, &ibAuiManager::OnChildFocus, this);
        m_frame->Unbind(ibEVT_AUI_FIND_MANAGER, &ibAuiManager::OnFindManager, this);
        m_frame->Unbind(wxEVT_SYS_COLOUR_CHANGED, &ibAuiManager::OnSysColourChanged, this);
#ifndef wxHAS_DPI_INDEPENDENT_PIXELS
        m_frame->Unbind(wxEVT_DPI_CHANGED, &ibAuiManager::OnDPIChanged, this);
#endif // !wxHAS_DPI_INDEPENDENT_PIXELS
        m_frame = nullptr;
    }
}

// GetManagedWindow() returns the window pointer being managed
wxWindow* ibAuiManager::GetManagedWindow() const
{
    return m_frame;
}

int ibAuiManager::GetMetricForWindow(int id, wxWindow* window) const
{
    // the art's sizes; a pane's border not scaled, so it stays one pixel at a high DPI
    switch (id)
    {
        case ibAUI_DOCKART_SASH_SIZE:        return wxWindow::FromDIP(3, window);
        case ibAUI_DOCKART_CAPTION_SIZE:     return wxWindow::FromDIP(17, window);
        case ibAUI_DOCKART_GRIPPER_SIZE:     return wxWindow::FromDIP(9, window);
        case ibAUI_DOCKART_PANE_BORDER_SIZE: return 1;
        case ibAUI_DOCKART_PANE_BUTTON_SIZE: return wxWindow::FromDIP(14, window);
    }

    wxFAIL_MSG(wxT("Invalid Metric Ordinal"));
    return 0;
}

wxSize ibAuiManager::GetMinPaneSize() const
{
    // This is pretty arbitrary, should we make it configurable?
    return wxWindow::FromDIP(wxSize(10, 10), m_frame);
}

void ibAuiManager::AllowDocksForMinPanes(int directions)
{
    wxCHECK_RET( directions, "Must specify at least one direction" );
    wxCHECK_RET
    (
         !(directions & ~wxALL),
         "Parameter may only contain wxLEFT, wxRIGHT, wxTOP and wxBOTTOM"
    );

    if ( directions == m_minDockAllowed )
        return;

    for ( const auto& dock : m_minDocks )
    {
        if ( dock )
        {
            wxFAIL_MSG( "Must be called before there are any minimized panes" );
            return;
        }
    }

    m_minDockAllowed = directions;
}

void ibAuiManager::SetDocksForMinPanesStyle(unsigned int style)
{
    wxASSERT_MSG
    (
        !(style & ~(ibAUI_MIN_DOCK_BOTH |
                    ibAUI_MIN_DOCK_ROTATE_ICON_WITH_TEXT)),
        "Parameter may only contain ibAUI_MIN_DOCK_XXX flags"
    );

    wxASSERT_MSG
    (
        style & ibAUI_MIN_DOCK_BOTH,
        "Either ibAUI_MIN_DOCK_ICONS or ibAUI_MIN_DOCK_TEXT must be specified"
    );

    wxASSERT_MSG
    (
        (style & ibAUI_MIN_DOCK_ICONS) ||
            !(style & ibAUI_MIN_DOCK_ROTATE_ICON_WITH_TEXT),
        "ibAUI_MIN_DOCK_ROTATE_ICON_WITH_TEXT requires ibAUI_MIN_DOCK_ICONS"
    );

    for ( const auto& dock : m_minDocks )
    {
        if ( dock )
        {
            wxFAIL_MSG( "Must be called before there are any minimized panes" );
            return;
        }
    }

    m_minDockStyle = style;
}

void ibAuiManager::ProcessMgrEvent(ibAuiManagerEvent& event)
{
    // first, give the owner frame a chance to override
    if (m_frame)
    {
        if (m_frame->GetEventHandler()->ProcessEvent(event))
            return;
    }

    ProcessEvent(event);
}


bool ibAuiManager::AddPane(wxWindow* window, const ibAuiPaneInfo& paneInfo)
{
    if ( !CanAddPane(window, paneInfo) )
        return false;

    // check if the window is already managed by us
    if (GetPane(paneInfo.window).IsOk())
        return false;

    // SEAM vs wx: the pane is a child of the window managed - made a child of the top-level window, it is moved into
    // the window laid out inside it (SetManagedWindow). A window with no parent is not made - a book's tab frame, which
    // only places its row of tabs - and stays as wx leaves it: wxOSX's Reparent embeds the window's view, and there is
    // none (the designer fell making its bottom pane, CI's look step, 2026-11-13); on MSW and GTK3 there was no native
    // window to move either
    if (window->GetParent() != nullptr && window->GetParent() != m_frame)
        window->Reparent(m_frame);

    // check if the pane name already exists, this could reveal a
    // bug in the library user's application
    bool already_exists = false;
    if (!paneInfo.name.empty() && GetPane(paneInfo.name).IsOk())
    {
        wxFAIL_MSG(wxT("A pane with that name already exists in the manager!"));
        already_exists = true;
    }

    // if the new pane is docked then we should undo maximize
    if (paneInfo.IsDocked())
        RestoreMaximizedPane();

    // special case:  ibAuiToolBar style interacts with docking flags
    ibAuiPaneInfo test(paneInfo);
    ibAuiToolBar* toolbar = wxDynamicCast(window, ibAuiToolBar);
    if (toolbar)
    {
        // if pane has default docking flags
        const unsigned int dockMask = ibAuiPaneInfo::optionLeftDockable |
                                        ibAuiPaneInfo::optionRightDockable |
                                        ibAuiPaneInfo::optionTopDockable |
                                        ibAuiPaneInfo::optionBottomDockable;
        const unsigned int defaultDock = ibAuiPaneInfo().
                                            DefaultPane().state & dockMask;
        if ((test.state & dockMask) == defaultDock)
        {
            // set docking flags based on toolbar style
            if (toolbar->GetWindowStyleFlag() & ibAUI_TB_VERTICAL)
            {
                test.TopDockable(false).BottomDockable(false);
            }
            else if (toolbar->GetWindowStyleFlag() & ibAUI_TB_HORIZONTAL)
            {
                test.LeftDockable(false).RightDockable(false);
            }
        }
        else
        {
            // see whether non-default docking flags are valid
            test.window = window;
            if (!test.IsValid())
                return false;
        }
    }

    m_panes.Add(test);

    ibAuiPaneInfo& pinfo = m_panes.Last();

    // set the pane window
    pinfo.window = window;


    // if the pane's name identifier is blank, create a random string
    if (pinfo.name.empty() || already_exists)
    {
        pinfo.name.Printf(wxT("%08lx%08x%08x%08lx"),
             (unsigned long)(wxPtrToUInt(pinfo.window) & 0xffffffff),
             (unsigned int)time(nullptr),
             (unsigned int)clock(),
             (unsigned long)m_panes.GetCount());
    }

    // set initial proportion (if not already set)
    if (pinfo.dock_proportion == 0)
        pinfo.dock_proportion = maxDockProportion;

    if (pinfo.HasGripper())
    {
        if (wxDynamicCast(pinfo.window, ibAuiToolBar))
        {
            // prevent duplicate gripper -- both ibAuiManager and ibAuiToolBar
            // have a gripper control.  The toolbar's built-in gripper
            // meshes better with the look and feel of the control than ours,
            // so turn ibAuiManager's gripper off, and the toolbar's on.

            ibAuiToolBar* tb = static_cast<ibAuiToolBar*>(pinfo.window);
            pinfo.SetFlag(ibAuiPaneInfo::optionGripper, false);
            tb->SetGripperVisible(true);
        }
    }


    if (pinfo.best_size == wxDefaultSize &&
        pinfo.window)
    {
        // It's important to use the current window size and not the best size
        // when adding a pane corresponding to a previously docked window: it
        // shouldn't change its size if it's dragged and docked in a different
        // place.
        pinfo.best_size = pinfo.window->GetSize();

        // But we still shouldn't make it too small.
        pinfo.best_size.IncTo(pinfo.window->GetBestSize());
        pinfo.best_size.IncTo(pinfo.min_size);
    }

    if ( pinfo.dock_size == 0 && paneInfo.best_size != wxDefaultSize )
    {
        switch ( pinfo.dock_direction )
        {
            case ibAUI_DOCK_LEFT:
            case ibAUI_DOCK_RIGHT:
                if ( pinfo.best_size.x != wxDefaultCoord )
                    pinfo.dock_size = pinfo.best_size.x;
                break;

            case ibAUI_DOCK_TOP:
            case ibAUI_DOCK_BOTTOM:
                if ( pinfo.best_size.y != wxDefaultCoord )
                    pinfo.dock_size = pinfo.best_size.y;
                break;

            default:
                break;
        }
    }

    AddPaneToMinDockIfNecessary(pinfo);

    return true;
}

bool ibAuiManager::AddPane(wxWindow* window,
                           int direction,
                           const wxString& caption)
{
    ibAuiPaneInfo pinfo;
    pinfo.Caption(caption);
    switch (direction)
    {
        case wxTOP:    pinfo.Top(); break;
        case wxBOTTOM: pinfo.Bottom(); break;
        case wxLEFT:   pinfo.Left(); break;
        case wxRIGHT:  pinfo.Right(); break;
        case wxCENTER: pinfo.CenterPane(); break;
    }
    return AddPane(window, pinfo);
}

bool ibAuiManager::AddPane(wxWindow* window,
                           const ibAuiPaneInfo& paneInfo,
                           const wxPoint& drop_pos)
{
    if (!AddPane(window, paneInfo))
        return false;

    ibAuiPaneInfo& pane = GetPane(window);

    DoDrop(m_docks, m_panes, pane, drop_pos, wxPoint(0,0));

    return true;
}

bool ibAuiManager::InsertPane(wxWindow* window, const ibAuiPaneInfo& paneInfo,
                                int insert_level)
{
    wxASSERT_MSG(window, wxT("null window ptrs are not allowed"));

    ibAuiPaneInfo& existing_pane = GetPane(window);
    if ( !existing_pane.IsOk() && !CanAddPane(window, paneInfo) )
        return false;

    // shift the panes around, depending on the insert level
    switch (insert_level)
    {
        case ibAUI_INSERT_PANE:
            DoInsertPane(m_panes,
                 paneInfo.dock_direction,
                 paneInfo.dock_layer,
                 paneInfo.dock_row,
                 paneInfo.dock_pos);
            break;
        case ibAUI_INSERT_ROW:
            DoInsertDockRow(m_panes,
                 paneInfo.dock_direction,
                 paneInfo.dock_layer,
                 paneInfo.dock_row);
            break;
        case ibAUI_INSERT_DOCK:
            DoInsertDockLayer(m_panes,
                 paneInfo.dock_direction,
                 paneInfo.dock_layer);
            break;
    }

    // if the window already exists, we are basically just moving/inserting the
    // existing window.  If it doesn't exist, we need to add it and insert it
    if (!existing_pane.IsOk())
    {
        return AddPane(window, paneInfo);
    }
    else
    {
        if (paneInfo.IsFloating())
        {
            existing_pane.Float();
            if (paneInfo.floating_pos != wxDefaultPosition)
                existing_pane.FloatingPosition(paneInfo.floating_pos);
            if (paneInfo.floating_size != wxDefaultSize)
                existing_pane.FloatingSize(paneInfo.floating_size);
            if (paneInfo.floating_client_size != wxDefaultSize)
                existing_pane.FloatingClientSize(paneInfo.floating_client_size);
        }
        else
        {
            // if the new pane is docked then we should undo maximize
            RestoreMaximizedPane();

            existing_pane.Direction(paneInfo.dock_direction);
            existing_pane.Layer(paneInfo.dock_layer);
            existing_pane.Row(paneInfo.dock_row);
            existing_pane.Position(paneInfo.dock_pos);
        }
    }

    return true;
}

wxSize ibAuiManager::CalculateNewSplitSize() const
{
    // Don't do anything if we're not fully initialized yet.
    if ( !m_frame || m_docks.empty() )
        return GetMinPaneSize();

    // Calculate the number of vertical and horizontal rows that we have.
    int numColumns = 0;
    int numRows = 0;

    // We could have called FindDocks() multiple times here, but it is more
    // efficient to count the docks ourselves just once.
    for ( const auto& d : m_docks )
    {
        switch ( d.dock_direction )
        {
            case ibAUI_DOCK_LEFT:
            case ibAUI_DOCK_RIGHT:
                numColumns++;
                break;

            case ibAUI_DOCK_TOP:
            case ibAUI_DOCK_BOTTOM:
                numRows++;
                break;

            case ibAUI_DOCK_CENTER:
                // Center dock counts as both a row and a column.
                numColumns++;
                numRows++;
                break;

            case ibAUI_DOCK_NONE:
                wxFAIL_MSG( "Unexpected dock direction when calculating new split size" );
        }
    }

    // We don't know if the new split is going to be horizontal or vertical,
    // but it doesn't matter because the other size component won't be used, so
    // we can just set each of them as if the split was in that direction.
    wxSize size = m_frame->GetClientSize();

    // When we have only one pane, the new split should be half of the total
    // available size, hence +1.
    size.x /= numColumns + 1;
    size.y /= numRows + 1;

    // Always use some minimum size to avoid creating too small panes or
    // showing tiny hint window (because this function is also used to
    // determine its size).
    size.IncTo(GetMinPaneSize());

    return size;
}

bool
ibAuiManager::SplitPane(wxWindow* window,
                        wxWindow* newWindow,
                        int direction,
                        const wxPoint& dropPos)
{
    const ibAuiPaneInfo& paneOrig = GetPane(window);
    wxCHECK_MSG( paneOrig.IsOk(), false, "Window being split must be present" );

    ibAuiPaneInfo paneInfo = ibAuiPaneInfo().CaptionVisible(false);

    wxPoint defaultDropPos;
    const wxSize sizeWindow = m_frame->GetClientSize();

    if (direction == wxLEFT)
    {
        paneInfo.Left();
        defaultDropPos = wxPoint(0, sizeWindow.y/2);
    }
    else if (direction == wxRIGHT)
    {
        paneInfo.Right();
        defaultDropPos = wxPoint(sizeWindow.x, sizeWindow.y/2);
    }
    else if (direction == wxTOP)
    {
        paneInfo.Top();
        defaultDropPos = wxPoint(sizeWindow.x/2, 0);
    }
    else if (direction == wxBOTTOM)
    {
        paneInfo.Bottom();
        defaultDropPos = wxPoint(sizeWindow.x/2, sizeWindow.y);
    }

    // Get the new split size before adding the pane, as this would change it.
    const wxSize size = CalculateNewSplitSize();

    AddPane(newWindow, paneInfo,
            dropPos != wxDefaultPosition ? dropPos : defaultDropPos);

    // The direction specified in the pane info may be overridden by the drop
    // position, so get the real direction of the new pane now.
    bool adjustHorz = false;
    bool adjustVert = false;
    switch ( GetPane(newWindow).dock_direction )
    {
        case ibAUI_DOCK_LEFT:
        case ibAUI_DOCK_RIGHT:
            adjustHorz = true;
            break;

        case ibAUI_DOCK_TOP:
        case ibAUI_DOCK_BOTTOM:
            adjustVert = true;
            break;

        case ibAUI_DOCK_CENTER:
        case ibAUI_DOCK_NONE:
            wxFAIL_MSG( "Unexpected dock direction for new pane after split" );
    }

    // Adjust all the existing docks to have the same size as the new one in
    // the split direction.
    for ( auto& d : m_docks )
    {
        switch ( d.dock_direction )
        {
            case ibAUI_DOCK_LEFT:
            case ibAUI_DOCK_RIGHT:
                if ( adjustHorz )
                    d.size = size.x;
                break;

            case ibAUI_DOCK_TOP:
            case ibAUI_DOCK_BOTTOM:
                if ( adjustVert )
                    d.size = size.y;
                break;

            case ibAUI_DOCK_CENTER:
                d.size = adjustHorz ? size.x : size.y;
                break;

            case ibAUI_DOCK_NONE:
                wxFAIL_MSG( "Unexpected dock direction when calculating new split size" );
        }
    }

    // And if we need to create a new dock for the new pane, set its size to be
    // the same too.
    //
    // Note: this is similar to the code in LayoutAll() and we could also call
    // Update() to do this, but then we'd need to call it again to take the
    // changed dock size into account, so we prefer to set the size here to
    // avoid an extra call to Update().
    //
    // Also note that it is _not_ sufficient to check if the dock is going to
    // be created for the new pane, in some layouts it can go into an existing
    // dock and a new dock is created for an existing pane, so we really need
    // to iterate over all of them.
    for ( const auto& p : m_panes )
    {
        if ( FindDocks(m_docks,
                       p.dock_direction,
                       p.dock_layer,
                       p.dock_row,
                       FindDocksFlags::OnlyFirst).IsEmpty() )
        {
            ibAuiDockInfo d;
            d.dock_direction = p.dock_direction;
            d.dock_layer = p.dock_layer;
            d.dock_row = p.dock_row;
            d.size = adjustHorz ? size.x : size.y;
            m_docks.Add(d);
        }
    }

    Update();

    return true;
}

// DetachPane() removes a pane from the frame manager.  This
// method will not destroy the window that is removed.
bool ibAuiManager::DetachPane(wxWindow* window)
{
    wxASSERT_MSG(window, wxT("null window ptrs are not allowed"));

    int i, count;
    for (i = 0, count = m_panes.GetCount(); i < count; ++i)
    {
        ibAuiPaneInfo& p = m_panes.Item(i);
        if (p.window == window)
        {
            if (p.frame)
            {
                // we have a floating frame which is being detached. We need to
                // reparent it to m_frame and destroy the floating frame

                // reduce flicker
                p.window->SetSize(1,1);

                if (p.frame->IsShown())
                    p.frame->Show(false);

                DestroyFloatingFrame(p);
            }

            // make sure there are no references to this pane in our uiparts,
            // just in case the caller doesn't call Update() immediately after
            // the DetachPane() call.  This prevets obscure crashes which would
            // happen at window repaint if the caller forgets to call Update()
            int actionPartIndex = GetActionPartIndex();
            int pi, part_count;
            for (pi = 0, part_count = (int)m_uiParts.GetCount(); pi < part_count; ++pi)
            {
                ibAuiDockUIPart& part = m_uiParts.Item(pi);
                if (part.pane == &p)
                {
                    if (actionPartIndex != wxNOT_FOUND)
                    {
                        if (pi == actionPartIndex)
                        {
                            // We're removing the action part, so invalidate it.
                            actionPartIndex = wxNOT_FOUND;
                        }
                        else if (pi < actionPartIndex)
                        {
                            // Just adjust the action part index.
                            actionPartIndex--;
                        }
                    }

                    m_uiParts.RemoveAt(pi);
                    part_count--;
                    pi--;
                    continue;
                }
            }

            // Update m_actionPart pointer too to ensure that it remains valid.
            m_actionPart = actionPartIndex == wxNOT_FOUND
                            ? nullptr
                            : &m_uiParts.Item(actionPartIndex);

            m_panes.RemoveAt(i);
            return true;
        }
    }
    return false;
}

// ClosePane() destroys or hides the pane depending on its flags
void ibAuiManager::DoHidePaneWindow(ibAuiPaneInfo& paneInfo)
{
    // if we were maximized, restore
    if (paneInfo.IsMaximized())
    {
        RestorePane(paneInfo);
    }

    // first, hide the window
    if (paneInfo.window && paneInfo.window->IsShown())
    {
        paneInfo.window->Show(false);
    }

    // make sure that we are the parent of this window
    if (paneInfo.window && paneInfo.window->GetParent() != m_frame)
    {
        paneInfo.window->Reparent(m_frame);
    }

    // if we have a frame, destroy it
    if (paneInfo.frame)
    {
        DestroyFloatingFrame(paneInfo);
    }
}

void ibAuiManager::DestroyFloatingFrame(ibAuiPaneInfo& paneInfo)
{
    wxCHECK_RET( paneInfo.frame, "no floating frame to destroy" );

    if (paneInfo.window && paneInfo.window->GetParent() != m_frame)
        paneInfo.window->Reparent(m_frame);

    // ibAuiFloatingFrame::Destroy() removes the pane window from its sizer.
    paneInfo.frame->Destroy();
    paneInfo.frame = nullptr;
}

void ibAuiManager::ClosePane(ibAuiPaneInfo& paneInfo)
{
    DoHidePaneWindow(paneInfo);

    // Ensure that the pane doesn't remain referenced by any docks, otherwise
    // closing it would have the same effect as minimizing it, which is not
    // what the user expects.
    //
    // This also ensures that docks don't reference the already destroyed pane
    // in case we're really destroying it below (i.e. if IsDestroyOnClose()).
    if (paneInfo.HasMinimizeButton())
    {
        auto const minDirection = GetMinDockDirectionFor(paneInfo.dock_direction);
        if (minDirection != ibAUI_DOCK_NONE)
        {
            RemovePaneFromMinDockIfNecessary(minDirection, paneInfo);
        }

        // Also prevent it from being added to the dock if it is shown later by
        // marking it as "closed": this flag will be reset if the frame is
        // shown again later.
        paneInfo.SetFlag(ibAuiPaneInfo::savedClosed, true);
    }

    // now we need to either destroy or hide the pane
    if (paneInfo.IsDestroyOnClose())
    {
        wxWindow * window = paneInfo.window;
        DetachPane(window);
        if (window)
        {
            window->Destroy();
        }
    }
    else
    {
        paneInfo.Hide();
    }
}

ibAuiManagerDock ibAuiManager::GetMinDockDirectionFor(int paneDirection) const
{
    // Panes prefer to minimize in the direction of their docked position, but
    // if minimized dock in this direction is not allowed, try to find the
    // direction "closest" to it. This is somewhat arbitrary, we decide to
    // prefer left to right and bottom to top.
    ibAuiManagerDock direction = ibAUI_DOCK_NONE;
    switch ( paneDirection )
    {
        case ibAUI_DOCK_TOP:
            if ( m_minDockAllowed & wxTOP )
                direction = ibAUI_DOCK_TOP;
            else if ( m_minDockAllowed & wxLEFT )
                direction = ibAUI_DOCK_LEFT;
            else if ( m_minDockAllowed & wxRIGHT )
                direction = ibAUI_DOCK_RIGHT;
            else if ( m_minDockAllowed & wxBOTTOM )
                direction = ibAUI_DOCK_BOTTOM;
            break;

        case ibAUI_DOCK_BOTTOM:
            if ( m_minDockAllowed & wxBOTTOM )
                direction = ibAUI_DOCK_BOTTOM;
            else if ( m_minDockAllowed & wxLEFT )
                direction = ibAUI_DOCK_LEFT;
            else if ( m_minDockAllowed & wxRIGHT )
                direction = ibAUI_DOCK_RIGHT;
            else if ( m_minDockAllowed & wxTOP )
                direction = ibAUI_DOCK_TOP;
            break;

        case ibAUI_DOCK_LEFT:
            if ( m_minDockAllowed & wxLEFT )
                direction = ibAUI_DOCK_LEFT;
            else if ( m_minDockAllowed & wxBOTTOM )
                direction = ibAUI_DOCK_BOTTOM;
            else if ( m_minDockAllowed & wxTOP )
                direction = ibAUI_DOCK_TOP;
            else if ( m_minDockAllowed & wxRIGHT )
                direction = ibAUI_DOCK_RIGHT;
            break;

        case ibAUI_DOCK_RIGHT:
            if ( m_minDockAllowed & wxRIGHT )
                direction = ibAUI_DOCK_RIGHT;
            else if ( m_minDockAllowed & wxBOTTOM )
                direction = ibAUI_DOCK_BOTTOM;
            else if ( m_minDockAllowed & wxTOP )
                direction = ibAUI_DOCK_TOP;
            else if ( m_minDockAllowed & wxLEFT )
                direction = ibAUI_DOCK_LEFT;
            break;

        case ibAUI_DOCK_NONE:
            // Shouldn't happen.
            wxFAIL_MSG( "Invalid pane docking direction" );
            return direction;

        case ibAUI_DOCK_CENTER:
            // May happen, but center panes can't be minimized, so just return
            // an invalid value for them.
            return direction;
    }

    // This shouldn't happen because we prevent m_minDockAllowed from being 0.
    wxASSERT_MSG( direction, "No allowed direction for minimizing pane?" );

    return direction;
}

ibAuiMinDock*&
ibAuiManager::GetMinDockInDirection(ibAuiManagerDock direction)
{
    if ( direction < ibAUI_DOCK_TOP || direction > ibAUI_DOCK_LEFT )
    {
        wxFAIL_MSG( "Invalid direction for minimized dock" );

        // Return a reference to something to avoid crashing below.
        direction = ibAUI_DOCK_TOP;
    }

    return m_minDocks[direction - 1];
}

void ibAuiManager::AddPaneToMinDockIfNecessary(ibAuiPaneInfo& pinfo)
{
    // Add panes that can be minimized to the corresponding docking bar if it's
    // already shown.
    if (pinfo.HasMinimizeButton())
    {
        auto const minDirection = GetMinDockDirectionFor(pinfo.dock_direction);
        if (minDirection != ibAUI_DOCK_NONE)
        {
            if (auto const minDock = GetMinDockInDirection(minDirection))
                minDock->AddPane(pinfo);
        }
    }
}

void
ibAuiManager::RemovePaneFromMinDockIfNecessary(ibAuiManagerDock direction,
                                               ibAuiPaneInfo& paneInfo)
{
    if(auto& minDock = GetMinDockInDirection(direction))
    {
        if (!minDock->RemovePane(paneInfo))
        {
            // The dock has become empty, so delete it.
            DetachPane(minDock);
            delete minDock;
            minDock = nullptr;
        }
    }
}

void ibAuiManager::MinimizePane(ibAuiPaneInfo& paneInfo)
{
    // Allowing to minimize arbitrary panes would break the UI invariant that
    // all panes that can be minimized, and only they, are shown in the
    // corresponding dock as soon as this dock is created.
    wxCHECK_RET
    (
        paneInfo.HasMinimizeButton(),
        "Only panes with minimize button can be minimized"
    );

    // Find the dock where the pane is minimized.
    auto const minDirection = GetMinDockDirectionFor(paneInfo.dock_direction);
    if ( minDirection == ibAUI_DOCK_NONE )
    {
        // This can only happen if this function is called for a center pane
        // normally (and if it happened for any other reason, an asert message
        // would have be already given by GetMinDockDirectionFor() itself).
        wxASSERT_MSG
        (
            paneInfo.dock_direction != ibAUI_DOCK_CENTER,
            "Center panes cannot be minimized"
        );
        return;
    }

    DoHidePaneWindow(paneInfo);

    paneInfo.Hide();

    auto& dock = GetMinDockInDirection(minDirection);
    if ( !dock )
    {
        dock = new ibAuiMinDock(*this, minDirection, m_minDockStyle);

        auto paneTB = ibAuiPaneInfo().
            Name(wxString::Format("minimized-dock-%d", minDirection)).
            ToolbarPane().
            Direction(minDirection).
            Gripper(false);

        AddPane(dock, paneTB);

        // Note that we want to add all panes that would minimize to this dock
        // to it as soon as it's shown, as it would be weird to have some panes
        // that would be shown in the dock when minimized already shown on it
        // but not others.
        for ( auto& p : m_panes )
        {
            if ( p.HasMinimizeButton() &&
                 !p.HasFlag(ibAuiPaneInfo::savedClosed) &&
                 GetMinDockDirectionFor(p.dock_direction) == minDirection )
            {
                dock->AddPane(p);
            }
        }
    }
    //else: If the dock already exists, the pane must be already shown in it.
}

void ibAuiManager::MaximizePane(ibAuiPaneInfo& paneInfo)
{
    // un-maximize and hide all other panes
    for ( auto& p : m_panes )
    {
        if (!p.IsToolbar() && !p.IsFloating())
        {
            p.Restore();

            // save hidden state
            p.SetFlag(ibAuiPaneInfo::savedHiddenState,
                      p.HasFlag(ibAuiPaneInfo::optionHidden));

            // hide the pane, because only the newly
            // maximized pane should show
            p.Hide();
        }
    }

    // mark ourselves maximized
    paneInfo.Maximize();
    paneInfo.Show();
    m_hasMaximized = true;

    // last, show the window
    if (paneInfo.window && !paneInfo.window->IsShown())
    {
        paneInfo.window->Show(true);
    }
}

void ibAuiManager::RestorePane(ibAuiPaneInfo& paneInfo)
{
    // restore all the panes
    for ( auto& p : m_panes )
    {
        if (!p.IsToolbar() && !p.IsFloating())
        {
            p.SetFlag(ibAuiPaneInfo::optionHidden,
                      p.HasFlag(ibAuiPaneInfo::savedHiddenState));
        }
    }

    // mark ourselves non-maximized
    paneInfo.Restore();
    m_hasMaximized = false;

    // last, show the window
    if (paneInfo.window && !paneInfo.window->IsShown())
    {
        paneInfo.window->Show(true);
    }
}

void ibAuiManager::RestoreMaximizedPane()
{
    // restore all the panes
    for ( auto& p : m_panes )
    {
        if (p.IsMaximized())
        {
            RestorePane(p);
            break;
        }
    }
}

// EscapeDelimiters() changes ";" into "\;" and "|" into "\|"
// in the input string.  This is an internal functions which is
// used for saving perspectives
static wxString EscapeDelimiters(const wxString& s)
{
    wxString result;
    result.Alloc(s.length());
    const wxChar* ch = s.c_str();
    while (*ch)
    {
        if (*ch == wxT(';') || *ch == wxT('|'))
            result += wxT('\\');
        result += *ch;
        ++ch;
    }
    return result;
}

wxString ibAuiManager::SavePaneInfo(const ibAuiPaneInfo& pane)
{
    wxString result = wxT("name=");
    result += EscapeDelimiters(pane.name);
    result += wxT(";");

    result += wxT("caption=");
    result += EscapeDelimiters(pane.caption);
    result += wxT(";");

    result += wxString::Format(wxT("state=%u;"), pane.state);
    result += wxString::Format(wxT("dir=%d;"), pane.dock_direction);
    result += wxString::Format(wxT("layer=%d;"), pane.dock_layer);
    result += wxString::Format(wxT("row=%d;"), pane.dock_row);
    result += wxString::Format(wxT("pos=%d;"), pane.dock_pos);
    result += wxString::Format(wxT("prop=%d;"), pane.dock_proportion);
    result += wxString::Format(wxT("bestw=%d;"), pane.best_size.x);
    result += wxString::Format(wxT("besth=%d;"), pane.best_size.y);
    result += wxString::Format(wxT("minw=%d;"), pane.min_size.x);
    result += wxString::Format(wxT("minh=%d;"), pane.min_size.y);
    result += wxString::Format(wxT("maxw=%d;"), pane.max_size.x);
    result += wxString::Format(wxT("maxh=%d;"), pane.max_size.y);
    result += wxString::Format(wxT("floatx=%d;"), pane.floating_pos.x);
    result += wxString::Format(wxT("floaty=%d;"), pane.floating_pos.y);
    result += wxString::Format(wxT("floatw=%d;"), pane.floating_size.x);
    result += wxString::Format(wxT("floath=%d;"), pane.floating_size.y);
    result += wxString::Format(wxT("floatw_cli=%d;"), pane.floating_client_size.x);
    result += wxString::Format(wxT("floath_cli=%d"), pane.floating_client_size.y);

    return result;
}

// Load a "pane" with the pane information settings in pane_part
void ibAuiManager::LoadPaneInfo(wxString pane_part, ibAuiPaneInfo &pane)
{
    // For backward compatibility, this function needs to handle
    // both layout2 and layout3.  However, layout3 is a superset
    // of layout2, so we don't need to actually check both
    LoadPaneInfoVersioned("layout3", pane_part, pane);
}

bool ibAuiManager::LoadPaneInfoVersioned(wxString layoutVersion, wxString pane_part, ibAuiPaneInfo& destination)
{
    // don't overwrite destination unless pane_part is valid
    ibAuiPaneInfo pane(destination);
    // replace escaped characters so we can
    // split up the string easily
    pane_part.Replace(wxT("\\|"), wxT("\a"));
    pane_part.Replace(wxT("\\;"), wxT("\b"));

    while(1)
    {
        wxString val_part = pane_part.BeforeFirst(wxT(';'));
        pane_part = pane_part.AfterFirst(wxT(';'));
        wxString val_name = val_part.BeforeFirst(wxT('='));
        wxString value = val_part.AfterFirst(wxT('='));
        val_name.MakeLower();
        val_name.Trim(true);
        val_name.Trim(false);
        value.Trim(true);
        value.Trim(false);

        if (val_name.empty())
            break;

        if (val_name == wxT("name"))
            pane.name = value;
        else if (val_name == wxT("caption"))
            pane.caption = value;
        else if (val_name == wxT("state"))
            pane.state = (unsigned int)wxAtoi(value.c_str());
        else if (val_name == wxT("dir"))
            pane.dock_direction = wxAtoi(value.c_str());
        else if (val_name == wxT("layer"))
            pane.dock_layer = wxAtoi(value.c_str());
        else if (val_name == wxT("row"))
            pane.dock_row = wxAtoi(value.c_str());
        else if (val_name == wxT("pos"))
            pane.dock_pos = wxAtoi(value.c_str());
        else if (val_name == wxT("prop"))
            pane.dock_proportion = wxAtoi(value.c_str());
        else if (val_name == wxT("bestw"))
            pane.best_size.x = wxAtoi(value.c_str());
        else if (val_name == wxT("besth"))
            pane.best_size.y = wxAtoi(value.c_str());
        else if (val_name == wxT("minw"))
            pane.min_size.x = wxAtoi(value.c_str());
        else if (val_name == wxT("minh"))
            pane.min_size.y = wxAtoi(value.c_str());
        else if (val_name == wxT("maxw"))
            pane.max_size.x = wxAtoi(value.c_str());
        else if (val_name == wxT("maxh"))
            pane.max_size.y = wxAtoi(value.c_str());
        else if (val_name == wxT("floatx"))
            pane.floating_pos.x = wxAtoi(value.c_str());
        else if (val_name == wxT("floaty"))
            pane.floating_pos.y = wxAtoi(value.c_str());
        else if (val_name == wxT("floatw"))
            pane.floating_size.x = wxAtoi(value.c_str());
        else if (val_name == wxT("floath"))
            pane.floating_size.y = wxAtoi(value.c_str());
        else if (val_name == wxT("floatw_cli") && layoutVersion == "layout3")
            pane.floating_client_size.x = wxAtoi(value.c_str());
        else if (val_name == wxT("floath_cli") && layoutVersion == "layout3")
            pane.floating_client_size.y = wxAtoi(value.c_str());
        else {
            return false;
        }
    }

    // replace escaped characters so we can
    // split up the string easily
    pane.name.Replace(wxT("\a"), wxT("|"));
    pane.name.Replace(wxT("\b"), wxT(";"));
    pane.caption.Replace(wxT("\a"), wxT("|"));
    pane.caption.Replace(wxT("\b"), wxT(";"));
    pane_part.Replace(wxT("\a"), wxT("|"));
    pane_part.Replace(wxT("\b"), wxT(";"));

    destination = pane;
    return true;
}


// SavePerspective() saves all pane information as a single string.
// This string may later be fed into LoadPerspective() to restore
// all pane settings.  This save and load mechanism allows an
// exact pane configuration to be saved and restored at a later time

wxString ibAuiManager::SavePerspective()
{
    wxString result;
    result.Alloc(500);
    result = wxT("layout3|");

    for ( const auto& pane : m_panes )
    {
        result += SavePaneInfo(pane)+wxT("|");
    }

    for ( const auto& dock : m_docks )
    {
        result += wxString::Format(wxT("dock_size(%d,%d,%d)=%d|"),
                                   dock.dock_direction, dock.dock_layer,
                                   dock.dock_row, dock.size);
    }

    return result;
}

// LoadPerspective() loads a layout which was saved with SavePerspective()
// If the "update" flag parameter is true, the GUI will immediately be updated

bool ibAuiManager::LoadPerspective(const wxString& layout, bool update)
{
    wxString input = layout;
    wxString layoutVersion;

    // check layout string version
    //    'layout1' = wxAUI 0.9.0 - wxAUI 0.9.2
    //    'layout2' = wxAUI 0.9.2 (wxWidgets 2.8)
    //    'layout3' = wxWidgets 3.3.1
    layoutVersion = input.BeforeFirst(wxT('|'));
    input = input.AfterFirst(wxT('|'));
    layoutVersion.Trim(true);
    layoutVersion.Trim(false);
    if (layoutVersion != wxT("layout2") &&
        layoutVersion != wxT("layout3"))
        return false;

    // Mark all panes currently managed as hidden. Also, dock all panes that are dockable.
    for ( auto& p : m_panes )
    {
        if(p.IsDockable())
            p.Dock();
        p.Hide();
    }

    // clear out the dock array; this will be reconstructed
    m_docks.Clear();

    // replace escaped characters so we can
    // split up the string easily
    input.Replace(wxT("\\|"), wxT("\a"));
    input.Replace(wxT("\\;"), wxT("\b"));

    m_hasMaximized = false;
    while (1)
    {
        ibAuiPaneInfo pane;

        wxString pane_part = input.BeforeFirst(wxT('|'));
        input = input.AfterFirst(wxT('|'));
        pane_part.Trim(true);

        // if the string is empty, we're done parsing
        if (pane_part.empty())
            break;

        if (pane_part.Left(9) == wxT("dock_size"))
        {
            wxString val_name = pane_part.BeforeFirst(wxT('='));
            wxString value = pane_part.AfterFirst(wxT('='));

            long dir, layer, row, size;
            wxString piece = val_name.AfterFirst(wxT('('));
            piece = piece.BeforeLast(wxT(')'));
            piece.BeforeFirst(wxT(',')).ToLong(&dir);
            piece = piece.AfterFirst(wxT(','));
            piece.BeforeFirst(wxT(',')).ToLong(&layer);
            piece.AfterFirst(wxT(',')).ToLong(&row);
            value.ToLong(&size);

            ibAuiDockInfo dock;
            dock.dock_direction = dir;
            dock.dock_layer = layer;
            dock.dock_row = row;
            dock.size = size;
            m_docks.Add(dock);
            continue;
        }

        // Undo our escaping as LoadPaneInfo needs to take an unescaped
        // name so it can be called by external callers
        pane_part.Replace(wxT("\a"), wxT("|"));
        pane_part.Replace(wxT("\b"), wxT(";"));

        if (!LoadPaneInfoVersioned(layoutVersion, pane_part, pane))
        {
            return false;
        }

        if ( pane.IsMaximized() )
            m_hasMaximized = true;

        ibAuiPaneInfo& p = GetPane(pane.name);
        if (!p.IsOk())
        {
            // the pane window couldn't be found
            // in the existing layout -- skip it
            continue;
        }

        p.SafeSet(pane);
    }

    if (update)
        Update();

    return true;
}

// These helper functions are used by SaveLayout() and LoadLayout() below, as
// we save the panes and docks geometries using DIPs on all platforms in order
// to ensure that they're restored correctly if the display DPI changes between
// saving and restoring the layout even on the platforms not using DIPs.
namespace
{

void MakeDIP(wxWindow* w, wxPoint& pos)
{
    pos = w->ToDIP(pos);
}

void MakeDIP(wxWindow* w, wxSize& size)
{
    size = w->ToDIP(size);
}

void MakeLogical(wxWindow* w, wxPoint& pos)
{
    pos = w->FromDIP(pos);
}

void MakeLogical(wxWindow* w, wxSize& size)
{
    size = w->FromDIP(size);
}

} // anonymous namespace

// Copy pane layout information between structs used when (de)serializing the
// layout and ibAuiPaneInfo itself.

void
ibAuiManager::CopyDockLayoutFrom(ibAuiDockLayoutInfo& dockInfo,
                                 const ibAuiPaneInfo& paneInfo) const
{
    dockInfo.dock_direction = paneInfo.dock_direction;
    dockInfo.dock_layer = paneInfo.dock_layer;
    dockInfo.dock_row = paneInfo.dock_row;
    dockInfo.dock_pos = paneInfo.dock_pos;
    dockInfo.dock_proportion = paneInfo.dock_proportion;
    dockInfo.dock_size = paneInfo.dock_size;

    // Storing the default proportion is not really useful and it looks weird
    // as it's an arbitrary huge number, so replace it with 0 in serialized
    // representation, it will be mapped back to maxDockProportion after load.
    if ( dockInfo.dock_proportion == maxDockProportion )
        dockInfo.dock_proportion = 0;
}

void
ibAuiManager::CopyDockLayoutTo(const ibAuiDockLayoutInfo& dockInfo,
                               ibAuiPaneInfo& paneInfo) const
{
    paneInfo.dock_direction = dockInfo.dock_direction;
    paneInfo.dock_layer = dockInfo.dock_layer;
    paneInfo.dock_row = dockInfo.dock_row;
    paneInfo.dock_pos = dockInfo.dock_pos;
    paneInfo.dock_proportion = dockInfo.dock_proportion;
    paneInfo.dock_size = dockInfo.dock_size;

    // Undo the transformation done in CopyDockLayoutFrom() above.
    if ( dockInfo.dock_proportion == 0 )
        paneInfo.dock_proportion = maxDockProportion;
}

void
ibAuiManager::CopyLayoutFrom(ibAuiPaneLayoutInfo& layoutInfo,
                             const ibAuiPaneInfo& pane) const
{
    CopyDockLayoutFrom(layoutInfo, pane);

    layoutInfo.floating_pos = pane.floating_pos;
    layoutInfo.floating_size = pane.floating_size;
    layoutInfo.floating_client_size = pane.floating_client_size;

    layoutInfo.is_maximized = pane.HasFlag(ibAuiPaneInfo::optionMaximized);
    layoutInfo.is_hidden = pane.HasFlag(ibAuiPaneInfo::optionHidden);
}

void
ibAuiManager::CopyLayoutTo(const ibAuiPaneLayoutInfo& layoutInfo,
                           ibAuiPaneInfo& pane) const
{
    CopyDockLayoutTo(layoutInfo, pane);

    pane.floating_pos = layoutInfo.floating_pos;
    pane.floating_size = layoutInfo.floating_size;
    pane.floating_client_size = layoutInfo.floating_client_size;

    pane.SetFlag(ibAuiPaneInfo::optionMaximized, layoutInfo.is_maximized);
    pane.SetFlag(ibAuiPaneInfo::optionHidden, layoutInfo.is_hidden);
}

void ibAuiManager::SaveLayout(ibAuiSerializer& serializer) const
{
    serializer.BeforeSave();

    if ( !m_panes.empty() )
    {
        serializer.BeforeSavePanes();

        // Collect information about all the notebooks we may have while saving
        // the panes layout.
        std::map<wxString, ibAuiNotebook*> notebooks;

        for ( const auto& pane : m_panes )
        {
            ibAuiPaneLayoutInfo layoutInfo{pane.name};
            CopyLayoutFrom(layoutInfo, pane);

            MakeDIP(m_frame, layoutInfo.floating_pos);
            MakeDIP(m_frame, layoutInfo.floating_size);
            MakeDIP(m_frame, layoutInfo.floating_client_size);

            serializer.SavePane(layoutInfo);

            if ( auto* const nb = wxDynamicCast(pane.window, ibAuiNotebook) )
            {
                notebooks[pane.name] = nb;
            }
        }

        serializer.AfterSavePanes();

        if ( !notebooks.empty() )
        {
            serializer.BeforeSaveNotebooks();

            for ( const auto& kv : notebooks )
            {
                kv.second->SaveLayout(kv.first, serializer);
            }

            serializer.AfterSaveNotebooks();
        }
    }

    serializer.AfterSave();
}

void ibAuiManager::LoadLayout(ibAuiDeserializer& deserializer)
{
    deserializer.BeforeLoad();

    // This will be non-empty only if we have a maximized pane.
    wxString maximizedPaneName;

    // Also keep local variables for the existing (and possibly updated) panes
    // and the new ones for the same reason.
    ibAuiPaneInfoArray panes = m_panes;

    struct NewPane
    {
        // In C++11 this ctor is required.
        NewPane(wxWindow* window_, const ibAuiPaneInfo& info_)
            : window(window_), info(info_)
        {
        }

        wxWindow* window = nullptr;
        ibAuiPaneInfo info;
    };
    std::vector<NewPane> newPanes;

    auto layoutInfos = deserializer.LoadPanes();
    for ( auto& layoutInfo : layoutInfos )
    {
        MakeLogical(m_frame, layoutInfo.floating_pos);
        MakeLogical(m_frame, layoutInfo.floating_size);
        MakeLogical(m_frame, layoutInfo.floating_client_size);

        // Find the pane with the same name in the existing layout.
        wxWindow* window = nullptr;
        for ( auto& existingPane : panes )
        {
            if ( existingPane.name == layoutInfo.name )
            {
                // Update the existing pane with the restored layout.
                CopyLayoutTo(layoutInfo, existingPane);

                if ( layoutInfo.is_maximized )
                    maximizedPaneName = existingPane.name;

                window = existingPane.window;
                break;
            }
        }

        // This pane couldn't be found in the existing layout, let deserializer
        // create a new window for it if desired, otherwise just ignore it.
        if ( !window )
        {
            ibAuiPaneInfo pane;
            pane.name = layoutInfo.name;
            CopyLayoutTo(layoutInfo, pane);

            window = deserializer.CreatePaneWindow(pane);
            if ( !window )
                continue;

            newPanes.emplace_back(window, pane);

            if ( layoutInfo.is_maximized )
                maximizedPaneName = pane.name;
        }

        if ( auto* const nb = wxDynamicCast(window, ibAuiNotebook) )
        {
            nb->LoadLayout(layoutInfo.name, deserializer);
        }
    }

    // After loading everything successfully, do update the internal variables.
    m_panes.swap(panes);

    for ( const auto& newPane : newPanes )
        AddPane(newPane.window, newPane.info);

    if ( !maximizedPaneName.empty() )
        MaximizePane(GetPane(maximizedPaneName));

    // Force recreating the docks using the new sizes from the panes.
    m_docks.clear();

    deserializer.AfterLoad();
}

void ibAuiManager::GetPanePositionsAndSizes(ibAuiDockInfo& dock,
                                            wxArrayInt& positions,
                                            wxArrayInt& sizes)
{
    positions.Empty();
    sizes.Empty();

    int action_pane = -1;
    int pane_i, pane_count = dock.panes.GetCount();

    // find the pane marked as our action pane
    for (pane_i = 0; pane_i < pane_count; ++pane_i)
    {
        ibAuiPaneInfo& pane = *(dock.panes.Item(pane_i));

        if (pane.HasFlag(ibAuiPaneInfo::actionPane))
        {
            wxASSERT_MSG(action_pane==-1, wxT("Too many fixed action panes"));
            action_pane = pane_i;
        }
    }

    // set up each panes default position, and
    // determine the size (width or height, depending
    // on the dock's orientation) of each pane
    for ( auto* p : dock.panes )
    {
        ibAuiPaneInfo& pane = *p;
        int caption_size = GetMetricForWindow(ibAUI_DOCKART_CAPTION_SIZE, pane.window);
        int pane_borderSize = GetMetricForWindow(ibAUI_DOCKART_PANE_BORDER_SIZE, pane.window);
        int gripperSize = GetMetricForWindow(ibAUI_DOCKART_GRIPPER_SIZE, pane.window);

        positions.Add(pane.dock_pos);
        int size = 0;

        if (pane.HasBorder())
            size += (pane_borderSize*2);

        if (dock.IsHorizontal())
        {
            if (pane.HasGripper() && !pane.HasGripperTop())
                size += gripperSize;
            size += pane.best_size.x;
        }
        else
        {
            if (pane.HasGripper() && pane.HasGripperTop())
                size += gripperSize;

            if (pane.HasCaption())
                size += caption_size;
            size += pane.best_size.y;
        }

        sizes.Add(size);
    }

    // if there is no action pane, just return the default
    // positions (as specified in pane.pane_pos)
    if (action_pane == -1)
        return;

    for (pane_i = action_pane-1; pane_i >= 0; --pane_i)
    {
        int amount = positions[pane_i+1] - (positions[pane_i] + sizes[pane_i]);

        if (amount >= 0)
            ;
        else
            positions[pane_i] -= -amount;
    }

    // if the dock mode is fixed, make sure none of the panes
    // overlap; we will bump panes that overlap
    int offset = 0;
    for (pane_i = action_pane; pane_i < pane_count; ++pane_i)
    {
        int amount = positions[pane_i] - offset;
        if (amount >= 0)
            offset += amount;
        else
            positions[pane_i] += -amount;

        offset += sizes[pane_i];
    }
}


void ibAuiManager::LayoutAddPane(wxSizer* cont,
                                 ibAuiDockInfo& dock,
                                 ibAuiPaneInfo& pane,
                                 ibAuiDockUIPartArray& uiparts,
                                 bool spacer_only)
{
    ibAuiDockUIPart part;
    wxSizerItem* sizer_item;

    int caption_size = GetMetricForWindow(ibAUI_DOCKART_CAPTION_SIZE, pane.window);
    int gripperSize = GetMetricForWindow(ibAUI_DOCKART_GRIPPER_SIZE, pane.window);
    int pane_borderSize = GetMetricForWindow(ibAUI_DOCKART_PANE_BORDER_SIZE, pane.window);
    int pane_button_size = GetMetricForWindow(ibAUI_DOCKART_PANE_BUTTON_SIZE, pane.window);

    // find out the orientation of the item (orientation for panes
    // is the same as the dock's orientation)
    int orientation;
    if (dock.IsHorizontal())
        orientation = wxHORIZONTAL;
    else
        orientation = wxVERTICAL;

    // this variable will store the proportion
    // value that the pane will receive
    int pane_proportion = pane.dock_proportion;

    wxBoxSizer* horz_pane_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* vert_pane_sizer = new wxBoxSizer(wxVERTICAL);

    if (pane.HasGripper())
    {
        if (pane.HasGripperTop())
            sizer_item = vert_pane_sizer ->Add(1, gripperSize, 0, wxEXPAND);
        else
            sizer_item = horz_pane_sizer ->Add(gripperSize, 1, 0, wxEXPAND);

        part.type = ibAuiDockUIPart::typeGripper;
        part.dock = &dock;
        part.pane = &pane;
        part.button = 0;
        part.orientation = orientation;
        part.cont_sizer = horz_pane_sizer;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }

    if (pane.HasCaption())
    {
        // create the caption sizer
        wxBoxSizer* caption_sizer = new wxBoxSizer(wxHORIZONTAL);

        sizer_item = caption_sizer->Add(1, caption_size, 1, wxEXPAND);

        part.type = ibAuiDockUIPart::typeCaption;
        part.dock = &dock;
        part.pane = &pane;
        part.button = 0;
        part.orientation = orientation;
        part.cont_sizer = vert_pane_sizer;
        part.sizer_item = sizer_item;
        int caption_part_idx = uiparts.GetCount();
        uiparts.Add(part);

        // add pane buttons to the caption
        int button_count = 0;
        constexpr int NUM_SUPPORTED_BUTTONS = 4;
        ibAuiButtonId buttons[NUM_SUPPORTED_BUTTONS] = {
            ibAUI_BUTTON_MINIMIZE,
            ibAUI_BUTTON_MAXIMIZE_RESTORE,
            ibAUI_BUTTON_PIN,
            ibAUI_BUTTON_CLOSE
        };
        int flags[NUM_SUPPORTED_BUTTONS] = {
            ibAuiPaneInfo::buttonMinimize,
            ibAuiPaneInfo::buttonMaximize,
            ibAuiPaneInfo::buttonPin,
            ibAuiPaneInfo::buttonClose
        };

        for (int i = 0; i < NUM_SUPPORTED_BUTTONS; ++i)
        {
            if (pane.HasFlag(flags[i]))
            {
                sizer_item = caption_sizer->Add(pane_button_size,
                    caption_size,
                    0, wxEXPAND);

                part.type = ibAuiDockUIPart::typePaneButton;
                part.dock = &dock;
                part.pane = &pane;
                part.button = buttons[i];
                part.orientation = orientation;
                part.cont_sizer = caption_sizer;
                part.sizer_item = sizer_item;
                uiparts.Add(part);
                button_count++;
            }
        }

        // if we have buttons, add a little space to the right
        // of them to ease visual crowding
        if (button_count >= 1)
        {
            caption_sizer->Add(m_frame->FromDIP(3),1);
        }

        // add the caption sizer
        sizer_item = vert_pane_sizer->Add(caption_sizer, 0, wxEXPAND);

        uiparts.Item(caption_part_idx).sizer_item = sizer_item;
    }

    // add the pane window itself
    if (spacer_only)
    {
        sizer_item = vert_pane_sizer->Add(1, 1, 1, wxEXPAND);
    }
    else
    {
        sizer_item = vert_pane_sizer->Add(pane.window, 1, wxEXPAND);
    }

    part.type = ibAuiDockUIPart::typePane;
    part.dock = &dock;
    part.pane = &pane;
    part.button = 0;
    part.orientation = orientation;
    part.cont_sizer = vert_pane_sizer;
    part.sizer_item = sizer_item;
    uiparts.Add(part);


    // determine if the pane should have a minimum size; if the pane is
    // non-resizable (fixed) then we must set a minimum size. Alternatively,
    // if the pane.min_size is set, we must use that value as well

    wxSize min_size = pane.min_size;
    if (pane.IsFixed())
    {
        if (min_size == wxDefaultSize)
        {
            min_size = pane.best_size;

            // Toolbars may be fixed, i.e. non-resizable, but still need to
            // stretch if they contain stretchable spacers, so we should avoid
            // setting their proportion to 0 in this case.
            auto* const toolbar = wxDynamicCast(pane.window, ibAuiToolBar);
            if (!toolbar || !toolbar->CanStretch())
                pane_proportion = 0;
        }
    }

    // We need to reset any previous set min size to allow decreasing the pane
    // size by dragging the sash between it and other panes, so always set it
    // to something, even if it's not specified.
    min_size.IncTo(wxSize(1, 1));

    sizer_item->SetMinSize(min_size);


    // add the vertical sizer (caption, pane window) to the
    // horizontal sizer (gripper, vertical sizer)
    horz_pane_sizer->Add(vert_pane_sizer, 1, wxEXPAND);

    // finally, add the pane sizer to the dock sizer

    if (pane.HasBorder())
    {
        // allowing space for the pane's border
        sizer_item = cont->Add(horz_pane_sizer, pane_proportion,
                               wxEXPAND | wxALL, pane_borderSize);

        part.type = ibAuiDockUIPart::typePaneBorder;
        part.dock = &dock;
        part.pane = &pane;
        part.button = 0;
        part.orientation = orientation;
        part.cont_sizer = cont;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }
    else
    {
        cont->Add(horz_pane_sizer, pane_proportion, wxEXPAND);
    }
}

void ibAuiManager::LayoutAddDock(wxSizer* cont,
                                 ibAuiDockInfo& dock,
                                 ibAuiDockUIPartArray& uiparts,
                                 bool spacer_only)
{
    wxSizerItem* sizer_item;
    ibAuiDockUIPart part;

    int sashSize = GetMetricForWindow(ibAUI_DOCKART_SASH_SIZE, m_frame);
    int orientation = dock.IsHorizontal() ? wxHORIZONTAL : wxVERTICAL;

    // resizable bottom and right docks have a sash before them
    if (!m_hasMaximized && !dock.fixed && (dock.dock_direction == ibAUI_DOCK_BOTTOM ||
                        dock.dock_direction == ibAUI_DOCK_RIGHT))
    {
        sizer_item = cont->Add(sashSize, sashSize, 0, wxEXPAND);

        part.type = ibAuiDockUIPart::typeDockSizer;
        part.orientation = orientation;
        part.dock = &dock;
        part.pane = nullptr;
        part.button = 0;
        part.cont_sizer = cont;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }

    // create the sizer for the dock
    wxSizer* dock_sizer = new wxBoxSizer(orientation);

    // add each pane to the dock
    bool has_maximized_pane = false;

    if (dock.fixed)
    {
        wxArrayInt pane_positions, pane_sizes;

        // figure out the real pane positions we will
        // use, without modifying the each pane's pane_pos member
        GetPanePositionsAndSizes(dock, pane_positions, pane_sizes);

        int offset = 0;
        const int pane_count = dock.panes.GetCount();
        for (int pane_i = 0; pane_i < pane_count; ++pane_i)
        {
            ibAuiPaneInfo& pane = *(dock.panes.Item(pane_i));
            int pane_pos = pane_positions.Item(pane_i);

            if (pane.IsMaximized())
                has_maximized_pane = true;


            int amount = pane_pos - offset;
            if (amount > 0)
            {
                if (dock.IsVerticalOrCenter())
                    sizer_item = dock_sizer->Add(1, amount, 0, wxEXPAND);
                else
                    sizer_item = dock_sizer->Add(amount, 1, 0, wxEXPAND);

                part.type = ibAuiDockUIPart::typeBackground;
                part.dock = &dock;
                part.pane = nullptr;
                part.button = 0;
                part.orientation = (orientation==wxHORIZONTAL) ? wxVERTICAL:wxHORIZONTAL;
                part.cont_sizer = dock_sizer;
                part.sizer_item = sizer_item;
                uiparts.Add(part);

                offset += amount;
            }

            LayoutAddPane(dock_sizer, dock, pane, uiparts, spacer_only);

            offset += pane_sizes.Item(pane_i);
        }

        // at the end add a very small stretchable background area
        sizer_item = dock_sizer->Add(0,0, 1, wxEXPAND);

        part.type = ibAuiDockUIPart::typeBackground;
        part.dock = &dock;
        part.pane = nullptr;
        part.button = 0;
        part.orientation = orientation;
        part.cont_sizer = dock_sizer;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }
    else
    {
        ibAuiPaneInfo* last_pane = nullptr;
        for ( auto* p : dock.panes )
        {
            ibAuiPaneInfo& pane = *p;

            if (pane.IsMaximized())
                has_maximized_pane = true;

            // if this is not the first pane being added,
            // we need to add a pane sizer
            if (!m_hasMaximized && last_pane)
            {
                sizer_item = dock_sizer->Add(sashSize, sashSize, 0, wxEXPAND);

                part.type = ibAuiDockUIPart::typePaneSizer;
                part.dock = &dock;
                part.pane = last_pane;
                part.button = 0;
                part.orientation = (orientation==wxHORIZONTAL) ? wxVERTICAL:wxHORIZONTAL;
                part.cont_sizer = dock_sizer;
                part.sizer_item = sizer_item;
                uiparts.Add(part);
            }

            LayoutAddPane(dock_sizer, dock, pane, uiparts, spacer_only);

            last_pane = p;
        }
    }

    if (dock.dock_direction == ibAUI_DOCK_CENTER || has_maximized_pane)
        sizer_item = cont->Add(dock_sizer, 1, wxEXPAND);
    else
        sizer_item = cont->Add(dock_sizer, 0, wxEXPAND);

    part.type = ibAuiDockUIPart::typeDock;
    part.dock = &dock;
    part.pane = nullptr;
    part.button = 0;
    part.orientation = orientation;
    part.cont_sizer = cont;
    part.sizer_item = sizer_item;
    uiparts.Add(part);

    if (dock.IsHorizontal())
        cont->SetItemMinSize(dock_sizer, 0, dock.size);
    else
        cont->SetItemMinSize(dock_sizer, dock.size, 0);

    //  top and left docks have a sash after them
    if (!m_hasMaximized &&
        !dock.fixed &&
          (dock.dock_direction == ibAUI_DOCK_TOP ||
           dock.dock_direction == ibAUI_DOCK_LEFT))
    {
        sizer_item = cont->Add(sashSize, sashSize, 0, wxEXPAND);

        part.type = ibAuiDockUIPart::typeDockSizer;
        part.dock = &dock;
        part.pane = nullptr;
        part.button = 0;
        part.orientation = orientation;
        part.cont_sizer = cont;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }
}

wxSizer* ibAuiManager::LayoutAll(ibAuiPaneInfoArray& panes,
                                 ibAuiDockInfoArray& docks,
                                 ibAuiDockUIPartArray& uiparts,
                                 bool spacer_only)
{
    int pane_borderSize = GetMetricForWindow(ibAUI_DOCKART_PANE_BORDER_SIZE, m_frame);
    int caption_size = GetMetricForWindow(ibAUI_DOCKART_CAPTION_SIZE, m_frame);
    wxSize cli_size = m_frame->GetClientSize();


    // empty all docks out
    for ( auto& dock : docks )
    {
        // empty out all panes, as they will be readded below
        dock.panes.Empty();

        if (dock.fixed)
        {
            // always reset fixed docks' sizes, because
            // the contained windows may have been resized
            dock.size = 0;
        }
        //else: non-fixed docks must keep their size to allow resizing them
        // interactively, if we reset their size too here, they would always
        // have the size determined by their contents and drag-resizing them
        // wouldn't work
    }


    // iterate through all known panes, filing each
    // of them into the appropriate dock. If the
    // pane does not exist in the dock, add it
    for ( auto& p : panes )
    {
        // find any docks with the same dock direction, dock layer, and
        // dock row as the pane we are working on
        ibAuiDockInfo* dock = nullptr;
        for ( auto dockInfo : FindDocks(docks,
                                        p.dock_direction,
                                        p.dock_layer,
                                        p.dock_row,
                                        FindDocksFlags::OnlyFirst) )
        {
            // found the right dock
            dock = dockInfo;

            // if we've just recreated it, apply the dock size possibly saved
            // in the pane to it
            if ( dock->size == 0 )
                dock->size = p.dock_size;
        }

        if ( !p.IsDocked() || !p.IsShown() )
        {
            // remove the pane from any existing docks
            RemovePaneFromDocks(docks, p);
            continue;
        }

        if ( !dock )
        {
            // dock was not found, so we need to create a new one
            ibAuiDockInfo d;
            d.dock_direction = p.dock_direction;
            d.dock_layer = p.dock_layer;
            d.dock_row = p.dock_row;
            d.size = p.dock_size;
            docks.Add(d);
            dock = &docks.Last();
        }


        // remove the pane from any existing docks except this one
        RemovePaneFromDocks(docks, p, dock);

        // pane needs to be added to the dock,
        // if it doesn't already exist
        if (!FindPaneInDock(*dock, p.window))
            dock->panes.Add(&p);
    }

    // remove any empty docks
    for (int i = docks.GetCount()-1; i >= 0; --i)
    {
        if (docks.Item(i).panes.GetCount() == 0)
            docks.RemoveAt(i);
    }

    // configure the docks further
    for ( auto& dock : docks )
    {
        int j, dock_pane_count = dock.panes.GetCount();
        // sort the dock pane array by the pane's
        // dock position (dock_pos), in ascending order
        dock.panes.Sort(PaneSortFunc);

        // for newly created docks, set up their initial size
        if (dock.size == 0)
        {
            int size = 0;

            for ( const auto* p : dock.panes )
            {
                const ibAuiPaneInfo& pane = *p;
                wxSize pane_size = pane.best_size;
                if (pane_size == wxDefaultSize)
                    pane_size = pane.min_size;
                if (pane_size == wxDefaultSize)
                    pane_size = pane.window->GetSize();

                pane_size.IncTo(GetMinPaneSize());

                if (dock.IsHorizontal())
                    size = wxMax(pane_size.y, size);
                else
                    size = wxMax(pane_size.x, size);
            }

            // add space for the border (two times), but only
            // if at least one pane inside the dock has a pane border
            for ( const auto* p : dock.panes )
            {
                if (p->HasBorder())
                {
                    size += (pane_borderSize*2);
                    break;
                }
            }

            // if pane is on the top or bottom, add the caption height,
            // but only if at least one pane inside the dock has a caption
            if (dock.IsHorizontal())
            {
                for ( const auto* p : dock.panes )
                {
                    if (p->HasCaption())
                    {
                        size += caption_size;
                        break;
                    }
                }
            }


            // new dock's size may not be more than the dock constraint
            // parameter specifies.  See SetDockSizeConstraint()

            int max_dock_x_size = (int)(m_dockConstraintX * ((double)cli_size.x));
            int max_dock_y_size = (int)(m_dockConstraintY * ((double)cli_size.y));

            if (dock.IsHorizontal())
                size = wxMin(size, max_dock_y_size);
            else
                size = wxMin(size, max_dock_x_size);

            dock.size = size;
        }


        // determine the dock's minimum size
        bool plus_border = false;
        bool plus_caption = false;
        int dock_min_size = 0;
        for ( const auto* p : dock.panes )
        {
            const ibAuiPaneInfo& pane = *p;
            if (pane.min_size != wxDefaultSize)
            {
                if (pane.HasBorder())
                    plus_border = true;
                if (pane.HasCaption())
                    plus_caption = true;
                if (dock.IsHorizontal())
                {
                    if (pane.min_size.y > dock_min_size)
                        dock_min_size = pane.min_size.y;
                }
                else
                {
                    if (pane.min_size.x > dock_min_size)
                        dock_min_size = pane.min_size.x;
                }
            }
        }

        if (plus_border)
            dock_min_size += (pane_borderSize*2);
        if (plus_caption && dock.IsHorizontal())
            dock_min_size += (caption_size);

        dock.min_size = dock_min_size;


        // if the pane's current size is less than its
        // minimum, increase the dock's size to its minimum
        if (dock.size < dock.min_size)
            dock.size = dock.min_size;


        // determine the dock's mode (fixed or proportional);
        // determine whether the dock has only toolbars
        bool action_pane_marked = false;
        dock.fixed = true;
        dock.toolbar = true;
        for ( const auto* p : dock.panes )
        {
            const ibAuiPaneInfo& pane = *p;
            if (!pane.IsFixed())
                dock.fixed = false;
            if (!pane.IsToolbar())
                dock.toolbar = false;
            if (pane.HasFlag(ibAuiPaneInfo::optionDockFixed))
                dock.fixed = true;
            if (pane.HasFlag(ibAuiPaneInfo::actionPane))
                action_pane_marked = true;
        }


        // if the dock mode is proportional and not fixed-pixel,
        // reassign the dock_pos to the sequential 0, 1, 2, 3;
        // e.g. remove gaps like 1, 2, 30, 500
        if (!dock.fixed)
        {
            for (j = 0; j < dock_pane_count; ++j)
            {
                ibAuiPaneInfo& pane = *dock.panes.Item(j);
                pane.dock_pos = j;
            }
        }

        // if the dock mode is fixed, and none of the panes
        // are being moved right now, make sure the panes
        // do not overlap each other.  If they do, we will
        // adjust the positions of the panes
        if (dock.fixed && !action_pane_marked)
        {
            wxArrayInt pane_positions, pane_sizes;
            GetPanePositionsAndSizes(dock, pane_positions, pane_sizes);

            int offset = 0;
            for (j = 0; j < dock_pane_count; ++j)
            {
                ibAuiPaneInfo& pane = *(dock.panes.Item(j));
                pane.dock_pos = pane_positions[j];

                int amount = pane.dock_pos - offset;
                if (amount >= 0)
                    offset += amount;
                else
                    pane.dock_pos += -amount;

                offset += pane_sizes[j];
            }
        }
    }

    // discover the maximum dock layer
    int max_layer = 0;
    for ( const auto& dock : docks )
        max_layer = wxMax(max_layer, dock.dock_layer);


    // clear out uiparts
    uiparts.Empty();

    // create a bunch of box sizers,
    // from the innermost level outwards.
    wxSizer* cont = nullptr;
    wxSizer* middle = nullptr;

    for (int layer = 0; layer <= max_layer; ++layer)
    {
        // find any docks in this layer
        // if there aren't any, skip to the next layer
        if ( FindDocks(docks, -1, layer, -1, FindDocksFlags::OnlyFirst).IsEmpty() )
            continue;

        wxSizer* old_cont = cont;

        // create a container which will hold this layer's
        // docks (top, bottom, left, right)
        cont = new wxBoxSizer(wxVERTICAL);


        // find any top docks in this layer
        for ( auto dockInfo : FindDocks(docks, ibAUI_DOCK_TOP, layer, -1) )
        {
            LayoutAddDock(cont, *dockInfo, uiparts, spacer_only);
        }


        // fill out the middle layer (which consists
        // of left docks, content area and right docks)

        middle = new wxBoxSizer(wxHORIZONTAL);

        // find any left docks in this layer
        for ( auto dockInfo : FindDocks(docks, ibAUI_DOCK_LEFT, layer, -1) )
        {
            LayoutAddDock(middle, *dockInfo, uiparts, spacer_only);
        }

        // add content dock (or previous layer's sizer
        // to the middle
        if (!old_cont)
        {
            // find any center docks
            bool hasCenter = false;
            for ( auto dockInfo : FindDocks(docks, ibAUI_DOCK_CENTER, -1, -1) )
            {
                LayoutAddDock(middle, *dockInfo, uiparts, spacer_only);
                hasCenter = true;
            }
            if (!hasCenter && !m_hasMaximized)
            {
                // there are no center docks, add a background area
                wxSizerItem* sizer_item = middle->Add(1,1, 1, wxEXPAND);
                ibAuiDockUIPart part;
                part.type = ibAuiDockUIPart::typeBackground;
                part.pane = nullptr;
                part.dock = nullptr;
                part.button = 0;
                part.cont_sizer = middle;
                part.sizer_item = sizer_item;
                uiparts.Add(part);
            }
        }
        else
        {
            middle->Add(old_cont, 1, wxEXPAND);
        }

        // find any right docks in this layer
        for ( auto dockInfo : FindDocks(docks, ibAUI_DOCK_RIGHT, layer, -1,
                                        FindDocksFlags::ReverseOrder) )
        {
            LayoutAddDock(middle, *dockInfo, uiparts, spacer_only);
        }

        if (middle->GetChildren().GetCount() > 0)
            cont->Add(middle, 1, wxEXPAND);
        else
            delete middle;



        // find any bottom docks in this layer
        for ( auto dockInfo : FindDocks(docks, ibAUI_DOCK_BOTTOM, layer, -1,
                                        FindDocksFlags::ReverseOrder) )
        {
            LayoutAddDock(cont, *dockInfo, uiparts, spacer_only);
        }

    }

    if (!cont)
    {
        // no sizer available, because there are no docks,
        // therefore we will create a simple background area
        cont = new wxBoxSizer(wxVERTICAL);
        wxSizerItem* sizer_item = cont->Add(1,1, 1, wxEXPAND);
        ibAuiDockUIPart part;
        part.type = ibAuiDockUIPart::typeBackground;
        part.pane = nullptr;
        part.dock = nullptr;
        part.button = 0;
        part.cont_sizer = middle;
        part.sizer_item = sizer_item;
        uiparts.Add(part);
    }

    return cont;
}


// SetDockSizeConstraint() allows the dock constraints to be set.  For example,
// specifying values of 0.5, 0.5 will mean that upon dock creation, a dock may
// not be larger than half of the window's size

void ibAuiManager::SetDockSizeConstraint(double width_pct, double height_pct)
{
    m_dockConstraintX = wxMax(0.0, wxMin(1.0, width_pct));
    m_dockConstraintY = wxMax(0.0, wxMin(1.0, height_pct));
}

void ibAuiManager::GetDockSizeConstraint(double* width_pct, double* height_pct) const
{
    if (width_pct)
        *width_pct = m_dockConstraintX;

    if (height_pct)
        *height_pct = m_dockConstraintY;
}



// Update() updates the layout.  Whenever changes are made to
// one or more panes, this function should be called.  It is the
// external entry point for running the layout engine.

void ibAuiManager::Update()
{
    wxTopLevelWindow * const
        tlw = wxDynamicCast(wxGetTopLevelParent(m_frame), wxTopLevelWindow);
    if ( tlw && tlw->IsIconized() )
    {
        // We can't compute the layout correctly when the frame is minimized
        // because at least under MSW its client size is (0,0) in this case
        // but, luckily, we don't need to do it right now anyhow.
        m_updateOnRestore = true;
        return;
    }

    m_hoverButton = nullptr;
    m_actionPart = nullptr;

    for ( auto* minDock : m_minDocks )
    {
        if ( minDock && minDock->RealizeIfNeeded() )
        {
            // Update best size to account for the added or removed tools.
            GetPane(minDock).BestSize(minDock->GetBestSize());
        }
    }

    wxSizer* sizer;
    int i, pane_count = m_panes.GetCount();


    // destroy floating panes which have been
    // redocked or are becoming non-floating
    for ( auto& p : m_panes )
    {
        if (!p.IsFloating() && p.frame)
        {
            // because the pane is no longer in a floating, we need to
            // reparent it to m_frame and destroy the floating frame

            // reduce flicker
            p.window->SetSize(1,1);


            // the following block is a workaround for bug #1531361
            // (see wxWidgets sourceforge page).  On wxGTK (only), when
            // a frame is shown/hidden, a move event unfortunately
            // also gets fired.  Because we may be dragging around
            // a pane, we need to cancel that action here to prevent
            // a spurious crash.
            if (m_actionWindow == p.frame)
            {
                if (wxWindow::GetCapture() == m_frame)
                    m_frame->ReleaseMouse();
                m_action = actionNone;
                m_actionWindow = nullptr;
            }

            // hide the frame
            if (p.frame->IsShown())
                p.frame->Show(false);

            DestroyFloatingFrame(p);
        }
    }

    // Disable all updates until everything can be repainted at once at the end
    // when not using live resizing.
    //
    // Note that:
    //  - This is useless under Mac, where HasLiveResize() always returns false.
    //  - This is harmful under GTK, where it results in extra flicker (sic).
    //  - This results in display artefacts when using live resizing under MSW.
    //
    // So we only do this under MSW and only when not using live resizing.
#ifdef __WXMSW__
    wxWindowUpdateLocker noUpdates;
    if (!HasLiveResize())
        noUpdates.Lock(m_frame);
#endif // __WXMSW__

    // delete old sizer first
    m_frame->SetSizer(nullptr);

    // create a layout for all of the panes
    sizer = LayoutAll(m_panes, m_docks, m_uiParts, false);

    // Store the current dock size in all of its panes, as the docks may be
    // recreated from the panes later, e.g. when all panes in the dock are
    // hidden and then shown again or when the docks are cleared on DPI change,
    // and we want to preserve the size in this case.
    for ( const auto& dock : m_docks )
    {
        // Don't do it for fixed docks, however, as their size is computed from
        // the size of their panes and storing the dock size in the panes would
        // prevent the dock from shrinking when an element with the biggest
        // size across the dock's orientation is removed from it.
        if ( dock.fixed )
            continue;

        for ( auto* p : dock.panes )
            p->dock_size = dock.size;
    }

    // hide or show panes as necessary,
    // and float panes as necessary
    for ( auto& p : m_panes )
    {
        if (p.IsFloating())
        {
            if (p.frame == nullptr)
            {
                // we need to create a frame for this
                // pane, which has recently been floated
                ibAuiFloatingFrame* frame = CreateFloatingFrame(m_frame, p);

                // on MSW and Mac, if the owner desires transparent dragging, and
                // the dragging is happening right now, then the floating
                // window should have this style by default
                if (m_action == actionDragFloatingPane &&
                    (m_flags & ibAUI_MGR_TRANSPARENT_DRAG))
                        frame->SetTransparent(150);

                frame->SetPaneWindow(p);
                p.frame = frame;

                if (p.IsShown() && !frame->IsShown())
                    frame->Show();
            }
            else
            {
                // frame already exists, make sure its position
                // and size reflect the information in ibAuiPaneInfo
                // give floating_client_size precedence over floating_size
                if ((p.frame->GetPosition() != p.floating_pos) ||
                    ((p.floating_size != wxDefaultSize) && (p.frame->GetSize() != p.floating_size)) ||
                    ((p.floating_client_size != wxDefaultSize) && (p.frame->GetClientSize() != p.floating_client_size)))
                {
                    if (p.floating_client_size != wxDefaultSize)
                    {
                        p.frame->SetPosition(p.floating_pos);
                        p.frame->SetClientSize(p.floating_client_size);
                    }
                    else
                    {
                        p.frame->SetSize(p.floating_pos.x, p.floating_pos.y,
                                         p.floating_size.x, p.floating_size.y,
                                         wxSIZE_USE_EXISTING);
                    }
                /*
                    p.frame->SetSize(p.floating_pos.x, p.floating_pos.y,
                                     wxDefaultCoord, wxDefaultCoord,
                                     wxSIZE_USE_EXISTING);
                    //p.frame->Move(p.floating_pos.x, p.floating_pos.y);
                */
                }

                // update whether the pane is resizable or not
                long style = p.frame->GetWindowStyleFlag();
                if (p.IsFixed())
                    style &= ~wxRESIZE_BORDER;
                else
                    style |= wxRESIZE_BORDER;
                p.frame->SetWindowStyleFlag(style);

                if (p.frame->GetLabel() != p.caption)
                    p.frame->SetLabel(p.caption);

                if (p.frame->IsShown() != p.IsShown())
                    p.frame->Show(p.IsShown());
            }
        }
        else
        {
            if (p.window->IsShown() != p.IsShown())
                p.window->Show(p.IsShown());
        }

        // if "active panes" are no longer allowed, clear
        // any optionActive values from the pane states
        if ((m_flags & ibAUI_MGR_ALLOW_ACTIVE_PANE) == 0)
        {
            p.state &= ~ibAuiPaneInfo::optionActive;
        }
    }


    // keep track of the old window rectangles so we can
    // refresh those windows whose rect has changed
    std::vector<wxRect> old_pane_rects;
    for ( const auto& p : m_panes )
    {
        wxRect r;
        if (p.window && p.IsShown() && p.IsDocked())
            r = p.rect;

        old_pane_rects.push_back(r);
    }




    // apply the new sizer
    m_frame->SetSizer(sizer);
    m_frame->SetAutoLayout(false);
    DoFrameLayout();



    // now that the frame layout is done, we need to check
    // the new pane rectangles against the old rectangles that
    // we saved a few lines above here.  If the rectangles have
    // changed, the corresponding panes must also be updated
    for (i = 0; i < pane_count; ++i)
    {
        ibAuiPaneInfo& p = m_panes.Item(i);
        if (p.window && p.window->IsShown() && p.IsDocked())
        {
            // SEAM vs wx: refreshed, not painted at once (Update) - a pane painted in the middle of the layout stood
            // on the screen beside the old one of its neighbours, and a layout done twice (ibFloatingNotebook) showed
            // its first half; the panes are painted as the window paints, after the layout
            if (p.rect != old_pane_rects[i])
                p.window->Refresh();
        }
    }


    Repaint();

    // set frame's minimum size

/*
    // N.B. More work needs to be done on frame minimum sizes;
    // this is some interesting code that imposes the minimum size,
    // but we may want to include a more flexible mechanism or
    // options for multiple minimum-size modes, e.g. strict or lax
    wxSize min_size = sizer->GetMinSize();
    wxSize frame_size = m_frame->GetSize();
    wxSize client_size = m_frame->GetClientSize();

    wxSize minframe_size(min_size.x+frame_size.x-client_size.x,
                         min_size.y+frame_size.y-client_size.y );

    m_frame->SetMinSize(minframe_size);

    if (frame_size.x < minframe_size.x ||
        frame_size.y < minframe_size.y)
            sizer->Fit(m_frame);
*/
}


// DoFrameLayout() is an internal function which invokes wxSizer::Layout
// on the frame's main sizer, then measures all the various UI items
// and updates their internal rectangles.  This should always be called
// instead of calling m_frame->Layout() directly

void ibAuiManager::DoFrameLayout()
{
    // Keep track whether we're inside a layout in order to be able to ignore
    // mouse motion events arriving while we're here, see OnMotion().
    ++m_frameLayoutDepth;
    m_frame->Layout();
    --m_frameLayoutDepth;

    for ( auto& part : m_uiParts )
    {
        // get the rectangle of the UI part
        // originally, this code looked like this:
        //    part.rect = wxRect(part.sizer_item->GetPosition(),
        //                       part.sizer_item->GetSize());
        // this worked quite well, with one exception: the mdi
        // client window had a "deferred" size variable
        // that returned the wrong size.  It looks like
        // a bug in wx, because the former size of the window
        // was being returned.  So, we will retrieve the part's
        // rectangle via other means


        part.rect = part.sizer_item->GetRect();
        int flag = part.sizer_item->GetFlag();
        int border = part.sizer_item->GetBorder();
        if (flag & wxTOP)
        {
            part.rect.y -= border;
            part.rect.height += border;
        }
        if (flag & wxLEFT)
        {
            part.rect.x -= border;
            part.rect.width += border;
        }
        if (flag & wxBOTTOM)
            part.rect.height += border;
        if (flag & wxRIGHT)
            part.rect.width += border;


        if (part.type == ibAuiDockUIPart::typeDock)
            part.dock->rect = part.rect;
        if (part.type == ibAuiDockUIPart::typePane)
            part.pane->rect = part.rect;
    }
}

// GetPanePart() looks up the pane the pane border UI part (or the regular
// pane part if there is no border). This allows the caller to get the exact
// rectangle of the pane in question, including decorations like
// caption and border (if any).

ibAuiDockUIPart* ibAuiManager::GetPanePart(wxWindow* wnd)
{
    for ( auto& part : m_uiParts )
    {
        if ( (part.type == ibAuiDockUIPart::typePaneBorder ||
              part.type == ibAuiDockUIPart::typePane) &&
                part.pane && part.pane->window == wnd)
        {
            return &part;
        }
    }

    return nullptr;
}



// GetDockPixelOffset() is an internal function which returns
// a dock's offset in pixels from the left side of the window
// (for horizontal docks) or from the top of the window (for
// vertical docks).  This value is necessary for calculating
// pixel-pane/toolbar offsets when they are dragged.

int ibAuiManager::GetDockPixelOffset(ibAuiPaneInfo& test)
{
    // the only way to accurately calculate the dock's
    // offset is to actually run a theoretical layout

    ibAuiDockInfoArray docks;
    ibAuiPaneInfoArray panes;
    ibAuiDockUIPartArray uiparts;
    CopyDocksAndPanes(docks, panes, m_docks, m_panes);
    panes.Add(test);

    wxSizer* sizer = LayoutAll(panes, docks, uiparts, true);
    wxSize client_size = m_frame->GetClientSize();
    sizer->SetDimension(0, 0, client_size.x, client_size.y);
    sizer->Layout();

    for ( auto& part : uiparts )
    {
        part.rect = wxRect(part.sizer_item->GetPosition(),
                           part.sizer_item->GetSize());
        if (part.type == ibAuiDockUIPart::typeDock)
            part.dock->rect = part.rect;
    }

    delete sizer;

    for ( const auto& dock : docks )
    {
        if (test.dock_direction == dock.dock_direction &&
            test.dock_layer==dock.dock_layer && test.dock_row==dock.dock_row)
        {
            if (dock.IsVerticalOrCenter())
                return dock.rect.y;
            else
                return dock.rect.x;
        }
    }

    return 0;
}



// ProcessDockResult() is a utility function used by DoDrop() - it checks
// if a dock operation is allowed, the new dock position is copied into
// the target info.  If the operation was allowed, the function returns true.

bool ibAuiManager::ProcessDockResult(ibAuiPaneInfo& target,
                                     const ibAuiPaneInfo& new_pos)
{
    bool allowed = false;
    switch (new_pos.dock_direction)
    {
        case ibAUI_DOCK_TOP:    allowed = target.IsTopDockable();    break;
        case ibAUI_DOCK_BOTTOM: allowed = target.IsBottomDockable(); break;
        case ibAUI_DOCK_LEFT:   allowed = target.IsLeftDockable();   break;
        case ibAUI_DOCK_RIGHT:  allowed = target.IsRightDockable();  break;
    }

    if (allowed)
    {
        // If the pane is moved to a dock with the same orientation, e.g. from
        // the left side to the right one, keep the size of its old dock, so
        // that the pane keeps its width (or height) if a new dock is created
        // for it. But if the orientation changes, the old size is meaningless
        // and the new dock size needs to be computed from the pane size.
        const bool keepsSameOrientation =
            (target.IsHorizontal() && new_pos.IsHorizontal()) ||
            (target.IsVertical() && new_pos.IsVertical());

        target = new_pos;

        if ( !keepsSameOrientation )
            target.dock_size = 0;

        // Should this RTTI and function call be rewritten as
        // sending a new event type to allow other window types
        // to vary size based on dock location?
        ibAuiToolBar* toolbar = wxDynamicCast(target.window, ibAuiToolBar);
        if (toolbar)
        {
            wxSize hintSize = toolbar->GetHintSize(target.dock_direction);
            if (target.best_size != hintSize)
            {
                target.best_size = hintSize;
                target.floating_size = wxDefaultSize;
                target.floating_client_size = wxDefaultSize;
            }
        }
    }

    return allowed;
}


// DoDrop() is an important function.  It basically takes a mouse position,
// and determines where the pane's new position would be.  If the pane is to be
// dropped, it performs the drop operation using the specified dock and pane
// arrays.  By specifying copied dock and pane arrays when calling, a "what-if"
// scenario can be performed, giving precise coordinates for drop hints.
// If, however, ibAuiManager:m_docks and ibAuiManager::m_panes are specified
// as parameters, the changes will be made to the main state arrays

const int auiInsertRowPixels = 10;
const int auiNewRowPixels = 40;
const int auiLayerInsertPixels = 40;
const int auiLayerInsertOffset = 5;

bool ibAuiManager::DoDrop(ibAuiDockInfoArray& docks,
                          ibAuiPaneInfoArray& panes,
                          ibAuiPaneInfo& target,
                          const wxPoint& pt,
                          const wxPoint& offset)
{
    wxSize cli_size = m_frame->GetClientSize();

    ibAuiPaneInfo drop = target;


    // The result should always be shown
    drop.Show();


    // Check to see if the pane has been dragged outside of the window
    // (or near to the outside of the window), if so, dock it along the edge


    wxSize layer_insert_offset;
    if (!drop.IsToolbar())
        layer_insert_offset = m_frame->FromDIP(wxSize(auiLayerInsertOffset, auiLayerInsertOffset));

    wxSize layer_insert_pixels = m_frame->FromDIP(wxSize(auiLayerInsertPixels, auiLayerInsertPixels));

    if (pt.x < layer_insert_offset.x &&
        pt.x > layer_insert_offset.x-layer_insert_pixels.x &&
        pt.y > 0 &&
        pt.y < cli_size.y)
    {
        int new_layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_LEFT),
                                GetMaxLayer(docks, ibAUI_DOCK_BOTTOM)),
                                GetMaxLayer(docks, ibAUI_DOCK_TOP)) + 1;

        if (drop.IsToolbar())
            new_layer = auiToolBarLayer;

        drop.Dock().Left().
             Layer(new_layer).
             Row(0).
             Position(pt.y - GetDockPixelOffset(drop) - offset.y);
        return ProcessDockResult(target, drop);
    }
    else if (pt.y < layer_insert_offset.y &&
             pt.y > layer_insert_offset.y-layer_insert_pixels.y &&
             pt.x > 0 &&
             pt.x < cli_size.x)
    {
        int new_layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_TOP),
                                GetMaxLayer(docks, ibAUI_DOCK_LEFT)),
                                GetMaxLayer(docks, ibAUI_DOCK_RIGHT)) + 1;

        if (drop.IsToolbar())
            new_layer = auiToolBarLayer;

        drop.Dock().Top().
             Layer(new_layer).
             Row(0).
             Position(pt.x - GetDockPixelOffset(drop) - offset.x);
        return ProcessDockResult(target, drop);
    }
    else if (pt.x >= cli_size.x - layer_insert_offset.x &&
             pt.x < cli_size.x - layer_insert_offset.x + layer_insert_pixels.x &&
             pt.y > 0 &&
             pt.y < cli_size.y)
    {
        int new_layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_RIGHT),
                                GetMaxLayer(docks, ibAUI_DOCK_TOP)),
                                GetMaxLayer(docks, ibAUI_DOCK_BOTTOM)) + 1;

        if (drop.IsToolbar())
            new_layer = auiToolBarLayer;

        drop.Dock().Right().
             Layer(new_layer).
             Row(0).
             Position(pt.y - GetDockPixelOffset(drop) - offset.y);
        return ProcessDockResult(target, drop);
    }
    else if (pt.y >= cli_size.y - layer_insert_offset.y &&
             pt.y < cli_size.y - layer_insert_offset.y + layer_insert_pixels.y &&
             pt.x > 0 &&
             pt.x < cli_size.x)
    {
        int new_layer = wxMax( wxMax( GetMaxLayer(docks, ibAUI_DOCK_BOTTOM),
                                      GetMaxLayer(docks, ibAUI_DOCK_LEFT)),
                                      GetMaxLayer(docks, ibAUI_DOCK_RIGHT)) + 1;

        if (drop.IsToolbar())
            new_layer = auiToolBarLayer;

        drop.Dock().Bottom().
             Layer(new_layer).
             Row(0).
             Position(pt.x - GetDockPixelOffset(drop) - offset.x);
        return ProcessDockResult(target, drop);
    }


    ibAuiDockUIPart* part = HitTest(pt.x, pt.y);


    if (drop.IsToolbar())
    {
        if (!part || !part->dock)
            return false;

        // calculate the offset from where the dock begins
        // to the point where the user dropped the pane
        int dock_drop_offset = 0;
        if (part->dock->IsHorizontal())
            dock_drop_offset = pt.x - part->dock->rect.x - offset.x;
        else
            dock_drop_offset = pt.y - part->dock->rect.y - offset.y;


        // toolbars may only be moved in and to fixed-pane docks,
        // otherwise we will try to float the pane.  Also, the pane
        // should float if being dragged over center pane windows
        if (!part->dock->fixed || part->dock->dock_direction == ibAUI_DOCK_CENTER ||
            pt.x >= cli_size.x || pt.x <= 0 || pt.y >= cli_size.y || pt.y <= 0)
        {
            if ((m_flags & ibAUI_MGR_ALLOW_FLOATING) && drop.IsFloatable())
            {
                drop.Float();
            }
            else
            {
                drop.Position(pt.x - GetDockPixelOffset(drop) - offset.x);
            }
            return ProcessDockResult(target, drop);
        }

        m_lastRect = part->dock->rect;
        m_lastRect.Inflate( m_frame->FromDIP(wxSize(15, 15)) );

        drop.Dock().
             Direction(part->dock->dock_direction).
             Layer(part->dock->dock_layer).
             Row(part->dock->dock_row).
             Position(dock_drop_offset);

        if ((
            ((pt.y < part->dock->rect.y + 1) && part->dock->IsHorizontal()) ||
            ((pt.x < part->dock->rect.x + 1) && part->dock->IsVerticalOrCenter())
            ) && part->dock->panes.GetCount() > 1)
        {
            if ((part->dock->dock_direction == ibAUI_DOCK_TOP) ||
                (part->dock->dock_direction == ibAUI_DOCK_LEFT))
            {
                int row = drop.dock_row;
                DoInsertDockRow(panes, part->dock->dock_direction,
                                part->dock->dock_layer,
                                part->dock->dock_row);
                drop.dock_row = row;
            }
            else
            {
                DoInsertDockRow(panes, part->dock->dock_direction,
                                part->dock->dock_layer,
                                part->dock->dock_row+1);
                drop.dock_row = part->dock->dock_row+1;
            }
        }

        if ((
            ((pt.y > part->dock->rect.y + part->dock->rect.height - 2 ) && part->dock->IsHorizontal()) ||
            ((pt.x > part->dock->rect.x + part->dock->rect.width - 2 ) && part->dock->IsVerticalOrCenter())
            ) && part->dock->panes.GetCount() > 1)
        {
            if ((part->dock->dock_direction == ibAUI_DOCK_TOP) ||
                (part->dock->dock_direction == ibAUI_DOCK_LEFT))
            {
                DoInsertDockRow(panes, part->dock->dock_direction,
                                part->dock->dock_layer,
                                part->dock->dock_row+1);
                drop.dock_row = part->dock->dock_row+1;
            }
            else
            {
                int row = drop.dock_row;
                DoInsertDockRow(panes, part->dock->dock_direction,
                                part->dock->dock_layer,
                                part->dock->dock_row);
                drop.dock_row = row;
            }
        }

        return ProcessDockResult(target, drop);
    }




    if (!part)
        return false;

    if (part->type == ibAuiDockUIPart::typePaneBorder ||
        part->type == ibAuiDockUIPart::typeCaption ||
        part->type == ibAuiDockUIPart::typeGripper ||
        part->type == ibAuiDockUIPart::typePaneButton ||
        part->type == ibAuiDockUIPart::typePane ||
        part->type == ibAuiDockUIPart::typePaneSizer ||
        part->type == ibAuiDockUIPart::typeDockSizer ||
        part->type == ibAuiDockUIPart::typeBackground)
    {
        if (part->type == ibAuiDockUIPart::typeDockSizer)
        {
            if (part->dock->panes.GetCount() != 1)
                return false;
            part = GetPanePart(part->dock->panes.Item(0)->window);
            if (!part)
                return false;
        }



        // If a normal frame is being dragged over a toolbar, insert it
        // along the edge under the toolbar, but over all other panes.
        // (this could be done much better, but somehow factoring this
        // calculation with the one at the beginning of this function)
        if (part->dock && part->dock->toolbar)
        {
            int layer = 0;

            switch (part->dock->dock_direction)
            {
                case ibAUI_DOCK_LEFT:
                    layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_LEFT),
                                      GetMaxLayer(docks, ibAUI_DOCK_BOTTOM)),
                                      GetMaxLayer(docks, ibAUI_DOCK_TOP));
                    break;
                case ibAUI_DOCK_TOP:
                    layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_TOP),
                                      GetMaxLayer(docks, ibAUI_DOCK_LEFT)),
                                      GetMaxLayer(docks, ibAUI_DOCK_RIGHT));
                    break;
                case ibAUI_DOCK_RIGHT:
                    layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_RIGHT),
                                      GetMaxLayer(docks, ibAUI_DOCK_TOP)),
                                      GetMaxLayer(docks, ibAUI_DOCK_BOTTOM));
                    break;
                case ibAUI_DOCK_BOTTOM:
                    layer = wxMax(wxMax(GetMaxLayer(docks, ibAUI_DOCK_BOTTOM),
                                      GetMaxLayer(docks, ibAUI_DOCK_LEFT)),
                                      GetMaxLayer(docks, ibAUI_DOCK_RIGHT));
                    break;
            }

            DoInsertDockRow(panes, part->dock->dock_direction,
                            layer, 0);
            drop.Dock().
                 Direction(part->dock->dock_direction).
                 Layer(layer).Row(0).Position(0);
            return ProcessDockResult(target, drop);
        }


        if (!part->pane)
            return false;

        part = GetPanePart(part->pane->window);
        if (!part)
            return false;

        bool insert_dock_row = false;
        int insert_row = part->pane->dock_row;
        int insert_dir = part->pane->dock_direction;
        int insert_layer = part->pane->dock_layer;
        wxSize insert_row_pixels = m_frame->FromDIP(wxSize(auiInsertRowPixels, auiInsertRowPixels));

        switch (part->pane->dock_direction)
        {
            case ibAUI_DOCK_TOP:
                if (pt.y >= part->rect.y &&
                    pt.y < part->rect.y+insert_row_pixels.y)
                        insert_dock_row = true;
                break;
            case ibAUI_DOCK_BOTTOM:
                if (pt.y > part->rect.y+part->rect.height-insert_row_pixels.y &&
                    pt.y <= part->rect.y + part->rect.height)
                        insert_dock_row = true;
                break;
            case ibAUI_DOCK_LEFT:
                if (pt.x >= part->rect.x &&
                    pt.x < part->rect.x+insert_row_pixels.x)
                        insert_dock_row = true;
                break;
            case ibAUI_DOCK_RIGHT:
                if (pt.x > part->rect.x+part->rect.width-insert_row_pixels.x &&
                    pt.x <= part->rect.x+part->rect.width)
                        insert_dock_row = true;
                break;
            case ibAUI_DOCK_CENTER:
            {
                // "new row pixels" will be set to the default, but
                // must never exceed 20% of the window size
                wxSize new_row_pixels = m_frame->FromDIP(wxSize(auiNewRowPixels, auiNewRowPixels));
                int new_row_pixels_x = new_row_pixels.x;
                int new_row_pixels_y = new_row_pixels.y;

                if (new_row_pixels_x > (part->rect.width*20)/100)
                    new_row_pixels_x = (part->rect.width*20)/100;

                if (new_row_pixels_y > (part->rect.height*20)/100)
                    new_row_pixels_y = (part->rect.height*20)/100;


                // determine if the mouse pointer is in a location that
                // will cause a new row to be inserted.  The hot spot positions
                // are along the borders of the center pane

                insert_layer = 0;
                insert_dock_row = true;
                const wxRect& pr = part->rect;
                if (pt.x >= pr.x && pt.x < pr.x + new_row_pixels_x)
                    insert_dir = ibAUI_DOCK_LEFT;
                else if (pt.y >= pr.y && pt.y < pr.y + new_row_pixels_y)
                    insert_dir = ibAUI_DOCK_TOP;
                else if (pt.x >= pr.x + pr.width - new_row_pixels_x &&
                         pt.x < pr.x + pr.width)
                    insert_dir = ibAUI_DOCK_RIGHT;
                else if (pt.y >= pr.y+ pr.height - new_row_pixels_y &&
                         pt.y < pr.y + pr.height)
                    insert_dir = ibAUI_DOCK_BOTTOM;
                else
                    return false;

                insert_row = GetMaxRow(panes, insert_dir, insert_layer) + 1;
            }
        }

        if (insert_dock_row)
        {
            DoInsertDockRow(panes, insert_dir, insert_layer, insert_row);
            drop.Dock().Direction(insert_dir).
                        Layer(insert_layer).
                        Row(insert_row).
                        Position(0);
            return ProcessDockResult(target, drop);
        }

        // determine the mouse offset and the pane size, both in the
        // direction of the dock itself, and perpendicular to the dock

        int mouseOffset, size;

        if (part->orientation == wxVERTICAL)
        {
            mouseOffset = pt.y - part->rect.y;
            size = part->rect.GetHeight();
        }
        else
        {
            mouseOffset = pt.x - part->rect.x;
            size = part->rect.GetWidth();
        }

        int drop_position = part->pane->dock_pos;

        // if we are in the top/left part of the pane,
        // insert the pane before the pane being hovered over
        if (mouseOffset <= size/2)
        {
            drop_position = part->pane->dock_pos;
            DoInsertPane(panes,
                         part->pane->dock_direction,
                         part->pane->dock_layer,
                         part->pane->dock_row,
                         part->pane->dock_pos);
        }

        // if we are in the bottom/right part of the pane,
        // insert the pane before the pane being hovered over
        if (mouseOffset > size/2)
        {
            drop_position = part->pane->dock_pos+1;
            DoInsertPane(panes,
                         part->pane->dock_direction,
                         part->pane->dock_layer,
                         part->pane->dock_row,
                         part->pane->dock_pos+1);
        }

        drop.Dock().
             Direction(part->dock->dock_direction).
             Layer(part->dock->dock_layer).
             Row(part->dock->dock_row).
             Position(drop_position);
        return ProcessDockResult(target, drop);
    }

    return false;
}

void ibAuiManager::OnHintFadeTimer(wxTimerEvent& WXUNUSED(event))
{
    if (m_hintFadeAmt >= m_hintFadeMax)
    {
        m_hintFadeTimer.Stop();
        Unbind(wxEVT_TIMER, &ibAuiManager::OnHintFadeTimer, this,
               m_hintFadeTimer.GetId());
        return;
    }

    m_hintFadeAmt++;

    ShowHint(m_lastHint);
}

void ibAuiManager::ShowHint(const wxRect& rectScreen)
{
    wxOverlayDC dc(m_overlay, m_frame);
    dc.Clear();

    wxRect rect = rectScreen;
    m_frame->ScreenToClient(&rect.x, &rect.y);

    wxDCClipper clip(dc, rect);

    if ( m_flags & ibAUI_MGR_RECTANGLE_HINT )
    {
        // Not using a transparent hint window...
        wxBitmap stipple = wxPaneCreateStippleBitmap();
        wxBrush brush(stipple);
        dc.SetBrush(brush);
        dc.SetPen(*wxTRANSPARENT_PEN);

        const int d = m_frame->FromDIP(5);
        const int dd = m_frame->FromDIP(10);

        dc.DrawRectangle(rect.x, rect.y, d, rect.height);
        dc.DrawRectangle(rect.x + d, rect.y, rect.width - dd, d);
        dc.DrawRectangle(rect.x + rect.width - d, rect.y, d, rect.height);
        dc.DrawRectangle(rect.x + d, rect.y + rect.height - d, rect.width - dd, d);

        return;
    }

#ifdef __WXGTK3__
    // The standard DC under wxGTK3 supports alpha drawing, whether the overlay
    // is native (Wayland) or generic (X11).
    const bool canDrawTransparentHint = true;
#else
    const bool canDrawTransparentHint = m_overlay.IsNative();
#endif

    // SEAM vs wx: the hint is the theme's accent (terracotta in Luna), not the system's HOTLIGHT blue (2026-11-12, Max).
    // On GTK3/Cocoa the same - the colour is the theme's there too
    const auto hintCol = wxTHEME_COLOUR(HIGHLIGHT);
    const unsigned char r = hintCol.GetRed();
    const unsigned char g = hintCol.GetGreen();
    const unsigned char b = hintCol.GetBlue();
    const unsigned char a = m_hintFadeAmt;

    const auto makeBrush = [this, r, g, b, a]()
    {
        return (m_flags & ibAUI_MGR_VENETIAN_BLINDS_HINT) != 0
                    ? ibCreateVenetianBlindsBitmap(r, g, b, a)
                    : wxBrush(wxColour(r, g, b, a));
    };

    if ( canDrawTransparentHint )
    {
#ifdef __WXMSW__
        m_overlay.SetOpacity(m_hintFadeAmt);
#endif // __WXMSW__

        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(makeBrush());
        dc.DrawRectangle(rect);
    }
    else
    {
        wxBitmap bmp(rect.GetSize(), 32);
        wxMemoryDC mdc(bmp);
        mdc.Blit(0, 0, rect.width, rect.height, &dc, rect.x, rect.y);
        wxRasterOperationMode logicalFunc = wxINVERT;

#if wxUSE_GRAPHICS_CONTEXT
        {
            wxGCDC gdc(mdc);
            gdc.SetPen(*wxTRANSPARENT_PEN);
            gdc.SetBrush(makeBrush());
            gdc.DrawRectangle(wxPoint(0, 0), rect.GetSize());
        }

        logicalFunc = wxCOPY;
#endif // wxUSE_GRAPHICS_CONTEXT

        dc.Blit(rect.x, rect.y, rect.width, rect.height, &mdc, 0, 0, logicalFunc);
    }

    // if we are dragging a floating pane, set the focus
    // back to that floating pane (otherwise it becomes unfocused)
    if (m_action == actionDragFloatingPane && m_actionWindow)
        m_actionWindow->SetFocus();

    if (m_hintFadeAmt != m_hintFadeMax) //  Only fade if we need to
    {
        // start fade in timer
        m_hintFadeTimer.SetOwner(this);
        m_hintFadeTimer.Start(10);
        Bind(wxEVT_TIMER, &ibAuiManager::OnHintFadeTimer, this,
             m_hintFadeTimer.GetId());
    }
}

void ibAuiManager::HideHint()
{
    m_overlay.Reset();
    m_hintFadeTimer.Stop();
    // In case this is called while a hint fade is going, we need to
    // disconnect the event handler.
    Unbind(wxEVT_TIMER, &ibAuiManager::OnHintFadeTimer, this,
           m_hintFadeTimer.GetId());

    m_lastHint = wxRect();
}


void ibAuiManager::StartPaneDrag(wxWindow* pane_window,
                                 const wxPoint& offset)
{
    ibAuiPaneInfo& pane = GetPane(pane_window);
    if (!pane.IsOk())
        return;

    if (pane.IsToolbar())
    {
        m_action = actionDragToolbarPane;
    }
    else
    {
        m_action = actionDragFloatingPane;
    }

    m_actionWindow = pane_window;
    m_actionOffset = offset;
    m_frame->CaptureMouse();

    if (pane.frame)
    {
        wxRect window_rect = pane.frame->GetRect();
        wxRect client_rect = pane.frame->GetClientRect();
        wxPoint client_pt = pane.frame->ClientToScreen(client_rect.GetTopLeft());
        wxPoint origin_pt = client_pt - window_rect.GetTopLeft();
        m_actionOffset += origin_pt;

        StartDragSession(pane, pane.frame);
    }
}


// CalculateHintRect() calculates the drop hint rectangle.  The method
// first calls DoDrop() to determine the exact position the pane would
// be at were if dropped.  If the pane would indeed become docked at the
// specified drop point, the rectangle hint will be returned in
// screen coordinates.  Otherwise, an empty rectangle is returned.
// |pane_window| is the window pointer of the pane being dragged, |pt| is
// the mouse position, in client coordinates.  |offset| describes the offset
// that the mouse is from the upper-left corner of the item being dragged

wxRect ibAuiManager::CalculateHintRect(wxWindow* pane_window,
                                       const wxPoint& pt,
                                       const wxPoint& offset)
{
    wxRect rect;

    // we need to paint a hint rectangle; to find out the exact hint rectangle,
    // we will create a new temporary layout and then measure the resulting
    // rectangle; we will create a copy of the docking structures (m_dock)
    // so that we don't modify the real thing on screen

    int i, pane_count;
    ibAuiDockInfoArray docks;
    ibAuiPaneInfoArray panes;
    ibAuiDockUIPartArray uiparts;
    ibAuiPaneInfo hint = GetPane(pane_window);
    hint.name = wxT("__HINT__");
    hint.PaneBorder(true);
    hint.Show();

    if (!hint.IsOk())
        return rect;

    CopyDocksAndPanes(docks, panes, m_docks, m_panes);

    // remove any pane already there which bears the same window;
    // this happens when you are moving a pane around in a dock
    for (i = 0, pane_count = panes.GetCount(); i < pane_count; ++i)
    {
        if (panes.Item(i).window == pane_window)
        {
            RemovePaneFromDocks(docks, panes.Item(i));
            panes.RemoveAt(i);
            break;
        }
    }

    // find out where the new pane would be
    if (!DoDrop(docks, panes, hint, pt, offset))
    {
        return rect;
    }

    panes.Add(hint);

    wxSizer* sizer = LayoutAll(panes, docks, uiparts, true);
    wxSize client_size = m_frame->GetClientSize();
    sizer->SetDimension(0, 0, client_size.x, client_size.y);
    sizer->Layout();

    for ( auto& part : uiparts )
    {
        if (part.type == ibAuiDockUIPart::typePaneBorder &&
            part.pane && part.pane->name == wxT("__HINT__"))
        {
            rect = wxRect(part.sizer_item->GetPosition(),
                          part.sizer_item->GetSize());
            break;
        }
    }

    delete sizer;

    if ( rect.IsEmpty() )
        return rect;

    m_frame->ClientToScreen(&rect.x, &rect.y);

    return rect;
}

// DrawHintRect() calculates the hint rectangle by calling
// CalculateHintRect().  If there is a rectangle, it shows it
// by calling ShowHint(), otherwise it hides any hint
// rectangle currently shown
void ibAuiManager::DrawHintRect(wxWindow* pane_window,
                                const wxPoint& pt,
                                const wxPoint& offset)
{
    UpdateHint(CalculateHintRect(pane_window, pt, offset));
}

void ibAuiManager::UpdateHint(const wxRect& rect)
{
    if (rect == m_lastHint)
        return;

    m_lastHint = rect;

    if (rect.IsEmpty())
    {
        HideHint();
    }
    else
    {
        // Decide if we want to fade in the hint and set it to the end value if
        // we don't.
        if (m_flags & ibAUI_MGR_HINT_FADE)
            m_hintFadeAmt = 0;
        else
            m_hintFadeAmt = m_hintFadeMax;

         ShowHint(rect);
     }
}

void ibAuiManager::OnFloatingPaneMoveStart(wxWindow* wnd)
{
    // try to find the pane
    ibAuiPaneInfo& pane = GetPane(wnd);
    wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));

    if(!pane.frame)
        return;

    if (m_flags & ibAUI_MGR_TRANSPARENT_DRAG)
        pane.frame->SetTransparent(150);
}

void ibAuiManager::OnFloatingPaneMoving(wxWindow* wnd, wxDirection dir)
{
    // try to find the pane
    ibAuiPaneInfo& pane = GetPane(wnd);
    wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));

    if(!pane.frame)
        return;

    wxPoint pt = ::wxGetMousePosition();

#if 0
    // Adapt pt to direction
    if (dir == wxNORTH)
    {
        // move to pane's upper border
        wxPoint pos( 0,0 );
        pos = wnd->ClientToScreen( pos );
        pt.y = pos.y;
        // and some more pixels for the title bar
        pt.y -= wnd->FromDIP(5);
    }
    else if (dir == wxWEST)
    {
        // move to pane's left border
        wxPoint pos( 0,0 );
        pos = wnd->ClientToScreen( pos );
        pt.x = pos.x;
    }
    else if (dir == wxEAST)
    {
        // move to pane's right border
        wxPoint pos( wnd->GetSize().x, 0 );
        pos = wnd->ClientToScreen( pos );
        pt.x = pos.x;
    }
    else if (dir == wxSOUTH)
    {
        // move to pane's bottom border
        wxPoint pos( 0, wnd->GetSize().y );
        pos = wnd->ClientToScreen( pos );
        pt.y = pos.y;
    }
#else
    wxUnusedVar(dir);
#endif

    // calculate the offset from the upper left-hand corner
    // of the frame to the mouse pointer
    wxPoint frame_pos = pane.frame->GetPosition();

    DoMovePane(pane, pt, wxPoint(pt.x-frame_pos.x, pt.y-frame_pos.y));
}

// Common part of OnFloatingPaneMoving() and OnPaneDragMove(): update the hint
// shown for the pane being dragged to the given position.
void ibAuiManager::DoMovePane(ibAuiPaneInfo& pane,
                              const wxPoint& pt,
                              const wxPoint& action_offset)
{
    wxPoint client_pt = m_frame->ScreenToClient(pt);

    // no hint for toolbar floating windows
    if (pane.IsToolbar() && m_action == actionDragFloatingPane)
    {
        ibAuiDockInfoArray docks;
        ibAuiPaneInfoArray panes;
        ibAuiDockUIPartArray uiparts;
        ibAuiPaneInfo hint = pane;

        CopyDocksAndPanes(docks, panes, m_docks, m_panes);

        // find out where the new pane would be
        if (!DoDrop(docks, panes, hint, client_pt))
            return;
        if (hint.IsFloating())
            return;

        pane = hint;
        m_action = actionDragToolbarPane;
        m_actionWindow = pane.window;

        Update();

        return;
    }


    // if a key modifier is pressed while dragging the frame,
    // don't dock the window
    if (!CanDockPanel(pane))
    {
        HideHint();
        return;
    }


    DrawHintRect(pane.window, client_pt, action_offset);


    // reduces flicker
    m_frame->Update();
}

void ibAuiManager::OnFloatingPaneMoved(wxWindow* wnd, wxDirection dir)
{
    // try to find the pane
    ibAuiPaneInfo& pane = GetPane(wnd);
    wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));

    if(!pane.frame)
        return;

    wxPoint pt = ::wxGetMousePosition();

#if 0
    // Adapt pt to direction
    if (dir == wxNORTH)
    {
        // move to pane's upper border
        wxPoint pos( 0,0 );
        pos = wnd->ClientToScreen( pos );
        pt.y = pos.y;
        // and some more pixels for the title bar
        pt.y -= wnd->FromDIP(10);
    }
    else if (dir == wxWEST)
    {
        // move to pane's left border
        wxPoint pos( 0,0 );
        pos = wnd->ClientToScreen( pos );
        pt.x = pos.x;
    }
    else if (dir == wxEAST)
    {
        // move to pane's right border
        wxPoint pos( wnd->GetSize().x, 0 );
        pos = wnd->ClientToScreen( pos );
        pt.x = pos.x;
    }
    else if (dir == wxSOUTH)
    {
        // move to pane's bottom border
        wxPoint pos( 0, wnd->GetSize().y );
        pos = wnd->ClientToScreen( pos );
        pt.y = pos.y;
    }
#else
    wxUnusedVar(dir);
#endif

    // calculate the offset from the upper left-hand corner
    // of the frame to the mouse pointer
    wxPoint frame_pos = pane.frame->GetPosition();
    wxPoint action_offset(pt.x-frame_pos.x, pt.y-frame_pos.y);
    DoDropPane(pane, pt, action_offset);

    DoEndMovePane(pane);
}

// Common part of OnFloatingPaneMoved() and OnPaneDragDrop().
void ibAuiManager::DoDropPane(ibAuiPaneInfo& pane,
                              const wxPoint& pt,
                              const wxPoint& action_offset)
{
    // if a key modifier is pressed while dragging the frame,
    // don't dock the window
    if (CanDockPanel(pane))
    {
        wxPoint client_pt = m_frame->ScreenToClient(pt);

        // do the drop calculation
        DoDrop(m_docks, m_panes, pane, client_pt, action_offset);
    }
}

// Common part of OnFloatingPaneMoved() and OnPaneDragCancel(): just update the
// layout after moving a pane ended without a drop.
void ibAuiManager::DoEndMovePane(ibAuiPaneInfo& pane)
{
    // if the pane is still floating, update its floating
    // position (that we store)
    if (pane.IsFloating())
    {
        pane.floating_pos = pane.frame->GetPosition();

        if (m_flags & ibAUI_MGR_TRANSPARENT_DRAG)
            pane.frame->SetTransparent(255);
    }
    else // it is docked
    {
        if (m_hasMaximized)
            RestoreMaximizedPane();

        AddPaneToMinDockIfNecessary(pane);
    }

    Update();

    HideHint();
}

// Save the positions of all the panes in the dock containing the given pane:
// this is done at the end of dragging a toolbar pane to ensure that the panes
// of the dock it ended up in have sequential positions.
void ibAuiManager::SaveDockPositions(const ibAuiPaneInfo& pane)
{
    for ( auto dockInfo : FindDocks(m_docks, pane.dock_direction,
                                    pane.dock_layer, pane.dock_row,
                                    FindDocksFlags::OnlyFirst) )
    {
        ibAuiDockInfo& dock = *dockInfo;

        wxArrayInt pane_positions, pane_sizes;
        GetPanePositionsAndSizes(dock, pane_positions, pane_sizes);

        int i, dock_pane_count = dock.panes.GetCount();
        for (i = 0; i < dock_pane_count; ++i)
            dock.panes.Item(i)->dock_pos = pane_positions[i];
    }
}

// Try to start dragging the floating frame of the given pane using the system
// drag support: this currently only works under Wayland, where we can't move
// the frame ourselves, and does nothing elsewhere.
void ibAuiManager::StartDragSession(ibAuiPaneInfo& pane, wxWindow* origin)
{
#ifdef wxHAS_TLW_DRAG_SESSION
    auto handler = std::make_unique<ibAuiPaneDragHandler>
                   (
                     this, pane.window, m_actionOffset
                   );

    // When the drag starts in the managed window itself, we already know that
    // the user is dragging the pane because the caller had already checked for
    // it (but when it starts in the floating frame we still have to check
    // whether the pointer really moves, so we don't do this then).
    if ( origin == m_frame )
    {
        handler->SetDragStarted();
    }

    auto session = wxTLWDragSession::Create(origin, m_frame, std::move(handler));
    if (!session)
        return;

    session->AttachWindow(pane.frame, m_actionOffset);

    m_dragSession = std::move(session);

    // We're not going to receive any mouse events until the drag ends, so
    // don't keep the capture and reset our state to avoid confusing the
    // normal, mouse-based, code.
    if (m_frame->HasCapture())
        m_frame->ReleaseMouse();

    m_action = actionNone;
    m_actionWindow = nullptr;
#else // !wxHAS_TLW_DRAG_SESSION
    wxUnusedVar(pane);
    wxUnusedVar(origin);
#endif // wxHAS_TLW_DRAG_SESSION/!wxHAS_TLW_DRAG_SESSION
}

void ibAuiManager::OnPaneDragMove(wxWindow* paneWindow,
                                  wxWindow* win,
                                  const wxPoint& pt,
                                  const wxPoint& offset)
{
#ifdef wxHAS_TLW_DRAG_SESSION
    ibAuiPaneInfo& pane = GetPane(paneWindow);
    if (!pane.IsOk() || !pane.frame)
        return;

    // We can only dock the pane if the pointer is over the managed window.
    if (win != m_frame)
    {
        HideHint();
        return;
    }

    DoMovePane(pane, pt, offset);
#else // !wxHAS_TLW_DRAG_SESSION
    wxUnusedVar(paneWindow);
    wxUnusedVar(win);
    wxUnusedVar(pt);
    wxUnusedVar(offset);
#endif // wxHAS_TLW_DRAG_SESSION/!wxHAS_TLW_DRAG_SESSION
}

void ibAuiManager::OnPaneDragDrop(wxWindow* paneWindow,
                                  wxWindow* win,
                                  const wxPoint& pt,
                                  const wxPoint& offset)
{
#ifdef wxHAS_TLW_DRAG_SESSION
    // If the drag didn't end over the managed window, just leave the pane
    // floating where the compositor has put it.
    if ( win != m_frame )
        return;

    ibAuiPaneInfo& pane = GetPane(paneWindow);
    if (!pane.IsOk() || !pane.frame)
        return;

    DoDropPane(pane, pt, offset);
#else // !wxHAS_TLW_DRAG_SESSION
    wxUnusedVar(paneWindow);
    wxUnusedVar(win);
    wxUnusedVar(pt);
    wxUnusedVar(offset);
#endif // wxHAS_TLW_DRAG_SESSION/!wxHAS_TLW_DRAG_SESSION
}

void ibAuiManager::OnPaneDragEnd(wxWindow* paneWindow)
{
#ifdef wxHAS_TLW_DRAG_SESSION
    // Update the layout to actually dock the pane if it had been dropped on a
    // dock by OnPaneDragDrop() or just leave it floating otherwise. Note that
    // this must be done in any case, including when the drag was cancelled, if
    // only to hide the hint which could be still shown.
    ibAuiPaneInfo& pane = GetPane(paneWindow);
    if (pane.IsOk() && pane.frame)
    {
        // Do the same thing as at the end of a mouse-driven toolbar drag in
        // OnLeftUp() if the toolbar was dropped into a dock.
        if (pane.IsToolbar() && !pane.IsFloating())
            SaveDockPositions(pane);

        DoEndMovePane(pane);
    }

    // We don't need the session any more, but don't delete it right now as
    // we're called from it, do it as soon as possible instead.
    CallAfter([this]() { m_dragSession.reset(); });
#else // !wxHAS_TLW_DRAG_SESSION
    wxUnusedVar(paneWindow);
#endif // wxHAS_TLW_DRAG_SESSION/!wxHAS_TLW_DRAG_SESSION
}

void ibAuiManager::OnFloatingPaneResized(wxWindow* wnd, const wxRect& rect)
{
    // try to find the pane
    ibAuiPaneInfo& pane = GetPane(wnd);
    wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));
    // if frame isn't fully set up, don't stomp on pos/size info
    if (!pane.frame)
    {
        return;
    }

    // Setting floating client size is enough, there is no need to set floating
    // size, as it won't be used if the client size is set.
    pane.FloatingClientSize(pane.frame->WindowToClientSize(rect.GetSize()));

    // the top-left position may change as well as the size
    pane.FloatingPosition(rect.x, rect.y);
}


void ibAuiManager::OnFloatingPaneClosed(wxWindow* wnd, wxCloseEvent& evt)
{
    // try to find the pane
    ibAuiPaneInfo& pane = GetPane(wnd);
    wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));


    // fire pane close event
    ibAuiManagerEvent e(ibEVT_AUI_PANE_CLOSE);
    e.SetPane(&pane);
    e.SetCanVeto(evt.CanVeto());
    ProcessMgrEvent(e);

    if (e.GetVeto())
    {
        evt.Veto();
        return;
    }
    else
    {
        // close the pane, but check that it
        // still exists in our pane array first
        // (the event handler above might have removed it)

        ibAuiPaneInfo& check = GetPane(wnd);
        if (check.IsOk())
        {
            ClosePane(pane);
        }
    }
}



void ibAuiManager::OnFloatingPaneActivated(wxWindow* wnd)
{
    if ((GetFlags() & ibAUI_MGR_ALLOW_ACTIVE_PANE) && GetPane(wnd).IsOk())
    {
        SetActivePane(wnd);
        Repaint();
    }
}

// OnRender() draws all of the pane captions, sashes,
// backgrounds, captions, grippers, pane borders and buttons.
// It renders the entire user interface.

void ibAuiManager::OnRender(ibAuiManagerEvent& evt)
{
    // if the frame is about to be deleted, don't bother
    if (!m_frame || wxPendingDelete.Member(m_frame))
        return;

    wxDC* dc = evt.GetDC();

#ifdef __WXMAC__
    dc->Clear() ;
#endif
    // SEAM vs wx: the theme draws the docking, the colour round the panes its scheme's DESKTOP - wx drew it by a dock
    // art (theme/luna_dockart until 2026-11-11)
    ibRenderer* const renderer = ibThemeEngine::Get()->GetRenderer();
    for ( const auto& part : m_uiParts )
    {
        // don't draw hidden pane items or items that aren't windows
        if (part.sizer_item &&
                ((!part.sizer_item->IsWindow() &&
                  !part.sizer_item->IsSpacer() &&
                  !part.sizer_item->IsSizer()) ||
                  !part.sizer_item->IsShown() ||
                   part.rect.IsEmpty()))
            continue;

        switch (part.type)
        {
            case ibAuiDockUIPart::typeDockSizer:
            case ibAuiDockUIPart::typePaneSizer:
                renderer->DrawSash(*dc, part.rect, part.orientation == wxVERTICAL ? wxVERTICAL : wxHORIZONTAL);
                break;
            case ibAuiDockUIPart::typeBackground:
                renderer->DrawBackground(*dc, wxTHEME_COLOUR(DESKTOP), part.rect, 0);
                break;
            case ibAuiDockUIPart::typeCaption:
            {
                // in two pieces, the title's and its buttons' after it: the title cut to its own, the icon fitted
                const int flags = (part.pane->state & ibAuiPaneInfo::optionActive) ? wxCONTROL_FOCUSED : 0;
                wxRect title = part.rect;
                for ( const auto& button : m_uiParts )
                {
                    if ( button.type == ibAuiDockUIPart::typePaneButton && button.pane == part.pane )
                        title.width = wxMin(title.width, button.rect.x - title.x);
                }

                wxBitmap icon;
                if ( part.pane->icon.IsOk() )
                {
                    wxSize size = part.pane->icon.GetPreferredLogicalSizeFor(m_frame);
                    if ( size.y > part.rect.height )
                        size = size * part.rect.height / size.y;
                    icon = part.pane->icon.GetBitmap(m_frame->ToPhys(size));
                }

                renderer->DrawPaneCaption(*dc, title, part.pane->caption, icon, flags);
                if ( title.width < part.rect.width )
                    renderer->DrawPaneCaption(*dc, wxRect(title.GetRight() + 1, part.rect.y,
                        part.rect.width - title.width, part.rect.height), wxEmptyString, wxNullBitmap, flags);
                break;
            }
            case ibAuiDockUIPart::typeGripper:
                renderer->DrawGripper(*dc, part.rect, part.pane->HasGripperTop() ? wxVERTICAL : wxHORIZONTAL);
                break;
            case ibAuiDockUIPart::typePaneBorder:
                // a tool bar's is the colour round it, nothing to draw; a book's pages' square, the other panes' the
                // theme's
                if ( part.pane->IsToolbar() )
                    break;
                if ( wxDynamicCast(m_frame, ibAuiNotebook) != nullptr )
                    renderer->DrawBorder(*dc, wxBORDER_SIMPLE, part.rect);
                else
                    renderer->DrawPaneBorder(*dc, part.rect);
                break;
            case ibAuiDockUIPart::typePaneButton:
            {
                int flags = ibAuiGetControlFlags(part.state);
                if ( part.pane->state & ibAuiPaneInfo::optionActive )
                    flags |= wxCONTROL_FOCUSED;
                if ( part.pane->IsMaximized() )
                    flags |= wxCONTROL_CHECKED;
                renderer->DrawPaneButton(*dc, part.rect, part.button, flags);
                break;
            }
        }
    }
}


// Render() fire a render event, which is normally handled by
// ibAuiManager::OnRender().  This allows the render function to
// be overridden via the render event.  This can be useful for paintin
// custom graphics in the main window. Default behaviour can be
// invoked in the overridden function by calling OnRender()

void ibAuiManager::Render(wxDC* dc)
{
    ibAuiManagerEvent e(ibEVT_AUI_RENDER);
    e.SetManager(this);
    e.SetDC(dc);
    ProcessMgrEvent(e);
}

void ibAuiManager::Repaint(wxDC* dc)
{
    // SEAM vs wx: with no dc, the frame's parts are painted when it paints (OnPaint), not at once on a client dc -
    // drawn at once, they stood at the new layout over panes not painted there yet, and stayed as dotted residue
    // where a pane moved after them (ibFloatingNotebook refreshed a strip round itself for it); on MSW the frame
    // alone, its panes repaint as they moved (Update)
    if (!dc)
    {
#ifdef __WXMSW__
        ::RedrawWindow(GetHwndOf(m_frame), nullptr, nullptr, RDW_INVALIDATE | RDW_NOCHILDREN);
#else
        m_frame->Refresh();
#endif
        return;
    }

    int w, h;
    m_frame->GetClientSize(&w, &h);

    // if the frame has a toolbar, the client area
    // origin will not be (0,0).
    wxPoint pt = m_frame->GetClientAreaOrigin();
    if (pt.x != 0 || pt.y != 0)
        dc->SetDeviceOrigin(pt.x, pt.y);

    // render all the items
    Render(dc);
}

void ibAuiManager::OnDestroy(wxWindowDestroyEvent& event)
{
    if ( event.GetEventObject() == m_frame )
    {
        wxWindow* const frame = m_frame;

        UnInit();

        // Just calling Skip() would be insufficient in this case, as by
        // removing the event handler from the event handlers chain in UnInit()
        // we'd still prevent the frame from getting this event, so we need to
        // forward it to it manually. Note that this must be done after calling
        // UnInit() to prevent infinite recursion.
        if ( frame )
            frame->ProcessWindowEventLocally(event);
    }

    // Make sure this event get's propagated to other handlers.
    event.Skip();
}

void ibAuiManager::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxPaintDC dcPaint(m_frame);

    // SEAM vs wx: the dock's colour under the client area alone: Clear filled the whole window, and over the decorations
    // the theme's frame paints round it before this (ibTopLevelWindow::TryBefore) - its title bar was never seen. From
    // the client area's corner, as Repaint takes it: wxMSW's paint DC begins there (wxClientDCImpl::InitDC), wxGTK's
    // at the window's
    const wxPoint origin = m_frame->GetClientAreaOrigin();
    dcPaint.SetDeviceOrigin(origin.x, origin.y);

    // SEAM vs wx: one buffered pass, as an ib window paints (ibWindow::OnPaint) - on the screen itself the dock's colour
    // went over the captions and the sashes before they were drawn again, and every step of a size showed it: the
    // caption and the strip above the bottom notebook flickered (2026-11-12, Max). On GTK3/Cocoa the window is
    // double-buffered natively, the paint DC is the buffer
    const wxSize size = m_frame->GetClientSize();
#if wxALWAYS_NATIVE_DOUBLE_BUFFER
    wxDC& dc = dcPaint;
#else
    wxBufferedDC dc(&dcPaint, size);
    // the window's font and colours, as wxBufferedPaintDC takes them
    dc.GetImpl()->InheritAttributes(m_frame);
#endif
    dc.SetBrush(wxTHEME_COLOUR(DESKTOP));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(wxPoint(0, 0), size);

    // not Repaint(&dc): it moves the DC's origin to the client area's corner, where the buffer begins already
    Render(&dc);
}

void ibAuiManager::OnEraseBackground(wxEraseEvent& event)
{
#ifdef __WXMAC__
    event.Skip() ;
#else
    wxUnusedVar(event);
#endif
}

void ibAuiManager::OnSize(wxSizeEvent& event)
{
    if (m_frame)
    {
        if ( m_updateOnRestore )
        {
            // If we had postponed updating, do it now: we only receive size
            // events once the window is restored.
            m_updateOnRestore = false;

            Update();
        }
        else // Otherwise just re-layout, without redoing the full update.
        {
            DoFrameLayout();
            Repaint();
        }

#if wxUSE_MDI
        if (wxDynamicCast(m_frame, wxMDIParentFrame))
        {
            // for MDI parent frames, this event must not
            // be "skipped".  In other words, the parent frame
            // must not be allowed to resize the client window
            // after we are finished processing sizing changes
            return;
        }
#endif
    }
    event.Skip();
}

void ibAuiManager::OnFindManager(ibAuiManagerEvent& evt)
{
    // get the window we are managing, if none, return nullptr
    wxWindow* window = GetManagedWindow();
    if (!window)
    {
        evt.SetManager(nullptr);
        return;
    }

    // if we are managing a child frame, get the 'real' manager - the window laid out inside it (SetManagedWindow)
    if (wxDynamicCast(window->GetParent(), ibAuiFloatingFrame))
    {
        ibAuiFloatingFrame* float_frame = static_cast<ibAuiFloatingFrame*>(window->GetParent());
        evt.SetManager(float_frame->GetOwnerManager());
        return;
    }

    // return pointer to ourself
    evt.SetManager(this);
}

void ibAuiManager::OnSetCursor(wxSetCursorEvent& event)
{
    // Don't set any cursor by default.
    event.Skip();

    // determine cursor
    ibAuiDockUIPart* part = HitTest(event.GetX(), event.GetY());
    wxCursor cursor;

    if (part)
    {
        if (part->type == ibAuiDockUIPart::typeDockSizer ||
            part->type == ibAuiDockUIPart::typePaneSizer)
        {
            // a dock may not be resized if it has a single
            // pane which is not resizable
            if (part->type == ibAuiDockUIPart::typeDockSizer && part->dock &&
                part->dock->panes.GetCount() == 1 &&
                part->dock->panes.Item(0)->IsFixed())
                    return;

            // panes that may not be resized do not get a sizing cursor
            if (part->pane && part->pane->IsFixed())
                return;

            if (part->orientation == wxVERTICAL)
                cursor = wxCursor(wxCURSOR_SIZEWE);
            else
                cursor = wxCursor(wxCURSOR_SIZENS);
        }
        else if (part->type == ibAuiDockUIPart::typeGripper)
        {
            cursor = wxCursor(wxCURSOR_SIZING);
        }
    }

    if ( cursor.IsOk() )
    {
        event.SetCursor(cursor);

        // Undo Skip() done above.
        event.Skip(false);
    }
}



void ibAuiManager::UpdateButtonOnScreen(ibAuiDockUIPart* button_ui_part, int state)
{
    wxCHECK_RET( button_ui_part,
                 "UpdateButtonOnScreen() called with null part" );

    wxASSERT_MSG( button_ui_part->type == ibAuiDockUIPart::typePaneButton,
                  "UpdateButtonOnScreen() called for non-button part" );

    // Don't bother doing anything if nothing changed.
    if ( button_ui_part->state == state )
        return;

    button_ui_part->state = state;

    m_frame->RefreshRect(button_ui_part->rect);
    m_frame->Update();
}

void ibAuiManager::OnLeftDown(wxMouseEvent& event)
{
    m_currentDragItem = -1;

    ibAuiDockUIPart* part = HitTest(event.GetX(), event.GetY());
    if (part)
    {
        if (part->type == ibAuiDockUIPart::typeDockSizer ||
            part->type == ibAuiDockUIPart::typePaneSizer)
        {
            // Removing this restriction so that a centre pane can be resized
            //if (part->dock && part->dock->dock_direction == ibAUI_DOCK_CENTER)
            //    return;

            // a dock may not be resized if it has a single
            // pane which is not resizable
            if (part->type == ibAuiDockUIPart::typeDockSizer && part->dock &&
                part->dock->panes.GetCount() == 1 &&
                part->dock->panes.Item(0)->IsFixed())
                    return;

            // panes that may not be resized should be ignored here
            if (part->pane && part->pane->IsFixed())
                return;

            m_action = actionResize;
            m_actionPart = part;
            m_actionHintRect = wxRect();
            m_actionStart = wxPoint(event.m_x, event.m_y);
            m_actionOffset = wxPoint(event.m_x - part->rect.x,
                                      event.m_y - part->rect.y);
            m_frame->CaptureMouse();
        }
        else if (part->type == ibAuiDockUIPart::typePaneButton)
        {
            m_action = actionClickButton;
            m_actionPart = part;
            m_actionStart = wxPoint(event.m_x, event.m_y);
            m_frame->CaptureMouse();

            UpdateButtonOnScreen(part, ibAUI_BUTTON_STATE_PRESSED);
        }
        else if (part->type == ibAuiDockUIPart::typeCaption ||
                  part->type == ibAuiDockUIPart::typeGripper)
        {
            // if we are managing a ibAuiFloatingFrame window, then
            // we are an embedded ibAuiManager inside the ibAuiFloatingFrame.
            // We want to initiate a toolbar drag in our owner manager
            wxWindow* managed_wnd = GetManagedWindow();

            if (part->pane &&
                part->pane->window &&
                managed_wnd &&
                wxDynamicCast(managed_wnd->GetParent(), ibAuiFloatingFrame))
            {
                ibAuiFloatingFrame* floating_frame = (ibAuiFloatingFrame*)managed_wnd->GetParent();
                ibAuiManager* owner_mgr = floating_frame->GetOwnerManager();
                owner_mgr->StartPaneDrag(part->pane->window,
                                             wxPoint(event.m_x - part->rect.x,
                                                     event.m_y - part->rect.y));
                return;
            }



            if (GetFlags() & ibAUI_MGR_ALLOW_ACTIVE_PANE)
            {
                // set the caption as active
                SetActivePane(part->pane->window);
                Repaint();
            }

            if (part->dock && part->dock->dock_direction == ibAUI_DOCK_CENTER)
                return;

            m_action = actionClickCaption;
            m_actionPart = part;
            m_actionStart = wxPoint(event.m_x, event.m_y);
            m_actionOffset = wxPoint(event.m_x - part->rect.x,
                                      event.m_y - part->rect.y);
            m_frame->CaptureMouse();
        }
        else
        {
            event.Skip();
        }
    }
    else
    {
        event.Skip();
    }
}

/// Ends a resize action, or for live update, resizes the sash
bool ibAuiManager::DoEndResizeAction(wxMouseEvent& event)
{
    // resize the dock or the pane
    if (m_actionPart && m_actionPart->type==ibAuiDockUIPart::typeDockSizer)
    {
        // first, we must calculate the maximum size the dock may be
        int sashSize = GetMetricForWindow(ibAUI_DOCKART_SASH_SIZE, m_frame);

        int used_width = 0, used_height = 0;

        wxSize client_size = m_frame->GetClientSize();

        for ( const auto& dock : m_docks )
        {
            if (dock.dock_direction == ibAUI_DOCK_TOP ||
                dock.dock_direction == ibAUI_DOCK_BOTTOM)
            {
                used_height += dock.size;
            }
            if (dock.dock_direction == ibAUI_DOCK_LEFT ||
                dock.dock_direction == ibAUI_DOCK_RIGHT)
            {
                used_width += dock.size;
            }
            if (dock.resizable)
                used_width += sashSize;
        }


        int available_width = client_size.GetWidth() - used_width;
        int available_height = client_size.GetHeight() - used_height;

        const wxRect& rect = m_actionPart->dock->rect;

        wxPoint new_pos(event.m_x - m_actionOffset.x,
            event.m_y - m_actionOffset.y);
        int new_size, old_size = m_actionPart->dock->size;

        switch (m_actionPart->dock->dock_direction)
        {
        case ibAUI_DOCK_LEFT:
            new_size = new_pos.x - rect.x;
            if (new_size-old_size > available_width)
                new_size = old_size+available_width;
            m_actionPart->dock->size = new_size;
            break;
        case ibAUI_DOCK_TOP:
            new_size = new_pos.y - rect.y;
            if (new_size-old_size > available_height)
                new_size = old_size+available_height;
            m_actionPart->dock->size = new_size;
            break;
        case ibAUI_DOCK_RIGHT:
            new_size = rect.x + rect.width - new_pos.x -
                       m_actionPart->rect.GetWidth();
            if (new_size-old_size > available_width)
                new_size = old_size+available_width;
            m_actionPart->dock->size = new_size;
            break;
        case ibAUI_DOCK_BOTTOM:
            new_size = rect.y + rect.height -
                new_pos.y - m_actionPart->rect.GetHeight();
            if (new_size-old_size > available_height)
                new_size = old_size+available_height;
            m_actionPart->dock->size = new_size;
            break;
        }

        Update();
    }
    else if (m_actionPart &&
        m_actionPart->type == ibAuiDockUIPart::typePaneSizer)
    {
        ibAuiDockInfo& dock = *m_actionPart->dock;
        ibAuiPaneInfo& pane = *m_actionPart->pane;

        int total_proportion = 0;
        int dock_pixels = 0;
        int new_pixsize = 0;

        int caption_size = GetMetricForWindow(ibAUI_DOCKART_CAPTION_SIZE, pane.window);
        int pane_borderSize = GetMetricForWindow(ibAUI_DOCKART_PANE_BORDER_SIZE, pane.window);
        int sashSize = GetMetricForWindow(ibAUI_DOCKART_SASH_SIZE, pane.window);

        wxPoint new_pos(event.m_x - m_actionOffset.x,
            event.m_y - m_actionOffset.y);

        // determine the pane rectangle by getting the pane part
        ibAuiDockUIPart* pane_part = GetPanePart(pane.window);
        wxASSERT_MSG(pane_part,
            wxT("Pane border part not found -- shouldn't happen"));

        // determine the new pixel size that the user wants;
        // this will help us recalculate the pane's proportion
        if (dock.IsHorizontal())
            new_pixsize = new_pos.x - pane_part->rect.x;
        else
            new_pixsize = new_pos.y - pane_part->rect.y;

        // determine the size of the dock, based on orientation
        if (dock.IsHorizontal())
            dock_pixels = dock.rect.GetWidth();
        else
            dock_pixels = dock.rect.GetHeight();

        // determine the total proportion of all resizable panes,
        // and the total size of the dock minus the size of all
        // the fixed panes
        int i, dock_pane_count = dock.panes.GetCount();
        int pane_position = -1;
        for (i = 0; i < dock_pane_count; ++i)
        {
            ibAuiPaneInfo& p = *dock.panes.Item(i);
            if (p.window == pane.window)
                pane_position = i;

            // while we're at it, subtract the pane sash
            // width from the dock width, because this would
            // skew our proportion calculations
            if (i > 0)
                dock_pixels -= sashSize;

            // also, the whole size (including decorations) of
            // all fixed panes must also be subtracted, because they
            // are not part of the proportion calculation
            if (p.IsFixed())
            {
                if (dock.IsHorizontal())
                    dock_pixels -= p.best_size.x;
                else
                    dock_pixels -= p.best_size.y;
            }
            else
            {
                total_proportion += p.dock_proportion;
            }
        }

        // new size can never be more than the number of dock pixels
        if (new_pixsize > dock_pixels)
            new_pixsize = dock_pixels;


        // find a pane in our dock to 'steal' space from or to 'give'
        // space to -- this is essentially what is done when a pane is
        // resized; the pane should usually be the first non-fixed pane
        // to the right of the action pane
        int borrow_pane = -1;
        for (i = pane_position+1; i < dock_pane_count; ++i)
        {
            ibAuiPaneInfo& p = *dock.panes.Item(i);
            if (!p.IsFixed())
            {
                borrow_pane = i;
                break;
            }
        }


        // demand that the pane being resized is found in this dock
        // (this assert really never should be raised)
        wxASSERT_MSG(pane_position != -1, wxT("Pane not found in dock"));

        // prevent division by zero
        if (dock_pixels == 0 || total_proportion == 0 || borrow_pane == -1)
            return false;

        // calculate the new proportion of the pane
        int new_proportion = (new_pixsize*total_proportion)/dock_pixels;

        // default minimum size
        int min_size = 0;

        // check against the pane's minimum size, if specified. please note
        // that this is not enough to ensure that the minimum size will
        // not be violated, because the whole frame might later be shrunk,
        // causing the size of the pane to violate its minimum size
        if (pane.min_size.IsFullySpecified())
        {
            min_size = 0;

            if (pane.HasBorder())
                min_size += (pane_borderSize*2);

            // calculate minimum size with decorations (border,caption)
            if (pane_part->orientation == wxVERTICAL)
            {
                min_size += pane.min_size.y;
                if (pane.HasCaption())
                    min_size += caption_size;
            }
            else
            {
                min_size += pane.min_size.x;
            }
        }


        // for some reason, an arithmetic error somewhere is causing
        // the proportion calculations to always be off by 1 pixel;
        // for now we will add the 1 pixel on, but we really should
        // determine what's causing this.
        min_size++;

        int min_proportion = (min_size*total_proportion)/dock_pixels;

        if (new_proportion < min_proportion)
            new_proportion = min_proportion;



        int prop_diff = new_proportion - pane.dock_proportion;

        // borrow the space from our neighbor pane to the
        // right or bottom (depending on orientation);
        // also make sure we don't make the neighbor too small
        int prop_borrow = dock.panes.Item(borrow_pane)->dock_proportion;

        if (prop_borrow - prop_diff < 0)
        {
            // borrowing from other pane would make it too small,
            // so cancel the resize operation
            prop_borrow = min_proportion;
        }
         else
        {
            prop_borrow -= prop_diff;
        }


        dock.panes.Item(borrow_pane)->dock_proportion = prop_borrow;
        pane.dock_proportion = new_proportion;

        Update();
    }

    return true;
}

void ibAuiManager::OnLeftUp(wxMouseEvent& event)
{
    if (m_action == actionResize)
    {
        const bool wasDragged =
            event.GetPosition() != m_actionStart || m_currentDragItem != -1;

        m_frame->ReleaseMouse();

        if (!HasLiveResize())
        {
            // get rid of the hint rectangle
            m_overlay.Reset();
        }

        if ( wasDragged )
        {
            if (m_currentDragItem != -1 && HasLiveResize())
                m_actionPart = & (m_uiParts.Item(m_currentDragItem));

            DoEndResizeAction(event);
        }

        m_currentDragItem = -1;

    }
    else if (m_action == actionClickButton)
    {
        m_hoverButton = nullptr;
        m_frame->ReleaseMouse();

        if (m_actionPart)
        {
            // make sure we're still over the item that was originally clicked
            if (m_actionPart == HitTest(event.GetX(), event.GetY()))
            {
                // fire button-click event
                ibAuiManagerEvent e(ibEVT_AUI_PANE_BUTTON);
                e.SetManager(this);
                e.SetPane(m_actionPart->pane);
                e.SetButton(m_actionPart->button);
                ProcessMgrEvent(e);
            }
            else
            {
                // normally we should have reset the button state when mouse
                // moved out of the active part, but do it here just in case we
                // somehow didn't get it
                UpdateButtonOnScreen(m_actionPart, ibAUI_BUTTON_STATE_NORMAL);
            }
        }
    }
    else if (m_action == actionClickCaption)
    {
        m_frame->ReleaseMouse();
    }
    else if (m_action == actionDragFloatingPane)
    {
        m_frame->ReleaseMouse();
    }
    else if (m_action == actionDragToolbarPane)
    {
        m_frame->ReleaseMouse();

        ibAuiPaneInfo& pane = GetPane(m_actionWindow);
        wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));

        SaveDockPositions(pane);

        pane.state &= ~ibAuiPaneInfo::actionPane;
        Update();
    }
    else
    {
        event.Skip();
    }

    m_action = actionNone;
    m_lastMouseMove = wxPoint(); // see comment in OnMotion()
}


void ibAuiManager::OnMotion(wxMouseEvent& event)
{
    // At least with GTK4, this event can arrive while DoFrameLayout() is using
    // the sizer and calling Update(), as we can do below, would be
    // catastrophic in this case as it would destroy this sizer and cause a
    // crash, so simply don't do anything in this case -- we'll get another
    // motion event later anyway.
    if ( m_frameLayoutDepth > 0 )
        return;

    // sometimes when Update() is called from inside this method,
    // a spurious mouse move event is generated; this check will make
    // sure that only real mouse moves will get anywhere in this method;
    // this appears to be a bug somewhere, and I don't know where the
    // mouse move event is being generated.  only verified on MSW

    wxPoint mouse_pos = event.GetPosition();
    if (m_lastMouseMove == mouse_pos)
        return;
    m_lastMouseMove = mouse_pos;


    if (m_action == actionResize)
    {
        if ( mouse_pos == m_actionStart && m_currentDragItem == -1 )
            return;

        // It's necessary to reset m_actionPart since it destroyed
        // by the Update within DoEndResizeAction.
        if (m_currentDragItem != -1)
            m_actionPart = & (m_uiParts.Item(m_currentDragItem));
        else
            m_currentDragItem = GetActionPartIndex();

        if (m_actionPart)
        {
            wxPoint pos = m_actionPart->rect.GetPosition();
            if (m_actionPart->orientation == wxHORIZONTAL)
                pos.y = wxMax(0, event.m_y - m_actionOffset.y);
            else
                pos.x = wxMax(0, event.m_x - m_actionOffset.x);

            if (HasLiveResize())
            {
                DoEndResizeAction(event);
            }
            else
            {
                // draw resize hint
                m_actionHintRect = wxRect(pos, m_actionPart->rect.GetSize());
                wxDrawOverlayResizeHint(m_frame, m_overlay, m_actionHintRect);
            }
        }
    }
    else if (m_action == actionClickCaption)
    {
        // caption has been clicked.  we need to check if the mouse
        // is now being dragged. if it is, we need to change the
        // mouse action to 'drag'
        if (m_actionPart &&
            wxSystemSettings::ExceedsDragThreshold(m_actionStart,
                                                   event.GetPosition(),
                                                   m_frame) )
        {
            ibAuiPaneInfo* paneInfo = m_actionPart->pane;

            if (!paneInfo->IsToolbar())
            {
                if ((m_flags & ibAUI_MGR_ALLOW_FLOATING) &&
                    paneInfo->IsFloatable())
                {
                    m_action = actionDragFloatingPane;

                    // set initial float position
                    wxPoint pt = m_frame->ClientToScreen(event.GetPosition());
                    paneInfo->floating_pos = wxPoint(pt.x - m_actionOffset.x,
                                                      pt.y - m_actionOffset.y);

                    // float the window
                    if (paneInfo->IsMaximized())
                        RestorePane(*paneInfo);

                    // Remove the button corresponding to this pane from the
                    // min dock if it's shown there, it doesn't make sense to
                    // keep it for a floating pane.
                    if (paneInfo->HasMinimizeButton())
                    {
                        auto const minDir = GetMinDockDirectionFor(paneInfo->dock_direction);
                        if(minDir != ibAUI_DOCK_NONE)
                        {
                            RemovePaneFromMinDockIfNecessary(minDir, *paneInfo);
                        }
                    }

                    paneInfo->Float();
                    Update();

                    m_actionWindow = paneInfo->frame;

                    // action offset is used here to make it feel "natural" to the user
                    // to drag a docked pane and suddenly have it become a floating frame.
                    // Sometimes, however, the offset where the user clicked on the docked
                    // caption is bigger than the width of the floating frame itself, so
                    // in that case we need to set the action offset to a sensible value
                    wxSize frame_size = m_actionWindow->GetSize();
                    if (frame_size.x <= m_actionOffset.x)
                        m_actionOffset.x = paneInfo->frame->FromDIP(30);

                    // Under Wayland we can't move the floating frame ourselves
                    // and have to ask the system to do it for us, which also
                    // means that we won't get any more mouse events until the
                    // end of the drag and will be notified about its progress
                    // by ibAuiPaneDragHandler instead.
                    StartDragSession(*paneInfo);
                }
            }
            else
            {
                m_action = actionDragToolbarPane;
                m_actionWindow = paneInfo->window;
            }
        }
    }
    else if (m_action == actionDragFloatingPane)
    {
        if (m_actionWindow)
        {
            // We can't move the child window so we need to get the frame that
            // we want to be really moving. This is probably not the best place
            // to do this but at least it fixes the bug (#13177) for now.
            if (!wxDynamicCast(m_actionWindow, ibAuiFloatingFrame))
            {
                ibAuiPaneInfo& pane = GetPane(m_actionWindow);
                m_actionWindow = pane.frame;
            }

            wxPoint pt = m_frame->ClientToScreen(event.GetPosition());
            m_actionWindow->Move(pt.x - m_actionOffset.x,
                                pt.y - m_actionOffset.y);
        }
    }
    else if (m_action == actionDragToolbarPane)
    {
        ibAuiPaneInfo& pane = GetPane(m_actionWindow);
        wxASSERT_MSG(pane.IsOk(), wxT("Pane window not found"));

        pane.SetFlag(ibAuiPaneInfo::actionPane, true);

        auto const dockDirectionOld = pane.dock_direction;

        wxPoint point = event.GetPosition();
        DoDrop(m_docks, m_panes, pane, point, m_actionOffset);

        // if DoDrop() decided to float the pane, set up
        // the floating pane's initial position
        if (pane.IsFloating())
        {
            wxPoint pt = m_frame->ClientToScreen(event.GetPosition());
            pane.floating_pos = wxPoint(pt.x - m_actionOffset.x,
                                        pt.y - m_actionOffset.y);
        }
        else // the pane is still docked
        {
            // Check if it's minimized direction has changed.
            if (pane.HasMinimizeButton()
                && pane.dock_direction != dockDirectionOld)
            {
                // The dock into which the pane is minimized may remain the
                // same even if the docking direction has changed, e.g. if
                // there is just one dock showing minimized panes, it will
                // always remain the same.
                auto const
                    minDockOld = GetMinDockDirectionFor(dockDirectionOld);
                auto const
                    minDockNew = GetMinDockDirectionFor(pane.dock_direction);

                if (minDockOld != minDockNew)
                {
                    RemovePaneFromMinDockIfNecessary(minDockOld, pane);

                    if (auto const dockNew = GetMinDockInDirection(minDockNew))
                        dockNew->AddPane(pane);
                }
            }
        }

        // this will do the actual move operation;
        // in the case that the pane has been floated,
        // this call will create the floating pane
        // and do the reparenting
        Update();

        // SEAM vs wx: the move painted at once, after the layout, as a frame paints after its own (ibTopLevelWindow::Layout).
        // A drag's next move is taken before a paint (input outranks it): the bar stood where it was while the mouse
        // moved and was drawn where it went only when the mouse stopped, its old place left in the dock till then
        // (2026-11-12, Max: "a very slow response when moving"). On GTK3/Cocoa the paint follows the frame clock, input
        // does not hold it back; Update() draws what is pending there too
        m_frame->Update();

        // if the pane has been floated, change the mouse
        // action actionDragFloatingPane so that subsequent
        // EVT_MOTION() events will move the floating pane
        if (pane.IsFloating())
        {
            pane.state &= ~ibAuiPaneInfo::actionPane;
            m_action = actionDragFloatingPane;
            m_actionWindow = pane.frame;

            // Allow dragging under Wayland, see comment in OnMotion().
            StartDragSession(pane);
        }
    }
    else
    {
        ibAuiDockUIPart* part = HitTest(event.GetX(), event.GetY());
        if (part && part->type == ibAuiDockUIPart::typePaneButton)
        {
            if (part != m_hoverButton)
            {
                // make the old button normal
                if (m_hoverButton)
                {
                    UpdateButtonOnScreen(m_hoverButton, ibAUI_BUTTON_STATE_NORMAL);
                }

                // mouse is over a button, so repaint the
                // button in hover mode
                UpdateButtonOnScreen(part, ibAUI_BUTTON_STATE_HOVER);
                m_hoverButton = part;

            }
        }
        else
        {
            if (m_hoverButton)
            {
                UpdateButtonOnScreen(m_hoverButton, ibAUI_BUTTON_STATE_NORMAL);
                m_hoverButton = nullptr;
            }
            else
            {
                event.Skip();
            }
        }
    }
}

void ibAuiManager::OnLeaveWindow(wxMouseEvent& WXUNUSED(event))
{
    if (m_hoverButton)
    {
        UpdateButtonOnScreen(m_hoverButton, ibAUI_BUTTON_STATE_NORMAL);
        m_hoverButton = nullptr;
    }
}

void ibAuiManager::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(event))
{
    // cancel the operation in progress, if any
    if ( m_action != actionNone )
    {
        m_action = actionNone;
        HideHint();
    }
}

void ibAuiManager::OnChildFocus(wxChildFocusEvent& event)
{
    // when a child pane has its focus set, we should change the
    // pane's active state to reflect this. (this is only true if
    // active panes are allowed by the owner)
    if (GetFlags() & ibAUI_MGR_ALLOW_ACTIVE_PANE)
    {
        ibAuiPaneInfo& pane = GetPane(event.GetWindow());
        if (pane.IsOk() && (pane.state & ibAuiPaneInfo::optionActive) == 0)
        {
            SetActivePane(event.GetWindow());
            m_frame->Refresh();
        }
    }

    event.Skip();
}


// OnPaneButton() is an event handler that is called
// when a pane button has been pressed.
void ibAuiManager::OnPaneButton(ibAuiManagerEvent& evt)
{
    wxASSERT_MSG(evt.pane, wxT("Pane Info passed to ibAuiManager::OnPaneButton must be non-null"));

    ibAuiPaneInfo& pane = *(evt.pane);

    if (evt.button == ibAUI_BUTTON_CLOSE)
    {
        // If we're the manager of a floating frame, close the frame itself
        // instead of just closing the pane inside it: this ensures that the
        // manager owning this pane is notified about it, just as it would be
        // if the frame was closed using its own close button.
        if (auto* const frame = wxDynamicCast(m_frame, ibAuiFloatingFrame))
        {
            frame->Close();
            return;
        }

        // fire pane close event
        ibAuiManagerEvent e(ibEVT_AUI_PANE_CLOSE);
        e.SetManager(this);
        e.SetPane(evt.pane);
        ProcessMgrEvent(e);

        if (!e.GetVeto())
        {
            // close the pane, but check that it
            // still exists in our pane array first
            // (the event handler above might have removed it)

            ibAuiPaneInfo& check = GetPane(pane.window);
            if (check.IsOk())
            {
                ClosePane(pane);
            }

            Update();
        }
    }
    else if (evt.button == ibAUI_BUTTON_MINIMIZE)
    {
        ibAuiManagerEvent e(ibEVT_AUI_PANE_MINIMIZE);
        e.SetManager(this);
        e.SetPane(evt.pane);
        ProcessMgrEvent(e);

        if (!e.GetVeto())
        {
            MinimizePane(pane);
            Update();
        }
    }
    else if (evt.button == ibAUI_BUTTON_MAXIMIZE_RESTORE && !pane.IsMaximized())
    {
        // fire pane maximize event
        ibAuiManagerEvent e(ibEVT_AUI_PANE_MAXIMIZE);
        e.SetManager(this);
        e.SetPane(evt.pane);
        ProcessMgrEvent(e);

        if (!e.GetVeto())
        {
            MaximizePane(pane);
            Update();
        }
    }
    else if (evt.button == ibAUI_BUTTON_MAXIMIZE_RESTORE && pane.IsMaximized())
    {
        // fire pane restore event
        ibAuiManagerEvent e(ibEVT_AUI_PANE_RESTORE);
        e.SetManager(this);
        e.SetPane(evt.pane);
        ProcessMgrEvent(e);

        if (!e.GetVeto())
        {
            RestorePane(pane);
            Update();
        }
    }
    else if (evt.button == ibAUI_BUTTON_PIN &&
                (m_flags & ibAUI_MGR_ALLOW_FLOATING) && pane.IsFloatable())
    {
        if (pane.IsMaximized())
        {
            // If the pane is maximized, the original state must be restored
            // before trying to float the pane, otherwise the other panels
            // wouldn't appear correctly when it becomes floating.
            ibAuiManagerEvent e(ibEVT_AUI_PANE_RESTORE);
            e.SetManager(this);
            e.SetPane(evt.pane);
            ProcessMgrEvent(e);

            if (e.GetVeto())
            {
                // If it can't be restored, it can't be floated either.
                return;
            }

            RestorePane(pane);
        }

        pane.Float();
        Update();
    }
}

