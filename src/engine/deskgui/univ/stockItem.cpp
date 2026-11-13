// FORKED from wxWidgets: wxGetStockLabel and wxGetStockHelpString (src/common/stockitem.cpp) - see stockItem.h.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/common/stockitem.cpp
// Purpose:     Stock buttons, menu and toolbar items labels
// Author:      Vaclav Slavik
// Created:     2004-08-15
// Copyright:   (c) Vaclav Slavik, 2004
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/stockItem.h"

#include <wx/accel.h>

wxString ibGetStockLabel(wxWindowID id, long flags)
{
    wxString stockLabel;

#ifdef __WXMSW__
    // special case: the "Cancel" button shouldn't have a mnemonic under MSW
    // for consistency with the native dialogs (which don't use any mnemonic
    // for it because it is already bound to Esc implicitly)
    if ( id == wxID_CANCEL )
        flags &= ~wxSTOCK_WITH_MNEMONIC;

    // Another one: "Help" in the context of a menu label has a different
    // translation for some languages (Italian), so try this first.
    if ( id == wxID_HELP && (flags & wxSTOCK_WITH_MNEMONIC) )
    {
        stockLabel = ibGETTEXT_IN_CONTEXT("standard Windows menu", "&Help");
        if ( !stockLabel.empty() )
            return stockLabel;
    }
#endif // __WXMSW__


    #define STOCKITEM(stockid, labelWithMnemonic, labelPlain)                 \
        case stockid:                                                         \
            if(flags & wxSTOCK_WITH_MNEMONIC)                                 \
                stockLabel = labelWithMnemonic;                               \
            else                                                              \
                stockLabel = labelPlain;                                      \
            break

    switch (id)
    {
        STOCKITEM(wxID_ABOUT,               _("&About"),           _("About"));
        STOCKITEM(wxID_ADD,                 _("Add"),                 _("Add"));
        STOCKITEM(wxID_APPLY,               _("&Apply"),              _("Apply"));
        STOCKITEM(wxID_BACKWARD,            _("&Back"),               _("Back"));
        STOCKITEM(wxID_BOLD,                _("&Bold"),               _("Bold"));
        STOCKITEM(wxID_BOTTOM,              _("&Bottom"),             _("Bottom"));
        STOCKITEM(wxID_CANCEL,              _("&Cancel"),             _("Cancel"));
        STOCKITEM(wxID_CDROM,               _("&CD-ROM"),             _("CD-ROM"));
        STOCKITEM(wxID_CLEAR,               _("&Clear"),              _("Clear"));
        STOCKITEM(wxID_CLOSE,               _("&Close"),              _("Close"));
        STOCKITEM(wxID_CONVERT,             _("&Convert"),            _("Convert"));
        STOCKITEM(wxID_COPY,                _("&Copy"),               _("Copy"));
        STOCKITEM(wxID_CUT,                 _("Cu&t"),                _("Cut"));
        STOCKITEM(wxID_DELETE,              _("&Delete"),             _("Delete"));
        STOCKITEM(wxID_DOWN,                _("&Down"),               _("Down"));
        STOCKITEM(wxID_EDIT,                _("&Edit"),               _("Edit"));
        STOCKITEM(wxID_EXECUTE,             _("&Execute"),            _("Execute"));
        STOCKITEM(wxID_EXIT,                _("&Quit"),               _("Quit"));
        STOCKITEM(wxID_FILE,                _("&File"),               _("File"));
        STOCKITEM(wxID_FIND,                _("&Find..."),            _("Find..."));
        STOCKITEM(wxID_FIRST,               _("&First"),              _("First"));
        STOCKITEM(wxID_FLOPPY,              _("&Floppy"),             _("Floppy"));
        STOCKITEM(wxID_FORWARD,             _("&Forward"),            _("Forward"));
        STOCKITEM(wxID_HARDDISK,            _("&Harddisk"),           _("Harddisk"));
        STOCKITEM(wxID_HELP,                _("&Help"),               _("Help"));
        STOCKITEM(wxID_HOME,                _("&Home"),               _("Home"));
        STOCKITEM(wxID_INDENT,              _("Indent"),              _("Indent"));
        STOCKITEM(wxID_INDEX,               _("&Index"),              _("Index"));
        STOCKITEM(wxID_INFO,                _("&Info"),               _("Info"));
        STOCKITEM(wxID_ITALIC,              _("&Italic"),             _("Italic"));
        STOCKITEM(wxID_JUMP_TO,             _("&Jump to"),            _("Jump to"));
        STOCKITEM(wxID_JUSTIFY_CENTER,      _("Centered"),            _("Centered"));
        STOCKITEM(wxID_JUSTIFY_FILL,        _("Justified"),           _("Justified"));
        STOCKITEM(wxID_JUSTIFY_LEFT,        _("Align Left"),          _("Align Left"));
        STOCKITEM(wxID_JUSTIFY_RIGHT,       _("Align Right"),         _("Align Right"));
        STOCKITEM(wxID_LAST,                _("&Last"),               _("Last"));
        STOCKITEM(wxID_NETWORK,             _("&Network"),            _("Network"));
        STOCKITEM(wxID_NEW,                 _("&New"),                _("New"));
        STOCKITEM(wxID_NO,                  _("&No"),                 _("No"));
        STOCKITEM(wxID_OK,                  _("&OK"),                 _("OK"));
        STOCKITEM(wxID_OPEN,                _("&Open..."),            _("Open..."));
        STOCKITEM(wxID_PASTE,               _("&Paste"),              _("Paste"));
        STOCKITEM(wxID_PREFERENCES,         _("&Preferences"),        _("Preferences"));
        STOCKITEM(wxID_PREVIEW,             _("Print previe&w..."),   _("Print preview..."));
        STOCKITEM(wxID_PRINT,               _("&Print..."),           _("Print..."));
        STOCKITEM(wxID_PROPERTIES,          _("&Properties"),         _("Properties"));
        STOCKITEM(wxID_REDO,                _("&Redo"),               _("Redo"));
        STOCKITEM(wxID_REFRESH,             _("Refresh"),             _("Refresh"));
        STOCKITEM(wxID_REMOVE,              _("Remove"),              _("Remove"));
        STOCKITEM(wxID_REPLACE,             _("Rep&lace..."),         _("Replace..."));
        STOCKITEM(wxID_REVERT_TO_SAVED,     _("Revert to Saved"),     _("Revert to Saved"));
        STOCKITEM(wxID_SAVE,                _("&Save"),               _("Save"));
        STOCKITEM(wxID_SAVEAS,              _("Save &As..."),         _("Save As..."));
        STOCKITEM(wxID_SELECTALL,           _("Select &All"),         _("Select All"));
        STOCKITEM(wxID_SELECT_COLOR,        _("&Color"),              _("Color"));
        STOCKITEM(wxID_SELECT_FONT,         _("&Font"),               _("Font"));
        STOCKITEM(wxID_SORT_ASCENDING,      _("&Ascending"),          _("Ascending"));
        STOCKITEM(wxID_SORT_DESCENDING,     _("&Descending"),         _("Descending"));
        STOCKITEM(wxID_SPELL_CHECK,         _("&Spell Check"),        _("Spell Check"));
        STOCKITEM(wxID_STOP,                _("&Stop"),               _("Stop"));
        STOCKITEM(wxID_STRIKETHROUGH,       _("&Strikethrough"),      _("Strikethrough"));
        STOCKITEM(wxID_TOP,                 _("&Top"),                _("Top"));
        STOCKITEM(wxID_UNDELETE,            _("Undelete"),            _("Undelete"));
        STOCKITEM(wxID_UNDERLINE,           _("&Underline"),          _("Underline"));
        STOCKITEM(wxID_UNDO,                _("&Undo"),               _("Undo"));
        STOCKITEM(wxID_UNINDENT,            _("&Unindent"),           _("Unindent"));
        STOCKITEM(wxID_UP,                  _("&Up"),                 _("Up"));
        STOCKITEM(wxID_YES,                 _("&Yes"),                _("Yes"));
        STOCKITEM(wxID_ZOOM_100,            _("&Actual Size"),        _("Actual Size"));
        STOCKITEM(wxID_ZOOM_FIT,            _("Zoom to &Fit"),        _("Zoom to Fit"));
        STOCKITEM(wxID_ZOOM_IN,             _("Zoom &In"),            _("Zoom In"));
        STOCKITEM(wxID_ZOOM_OUT,            _("Zoom &Out"),           _("Zoom Out"));

        default:
            wxFAIL_MSG( wxT("invalid stock item ID") );
            break;
    }

    #undef STOCKITEM

    if ( flags & wxSTOCK_WITHOUT_ELLIPSIS )
    {
        wxString baseLabel;
        if ( stockLabel.EndsWith("...", &baseLabel) )
            stockLabel = baseLabel;

        // accelerators only make sense for the menu items which should have
        // ellipsis too while wxSTOCK_WITHOUT_ELLIPSIS is mostly useful for
        // buttons which shouldn't have accelerators in their labels
        wxASSERT_MSG( !(flags & wxSTOCK_WITH_ACCELERATOR),
                        "labels without ellipsis shouldn't use accelerators" );
    }

#if wxUSE_ACCEL
    if ( !stockLabel.empty() && (flags & wxSTOCK_WITH_ACCELERATOR) )
    {
        wxAcceleratorEntry accel = wxGetStockAccelerator(id);
        if (accel.IsOk())
            stockLabel << wxT('\t') << accel.ToString();
    }
#endif // wxUSE_ACCEL

    return stockLabel;
}

wxString ibGetStockHelpString(wxWindowID id, wxStockHelpStringClient client)
{
    wxString stockHelp;

    #define STOCKITEM(stockid, ctx, helpstr)             \
        case stockid:                                    \
            if (client==ctx) stockHelp = helpstr;        \
            break;

    switch (id)
    {
        // NB: these help string should be not too specific as they could be used
        //     in completely different programs!
        STOCKITEM(wxID_ABOUT,    wxSTOCK_MENU, _("Show about dialog"))
        STOCKITEM(wxID_COPY,     wxSTOCK_MENU, _("Copy selection"))
        STOCKITEM(wxID_CUT,      wxSTOCK_MENU, _("Cut selection"))
        STOCKITEM(wxID_DELETE,   wxSTOCK_MENU, _("Delete selection"))
        STOCKITEM(wxID_FIND,     wxSTOCK_MENU, _("Find in document"))
        STOCKITEM(wxID_REPLACE,  wxSTOCK_MENU, _("Find and replace in document"))
        STOCKITEM(wxID_PASTE,    wxSTOCK_MENU, _("Paste selection"))
        STOCKITEM(wxID_EXIT,     wxSTOCK_MENU, _("Quit this program"))
        STOCKITEM(wxID_REDO,     wxSTOCK_MENU, _("Redo last action"))
        STOCKITEM(wxID_UNDO,     wxSTOCK_MENU, _("Undo last action"))
        STOCKITEM(wxID_NEW,      wxSTOCK_MENU, _("Create new document"))
        STOCKITEM(wxID_OPEN,     wxSTOCK_MENU, _("Open an existing document"))
        STOCKITEM(wxID_CLOSE,    wxSTOCK_MENU, _("Close current document"))
        STOCKITEM(wxID_SAVE,     wxSTOCK_MENU, _("Save current document"))
        STOCKITEM(wxID_SAVEAS,   wxSTOCK_MENU, _("Save current document with a different filename"))

        default:
            // there's no stock help string for this ID / client
            break;
    }

    #undef STOCKITEM

    return stockHelp;
}
