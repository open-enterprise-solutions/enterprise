// FORKED from wxWidgets' AUI (include/wx/aui/auibar.h of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/aui/toolbar.h
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2008-08-04
// Copyright:   (C) Copyright 2005, Kirix Corporation, All Rights Reserved.
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_AUI_AUIBAR_H_
#define _DESKGUI_UNIV_AUI_AUIBAR_H_

#include "wx/defs.h"

#include "deskgui/deskgui.h"


#include "wx/bmpbndl.h"
#include "wx/sizer.h"

#include "deskgui/univ/ctrl/control.h"

class WXDLLIMPEXP_FWD_CORE wxClientDC;
class WXDLLIMPEXP_FWD_CORE wxReadOnlyDC;
class DESKGUI_API ibAuiPaneInfo;

enum ibAuiToolBarStyle
{
    ibAUI_TB_TEXT          = 1 << 0,
    ibAUI_TB_NO_TOOLTIPS   = 1 << 1,
    ibAUI_TB_NO_AUTORESIZE = 1 << 2,
    ibAUI_TB_GRIPPER       = 1 << 3,
    ibAUI_TB_OVERFLOW      = 1 << 4,
    // using this style forces the toolbar to be vertical and
    // be only dockable to the left or right sides of the window
    // whereas by default it can be horizontal or vertical and
    // be docked anywhere
    ibAUI_TB_VERTICAL      = 1 << 5,
    ibAUI_TB_HORZ_LAYOUT   = 1 << 6,
    // analogous to ibAUI_TB_VERTICAL, but forces the toolbar
    // to be horizontal
    ibAUI_TB_HORIZONTAL    = 1 << 7,
    ibAUI_TB_PLAIN_BACKGROUND = 1 << 8,
    // For vertical toolbars, put icon before or after vertical text.
    ibAUI_TB_VERT_LAYOUT_DOWN = 1 << 9,
    ibAUI_TB_VERT_LAYOUT_UP = 1 << 10,
    // And rotate the icon to match the text orientation if this flag is set.
    ibAUI_TB_ROTATE_ICON_WITH_TEXT = 1 << 11,

    ibAUI_TB_HORZ_TEXT     = (ibAUI_TB_HORZ_LAYOUT | ibAUI_TB_TEXT),
    ibAUI_TB_VERT_TEXT_DOWN = (ibAUI_TB_VERT_LAYOUT_DOWN | ibAUI_TB_TEXT),
    ibAUI_TB_VERT_TEXT_UP = (ibAUI_TB_VERT_LAYOUT_UP | ibAUI_TB_TEXT),
    ibAUI_ORIENTATION_MASK = (ibAUI_TB_VERTICAL | ibAUI_TB_HORIZONTAL),
    ibAUI_TB_DEFAULT_STYLE = 0
};

enum ibAuiToolBarToolTextOrientation
{
    ibAUI_TBTOOL_TEXT_LEFT = 0,     // unused/unimplemented
    ibAUI_TBTOOL_TEXT_RIGHT = 1,
    ibAUI_TBTOOL_TEXT_TOP = 2,      // unused/unimplemented
    ibAUI_TBTOOL_TEXT_BOTTOM = 3
};

enum class ibAuiTextDirection
{
    // This is always used for horizontal toolbars.
    LeftToRight,

    // This may be used for vertical toolbars, typically on the right side.
    TopToBottom,

    // This may be used for vertical toolbars, typically on the left side.
    BottomToTop
};


// aui toolbar event class

class DESKGUI_API ibAuiToolBarEvent : public wxNotifyEvent
{
public:
    ibAuiToolBarEvent(wxEventType commandType = wxEVT_NULL,
                      int winId = 0)
          : wxNotifyEvent(commandType, winId)
        , m_clickPt(-1, -1)
        , m_rect(-1, -1, 0, 0)
    {
        m_isDropdownClicked = false;
        m_toolId = -1;
    }
    wxNODISCARD wxEvent *Clone() const override { return new ibAuiToolBarEvent(*this); }

    bool IsDropDownClicked() const  { return m_isDropdownClicked; }
    void SetDropDownClicked(bool c) { m_isDropdownClicked = c;    }

    wxPoint GetClickPoint() const        { return m_clickPt; }
    void SetClickPoint(const wxPoint& p) { m_clickPt = p;    }

    wxRect GetItemRect() const        { return m_rect; }
    void SetItemRect(const wxRect& r) { m_rect = r;    }

    int GetToolId() const  { return m_toolId; }
    void SetToolId(int toolId) { m_toolId = toolId; }

private:

    bool m_isDropdownClicked;
    wxPoint m_clickPt;
    wxRect m_rect;
    int m_toolId;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN_DEF_COPY(ibAuiToolBarEvent);
};


class DESKGUI_API ibAuiToolBarItem
{
    friend class ibAuiToolBar;

public:
    ibAuiToolBarItem() = default;
    ibAuiToolBarItem(const ibAuiToolBarItem&) = default;
    ibAuiToolBarItem& operator=(const ibAuiToolBarItem& c) = default;

    void Assign(const ibAuiToolBarItem& c)
    {
        *this = c;
    }


    void SetWindow(wxWindow* w) { m_window = w; }
    wxWindow* GetWindow() { return m_window; }

    void SetId(int newId) { m_toolId = newId; }
    int GetId() const { return m_toolId; }

    void SetKind(int newKind) { m_kind = newKind; }
    int GetKind() const { return m_kind; }

    void SetState(int newState) { m_state = newState; }
    int GetState() const { return m_state; }

    void SetSizerItem(wxSizerItem* s) { m_sizerItem = s; }
    wxSizerItem* GetSizerItem() const { return m_sizerItem; }

    void SetLabel(const wxString& s) { m_label = s; }
    const wxString& GetLabel() const { return m_label; }

    void SetBitmap(const wxBitmapBundle& bmp) { m_bitmap = bmp; }
    const wxBitmapBundle& GetBitmapBundle() const { return m_bitmap; }
    wxBitmap GetBitmapFor(wxWindow* wnd) const { return m_bitmap.GetBitmapFor(wnd); }
    wxBitmap GetBitmap() const { return GetBitmapFor(m_window); }

    void SetDisabledBitmap(const wxBitmapBundle& bmp) { m_disabledBitmap = bmp; }
    const wxBitmapBundle& GetDisabledBitmapBundle() const { return m_disabledBitmap; }
    wxBitmap GetDisabledBitmapFor(wxWindow* wnd) const { return m_disabledBitmap.GetBitmapFor(wnd); }
    wxBitmap GetDisabledBitmap() const { return GetDisabledBitmapFor(m_window); }

    // Return the bitmap for the current state, normal or disabled.
    wxBitmap GetCurrentBitmapFor(wxWindow* wnd) const;

    void SetHoverBitmap(const wxBitmapBundle& bmp) { m_hoverBitmap = bmp; }
    const wxBitmapBundle& GetHoverBitmapBundle() const { return m_hoverBitmap; }
    wxBitmap GetHoverBitmap() const { return m_hoverBitmap.GetBitmapFor(m_window); }

    void SetShortHelp(const wxString& s) { m_shortHelp = s; }
    const wxString& GetShortHelp() const { return m_shortHelp; }

    void SetLongHelp(const wxString& s) { m_longHelp = s; }
    const wxString& GetLongHelp() const { return m_longHelp; }

    void SetMinSize(const wxSize& s) { m_minSize = s; }
    const wxSize& GetMinSize() const { return m_minSize; }

    void SetSpacerPixels(int s) { m_spacerPixels = s; }
    int GetSpacerPixels() const { return m_spacerPixels; }

    void SetProportion(int p) { m_proportion = p; }
    int GetProportion() const { return m_proportion; }

    void SetActive(bool b) { m_active = b; }
    bool IsActive() const { return m_active; }

    void SetHasDropDown(bool b)
    {
        wxCHECK_RET( !b || m_kind == wxITEM_NORMAL,
                     wxS("Only normal tools can have drop downs") );

        m_dropDown = b;
    }

    bool HasDropDown() const { return m_dropDown; }

    void SetSticky(bool b) { m_sticky = b; }
    bool IsSticky() const { return m_sticky; }

    // the bar's default command (a form's «… and close»), drawn as a dialog's default button (wxCONTROL_ISDEFAULT)
    void SetDefault(bool b) { m_default = b; }
    bool IsDefault() const { return m_default; }

    void SetUserData(long l) { m_userData = l; }
    long GetUserData() const { return m_userData; }

    void SetClientData(wxObject* l) { m_clientData = l; }
    wxObject* GetClientData() const { return m_clientData; }

    void SetAlignment(int l) { m_alignment = l; }
    int GetAlignment() const { return m_alignment; }

    bool CanBeToggled() const
    {
        return m_kind == wxITEM_CHECK || m_kind == wxITEM_RADIO;
    }

private:

    wxWindow* m_window = nullptr;       // item's associated window
    wxString m_label;                   // label displayed on the item
    wxBitmapBundle m_bitmap;            // item's bitmap
    wxBitmapBundle m_disabledBitmap;    // item's disabled bitmap
    wxBitmapBundle m_hoverBitmap;       // item's hover bitmap
    wxString m_shortHelp;               // short help (for tooltip)
    wxString m_longHelp;                // long help (for status bar)
    wxSizerItem* m_sizerItem = nullptr; // sizer item
    wxSize m_minSize = wxDefaultSize;   // item's minimum size
    int m_spacerPixels = 0;             // size of a spacer
    int m_toolId = 0;                   // item's id
    int m_kind = wxITEM_NORMAL;         // item's kind
    int m_state = 0;                    // state
    int m_proportion = 0;               // proportion
    bool m_active = true;               // true if the item is currently active
    bool m_dropDown = false;            // item has a dropdown button if true
    bool m_sticky = false;              // button is always active if true
    bool m_default = false;             // the bar's default command if true
    long m_userData = 0;                // number associated with the item
    wxObject* m_clientData = nullptr;   // wxObject associated with the item
    int m_alignment = wxALIGN_CENTER;   // sizer alignment flag
};

using ibAuiToolBarItemArray = wxBaseObjectArray<ibAuiToolBarItem>;




// SEAM vs wx: a control of the theme's (ibControl), drawn by its renderer - wx's tool bar art (ibAuiToolBarArt, the
// generic and the Luna one) is gone, its sizes and its drop-down menu are the bar's own
class DESKGUI_API ibAuiToolBar : public ibControl
{
public:
    ibAuiToolBar() { Init(); }

    ibAuiToolBar(wxWindow* parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = ibAUI_TB_DEFAULT_STYLE)
    {
        Init();
        Create(parent, id, pos, size, style);
    }

    virtual ~ibAuiToolBar();

    bool Create(wxWindow* parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = ibAUI_TB_DEFAULT_STYLE);

    virtual void SetWindowStyleFlag(long style) override;

    // SEAM vs wx: the bar's background is the theme's tool bar (DrawToolBarBackground), as the art drew it
    // (wxAuiToolBarArt::DrawBackground) - a plain one (ibAUI_TB_PLAIN_BACKGROUND) the surface it stands on, flat. On
    // GTK3/Cocoa the same - the bar is the engine's there too
    virtual void EraseBackground(wxDC& dc, const wxRect& rect) override;


    ibAuiToolBarItem* AddTool(int toolId,
                 const wxString& label,
                 const wxBitmapBundle& bitmap,
                 const wxString& shortHelpString = wxEmptyString,
                 wxItemKind kind = wxITEM_NORMAL);

    ibAuiToolBarItem* AddTool(int toolId,
                 const wxString& label,
                 const wxBitmapBundle& bitmap,
                 const wxBitmapBundle& disabledBitmap,
                 wxItemKind kind,
                 const wxString& shortHelpString,
                 const wxString& longHelpString,
                 wxObject* clientData);

    ibAuiToolBarItem* AddTool(int toolId,
                 const wxBitmapBundle& bitmap,
                 const wxBitmapBundle& disabledBitmap,
                 bool toggle = false,
                 wxObject* clientData = nullptr,
                 const wxString& shortHelpString = wxEmptyString,
                 const wxString& longHelpString = wxEmptyString)
    {
        return AddTool(toolId,
                wxEmptyString,
                bitmap,
                disabledBitmap,
                toggle ? wxITEM_CHECK : wxITEM_NORMAL,
                shortHelpString,
                longHelpString,
                clientData);
    }

    ibAuiToolBarItem* AddLabel(int toolId,
                  const wxString& label = wxEmptyString,
                  const int width = -1);
    ibAuiToolBarItem* AddControl(ibControl* control,
                    const wxString& label = wxEmptyString);
    ibAuiToolBarItem* AddSeparator();
    ibAuiToolBarItem* AddSpacer(int pixels);
    ibAuiToolBarItem* AddStretchSpacer(int proportion = 1);

    bool Realize();

    ibControl* FindControl(int windowId);
    ibAuiToolBarItem* FindToolByPosition(wxCoord x, wxCoord y) const;
    ibAuiToolBarItem* FindToolByIndex(int idx) const;
    ibAuiToolBarItem* FindTool(int toolId) const;

    void ClearTools() { Clear() ; }
    void Clear();

    bool DestroyTool(int toolId);
    bool DestroyToolByIndex(int idx);

    // Note that these methods do _not_ delete the associated control, if any.
    // Use DestroyTool() or DestroyToolByIndex() if this is wanted.
    bool DeleteTool(int toolId);
    bool DeleteByIndex(int toolId);

    size_t GetToolCount() const;
    int GetToolPos(int toolId) const { return GetToolIndex(toolId); }
    int GetToolIndex(int toolId) const;
    bool GetToolFits(int toolId) const;
    wxRect GetToolRect(int toolId) const;
    bool GetToolFitsByIndex(int toolId) const;
    bool GetToolBarFits() const;

    void SetMargins(const wxSize& size) { SetMargins(size.x, size.x, size.y, size.y); }
    void SetMargins(int x, int y) { SetMargins(x, x, y, y); }
    void SetMargins(int left, int right, int top, int bottom);

    void SetToolClientData (int tool_id, wxObject* client_data);
    wxObject* GetToolClientData(int tool_id) const;

    void SetToolBitmapSize(const wxSize& size);
    wxSize GetToolBitmapSize() const;

    bool GetOverflowVisible() const;
    void SetOverflowVisible(bool visible);

    // SEAM vs wx: a bar that collapses its tools into the overflow (ibAUI_TB_OVERFLOW) asks for no more than that
    // button's width - wx 3.3 asks for every tool's, and a form pushed a horizontal scroll bar onto itself; edited in
    // the designer, it shows its real width.
    void SetDesignerMode(bool designer);

    bool GetGripperVisible() const;
    void SetGripperVisible(bool visible);

    void ToggleTool(int toolId, bool state);
    bool GetToolToggled(int toolId) const;

    void EnableTool(int toolId, bool state);
    bool GetToolEnabled(int toolId) const;

    void SetToolDropDown(int toolId, bool dropdown);
    bool GetToolDropDown(int toolId) const;

    void SetToolBorderPadding(int padding);
    int  GetToolBorderPadding() const;

    void SetToolTextOrientation(int orientation);
    int  GetToolTextOrientation() const;

    void SetToolTextDirection(ibAuiTextDirection direction);
    ibAuiTextDirection GetToolTextDirection() const;
    bool IsToolTextVertical() const;

    void SetToolPacking(int packing);
    int  GetToolPacking() const;

    void SetToolProportion(int toolId, int proportion);
    int  GetToolProportion(int toolId) const;

    void SetToolSeparation(int separation);
    int GetToolSeparation() const;

    void SetToolSticky(int toolId, bool sticky);
    bool GetToolSticky(int toolId) const;

    wxString GetToolLabel(int toolId) const;
    void SetToolLabel(int toolId, const wxString& label);

    wxBitmap GetToolBitmap(int toolId) const;
    void SetToolBitmap(int toolId, const wxBitmapBundle& bitmap);

    wxString GetToolShortHelp(int toolId) const;
    void SetToolShortHelp(int toolId, const wxString& helpString);

    wxString GetToolLongHelp(int toolId) const;
    void SetToolLongHelp(int toolId, const wxString& helpString);

    void SetCustomOverflowItems(const ibAuiToolBarItemArray& prepend,
                                const ibAuiToolBarItemArray& append);

    // get size of hint rectangle for a particular dock location
    wxSize GetHintSize(int dockDirection) const;
    bool IsPaneValid(const ibAuiPaneInfo& pane) const;

    // Override to call DoIdleUpdate().
    virtual void UpdateWindowUI(long flags = wxUPDATE_UI_NONE) override;

    // This function is for internal use only, don't call it from your code.
    bool CanStretch() const;

protected:
    void Init();

    // Override to return the minimum acceptable size because under wxMSW this
    // function returns DEFAULT_ITEM_HEIGHT (see wxControl::DoGetBestSize())
    // which is not suitable as height/width for a horizontal/vertical toolbar
    // if icon sizes are much smaller than DEFAULT_ITEM_HEIGHT.
    virtual wxSize DoGetBestSize() const override;

    virtual void OnCustomRender(wxDC& WXUNUSED(dc),
                                const ibAuiToolBarItem& WXUNUSED(item),
                                const wxRect& WXUNUSED(rect)) { }

protected:

    void DoIdleUpdate();
    void SetOrientation(int orientation);
    void SetHoverItem(ibAuiToolBarItem* item);
    void SetPressedItem(ibAuiToolBarItem* item);
    void RefreshOverflowState();

    int GetOverflowState() const;
    wxRect GetOverflowRect() const;
    wxSize GetLabelSize(const wxString& label);
    wxSize GetLabelSize(wxReadOnlyDC& dc, const ibAuiToolBarItem& item) const;
    wxSize GetToolSize(wxReadOnlyDC& dc, const ibAuiToolBarItem& item) const;
    int ShowDropDown(const ibAuiToolBarItemArray& items);
    ibAuiToolBarItem* FindToolByPositionWithPacking(wxCoord x, wxCoord y) const;

protected: // handlers

    void OnSize(wxSizeEvent& evt);
    void OnIdle(wxIdleEvent& evt);
    void OnDPIChanged(wxDPIChangedEvent& evt);
    void OnPaint(wxPaintEvent& evt);
    void OnLeftDown(wxMouseEvent& evt);
    void OnLeftUp(wxMouseEvent& evt);
    void OnRightDown(wxMouseEvent& evt);
    void OnRightUp(wxMouseEvent& evt);
    void OnMiddleDown(wxMouseEvent& evt);
    void OnMiddleUp(wxMouseEvent& evt);
    void OnMotion(wxMouseEvent& evt);
    void OnLeaveWindow(wxMouseEvent& evt);
    void OnCaptureLost(wxMouseCaptureLostEvent& evt);
    void OnSetCursor(wxSetCursorEvent& evt);

protected:

    ibAuiToolBarItemArray m_items;      // array of toolbar items
    wxBoxSizer* m_sizer;                // main sizer for toolbar
    ibAuiToolBarItem* m_actionItem;    // item that's being acted upon (pressed)
    ibAuiToolBarItem* m_tipItem;       // item that has its tooltip shown
    wxBitmap m_bitmap;                  // double-buffer bitmap
    wxSizerItem* m_gripperSizerItem;
    wxSizerItem* m_overflowSizerItem;
    wxSize m_absoluteMinSize;
    wxPoint m_actionPos;               // position of left-mouse down
    ibAuiToolBarItemArray m_customOverflowPrepend;
    ibAuiToolBarItemArray m_customOverflowAppend;

    int m_buttonWidth;
    int m_buttonHeight;
    int m_sizerElementCount;
    int m_leftPadding;
    int m_rightPadding;
    int m_topPadding;
    int m_bottomPadding;
    int m_toolPacking;
    int m_toolBorderPadding;
    int m_toolTextOrientation;
    int m_overflowState;
    bool m_dragging;
    bool m_gripperVisible;
    bool m_overflowVisible;

    // This function is only kept for compatibility, don't use in the new code.
    bool RealizeHelper(wxClientDC& dc, bool horizontal);

    static bool IsPaneValid(long style, const ibAuiPaneInfo& pane);
    bool IsPaneValid(long style) const;
    wxOrientation m_orientation;
    wxSize m_horzHintSize;
    wxSize m_vertHintSize;

private:
    // Common part of OnLeaveWindow() and OnCaptureLost().
    void DoResetMouseState();

    // Common part of On{Right,Middle}Up().
    void DoRightOrMiddleUp(wxMouseEvent& evt, wxEventType eventType);

    wxSize RealizeHelper(wxReadOnlyDC& dc, wxOrientation orientation);

    // the sizes the art had (Luna's), in DIPs
    int m_separatorSize = 7;
    int m_gripperSize = 7;
    int m_overflowSize = 16;
    int m_dropdownSize = 10;

    ibAuiTextDirection m_textDirection = ibAuiTextDirection::LeftToRight;

    bool m_designerMode = false;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_CLASS(ibAuiToolBar);
};




// wx event machinery

#ifndef SWIG

wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUITOOLBAR_TOOL_DROPDOWN, ibAuiToolBarEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUITOOLBAR_OVERFLOW_CLICK, ibAuiToolBarEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUITOOLBAR_RIGHT_CLICK, ibAuiToolBarEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUITOOLBAR_MIDDLE_CLICK, ibAuiToolBarEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUITOOLBAR_BEGIN_DRAG, ibAuiToolBarEvent );

typedef void (wxEvtHandler::*ibAuiToolBarEventFunction)(ibAuiToolBarEvent&);

#define ibAuiToolBarEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibAuiToolBarEventFunction, func)

#define EVT_AUITOOLBAR_TOOL_DROPDOWN(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUITOOLBAR_TOOL_DROPDOWN, winid, ibAuiToolBarEventHandler(fn))
#define EVT_AUITOOLBAR_OVERFLOW_CLICK(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUITOOLBAR_OVERFLOW_CLICK, winid, ibAuiToolBarEventHandler(fn))
#define EVT_AUITOOLBAR_RIGHT_CLICK(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUITOOLBAR_RIGHT_CLICK, winid, ibAuiToolBarEventHandler(fn))
#define EVT_AUITOOLBAR_MIDDLE_CLICK(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUITOOLBAR_MIDDLE_CLICK, winid, ibAuiToolBarEventHandler(fn))
#define EVT_AUITOOLBAR_BEGIN_DRAG(winid, fn) \
    wx__DECLARE_EVT1(ibEVT_AUITOOLBAR_BEGIN_DRAG, winid, ibAuiToolBarEventHandler(fn))

#else

// wxpython/swig event work
%constant wxEventType ibEVT_AUITOOLBAR_TOOL_DROPDOWN;
%constant wxEventType ibEVT_AUITOOLBAR_OVERFLOW_CLICK;
%constant wxEventType ibEVT_AUITOOLBAR_RIGHT_CLICK;
%constant wxEventType ibEVT_AUITOOLBAR_MIDDLE_CLICK;
%constant wxEventType ibEVT_AUITOOLBAR_BEGIN_DRAG;

%pythoncode {
    EVT_AUITOOLBAR_TOOL_DROPDOWN = wx.PyEventBinder( ibEVT_AUITOOLBAR_TOOL_DROPDOWN, 1 )
    EVT_AUITOOLBAR_OVERFLOW_CLICK = wx.PyEventBinder( ibEVT_AUITOOLBAR_OVERFLOW_CLICK, 1 )
    EVT_AUITOOLBAR_RIGHT_CLICK = wx.PyEventBinder( ibEVT_AUITOOLBAR_RIGHT_CLICK, 1 )
    EVT_AUITOOLBAR_MIDDLE_CLICK = wx.PyEventBinder( ibEVT_AUITOOLBAR_MIDDLE_CLICK, 1 )
    EVT_AUITOOLBAR_BEGIN_DRAG = wx.PyEventBinder( ibEVT_AUITOOLBAR_BEGIN_DRAG, 1 )
}
#endif  // SWIG

// old wxEVT_COMMAND_* constants
#define ibEVT_COMMAND_AUITOOLBAR_TOOL_DROPDOWN    ibEVT_AUITOOLBAR_TOOL_DROPDOWN
#define ibEVT_COMMAND_AUITOOLBAR_OVERFLOW_CLICK   ibEVT_AUITOOLBAR_OVERFLOW_CLICK
#define ibEVT_COMMAND_AUITOOLBAR_RIGHT_CLICK      ibEVT_AUITOOLBAR_RIGHT_CLICK
#define ibEVT_COMMAND_AUITOOLBAR_MIDDLE_CLICK     ibEVT_AUITOOLBAR_MIDDLE_CLICK
#define ibEVT_COMMAND_AUITOOLBAR_BEGIN_DRAG       ibEVT_AUITOOLBAR_BEGIN_DRAG

#endif  // _DESKGUI_UNIV_AUI_AUIBAR_H_

