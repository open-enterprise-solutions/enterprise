// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/settingsuniv.cpp
// Purpose:     wxSystemSettings wxUniv-specific parts
// Author:      Vadim Zeitlin
// Created:     20.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

// ===========================================================================
// declarations
// ===========================================================================

// ---------------------------------------------------------------------------
// headers
// ---------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>


#ifndef WX_PRECOMP
    #include <wx/settings.h>
    #include <wx/gdicmn.h>
#endif // WX_PRECOMP

#include "frontend/uikit/colourScheme.h"
#include "frontend/uikit/window.h"
#include "frontend/uikit/theme.h"
#include "frontend/uikit/renderer.h"

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxSystemSettings
// ----------------------------------------------------------------------------

wxColour wxSystemSettings::GetColour(wxSystemColour index)
{
    // the elements of this enum must be kept in sync with wxSystemColour
    static const ibColourScheme::StdColour s_mapSysToThemeCol[] =
    {
        ibColourScheme::SCROLLBAR /* wxSYS_COLOUR_SCROLLBAR */,
        ibColourScheme::WINDOW /* wxSYS_COLOUR_BACKGROUND */,
        ibColourScheme::TITLEBAR_ACTIVE_TEXT /* wxSYS_COLOUR_ACTIVECAPTION */,
        ibColourScheme::TITLEBAR_TEXT /* wxSYS_COLOUR_INACTIVECAPTION */,
        ibColourScheme::MAX /* wxSYS_COLOUR_MENU */,
        ibColourScheme::MAX /* wxSYS_COLOUR_WINDOW */,
        ibColourScheme::MAX /* wxSYS_COLOUR_WINDOWFRAME */,
        ibColourScheme::CONTROL_TEXT /* wxSYS_COLOUR_MENUTEXT */,
        ibColourScheme::CONTROL_TEXT /* wxSYS_COLOUR_WINDOWTEXT */,
        ibColourScheme::CONTROL_TEXT /* wxSYS_COLOUR_CAPTIONTEXT */,
        ibColourScheme::MAX /* wxSYS_COLOUR_ACTIVEBORDER */,
        ibColourScheme::MAX /* wxSYS_COLOUR_INACTIVEBORDER */,
        ibColourScheme::FRAME /* wxSYS_COLOUR_APPWORKSPACE */,
        ibColourScheme::HIGHLIGHT /* wxSYS_COLOUR_HIGHLIGHT */,
        ibColourScheme::HIGHLIGHT_TEXT /* wxSYS_COLOUR_HIGHLIGHTTEXT */,
        ibColourScheme::CONTROL /* wxSYS_COLOUR_BTNFACE */,
        ibColourScheme::SHADOW_DARK /* wxSYS_COLOUR_BTNSHADOW */,
        ibColourScheme::CONTROL_TEXT_DISABLED /* wxSYS_COLOUR_GRAYTEXT */,
        ibColourScheme::CONTROL_TEXT /* wxSYS_COLOUR_BTNTEXT */,
        ibColourScheme::MAX /* wxSYS_COLOUR_INACTIVECAPTIONTEXT */,
        ibColourScheme::SHADOW_HIGHLIGHT /* wxSYS_COLOUR_BTNHIGHLIGHT */,
        ibColourScheme::SHADOW_DARK /* wxSYS_COLOUR_3DDKSHADOW */,
        ibColourScheme::SHADOW_OUT /* wxSYS_COLOUR_3DLIGHT */,
        ibColourScheme::MAX /* wxSYS_COLOUR_INFOTEXT */,
        ibColourScheme::MAX /* wxSYS_COLOUR_INFOBK */,
        ibColourScheme::WINDOW /* wxSYS_COLOUR_LISTBOX */,
        ibColourScheme::MAX /* wxSYS_COLOUR_HOTLIGHT */,
        ibColourScheme::TITLEBAR_ACTIVE_TEXT /* wxSYS_COLOUR_GRADIENTACTIVECAPTION */,
        ibColourScheme::TITLEBAR_TEXT /* wxSYS_COLOUR_GRADIENTINACTIVECAPTION */,
        ibColourScheme::MAX /* wxSYS_COLOUR_MENUHILIGHT */,
        ibColourScheme::MAX /* wxSYS_COLOUR_MENUBAR */,
        ibColourScheme::CONTROL_TEXT /* wxSYS_COLOUR_LISTBOXTEXT */,
        ibColourScheme::HIGHLIGHT_TEXT /* wxSYS_COLOUR_LISTBOXHIGHLIGHTTEXT */,
        ibColourScheme::CONTROL /* wxSYS_COLOUR_GRIDLINES */,
        ibColourScheme::HIGHLIGHT /* wxSYS_COLOUR_LISTBOXHIGHLIGHT */,
    };

    wxCOMPILE_TIME_ASSERT( WXSIZEOF(s_mapSysToThemeCol) == wxSYS_COLOUR_MAX,
                           StdColDefsMismatch );

    wxCHECK_MSG( index < (int)WXSIZEOF(s_mapSysToThemeCol), wxNullColour,
                 wxT("invalid wxSystemColour") );

    ibColourScheme::StdColour col = s_mapSysToThemeCol[index];
    if ( col == ibColourScheme::MAX )
    {
        // we don't have theme-equivalent for this colour
        return wxSystemSettingsNative::GetColour(index);
    }

    return ibThemeEngine::Get()->GetColourScheme()->Get(col);
}

int wxSystemSettings::GetMetric(wxSystemMetric index, const ibWindow* win)
{
    switch ( index )
    {
        case wxSYS_VSCROLL_X:
            return ibThemeEngine::Get()->GetRenderer()->GetScrollbarArrowSize().x;
        case wxSYS_HSCROLL_Y:
            return ibThemeEngine::Get()->GetRenderer()->GetScrollbarArrowSize().y;

        default:
            return wxSystemSettingsNative::GetMetric(index, win);
    }
}
