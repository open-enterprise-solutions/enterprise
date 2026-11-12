// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to the demo form (docs/private/deskgui.md,
// the porting workflow).

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/renderer.h
// Purpose:     ibRenderer class declaration
// Author:      Vadim Zeitlin
// Created:     06.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_RENDERER_H_
#define _DESKGUI_UNIV_RENDERER_H_

#include "deskgui/deskgui.h"

/*
   ibRenderer class is used to draw all wxWidgets controls. This is an ABC and
   the look of the application is determined by the concrete derivation of
   ibRenderer used in the program.

   It also contains a few static methods which may be used by the concrete
   renderers and provide the functionality which is often similar or identical
   in all renderers (using inheritance here would be more restrictive as the
   given concrete renderer may need an arbitrary subset of the base class
   methods).

   Finally note that ibRenderer supersedes ibRendererNative in wxUniv build and
   includes the latter's functionality (which it may delegate to the generic
   implementation of the latter or reimplement itself).
 */

#include <wx/renderer.h>

class ibWindow;
class wxDC;

#include <wx/string.h>
#include <wx/gdicmn.h>
#include <wx/icon.h>

// ----------------------------------------------------------------------------
// constants
// ----------------------------------------------------------------------------

// frame decorations type flags used in ibRenderer and ibColourScheme
enum
{
    wxTOPLEVEL_ACTIVE          = 0x00000001,
    wxTOPLEVEL_MAXIMIZED       = 0x00000002,
    wxTOPLEVEL_TITLEBAR        = 0x00000004,
    wxTOPLEVEL_ICON            = 0x00000008,
    wxTOPLEVEL_RESIZEABLE      = 0x00000010,
    wxTOPLEVEL_BORDER          = 0x00000020,
    wxTOPLEVEL_BUTTON_CLOSE    = 0x01000000,
    wxTOPLEVEL_BUTTON_MAXIMIZE = 0x02000000,
    wxTOPLEVEL_BUTTON_ICONIZE =  0x04000000,
    wxTOPLEVEL_BUTTON_RESTORE  = 0x08000000,
    wxTOPLEVEL_BUTTON_HELP     = 0x10000000
};

// frame hit test return values:
enum
{
    wxHT_TOPLEVEL_NOWHERE         = 0x00000000,
    wxHT_TOPLEVEL_CLIENT_AREA     = 0x00000001,
    wxHT_TOPLEVEL_ICON            = 0x00000002,
    wxHT_TOPLEVEL_TITLEBAR        = 0x00000004,

    wxHT_TOPLEVEL_BORDER_N        = 0x00000010,
    wxHT_TOPLEVEL_BORDER_S        = 0x00000020,
    wxHT_TOPLEVEL_BORDER_E        = 0x00000040,
    wxHT_TOPLEVEL_BORDER_W        = 0x00000080,
    wxHT_TOPLEVEL_BORDER_NE       = wxHT_TOPLEVEL_BORDER_N | wxHT_TOPLEVEL_BORDER_E,
    wxHT_TOPLEVEL_BORDER_SE       = wxHT_TOPLEVEL_BORDER_S | wxHT_TOPLEVEL_BORDER_E,
    wxHT_TOPLEVEL_BORDER_NW       = wxHT_TOPLEVEL_BORDER_N | wxHT_TOPLEVEL_BORDER_W,
    wxHT_TOPLEVEL_BORDER_SW       = wxHT_TOPLEVEL_BORDER_S | wxHT_TOPLEVEL_BORDER_W,
    wxHT_TOPLEVEL_ANY_BORDER      = 0x000000F0,

    wxHT_TOPLEVEL_BUTTON_CLOSE    = /*0x01000000*/ wxTOPLEVEL_BUTTON_CLOSE,
    wxHT_TOPLEVEL_BUTTON_MAXIMIZE = /*0x02000000*/ wxTOPLEVEL_BUTTON_MAXIMIZE,
    wxHT_TOPLEVEL_BUTTON_ICONIZE =  /*0x04000000*/ wxTOPLEVEL_BUTTON_ICONIZE,
    wxHT_TOPLEVEL_BUTTON_RESTORE  = /*0x08000000*/ wxTOPLEVEL_BUTTON_RESTORE,
    wxHT_TOPLEVEL_BUTTON_HELP     = /*0x10000000*/ wxTOPLEVEL_BUTTON_HELP,
    wxHT_TOPLEVEL_ANY_BUTTON      =   0x1F000000
};

#if wxUSE_MENUS

// ----------------------------------------------------------------------------
// ibMenuGeometryInfo: the layout of a menu's items, aligned for all of them
// ----------------------------------------------------------------------------

// SEAM vs univ: the menu lays itself out (ibMenu::GetGeometryInfo) from the theme's metrics (GetMenuMargin and the
// rest below) and the theme draws its items by it; the canon's theme laid out the menu itself (GetMenuGeometry),
// reading its items and setting their places, and returned its own subclass of this
class DESKGUI_API ibMenuGeometryInfo
{
public:
    // get the total size of the menu
    wxSize GetSize() const { return m_size; }

    // get the offset of the start of the items' labels and of their accel strings
    wxCoord GetLabelOffset() const { return m_ofsLabel; }
    wxCoord GetAccelOffset() const { return m_ofsAccel; }

    // get the height of a normal (not separator) item
    wxCoord GetItemHeight() const { return m_heightItem; }

private:
    // the total size of the menu
    wxSize m_size;

    // the offset of the start of the menu item label
    wxCoord m_ofsLabel = 0;

    // the offset of the start of the accel label
    wxCoord m_ofsAccel = 0;

    // the height of a normal (not separator) item
    wxCoord m_heightItem = 0;

    friend class ibMenu;
};

#endif // wxUSE_MENUS

// ----------------------------------------------------------------------------
// ibRenderer: abstract renderers interface
// ----------------------------------------------------------------------------

class DESKGUI_API ibRenderer : public wxDelegateRendererNative
{
public:
    // drawing functions
    // -----------------

    // draw the controls background
    virtual void DrawBackground(wxDC& dc,
                                const wxColour& col,
                                const wxRect& rect,
                                int flags,
                                ibWindow *window = nullptr) = 0;

    // draw the button surface
    virtual void DrawButtonSurface(wxDC& dc,
                                   const wxColour& col,
                                   const wxRect& rect,
                                   int flags) = 0;

    // draw the label inside the given rectangle with the specified alignment
    // and optionally emphasize the character with the given index
    virtual void DrawLabel(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int alignment = wxALIGN_LEFT | wxALIGN_TOP,
                           int indexAccel = -1,
                           wxRect *rectBounds = nullptr) = 0;

    // same but also draw a bitmap if it is valid
    virtual void DrawButtonLabel(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& image,
                                 const wxRect& rect,
                                 int flags = 0,
                                 int alignment = wxALIGN_LEFT | wxALIGN_TOP,
                                 int indexAccel = -1,
                                 wxRect *rectBounds = nullptr) = 0;


    // draw the border and optionally return the rectangle containing the
    // region inside the border
    virtual void DrawBorder(wxDC& dc,
                            wxBorder border,
                            const wxRect& rect,
                            int flags = 0,
                            wxRect *rectIn = nullptr) = 0;

    // draw text control border (I hate to have a separate method for this but
    // it is needed to accommodate GTK+)
    virtual void DrawTextBorder(wxDC& dc,
                                wxBorder border,
                                const wxRect& rect,
                                int flags = 0,
                                wxRect *rectIn = nullptr) = 0;

    // draw push button border and return the rectangle left for the label
    virtual void DrawButtonBorder(wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxRect *rectIn = nullptr) = 0;

    // draw a horizontal line
    virtual void DrawHorizontalLine(wxDC& dc,
                                    wxCoord y, wxCoord x1, wxCoord x2) = 0;

    // draw a vertical line
    virtual void DrawVerticalLine(wxDC& dc,
                                  wxCoord x, wxCoord y1, wxCoord y2) = 0;

    // draw a frame with the label (horizontal alignment can be specified)
    virtual void DrawFrame(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int alignment = wxALIGN_LEFT,
                           int indexAccel = -1) = 0;

    // draw an arrow in the given direction
    virtual void DrawArrow(wxDC& dc,
                           wxDirection dir,
                           const wxRect& rect,
                           int flags = 0) = 0;

    // draw a scrollbar arrow (may be the same as arrow but may be not)
    virtual void DrawScrollbarArrow(wxDC& dc,
                                    wxDirection dir,
                                    const wxRect& rect,
                                    int flags = 0) = 0;

    // draw the scrollbar thumb
    virtual void DrawScrollbarThumb(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) = 0;

    // draw a (part of) scrollbar shaft
    virtual void DrawScrollbarShaft(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) = 0;

    // draw the rectangle in the corner between two scrollbars
    virtual void DrawScrollCorner(wxDC& dc,
                                  const wxRect& rect) = 0;

    // draw an item of a ibListBox
    virtual void DrawItem(wxDC& dc,
                          const wxString& label,
                          const wxRect& rect,
                          int flags = 0) = 0;

    // draw an item of a ibCheckListBox
    virtual void DrawCheckItem(wxDC& dc,
                               const wxString& label,
                               const wxBitmap& bitmap,
                               const wxRect& rect,
                               int flags = 0) = 0;

    // draw a checkbutton (bitmap may be invalid to use default one)
    virtual void DrawCheckButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) = 0;

    // draw a radio button
    virtual void DrawRadioButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) = 0;

#if wxUSE_TOOLBAR
    // draw a tool bar's background: col is the surface the bar stands on
    virtual void DrawToolBarBackground(wxDC& dc,
                                       const wxColour& col,
                                       const wxRect& rect,
                                       int flags = 0) = 0;

    // draw a toolbar button (label may be empty, bitmap may be invalid, if
    // both conditions are true this function draws a separator)
    virtual void DrawToolBarButton(wxDC& dc,
                                   const wxString& label,
                                   const wxBitmap& bitmap,
                                   const wxRect& rect,
                                   int flags = 0,
                                   long style = 0,
                                   int tbarStyle = 0) = 0;

    // draw the handle a tool bar is dragged by: rect is the handle's, orient
    // the bar's - a horizontal bar's handle stands upright at its start
    virtual void DrawGripper(wxDC& dc,
                             const wxRect& rect,
                             wxOrientation orient) = 0;
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    // draw a (part of) line in the text control
    virtual void DrawTextLine(wxDC& dc,
                              const wxString& text,
                              const wxRect& rect,
                              int selStart = -1,
                              int selEnd = -1,
                              int flags = 0) = 0;

    // draw a line wrap indicator
    virtual void DrawLineWrapMark(wxDC& dc, const wxRect& rect) = 0;
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    // draw a notebook tab
    virtual void DrawTab(wxDC& dc,
                         const wxRect& rect,
                         wxDirection dir,
                         const wxString& label,
                         const wxBitmap& bitmap = wxNullBitmap,
                         int flags = 0,
                         int indexAccel = -1) = 0;

    // an AUI book's row of tabs (ibAuiTabContainer), dir the side of the pages it stands on (wxBOTTOM - the row under
    // them): the strip under the tabs
    virtual void DrawTabStrip(wxDC& dc,
                              const wxRect& rect,
                              wxDirection dir) = 0;

    // a tab of that row, clipped to it by the row: the face of the selected one (wxCONTROL_SELECTED) - its picture, its
    // label and its button are the row's
    virtual void DrawStripTab(wxDC& dc,
                              const wxRect& rect,
                              wxDirection dir,
                              int flags = 0) = 0;

    // a button of that row or of a tab in it, button its ibAuiButtonId (close, left, right, the list of the tabs): the
    // glyph in rect; wxCONTROL_FLAT - a tab's, faint until the mouse is over it
    virtual void DrawStripButton(wxDC& dc,
                                 const wxRect& rect,
                                 int button,
                                 int flags = 0) = 0;
#endif // wxUSE_NOTEBOOK

    // THE DOCKING'S, NOT wx's AUI: the fork is the library's own (univ/aui), built whatever wx's switch says (wxUSE_AUI
    // is off in the CMake build - wx makes no wxaui library)
    // a pane's caption in the docking (ibAuiManager), drawn in pieces round its buttons: the face - the active pane's,
    // wxCONTROL_FOCUSED - and, given them, the icon and the title, the title cut to the piece
    virtual void DrawPaneCaption(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& title,
                                 const wxBitmap& icon,
                                 int flags = 0) = 0;

    // a button of that caption, button its ibAuiButtonId (close, pin, maximize - wxCONTROL_CHECKED: restore): the glyph,
    // on a pad while the mouse is over it or presses it
    virtual void DrawPaneButton(wxDC& dc,
                                const wxRect& rect,
                                int button,
                                int flags = 0) = 0;

    // the line round a pane in the docking
    virtual void DrawPaneBorder(wxDC& dc,
                                const wxRect& rect) = 0;

    // the bar between two docks or two panes in the docking, orient the bar's
    virtual void DrawSash(wxDC& dc,
                          const wxRect& rect,
                          wxOrientation orient) = 0;

#if wxUSE_SLIDER

    // draw the slider shaft
    virtual void DrawSliderShaft(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0,
                                 wxRect *rectShaft = nullptr) = 0;

    // draw the slider thumb
    virtual void DrawSliderThumb(wxDC& dc,
                                 const wxRect& rect,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0) = 0;

    // draw the slider ticks
    virtual void DrawSliderTicks(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int start,
                                 int end,
                                 int step = 1,
                                 int flags = 0,
                                 long style = 0) = 0;
#endif // wxUSE_SLIDER

#if wxUSE_MENUS
    // draw a menu bar item
    virtual void DrawMenuBarItem(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0,
                                 int indexAccel = -1) = 0;

    // draw a menu item (also used for submenus if flags has ISSUBMENU flag)
    //
    // the geometryInfo is the menu's (ibMenu::GetGeometryInfo), laid out by the metrics from below
    virtual void DrawMenuItem(wxDC& dc,
                              wxCoord y,
                              const ibMenuGeometryInfo& geometryInfo,
                              const wxString& label,
                              const wxString& accel,
                              const wxBitmap& bitmap = wxNullBitmap,
                              int flags = 0,
                              int indexAccel = -1) = 0;

    // draw a menu bar separator
    virtual void DrawMenuSeparator(wxDC& dc,
                                   wxCoord y,
                                   const ibMenuGeometryInfo& geomInfo) = 0;
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    // draw a status bar field: wxCONTROL_ISDEFAULT bit in the flags is
    // interpreted specially and means "draw the status bar grip" here
    virtual void DrawStatusField(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0, int style = 0) = 0;
#endif // wxUSE_STATUSBAR

    // draw a tooltip - its plate, its frame and its text - over the whole of the tooltip's window (ibToolTipWindow)
    virtual void DrawToolTip(wxDC& dc, const wxRect& rect, const wxString& text) = 0;

    // draw complete frame/dialog titlebar
    virtual void DrawFrameTitleBar(wxDC& dc,
                                   const wxRect& rect,
                                   const wxString& title,
                                   const wxIcon& icon,
                                   int flags,
                                   int specialButton = 0,
                                   int specialButtonFlags = 0) = 0;

    // draw frame borders
    virtual void DrawFrameBorder(wxDC& dc,
                                 const wxRect& rect,
                                 int flags) = 0;

    // draw frame titlebar background
    virtual void DrawFrameBackground(wxDC& dc,
                                     const wxRect& rect,
                                     int flags) = 0;

    // draw frame title
    virtual void DrawFrameTitle(wxDC& dc,
                                const wxRect& rect,
                                const wxString& title,
                                int flags) = 0;

    // draw frame icon
    virtual void DrawFrameIcon(wxDC& dc,
                               const wxRect& rect,
                               const wxIcon& icon,
                               int flags) = 0;

    // draw frame buttons
    virtual void DrawFrameButton(wxDC& dc,
                                 wxCoord x, wxCoord y,
                                 int button,
                                 int flags = 0) = 0;

    // misc functions
    // --------------

#if wxUSE_COMBOBOX
    // return the bitmaps to use for combobox button
    virtual void GetComboBitmaps(wxBitmap *bmpNormal,
                                 wxBitmap *bmpFocus,
                                 wxBitmap *bmpPressed,
                                 wxBitmap *bmpDisabled) = 0;
#endif // wxUSE_COMBOBOX

    // geometry functions
    // ------------------

    // get the dimensions of the border: rect.x/y contain the width/height of
    // the left/top side, width/height - of the right/bottom one
    virtual wxRect GetBorderDimensions(wxBorder border) const = 0;

    // the scrollbars may be drawn either inside the window border or outside
    // it - this function is used to decide how to draw them
    virtual bool AreScrollbarsInsideBorder() const = 0;

    // adjust the size of the control of the given class: for most controls,
    // this just takes into account the border, but for some (buttons, for
    // example) it is more complicated - the result being, in any case, that
    // the control looks "nice" if it uses the adjusted rectangle
    virtual void AdjustSize(wxSize *size, const ibWindow *window) = 0;

#if wxUSE_SCROLLBAR
    // get the size of a scrollbar arrow
    virtual wxSize GetScrollbarArrowSize() const = 0;
#endif // wxUSE_SCROLLBAR

    // get the height of a listbox item from the base font height
    virtual wxCoord GetListboxItemHeight(wxCoord fontHeight) = 0;

    // get the size of a checkbox/radio button bitmap
    virtual wxSize GetCheckBitmapSize() const = 0;
    virtual wxSize GetRadioBitmapSize() const = 0;
    virtual wxCoord GetCheckItemMargin() const = 0;

#if wxUSE_TOOLBAR
    // get the standard size of a toolbar button and also return the size of
    // a toolbar separator in the provided pointer
    virtual wxSize GetToolBarButtonSize(wxCoord *separator) const = 0;

    // get the margins between/around the toolbar buttons
    virtual wxSize GetToolBarMargin() const = 0;
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    // convert between text rectangle and client rectangle for text controls:
    // the former is typically smaller to leave margins around text
    virtual wxRect GetTextTotalArea(const wxRect& rectText) const = 0;

    // extra space is for line indicators
    virtual wxRect GetTextClientArea(const wxRect& rectTotal,
                                     wxCoord *extraSpaceBeyond) const = 0;
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    // get the overhang of a selected tab
    virtual wxSize GetTabIndent() const = 0;

    // get the padding around the text in a tab
    virtual wxSize GetTabPadding() const = 0;
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    // get the default size of the slider in lesser dimension (i.e. height of a
    // horizontal slider or width of a vertical one)
    virtual wxCoord GetSliderDim() const = 0;

    // get the length of the slider ticks displayed along side slider
    virtual wxCoord GetSliderTickLen() const = 0;

    // get the slider shaft rect from the total slider rect
    virtual wxRect GetSliderShaftRect(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient,
                                      long style = 0) const = 0;

    // get the size of the slider thumb for the given total slider rect
    virtual wxSize GetSliderThumbSize(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient) const = 0;
#endif // wxUSE_SLIDER

    // get the size of one progress bar step (in horz and vertical directions)
    virtual wxSize GetProgressBarStep() const = 0;

#if wxUSE_MENUS
    // get the size of rectangle to use in the menubar for the given text rect
    virtual wxSize GetMenuBarItemSize(const wxSize& sizeText) const = 0;

    // the metrics a menu lays its items out with (ibMenu::GetGeometryInfo):

    // get the empty strip round the items, inside the menu's frame
    virtual wxSize GetMenuMargin() const = 0;

    // get the height of an item for the given height of its text, and of a separator
    virtual wxCoord GetMenuItemHeight(wxCoord heightText) const = 0;
    virtual wxCoord GetMenuSeparatorHeight() const = 0;

    // get the width of the column of the items' pictures (and check marks) for the widest picture
    virtual wxCoord GetMenuBitmapWidth(wxCoord widthBitmap) const = 0;

    // get the gap between the labels and the accel strings, if there are any
    virtual wxCoord GetMenuAccelMargin() const = 0;

    // get the width of the column of the submenu indicators
    virtual wxCoord GetMenuSubmenuWidth() const = 0;
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    // get the borders around the status bar fields (x and y fields of the
    // return value)
    virtual wxSize GetStatusBarBorders() const = 0;

    // get the border between the status bar fields
    virtual wxCoord GetStatusBarBorderBetweenFields() const = 0;

    // get the margin between a field and its border
    virtual wxSize GetStatusBarFieldMargins() const = 0;
#endif // wxUSE_STATUSBAR

    // get client area rectangle of top level window (i.e. subtract
    // decorations from given rectangle)
    virtual wxRect GetFrameClientArea(const wxRect& rect, int flags) const = 0;

    // get size of whole top level window, given size of its client area size
    virtual wxSize GetFrameTotalSize(const wxSize& clientSize, int flags) const = 0;

    // get the minimal size of top level window
    virtual wxSize GetFrameMinSize(int flags) const = 0;

    // get titlebar icon size
    virtual wxSize GetFrameIconSize() const = 0;

    // returns one of wxHT_TOPLEVEL_XXX constants
    virtual int HitTestFrame(const wxRect& rect,
                             const wxPoint& pt,
                             int flags = 0) const = 0;

    // virtual dtor for any base class
    virtual ~ibRenderer();

    // a bitmap in rect: aligned, tiled or stretched over it (a window's background bitmap, a static bitmap's)
    static void DrawBitmap(wxDC &dc,
                           const wxBitmap& bitmap,
                           const wxRect& rect,
                           int alignment = wxALIGN_CENTRE |
                                           wxALIGN_CENTRE_VERTICAL,
                           wxStretch stretch = wxSTRETCH_NOT);
};

// ----------------------------------------------------------------------------
// ibDelegateRenderer: it is impossible to inherit from any of standard
// renderers as their declarations are in private code, but you can use this
// class to override only some of the Draw() functions - all the other ones
// will be left to the original renderer
// ----------------------------------------------------------------------------

class DESKGUI_API ibDelegateRenderer : public ibRenderer
{
public:
    ibDelegateRenderer(ibRenderer *renderer) : m_renderer(renderer) { }

    virtual void DrawBackground(wxDC& dc,
                                const wxColour& col,
                                const wxRect& rect,
                                int flags,
                                ibWindow *window = nullptr ) override
        { m_renderer->DrawBackground(dc, col, rect, flags, window ); }
    virtual void DrawButtonSurface(wxDC& dc,
                                   const wxColour& col,
                                   const wxRect& rect,
                                   int flags) override
        { m_renderer->DrawButtonSurface(dc, col, rect, flags); }
    virtual void DrawFocusRect(wxWindow* win, wxDC& dc, const wxRect& rect, int flags = 0) override
        { m_renderer->DrawFocusRect(win, dc, rect, flags); }
    virtual void DrawLabel(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int align = wxALIGN_LEFT | wxALIGN_TOP,
                           int indexAccel = -1,
                           wxRect *rectBounds = nullptr) override
        { m_renderer->DrawLabel(dc, label, rect,
                                flags, align, indexAccel, rectBounds); }
    virtual void DrawButtonLabel(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& image,
                                 const wxRect& rect,
                                 int flags = 0,
                                 int align = wxALIGN_LEFT | wxALIGN_TOP,
                                 int indexAccel = -1,
                                 wxRect *rectBounds = nullptr) override
        { m_renderer->DrawButtonLabel(dc, label, image, rect,
                                      flags, align, indexAccel, rectBounds); }
    virtual void DrawBorder(wxDC& dc,
                            wxBorder border,
                            const wxRect& rect,
                            int flags = 0,
                            wxRect *rectIn = nullptr) override
        { m_renderer->DrawBorder(dc, border, rect, flags, rectIn); }
    virtual void DrawTextBorder(wxDC& dc,
                                wxBorder border,
                                const wxRect& rect,
                                int flags = 0,
                                wxRect *rectIn = nullptr) override
        { m_renderer->DrawTextBorder(dc, border, rect, flags, rectIn); }
    virtual void DrawButtonBorder(wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxRect *rectIn = nullptr) override
        { m_renderer->DrawButtonBorder(dc, rect, flags, rectIn); }
    virtual void DrawFrame(wxDC& dc,
                           const wxString& label,
                           const wxRect& rect,
                           int flags = 0,
                           int align = wxALIGN_LEFT,
                           int indexAccel = -1) override
        { m_renderer->DrawFrame(dc, label, rect, flags, align, indexAccel); }
    virtual void DrawHorizontalLine(wxDC& dc,
                                    wxCoord y, wxCoord x1, wxCoord x2) override
        { m_renderer->DrawHorizontalLine(dc, y, x1, x2); }
    virtual void DrawVerticalLine(wxDC& dc,
                                  wxCoord x, wxCoord y1, wxCoord y2) override
        { m_renderer->DrawVerticalLine(dc, x, y1, y2); }
    virtual void DrawArrow(wxDC& dc,
                           wxDirection dir,
                           const wxRect& rect,
                           int flags = 0) override
        { m_renderer->DrawArrow(dc, dir, rect, flags); }
    virtual void DrawScrollbarArrow(wxDC& dc,
                           wxDirection dir,
                           const wxRect& rect,
                           int flags = 0) override
        { m_renderer->DrawScrollbarArrow(dc, dir, rect, flags); }
    virtual void DrawScrollbarThumb(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) override
        { m_renderer->DrawScrollbarThumb(dc, orient, rect, flags); }
    virtual void DrawScrollbarShaft(wxDC& dc,
                                    wxOrientation orient,
                                    const wxRect& rect,
                                    int flags = 0) override
        { m_renderer->DrawScrollbarShaft(dc, orient, rect, flags); }
    virtual void DrawScrollCorner(wxDC& dc,
                                  const wxRect& rect) override
        { m_renderer->DrawScrollCorner(dc, rect); }
    virtual void DrawItem(wxDC& dc,
                          const wxString& label,
                          const wxRect& rect,
                          int flags = 0) override
        { m_renderer->DrawItem(dc, label, rect, flags); }
    virtual void DrawCheckItem(wxDC& dc,
                               const wxString& label,
                               const wxBitmap& bitmap,
                               const wxRect& rect,
                               int flags = 0) override
        { m_renderer->DrawCheckItem(dc, label, bitmap, rect, flags); }
    virtual void DrawCheckButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) override
        { m_renderer->DrawCheckButton(dc, label, bitmap, rect,
                                      flags, align, indexAccel); }
    virtual void DrawRadioButton(wxDC& dc,
                                 const wxString& label,
                                 const wxBitmap& bitmap,
                                 const wxRect& rect,
                                 int flags = 0,
                                 wxAlignment align = wxALIGN_LEFT,
                                 int indexAccel = -1) override
        { m_renderer->DrawRadioButton(dc, label, bitmap, rect,
                                      flags, align, indexAccel); }
#if wxUSE_TOOLBAR
    virtual void DrawToolBarBackground(wxDC& dc,
                                       const wxColour& col,
                                       const wxRect& rect,
                                       int flags = 0) override
        { m_renderer->DrawToolBarBackground(dc, col, rect, flags); }
    virtual void DrawToolBarButton(wxDC& dc,
                                   const wxString& label,
                                   const wxBitmap& bitmap,
                                   const wxRect& rect,
                                   int flags = 0,
                                   long style = 0,
                                   int tbarStyle = 0) override
        { m_renderer->DrawToolBarButton(dc, label, bitmap, rect, flags, style, tbarStyle); }
    virtual void DrawGripper(wxDC& dc,
                             const wxRect& rect,
                             wxOrientation orient) override
        { m_renderer->DrawGripper(dc, rect, orient); }
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    virtual void DrawTextLine(wxDC& dc,
                              const wxString& text,
                              const wxRect& rect,
                              int selStart = -1,
                              int selEnd = -1,
                              int flags = 0) override
        { m_renderer->DrawTextLine(dc, text, rect, selStart, selEnd, flags); }
    virtual void DrawLineWrapMark(wxDC& dc, const wxRect& rect) override
        { m_renderer->DrawLineWrapMark(dc, rect); }
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    virtual void DrawTab(wxDC& dc,
                         const wxRect& rect,
                         wxDirection dir,
                         const wxString& label,
                         const wxBitmap& bitmap = wxNullBitmap,
                         int flags = 0,
                         int accel = -1) override
        { m_renderer->DrawTab(dc, rect, dir, label, bitmap, flags, accel); }
    virtual void DrawTabStrip(wxDC& dc,
                              const wxRect& rect,
                              wxDirection dir) override
        { m_renderer->DrawTabStrip(dc, rect, dir); }
    virtual void DrawStripTab(wxDC& dc,
                              const wxRect& rect,
                              wxDirection dir,
                              int flags = 0) override
        { m_renderer->DrawStripTab(dc, rect, dir, flags); }
    virtual void DrawStripButton(wxDC& dc,
                                 const wxRect& rect,
                                 int button,
                                 int flags = 0) override
        { m_renderer->DrawStripButton(dc, rect, button, flags); }
#endif // wxUSE_NOTEBOOK

    virtual void DrawPaneCaption(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& title,
                                 const wxBitmap& icon,
                                 int flags = 0) override
        { m_renderer->DrawPaneCaption(dc, rect, title, icon, flags); }
    virtual void DrawPaneButton(wxDC& dc,
                                const wxRect& rect,
                                int button,
                                int flags = 0) override
        { m_renderer->DrawPaneButton(dc, rect, button, flags); }
    virtual void DrawPaneBorder(wxDC& dc,
                                const wxRect& rect) override
        { m_renderer->DrawPaneBorder(dc, rect); }
    virtual void DrawSash(wxDC& dc,
                          const wxRect& rect,
                          wxOrientation orient) override
        { m_renderer->DrawSash(dc, rect, orient); }

#if wxUSE_SLIDER

    virtual void DrawSliderShaft(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0,
                                 wxRect *rectShaft = nullptr) override
        { m_renderer->DrawSliderShaft(dc, rect, lenThumb, orient, flags, style, rectShaft); }
    virtual void DrawSliderThumb(wxDC& dc,
                                 const wxRect& rect,
                                 wxOrientation orient,
                                 int flags = 0,
                                 long style = 0) override
        { m_renderer->DrawSliderThumb(dc, rect, orient, flags, style); }
    virtual void DrawSliderTicks(wxDC& dc,
                                 const wxRect& rect,
                                 int lenThumb,
                                 wxOrientation orient,
                                 int start,
                                 int end,
                                 int WXUNUSED(step) = 1,
                                 int flags = 0,
                                 long style = 0) override
        { m_renderer->DrawSliderTicks(dc, rect, lenThumb, orient,
                                      start, end, start, flags, style); }
#endif // wxUSE_SLIDER

#if wxUSE_MENUS
    virtual void DrawMenuBarItem(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0,
                                 int indexAccel = -1) override
        { m_renderer->DrawMenuBarItem(dc, rect, label, flags, indexAccel); }
    virtual void DrawMenuItem(wxDC& dc,
                              wxCoord y,
                              const ibMenuGeometryInfo& gi,
                              const wxString& label,
                              const wxString& accel,
                              const wxBitmap& bitmap = wxNullBitmap,
                              int flags = 0,
                              int indexAccel = -1) override
        { m_renderer->DrawMenuItem(dc, y, gi, label, accel,
                                   bitmap, flags, indexAccel); }
    virtual void DrawMenuSeparator(wxDC& dc,
                                   wxCoord y,
                                   const ibMenuGeometryInfo& geomInfo) override
        { m_renderer->DrawMenuSeparator(dc, y, geomInfo); }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    virtual void DrawStatusField(wxDC& dc,
                                 const wxRect& rect,
                                 const wxString& label,
                                 int flags = 0, int style = 0) override
        { m_renderer->DrawStatusField(dc, rect, label, flags, style); }
#endif // wxUSE_STATUSBAR

    virtual void DrawToolTip(wxDC& dc, const wxRect& rect, const wxString& text) override
        { m_renderer->DrawToolTip(dc, rect, text); }

    virtual void DrawFrameTitleBar(wxDC& dc,
                                   const wxRect& rect,
                                   const wxString& title,
                                   const wxIcon& icon,
                                   int flags,
                                   int specialButton = 0,
                                   int specialButtonFlag = 0) override
        { m_renderer->DrawFrameTitleBar(dc, rect, title, icon, flags,
                                        specialButton, specialButtonFlag); }
    virtual void DrawFrameBorder(wxDC& dc,
                                 const wxRect& rect,
                                 int flags) override
        { m_renderer->DrawFrameBorder(dc, rect, flags); }
    virtual void DrawFrameBackground(wxDC& dc,
                                     const wxRect& rect,
                                     int flags) override
        { m_renderer->DrawFrameBackground(dc, rect, flags); }
    virtual void DrawFrameTitle(wxDC& dc,
                                const wxRect& rect,
                                const wxString& title,
                                int flags) override
        { m_renderer->DrawFrameTitle(dc, rect, title, flags); }
    virtual void DrawFrameIcon(wxDC& dc,
                               const wxRect& rect,
                               const wxIcon& icon,
                               int flags) override
        { m_renderer->DrawFrameIcon(dc, rect, icon, flags); }
    virtual void DrawFrameButton(wxDC& dc,
                                 wxCoord x, wxCoord y,
                                 int button,
                                 int flags = 0) override
        { m_renderer->DrawFrameButton(dc, x, y, button, flags); }

#if wxUSE_COMBOBOX
    virtual void GetComboBitmaps(wxBitmap *bmpNormal,
                                 wxBitmap *bmpFocus,
                                 wxBitmap *bmpPressed,
                                 wxBitmap *bmpDisabled) override
        { m_renderer->GetComboBitmaps(bmpNormal, bmpFocus,
                                      bmpPressed, bmpDisabled); }
#endif // wxUSE_COMBOBOX

    virtual void AdjustSize(wxSize *size, const ibWindow *window) override
        { m_renderer->AdjustSize(size, window); }
    virtual wxRect GetBorderDimensions(wxBorder border) const override
        { return m_renderer->GetBorderDimensions(border); }
    virtual bool AreScrollbarsInsideBorder() const override
        { return m_renderer->AreScrollbarsInsideBorder(); }

#if wxUSE_SCROLLBAR
    virtual wxSize GetScrollbarArrowSize() const override
        { return m_renderer->GetScrollbarArrowSize(); }
#endif // wxUSE_SCROLLBAR

    virtual wxCoord GetListboxItemHeight(wxCoord fontHeight) override
        { return m_renderer->GetListboxItemHeight(fontHeight); }
    virtual wxSize GetCheckBitmapSize() const override
        { return m_renderer->GetCheckBitmapSize(); }
    virtual wxSize GetRadioBitmapSize() const override
        { return m_renderer->GetRadioBitmapSize(); }
    virtual wxCoord GetCheckItemMargin() const override
        { return m_renderer->GetCheckItemMargin(); }

#if wxUSE_TOOLBAR
    virtual wxSize GetToolBarButtonSize(wxCoord *separator) const override
        { return m_renderer->GetToolBarButtonSize(separator); }
    virtual wxSize GetToolBarMargin() const override
        { return m_renderer->GetToolBarMargin(); }
#endif // wxUSE_TOOLBAR

#if wxUSE_TEXTCTRL
    virtual wxRect GetTextTotalArea(const wxRect& rect) const override
        { return m_renderer->GetTextTotalArea(rect); }
    virtual wxRect GetTextClientArea(const wxRect& rect,
                                     wxCoord *extraSpaceBeyond) const override
        { return m_renderer->GetTextClientArea(rect, extraSpaceBeyond); }
#endif // wxUSE_TEXTCTRL

#if wxUSE_NOTEBOOK
    virtual wxSize GetTabIndent() const override { return m_renderer->GetTabIndent(); }
    virtual wxSize GetTabPadding() const override { return m_renderer->GetTabPadding(); }
#endif // wxUSE_NOTEBOOK

#if wxUSE_SLIDER
    virtual wxCoord GetSliderDim() const override
        { return m_renderer->GetSliderDim(); }
    virtual wxCoord GetSliderTickLen() const override
        { return m_renderer->GetSliderTickLen(); }

    virtual wxRect GetSliderShaftRect(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient,
                                      long style = 0) const override
        { return m_renderer->GetSliderShaftRect(rect, lenThumb, orient, style); }
    virtual wxSize GetSliderThumbSize(const wxRect& rect,
                                      int lenThumb,
                                      wxOrientation orient) const override
        { return m_renderer->GetSliderThumbSize(rect, lenThumb, orient); }
#endif // wxUSE_SLIDER

    virtual wxSize GetProgressBarStep() const override
        { return m_renderer->GetProgressBarStep(); }

#if wxUSE_MENUS
    virtual wxSize GetMenuBarItemSize(const wxSize& sizeText) const override
        { return m_renderer->GetMenuBarItemSize(sizeText); }
    virtual wxSize GetMenuMargin() const override
        { return m_renderer->GetMenuMargin(); }
    virtual wxCoord GetMenuItemHeight(wxCoord heightText) const override
        { return m_renderer->GetMenuItemHeight(heightText); }
    virtual wxCoord GetMenuSeparatorHeight() const override
        { return m_renderer->GetMenuSeparatorHeight(); }
    virtual wxCoord GetMenuBitmapWidth(wxCoord widthBitmap) const override
        { return m_renderer->GetMenuBitmapWidth(widthBitmap); }
    virtual wxCoord GetMenuAccelMargin() const override
        { return m_renderer->GetMenuAccelMargin(); }
    virtual wxCoord GetMenuSubmenuWidth() const override
        { return m_renderer->GetMenuSubmenuWidth(); }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    virtual wxSize GetStatusBarBorders() const override
        { return m_renderer->GetStatusBarBorders(); }
    virtual wxCoord GetStatusBarBorderBetweenFields() const override
        { return m_renderer->GetStatusBarBorderBetweenFields(); }
    virtual wxSize GetStatusBarFieldMargins() const override
        { return m_renderer->GetStatusBarFieldMargins(); }
#endif // wxUSE_STATUSBAR

    virtual wxRect GetFrameClientArea(const wxRect& rect, int flags) const override
        { return m_renderer->GetFrameClientArea(rect, flags); }
    virtual wxSize GetFrameTotalSize(const wxSize& clientSize, int flags) const override
        { return m_renderer->GetFrameTotalSize(clientSize, flags); }
    virtual wxSize GetFrameMinSize(int flags) const override
        { return m_renderer->GetFrameMinSize(flags); }
    virtual wxSize GetFrameIconSize() const override
        { return m_renderer->GetFrameIconSize(); }
    virtual int HitTestFrame(const wxRect& rect,
                             const wxPoint& pt,
                             int flags) const override
        { return m_renderer->HitTestFrame(rect, pt, flags); }

    virtual int  DrawHeaderButton(wxWindow *win,
                                  wxDC& dc,
                                  const wxRect& rect,
                                  int flags = 0,
                                  wxHeaderSortIconType sortIcon = wxHDR_SORT_ICON_NONE,
                                  wxHeaderButtonParams* params = nullptr) override
        { return m_renderer->DrawHeaderButton(win, dc, rect, flags, sortIcon, params); }
    virtual void DrawTreeItemButton(wxWindow *win,
                                    wxDC& dc,
                                    const wxRect& rect,
                                    int flags = 0) override
        { m_renderer->DrawTreeItemButton(win, dc, rect, flags); }

protected:
    ibRenderer *m_renderer;
};

#endif // _DESKGUI_UNIV_RENDERER_H_
