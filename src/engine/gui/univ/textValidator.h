// FORKED from the wx text validator (wxTextValidator -> ibTextValidator, mechanical ib prefixes). wx's finds its
// field as a wxTextCtrl, a wxComboBox or a wxComboCtrl only, so on the library's field it asserted and filtered
// nothing; this one finds it as the library's text control.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/valtext.h
// Purpose:     ibTextValidator class
// Author:      Julian Smart
// Modified by: Francesco Montorsi
// Created:     29/01/98
// Copyright:   (c) 1998 Julian Smart
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _GUI_UNIV_TEXTVALIDATOR_H_
#define _GUI_UNIV_TEXTVALIDATOR_H_

#include "gui/gui.h"

#if wxUSE_VALIDATORS

class ibTextCtrl;

#include <wx/validate.h>
#include <wx/valtext.h>       // wxTextValidatorStyle, the wxFILTER_* styles

// ----------------------------------------------------------------------------
// ibTextValidator
// ----------------------------------------------------------------------------

class GUI_API ibTextValidator: public wxValidator
{
public:
    ibTextValidator(long style = wxFILTER_NONE, wxString *val = nullptr);
    ibTextValidator(const ibTextValidator& val);

    virtual ~ibTextValidator() = default;

    // Make a clone of this validator (or return nullptr) - currently necessary
    // if you're passing a reference to a validator.
    // Another possibility is to always pass a pointer to a new validator
    // (so the calling code can use a copy constructor of the relevant class).
    wxNODISCARD virtual wxObject *Clone() const override { return new ibTextValidator(*this); }
    bool Copy(const ibTextValidator& val);

    // Called when the value in the window must be validated.
    // This function can pop up an error message.
    virtual bool Validate(wxWindow *parent) override;

    // Called to transfer data to the window
    virtual bool TransferToWindow() override;

    // Called to transfer data from the window
    virtual bool TransferFromWindow() override;

    // Filter keystrokes
    void OnChar(wxKeyEvent& event);

    // ACCESSORS
    inline long GetStyle() const { return m_validatorStyle; }
    void SetStyle(long style);

    ibTextCtrl *GetTextEntry();

    // strings & chars inclusions:
    // ---------------------------

    void SetCharIncludes(const wxString& chars);
    void AddCharIncludes(const wxString& chars);

    void SetIncludes(const wxArrayString& includes);
    void AddInclude(const wxString& include);

    const wxArrayString& GetIncludes() const { return m_includes; }
    wxString GetCharIncludes() const { return m_charIncludes; }

    // strings & chars exclusions:
    // ---------------------------

    void SetCharExcludes(const wxString& chars);
    void AddCharExcludes(const wxString& chars);

    void SetExcludes(const wxArrayString& excludes);
    void AddExclude(const wxString& exclude);

    const wxArrayString& GetExcludes() const { return m_excludes; }
    wxString GetCharExcludes() const { return m_charExcludes; }

    bool HasFlag(wxTextValidatorStyle style) const
        { return (m_validatorStyle & style) != 0; }

    // implementation only
    // --------------------

    // returns the error message if the contents of 'str' are invalid
    virtual wxString IsValid(const wxString& str) const;

protected:

    bool IsCharIncluded(const wxUniChar& c) const
    {
        return m_charIncludes.find(c) != wxString::npos;
    }

    bool IsCharExcluded(const wxUniChar& c) const
    {
        return m_charExcludes.find(c) != wxString::npos;
    }

    bool IsIncluded(const wxString& str) const
    {
        if ( HasFlag(wxFILTER_INCLUDE_LIST) )
            return m_includes.Index(str) != wxNOT_FOUND;

        // m_includes should be ignored (i.e. return true)
        // if the style is not set.
        return true;
    }

    bool IsExcluded(const wxString& str) const
    {
        return m_excludes.Index(str) != wxNOT_FOUND;
    }

    // returns false if the character is invalid
    bool IsValidChar(const wxUniChar& c) const;

    // These two functions (undocumented now) are kept for compatibility reasons.
    bool ContainsOnlyIncludedCharacters(const wxString& val) const;
    bool ContainsExcludedCharacters(const wxString& val) const;

protected:
    long                 m_validatorStyle;
    wxString*            m_stringValue;
    wxString             m_charIncludes;
    wxString             m_charExcludes;
    wxArrayString        m_includes;
    wxArrayString        m_excludes;

private:
    void OnPaste(wxClipboardTextEvent& event);

    wxDECLARE_NO_ASSIGN_CLASS(ibTextValidator);
    wxDECLARE_DYNAMIC_CLASS(ibTextValidator);
    wxDECLARE_EVENT_TABLE();
};

#endif // wxUSE_VALIDATORS

#endif // _GUI_UNIV_TEXTVALIDATOR_H_
