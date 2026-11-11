// FORKED from wxWidgets' AUI (include/wx/aui/framemanager.h of wx 3.3.4; wx -> ib prefixes applied mechanically to the names it
// defines, its native arts left out): the docking, the tool bar and the notebook of the theme engine
// (docs/private/wx-fork.md).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/aui/framemanager.h
// Purpose:     wxaui: wx advanced user interface - docking window manager
// Author:      Benjamin I. Williams
// Created:     2005-05-17
// Copyright:   (C) Copyright 2005, Kirix Corporation, All Rights Reserved.
// Licence:     wxWindows Library Licence, Version 3.1
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_AUI_FRAMEMANAGER_H_
#define _DESKGUI_UNIV_AUI_FRAMEMANAGER_H_

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include "wx/defs.h"

#include "deskgui/deskgui.h"


#include "wx/dynarray.h"
#include "wx/gdicmn.h"
#include "wx/window.h"
#include "wx/timer.h"
#include "wx/sizer.h"
#include "wx/bmpbndl.h"
#include "wx/overlay.h"

#include <memory>

enum ibAuiManagerDock
{
    ibAUI_DOCK_NONE = 0,
    ibAUI_DOCK_TOP = 1,
    ibAUI_DOCK_RIGHT = 2,
    ibAUI_DOCK_BOTTOM = 3,
    ibAUI_DOCK_LEFT = 4,
    ibAUI_DOCK_CENTER = 5,
    ibAUI_DOCK_CENTRE = ibAUI_DOCK_CENTER
};

enum ibAuiManagerOption
{
    ibAUI_MGR_ALLOW_FLOATING           = 1 << 0,
    ibAUI_MGR_ALLOW_ACTIVE_PANE        = 1 << 1,
    ibAUI_MGR_TRANSPARENT_DRAG         = 1 << 2,
    ibAUI_MGR_TRANSPARENT_HINT         = 1 << 3,
    ibAUI_MGR_VENETIAN_BLINDS_HINT     = 1 << 4,
    ibAUI_MGR_RECTANGLE_HINT           = 1 << 5,
    ibAUI_MGR_HINT_FADE                = 1 << 6,
    ibAUI_MGR_NO_VENETIAN_BLINDS_FADE  = 0, // For compatibility only.
    ibAUI_MGR_LIVE_RESIZE              = 1 << 8,

    ibAUI_MGR_DEFAULT = ibAUI_MGR_ALLOW_FLOATING |
                        ibAUI_MGR_TRANSPARENT_HINT |
                        ibAUI_MGR_LIVE_RESIZE
};

enum ibAuiMinDockOption
{
    ibAUI_MIN_DOCK_ICONS       = 1 << 0,
    ibAUI_MIN_DOCK_TEXT        = 1 << 1,
    ibAUI_MIN_DOCK_BOTH        = ibAUI_MIN_DOCK_ICONS | ibAUI_MIN_DOCK_TEXT,

    ibAUI_MIN_DOCK_ROTATE_ICON_WITH_TEXT = 1 << 2,

    ibAUI_MIN_DOCK_DEFAULT = ibAUI_MIN_DOCK_BOTH
};

enum ibAuiPaneDockArtSetting
{
    ibAUI_DOCKART_SASH_SIZE = 0,
    ibAUI_DOCKART_CAPTION_SIZE = 1,
    ibAUI_DOCKART_GRIPPER_SIZE = 2,
    ibAUI_DOCKART_PANE_BORDER_SIZE = 3,
    ibAUI_DOCKART_PANE_BUTTON_SIZE = 4
};

enum ibAuiPaneButtonState
{
    ibAUI_BUTTON_STATE_NORMAL   = 0,
    ibAUI_BUTTON_STATE_HOVER    = 1 << 1,
    ibAUI_BUTTON_STATE_PRESSED  = 1 << 2,
    ibAUI_BUTTON_STATE_DISABLED = 1 << 3,
    ibAUI_BUTTON_STATE_HIDDEN   = 1 << 4,
    ibAUI_BUTTON_STATE_CHECKED  = 1 << 5
};

enum ibAuiButtonId
{
    ibAUI_BUTTON_CLOSE = 101,
    ibAUI_BUTTON_MAXIMIZE_RESTORE = 102,
    ibAUI_BUTTON_MINIMIZE = 103,
    ibAUI_BUTTON_PIN = 104,
    ibAUI_BUTTON_OPTIONS = 105,
    ibAUI_BUTTON_WINDOWLIST = 106,
    ibAUI_BUTTON_LEFT = 107,
    ibAUI_BUTTON_RIGHT = 108,
    ibAUI_BUTTON_UP = 109,
    ibAUI_BUTTON_DOWN = 110,
    ibAUI_BUTTON_CUSTOM1 = 201,
    ibAUI_BUTTON_CUSTOM2 = 202,
    ibAUI_BUTTON_CUSTOM3 = 203
};

enum ibAuiPaneInsertLevel
{
    ibAUI_INSERT_PANE = 0,
    ibAUI_INSERT_ROW = 1,
    ibAUI_INSERT_DOCK = 2
};




// forwards and array declarations
class ibAuiDockUIPart;
class ibAuiPaneInfo;
class ibAuiDockInfo;
class ibAuiManagerEvent;
class ibAuiMinDock;
class ibAuiSerializer;
class ibAuiDeserializer;

struct ibAuiDockLayoutInfo;
struct ibAuiPaneLayoutInfo;

using ibAuiDockUIPartArray = wxBaseArray<ibAuiDockUIPart>;
using ibAuiDockInfoArray = wxBaseArray<ibAuiDockInfo>;
using ibAuiDockInfoPtrArray = wxBaseArray<ibAuiDockInfo*>;
using ibAuiPaneInfoPtrArray = wxBaseArray<ibAuiPaneInfo*>;

extern DESKGUI_API ibAuiDockInfo ibAuiNullDockInfo;
extern DESKGUI_API ibAuiPaneInfo ibAuiNullPaneInfo;


// Basic information about a dock, used by ibAuiPaneInfo and ibAuiDockInfo.
class ibAuiDockCoords
{
public:
    ibAuiDockCoords()
    {
        dock_direction = 0;
        dock_layer = 0;
        dock_row = 0;
    }

    bool IsHorizontal() const { return dock_direction == ibAUI_DOCK_TOP ||
                             dock_direction == ibAUI_DOCK_BOTTOM; }
    bool IsVertical() const { return dock_direction == ibAUI_DOCK_LEFT ||
                             dock_direction == ibAUI_DOCK_RIGHT; }
    bool IsVerticalOrCenter() const { return IsVertical() ||
                             dock_direction == ibAUI_DOCK_CENTER; }

    int dock_direction;     // dock direction (top, bottom, left, right, center)
    int dock_layer;         // layer number (0 = innermost layer)
    int dock_row;           // row number on the docking bar (0 = first row)
};

class DESKGUI_API ibAuiPaneInfo : public ibAuiDockCoords
{
public:

    ibAuiPaneInfo()
        : best_size(wxDefaultSize)
        , min_size(wxDefaultSize)
        , max_size(wxDefaultSize)
        , floating_pos(wxDefaultPosition)
        , floating_size(wxDefaultSize)
        , floating_client_size(wxDefaultSize)
    {
        window = nullptr;
        frame = nullptr;
        state = 0;
        dock_direction = ibAUI_DOCK_LEFT;
        dock_pos = 0;
        dock_size = 0;
        dock_proportion = 0;

        DefaultPane();
    }

    ~ibAuiPaneInfo() = default;

    // Write the safe parts of a newly loaded PaneInfo structure "source" into "this"
    // used on loading perspectives etc.
    void SafeSet(ibAuiPaneInfo source)
    {
        // note source is not passed by reference so we can overwrite, to keep the
        // unsafe bits of "dest"
        source.window = window;
        source.frame = frame;
        if (source.IsValid())
            *this = source;
    }

    bool IsOk() const { return window != nullptr; }
    bool IsFixed() const { return !HasFlag(optionResizable); }
    bool IsResizable() const { return HasFlag(optionResizable); }
    bool IsShown() const { return !HasFlag(optionHidden); }
    bool IsFloating() const { return HasFlag(optionFloating); }
    bool IsDocked() const { return !HasFlag(optionFloating); }
    bool IsToolbar() const { return HasFlag(optionToolbar); }
    bool IsTopDockable() const { return HasFlag(optionTopDockable); }
    bool IsBottomDockable() const { return HasFlag(optionBottomDockable); }
    bool IsLeftDockable() const { return HasFlag(optionLeftDockable); }
    bool IsRightDockable() const { return HasFlag(optionRightDockable); }
    bool IsDockable() const
    {
        return HasFlag(optionTopDockable | optionBottomDockable |
                        optionLeftDockable | optionRightDockable);
    }
    bool IsFloatable() const { return HasFlag(optionFloatable); }
    bool IsMovable() const { return HasFlag(optionMovable); }
    bool IsDestroyOnClose() const { return HasFlag(optionDestroyOnClose); }
    bool IsMaximized() const { return HasFlag(optionMaximized); }
    bool HasCaption() const { return HasFlag(optionCaption); }
    bool HasGripper() const { return HasFlag(optionGripper); }
    bool HasBorder() const { return HasFlag(optionPaneBorder); }
    bool HasCloseButton() const { return HasFlag(buttonClose); }
    bool HasMaximizeButton() const { return HasFlag(buttonMaximize); }
    bool HasMinimizeButton() const { return HasFlag(buttonMinimize); }
    bool HasPinButton() const { return HasFlag(buttonPin); }
    bool HasGripperTop() const { return HasFlag(optionGripperTop); }

#ifdef SWIG
    %typemap(out) ibAuiPaneInfo& { $result = $self; Py_INCREF($result); }
#endif
    ibAuiPaneInfo& Window(wxWindow* w)
    {
        ibAuiPaneInfo test(*this);
        test.window = w;
        if ( test.IsValid() )
            *this = test;
        return *this;
    }
    ibAuiPaneInfo& Name(const wxString& n) { name = n; return *this; }
    ibAuiPaneInfo& Caption(const wxString& c) { caption = c; return *this; }
    ibAuiPaneInfo& Icon(const wxBitmapBundle& b) { icon = b; return *this; }
    ibAuiPaneInfo& IconMin(const wxBitmapBundle& b) { iconMin = b; return *this; }
    ibAuiPaneInfo& Left() { dock_direction = ibAUI_DOCK_LEFT; return *this; }
    ibAuiPaneInfo& Right() { dock_direction = ibAUI_DOCK_RIGHT; return *this; }
    ibAuiPaneInfo& Top() { dock_direction = ibAUI_DOCK_TOP; return *this; }
    ibAuiPaneInfo& Bottom() { dock_direction = ibAUI_DOCK_BOTTOM; return *this; }
    ibAuiPaneInfo& Center() { dock_direction = ibAUI_DOCK_CENTER; return Layer(0).Row(0).Position(0); }
    ibAuiPaneInfo& Centre() { dock_direction = ibAUI_DOCK_CENTRE; return Layer(0).Row(0).Position(0); }
    ibAuiPaneInfo& Direction(int direction)
    {
        if (dock_direction == ibAUI_DOCK_CENTRE)
            return Centre();

        dock_direction = direction;
        return *this;
    }
    ibAuiPaneInfo& Layer(int layer) { dock_layer = layer; return *this; }
    ibAuiPaneInfo& Row(int row) { dock_row = row; return *this; }
    ibAuiPaneInfo& Position(int pos) { dock_pos = pos; return *this; }
    ibAuiPaneInfo& BestSize(const wxSize& size) { best_size = size; return *this; }
    ibAuiPaneInfo& MinSize(const wxSize& size) { min_size = size; return *this; }
    ibAuiPaneInfo& MaxSize(const wxSize& size) { max_size = size; return *this; }
    ibAuiPaneInfo& BestSize(int x, int y) { best_size.Set(x,y); return *this; }
    ibAuiPaneInfo& MinSize(int x, int y) { min_size.Set(x,y); return *this; }
    ibAuiPaneInfo& MaxSize(int x, int y) { max_size.Set(x,y); return *this; }
    ibAuiPaneInfo& FloatingPosition(const wxPoint& pos) { floating_pos = pos; return *this; }
    ibAuiPaneInfo& FloatingPosition(int x, int y) { floating_pos.x = x; floating_pos.y = y; return *this; }
    ibAuiPaneInfo& FloatingSize(const wxSize& size) { floating_size = size; return *this; }
    ibAuiPaneInfo& FloatingSize(int x, int y) { floating_size.Set(x,y); return *this; }
    ibAuiPaneInfo& FloatingClientSize(const wxSize& size) { floating_client_size = size; return *this; }
    ibAuiPaneInfo& FloatingClientSize(int x, int y) { floating_client_size.Set(x,y); return *this; }
    ibAuiPaneInfo& Fixed() { return SetFlag(optionResizable, false); }
    ibAuiPaneInfo& Resizable(bool resizable = true) { return SetFlag(optionResizable, resizable); }
    ibAuiPaneInfo& Dock() { return SetFlag(optionFloating, false); }
    ibAuiPaneInfo& Float() { return SetFlag(optionFloating, true); }
    ibAuiPaneInfo& Hide() { return SetFlag(optionHidden, true); }
    ibAuiPaneInfo& Show(bool show = true) { return SetFlag(optionHidden, !show).SetFlag(savedClosed, false); }
    ibAuiPaneInfo& CaptionVisible(bool visible = true) { return SetFlag(optionCaption, visible); }
    ibAuiPaneInfo& Maximize() { return SetFlag(optionMaximized, true); }
    ibAuiPaneInfo& Restore() { return SetFlag(optionMaximized, false); }
    ibAuiPaneInfo& PaneBorder(bool visible = true) { return SetFlag(optionPaneBorder, visible); }
    ibAuiPaneInfo& Gripper(bool visible = true) { return SetFlag(optionGripper, visible); }
    ibAuiPaneInfo& GripperTop(bool attop = true) { return SetFlag(optionGripperTop, attop); }
    ibAuiPaneInfo& CloseButton(bool visible = true) { return SetFlag(buttonClose, visible); }
    ibAuiPaneInfo& MaximizeButton(bool visible = true) { return SetFlag(buttonMaximize, visible); }
    ibAuiPaneInfo& MinimizeButton(bool visible = true) { return SetFlag(buttonMinimize, visible); }
    ibAuiPaneInfo& PinButton(bool visible = true) { return SetFlag(buttonPin, visible); }
    ibAuiPaneInfo& DestroyOnClose(bool b = true) { return SetFlag(optionDestroyOnClose, b); }
    ibAuiPaneInfo& TopDockable(bool b = true) { return SetFlag(optionTopDockable, b); }
    ibAuiPaneInfo& BottomDockable(bool b = true) { return SetFlag(optionBottomDockable, b); }
    ibAuiPaneInfo& LeftDockable(bool b = true) { return SetFlag(optionLeftDockable, b); }
    ibAuiPaneInfo& RightDockable(bool b = true) { return SetFlag(optionRightDockable, b); }
    ibAuiPaneInfo& Floatable(bool b = true) { return SetFlag(optionFloatable, b); }
    ibAuiPaneInfo& Movable(bool b = true) { return SetFlag(optionMovable, b); }
    ibAuiPaneInfo& DockFixed(bool b = true) { return SetFlag(optionDockFixed, b); }

    ibAuiPaneInfo& Dockable(bool b = true)
    {
        return TopDockable(b).BottomDockable(b).LeftDockable(b).RightDockable(b);
    }

    ibAuiPaneInfo& DefaultPane()
    {
        ibAuiPaneInfo test(*this);
        test.state |= optionTopDockable | optionBottomDockable |
                 optionLeftDockable | optionRightDockable |
                 optionFloatable | optionMovable | optionResizable |
                 optionCaption | optionPaneBorder | buttonClose;
        if (test.IsValid())
            *this = test;
        return *this;
    }

    ibAuiPaneInfo& CentrePane() { return CenterPane(); }
    ibAuiPaneInfo& CenterPane()
    {
        state = 0;
        return Center().PaneBorder().Resizable();
    }

    // SEAM vs wx: A TOOL BAR IS NOT TORN OFF - it moves along its band and its rows, never into a window of its own
    // (2026-11-11, the tool bar torn off stood alone in a frame: Max, "crooked, and nonsense altogether"); a pane of
    // another kind floats as before
    ibAuiPaneInfo& ToolbarPane()
    {
        DefaultPane();
        state |= (optionToolbar | optionGripper);
        state &= ~(optionResizable | optionCaption | optionFloatable);
        if (dock_layer == 0)
            dock_layer = 10;
        return *this;
    }

    ibAuiPaneInfo& SetFlag(int flag, bool option_state)
    {
        ibAuiPaneInfo test(*this);
        if (option_state)
            test.state |= flag;
        else
            test.state &= ~flag;
        if (test.IsValid())
            *this = test;
        return *this;
    }

    bool HasFlag(int flag) const
    {
        return (state & flag) != 0;
    }

#ifdef SWIG
    %typemap(out) ibAuiPaneInfo& ;
#endif

public:

    // NOTE: You can add and subtract flags from this list,
    // but do not change the values of the flags, because
    // they are stored in a binary integer format in the
    // perspective string.  If you really need to change the
    // values around, you'll have to ensure backwards-compatibility
    // in the perspective loading code.
    enum ibAuiPaneState
    {
        optionFloating        = 1 << 0,
        optionHidden          = 1 << 1,
        optionLeftDockable    = 1 << 2,
        optionRightDockable   = 1 << 3,
        optionTopDockable     = 1 << 4,
        optionBottomDockable  = 1 << 5,
        optionFloatable       = 1 << 6,
        optionMovable         = 1 << 7,
        optionResizable       = 1 << 8,
        optionPaneBorder      = 1 << 9,
        optionCaption         = 1 << 10,
        optionGripper         = 1 << 11,
        optionDestroyOnClose  = 1 << 12,
        optionToolbar         = 1 << 13,
        optionActive          = 1 << 14,
        optionGripperTop      = 1 << 15,
        optionMaximized       = 1 << 16,
        optionDockFixed       = 1 << 17,

        buttonClose           = 1 << 21,
        buttonMaximize        = 1 << 22,
        buttonMinimize        = 1 << 23,
        buttonPin             = 1 << 24,

        buttonCustom1         = 1 << 26,
        buttonCustom2         = 1 << 27,
        buttonCustom3         = 1 << 28,

        savedClosed           = 1 << 29, // used internally
        savedHiddenState      = 1 << 30, // used internally
        actionPane            = 1u << 31  // used internally
    };

public:
    wxString name;        // name of the pane
    wxString caption;     // caption displayed on the window
    wxBitmapBundle icon;  // icon of the pane, may be invalid
    wxBitmapBundle iconMin; // icon used when minimized, may also be invalid

    wxWindow* window;     // window that is in this pane
    wxFrame* frame;       // floating frame window that holds the pane
    unsigned int state;   // a combination of ibPaneState values

    int dock_pos;         // position inside the row (0 = first position)
    int dock_size;        // size of the containing dock (0 if not set)

    wxSize best_size;     // size that the layout engine will prefer
    wxSize min_size;      // minimum size the pane window can tolerate
    wxSize max_size;      // maximum size the pane window can tolerate

    wxPoint floating_pos; // position while floating
    wxSize floating_size; // size while floating
    // this has precedence over floating_size
    wxSize floating_client_size; // client size while floating
    int dock_proportion;  // proportion while docked

    wxRect rect;              // current rectangle (populated by wxAUI)

    // If the pane settings are internally consistent, return true, otherwise
    // return false and trigger an assertion failure in debug builds.
    bool IsValid() const;
};


// Note that this one must remain a wxBaseObjectArray, i.e. store pointers to
// heap-allocated objects, as it is returned by ibAuiManager::GetPane() and the
// existing code expects these pointers to remain valid even if the array is
// modified.
using ibAuiPaneInfoArray = wxBaseObjectArray<ibAuiPaneInfo>;



class DESKGUI_API ibAuiFloatingFrame;

// A button's state (ibAUI_BUTTON_STATE_*) as the theme reads it (wxCONTROL_*): a disabled one is nothing else - a tool of
// a tool bar, a button of a book's row of tabs
int ibAuiGetControlFlags(int state);

// Private helper classes used to drag the panes under Wayland.
class ibAuiPaneDragHandler;
class wxTLWDragSession;

class DESKGUI_API ibAuiManager : public wxEvtHandler
{
    friend class ibAuiFloatingFrame;
    friend class ibAuiPaneDragHandler;

public:

    ibAuiManager(wxWindow* managedWnd = nullptr,
                   unsigned int flags = ibAUI_MGR_DEFAULT);
    virtual ~ibAuiManager();
    void UnInit();

    void SetFlags(unsigned int flags);
    unsigned int GetFlags() const;

    static bool AlwaysUsesLiveResize(const wxWindow* window = nullptr);
    bool HasLiveResize() const;

    void SetManagedWindow(wxWindow* managedWnd);
    wxWindow* GetManagedWindow() const;

    static ibAuiManager* GetManager(wxWindow* window);

    // SEAM vs wx: the docking's sizes, ibAUI_DOCKART_*, at the window's DPI - the art kept them, the theme draws now
    int GetMetricForWindow(int id, wxWindow* window) const;

    // Return the minimum size for any pane.
    wxSize GetMinPaneSize() const;

    // Change the sides where docks for minimized panes can be created.
    // Must include one or more of wxLEFT, wxRIGHT, wxTOP, wxBOTTOM and must
    // currently be called before there any minimized panes.
    void AllowDocksForMinPanes(int directions);

    // Set the style to use for the docks containing minimizing panes.
    // Flags is the combination of ibAUI_MIN_DOCK_* values.
    void SetDocksForMinPanesStyle(unsigned int style);


    ibAuiPaneInfo& GetPane(wxWindow* window);
    ibAuiPaneInfo& GetPane(const wxString& name);
    const ibAuiPaneInfoArray& GetAllPanes() const { return m_panes; }
    ibAuiPaneInfoArray& GetAllPanes() { return m_panes; }

    bool AddPane(wxWindow* window,
                 const ibAuiPaneInfo& paneInfo);

    bool AddPane(wxWindow* window,
                 const ibAuiPaneInfo& paneInfo,
                 const wxPoint& dropPos);

    bool AddPane(wxWindow* window,
                 int direction = wxLEFT,
                 const wxString& caption = wxEmptyString);

    bool InsertPane(wxWindow* window,
                 const ibAuiPaneInfo& insertLocation,
                 int insertLevel = ibAUI_INSERT_PANE);

    bool SplitPane(wxWindow* window,
                   wxWindow* newWindow,
                   int direction,
                   const wxPoint& dropPos = wxDefaultPosition);

    bool DetachPane(wxWindow* window);

    void Update();

    // Serialize or restore the whole layout using the provided serializer.
    void SaveLayout(ibAuiSerializer& serializer) const;
    void LoadLayout(ibAuiDeserializer& deserializer);

    // Older functions using bespoke text format, prefer using the ones using
    // ibAuiSerializer and ibAuiDeserializer above instead in the new code.
    wxString SavePaneInfo(const ibAuiPaneInfo& pane);
    void LoadPaneInfo(wxString panePart, ibAuiPaneInfo &pane);
private:
    bool LoadPaneInfoVersioned(wxString layoutVersion, wxString panePart, ibAuiPaneInfo& pane);
public:
    wxString SavePerspective();
    bool LoadPerspective(const wxString& perspective, bool update = true);

    void SetDockSizeConstraint(double widthPct, double heightPct);
    void GetDockSizeConstraint(double* widthPct, double* heightPct) const;

    void ClosePane(ibAuiPaneInfo& paneInfo);
    void MinimizePane(ibAuiPaneInfo& paneInfo);
    void MaximizePane(ibAuiPaneInfo& paneInfo);
    void RestorePane(ibAuiPaneInfo& paneInfo);
    void RestoreMaximizedPane();

public:

    virtual ibAuiFloatingFrame* CreateFloatingFrame(wxWindow* parent, const ibAuiPaneInfo& p);
    virtual bool CanDockPanel(const ibAuiPaneInfo & p);

    void StartPaneDrag(
                 wxWindow* paneWindow,
                 const wxPoint& offset);

    wxRect CalculateHintRect(
                 wxWindow* paneWindow,
                 const wxPoint& pt,
                 const wxPoint& offset = wxPoint{});

    wxSize CalculateNewSplitSize() const;

    void DrawHintRect(
                 wxWindow* paneWindow,
                 const wxPoint& pt,
                 const wxPoint& offset = wxPoint{});

    void UpdateHint(const wxRect& rect);

    // These functions are public for compatibility reasons, but should never
    // be called directly, use UpdateHint() above instead.
    virtual void ShowHint(const wxRect& rect);
    virtual void HideHint();

    // Internal functions, don't use them outside of wxWidgets itself.
    void CopyDockLayoutFrom(ibAuiDockLayoutInfo& layoutInfo,
                            const ibAuiPaneInfo& pane) const;
    void CopyDockLayoutTo(const ibAuiDockLayoutInfo& layoutInfo,
                          ibAuiPaneInfo& pane) const;

    void CopyLayoutFrom(ibAuiPaneLayoutInfo& layoutInfo,
                        const ibAuiPaneInfo& pane) const;
    void CopyLayoutTo(const ibAuiPaneLayoutInfo& layoutInfo,
                      ibAuiPaneInfo& pane) const;

public:

    // deprecated -- please use SetManagedWindow()
    // and GetManagedWindow() instead

    wxDEPRECATED( void SetFrame(wxFrame* frame) );
    wxDEPRECATED( wxFrame* GetFrame() const );

protected:

    void DoFrameLayout();

    virtual bool CanAddPane(wxWindow* window,
                            const ibAuiPaneInfo& paneInfo) const;

    void LayoutAddPane(wxSizer* container,
                       ibAuiDockInfo& dock,
                       ibAuiPaneInfo& pane,
                       ibAuiDockUIPartArray& uiparts,
                       bool spacerOnly);

    void LayoutAddDock(wxSizer* container,
                       ibAuiDockInfo& dock,
                       ibAuiDockUIPartArray& uiParts,
                       bool spacerOnly);

    wxSizer* LayoutAll(ibAuiPaneInfoArray& panes,
                       ibAuiDockInfoArray& docks,
                          ibAuiDockUIPartArray & uiParts,
                       bool spacerOnly = false);

    virtual bool ProcessDockResult(ibAuiPaneInfo& target,
                                   const ibAuiPaneInfo& newPos);

    bool DoDrop(ibAuiDockInfoArray& docks,
                ibAuiPaneInfoArray& panes,
                ibAuiPaneInfo& drop,
                const wxPoint& pt,
                const wxPoint& actionOffset = wxPoint(0,0));

    ibAuiDockUIPart* HitTest(int x, int y);
    ibAuiDockUIPart* GetPanePart(wxWindow* pane);
    int GetDockPixelOffset(ibAuiPaneInfo& test);
    void OnFloatingPaneMoveStart(wxWindow* window);
    void OnFloatingPaneMoving(wxWindow* window, wxDirection dir );
    void OnFloatingPaneMoved(wxWindow* window, wxDirection dir);
    void OnFloatingPaneActivated(wxWindow* window);
    void OnFloatingPaneClosed(wxWindow* window, wxCloseEvent& evt);
    void OnFloatingPaneResized(wxWindow* window, const wxRect& rect);

    // Common parts of OnFloatingPaneMoving() and OnFloatingPaneMoved() and of
    // the drag session handlers below: "pt" is the current pointer position in
    // screen coordinates and "offset" the position of the pointer inside the
    // frame being dragged.
    void DoMovePane(ibAuiPaneInfo& pane, const wxPoint& pt, const wxPoint& offset);
    void DoDropPane(ibAuiPaneInfo& pane, const wxPoint& pt, const wxPoint& offset);
    void DoEndMovePane(ibAuiPaneInfo& pane);

    // Ensure that the panes of the dock containing the given pane use
    // sequential positions after finishing dragging a toolbar pane.
    void SaveDockPositions(const ibAuiPaneInfo& pane);

    // Try to start dragging the given pane using the system drag support,
    // "origin" is the window in which the mouse is currently captured.
    void StartDragSession(ibAuiPaneInfo& pane, wxWindow* origin);

    // By default the origin window is the managed window itself.
    void StartDragSession(ibAuiPaneInfo& pane)
    {
        StartDragSession(pane, m_frame);
    }

    // Handlers for the drag session events, used under Wayland only, see
    // ibAuiPaneDragHandler. Here "win" is the TLW under the pointer or null.
    void OnPaneDragMove(wxWindow* paneWindow,
                        wxWindow* win,
                        const wxPoint& pt,
                        const wxPoint& offset);
    void OnPaneDragDrop(wxWindow* paneWindow,
                        wxWindow* win,
                        const wxPoint& pt,
                        const wxPoint& offset);
    void OnPaneDragEnd(wxWindow* paneWindow);

    void Render(wxDC* dc);
    void Repaint(wxDC* dc = nullptr);
    void ProcessMgrEvent(ibAuiManagerEvent& event);
    void UpdateButtonOnScreen(ibAuiDockUIPart* buttonUiPart,
                              int state = ibAUI_BUTTON_STATE_NORMAL);
    void GetPanePositionsAndSizes(ibAuiDockInfo& dock,
                              wxArrayInt& positions,
                              wxArrayInt& sizes);

    /// Ends a resize action, or for live update, resizes the sash
    bool DoEndResizeAction(wxMouseEvent& event);

    void SetActivePane(wxWindow* active_pane);

public:

    // public events (which can be invoked externally)
    void OnRender(ibAuiManagerEvent& evt);
    void OnPaneButton(ibAuiManagerEvent& evt);

protected:

    // protected events
    void OnDestroy(wxWindowDestroyEvent& evt);
    void OnPaint(wxPaintEvent& evt);
    void OnEraseBackground(wxEraseEvent& evt);
    void OnSize(wxSizeEvent& evt);
    void OnSetCursor(wxSetCursorEvent& evt);
    void OnLeftDown(wxMouseEvent& evt);
    void OnLeftUp(wxMouseEvent& evt);
    void OnMotion(wxMouseEvent& evt);
    void OnCaptureLost(wxMouseCaptureLostEvent& evt);
    void OnLeaveWindow(wxMouseEvent& evt);
    void OnChildFocus(wxChildFocusEvent& evt);
    void OnHintFadeTimer(wxTimerEvent& evt);
    void OnFindManager(ibAuiManagerEvent& evt);
    void OnSysColourChanged(wxSysColourChangedEvent& event);
#ifndef wxHAS_DPI_INDEPENDENT_PIXELS
    void OnDPIChanged(wxDPIChangedEvent& event);
#endif // !wxHAS_DPI_INDEPENDENT_PIXELS

protected:

    enum
    {
        actionNone = 0,
        actionResize,
        actionClickButton,
        actionClickCaption,
        actionDragToolbarPane,
        actionDragFloatingPane
    };

protected:

    wxWindow* m_frame;           // the window being managed
    unsigned int m_flags;        // manager flags ibAUI_MGR_*

    ibAuiPaneInfoArray m_panes;     // array of panes structures
    ibAuiDockInfoArray m_docks;     // array of docks structures
    ibAuiDockUIPartArray m_uiParts; // array of UI parts (captions, buttons, etc)

    int m_action;                // current mouse action
    wxPoint m_actionStart;      // position where the action click started
    wxPoint m_actionOffset;     // offset from upper left of the item clicked
    ibAuiDockUIPart* m_actionPart; // ptr to the part the action happened to
    wxWindow* m_actionWindow;   // action frame or window (nullptr if none)
    wxRect m_actionHintRect;    // hint rectangle for the action
    wxRect m_lastRect;
    ibAuiDockUIPart* m_hoverButton;// button uipart being hovered over
    wxRect m_lastHint;          // last hint rectangle
    wxPoint m_lastMouseMove;   // last mouse move position (see OnMotion)
    int  m_currentDragItem;
    bool m_hasMaximized;

    double m_dockConstraintX;  // 0.0 .. 1.0; max pct of window width a dock can consume
    double m_dockConstraintY;  // 0.0 .. 1.0; max pct of window height a dock can consume

    wxTimer m_hintFadeTimer;    // transparent fade timer
    wxByte m_hintFadeAmt;       // transparent fade amount
    wxByte m_hintFadeMax;       // maximum value of hint fade

    wxOverlay m_overlay;

    void* m_reserved;

private:
    // Return the index in m_uiParts corresponding to the current value of
    // m_actionPart. If m_actionPart is null, returns wxNOT_FOUND.
    int GetActionPartIndex() const;

    // Get direction to use for minimizing the given pane docking direction.
    //
    // The returned value is one of 4 ibAUI_DOCK_{TOP,RIGHT,BOTTOM,LEFT} values
    // but may be ibAUI_DOCK_NONE if paneDirection is ibAUI_DOCK_CENTER or
    // invalid.
    ibAuiManagerDock GetMinDockDirectionFor(int paneDirection) const;

    // Get reference to the minimized dock for the given direction, which must
    // be valid, i.e. not ibAUI_DOCK_NONE and not ibAUI_DOCK_CENTER.
    ibAuiMinDock*& GetMinDockInDirection(ibAuiManagerDock direction);

    // If the pane can be minimized and if the docking toolbar into which it
    // would minimize is shown, add it to it.
    void AddPaneToMinDockIfNecessary(ibAuiPaneInfo& paneInfo);

    // Remove the pane from the docking toolbar in the given direction if it is
    // shown and remove the toolbar itself if it becomes empty.
    void
    RemovePaneFromMinDockIfNecessary(ibAuiManagerDock direction,
                                     ibAuiPaneInfo& paneInfo);


    // Common part of ClosePane() and MinimizePane(): hide the pane window.
    void DoHidePaneWindow(ibAuiPaneInfo& paneInfo);

    // Reparent the pane window back to the managed window and destroy the
    // floating frame containing it, which must be non-null.
    void DestroyFloatingFrame(ibAuiPaneInfo& paneInfo);


    // This flag is set to true if Update() is called while the window is
    // minimized, in which case we postpone updating it until it is restored.
    bool m_updateOnRestore = false;

    // Number of (possibly nested) calls to DoFrameLayout() currently executing.
    int m_frameLayoutDepth = 0;

    // Toolbars used to show minimized panes. Some, or all, of them can be null.
    //
    // This is indexed by ibAUI_DOCK_TOP, ibAUI_DOCK_BOTTOM, ibAUI_DOCK_RIGHT
    // and ibAUI_DOCK_LEFT with offset of -1, so don't access it directly and
    // use GetMinDockInDirection() instead to not have to remember this -1.
    ibAuiMinDock* m_minDocks[ibAUI_DOCK_LEFT] = { nullptr };

    // Mask of directions where we are allowed to create docks for minimized
    // panes.
    int m_minDockAllowed = wxLEFT | wxRIGHT | wxBOTTOM;

    // Style flags to use for the docks containing minimized panes.
    unsigned int m_minDockStyle = ibAUI_MIN_DOCK_DEFAULT;

    // Non-null only while dragging a pane using the system drag machinery,
    // which is currently only done under Wayland, where we can't move the
    // floating frame ourselves.
    std::unique_ptr<wxTLWDragSession> m_dragSession;

#ifndef SWIG
    wxDECLARE_CLASS(ibAuiManager);
#endif // SWIG
};



// event declarations/classes

class DESKGUI_API ibAuiManagerEvent : public wxEvent
{
public:
    ibAuiManagerEvent(wxEventType type=wxEVT_NULL) : wxEvent(0, type)
    {
        manager = nullptr;
        pane = nullptr;
        button = 0;
        veto_flag = false;
        canveto_flag = true;
        dc = nullptr;
    }
    wxNODISCARD wxEvent *Clone() const override { return new ibAuiManagerEvent(*this); }

    void SetManager(ibAuiManager* mgr) { manager = mgr; }
    void SetPane(ibAuiPaneInfo* p) { pane = p; }
    void SetButton(int b) { button = b; }
    void SetDC(wxDC* pdc) { dc = pdc; }

    ibAuiManager* GetManager() const { return manager; }
    ibAuiPaneInfo* GetPane() const { return pane; }
    int GetButton() const { return button; }
    wxDC* GetDC() const { return dc; }

    void Veto(bool veto = true) { veto_flag = veto; }
    bool GetVeto() const { return veto_flag; }
    void SetCanVeto(bool can_veto) { canveto_flag = can_veto; }
    bool CanVeto() const { return  canveto_flag && veto_flag; }

public:
    ibAuiManager* manager;
    ibAuiPaneInfo* pane;
    int button;
    bool veto_flag;
    bool canveto_flag;
    wxDC* dc;

#ifndef SWIG
private:
    wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN_DEF_COPY(ibAuiManagerEvent);
#endif
};


class DESKGUI_API ibAuiDockInfo : public ibAuiDockCoords
{
public:
    ibAuiDockInfo()
    {
        size = 0;
        min_size = 0;
        resizable = true;
        fixed = false;
        toolbar = false;
        reserved1 = false;
    }

    bool IsOk() const { return dock_direction != 0; }

public:
    ibAuiPaneInfoPtrArray panes; // array of panes
    wxRect rect;              // current rectangle
    int size;                 // size of the dock
    int min_size;             // minimum size of a dock (0 if there is no min)
    bool resizable;           // flag indicating whether the dock is resizable
    bool toolbar;             // flag indicating dock contains only toolbars
    bool fixed;               // flag indicating that the dock operates on
                              // absolute coordinates as opposed to proportional
    bool reserved1;
};


class DESKGUI_API ibAuiDockUIPart
{
public:
    enum
    {
        typeCaption,
        typeGripper,
        typeDock,
        typeDockSizer,
        typePane,
        typePaneSizer,
        typeBackground,
        typePaneBorder,
        typePaneButton
    };

    int type;                // ui part type (see enum above)
    int state = ibAUI_BUTTON_STATE_NORMAL; // only used if type == typePaneButton
    int orientation;         // orientation (either wxHORIZONTAL or wxVERTICAL)
    ibAuiDockInfo* dock;        // which dock the item is associated with
    ibAuiPaneInfo* pane;        // which pane the item is associated with
    int button;              // which pane button the item is associated with
    wxSizer* cont_sizer;     // the part's containing sizer
    wxSizerItem* sizer_item; // the sizer item of the part
    wxRect rect;             // client coord rectangle of the part itself
};




#ifndef SWIG

wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_BUTTON, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_CLOSE, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_MINIMIZE, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_MAXIMIZE, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_RESTORE, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_PANE_ACTIVATED, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_RENDER, ibAuiManagerEvent );
wxDECLARE_EXPORTED_EVENT( DESKGUI_API, ibEVT_AUI_FIND_MANAGER, ibAuiManagerEvent );

typedef void (wxEvtHandler::*ibAuiManagerEventFunction)(ibAuiManagerEvent&);

#define ibAuiManagerEventHandler(func) \
    wxEVENT_HANDLER_CAST(ibAuiManagerEventFunction, func)

#define EVT_AUI_PANE_BUTTON(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_BUTTON, ibAuiManagerEventHandler(func))
#define EVT_AUI_PANE_CLOSE(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_CLOSE, ibAuiManagerEventHandler(func))
#define EVT_AUI_PANE_MINIMIZE(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_MINIMIZE, ibAuiManagerEventHandler(func))
#define EVT_AUI_PANE_MAXIMIZE(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_MAXIMIZE, ibAuiManagerEventHandler(func))
#define EVT_AUI_PANE_RESTORE(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_RESTORE, ibAuiManagerEventHandler(func))
#define EVT_AUI_PANE_ACTIVATED(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_PANE_ACTIVATED, ibAuiManagerEventHandler(func))
#define EVT_AUI_RENDER(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_RENDER, ibAuiManagerEventHandler(func))
#define EVT_AUI_FIND_MANAGER(func) \
   wx__DECLARE_EVT0(ibEVT_AUI_FIND_MANAGER, ibAuiManagerEventHandler(func))

#else

%constant wxEventType ibEVT_AUI_PANE_BUTTON;
%constant wxEventType ibEVT_AUI_PANE_CLOSE;
%constant wxEventType ibEVT_AUI_PANE_MINIMIZE;
%constant wxEventType ibEVT_AUI_PANE_MAXIMIZE;
%constant wxEventType ibEVT_AUI_PANE_RESTORE;
%constant wxEventType ibEVT_AUI_PANE_ACTIVATED;
%constant wxEventType ibEVT_AUI_RENDER;
%constant wxEventType ibEVT_AUI_FIND_MANAGER;

%pythoncode {
    EVT_AUI_PANE_BUTTON = wx.PyEventBinder( ibEVT_AUI_PANE_BUTTON )
    EVT_AUI_PANE_CLOSE = wx.PyEventBinder( ibEVT_AUI_PANE_CLOSE )
    EVT_AUI_PANE_MINIMIZE = wx.PyEventBinder( ibEVT_AUI_PANE_MINIMIZE )
    EVT_AUI_PANE_MAXIMIZE = wx.PyEventBinder( ibEVT_AUI_PANE_MAXIMIZE )
    EVT_AUI_PANE_RESTORE = wx.PyEventBinder( ibEVT_AUI_PANE_RESTORE )
    EVT_AUI_PANE_ACTIVATED = wx.PyEventBinder( ibEVT_AUI_PANE_ACTIVATED )
    EVT_AUI_RENDER = wx.PyEventBinder( ibEVT_AUI_RENDER )
    EVT_AUI_FIND_MANAGER = wx.PyEventBinder( ibEVT_AUI_FIND_MANAGER )
}
#endif // SWIG

#endif //_DESKGUI_UNIV_AUI_FRAMEMANAGER_H_

