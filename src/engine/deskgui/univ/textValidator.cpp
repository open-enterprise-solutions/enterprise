// FORKED from the wx text validator (wxTextValidator -> ibTextValidator, mechanical ib prefixes). wx's finds its
// field as a wxTextCtrl, a wxComboBox or a wxComboCtrl only, so on the library's field it asserted and filtered
// nothing; this one finds it as the library's text control.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/common/valtext.cpp
// Purpose:     ibTextValidator
// Author:      Julian Smart
// Modified by: Francesco Montorsi
// Created:     04/01/98
// Copyright:   (c) Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "deskgui/univ/textValidator.h"

#if wxUSE_VALIDATORS

#ifndef WX_PRECOMP
  #include <stdio.h>
  #include "wx/log.h"
  #include "wx/utils.h"
  #include "wx/intl.h"
#endif

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "wx/clipbrd.h"

#include "deskgui/univ/ctrl/textCtrl.h"
#include "deskgui/univ/messageDialog.h"

// ----------------------------------------------------------------------------
// global helpers
// ----------------------------------------------------------------------------

static bool wxIsNumeric(const wxString& val)
{
    for ( const auto c : val )
    {
        // Allow for "," (French) as well as "." -- in future we should
        // use wxSystemSettings or other to do better localisation
        if ((!wxIsdigit(c)) &&
            (c != wxS('.')) && (c != wxS(',')) && (c != wxS('e')) &&
            (c != wxS('E')) && (c != wxS('+')) && (c != wxS('-')))
            return false;
    }
    return true;
}

// ----------------------------------------------------------------------------
// ibTextValidator
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibTextValidator, wxValidator);
wxBEGIN_EVENT_TABLE(ibTextValidator, wxValidator)
    EVT_CHAR(ibTextValidator::OnChar)
    EVT_TEXT_PASTE(wxID_ANY, ibTextValidator::OnPaste)
wxEND_EVENT_TABLE()

ibTextValidator::ibTextValidator(long style, wxString *val)
{
    m_stringValue = val;
    SetStyle(style);
}

ibTextValidator::ibTextValidator(const ibTextValidator& val)
    : wxValidator()
{
    Copy(val);
}

void ibTextValidator::SetStyle(long style)
{
    m_validatorStyle = style;
}

bool ibTextValidator::Copy(const ibTextValidator& val)
{
    wxValidator::Copy(val);

    m_validatorStyle = val.m_validatorStyle;
    m_stringValue    = val.m_stringValue;

    m_charIncludes = val.m_charIncludes;
    m_charExcludes = val.m_charExcludes;
    m_includes     = val.m_includes;
    m_excludes     = val.m_excludes;

    return true;
}

ibTextCtrl *ibTextValidator::GetTextEntry()
{
    // SEAM vs wx: the library's text control only - a combo box keeps its text in a child of its own
    if (ibTextCtrl* const text = wxDynamicCast(m_validatorWindow, ibTextCtrl))
        return text;

    wxFAIL_MSG("ibTextValidator can only be used with ibTextCtrl");

    return nullptr;
}

// Called when the value in the window must be validated.
// This function can pop up an error message.
bool ibTextValidator::Validate(wxWindow *parent)
{
    // If window is disabled, simply return
    if ( !m_validatorWindow->IsEnabled() )
        return true;

    ibTextCtrl * const text = GetTextEntry();
    if ( !text )
        return false;

    const wxString& errormsg = IsValid(text->GetValue());

    if ( !errormsg.empty() )
    {
        m_validatorWindow->SetFocus();
        ibMessageBox(errormsg, _("Validation conflict"),
                     wxOK | wxICON_EXCLAMATION, parent);

        return false;
    }

    return true;
}

// Called to transfer data to the window
bool ibTextValidator::TransferToWindow()
{
    if ( m_stringValue )
    {
        ibTextCtrl * const text = GetTextEntry();
        if ( !text )
            return false;

        text->SetValue(*m_stringValue);
    }

    return true;
}

// Called to transfer data to the window
bool ibTextValidator::TransferFromWindow()
{
    if ( m_stringValue )
    {
        ibTextCtrl * const text = GetTextEntry();
        if ( !text )
            return false;

        *m_stringValue = text->GetValue();
    }

    return true;
}

wxString ibTextValidator::IsValid(const wxString& str) const
{
    if ( HasFlag(wxFILTER_EMPTY) && str.empty() )
        return _("Required information entry is empty.");
    else if ( IsExcluded(str) )
        return ibString::Format(_("'%s' is one of the invalid strings"), str);
    else if ( !IsIncluded(str) )
        return ibString::Format(_("'%s' is not one of the valid strings"), str);

    // check the whole string for invalid chars.
    for ( wxString::const_iterator i = str.begin(), end = str.end();
          i != end; ++i )
    {
        if ( !IsValidChar(*i) )
        {
            return ibString::Format(
                _("'%s' contains invalid character(s)"), str);
        }
    }

    return wxString();
}


void ibTextValidator::SetCharIncludes(const wxString& chars)
{
    m_charIncludes.clear();

    AddCharIncludes(chars);
}

void ibTextValidator::AddCharIncludes(const wxString& chars)
{
    m_charIncludes += chars;
}

void ibTextValidator::SetCharExcludes(const wxString& chars)
{
    m_charExcludes.clear();

    AddCharExcludes(chars);
}

void ibTextValidator::AddCharExcludes(const wxString& chars)
{
    m_charExcludes += chars;
}

void ibTextValidator::SetIncludes(const wxArrayString& includes)
{
    // preserve compatibily with versions prior 3.1.3 which used m_includes
    // to store the list of char includes.
    if ( HasFlag(wxFILTER_INCLUDE_CHAR_LIST) )
    {
        for ( const auto& include : includes )
        {
            AddCharIncludes(include);
        }

        return;
    }

    m_includes = includes;
}

void ibTextValidator::AddInclude(const wxString& include)
{
    m_includes.push_back(include);
}

void ibTextValidator::SetExcludes(const wxArrayString& excludes)
{
    // preserve compatibily with versions prior 3.1.3 which used m_excludes
    // to store the list of char excludes.
    if ( HasFlag(wxFILTER_EXCLUDE_CHAR_LIST) )
    {
        for ( const auto& exclude : excludes )
        {
            AddCharExcludes(exclude);
        }

        return;
    }

    m_excludes = excludes;
}

void ibTextValidator::AddExclude(const wxString& exclude)
{
    m_excludes.push_back(exclude);
}

void ibTextValidator::OnChar(wxKeyEvent& event)
{
    // Let the event propagate by default.
    event.Skip();

    if (!m_validatorWindow)
        return;

    // We only filter normal, printable characters.
    const wxUniChar keyCode = event.GetUnicodeChar();

    // we don't filter special keys and delete
    if (keyCode < WXK_SPACE || keyCode == WXK_DELETE)
        return;

    // Filter out invalid characters
    if ( IsValidChar(keyCode) )
        return;

    if ( !wxValidator::IsSilent() )
        wxBell();

    // eat message
    event.Skip(false);
}

void ibTextValidator::OnPaste(wxClipboardTextEvent& event)
{
#if wxUSE_CLIPBOARD
    // Filter out invalid characters from the clipboard contents as it
    // shouldn't be possible to sneak them into the control in such a way.
    //
    // This seems better than not allowing to paste anything at all if there is
    // anything invalid on the clipboard, e.g. it is more user-friendly to omit
    // any trailing spaces in a control not allowing them than to refuse to
    // paste a string with some spaces into it completely.
    //
    // Out of abundance of caution also prefer to let the control do its own
    // thing if there are no invalid characters at all, as we can be sure it
    // does the right thing in all cases, while our code might not deal with
    // some edge cases correctly.
    wxClipboardLocker lock;
    wxTextDataObject data;
    wxTheClipboard->GetData(data);
    const wxString& text = data.GetText();

    wxString valid;
    valid.reserve(text.length());

    bool hasInvalid = false;

    // Examine all characters one by one.
    for ( const auto ch : text )
    {
        if ( IsValidChar(ch) )
        {
            valid += ch;
        }
        else // Invalid character.
        {
            // Only beep once per paste, not for every invalid character.
            if ( !hasInvalid && !wxValidator::IsSilent() )
                wxBell();

            hasInvalid = true;
        }
    }

    // If we can't let the control paste everything, do it ourselves.
    if ( hasInvalid )
    {
        ibTextCtrl * const entry = GetTextEntry();
        if ( entry )
        {
            entry->WriteText(valid);

            // Skip the call to wxEvent::Skip() below, preventing the normal
            // paste from happening.
            return;
        }
    }
#endif // wxUSE_CLIPBOARD

    event.Skip();
}

bool ibTextValidator::IsValidChar(const wxUniChar& c) const
{
    if ( !m_validatorStyle ) // no filtering if HasFlag(wxFILTER_NONE)
        return true;

    if ( IsCharExcluded(c) ) // disallow any char in the m_charExcludes.
        return false;

    if ( IsCharIncluded(c) ) // allow any char in the m_charIncludes.
        return true;

    if ( !HasFlag(wxFILTER_CC) )
    {
        // Validity is entirely determined by the exclude/include char lists
        // and this character is in neither, so consider that it is valid if
        // and only if we accept anything.
        return !HasFlag(wxFILTER_INCLUDE_CHAR_LIST);
    }

    if ( HasFlag(wxFILTER_SPACE) && wxIsspace(c) )
        return true;
    if ( HasFlag(wxFILTER_ASCII) && c.IsAscii() )
        return true;
    if ( HasFlag(wxFILTER_NUMERIC) && wxIsNumeric(c) )
        return true;
    if ( HasFlag(wxFILTER_ALPHANUMERIC) && wxIsalnum(c) )
        return true;
    if ( HasFlag(wxFILTER_ALPHA) && wxIsalpha(c) )
        return true;
    if ( HasFlag(wxFILTER_DIGITS) && wxIsdigit(c) )
        return true;
    if ( HasFlag(wxFILTER_XDIGITS) && wxIsxdigit(c) )
        return true;

    // If we are here, this means that the char c does not belong to any of the
    // character classes checked above (e.g. emoji chars) so just return false.

    return false;
}

// kept for compatibility reasons.
bool ibTextValidator::ContainsOnlyIncludedCharacters(const wxString& str) const
{
    for ( const auto ch : str )
    {
        if ( !IsCharIncluded(ch) )
            return false;
    }

    return true;
}

// kept for compatibility reasons.
bool ibTextValidator::ContainsExcludedCharacters(const wxString& str) const
{
    for ( const auto ch : str )
    {
        if (IsCharExcluded(ch))
            return true;
    }

    return false;
}

#endif // wxUSE_VALIDATORS
