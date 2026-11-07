// FORKED from wxWidgets: wxTextSizerWrapper (include/wx/textwrapper.h), of the library's static text (wx -> ib prefixes
// applied).

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/textwrapper.h
// Purpose:     declaration of wxTextWrapper class
// Author:      Vadim Zeitlin
// Created:     2009-05-31 (extracted from dlgcmn.cpp via wx/private/stattext.h)
// Copyright:   (c) 1999, 2009 Vadim Zeitlin <vadim@wxwidgets.org>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_TEXTSIZERWRAPPER_H_
#define _GUI_UNIV_TEXTSIZERWRAPPER_H_

#include <wx/textwrapper.h>

#include "gui/univ/ctrl/staticText.h"

// A class creating a sizer with one static text per line of text. Creation of
// the controls used for each line can be customized by overriding
// OnCreateLine() function.
//
// SEAM vs wx: wx's whole, but for its lines - wx's makes a wxStaticText of each; its OnCreateLine is the door wx left
// for this, so the rest is wx's own class under it
class ibTextSizerWrapper : public wxTextSizerWrapper
{
public:
    explicit ibTextSizerWrapper(wxWindow *win) : wxTextSizerWrapper(win) {}

protected:
    virtual wxWindow *OnCreateLine(const wxString& line) override
    {
        return new ibStaticText(GetParent(), wxID_ANY,
                                wxControl::EscapeMnemonics(line));
    }
};

#endif // _GUI_UNIV_TEXTSIZERWRAPPER_H_
