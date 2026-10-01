// FORKED from the wxUniversal theme engine (wx -> ib prefixes applied mechanically).
// Revive control by control: fix compile errors, add to frontend.vcxproj
// (set ObjectFileName on a name clash with visualView), add to the demo form.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/univ/theme.h
// Purpose:     ibThemeEngine class manages all configurable aspects of the
//              application including the look (ibRenderer), feel
//              (ibInputHandler) and the colours (ibColourScheme)
// Author:      Vadim Zeitlin
// Created:     06.08.00
// Copyright:   (c) 2000 SciTech Software, Inc. (www.scitechsoft.com)
// Licence:     ibWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_UNIV_THEME_H_
#define _WX_UNIV_THEME_H_

#include "frontend/frontend.h"

#include <wx/string.h>

// ----------------------------------------------------------------------------
// ibThemeEngine
// ----------------------------------------------------------------------------

class wxArtProvider;
class ibColourScheme;
class ibInputConsumer;
class ibInputHandler;
class ibRenderer;
struct ibThemeInfo;

class FRONTEND_API ibThemeEngine
{
public:
    // static methods
    // --------------

    // create the default theme
    static bool CreateDefault();

    // create the theme by name (will return nullptr if not found)
    static ibThemeEngine *Create(const wxString& name);

    // change the current scheme
    static ibThemeEngine *Set(ibThemeEngine *theme);

    // get the current theme (never null)
    // SEAM vs univ: the univ wxApp called CreateDefault() at startup; our
    // host apps are native, so the default theme is created lazily here
    static ibThemeEngine *Get() { if ( !ms_theme ) CreateDefault(); return ms_theme; }

    // the theme methods
    // -----------------

    // get the renderer implementing all the control-drawing operations in
    // this theme
    virtual ibRenderer *GetRenderer() = 0;

    // get the art provider to be used together with this theme
    virtual wxArtProvider *GetArtProvider() = 0;

    // get the input handler of the given type, forward to the standard one
    virtual ibInputHandler *GetInputHandler(const wxString& handlerType,
                                            ibInputConsumer *consumer) = 0;

    // get the colour scheme for the control with this name
    virtual ibColourScheme *GetColourScheme() = 0;

    // implementation only from now on
    // -------------------------------

    virtual ~ibThemeEngine();

private:
    // the list of descriptions of all known themes
    static ibThemeInfo *ms_allThemes;

    // the current theme
    static ibThemeEngine *ms_theme;
    friend struct ibThemeInfo;
};

// ----------------------------------------------------------------------------
// ibDelegateTheme: it is impossible to inherit from any of standard
// themes as their declarations are in private code, but you can use this
// class to override only some of their functions - all the other ones
// will be left to the original theme
// ----------------------------------------------------------------------------

class FRONTEND_API ibDelegateTheme : public ibThemeEngine
{
public:
    ibDelegateTheme(const wxString& theme);
    virtual ~ibDelegateTheme();

    virtual ibRenderer *GetRenderer();
    virtual wxArtProvider *GetArtProvider();
    virtual ibInputHandler *GetInputHandler(const wxString& control,
                                            ibInputConsumer *consumer);
    virtual ibColourScheme *GetColourScheme();

protected:
    // gets or creates theme and sets m_theme to point to it,
    // returns true on success
    bool GetOrCreateTheme();

    wxString    m_themeName;
    ibThemeEngine    *m_theme;
};

// ----------------------------------------------------------------------------
// dynamic theme creation helpers
// ----------------------------------------------------------------------------

struct FRONTEND_API ibThemeInfo
{
    typedef ibThemeEngine *(*Constructor)();

    // theme name and (user readable) description
    wxString name, desc;

    // the function to create a theme object
    Constructor ctor;

    // next node in the linked list or nullptr
    ibThemeInfo *next;

    // constructor for the struct itself
    ibThemeInfo(Constructor ctor, const wxString& name, const wxString& desc);
};

// ----------------------------------------------------------------------------
// macros
// ----------------------------------------------------------------------------

// to use a standard theme insert this macro into one of the application files:
// without it, an over optimizing linker may discard the object module
// containing the theme implementation entirely
#define WX_USE_THEME(themename)                                             \
    /* this indirection makes it possible to pass macro as the argument */  \
    WX_USE_THEME_IMPL(themename)

#define WX_USE_THEME_IMPL(themename)                                        \
    extern FRONTEND_API bool ibThemeUse##themename;                    \
    static struct ibThemeUserFor##themename                                 \
    {                                                                       \
        ibThemeUserFor##themename() { ibThemeUse##themename = true; }       \
    } ibThemeDoUse##themename

// to declare a new theme, this macro must be used in the class declaration
#define WX_DECLARE_THEME(themename)                                         \
    private:                                                                \
        static ibThemeInfo ms_info##themename;                              \
    public:                                                                 \
        const ibThemeInfo *GetThemeInfo() const                             \
            { return &ms_info##themename; }

// and this one must be inserted in the source file
#define WX_IMPLEMENT_THEME(classname, themename, themedesc)                 \
    FRONTEND_API bool ibThemeUse##themename = true;                    \
    ibThemeEngine *wxCtorFor##themename() { return new classname; }               \
    ibThemeInfo classname::ms_info##themename(wxCtorFor##themename,         \
                                              wxT( #themename ), themedesc)

// ----------------------------------------------------------------------------
// the only built-in theme is ours
#define wxUNIV_DEFAULT_THEME luna

#endif // _WX_UNIV_THEME_H_
