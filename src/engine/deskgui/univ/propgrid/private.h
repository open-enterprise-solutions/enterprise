// FORKED from wxWidgets' wxPropertyGrid (wx 3.3.4, include/wx/propgrid/private.h): wx -> ib prefixes
// applied mechanically to the names it defines (docs/private/wx-fork.md).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/propgrid/private.h
// Purpose:     Private ibPropertyGrid declarations
// Author:      Artur Wieczorek, Jaakko Salli
// Created:     2023-03-10
// Copyright:   (c) 2023 wxWidgets development team
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_PROPGRID_PRIVATE_H_
#define _DESKGUI_UNIV_PROPGRID_PRIVATE_H_

// (wx's `#error` unless WXBUILDING is not here: it is the fork's sources' own header, built in deskgui, not wx's)

#include "wx/defs.h"

// -----------------------------------------------------------------------

// space between vertical sides of a custom image
#define ibPG_CUSTOM_IMAGE_SPACINGY      1

// space between caption and selection rectangle,
#define ibPG_CAPRECTXMARGIN             2

// horizontally and vertically
#define ibPG_CAPRECTYMARGIN             1

// -----------------------------------------------------------------------

//
// Here are some platform dependent defines
// NOTE: More in propertygrid.cpp
//
// NB: Only define ibPG_TEXTCTRLXADJUST for platforms that do not
//     (yet) support wxTextEntry::SetMargins() for the left margin.

#if defined(__WXMSW__)

    // space between vertical line and value text
    #define ibPG_XBEFORETEXT            4
    // space between vertical line and value editor control
    #define ibPG_XBEFOREWIDGET          1

    // left margin can be set with wxTextEntry::SetMargins()
    #undef ibPG_TEXTCTRLXADJUST

    // comment to use bitmap buttons
    #define ibPG_ICON_WIDTH             9
    // width of optional bitmap/image in front of property
    #define ibPG_CUSTOM_IMAGE_WIDTH     20

    // 1 if splitter drag detect margin and control cannot overlap
    #define ibPG_NO_CHILD_EVT_MOTION    0

    #define ibPG_NAT_BUTTON_BORDER_ANY          1
    #define ibPG_NAT_BUTTON_BORDER_X            1
    #define ibPG_NAT_BUTTON_BORDER_Y            1

    // If 1 then controls are refreshed explicitly in a few places
    #define ibPG_REFRESH_CONTROLS 0

#elif defined(__WXGTK__)

    // space between vertical line and value text
    #define ibPG_XBEFORETEXT            5
    // space between vertical line and value editor control
    #define ibPG_XBEFOREWIDGET          1

    // x position adjustment for ibTextCtrl (and like)
    // left margin can be set with wxTextEntry::SetMargins()
    #undef ibPG_TEXTCTRLXADJUST

    // comment to use bitmap buttons
    #define ibPG_ICON_WIDTH             9
    // width of optional bitmap/image in front of property
    #define ibPG_CUSTOM_IMAGE_WIDTH     20

    // 1 if splitter drag detect margin and control cannot overlap
    #define ibPG_NO_CHILD_EVT_MOTION    1

    #define ibPG_NAT_BUTTON_BORDER_ANY      1
    #define ibPG_NAT_BUTTON_BORDER_X        1
    #define ibPG_NAT_BUTTON_BORDER_Y        1

    // If 1 then controls are refreshed after selected was drawn.
    #define ibPG_REFRESH_CONTROLS 1

#elif defined(__WXMAC__)

    // space between vertical line and value text
    #define ibPG_XBEFORETEXT            4
    // space between vertical line and value editor widget
    #define ibPG_XBEFOREWIDGET          1

    // x position adjustment for ibTextCtrl (and like)
    // left margin cannot be set with wxTextEntry::SetMargins()
    #define ibPG_TEXTCTRLXADJUST        1

    // comment to use bitmap buttons
    #define ibPG_ICON_WIDTH             11
    // width of optional bitmap/image in front of property
    #define ibPG_CUSTOM_IMAGE_WIDTH     20

    // 1 if splitter drag detect margin and control cannot overlap
    #define ibPG_NO_CHILD_EVT_MOTION    0

    #define ibPG_NAT_BUTTON_BORDER_ANY      0
    #define ibPG_NAT_BUTTON_BORDER_X        0
    #define ibPG_NAT_BUTTON_BORDER_Y        0

    // If 1 then controls are refreshed after selected was drawn.
    #define ibPG_REFRESH_CONTROLS 0

#else // defaults

    // space between vertical line and value text
    #define ibPG_XBEFORETEXT            5
    // space between vertical line and value editor widget
    #define ibPG_XBEFOREWIDGET          1

    // x position adjustment for ibTextCtrl (and like)
    // left margin cannot be set with wxTextEntry::SetMargins()
    #define ibPG_TEXTCTRLXADJUST        3

    // comment to use bitmap buttons
    #define ibPG_ICON_WIDTH             9
    // width of optional bitmap/image in front of property
    #define ibPG_CUSTOM_IMAGE_WIDTH     20

    // 1 if splitter drag detect margin and control cannot overlap
    #define ibPG_NO_CHILD_EVT_MOTION    1

    #define ibPG_NAT_BUTTON_BORDER_ANY      0
    #define ibPG_NAT_BUTTON_BORDER_X        0
    #define ibPG_NAT_BUTTON_BORDER_Y        0

    // If 1 then controls are refreshed after selected was drawn.
    #define ibPG_REFRESH_CONTROLS 0
#endif // platform


#define ibPG_CONTROL_MARGIN             0 // space between splitter and control

#define ibCC_CUSTOM_IMAGE_MARGIN1       4  // before image
#define ibCC_CUSTOM_IMAGE_MARGIN2       5  // after image

#define DEFAULT_IMAGE_OFFSET_INCREMENT \
    (ibCC_CUSTOM_IMAGE_MARGIN1 + ibCC_CUSTOM_IMAGE_MARGIN2)

#define ibPG_DRAG_MARGIN                30

#if ibPG_NO_CHILD_EVT_MOTION
    #define ibPG_SPLITTERX_DETECTMARGIN1    3 // this much on left
    #define ibPG_SPLITTERX_DETECTMARGIN2    2 // this much on right
#else
    #define ibPG_SPLITTERX_DETECTMARGIN1    3 // this much on left
    #define ibPG_SPLITTERX_DETECTMARGIN2    2 // this much on right
#endif

// Use this macro to generate standard custom image height from
#define ibPG_STD_CUST_IMAGE_HEIGHT(LINEHEIGHT)  ((LINEHEIGHT)-3)

// Undefine ibPG_ICON_WIDTH to use supplied xpm bitmaps instead
// (for tree buttons)
//#undef ibPG_ICON_WIDTH

// -----------------------------------------------------------------------

// Events used only internally
wxDECLARE_EVENT(ibEVT_PG_HSCROLL, ibPropertyGridEvent);
wxDECLARE_EVENT(ibEVT_PG_COLS_RESIZED, ibPropertyGridEvent);

// -----------------------------------------------------------------------

// Flags used only internally

// ibBoolProperty, ibFlagsProperty specific flags
constexpr ibPGFlags ibPGPropertyFlags_UseCheckBox = ibPGFlags::Reserved_1;
// DCC = Double Click Cycles
constexpr ibPGFlags ibPGPropertyFlags_UseDCC = ibPGFlags::Reserved_2;

// ibStringProperty flag
constexpr ibPGFlags ibPGPropertyFlags_Password = ibPGFlags::Reserved_2;

// ibColourProperty flag - if set, then match from list is searched for a custom colour.
constexpr ibPGFlags ibPGPropertyFlags_TranslateCustom = ibPGFlags::Reserved_1;

// ibCursorProperty, ibSystemColourProperty - If set, then selection of choices is static
// and should not be changed (i.e. returns nullptr in GetPropertyChoices).
constexpr ibPGFlags ibPGPropertyFlags_StaticChoices = ibPGFlags::Reserved_1;

// ibSystemColourProperty - ibEnumProperty based classes cannot use ibPGFlags::Reserved_1
constexpr ibPGFlags ibPGPropertyFlags_HideCustomColour = ibPGFlags::Reserved_2;
constexpr ibPGFlags ibPGPropertyFlags_ColourHasAlpha = ibPGFlags::Reserved_3;

// ibFileProperty - if set, full path is shown in ibFileProperty.
constexpr ibPGFlags ibPGPropertyFlags_ShowFullFileName = ibPGFlags::ShowFullFileName;

// ibLongStringProperty - flag used to mark that edit button
// should be enabled even in the read-only mode.
constexpr ibPGFlags ibPGPropertyFlags_ActiveButton = ibPGFlags::Reserved_3;

#endif // _DESKGUI_UNIV_PROPGRID_PRIVATE_H_
