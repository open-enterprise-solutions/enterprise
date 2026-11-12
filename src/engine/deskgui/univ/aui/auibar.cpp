// FORKED from wxWidgets' AUI (src/aui/auibar.cpp of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////

// Name:        src/aui/auibar.cpp
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



#include "wx/statline.h"
#include "wx/dcbuffer.h"
#include "wx/frame.h"
#include "wx/sizer.h"
#include "wx/image.h"
#include "wx/settings.h"
#include "wx/menu.h"
#include "wx/toolbar.h"

#include "deskgui/univ/aui/auibar.h"
#include "deskgui/univ/aui/framemanager.h"

#ifndef WX_PRECOMP
    #include "wx/dcclient.h"
#endif

#include "deskgui/univ/menu.h"
#include "deskgui/univ/renderer.h"
#include "deskgui/univ/theme.h"          // ibThemeEngine - the colour the bar stands on (EraseBackground)
#include "deskgui/univ/colourScheme.h"   // wxTHEME_BG_COLOUR

wxDEFINE_EVENT( ibEVT_AUITOOLBAR_TOOL_DROPDOWN, ibAuiToolBarEvent );
wxDEFINE_EVENT( ibEVT_AUITOOLBAR_OVERFLOW_CLICK, ibAuiToolBarEvent );
wxDEFINE_EVENT( ibEVT_AUITOOLBAR_RIGHT_CLICK, ibAuiToolBarEvent );
wxDEFINE_EVENT( ibEVT_AUITOOLBAR_MIDDLE_CLICK, ibAuiToolBarEvent );
wxDEFINE_EVENT( ibEVT_AUITOOLBAR_BEGIN_DRAG, ibAuiToolBarEvent );


wxIMPLEMENT_CLASS(ibAuiToolBar, ibControl);
wxIMPLEMENT_DYNAMIC_CLASS(ibAuiToolBarEvent, wxEvent);


// missing ibITEM_* items
enum
{
    ibITEM_CONTROL = wxITEM_MAX,
    ibITEM_LABEL,
    ibITEM_SPACER
};


wxBitmap ibAuiToolBarItem::GetCurrentBitmapFor(wxWindow* wnd) const
{
    // We suppose that we don't have disabled bitmap if we don't have the
    // normal one either.
    if ( !m_bitmap.IsOk() )
        return wxNullBitmap;

    // SEAM vs wx: a tool of a disabled bar is a disabled one, as the native tool bar draws it (wx's drew it as it was)
    if ( (m_state & ibAUI_BUTTON_STATE_DISABLED) || !wnd->IsEnabled() )
    {
        if ( m_disabledBitmap.IsOk() )
            return m_disabledBitmap.GetBitmapFor(wnd);

        return m_bitmap.GetBitmapFor(wnd).ConvertToDisabled();
    }

    return m_bitmap.GetBitmapFor(wnd);
}


static wxOrientation GetOrientation(long style)
{
    switch (style & ibAUI_ORIENTATION_MASK)
    {
        case ibAUI_TB_HORIZONTAL:
            return wxHORIZONTAL;
        case ibAUI_TB_VERTICAL:
            return wxVERTICAL;
        default:
            wxFAIL_MSG("toolbar cannot be locked in both horizontal and vertical orientations (maybe no lock was intended?)");
            wxFALLTHROUGH;
        case 0:
            return wxBOTH;
    }
}

wxBEGIN_EVENT_TABLE(ibAuiToolBar, ibControl)
    EVT_SIZE(ibAuiToolBar::OnSize)
    EVT_IDLE(ibAuiToolBar::OnIdle)
    EVT_DPI_CHANGED(ibAuiToolBar::OnDPIChanged)
    EVT_PAINT(ibAuiToolBar::OnPaint)
    EVT_LEFT_DOWN(ibAuiToolBar::OnLeftDown)
    EVT_LEFT_DCLICK(ibAuiToolBar::OnLeftDown)
    EVT_LEFT_UP(ibAuiToolBar::OnLeftUp)
    EVT_RIGHT_DOWN(ibAuiToolBar::OnRightDown)
    EVT_RIGHT_DCLICK(ibAuiToolBar::OnRightDown)
    EVT_RIGHT_UP(ibAuiToolBar::OnRightUp)
    EVT_MIDDLE_DOWN(ibAuiToolBar::OnMiddleDown)
    EVT_MIDDLE_DCLICK(ibAuiToolBar::OnMiddleDown)
    EVT_MIDDLE_UP(ibAuiToolBar::OnMiddleUp)
    EVT_MOTION(ibAuiToolBar::OnMotion)
    EVT_LEAVE_WINDOW(ibAuiToolBar::OnLeaveWindow)
    EVT_MOUSE_CAPTURE_LOST(ibAuiToolBar::OnCaptureLost)
    EVT_SET_CURSOR(ibAuiToolBar::OnSetCursor)
wxEND_EVENT_TABLE()

void ibAuiToolBar::Init()
{
    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_buttonWidth = -1;
    m_buttonHeight = -1;
    m_sizerElementCount = 0;
    m_actionPos = wxDefaultPosition;
    m_actionItem = nullptr;
    m_tipItem = nullptr;
    m_toolTextOrientation = ibAUI_TBTOOL_TEXT_BOTTOM;
    m_gripperSizerItem = nullptr;
    m_overflowSizerItem = nullptr;
    m_dragging = false;
    m_gripperVisible = false;
    m_overflowVisible = false;
    m_overflowState = 0;
    m_orientation = wxHORIZONTAL;
}

bool ibAuiToolBar::Create(wxWindow* parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style)
{
    style = style|wxBORDER_NONE;

    if (!ibControl::Create(parent, id, pos, size, style))
        return false;

    m_windowStyle = style;

    // Tool packing and padding are in DIPs, i.e. do not use FromDIP() here.
    m_toolPacking = 2;
    m_toolBorderPadding = 3;

    m_gripperVisible  = (style & ibAUI_TB_GRIPPER) ? true : false;
    m_overflowVisible = (style & ibAUI_TB_OVERFLOW) ? true : false;

    m_orientation = GetOrientation(style);
    if (m_orientation == wxBOTH)
    {
        m_orientation = wxHORIZONTAL;
    }

    // Margins are in DIPs, i.e. do not use FromDIP() here.
    m_leftPadding =
    m_rightPadding = 5;
    m_topPadding =
    m_bottomPadding = 2;

    // SEAM vs wx: the tools' font is the window's, the theme's - not the system's normal font set here for the art
    SetExtraStyle(wxWS_EX_PROCESS_IDLE);

    if ( style & ibAUI_TB_HORIZONTAL )
    {
        wxASSERT_MSG
        (
         !(style & (ibAUI_TB_VERT_LAYOUT_DOWN | ibAUI_TB_VERT_LAYOUT_UP)),
         "Vertical text styles can't be used with horizontal toolbars"
        );
    }
    else if ( style & ibAUI_TB_VERTICAL )
    {
        wxASSERT_MSG
        (
         !(style & ibAUI_TB_HORZ_LAYOUT),
         "Horizontal layout style can't be used with vertical toolbars"
        );

        if ( style & ibAUI_TB_TEXT )
        {
            if ( style & ibAUI_TB_VERT_LAYOUT_DOWN )
                SetToolTextDirection(ibAuiTextDirection::TopToBottom);
            else if ( style & ibAUI_TB_VERT_LAYOUT_UP )
                SetToolTextDirection(ibAuiTextDirection::BottomToTop);
        }
        else
        {
            wxASSERT_MSG
            (
             !(style & (ibAUI_TB_VERT_LAYOUT_DOWN | ibAUI_TB_VERT_LAYOUT_UP)),
             "Vertical text styles don't make sense without ibAUI_TB_TEXT"
            );
        }
    }

    if (style & ibAUI_TB_HORZ_LAYOUT)
        SetToolTextOrientation(ibAUI_TBTOOL_TEXT_RIGHT);
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    return true;
}

ibAuiToolBar::~ibAuiToolBar()
{
    delete m_sizer;
}

void ibAuiToolBar::SetWindowStyleFlag(long style)
{
    GetOrientation(style);      // assert if style is invalid
    wxCHECK_RET(IsPaneValid(style),
                "window settings and pane settings are incompatible");

    ibControl::SetWindowStyleFlag(style);

    if (m_windowStyle & ibAUI_TB_GRIPPER)
        m_gripperVisible = true;
    else
        m_gripperVisible = false;


    if (m_windowStyle & ibAUI_TB_OVERFLOW)
        m_overflowVisible = true;
    else
        m_overflowVisible = false;

    if (style & ibAUI_TB_HORZ_LAYOUT)
        SetToolTextOrientation(ibAUI_TBTOOL_TEXT_RIGHT);
    else
        SetToolTextOrientation(ibAUI_TBTOOL_TEXT_BOTTOM);

    ibControl::SetWindowStyleFlag(style);

    // SEAM vs wx: ibAUI_TB_PLAIN_BACKGROUND chooses between the bar's two backgrounds, as it chose between the art's
    // (EraseBackground)
}

wxSize ibAuiToolBar::DoGetBestSize() const
{
    auto bestSize = m_orientation == wxHORIZONTAL ? m_horzHintSize
                                                  : m_vertHintSize;

    if ( !bestSize.IsFullySpecified() )
    {
        bestSize.SetDefaults(FromDIP(wxSize(1, 1)));
    }

    // a bar that collapses into the overflow can be given any width (SetDesignerMode)
    if ( !m_designerMode && HasFlag(ibAUI_TB_OVERFLOW) )
        bestSize.SetWidth(FromDIP(32));

    return bestSize;
}

void ibAuiToolBar::SetDesignerMode(bool designer)
{
    m_designerMode = designer;
    InvalidateBestSize();
}

ibAuiToolBarItem* ibAuiToolBar::AddTool(int tool_id,
                           const wxString& label,
                           const wxBitmapBundle& bitmap,
                           const wxString& shortHelp_string,
                           wxItemKind kind)
{
    return AddTool(tool_id,
            label,
            bitmap,
            wxBitmapBundle(),
            kind,
            shortHelp_string,
            wxEmptyString,
            nullptr);
}


ibAuiToolBarItem* ibAuiToolBar::AddTool(int tool_id,
                           const wxString& label,
                           const wxBitmapBundle& bitmap,
                           const wxBitmapBundle& disabledBitmap,
                           wxItemKind kind,
                           const wxString& shortHelpString,
                           const wxString& longHelpString,
                           wxObject* client_data)
{
    ibAuiToolBarItem item;
    item.m_label = label;
    item.m_bitmap = bitmap;
    item.m_disabledBitmap = disabledBitmap;
    item.m_shortHelp = shortHelpString;
    item.m_longHelp = longHelpString;
    item.m_toolId = tool_id;
    item.m_kind = kind;
    item.m_clientData = client_data;

    if (item.m_toolId == wxID_ANY)
        item.m_toolId = wxNewId();

    m_items.Add(item);
    return &m_items.Last();
}

ibAuiToolBarItem* ibAuiToolBar::AddControl(ibControl* control,
                              const wxString& label)
{
    ibAuiToolBarItem item;
    item.m_window = control;
    item.m_label = label;
    item.m_bitmap = wxBitmapBundle();
    item.m_disabledBitmap = wxBitmapBundle();
    item.m_toolId = control->GetId();
    item.m_kind = ibITEM_CONTROL;
    item.m_minSize = control->GetEffectiveMinSize();

    m_items.Add(item);
    return &m_items.Last();
}

ibAuiToolBarItem* ibAuiToolBar::AddLabel(int tool_id,
                            const wxString& label,
                            const int width)
{
    wxSize min_size = wxDefaultSize;
    if (width != -1)
        min_size.x = width;

    ibAuiToolBarItem item;
    item.m_label = label;
    item.m_toolId = tool_id;
    item.m_kind = ibITEM_LABEL;
    item.m_minSize = min_size;

    if (item.m_toolId == wxID_ANY)
        item.m_toolId = wxNewId();

    m_items.Add(item);
    return &m_items.Last();
}

ibAuiToolBarItem* ibAuiToolBar::AddSeparator()
{
    ibAuiToolBarItem item;
    item.m_toolId = -1;
    item.m_kind = wxITEM_SEPARATOR;

    m_items.Add(item);
    return &m_items.Last();
}

ibAuiToolBarItem* ibAuiToolBar::AddSpacer(int pixels)
{
    ibAuiToolBarItem item;
    item.m_spacerPixels = pixels;
    item.m_toolId = -1;
    item.m_kind = ibITEM_SPACER;

    m_items.Add(item);
    return &m_items.Last();
}

ibAuiToolBarItem* ibAuiToolBar::AddStretchSpacer(int proportion)
{
    ibAuiToolBarItem item;
    item.m_toolId = -1;
    item.m_proportion = proportion;
    item.m_kind = ibITEM_SPACER;

    m_items.Add(item);
    return &m_items.Last();
}

void ibAuiToolBar::Clear()
{
    m_items.Clear();
    m_sizerElementCount = 0;
}

bool ibAuiToolBar::DeleteTool(int tool_id)
{
    return DeleteByIndex(GetToolIndex(tool_id));
}

bool ibAuiToolBar::DeleteByIndex(int idx)
{
    if (idx >= 0 && idx < (int)m_items.GetCount())
    {
        m_items.RemoveAt(idx);
        Realize();
        return true;
    }

    return false;
}

bool ibAuiToolBar::DestroyTool(int tool_id)
{
    return DestroyToolByIndex(GetToolIndex(tool_id));
}

bool ibAuiToolBar::DestroyToolByIndex(int idx)
{
    if ( idx < 0 || static_cast<unsigned>(idx) >= m_items.GetCount() )
        return false;

    if ( wxWindow* window = m_items[idx].GetWindow() )
        window->Destroy();

    return DeleteByIndex(idx);
}


ibControl* ibAuiToolBar::FindControl(int id)
{
    return wxDynamicCast(FindWindow(id), ibControl);
}

ibAuiToolBarItem* ibAuiToolBar::FindTool(int tool_id) const
{
    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_toolId == tool_id)
            return &item;
    }

    return nullptr;
}

ibAuiToolBarItem* ibAuiToolBar::FindToolByPosition(wxCoord x, wxCoord y) const
{
    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);

        if (!item.m_sizerItem)
            continue;

        wxRect rect = item.m_sizerItem->GetRect();
        if (rect.Contains(x,y))
        {
            // if the item doesn't fit on the toolbar, return nullptr
            if (!GetToolFitsByIndex(i))
                return nullptr;

            return &item;
        }
    }

    return nullptr;
}

ibAuiToolBarItem* ibAuiToolBar::FindToolByPositionWithPacking(wxCoord x, wxCoord y) const
{
    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);

        if (!item.m_sizerItem)
            continue;

        wxRect rect = item.m_sizerItem->GetRect();

        // apply tool packing
        if (i+1 < count)
            rect.width += FromDIP(m_toolPacking);

        if (rect.Contains(x,y))
        {
            // if the item doesn't fit on the toolbar, return nullptr
            if (!GetToolFitsByIndex(i))
                return nullptr;

            return &item;
        }
    }

    return nullptr;
}

ibAuiToolBarItem* ibAuiToolBar::FindToolByIndex(int idx) const
{
    if (idx < 0)
        return nullptr;

    if (idx >= (int)m_items.size())
        return nullptr;

    return const_cast<ibAuiToolBarItem*>(&(m_items[idx]));
}

void ibAuiToolBar::SetToolClientData (int tool_id, wxObject *client_data)
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return;

    item->m_clientData = client_data;
}

wxObject* ibAuiToolBar::GetToolClientData(int tool_id) const
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return nullptr;

    return item->m_clientData;
}

void ibAuiToolBar::SetToolBitmapSize(const wxSize& WXUNUSED(size))
{
    // TODO: wxToolBar compatibility
}

wxSize ibAuiToolBar::GetToolBitmapSize() const
{
    // TODO: wxToolBar compatibility
    return FromDIP(wxSize(16,15));
}

void ibAuiToolBar::SetToolProportion(int tool_id, int proportion)
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return;

    item->m_proportion = proportion;
}

int ibAuiToolBar::GetToolProportion(int tool_id) const
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return 0;

    return item->m_proportion;
}

void ibAuiToolBar::SetToolSeparation(int separation)
{
    m_separatorSize = separation;
}

int ibAuiToolBar::GetToolSeparation() const
{
    return FromDIP(m_separatorSize);
}


void ibAuiToolBar::SetToolDropDown(int tool_id, bool dropdown)
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return;

    item->SetHasDropDown(dropdown);
}

bool ibAuiToolBar::GetToolDropDown(int tool_id) const
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return false;

    return item->HasDropDown();
}

void ibAuiToolBar::SetToolSticky(int tool_id, bool sticky)
{
    // ignore separators
    if (tool_id == -1)
        return;

    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return;

    if (item->m_sticky == sticky)
        return;

    item->m_sticky = sticky;

    Refresh(false);
    Update();
}

bool ibAuiToolBar::GetToolSticky(int tool_id) const
{
    ibAuiToolBarItem* item = FindTool(tool_id);
    if (!item)
        return 0;

    return item->m_sticky;
}




void ibAuiToolBar::SetToolBorderPadding(int padding)
{
    m_toolBorderPadding = ToDIP(padding);
}

int ibAuiToolBar::GetToolBorderPadding() const
{
    return m_toolBorderPadding;
}

void ibAuiToolBar::SetToolTextOrientation(int orientation)
{
    m_toolTextOrientation = orientation;
}

int ibAuiToolBar::GetToolTextOrientation() const
{
    return m_toolTextOrientation;
}

void ibAuiToolBar::SetToolTextDirection(ibAuiTextDirection direction)
{
    m_textDirection = direction;
}

ibAuiTextDirection ibAuiToolBar::GetToolTextDirection() const
{
    return m_textDirection;
}

bool ibAuiToolBar::IsToolTextVertical() const
{
    switch ( m_textDirection )
    {
        case ibAuiTextDirection::LeftToRight:
            return false;

        case ibAuiTextDirection::TopToBottom:
        case ibAuiTextDirection::BottomToTop:
            return true;
    }

    wxFAIL_MSG( "Unknown ibAuiTextDirection value" );
    return false;
}

void ibAuiToolBar::SetToolPacking(int packing)
{
    m_toolPacking = ToDIP(packing);
}

int ibAuiToolBar::GetToolPacking() const
{
    return m_toolPacking;
}


void ibAuiToolBar::SetOrientation(int orientation)
{
    wxCHECK_RET(orientation == wxHORIZONTAL ||
                orientation == wxVERTICAL,
                "invalid orientation value");
    if (orientation != m_orientation)
    {
        m_orientation = wxOrientation(orientation);

        Realize();
    }
}

void ibAuiToolBar::SetMargins(int left, int right, int top, int bottom)
{
    if (left != -1)
        m_leftPadding = ToDIP(left);
    if (right != -1)
        m_rightPadding = ToDIP(right);
    if (top != -1)
        m_topPadding = ToDIP(top);
    if (bottom != -1)
        m_bottomPadding = ToDIP(bottom);
}

bool ibAuiToolBar::GetGripperVisible() const
{
    return m_gripperVisible;
}

void ibAuiToolBar::SetGripperVisible(bool visible)
{
    m_gripperVisible = visible;
    if (visible)
        m_windowStyle |= ibAUI_TB_GRIPPER;
    else
        m_windowStyle &= ~ibAUI_TB_GRIPPER;
    Realize();
    Refresh(false);
}


bool ibAuiToolBar::GetOverflowVisible() const
{
    return m_overflowVisible;
}

void ibAuiToolBar::SetOverflowVisible(bool visible)
{
    m_overflowVisible = visible;
    if (visible)
        m_windowStyle |= ibAUI_TB_OVERFLOW;
    else
        m_windowStyle &= ~ibAUI_TB_OVERFLOW;
    Refresh(false);
}

void ibAuiToolBar::SetHoverItem(ibAuiToolBarItem* pitem)
{
    if ( wxFrame* frame = wxDynamicCast(wxGetTopLevelParent(this), wxFrame) )
    {
        wxString help;
        if (pitem)
        {
            help = pitem->GetLongHelp();
        }
        frame->DoGiveHelp(help, pitem);
    }

    if (pitem && (pitem->m_state & ibAUI_BUTTON_STATE_DISABLED))
        pitem = nullptr;

    ibAuiToolBarItem* former_hover = nullptr;

    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_state & ibAUI_BUTTON_STATE_HOVER)
            former_hover = &item;
        item.m_state &= ~ibAUI_BUTTON_STATE_HOVER;
    }

    if (pitem)
    {
        pitem->m_state |= ibAUI_BUTTON_STATE_HOVER;
    }

    if (former_hover != pitem)
    {
        Refresh(false);
        Update();
    }
}

void ibAuiToolBar::SetPressedItem(ibAuiToolBarItem* pitem)
{
    ibAuiToolBarItem* former_item = nullptr;

    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_state & ibAUI_BUTTON_STATE_PRESSED)
            former_item = &item;
        item.m_state &= ~ibAUI_BUTTON_STATE_PRESSED;
    }

    if (pitem)
    {
        pitem->m_state &= ~ibAUI_BUTTON_STATE_HOVER;
        pitem->m_state |= ibAUI_BUTTON_STATE_PRESSED;
    }

    if (former_item != pitem)
    {
        Refresh(false);
        Update();
    }
}

void ibAuiToolBar::RefreshOverflowState()
{
    if (!m_overflowSizerItem)
    {
        m_overflowState = 0;
        return;
    }

    int overflow_state = 0;


    // find out the mouse's current position
    const wxMouseState mouseState = ::wxGetMouseState();
    wxPoint pt = mouseState.GetPosition();
    pt = this->ScreenToClient(pt);

    // find out if the mouse cursor is inside the dropdown rectangle
    if (GetOverflowRect().Contains(pt))
    {
        if (mouseState.LeftIsDown())
            overflow_state = ibAUI_BUTTON_STATE_PRESSED;
        else
            overflow_state = ibAUI_BUTTON_STATE_HOVER;
    }

    if (overflow_state != m_overflowState)
    {
        m_overflowState = overflow_state;
        Refresh(false);
        Update();
    }

    m_overflowState = overflow_state;
}

void ibAuiToolBar::ToggleTool(int tool_id, bool state)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);

    if ( tool && tool->CanBeToggled() )
    {
        if (tool->m_kind == wxITEM_RADIO)
        {
            int idx, count;
            idx = GetToolIndex(tool_id);
            count = (int)m_items.GetCount();

            if (idx >= 0 && idx < count)
            {
                int i;
                for (i = idx + 1; i < count; ++i)
                {
                    if (m_items[i].m_kind != wxITEM_RADIO)
                        break;
                    m_items[i].m_state &= ~ibAUI_BUTTON_STATE_CHECKED;
                }
                for (i = idx - 1; i >= 0; i--)
                {
                    if (m_items[i].m_kind != wxITEM_RADIO)
                        break;
                    m_items[i].m_state &= ~ibAUI_BUTTON_STATE_CHECKED;
                }
            }

            tool->m_state |= ibAUI_BUTTON_STATE_CHECKED;
        }
         else if (tool->m_kind == wxITEM_CHECK)
        {
            if (state == true)
                tool->m_state |= ibAUI_BUTTON_STATE_CHECKED;
            else
                tool->m_state &= ~ibAUI_BUTTON_STATE_CHECKED;
        }
    }
}

bool ibAuiToolBar::GetToolToggled(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);

    if ( tool && tool->CanBeToggled() )
        return (tool->m_state & ibAUI_BUTTON_STATE_CHECKED) ? true : false;

    return false;
}

void ibAuiToolBar::EnableTool(int tool_id, bool state)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);

    if (tool)
    {
        if (state == true)
            tool->m_state &= ~ibAUI_BUTTON_STATE_DISABLED;
        else
            tool->m_state |= ibAUI_BUTTON_STATE_DISABLED;
    }
}

bool ibAuiToolBar::GetToolEnabled(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);

    if (tool)
        return (tool->m_state & ibAUI_BUTTON_STATE_DISABLED) ? false : true;

    return false;
}

wxString ibAuiToolBar::GetToolLabel(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    wxASSERT_MSG(tool, wxT("can't find tool in toolbar item array"));
    if (!tool)
        return wxEmptyString;

    return tool->m_label;
}

void ibAuiToolBar::SetToolLabel(int tool_id, const wxString& label)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    if (tool)
    {
        tool->m_label = label;
    }
}

wxBitmap ibAuiToolBar::GetToolBitmap(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    wxASSERT_MSG(tool, wxT("can't find tool in toolbar item array"));
    if (!tool)
        return wxNullBitmap;

    return tool->m_bitmap.GetBitmapFor(this);
}

void ibAuiToolBar::SetToolBitmap(int tool_id, const wxBitmapBundle& bitmap)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    if (tool)
    {
        tool->m_bitmap = bitmap;
    }
}

wxString ibAuiToolBar::GetToolShortHelp(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    wxASSERT_MSG(tool, wxT("can't find tool in toolbar item array"));
    if (!tool)
        return wxEmptyString;

    return tool->m_shortHelp;
}

void ibAuiToolBar::SetToolShortHelp(int tool_id, const wxString& help_string)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    if (tool)
    {
        tool->m_shortHelp = help_string;
    }
}

wxString ibAuiToolBar::GetToolLongHelp(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    wxASSERT_MSG(tool, wxT("can't find tool in toolbar item array"));
    if (!tool)
        return wxEmptyString;

    return tool->m_longHelp;
}

void ibAuiToolBar::SetToolLongHelp(int tool_id, const wxString& help_string)
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    if (tool)
    {
        tool->m_longHelp = help_string;
    }
}

void ibAuiToolBar::SetCustomOverflowItems(const ibAuiToolBarItemArray& prepend,
                                          const ibAuiToolBarItemArray& append)
{
    m_customOverflowPrepend = prepend;
    m_customOverflowAppend = append;
}

// get size of hint rectangle for a particular dock location
wxSize ibAuiToolBar::GetHintSize(int dock_direction) const
{
    switch (dock_direction)
    {
        case ibAUI_DOCK_TOP:
        case ibAUI_DOCK_BOTTOM:
            return m_horzHintSize;
        case ibAUI_DOCK_RIGHT:
        case ibAUI_DOCK_LEFT:
            return m_vertHintSize;
        default:
            wxFAIL_MSG("invalid dock location value");
    }
    return wxDefaultSize;
}

bool ibAuiToolBar::IsPaneValid(const ibAuiPaneInfo& pane) const
{
    return IsPaneValid(m_windowStyle, pane);
}

bool ibAuiToolBar::IsPaneValid(long style, const ibAuiPaneInfo& pane)
{
    if (style & ibAUI_TB_HORIZONTAL)
    {
        if (pane.IsLeftDockable() || pane.IsRightDockable())
        {
            return false;
        }
    }
    else if (style & ibAUI_TB_VERTICAL)
    {
        if (pane.IsTopDockable() || pane.IsBottomDockable())
        {
            return false;
        }
    }
    return true;
}

bool ibAuiToolBar::IsPaneValid(long style) const
{
    ibAuiManager* manager = ibAuiManager::GetManager(const_cast<ibAuiToolBar*>(this));
    if (manager)
    {
        return IsPaneValid(style, manager->GetPane(const_cast<ibAuiToolBar*>(this)));
    }
    return true;
}

size_t ibAuiToolBar::GetToolCount() const
{
    return m_items.size();
}

int ibAuiToolBar::GetToolIndex(int tool_id) const
{
    // this will prevent us from returning the index of the
    // first separator in the toolbar since its id is equal to -1
    if (tool_id == -1)
        return wxNOT_FOUND;

    size_t i, count = m_items.GetCount();
    for (i = 0; i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_toolId == tool_id)
            return i;
    }

    return wxNOT_FOUND;
}

bool ibAuiToolBar::GetToolFitsByIndex(int tool_idx) const
{
    if (tool_idx < 0 || tool_idx >= (int)m_items.GetCount())
        return false;

    if (!m_items[tool_idx].m_sizerItem)
        return false;

    int cli_w, cli_h;
    GetClientSize(&cli_w, &cli_h);

    wxRect rect = m_items[tool_idx].m_sizerItem->GetRect();

    if (m_orientation == wxVERTICAL)
    {
        // take the dropdown size into account
        if (m_overflowVisible && m_overflowSizerItem)
            cli_h -= m_overflowSizerItem->GetMinSize().y;

        if (rect.y+rect.height < cli_h)
            return true;
    }
    else
    {
        // take the dropdown size into account
        if (m_overflowVisible && m_overflowSizerItem)
            cli_w -= m_overflowSizerItem->GetMinSize().x;

        if (rect.x+rect.width < cli_w)
            return true;
    }

    return false;
}


bool ibAuiToolBar::GetToolFits(int tool_id) const
{
    return GetToolFitsByIndex(GetToolIndex(tool_id));
}

wxRect ibAuiToolBar::GetToolRect(int tool_id) const
{
    ibAuiToolBarItem* tool = FindTool(tool_id);
    if (tool && tool->m_sizerItem)
    {
        return tool->m_sizerItem->GetRect();
    }

    return wxRect();
}

bool ibAuiToolBar::GetToolBarFits() const
{
    if (m_items.GetCount() == 0)
    {
        // empty toolbar always 'fits'
        return true;
    }

    // entire toolbar content fits if the last tool fits
    return GetToolFitsByIndex(m_items.GetCount() - 1);
}

bool ibAuiToolBar::Realize()
{
    wxInfoDC dc(this);
    if (!dc.IsOk())
        return false;

    // calculate hint sizes for both horizontal and vertical orientations if we
    // can use them, i.e. if the toolbar isn't locked into just one of them, and
    // store the size appropriate for the current orientation in this variable.
    wxSize size;
    if (m_orientation == wxHORIZONTAL)
    {
        if (!HasFlag(ibAUI_TB_HORIZONTAL))
            m_vertHintSize = RealizeHelper(dc, wxVERTICAL);

        m_horzHintSize = RealizeHelper(dc, wxHORIZONTAL);

        size = m_horzHintSize;
    }
    else
    {
        if (!HasFlag(ibAUI_TB_VERTICAL))
            m_horzHintSize = RealizeHelper(dc, wxHORIZONTAL);

        m_vertHintSize = RealizeHelper(dc, wxVERTICAL);

        size = m_vertHintSize;
    }

    InvalidateBestSize();

    // And set control size if we are not forbidden from doing it by the use of
    // a special flag and if it did actually change.
    wxSize curSize = GetClientSize();

    if ((m_windowStyle & ibAUI_TB_NO_AUTORESIZE) == 0 && (size != curSize))
    {
        SetClientSize(size);
    }
    else // Don't change the size but let the sizer know about available size.
    {
        m_sizer->SetDimension(0, 0, curSize.x, curSize.y);
    }

    Refresh(false);
    return true;
}

wxSize ibAuiToolBar::RealizeHelper(wxReadOnlyDC& dc, wxOrientation orientation)
{
    // Remove old sizer before adding any controls in this tool bar, which are
    // elements of this sizer, to the new sizer below.
    delete m_sizer;
    m_sizer = nullptr;

    // create the new sizer to add toolbar elements to
    wxBoxSizer* sizer = new wxBoxSizer(orientation);

    // add gripper area
    int separatorSize = FromDIP(m_separatorSize);
    int gripperSize = FromDIP(m_gripperSize);
    if (gripperSize > 0 && m_gripperVisible)
    {
        m_gripperSizerItem = sizer->AddSpacer(gripperSize);
        m_gripperSizerItem->SetFlag(wxEXPAND);
    }
    else
    {
        m_gripperSizerItem = nullptr;
    }

    // add "left" padding
    if (m_leftPadding > 0)
    {
        sizer->AddSpacer(FromDIP(m_leftPadding));
    }

    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        wxSizerItem* sizerItem = nullptr;

        switch (item.m_kind)
        {
            case ibITEM_LABEL:
            {
                wxSize size = GetLabelSize(dc, item);
                sizerItem = sizer->Add(size.x + (FromDIP(m_toolBorderPadding*2)),
                                        size.y + (FromDIP(m_toolBorderPadding*2)),
                                        item.m_proportion,
                                        item.m_alignment);
                if (i+1 < count)
                {
                    sizer->AddSpacer(FromDIP(m_toolPacking));
                }

                break;
            }

            case wxITEM_CHECK:
            case wxITEM_NORMAL:
            case wxITEM_RADIO:
            {
                wxSize size = GetToolSize(dc, item);
                sizerItem = sizer->Add(size.x + (FromDIP(m_toolBorderPadding*2)),
                                        size.y + (FromDIP(m_toolBorderPadding*2)),
                                        0,
                                        item.m_alignment);
                // add tool packing
                if (i+1 < count)
                {
                    sizer->AddSpacer(FromDIP(m_toolPacking));
                }

                break;
            }

            case wxITEM_SEPARATOR:
            {
                sizerItem = sizer->AddSpacer(separatorSize);
                sizerItem->SetFlag(wxEXPAND);

                // add tool packing
                if (i+1 < count)
                {
                    sizer->AddSpacer(FromDIP(m_toolPacking));
                }

                break;
            }

            case ibITEM_SPACER:
                if (item.m_proportion > 0)
                    sizerItem = sizer->AddStretchSpacer(item.m_proportion);
                else
                    sizerItem = sizer->Add(item.m_spacerPixels, 1);
                break;

            case ibITEM_CONTROL:
            {
                wxSizerItem* ctrl_m_sizerItem;

                wxBoxSizer* vert_sizer = new wxBoxSizer(wxVERTICAL);
                vert_sizer->AddStretchSpacer();
                ctrl_m_sizerItem = vert_sizer->Add(item.m_window, 0, wxEXPAND);
                vert_sizer->AddStretchSpacer();
                if ( (m_windowStyle & ibAUI_TB_TEXT) &&
                     m_toolTextOrientation == ibAUI_TBTOOL_TEXT_BOTTOM &&
                     !item.GetLabel().empty() )
                {
                    wxSize s = GetLabelSize(item.GetLabel());
                    vert_sizer->Add(1, s.y);
                }


                sizerItem = sizer->Add(vert_sizer, item.m_proportion, wxEXPAND);

                wxSize min_size = item.m_minSize;


                // proportional items will disappear from the toolbar if
                // their min width is not set to something really small
                if (item.m_proportion != 0)
                {
                    min_size.x = 1;
                }

                if (min_size.IsFullySpecified())
                {
                    sizerItem->SetMinSize(min_size);
                    ctrl_m_sizerItem->SetMinSize(min_size);
                }

                // add tool packing
                if (i+1 < count)
                {
                    sizer->AddSpacer(FromDIP(m_toolPacking));
                }
            }
        }

        item.m_sizerItem = sizerItem;
    }

    // add "right" padding
    if (m_rightPadding > 0)
    {
        sizer->AddSpacer(FromDIP(m_rightPadding));
    }

    // add drop down area
    m_overflowSizerItem = nullptr;

    if (m_windowStyle & ibAUI_TB_OVERFLOW)
    {
        int overflow_size = FromDIP(m_overflowSize);
        if (overflow_size > 0 && m_overflowVisible)
        {
            m_overflowSizerItem = sizer->AddSpacer(overflow_size);
            m_overflowSizerItem->SetFlag(wxEXPAND);
            m_overflowSizerItem->SetMinSize(m_overflowSizerItem->GetSize());
        }
        else
        {
            m_overflowSizerItem = nullptr;
        }
    }


    // the outside sizer helps us apply the "top" and "bottom" padding
    wxBoxSizer* outside_sizer = new wxBoxSizer(orientation ^ wxBOTH);

    // add "top" padding
    if (m_topPadding > 0)
    {
        outside_sizer->AddSpacer(FromDIP(m_topPadding));
    }

    // add the sizer that contains all of the toolbar elements
    outside_sizer->Add(sizer, 1, wxEXPAND);

    // add "bottom" padding
    if (m_bottomPadding > 0)
    {
        outside_sizer->AddSpacer(FromDIP(m_bottomPadding));
    }

    m_sizer = outside_sizer;

    // calculate the rock-bottom minimum size
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_sizerItem && item.m_proportion > 0 && item.m_minSize.IsFullySpecified())
            item.m_sizerItem->SetMinSize(0,0);
    }

    m_absoluteMinSize = m_sizer->GetMinSize();

    // reset the min sizes to what they were
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);
        if (item.m_sizerItem && item.m_proportion > 0 && item.m_minSize.IsFullySpecified())
            item.m_sizerItem->SetMinSize(item.m_minSize);
    }

    return m_sizer->GetMinSize();
}

bool ibAuiToolBar::RealizeHelper(wxClientDC& dc, bool horizontal)
{
    RealizeHelper(static_cast<wxReadOnlyDC&>(dc),
                  horizontal ? wxHORIZONTAL : wxVERTICAL);
    return true;
}

bool ibAuiToolBar::CanStretch() const
{
    for (auto const& item : m_items)
    {
        if (item.m_proportion)
            return true;
    }

    return false;
}

int ibAuiToolBar::GetOverflowState() const
{
    return m_overflowState;
}

wxRect ibAuiToolBar::GetOverflowRect() const
{
    const wxSize cli_size = GetClientSize();
    wxRect overflow_rect;
    int overflow_size = FromDIP(m_overflowSize);

    if (m_orientation == wxVERTICAL)
    {
        overflow_rect.y = cli_size.y - overflow_size;
        overflow_rect.x = 0;
        overflow_rect.width = cli_size.x;
        overflow_rect.height = overflow_size;
    }
    else
    {
        overflow_rect.x = cli_size.x - overflow_size;
        overflow_rect.y = 0;
        overflow_rect.width = overflow_size;
        overflow_rect.height = cli_size.y;
    }

    return overflow_rect;
}

wxSize ibAuiToolBar::GetLabelSize(const wxString& label)
{
    wxInfoDC dc(this);

    int textWidth = 0, textHeight = 0;

    dc.SetFont(m_font);

    // get the text height
    dc.GetTextExtent(wxT("ABCDHgj"), nullptr, &textHeight);

    // get the text width
    dc.GetTextExtent(label, &textWidth, nullptr);

    return wxSize(textWidth, textHeight);
}

// SEAM vs wx: a tool's size and a label's are the bar's - the art measured them (ibAuiGenericToolBarArt), the theme
// draws them in the sizes measured here
wxSize ibAuiToolBar::GetLabelSize(wxReadOnlyDC& dc, const ibAuiToolBarItem& item) const
{
    dc.SetFont(GetFont());

    // get label's height
    int width = 0, height = 0;
    dc.GetTextExtent(wxT("ABCDHgj"), &width, &height);

    // get item's width
    width = item.GetMinSize().GetWidth();

    if (width == -1)
    {
        // no width specified, measure the text ourselves
        width = dc.GetTextExtent(item.GetLabel()).GetX();
    }

    return wxSize(width, height);
}

wxSize ibAuiToolBar::GetToolSize(wxReadOnlyDC& dc, const ibAuiToolBarItem& item) const
{
    const wxBitmap bmp = item.GetBitmapBundle().GetBitmapFor(this);
    if (!bmp.IsOk() && !HasFlag(ibAUI_TB_TEXT))
        return FromDIP(wxSize(16, 16));

    int width = bmp.IsOk() ? bmp.GetLogicalWidth() : 0;
    int height = bmp.IsOk() ? bmp.GetLogicalHeight() : 0;

    if (HasFlag(ibAUI_TB_TEXT))
    {
        // the bar's default command is written bold (OnPaint), and measured so
        dc.SetFont(item.IsDefault() ? GetFont().Bold() : GetFont());
        int tx, ty;

        if (m_toolTextOrientation == ibAUI_TBTOOL_TEXT_BOTTOM)
        {
            dc.GetTextExtent(wxT("ABCDHgj"), &tx, &ty);
            height += ty;

            if (!item.GetLabel().empty())
            {
                dc.GetTextExtent(item.GetLabel(), &tx, &ty);
                width = wxMax(width, tx + FromDIP(6));
            }
        }
        else if (m_toolTextOrientation == ibAUI_TBTOOL_TEXT_RIGHT && !item.GetLabel().empty())
        {
            width += FromDIP(3); // space between left border and bitmap
            width += FromDIP(3); // space between bitmap and text

            dc.GetTextExtent(item.GetLabel(), &tx, &ty);
            width += tx;
            height = wxMax(height, ty);
        }
    }

    // if the tool has a dropdown button, add it to the width
    // and add some extra space in front of the drop down button
    if (item.HasDropDown())
        width += FromDIP(m_dropdownSize) + FromDIP(4);

    return wxSize(width, height);
}

// The tools the bar has no room for, as a menu under the bar where the mouse is: the command picked, or wxID_NONE
// (SEAM vs wx: the art's - a wxMenu and a handler pushed to catch its command; the menu is the theme's, ibMenu)
int ibAuiToolBar::ShowDropDown(const ibAuiToolBarItemArray& items)
{
    ibMenu menuPopup;

    size_t items_added = 0;
    for (const ibAuiToolBarItem& item : items)
    {
        if (item.GetKind() == wxITEM_NORMAL)
        {
            wxString text = item.GetShortHelp();
            if (text.empty())
                text = item.GetLabel();

            if (text.empty())
                text = wxT(" ");

            menuPopup.Append(item.GetId(), text, item.GetShortHelp())->SetBitmap(item.GetBitmapBundle());
            items_added++;
        }
        else if (item.GetKind() == wxITEM_SEPARATOR)
        {
            if (items_added > 0)
                menuPopup.AppendSeparator();
        }
    }

    // find out where to put the popup menu of window items
    wxPoint pt = ScreenToClient(::wxGetMousePosition());

    // find out the screen coordinate at the bottom of the tool bar
    const wxRect cli_rect = GetClientRect();
    pt.y = cli_rect.y + cli_rect.height;

    return GetPopupMenuSelectionFromUser(menuPopup, pt);
}


void ibAuiToolBar::DoIdleUpdate()
{
    wxEvtHandler* handler = GetEventHandler();

    bool need_refresh = false;

    size_t i, count;
    for (i = 0, count = m_items.GetCount(); i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);

        if (item.m_toolId == -1)
            continue;

        if (item.GetKind() != ibITEM_CONTROL)
        {
            wxUpdateUIEvent evt(item.m_toolId);
            evt.SetEventObject(this);

            if ( !item.CanBeToggled() )
                evt.DisallowCheck();

            if (handler->ProcessEvent(evt))
            {
                if (evt.GetSetEnabled())
                {
                    bool is_enabled;
                    if (item.m_window)
                        is_enabled = item.m_window->IsThisEnabled();
                    else
                        is_enabled = (item.m_state & ibAUI_BUTTON_STATE_DISABLED) ? false : true;

                    bool new_enabled = evt.GetEnabled();
                    if (new_enabled != is_enabled)
                    {
                        if (item.m_window)
                        {
                            item.m_window->Enable(new_enabled);
                        }
                        else
                        {
                            if (new_enabled)
                                item.m_state &= ~ibAUI_BUTTON_STATE_DISABLED;
                            else
                                item.m_state |= ibAUI_BUTTON_STATE_DISABLED;
                        }
                        need_refresh = true;
                    }
                }

                if (evt.GetSetChecked())
                {
                    // make sure we aren't checking an item that can't be
                    if (item.m_kind != wxITEM_CHECK && item.m_kind != wxITEM_RADIO)
                        continue;

                    bool is_checked = (item.m_state & ibAUI_BUTTON_STATE_CHECKED) ? true : false;
                    bool new_checked = evt.GetChecked();

                    if (new_checked != is_checked)
                    {
                        if (new_checked)
                            item.m_state |= ibAUI_BUTTON_STATE_CHECKED;
                        else
                            item.m_state &= ~ibAUI_BUTTON_STATE_CHECKED;

                        need_refresh = true;
                    }
                }

            }
        }
        else
        {
            item.GetWindow()->UpdateWindowUI();
        }
    }


    if (need_refresh)
    {
        Refresh(false);
    }
}


void ibAuiToolBar::OnSize(wxSizeEvent& WXUNUSED(evt))
{
    int x, y;
    GetClientSize(&x, &y);

    if (((x >= y) && m_absoluteMinSize.x > x) ||
        ((y > x) && m_absoluteMinSize.y > y))
    {
        // hide all flexible items
        size_t i, count;
        for (i = 0, count = m_items.GetCount(); i < count; ++i)
        {
            ibAuiToolBarItem& item = m_items.Item(i);
            if (item.m_sizerItem && item.m_proportion > 0 && item.m_sizerItem->IsShown())
            {
                item.m_sizerItem->Show(false);
                item.m_sizerItem->SetProportion(0);
            }
        }
    }
    else
    {
        // show all flexible items
        size_t i, count;
        for (i = 0, count = m_items.GetCount(); i < count; ++i)
        {
            ibAuiToolBarItem& item = m_items.Item(i);
            if (item.m_sizerItem && item.m_proportion > 0 && !item.m_sizerItem->IsShown())
            {
                item.m_sizerItem->Show(true);
                item.m_sizerItem->SetProportion(item.m_proportion);
            }
        }
    }

    const bool sized = m_sizer->GetSize() != wxSize(x, y);
    m_sizer->SetDimension(0, 0, x, y);

    // SEAM vs wx: the tools laid out anew are drawn anew - wx refreshed here when the art's background bitmap was remade
    // for the new size; the background is the theme's now (OnPaint). Not at a size that changed nothing: a sizer sends it
    // to the bar at each of its layouts (wxSizerItem::SetDimension, wxSIZE_FORCE_EVENT), and a frame sized by its left
    // edge painted every bar whole at every step (2026-11-11, measured)
    if ( sized )
        Refresh(false);

    // idle events aren't sent while user is resizing frame (why?),
    // but resizing toolbar here causes havoc,
    // so force idle handler to run after size handling complete
    QueueEvent(new wxIdleEvent);
}



void ibAuiToolBar::OnIdle(wxIdleEvent& evt)
{
    // if orientation doesn't match dock, fix it
    ibAuiManager* manager = ibAuiManager::GetManager(this);
    if (manager)
    {
        ibAuiPaneInfo& pane = manager->GetPane(this);
        // pane state member is public, so it might have been changed
        // without going through ibPaneInfo::SetFlag() check
        bool ok = pane.IsOk();
        wxCHECK2_MSG(!ok || IsPaneValid(m_windowStyle, pane), ok = false,
                    "window settings and pane settings are incompatible");
        if (ok)
        {
            wxOrientation newOrientation = m_orientation;
            if (pane.IsDocked())
            {
                switch (pane.dock_direction)
                {
                    case ibAUI_DOCK_TOP:
                    case ibAUI_DOCK_BOTTOM:
                        newOrientation = wxHORIZONTAL;
                        break;
                    case ibAUI_DOCK_LEFT:
                    case ibAUI_DOCK_RIGHT:
                        newOrientation = wxVERTICAL;
                        break;
                    default:
                        wxFAIL_MSG("invalid dock location value");
                }
            }
            else if (pane.IsResizable() &&
                    GetOrientation(m_windowStyle) == wxBOTH)
            {
                // changing orientation in OnSize causes havoc
                int x, y;
                GetClientSize(&x, &y);

                if (x > y)
                {
                    newOrientation = wxHORIZONTAL;
                }
                else
                {
                    newOrientation = wxVERTICAL;
                }
            }
            if (newOrientation != m_orientation)
            {
                SetOrientation(newOrientation);

                if (newOrientation == wxHORIZONTAL)
                {
                    pane.best_size = GetHintSize(ibAUI_DOCK_TOP);
                }
                else
                {
                    pane.best_size = GetHintSize(ibAUI_DOCK_LEFT);
                }
                if (pane.IsDocked())
                {
                    pane.floating_size = wxDefaultSize;
                    pane.floating_client_size = wxDefaultSize;
                }
                else
                {
                    SetSize(GetParent()->GetClientSize());
                }
                manager->Update();
            }
        }
    }
    evt.Skip();
}

void ibAuiToolBar::OnDPIChanged(wxDPIChangedEvent& event)
{
    for ( auto& item : m_items )
    {
        switch ( item.m_kind )
        {
            case wxITEM_NORMAL:
            case wxITEM_CHECK:
            case wxITEM_RADIO:
            case wxITEM_SEPARATOR:
            case ibITEM_LABEL:
                break;

            case ibITEM_CONTROL:
                // Take into account the new minimum control size when
                // performing layout in Realize() called below.
                item.m_minSize = item.m_window->GetEffectiveMinSize();
                break;
        }
    }

    Realize();

    event.Skip();
}

void ibAuiToolBar::UpdateWindowUI(long flags)
{
    if ( flags & wxUPDATE_UI_FROMIDLE )
    {
        DoIdleUpdate();
    }

    ibControl::UpdateWindowUI(flags);
}

void ibAuiToolBar::EraseBackground(wxDC& dc, const wxRect& rect)
{
    if ( HasFlag(ibAUI_TB_PLAIN_BACKGROUND) )
    {
        ibControl::EraseBackground(dc, rect);
        return;
    }
    GetRenderer()->DrawToolBarBackground(dc, wxTHEME_BG_COLOUR(this), rect, GetStateFlags());
}

void ibAuiToolBar::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
    // SEAM vs wx: the theme draws the bar - its background (ibThemePaintDC), the gripper, the tools, the separators, the
    // labels and the overflow button in the renderer's words; wx drew them by an art provider
    ibThemePaintDC dc(this);
    ibRenderer * const renderer = GetRenderer();
    const wxSize cli_size = GetClientSize();

    dc.SetFont(GetFont());
    dc.SetTextForeground(GetForegroundColour());

    bool horizontal = m_orientation == wxHORIZONTAL;

    // the bar in the words of the theme's tool bar: its orientation, the labels shown, beside the bitmaps or under them
    int tbarStyle = horizontal ? wxTB_HORIZONTAL : wxTB_VERTICAL;
    if (HasFlag(ibAUI_TB_TEXT))
        tbarStyle |= wxTB_TEXT;
    if (m_toolTextOrientation == ibAUI_TBTOOL_TEXT_RIGHT)
        tbarStyle |= wxTB_HORZ_LAYOUT;

    int gripperSize = FromDIP(m_gripperSize);
    int overflowSize = FromDIP(m_overflowSize);

    // the arrow of a drop-down tool and of the overflow button: a square of the drop-down part's width (the art's
    // bitmaps were 5x3 and 7x6)
    const wxRect arrowRect(FromDIP(wxSize(m_dropdownSize, m_dropdownSize)));

    // paint the gripper
    if (gripperSize > 0 && m_gripperSizerItem)
    {
        wxRect gripper_rect = m_gripperSizerItem->GetRect();
        if (horizontal)
            gripper_rect.width = gripperSize;
        else
            gripper_rect.height = gripperSize;
        renderer->DrawGripper(dc, gripper_rect, m_orientation);
    }

    // calculated how far we can draw items
    int last_extent;
    if (horizontal)
        last_extent = cli_size.x;
    else
        last_extent = cli_size.y;
    if (m_overflowVisible)
        last_extent -= overflowSize;

    // paint each individual tool
    size_t i, count = m_items.GetCount();
    for (i = 0; i < count; ++i)
    {
        ibAuiToolBarItem& item = m_items.Item(i);

        if (!item.m_sizerItem)
            continue;

        wxRect item_rect = item.m_sizerItem->GetRect();


        if ((horizontal  && item_rect.x + item_rect.width >= last_extent) ||
            (!horizontal && item_rect.y + item_rect.height >= last_extent))
        {
            break;
        }

        // the tool's state as the theme reads it - a sticky tool is drawn as the one under the mouse (wx's arts), every
        // tool of a disabled bar as a disabled one (GetCurrentBitmapFor), the bar's default command as the default
        // button - and its label, where the bar shows labels
        const int state = !IsEnabled() ? ibAUI_BUTTON_STATE_DISABLED
                        : item.IsSticky() ? item.GetState() | ibAUI_BUTTON_STATE_HOVER : item.GetState();
        const int flags = ibAuiGetControlFlags(state) | (item.IsDefault() ? wxCONTROL_ISDEFAULT : 0);
        const wxString label = HasFlag(ibAUI_TB_TEXT) ? item.GetLabel() : wxString();
        // the bar's default command written bold - the main button (2026-11-12, Max), measured so (GetToolSize), a
        // disabled one as well, so the bar does not move as it is enabled. On GTK3/Cocoa the same - the bar is the
        // engine's there too
        dc.SetFont(item.IsDefault() ? GetFont().Bold() : GetFont());

        switch ( item.m_kind )
        {
            case wxITEM_NORMAL:
                // draw a regular or dropdown button
                if (!item.m_dropDown)
                {
                    renderer->DrawToolBarButton(dc, label, item.GetCurrentBitmapFor(this), item_rect, flags,
                                                wxTOOL_STYLE_BUTTON, tbarStyle);
                }
                else
                {
                    // two buttons side by side, their lines on one pixel, the arrow on the second (the art's geometry)
                    const int dropdownWidth = FromDIP(m_dropdownSize);
                    const wxRect buttonRect(item_rect.x, item_rect.y, item_rect.width - dropdownWidth, item_rect.height);
                    const wxRect dropDownRect(item_rect.GetRight() - dropdownWidth, item_rect.y, dropdownWidth + 1,
                                              item_rect.height);
                    renderer->DrawToolBarButton(dc, label, item.GetCurrentBitmapFor(this), buttonRect, flags,
                                                wxTOOL_STYLE_BUTTON, tbarStyle);
                    renderer->DrawToolBarButton(dc, wxString(), wxNullBitmap, dropDownRect, flags, wxTOOL_STYLE_BUTTON,
                                                tbarStyle);
                    renderer->DrawArrow(dc, wxDOWN, arrowRect.CentreIn(dropDownRect), flags & wxCONTROL_DISABLED);
                }
                break;

            case wxITEM_CHECK:
            case wxITEM_RADIO:
                // draw a toggle button
                renderer->DrawToolBarButton(dc, label, item.GetCurrentBitmapFor(this), item_rect, flags,
                                            wxTOOL_STYLE_BUTTON, tbarStyle);
                break;

            case wxITEM_SEPARATOR:
                // draw a separator
                renderer->DrawToolBarButton(dc, wxString(), wxNullBitmap, item_rect, 0, wxTOOL_STYLE_SEPARATOR, tbarStyle);
                break;

            case ibITEM_LABEL:
            {
                // draw a text label only, cut at the item's end
                wxDCClipper clip(dc, item_rect);
                renderer->DrawLabel(dc, item.GetLabel(), item_rect, flags & wxCONTROL_DISABLED,
                                    wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL);
                break;
            }

            case ibITEM_CONTROL:
                // draw the control's label under it - none wider than the control
                if (m_toolTextOrientation == ibAUI_TBTOOL_TEXT_BOTTOM && !label.empty() &&
                    dc.GetTextExtent(label).x <= item_rect.width)
                {
                    renderer->DrawLabel(dc, label, item_rect, flags & wxCONTROL_DISABLED,
                                        wxALIGN_CENTRE_HORIZONTAL | wxALIGN_BOTTOM);
                }
                break;
        }

        // fire a signal to see if the item wants to be custom-rendered
        OnCustomRender(dc, item, item_rect);
    }

    // paint the overflow button
    if (overflowSize > 0 && m_overflowSizerItem && m_overflowVisible)
    {
        wxRect dropDownRect = GetOverflowRect();
        renderer->DrawToolBarButton(dc, wxString(), wxNullBitmap, dropDownRect, ibAuiGetControlFlags(m_overflowState),
                                    wxTOOL_STYLE_BUTTON, tbarStyle);
        renderer->DrawArrow(dc, wxDOWN, arrowRect.CentreIn(dropDownRect));
    }
}

void ibAuiToolBar::OnLeftDown(wxMouseEvent& evt)
{
    if (m_gripperSizerItem)
    {
        wxRect gripper_rect = m_gripperSizerItem->GetRect();
        if (gripper_rect.Contains(evt.GetPosition()))
        {
            // find aui manager
            ibAuiManager* manager = ibAuiManager::GetManager(this);
            if (!manager)
                return;

            int x_drag_offset = evt.GetX() - gripper_rect.GetX();
            int y_drag_offset = evt.GetY() - gripper_rect.GetY();

            // gripper was clicked
            manager->StartPaneDrag(this, wxPoint(x_drag_offset, y_drag_offset));
            return;
        }
    }

    if (m_overflowSizerItem && m_overflowVisible)
    {
        if (GetOverflowRect().Contains(evt.GetPosition()))
        {
            ibAuiToolBarEvent e(ibEVT_AUITOOLBAR_OVERFLOW_CLICK, -1);
            e.SetEventObject(this);
            e.SetToolId(-1);
            e.SetClickPoint(wxPoint(evt.GetX(), evt.GetY()));
            bool processed = GetEventHandler()->ProcessEvent(e);

            if (processed)
            {
                DoIdleUpdate();
            }
            else
            {
                size_t i, count;
                ibAuiToolBarItemArray overflow_items;


                // add custom overflow prepend items, if any
                count = m_customOverflowPrepend.GetCount();
                for (i = 0; i < count; ++i)
                    overflow_items.Add(m_customOverflowPrepend[i]);

                // only show items that don't fit in the dropdown
                count = m_items.GetCount();
                for (i = 0; i < count; ++i)
                {
                    if (!GetToolFitsByIndex(i))
                        overflow_items.Add(m_items[i]);
                }

                // add custom overflow append items, if any
                count = m_customOverflowAppend.GetCount();
                for (i = 0; i < count; ++i)
                    overflow_items.Add(m_customOverflowAppend[i]);

                int res = ShowDropDown(overflow_items);
                m_overflowState = 0;
                Refresh(false);
                if (res != -1)
                {
                    wxCommandEvent event(wxEVT_MENU, res);
                    event.SetEventObject(this);
                    GetEventHandler()->ProcessEvent(event);
                }
            }

            return;
        }
    }

    m_dragging = false;
    m_actionPos = wxPoint(evt.GetX(), evt.GetY());
    m_actionItem = FindToolByPosition(evt.GetX(), evt.GetY());

    if (m_actionItem)
    {
        if (m_actionItem->m_state & ibAUI_BUTTON_STATE_DISABLED)
        {
            m_actionPos = wxPoint(-1,-1);
            m_actionItem = nullptr;
            return;
        }

        UnsetToolTip();

        // fire the tool dropdown event
        ibAuiToolBarEvent e(ibEVT_AUITOOLBAR_TOOL_DROPDOWN, m_actionItem->m_toolId);
        e.SetEventObject(this);
        e.SetToolId(m_actionItem->m_toolId);

        int mouse_x = evt.GetX();
        wxRect rect = m_actionItem->m_sizerItem->GetRect();
        int dropdownWidth = FromDIP(m_dropdownSize);
        const bool dropDownHit = m_actionItem->m_dropDown &&
                                 mouse_x >= (rect.x+rect.width-dropdownWidth) &&
                                 mouse_x < (rect.x+rect.width);
        e.SetDropDownClicked(dropDownHit);

        e.SetClickPoint(evt.GetPosition());
        e.SetItemRect(rect);

        // we only set the 'pressed button' state if we hit the actual button
        // and not just the drop-down
        SetPressedItem(dropDownHit ? nullptr : m_actionItem);

        if(dropDownHit)
        {
            m_actionPos = wxPoint(-1,-1);
            m_actionItem = nullptr;
        }

        if(!GetEventHandler()->ProcessEvent(e) || e.GetSkipped())
            CaptureMouse();

        // Ensure hovered item is really ok, as mouse may have moved during
        //  event processing
        wxPoint cursor_pos_after_evt = ScreenToClient(wxGetMousePosition());
        SetHoverItem(FindToolByPosition(cursor_pos_after_evt.x, cursor_pos_after_evt.y));

        DoIdleUpdate();
    }
}

void ibAuiToolBar::OnLeftUp(wxMouseEvent& evt)
{
    if (!HasCapture())
        return;

    SetPressedItem(nullptr);

    ibAuiToolBarItem* hitItem;
    hitItem = FindToolByPosition(evt.GetX(), evt.GetY());
    SetHoverItem(hitItem);

    if (m_dragging)
    {
        // TODO: it would make sense to send out an 'END_DRAG' event here,
        // otherwise a client would never know what to do with the 'BEGIN_DRAG'
        // event

        // OnCaptureLost() will be called now and this will reset all our state
        // tracking variables
        ReleaseMouse();
    }
    else
    {
        if (m_actionItem && hitItem == m_actionItem)
        {
            UnsetToolTip();

            wxCommandEvent e(wxEVT_MENU, m_actionItem->m_toolId);
            e.SetEventObject(this);

            if (hitItem->m_kind == wxITEM_CHECK || hitItem->m_kind == wxITEM_RADIO)
            {
                const bool toggle = !(m_actionItem->m_state & ibAUI_BUTTON_STATE_CHECKED);

                ToggleTool(m_actionItem->m_toolId, toggle);

                // repaint immediately
                Refresh(false);
                Update();

                e.SetInt(toggle);
            }

            // we have to release the mouse *before* sending the event, because
            // we don't know what a handler might do. It could open up a popup
            // menu for example and that would make us lose our capture anyway.

            ReleaseMouse();

            GetEventHandler()->ProcessEvent(e);

            // Ensure hovered item is really ok, as mouse may have moved during
            // event processing
            wxPoint cursor_pos_after_evt = ScreenToClient(wxGetMousePosition());
            SetHoverItem(FindToolByPosition(cursor_pos_after_evt.x, cursor_pos_after_evt.y));

            DoIdleUpdate();
        }
        else
            ReleaseMouse();
    }
}

void ibAuiToolBar::OnRightDown(wxMouseEvent& evt)
{
    if (HasCapture())
        return;

    if (m_gripperSizerItem)
    {
        if (m_gripperSizerItem->GetRect().Contains(evt.GetPosition()))
            return;
    }

    if (m_overflowSizerItem && m_overflowVisible)
    {
        if (GetOverflowRect().Contains(evt.GetPosition()))
            return;
    }

    m_actionPos = wxPoint(evt.GetX(), evt.GetY());
    m_actionItem = FindToolByPosition(evt.GetX(), evt.GetY());

    if (m_actionItem && m_actionItem->m_state & ibAUI_BUTTON_STATE_DISABLED)
    {
        m_actionPos = wxPoint(-1,-1);
        m_actionItem = nullptr;
        return;
    }

    UnsetToolTip();
}

void ibAuiToolBar::DoRightOrMiddleUp(wxMouseEvent& evt, wxEventType eventType)
{
    if (HasCapture())
        return;

    ibAuiToolBarItem* hitItem;
    hitItem = FindToolByPosition(evt.GetX(), evt.GetY());

    int toolId;
    if (m_actionItem && hitItem == m_actionItem)
    {
        toolId = m_actionItem->m_toolId;
    }
    else
    {
        // right-clicked on the invalid area of the toolbar
        toolId = wxNOT_FOUND;
    }

    ibAuiToolBarEvent e(eventType, toolId);
    e.SetEventObject(this);
    e.SetToolId(toolId);
    e.SetClickPoint(m_actionPos);
    GetEventHandler()->ProcessEvent(e);
    DoIdleUpdate();

    // reset member variables
    m_actionPos = wxPoint(-1,-1);
    m_actionItem = nullptr;
}

void ibAuiToolBar::OnRightUp(wxMouseEvent& evt)
{
    DoRightOrMiddleUp(evt, ibEVT_AUITOOLBAR_RIGHT_CLICK);
}

void ibAuiToolBar::OnMiddleDown(wxMouseEvent& evt)
{
    if (HasCapture())
        return;

    if (m_gripperSizerItem)
    {
        if (m_gripperSizerItem->GetRect().Contains(evt.GetPosition()))
            return;
    }

    if (m_overflowSizerItem && m_overflowVisible)
    {
        if (GetOverflowRect().Contains(evt.GetPosition()))
            return;
    }

    m_actionPos = wxPoint(evt.GetX(), evt.GetY());
    m_actionItem = FindToolByPosition(evt.GetX(), evt.GetY());

    if (m_actionItem)
    {
        if (m_actionItem->m_state & ibAUI_BUTTON_STATE_DISABLED)
        {
            m_actionPos = wxPoint(-1,-1);
            m_actionItem = nullptr;
            return;
        }
    }

    UnsetToolTip();
}

void ibAuiToolBar::OnMiddleUp(wxMouseEvent& evt)
{
    DoRightOrMiddleUp(evt, ibEVT_AUITOOLBAR_MIDDLE_CLICK);
}

void ibAuiToolBar::OnMotion(wxMouseEvent& evt)
{
    const bool button_pressed = HasCapture();

    // start a drag event
    if (!m_dragging && button_pressed && m_actionItem &&
        abs(evt.GetX() - m_actionPos.x) + abs(evt.GetY() - m_actionPos.y) > 5)
    {
        // TODO: sending this event only makes sense if there is an 'END_DRAG'
        // event sent sometime in the future (see OnLeftUp())
        ibAuiToolBarEvent e(ibEVT_AUITOOLBAR_BEGIN_DRAG, GetId());
        e.SetEventObject(this);
        e.SetToolId(m_actionItem->m_toolId);
        m_dragging = GetEventHandler()->ProcessEvent(e) && !e.GetSkipped();

        DoIdleUpdate();
    }

    if(m_dragging)
        return;

    ibAuiToolBarItem* hitItem = FindToolByPosition(evt.GetX(), evt.GetY());
    if(button_pressed)
    {
        // if we have a button pressed we want it to be shown in 'depressed'
        // state unless we move the mouse outside the button, then we want it
        // to show as just 'highlighted'
        if (hitItem == m_actionItem)
            SetPressedItem(m_actionItem);
        else
        {
            SetPressedItem(nullptr);
            SetHoverItem(m_actionItem);
        }
    }
    else
    {
        SetHoverItem(hitItem);

        // tooltips handling
        if ( !HasFlag(ibAUI_TB_NO_TOOLTIPS) )
        {
            ibAuiToolBarItem* packingHitItem;
            packingHitItem = FindToolByPositionWithPacking(evt.GetX(), evt.GetY());
            if ( packingHitItem )
            {
                if (packingHitItem != m_tipItem)
                {
                    m_tipItem = packingHitItem;

                    if ( !packingHitItem->m_shortHelp.empty() )
                        SetToolTip(packingHitItem->m_shortHelp);
                    else
                        UnsetToolTip();
                }
            }
            else
            {
                UnsetToolTip();
                m_tipItem = nullptr;
            }
        }

        // figure out the dropdown button state (are we hovering or pressing it?)
        RefreshOverflowState();
    }
}

void ibAuiToolBar::DoResetMouseState()
{
    RefreshOverflowState();
    SetHoverItem(nullptr);
    SetPressedItem(nullptr);

    m_tipItem = nullptr;

    // we have to reset those here, because the mouse-up handlers which do
    // it usually won't be called if we let go of a mouse button while we
    // are outside of the window
    m_actionPos = wxPoint(-1,-1);
    m_actionItem = nullptr;
}

void ibAuiToolBar::OnLeaveWindow(wxMouseEvent& evt)
{
    if(HasCapture())
    {
        evt.Skip();
        return;
    }

    DoResetMouseState();
}

void ibAuiToolBar::OnCaptureLost(wxMouseCaptureLostEvent& WXUNUSED(evt))
{
    m_dragging = false;

    DoResetMouseState();
}

void ibAuiToolBar::OnSetCursor(wxSetCursorEvent& evt)
{
    wxCursor cursor = wxNullCursor;

    if (m_gripperSizerItem)
    {
        if (m_gripperSizerItem->GetRect().Contains(evt.GetPosition()))
        {
            cursor = wxCursor(wxCURSOR_SIZING);
        }
    }

    evt.SetCursor(cursor);
}



