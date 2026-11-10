// FORKED from the wx GENERIC date picker (src/generic/datectlg.cpp) - see datePickerCtrl.h.

/////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/datectlg.cpp
// Purpose:     generic wxDatePickerCtrlGeneric implementation
// Author:      Andreas Pflug
// Created:     2005-01-19
// Copyright:   (c) 2005 Andreas Pflug <pgadmin@pse-consulting.de>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/datePickerCtrl.h"

#if wxUSE_DATEPICKCTRL

#ifndef WX_PRECOMP
    #include <wx/intl.h>
    #include <wx/log.h>
#endif

#include <wx/uilocale.h>

#include "deskgui/univ/messageDialog.h"
#include "deskgui/univ/ctrl/calendarCtrl.h"
#include "deskgui/univ/ctrl/comboCtrl.h"
#include "deskgui/univ/ctrl/textCtrl.h"

// ----------------------------------------------------------------------------
// local classes
// ----------------------------------------------------------------------------

namespace
{

#if wxUSE_VALIDATORS

class DateValidator : public wxValidator
{
public:
    explicit DateValidator(const wxString& format) : m_format(format) {}

    DateValidator(const DateValidator& val) = default;

    virtual wxObject *Clone() const override
    {
        return new DateValidator(*this);
    }

    virtual bool Validate(wxWindow *parent) override
    {
        // We can only be used with ibComboCtrl, so just a static_cast<> would
        // be safe, but use checked cast to notice any problems in debug build.
        const wxString
            s = wxStaticCast(m_validatorWindow, ibComboCtrl)->GetValue();
        if ( s.empty() )
        {
            // There is no need to tell the user that an empty string is
            // invalid, this shouldn't be a surprise for them.
            return true;
        }

        wxDateTime dt;
        if ( !dt.ParseFormat(s, m_format) )
        {
            // SEAM vs wx: the library's message box, not the native one
            ibMessageBox
            (
                wxString::Format
                (
                    _("\"%s\" is not in the expected date format, "
                      "please enter it as e.g. \"%s\"."),
                    s, wxDateTime::Today().Format(m_format)
                ),
                _("Invalid date"),
                wxOK | wxICON_WARNING,
                parent
            );

            return false;
        }

        return true;
    }

    // We don't transfer any data, this validator is used only for validation.
    virtual bool TransferFromWindow() override { return true; }
    virtual bool TransferToWindow() override { return true; }

private:
    const wxString m_format;
};

#endif // wxUSE_VALIDATORS

} // anonymous namespace

class ibCalendarComboPopup : public ibCalendarCtrl,
                             public ibComboPopup
{
public:

    ibCalendarComboPopup() : ibCalendarCtrl(),
                             ibComboPopup()
    {
    }

    // NB: Don't create lazily since it didn't work that way before
    //     wxComboCtrl was used, and changing behaviour would almost
    //     certainly introduce new bugs.
    virtual bool Create(wxWindow* parent) override
    {
        if ( !ibCalendarCtrl::Create(parent, wxID_ANY, wxDefaultDateTime,
                              wxPoint(0, 0), wxDefaultSize,
                              wxCAL_SEQUENTIAL_MONTH_SELECTION
                              | wxCAL_SHOW_HOLIDAYS | wxBORDER_SUNKEN) )
            return false;

        SetFormat(GetLocaleDateFormat());

        m_useSize  = ibCalendarCtrl::GetBestSize();

        m_combo->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, &ibCalendarComboPopup::OnKillTextFocus, this);

        return true;
    }

    virtual wxSize GetAdjustedSize(int WXUNUSED(minWidth),
                                   int WXUNUSED(prefHeight),
                                   int WXUNUSED(maxHeight)) override
    {
        return m_useSize;
    }

    virtual wxWindow *GetControl() override { return this; }

    virtual void SetStringValue(const wxString& s) override
    {
        wxDateTime dt;
        if ( ParseDateTime(s, &dt) )
            SetDate(dt);
        //else: keep the old value
    }

    void SetDateValue(const wxDateTime& date)
    {
        if ( date.IsValid() )
        {
            m_combo->ChangeValue(date.Format(m_format));
            SetDate(date);
        }
        else // invalid date
        {
            wxASSERT_MSG( HasDPFlag(wxDP_ALLOWNONE),
                            wxT("this control must have a valid date") );

            m_combo->ChangeValue(wxEmptyString);
        }
    }

    bool IsTextEmpty() const
    {
        return m_combo->GetTextCtrl()->IsEmpty();
    }

    // This is public because it is used by ibDatePickerCtrl itself to
    // change the date when the text control field changes. The reason it's
    // done there and not in this class itself is mostly historic.
    void ChangeDateAndNotifyIfValid()
    {
        wxDateTime dt;
        if ( !ParseDateTime(m_combo->GetValue(), &dt) )
        {
            // The user must be in the process of updating the date, don't do
            // anything -- we'll take care of ensuring it's valid on focus loss
            // later.
            return;
        }

        if ( dt == GetDate() )
        {
            // No need to send event if the date hasn't changed.
            return;
        }

        // We change the date immediately, as it's more consistent with the
        // native MSW version and avoids another event on focus loss.
        SetDate(dt);

        SendDateEvent(dt);
    }

private:
    bool ParseDateTime(const wxString& s, wxDateTime* pDt) const
    {
        pDt->ParseFormat(s, m_format);
        if ( !pDt->IsValid() )
            return false;

        return true;
    }

    void SendDateEvent(const wxDateTime& dt)
    {
        // Sends both wxCalendarEvent and wxDateEvent
        wxWindow* datePicker = m_combo->GetParent();

        wxCalendarEvent cev(datePicker, dt, wxEVT_CALENDAR_SEL_CHANGED);
        datePicker->GetEventHandler()->ProcessEvent(cev);

        wxDateEvent event(datePicker, dt, wxEVT_DATE_CHANGED);
        datePicker->GetEventHandler()->ProcessEvent(event);
    }

    void OnCalKey(wxKeyEvent & ev)
    {
        if (ev.GetKeyCode() == WXK_ESCAPE && !ev.HasModifiers())
            m_combo->HidePopup();
        else
            ev.Skip();
    }

    void OnSelChange(wxCalendarEvent &ev)
    {
        m_combo->ChangeValue(GetDate().Format(m_format));

        if ( ev.GetEventType() == wxEVT_CALENDAR_DOUBLECLICKED )
        {
            m_combo->HidePopup();
        }

        SendDateEvent(GetDate());
    }

    void OnKillTextFocus(wxFocusEvent &ev)
    {
        ev.Skip();

        const wxDateTime& dtOld = GetDate();

        wxDateTime dt;
        wxString value = m_combo->GetValue();
        if ( !ParseDateTime(value, &dt) )
        {
            if ( !HasDPFlag(wxDP_ALLOWNONE) )
                dt = dtOld;
        }

        if ( dt.IsValid() )
        {
            // Set it at ibCalendarCtrl level.
            SetDate(dt);

            // And show it in the text field.
            m_combo->ChangeValue(GetStringValueFor(GetDate()));

            // And if the date has really changed, send an event about it.
            if ( dt != dtOld )
                SendDateEvent(dt);
        }
        else // Invalid date currently entered.
        {
            if ( HasDPFlag(wxDP_ALLOWNONE) )
            {
                // Clear the text part to indicate that the date is invalid.
                // Would it be better to indicate this in some more clear way,
                // e.g. maybe by using "[none]" or something like this?
                m_combo->ChangeValue(wxString());
            }
            else
            {
                // Restore the original value, as we can't have invalid value
                // in this control.
                m_combo->ChangeValue(GetStringValueFor(GetDate()));
            }
        }
    }

    bool HasDPFlag(int flag) const
    {
        return m_combo->GetParent()->HasFlag(flag);
    }

    // Return the format to be used for the dates shown by the control. This
    // functions honours wxDP_SHOWCENTURY flag.
    wxString GetLocaleDateFormat() const
    {
#if wxUSE_INTL
        wxString fmt = wxUILocale::GetCurrent().GetInfo(wxLOCALE_SHORT_DATE_FMT);
        if ( HasDPFlag(wxDP_SHOWCENTURY) )
            fmt.Replace("%y", "%Y");
#else // !wxUSE_INTL
        wxString fmt = wxS("%x");
#endif // wxUSE_INTL/!wxUSE_INTL

        // Also check if we can actually parse dates in this format because we
        // had several problems with unsupported format specifiers being used
        // in some locales date format strings in the past, and in this case
        // we'd just annoy the user with senseless messages about invalid dates
        // being entered when it's actually just our own bug.
        wxDateTime dt;
        if ( !dt.ParseFormat(wxDateTime::Now().Format(fmt), fmt) )
        {
            // If we can't parse the date in the format we're going to use, we
            // can't use it and have to fallback to something else -- this is
            // not ideal, but better than not allowing the user to enter any
            // dates at all.
            wxLogTrace("datectrl",
                       "Can't parse dates in format \"%s\", "
                       "using ISO 8601 as fallback",
                       fmt);

            fmt = HasDPFlag(wxDP_SHOWCENTURY) ? wxS("%Y-%m-%d")
                                              : wxS("%y-%m-%d");
        }

        return fmt;
    }

    bool SetFormat(const wxString& fmt)
    {
        m_format = fmt;

        if ( m_combo )
        {
#if wxUSE_VALIDATORS
            m_combo->SetValidator(DateValidator(m_format));
#endif // wxUSE_VALIDATORS

            if ( GetDate().IsValid() )
                m_combo->ChangeValue(GetDate().Format(m_format));
        }

        return true;
    }

    // returns either the given date representation using the current format or
    // an empty string if it's invalid
    wxString GetStringValueFor(const wxDateTime& dt) const
    {
        wxString val;
        if ( dt.IsValid() )
            val = dt.Format(m_format);

        return val;
    }

    wxSize          m_useSize;
    wxString        m_format;

    wxDECLARE_EVENT_TABLE();
};


wxBEGIN_EVENT_TABLE(ibCalendarComboPopup, ibCalendarCtrl)
    EVT_KEY_DOWN(ibCalendarComboPopup::OnCalKey)
    EVT_CALENDAR_SEL_CHANGED(wxID_ANY, ibCalendarComboPopup::OnSelChange)
    EVT_CALENDAR_PAGE_CHANGED(wxID_ANY, ibCalendarComboPopup::OnSelChange)
    EVT_CALENDAR(wxID_ANY, ibCalendarComboPopup::OnSelChange)
wxEND_EVENT_TABLE()


// ============================================================================
// ibDatePickerCtrl implementation
// ============================================================================

wxBEGIN_EVENT_TABLE(ibDatePickerCtrl, ibControl)
    EVT_TEXT(wxID_ANY, ibDatePickerCtrl::OnText)
    EVT_SIZE(ibDatePickerCtrl::OnSize)
wxEND_EVENT_TABLE()

wxIMPLEMENT_DYNAMIC_CLASS(ibDatePickerCtrl, ibControl);

// ----------------------------------------------------------------------------
// creation
// ----------------------------------------------------------------------------

bool ibDatePickerCtrl::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxDateTime& date,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxValidator& validator,
                              const wxString& name)
{
    wxASSERT_MSG( !(style & wxDP_SPIN),
                  wxT("wxDP_SPIN style not supported, use wxDP_DEFAULT") );

    // SEAM vs wx: the border is the combo's - the picker gives it its own (none given: the combo's default, a text
    // field's) and has none itself; wx gave the combo the platform's and added wxBORDER_NONE to the picker's style
    const long styleBorder = style & wxBORDER_MASK;

    if ( !ibControl::Create(parent, id, pos, size,
                            (style & ~wxBORDER_MASK) | wxCLIP_CHILDREN | wxWANTS_CHARS | wxBORDER_NONE,
                            validator, name) )
    {
        return false;
    }

    InheritAttributes();

    m_combo = new ibComboCtrl(this, -1, wxEmptyString,
                              wxDefaultPosition, wxDefaultSize, styleBorder);

    m_popup = new ibCalendarComboPopup();

    m_combo->SetPopupControl(m_popup);

    m_popup->SetDateValue(date.IsValid() ? date : wxDateTime::Today());

    SetInitialSize(size);

    return true;
}


void ibDatePickerCtrl::Init()
{
    m_combo = nullptr;
    m_popup = nullptr;
}

ibDatePickerCtrl::~ibDatePickerCtrl()
{
}

bool ibDatePickerCtrl::Destroy()
{
    if ( m_combo )
        m_combo->Destroy();

    m_combo = nullptr;
    m_popup = nullptr;

    return ibControl::Destroy();
}

// ----------------------------------------------------------------------------
// overridden base class methods
// ----------------------------------------------------------------------------

// SEAM vs wx: the drop button is a square of the value's height (ibComboCtrl::GetButtonRect), one pixel apart from
// the text (GetTextRect), the frame the combo's - wx asked its combo for the button's size
wxSize ibDatePickerCtrl::DoGetBestSize() const
{
    ibTextCtrl* const text = m_combo->GetTextCtrl();
    int w;
    text->GetTextExtent(text->GetValue(), &w, nullptr);
    const wxSize sizeText = text->GetSizeFromTextSize(w + 1);

    return wxSize(sizeText.x + 1 + sizeText.y, sizeText.y) + m_combo->GetWindowBorderSize();
}

wxWindowList ibDatePickerCtrl::GetCompositeWindowParts() const
{
    wxWindowList parts;
    parts.push_back(m_combo);
    parts.push_back(m_popup);
    return parts;
}

// ----------------------------------------------------------------------------
// ibDatePickerCtrl API
// ----------------------------------------------------------------------------

bool
ibDatePickerCtrl::SetDateRange(const wxDateTime& lowerdate,
                               const wxDateTime& upperdate)
{
    if ( !m_popup->SetDateRange(lowerdate, upperdate) )
        return false;

    // If the limits were, check that our current value lies between them and
    // adjust it if it doesn't.
    const wxDateTime old = GetValue();
    if ( old.IsValid() )
    {
        if ( lowerdate.IsValid() && old < lowerdate )
            SetValue(lowerdate);
        else if ( upperdate.IsValid() && old > upperdate )
            SetValue(upperdate);
    }

    return true;
}


wxDateTime ibDatePickerCtrl::GetValue() const
{
    if ( HasFlag(wxDP_ALLOWNONE) && m_popup->IsTextEmpty() )
        return wxInvalidDateTime;
    return m_popup->GetDate();
}


void ibDatePickerCtrl::SetValue(const wxDateTime& date)
{
    m_popup->SetDateValue(date);
}


bool ibDatePickerCtrl::GetRange(wxDateTime *dt1, wxDateTime *dt2) const
{
    return m_popup->GetDateRange(dt1, dt2);
}


void
ibDatePickerCtrl::SetRange(const wxDateTime &dt1, const wxDateTime &dt2)
{
    SetDateRange(dt1, dt2);
}

ibCalendarCtrl *ibDatePickerCtrl::GetCalendar() const
{
    return m_popup;
}

// ----------------------------------------------------------------------------
// event handlers
// ----------------------------------------------------------------------------


void ibDatePickerCtrl::OnSize(wxSizeEvent& event)
{
    if ( m_combo )
        m_combo->SetSize(GetClientSize());

    event.Skip();
}


void ibDatePickerCtrl::OnText(wxCommandEvent &ev)
{
    ev.SetEventObject(this);
    ev.SetId(GetId());
    GetParent()->GetEventHandler()->ProcessEvent(ev);

    if ( m_popup )
        m_popup->ChangeDateAndNotifyIfValid();
}

#endif // wxUSE_DATEPICKCTRL
