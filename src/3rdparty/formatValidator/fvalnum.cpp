/////////////////////////////////////////////////////////////////////////////
// Name:        fvalnum.cpp
// Purpose:     wxNumberValidator
// Author:      Manuel Martin
// Modified by: MM May 2017
// Created:     Mar 2003
// Licence:     wxWindows license
/////////////////////////////////////////////////////////////////////////////

#ifdef __GNUG__
#pragma implementation "fvalnum.h"
#endif

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
  #pragma hdrstop
#endif

#if wxUSE_VALIDATORS

#ifndef WX_PRECOMP
  #include  "wx/string.h"
  #include  "wx/intl.h"
  #include  "wx/msgdlg.h"
#endif

#include "fvalnum.h"

IMPLEMENT_DYNAMIC_CLASS(wxNumberValidator, wxValidator)

BEGIN_EVENT_TABLE(wxNumberValidator, wxValidator)
    EVT_KEY_DOWN(wxNumberValidator::OnKeyDown)
    EVT_CHAR(wxNumberValidator::OnChar)
    EVT_SET_FOCUS(wxNumberValidator::OnSetFocus)
    EVT_KILL_FOCUS(wxNumberValidator::OnKillFocus)
    EVT_IDLE(wxNumberValidator::OnIdle)
END_EVENT_TABLE()

/////////////////////////////////////////////////////////////////////////////
/*           How this code works
*
*  We have three "paths" for validation:
*  (a) For OnChar (or 'while typing')
*     OnChar(), DelAndSelection() and TestAndSet() are used
*  (b) For OnKillFocus and TAB/ENTER
*     ValidateQuiet() which also calls CheckMinMax()
*     SetFormatted() will be called if no error
*  (c) Validate() is called, which calls ValidateQuiet()
*
*  We can have two styles, so must be aware of the right one to use,
*  formatting and unformatting the value accordingly.
*  StyleMode() sets the right style and the 'strict' mode.
*
*  Other accepted strings are handled in TestOthers()
*
*  Setting what to do with an empty value is done in SetEmptyAs()
*  Testing an empty value is done in ValidateQuiet() and replacing it
*  is done in SetFormatted()
*
*  We consider a string as a valid number if it can be unformatted.
*  Depending on "strict" or "tolerant" the unformatting call may fail
*  or not; or fail but still get something useful.
*/
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
wxNumberValidator::wxNumberValidator(const wxString& valStyle,
                                   int behavior, wxString *val)
{
    m_validatorBehavior = behavior ;
    m_stringValue = val ;
    m_stringMin     = wxEmptyString;
    m_stringMax     = wxEmptyString;
    m_emptySurrogate = wxEmptyString;
    m_normalStyle = valStyle;
    m_typingStyle   = wxEmptyString;
    m_lastValue     = wxEmptyString;
    m_lastError     = wxEmptyString;
    m_otherStrings.Shrink();
    m_styleWorker = new wxFormatStringAsNumber;
    m_styleWorker->SetStyle(valStyle);
    m_colors.colOKForegn = wxNullColour;
    m_colors.colOKBackgn = wxNullColour;
    m_colors.colFaForegn = wxNullColour;
    m_colors.colFaBackgn = wxNullColour;
    m_MinLimit = false;
    m_MaxLimit = false;
    m_AllowEmpty = true;
    m_UseNormalStyle = true;
    m_ValueIsValid = false;
    m_LastCharWasNUMPADDEC = false;
    m_WantFocus = false;
    m_EscIsOn  = true;
    m_EscFired = false;
    m_CheckRes = 0;
}

wxNumberValidator::wxNumberValidator(const wxNumberValidator& val)
    : wxValidator()
{
    m_styleWorker = new wxFormatStringAsNumber;
    Copy(val);
}

bool wxNumberValidator::Copy(const wxNumberValidator& val)
{
    wxValidator::Copy(val);

    m_validatorBehavior     = val.m_validatorBehavior;
    m_stringValue           = val.m_stringValue;
    m_stringMin             = val.m_stringMin;
    m_stringMax             = val.m_stringMax;
    m_emptySurrogate        = val.m_emptySurrogate;
    m_normalStyle           = val.m_normalStyle;
    m_typingStyle           = val.m_typingStyle;
    m_lastValue             = val.m_lastValue;
    m_lastError             = val.m_lastError;
    m_otherStrings          = val.m_otherStrings;
    m_styleWorker->SetStyle(val.m_styleWorker->GetStyle());
    m_colors.colOKForegn    = val.m_colors.colOKForegn;
    m_colors.colOKBackgn    = val.m_colors.colOKBackgn;
    m_colors.colFaForegn    = val.m_colors.colFaForegn;
    m_colors.colFaBackgn    = val.m_colors.colFaBackgn;
    m_MinLimit              = val.m_MinLimit;
    m_MaxLimit              = val.m_MaxLimit;
    m_AllowEmpty            = val.m_AllowEmpty;
    m_UseNormalStyle        = val.m_UseNormalStyle;
    m_ValueIsValid          = val.m_ValueIsValid;
    m_LastCharWasNUMPADDEC  = val.m_LastCharWasNUMPADDEC;
    m_WantFocus             = val.m_WantFocus;
    m_EscIsOn               = val.m_EscIsOn;
    m_EscFired              = val.m_EscFired;
    m_CheckRes              = val.m_CheckRes;

    return true;
}

wxNumberValidator::~wxNumberValidator()
{
    wxDELETE(m_styleWorker);
}

/////////////////////////////////////////////////////////////////////////////
//wxTextEntry is only for single line text entry fields
wxTextEntry* wxNumberValidator::GetTextEntry() const
{
#if wxUSE_TEXTCTRL
    if ( wxTextCtrl *text = wxDynamicCast(m_validatorWindow, wxTextCtrl) )
        return text;
#endif // wxUSE_TEXTCTRL

#if wxUSE_COMBOBOX
    if ( wxComboBox *combo = wxDynamicCast(m_validatorWindow, wxComboBox) )
        return combo;
#endif // wxUSE_COMBOBOX

    //We can't work if there isn't a proper control.
    //Fails even in release mode.
    wxCHECK_MSG(false, NULL,
                 wxT("Can only be used with wxTextCtrl or wxComboBox"));
}

/////////////////////////////////////////////////////////////////////////////
// Called to transfer data from var to the window
bool wxNumberValidator::TransferToWindow(void)
{
    wxCHECK_MSG( m_stringValue, false,
                 wxT("No variable storage for validator") );

    wxTextEntry *control = GetTextEntry();
    if( !control )
        return false;

    if (m_validatorBehavior & wxTRANSFER_UNFORMATTED)
    {   //string at var is unformatted and we want to see a formatted one in control
        //First try other accepted strings.
        int beStrict = StyleMode(); //Set proper style
        wxString formatted = TestOthers(*m_stringValue, beStrict);
        if ( formatted.IsEmpty() )
        {   //supposing the string is unformatted, try formatting it now
            if ( m_styleWorker->FormatStr(*m_stringValue, formatted, beStrict,
                                          m_validatorBehavior & wxUF_ROUND) == -1 )
                control->SetValue(formatted);
            else //format failed
                control->SetValue(*m_stringValue); //the string "as is"
        }
        else
            control->SetValue(formatted);
    }
    else
        control->SetValue(*m_stringValue);

    //Update status
    m_CheckRes = 0;
    m_lastError = wxEmptyString;
    m_WantFocus = false;

    //return true even if value can't be formatted
    return true;
}

// Called to transfer data from the window to var
bool wxNumberValidator::TransferFromWindow(void)
{
    wxCHECK_MSG( m_stringValue, false,
                 wxT("No variable storage for validator") );

    //Here we can do some replacements: for an empty value, or when Min/max
    // are exceeded. This is done in GetFormatted()
    if (m_validatorBehavior & wxTRANSFER_UNFORMATTED)
    {   *m_stringValue = GetFormatted(fVALFORMAT_NONE);
        //Trim zeros if we have a number, not any string
        if ( (m_validatorBehavior & wxTZ_BOTH)
            && m_styleWorker->QCheck(*m_stringValue) == -1)
        {
            m_styleWorker->TrimZeros(*m_stringValue, m_validatorBehavior & wxTZ_BOTH);
        }
    }
    else
        *m_stringValue = GetFormatted(fVALFORMAT_NORMAL);

    return true;
}

/////////////////////////////////////////////////////////////////////////////
int wxNumberValidator::SetStyle(const wxString& valstyle)
{
    int res = m_styleWorker->SetStyle(valstyle);
    if (res == -1) //only replace the current style with a valid one
        m_normalStyle = valstyle;
    return res;
}

/////////////////////////////////////////////////////////////////////////////
int wxNumberValidator::SetStyleForTyping(const wxString& valOCstyle)
{
    //Empty style is not checked
    if ( valOCstyle.IsEmpty() )
    {   m_typingStyle = wxEmptyString;
        return -1;
    }

    wxString prevSty = m_styleWorker->GetStyle();
    //Try this style
    int res = m_styleWorker->SetStyle(valOCstyle);
    if (res == -1)
        m_typingStyle = valOCstyle;
    //Restore previous, currently used, style
    m_styleWorker->SetStyle(prevSty);

    return res;
}

/////////////////////////////////////////////////////////////////////////////
bool wxNumberValidator::SetEmptyAs(bool allowIt, const wxString& emptyAs)
{
    if ( !allowIt )
    {   // allowIt == false means "empty is invalid, the user must type something"
        m_AllowEmpty = allowIt;
        return true;
    }

    bool isOK = false;
    //A valid other accepted string?
    if( !TestOthers(emptyAs, wxUF_BESTRICT).IsEmpty() )
        isOK = true;
    //A valid, unformatted, C-locale number?
    if ( !isOK && m_styleWorker && m_styleWorker->QCheck(emptyAs) == -1 )
        isOK = true;

    if ( isOK )
    {   m_emptySurrogate = emptyAs;
        m_AllowEmpty = allowIt;
        return true;
    }

    return false; //emptyAs is invalid
}

/////////////////////////////////////////////////////////////////////////////
int wxNumberValidator::StyleMode()
{
    int beStrict = wxUF_BESTRICT;

    if ( !m_UseNormalStyle && !m_typingStyle.IsEmpty() )
    {   m_styleWorker->SetStyle(m_typingStyle);
        beStrict = wxUF_NOSTRICT;
    }
    else
    {   m_styleWorker->SetStyle(m_normalStyle);
        if (m_validatorBehavior & wxUF_NOSTRICT)
            beStrict = wxUF_NOSTRICT;
    }

    return beStrict;
}

/////////////////////////////////////////////////////////////////////////////
void wxNumberValidator::OnSetFocus(wxFocusEvent& event)
{
    event.Skip();
    m_WantFocus = false;
    m_EscFired = false;

    // If window is disabled, simply return
    if ( !m_validatorWindow->IsEnabled() )
        return;

    //Set default OS colors, not OK/FAiled ones
    SetForeBackColors(cNormal);

    if ( !m_UseNormalStyle || m_typingStyle.IsEmpty() )
        return;

    //When setting the focus the only thing we do is changing the format
    // of a number. No range-check, no replacing with a AlsoValid string.
    //We must also keep an invalid value unmodified.
    //SetFormatted() does checks and replacements. So, we can't use it here.
    wxTextEntry *control = GetTextEntry() ;
    if ( !control )
        return;
    wxString inControl( control->GetValue() );
    if ( !inControl.IsEmpty() )
    {   //If this may be an AlsoValid string, do nothing
        int beStrict = StyleMode(); //Set proper style and return proper [no]strict
        if ( TestOthers(inControl, beStrict).IsEmpty() )
        {   //Not an AlsoValid string, try a properly formatted number
            wxString unformatted;
            if ( m_styleWorker->UnFormatStr(inControl, unformatted, beStrict) == -1 )
            {   //Format with typing style, without calling the now unneeded QCheck()
                m_styleWorker->SetStyle(m_typingStyle);
                m_styleWorker->FormatStr(unformatted, inControl, wxUF_NOSTRICT,
                                         m_validatorBehavior & wxUF_ROUND);
                //Set value to control, without update event
                control->ChangeValue(inControl);
                m_ValueIsValid = true;
                m_UseNormalStyle = false;
                return; //avoid a second SetStyle()
            }
            else
                m_ValueIsValid = false;
        }
    }

    m_styleWorker->SetStyle(m_typingStyle);
    m_UseNormalStyle = false;
}

void wxNumberValidator::OnKillFocus(wxFocusEvent& event)
{
    event.Skip();

    // If window is disabled, simply return
    if ( !m_validatorWindow->IsEnabled() )
        return;

    m_EscFired = false;

    /* Execution arrives here because:
    * (a) The user has clicked in another window
    * (b) Other code made us to lose focus
    * (c) We handled TAB or ENTER events which made us lose focus
    *
    * In case of (c) we have already validated the value. It is possible that
    * between TAB and KillFocus events the control's value can be changed (i.e.
    * by the control for this validator, inside its own OnChar handler). So
    * we may need validate it now.
    * But if if value is the same as that of recent validation, we are doing it
    * twice. Even worst: if the value is invalid, we would show again the same
    * error message.
    * We prevent these effects comparing current and old values.
    *
    * We know we are in case (c) because m_WantFocus == true
    */
    if (   (m_validatorBehavior & wxVAL_ON_KILL_FOCUS)
        || (m_validatorBehavior & wxVAL_RETAIN_FOCUS)
        || !m_typingStyle.IsEmpty() )
    {
        wxTextEntry *control = GetTextEntry() ;
        if ( !m_WantFocus //we are not in case (c)
            || (m_WantFocus && control && !(control->GetValue().IsSameAs(m_lastValue)))
           )
        {   //We can't call Validate() here because we are loosing focus and
            //showing a MessageBox now will mess focus (and cursor, etc).
            //Instead, we call ValidateQuiet() and defer message for OnIdle
            ValidateQuiet();
        }

        if ( m_ValueIsValid ) //freshly set by ValidateQuiet()
        {   //When using two styles, and value is valid, we must re-format it
            SetFormatted(fVALFORMAT_NORMAL);
            SetForeBackColors(cOK); //colors for a valid string
            m_WantFocus = false; //don't try to regain focus
        }
        else
        {   SetForeBackColors(cFA); //colors for an invalid string
            m_WantFocus = true; //defer a possible RegainFocus until OnIdle
        }
    }
}

void wxNumberValidator::OnIdle(wxIdleEvent& event)
{
    event.Skip();
    //Be sure we don't have the focus
    if ( !m_WantFocus || m_validatorWindow->HasFocus() )
    {   m_WantFocus = false;
        return;
    }

    // m_WantFocus==true means OnKillFocus validation failed.
    //So let's show the error message
    MsgOrSound(m_validatorWindow->GetParent());

    //Regain focus
    if ( m_validatorBehavior & wxVAL_RETAIN_FOCUS )
        m_validatorWindow->SetFocus();

    m_WantFocus = false;
}

/////////////////////////////////////////////////////////////////////////////
void wxNumberValidator::OnKeyDown(wxKeyEvent& event)
{
    event.Skip();
    if ( !m_validatorWindow )
        return;

    //The only modifier we handle is "Shift"
    if ( event.HasModifiers() && event.GetModifiers() != wxMOD_SHIFT)
        return;

    int deltype = -1;

    switch ( event.GetKeyCode() )
    {
        /*We must be aware of the user has pressed numeric pad decimal key.
        * The following char-event will carry the translated char. In some
        * computers event.GetKeyCode() in this char-event may return a wrong
        * key code. Due to this, we will dismiss that translated char, and use
        * our own decimal separator instead.
        */
        case WXK_DECIMAL:
        case WXK_NUMPAD_DECIMAL:
            m_LastCharWasNUMPADDEC = true;
            break;

        case WXK_ESCAPE:
        case WXK_CANCEL:
            if (m_EscIsOn)
            {   //We handle the first Esc event restoring string at var.
                //Rest of consecutive Esc events have default processing.
                if ( !m_EscFired )
                {   //Back to the variable stored value
                    TransferToWindow();
                    m_EscFired = true;
                    event.Skip(false); //first time, eat this msg
                }
            }
            break;

        /*If the control hasn't  wxTE_PROCESS_ENTER or  wxTE_PROCESS_TAB
        * style, execution doesn't arrive here.
        * We may want to do validation here, but also allow navigation or
        * whatever is desired. This must be handled not here, but i.e. in
        * a wxTextCtrl derived control. See the sample.
        */
        case WXK_TAB:
        case WXK_NUMPAD_TAB:
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            m_EscFired = false;
            if ( (m_validatorBehavior & wxVAL_ON_ENTER_TAB)
                || !m_typingStyle.IsEmpty() )
            {   //use m_WantFocus to indicate Validate() that it is ourselves
                //calling it
                m_WantFocus = true;
                if ( !Validate(m_validatorWindow->GetParent()) )
                    return;
                SetFormatted(fVALFORMAT_CURRENT);
            }
            break;

        //Default processing for BACK and DELETE may be different. But we want
        // to process them once and only here.
        case WXK_BACK:
            deltype = -2;
        case WXK_DELETE:
        case WXK_NUMPAD_DELETE:
            m_EscFired = false;
            event.Skip(false); //no default processing
            long pos = DelAndSelection(deltype);
            wxTextEntry* control = GetTextEntry();
            if ( ((m_validatorBehavior & wxVAL_ON_CHAR) //only with this flag
                  || !m_typingStyle.IsEmpty()) //or special style
                 && pos != -1 ) //something changed
            {   pos = control->GetValue().Len() - pos;
                TestAndSet(control->GetValue(), control, pos);
            }
            else if (pos != -1)
                control->SetInsertionPoint(pos);
            break;
    }
}

/////////////////////////////////////////////////////////////////////////////
void wxNumberValidator::OnChar(wxKeyEvent& event)
{
    //If not handled here, allow this event to be processed somewhere else
    event.Skip();

    if ( !m_validatorWindow )
        return;

    //The only modifier we handle is "Shift"
    if ( event.HasModifiers() && event.GetModifiers() != wxMOD_SHIFT )
        return;

    //Reset 'Esc' status
    m_EscFired = false;

    //The char. Unicode or ASCII
    wxChar keyChar = '\0';
#if wxUSE_UNICODE
    if ( (int) event.GetUnicodeKey() == (int) WXK_NONE )
        return; //not a character
    keyChar = event.GetUnicodeKey();
#else // !wxUSE_UNICODE
    int kc = event.GetKeyCode();
    if ( kc < WXK_SPACE || kc >= WXK_DELETE )
        return; // Not an ASCII character neither.
    keyChar = (wxChar)kc;
#endif // wxUSE_UNICODE/!wxUSE_UNICODE

    //If an OnChar event was generated for these cases, don't process
    //them again. They have been already processed at OnKeyDown
    switch ( (int)keyChar )
    {   case WXK_ESCAPE:
        case WXK_CANCEL:
        case WXK_TAB:
        case WXK_NUMPAD_TAB:
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            event.Skip(true); //allow default processing
            return;
        case WXK_BACK:
        case WXK_DELETE:
        case WXK_NUMPAD_DELETE:
            event.Skip(false);
            return;
    }

    if ( !(m_validatorBehavior & wxVAL_ON_CHAR) && m_typingStyle.IsEmpty() )
        return;

    wxTextEntry* control = GetTextEntry();
    if ( !control )
        return;

    event.Skip(false); //This event will end here.

    //First handle the selection, if any
    long oldPos = DelAndSelection(0);
    if (oldPos == -1) //There was no selection
        oldPos = control->GetInsertionPoint();

    wxString testString( control->GetValue() );
    long rPos = testString.Len() - oldPos;
    //Compose new string
    if ( m_LastCharWasNUMPADDEC )
    {   //As a help, we insert the style's whole decimal separator
        testString.insert((size_t)oldPos, m_styleWorker->GetDecSep() );
        m_LastCharWasNUMPADDEC = false;
    }
    else //insert the char
        testString.insert((size_t)oldPos, keyChar);

    //Now, test it
    TestAndSet(testString, control, rPos);
}

/////////////////////////////////////////////////////////////////////////////
//Deletion-keys. Also, if there is a selection, removes it.
//'delType == -2' means "back delete key" (delete left char)
//'delType == -1' means "delete key" (delete right char)
//Returns the new insert position or '-1' which means "nothing done"
long wxNumberValidator::DelAndSelection(int delType)
{
    wxTextEntry* control = GetTextEntry();
    if ( !control )
        return 0;

    wxString sRes = control->GetValue();
    size_t oldLen = sRes.Len();
    long posC = control->GetInsertionPoint();
    long selFrom, selTo;
    control->GetSelection(&selFrom, &selTo);
    long selLen = selTo - selFrom;

    if ( selLen )
    {   //Remove selected text because pressing a key would make it disappear.
        sRes.erase(selFrom, selLen);
        posC = selFrom;
        control->SetSelection(posC, posC);
    }
    // 'back' key with no selection
    else if ( delType == -2 && posC > 0 )
        sRes.erase(--posC, 1);
    // 'delete' key with no selection
    else if ( delType == -1 && posC < (long)sRes.Len() )
        sRes.erase(posC, 1);

    //If we have erased something, update control, without wxEVT_COMMAND_TEXT_UPDATED
    if ( oldLen != sRes.Len() )
        control->ChangeValue(sRes);
    else
        posC = -1;

    return posC;
}

/////////////////////////////////////////////////////////////////////////////
//Test if the string is valid
//returns -1 (valid) or error-position
void wxNumberValidator::TestAndSet(const wxString& testStr,
                                   wxTextEntry* control, long rPos)
{
    /* The logic:
    *  'testStr' may be formed adding a new char, deleting some chars, or even
    *  adding some chars using copy&paste. Tracking those changes is a
    *  nightmare.
    *  Instead, We try to unformat testStr. If succeeds, we format and show it.
    *
    *  If "strict" behavior, and unformatting fails, we'll show an error,
    *  keeping control's value "as is".
    *
    *  If "tolerant", UnFormatStr() will give us an incomplete number
    *  (i.e. "-1.e" or just "-", etc.). We format it also in "tolerant" mode
    *  and get an incomplete number again, but formatted.
    */

    int beStrict = StyleMode();
    //First, try other valid values
    wxString unformatted = TestOthers(testStr, beStrict);
    if ( !unformatted.IsEmpty() )
    {   //We fill the control with the whole string
        control->SetValue(unformatted); //_with_ wxEVT_COMMAND_TEXT_UPDATED
        //Select the part that is not written yet
        control->SetSelection(testStr.Len(), unformatted.Len());
        //We finished, so return
        return;
    }

    //Check to see if this [number] is valid
    int res = m_styleWorker->UnFormatStr(testStr, unformatted, beStrict);
    //"tolerant" mode may fill 'unformatted' with something. "strict" will not.
    if (res != -1 && unformatted.Len() == 0)
    {   //We have a more important error than a 'typo'.
        //But we don't show a message, just sound if desired.
        control->SetInsertionPoint((long)res);
        if ( !wxValidator::IsSilent() )
            wxBell();
        return;
    }

    //In "tolerant" mode we show a formatted number, either a good value or
    // that "something" we got at unformatted
    wxString formatted = wxEmptyString;
    m_styleWorker->FormatStr(unformatted, formatted, beStrict,
                             m_validatorBehavior & wxUF_ROUND);
    control->SetValue(formatted); //_with_ wxEVT_COMMAND_TEXT_UPDATED

    //Guess the insertion point.
    //If Length changes, it may be at left, right or both sides of previous
    //position. If right-part is modified, we suppose a suffix is added.
    //Surely this is not perfect, but it's the best I figured out.
    if (testStr.Len() != formatted.Len() && testStr.Right(1) != formatted.Right(1))
        rPos = (long)testStr.Len() - rPos;
    else
        rPos = (long)formatted.Len() - rPos;
    if ( rPos < 0 ) rPos = 0;
    if ( rPos > (long)formatted.Len() ) rPos = (long)formatted.Len();
    control->SetInsertionPoint(rPos);
}

/////////////////////////////////////////////////////////////////////////////
//         Other valid strings
//Test the other valid string, returning a matched entry.
//In "strict mode" the full entry must be matched (case sensitive).
//In "tolerant mode" we accept an incomplete, no case sensitive, match.
//If there are more than one entry that matches, we return the first we found.
//If no matches are found, returns wxEmptyString.
wxString wxNumberValidator::TestOthers(const wxString& sToTest, int beStrict)
{
    if ( m_otherStrings.IsEmpty() || sToTest.IsEmpty() )
        return wxEmptyString;

    wxString sMatch;
    for (size_t i = 0; i < m_otherStrings.GetCount(); i++)
    {   //Empty entries are skipped
        sMatch = m_otherStrings.Item(i);
        if ( sMatch.IsEmpty() )
            continue;
        //Strict: full comparison
        if ( beStrict == wxUF_BESTRICT )
        {   if ( sToTest.IsSameAs(sMatch, true) )
                return sMatch;
        }
        else //tolerant
        {   if ( sToTest.IsSameAs(sMatch.Left(sToTest.Len()), false) )
                return sMatch;
        }
    }
    //No matched found.
    return wxEmptyString;
}

/////////////////////////////////////////////////////////////////////////////
void wxNumberValidator::MsgOrSound(wxWindow* parent)
{
    if ( m_validatorBehavior & wxVAL_NO_MSG_SHOWN )
    {   //We don't want an error message, but perhaps a sound
        if ( !wxValidator::IsSilent() )
            wxBell();
    }
    else
    {   wxMessageBox(m_lastError, _("Validation conflict"),
                 wxOK | wxICON_EXCLAMATION, parent);
    }
}

/////////////////////////////////////////////////////////////////////////////
//Validation with no noise and no message shown.
//Test against style. Check value to be in range.
//Returns -1 if no error, otherwise the error position.
int wxNumberValidator::ValidateQuiet()
{
    // If window is disabled, simply return
    if ( !m_validatorWindow->IsEnabled() )
        return -1;

    wxTextEntry *control = GetTextEntry() ;
    if( !control )
        return -1;

    m_lastValue = control->GetValue(); //store it
    m_lastError = wxEmptyString;

    //Check an empty value
    if ( m_lastValue.IsEmpty() )
    {   if ( !m_AllowEmpty  )
        {   //not allowed
            m_lastError = _("Empty value is not allowed");
            m_ValueIsValid = false;
            return 0;
        }
        //Yes, it's valid. And will replaced somewhere else.
        m_ValueIsValid = true;
        return -1; //Note no CheckMinMax() is done
    }

    int beStrict = StyleMode(); //Set proper style and return proper [no]strict

    //If we find an accepted entry, that's enough
    wxString chRes = TestOthers(m_lastValue, beStrict);
    if ( !chRes.IsEmpty() )
    {   m_ValueIsValid = true;
        return -1;
    }

    //No entry, so try a number
    wxString unformatted = wxEmptyString;
    m_ValueIsValid = true;
    int res = m_styleWorker->UnFormatStr(m_lastValue, unformatted, beStrict);
    if ( res == -1 )
    {   //Try to format the value with normal style
        wxString curSty = m_styleWorker->GetStyle(); //Store current style
        beStrict = wxUF_BESTRICT;
        if ( m_validatorBehavior & wxUF_NOSTRICT)
            beStrict = wxUF_NOSTRICT;
        m_styleWorker->SetStyle(m_normalStyle); //Set normal style

        wxString stmp;
        res = m_styleWorker->FormatStr(unformatted, stmp, beStrict,
                                       m_validatorBehavior & wxUF_ROUND);
        if ( res == -1)
        {   //Get the unformatted value to use
            m_styleWorker->UnFormatStr(stmp, unformatted, beStrict);
        }
        else //Formatting error message
            m_lastError = m_styleWorker->GetLastError();

        m_styleWorker->SetStyle(curSty);
    }
    else //UnFormatting error message
        m_lastError = m_styleWorker->GetLastError();

    bool cMinMax = true;
    if ( res == -1 )
    {   cMinMax = CheckMinMax(unformatted, chRes);
        if ( m_CheckRes == fCHE_LIMITTOMIN || m_CheckRes == fCHE_LIMITTOMAX )
            cMinMax = true; //make it valid. We'll replace the value later
    }

    if (res != -1 || !cMinMax)
    {   m_ValueIsValid = false;
        if ( m_lastError.IsEmpty() )
            m_lastError = chRes; //CheckMinMax() error message
        if (res == -1)
            res = m_lastValue.Len();
    }
    return res;
}

// Called when the value in the window must be validated.
// This function can pop up an error message.
bool wxNumberValidator::Validate(wxWindow* parent)
{
    int res = ValidateQuiet();

    if (res != -1) //validation failed
    {   //The control
        wxTextEntry *control = GetTextEntry() ;
        if( !control )
            return false;
        //Show a message
        MsgOrSound(parent);
        //If we are called from outside this validator (i.e. from a wxDialog)
        // we set focus
        if ( !m_WantFocus )
            m_validatorWindow->SetFocus();
        control->SetInsertionPoint((long)res);
        return false;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//This function is supposed to be called right after Validate{Quiet]().
//It uses the last results from validation and returns:
// * If value is invalid, the string without modifications
// * If the value is to be limited, the proper limit
// * If the value is empty, its replacement
// * The formatted/unformatted value, depending on 'formatType'
wxString wxNumberValidator::GetFormatted(int formatType)
{
    bool dummyRepl; //Our helper function needs this parameter
    return DoGetFormatted(formatType, dummyRepl);
}

wxString wxNumberValidator::DoGetFormatted(int formatType, bool &isReplaced)
{
    if ( !m_ValueIsValid )
        return m_lastValue;

    wxTextEntry *control = GetTextEntry() ;
    if( !control )
        return wxEmptyString;

    wxString inControl, sStr;
    isReplaced = false;
    int beStrict = 0;

    //Test first if we are asked to replace with Min or Max
    if ( m_CheckRes == fCHE_LIMITTOMIN || m_CheckRes == fCHE_LIMITTOMAX )
    {   //Min Max are unformatted
        isReplaced = true;
        inControl = (m_CheckRes == fCHE_LIMITTOMIN) ? m_stringMin : m_stringMax ;
    }
    else //work with control's value
    {
        inControl = control->GetValue() ;

        //We have recently validated an empty value. So, assume we may replace it.
        //m_emptySurrogate is either "", an AlsoValid string or an unformatted number.
        isReplaced = inControl.IsEmpty();
        if ( isReplaced )
            inControl = m_emptySurrogate; //unformatted

        //Test other allowed strings
        beStrict = StyleMode();
        sStr = TestOthers(inControl, beStrict);
        if ( !sStr.IsEmpty() )
            return sStr;

        if ( !isReplaced )
        {   //The unformatted string
            m_styleWorker->UnFormatStr(inControl, sStr, beStrict);
            inControl = sStr;
        }
    }

    //Store current style. Restore it later
    wxString curStyle = m_styleWorker->GetStyle();
    //Format or leave it "as is"
    if ( formatType != fVALFORMAT_NONE )
    {   if ( formatType == fVALFORMAT_NORMAL )
        {   m_styleWorker->SetStyle(m_normalStyle);
            beStrict = wxUF_BESTRICT;
            if (m_validatorBehavior & wxUF_NOSTRICT)
                beStrict = wxUF_NOSTRICT;
        }
        else if ( formatType == fVALFORMAT_TYPING )
        {   m_styleWorker->SetStyle(m_typingStyle);
            beStrict = wxUF_NOSTRICT;
        }
        //Format. We need the proper strict-mode because we can have
        //sub-styles for positive/negative/zero values
        m_styleWorker->FormatStr(inControl, sStr, beStrict,
                                 m_validatorBehavior & wxUF_ROUND);
        m_styleWorker->SetStyle(curStyle);
        return sStr;
    }
    else
        return inControl;
}

/////////////////////////////////////////////////////////////////////////////
//Changes the control's value formatting/unformatting it.
//Use it when you know there's no error. Otherwise, the string remains unmodified.
//It can also set formatted Min/Max values.
void wxNumberValidator::SetFormatted(int formatType)
{
    bool isReplaced;
    wxTextEntry *control = GetTextEntry() ;

    if ( m_ValueIsValid )
    {   //Get the final valid string
        if( !control )
            return;

        m_lastValue = DoGetFormatted(formatType, isReplaced);

        //Update status
        m_CheckRes = 0;
        m_lastError = wxEmptyString;
    }

    //Set desired style
    if ( formatType != fVALFORMAT_NONE )
    {   if ( formatType == fVALFORMAT_NORMAL )
        {   m_styleWorker->SetStyle(m_normalStyle);
            m_UseNormalStyle = true;
        }
        else if ( formatType == fVALFORMAT_TYPING )
        {   m_styleWorker->SetStyle(m_typingStyle);
            m_UseNormalStyle = false;
        }
    }

    //Update control
    if ( m_ValueIsValid )
    {   //If the "number" is not modified (no matter its format), don't send
        // update event
        if ( isReplaced )
            control->SetValue(m_lastValue);
        else
            control->ChangeValue(m_lastValue);
    }

    if ( formatType == fVALFORMAT_NONE )
    {   //Setting an unformatted value is against this validator sense. But the
        // user perhaps finds it useful.
        m_ValueIsValid = false; //unformatted means not style-checked. So, invalid
    }
}

/////////////////////////////////////////////////////////////////////////////
// Get the default colors, depending of the type of the control
bool wxNumberValidator::GetNormalColors(wxColor &fgColor, wxColor &bgColor)
{
#if wxUSE_TEXTCTRL
    if ( wxTextCtrl *text = wxDynamicCast(m_validatorWindow, wxTextCtrl) )
    {
        wxVisualAttributes viAtt = text->GetClassDefaultAttributes();
        fgColor = viAtt.colFg;
        bgColor = viAtt.colBg;
        return true;
    }
#endif // wxUSE_TEXTCTRL

#if wxUSE_COMBOBOX
    if ( wxComboBox *combo = wxDynamicCast(m_validatorWindow, wxComboBox) )
    {
        wxVisualAttributes viAtt = combo->GetClassDefaultAttributes();
        fgColor = viAtt.colFg;
        bgColor = viAtt.colBg;
        return true;
    }
#endif // wxUSE_COMBOBOX

    return false; //The control is not of the type we use here.
}

/////////////////////////////////////////////////////////////////////////////
// Change control's colors
void wxNumberValidator::SetForeBackColors(int colvalType)
{
    wxColor fgCol, bgCol;
    bool fgSet = true, bgSet = true;

    if ( colvalType == cOK )
    {   if ( m_colors.colOKForegn != wxNullColour )
            fgCol = m_colors.colOKForegn;
        else
            fgSet = false;
        if ( m_colors.colOKBackgn != wxNullColour )
            bgCol = m_colors.colOKBackgn;
        else
            bgSet = false;
    }
    else if ( colvalType == cFA )
    {   if ( m_colors.colFaForegn != wxNullColour )
            fgCol = m_colors.colFaForegn;
        else
            fgSet = false;
        if ( m_colors.colFaBackgn != wxNullColour )
            bgCol = m_colors.colFaBackgn;
        else
            bgSet = false;
    }
    else
    {   //Get default colors
        if ( !GetNormalColors(fgCol, bgCol) )
            return; //This control is of a type we don't use
    }

    //Set only if this is not wxNullColour
    if ( fgSet )
        m_validatorWindow->SetForegroundColour(fgCol);
    if ( bgSet )
        m_validatorWindow->SetBackgroundColour(bgCol);
    m_validatorWindow->Refresh();
}

/////////////////////////////////////////////////////////////////////////////
//                     Min and max values
bool wxNumberValidator::SetMin(const wxString& smin, bool asLimit)
{
    if ( m_styleWorker->QCheck(smin) == -1 )
    {   m_stringMin = smin;
        m_MinLimit = asLimit;
        return true;
    }

    return false;
}

bool wxNumberValidator::SetMax(const wxString& smax, bool asLimit)
{
    if ( m_styleWorker->QCheck(smax) == -1 )
    {   m_stringMax = smax;
        m_MaxLimit = asLimit;
        return true;
    }

    return false;
}

// Inform in 'm_CheckRes' the cause for error and in 'sRes' the message.
//Note that if Min or Max are limited, it does not matter here. If any of
// them are exceeded we still return false, but inform properly in m_CheckRes.
bool wxNumberValidator::CheckMinMax(const wxString& unformatted, wxString &sRes)
{
    m_CheckRes = 0;

    //If we have no limits, simply return
    if ( m_stringMin.IsEmpty() && m_stringMax.IsEmpty() )
        return true;

    //The value the validator transfers is formatted with normal style. This
    //means the value may be changed because of truncating or rounding.
    //So we first format with this normal style (to get those effects) and
    //unformat again to be able to compare the values.
    wxString unfMin, unfMax;
    wxString stmp;
    //Store current style
    wxString curSty = m_styleWorker->GetStyle();
    int beStrict = wxUF_BESTRICT;
    if ( m_validatorBehavior & wxUF_NOSTRICT)
        beStrict = wxUF_NOSTRICT;
    //Set normal style
    m_styleWorker->SetStyle(m_normalStyle);

    //Before calling this function, we suppose 'unformatted' has passed
    // the formatting/unformatting process. Min and Max must pass it now.

    //The min value
    if ( m_styleWorker->FormatStr(m_stringMin, stmp, beStrict,
                                  m_validatorBehavior & wxUF_ROUND) != -1)
    {   //Some problem with exponential
        sRes = _("Min. can't be formatted ");
        m_styleWorker->SetStyle(curSty);
        return false;
    }
    m_styleWorker->UnFormatStr(stmp, unfMin, beStrict);
    //The max value
    if ( m_styleWorker->FormatStr(m_stringMax, stmp, beStrict,
                                  m_validatorBehavior & wxUF_ROUND) != -1)
    {   //Some problem with exponential
        sRes = _("Max. can't be formatted ");
        m_styleWorker->SetStyle(curSty);
        return false;
    }
    m_styleWorker->UnFormatStr(stmp, unfMax, beStrict);

    stringParts sUnf;
    stringParts sTest;

    //If Min >= Max we test a excluded range
    bool isExcludedRange = false;
    if ( !unfMin.IsEmpty() && !unfMax.IsEmpty() )
    {   //Compare Min against Max
        sUnf.sDigits = unfMin;
        m_styleWorker->StringToParts(&sUnf);
        sTest.sDigits = unfMax;
        m_styleWorker->StringToParts(&sTest);
        if ( CompParts(sUnf, sTest) >= 0 )
            isExcludedRange = true;
    }

    //Convert unformatted to its pieces
    sUnf.sDigits = unformatted;
    m_styleWorker->StringToParts(&sUnf);

    long resMin = 0, resMax = 0;
    sRes = wxEmptyString;

    //Check min
    if ( !unfMin.IsEmpty() )
    {   sTest.sDigits = unfMin;
        m_styleWorker->StringToParts(&sTest);
        resMin = CompParts(sUnf, sTest);
        if ( resMin < 0 && !isExcludedRange )
        {   //If min is limited, tell it
            m_CheckRes = m_MinLimit ? fCHE_LIMITTOMIN : fCHE_MIN;
            //Form the error message
            sRes = _("Value is less than: ");
            //Format min string using normal style
            m_styleWorker->FormatStr(m_stringMin, stmp, beStrict,
                                     m_validatorBehavior & wxUF_ROUND);
            sRes += stmp;
            m_styleWorker->SetStyle(curSty);
            return false;
        }
    }

    //Check max
    if ( !unfMax.IsEmpty() )
    {   sTest.sDigits = unfMax;
        m_styleWorker->StringToParts(&sTest);
        resMax = CompParts(sUnf, sTest);
        if ( resMax > 0 && !isExcludedRange )
        {   //If max is limited, tell it
            m_CheckRes = m_MaxLimit ? fCHE_LIMITTOMAX : fCHE_MAX;
            //Form the error message
            sRes = _("Value is greater than: ");
            //Format max string using normal style
            m_styleWorker->FormatStr(m_stringMax, stmp, beStrict,
                                     m_validatorBehavior & wxUF_ROUND);
            sRes += stmp;
            m_styleWorker->SetStyle(curSty);
            return false;
        }
    }

    //Check excluded range
    //resMin = val - min        resMax = val - max
    if ( isExcludedRange && resMin <= 0 && resMax >= 0 )
    {   //Check if 'unformatted' is an allowed range-limit
        if ( (resMin == 0 && m_MinLimit) || (resMax == 0 && m_MaxLimit) )
        {   m_styleWorker->SetStyle(curSty);
            return true;
        }

        //Value is inside the forbidden range
        m_CheckRes = fCHE_EXCLUDED;
        //Form the error message
        if ( resMin == 0 && resMax == 0 ) //Min == Max == unformatted
            sRes = _("Value can not be\n");
        else
            sRes = _("Value can not be in the range from\n");
        //Format range string using normal style
        m_styleWorker->FormatStr(m_stringMax, stmp, beStrict,
                                 m_validatorBehavior & wxUF_ROUND);
        sRes += stmp;
        if ( resMin != 0 || resMax != 0 )
        {   sRes += _(" to ");
            m_styleWorker->FormatStr(m_stringMin, stmp, beStrict,
                                     m_validatorBehavior & wxUF_ROUND);
            sRes += stmp;
        }
        m_styleWorker->SetStyle(curSty);
        return false;
    }

    //Checks passed
    m_styleWorker->SetStyle(curSty);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
//If we allow a big number, we can't rely on hardware types (int, float, etc.)
//We do the comparison ourselves.

//Returns a negative, zero or positive vale, accordingly with sP1-sP2
long wxNumberValidator::CompParts(const stringParts& sP1, const stringParts& sP2)
{
    //We first check signs
    if ( sP1.sSign != sP2.sSign )
        return sP1.sSign ? 1 : -1;

    //Zero is special.
    // wxFormatStringAsNumber::StringToParts() made sP.sDigits = '0'
    if ( (wxChar)sP1.sDigits.GetChar(0) == wxT('0') )
    {
        if ( (wxChar)sP2.sDigits.GetChar(0) == wxT('0') )
            return 0; //both are truly zero
        return sP2.sSign ? -1 : 1;
    }
    if ( (wxChar)sP2.sDigits.GetChar(0) == wxT('0') )
        return sP1.sSign ? 1 : -1;

    //Now exponents
    long difE = sP1.sExp - sP2.sExp;
    if ( difE != 0 )
        return sP1.sSign ? difE : -difE;

    //Digit by digit comparison
    size_t len1 = sP1.sDigits.Len();
    size_t len2 = sP2.sDigits.Len();
    size_t pos = 0;
    wxChar c1 = wxT('\0');
    wxChar c2 = wxT('\0');

    while ( pos < len1 || pos < len2)
    {   //Shorter length must be padded with '0' at right side
        c1 = pos < len1 ? (wxChar)sP1.sDigits.GetChar(pos) : wxT('0');
        c2 = pos < len2 ? (wxChar)sP2.sDigits.GetChar(pos) : wxT('0');
        if ( c1 > c2 )
            return sP1.sSign ? 1 : -1;
        else if ( c1 < c2 )
            return sP1.sSign ? -1 : 1;
        pos++;
    }
    return 0;
}

#endif // wxUSE_VALIDATORS

