// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/univ/theme.cpp
// Purpose:     implementation of ibThemeEngine
// Author:      Vadim Zeitlin
// Created:     06.08.00
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
    #include <wx/intl.h>
    #include <wx/log.h>
#endif // WX_PRECOMP

#include <wx/artprov.h>

#include "frontend/uikit/renderer.h"
#include "frontend/uikit/inputHandler.h"
#include "frontend/uikit/theme.h"

// ============================================================================
// implementation
// ============================================================================

ibThemeInfo *ibThemeEngine::ms_allThemes = nullptr;
ibThemeEngine *ibThemeEngine::ms_theme = nullptr;

// ----------------------------------------------------------------------------
// "dynamic" theme creation
// ----------------------------------------------------------------------------

ibThemeInfo::ibThemeInfo(Constructor c,
                         const wxString& n,
                         const wxString& d)
           : name(n), desc(d), ctor(c)
{
    // insert us (in the head of) the linked list
    next = ibThemeEngine::ms_allThemes;
    ibThemeEngine::ms_allThemes = this;
}

/* static */ ibThemeEngine *ibThemeEngine::Create(const wxString& name)
{
    // find the theme in the list by name
    ibThemeInfo *info = ms_allThemes;
    while ( info )
    {
        if ( name.CmpNoCase(info->name) == 0 )
        {
            return info->ctor();
        }

        info = info->next;
    }

    return nullptr;
}

// ----------------------------------------------------------------------------
// the default theme (called by wxApp::OnInitGui)
// ----------------------------------------------------------------------------

/* static */ bool ibThemeEngine::CreateDefault()
{
    if ( ms_theme )
    {
        // we already have a theme
        return true;
    }

    wxString nameDefTheme;

    // use the environment variable first
    const wxChar *p = wxGetenv(wxT("WXTHEME"));
    if ( p )
    {
        nameDefTheme = p;
    }
#ifdef wxUNIV_DEFAULT_THEME
    else // use native theme by default
    {
        WX_USE_THEME(wxUNIV_DEFAULT_THEME);
        nameDefTheme = wxSTRINGIZE_T(wxUNIV_DEFAULT_THEME);
    }
#endif // wxUNIV_DEFAULT_THEME

    ibThemeEngine *theme = Create(nameDefTheme);

    // fallback to the first one in the list
    if ( !theme && ms_allThemes )
    {
        theme = ms_allThemes->ctor();
    }

    // abort if still nothing
    if ( !theme )
    {
        wxLogError(_("Failed to initialize GUI: no built-in themes found."));

        return false;
    }

    // Set the theme as current.
    ibThemeEngine::Set(theme);

    return true;
}

/* static */ ibThemeEngine *ibThemeEngine::Set(ibThemeEngine *theme)
{
    ibThemeEngine *themeOld = ms_theme;
    ms_theme = theme;

    if ( ms_theme )
    {
        // automatically start using the art provider of the new theme if it
        // has one
        wxArtProvider *art = ms_theme->GetArtProvider();
        if ( art )
            wxArtProvider::Push(art);
    }

    return themeOld;
}

// ----------------------------------------------------------------------------
// assorted trivial dtors
// ----------------------------------------------------------------------------

ibThemeEngine::~ibThemeEngine()
{
}


// ----------------------------------------------------------------------------
// ibDelegateTheme
// ----------------------------------------------------------------------------

ibDelegateTheme::ibDelegateTheme(const wxString& theme)
{
    m_themeName = theme;
    m_theme = nullptr;
}

ibDelegateTheme::~ibDelegateTheme()
{
    delete m_theme;
}

bool ibDelegateTheme::GetOrCreateTheme()
{
    if ( !m_theme )
        m_theme = ibThemeEngine::Create(m_themeName);
    return m_theme != nullptr;
}

ibRenderer *ibDelegateTheme::GetRenderer()
{
    if ( !GetOrCreateTheme() )
        return nullptr;

    return m_theme->GetRenderer();
}

wxArtProvider *ibDelegateTheme::GetArtProvider()
{
    if ( !GetOrCreateTheme() )
        return nullptr;

    return m_theme->GetArtProvider();
}

ibInputHandler *ibDelegateTheme::GetInputHandler(const wxString& control,
                                                 ibInputConsumer *consumer)
{
    if ( !GetOrCreateTheme() )
        return nullptr;

    return m_theme->GetInputHandler(control, consumer);
}

ibColourScheme *ibDelegateTheme::GetColourScheme()
{
    if ( !GetOrCreateTheme() )
        return nullptr;

    return m_theme->GetColourScheme();
}

// anchor: keep the Luna theme object file linked into frontend.dll
WX_USE_THEME(luna);
