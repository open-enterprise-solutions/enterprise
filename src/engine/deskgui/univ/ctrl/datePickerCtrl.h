// FORKED from the wx GENERIC date picker (wxDatePickerCtrlGeneric -> ibDatePickerCtrl, wxCalendarComboPopup ->
// ibCalendarComboPopup, mechanical ib prefixes): a date's field whose drop button opens a month of days - the
// library's combo host (ibComboCtrl) with the library's calendar (ibCalendarCtrl) as its popup.

/////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/datectrl.h
// Purpose:     generic wxDatePickerCtrl implementation
// Author:      Andreas Pflug
// Created:     2005-01-19
// Copyright:   (c) 2005 Andreas Pflug <pgadmin@pse-consulting.de>
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_DATEPICKERCTRL_H_
#define _DESKGUI_UNIV_DATEPICKERCTRL_H_

#include "deskgui/deskgui.h"

#if wxUSE_DATEPICKCTRL

#include <wx/datectrl.h>      // wx's: the wxDP_* styles, wxDatePickerCtrlNameStr
#include <wx/dateevt.h>       // wxDateEvent, wxEVT_DATE_CHANGED
#include <wx/compositewin.h>
#include <wx/containr.h>

#include "deskgui/univ/ctrl/control.h"

class ibComboCtrl;
class ibCalendarCtrl;
class ibCalendarComboPopup;

// SEAM vs wx: wxDatePickerCtrlCommonBase<wxDateTimePickerCtrlBase> (wx/datectrl.h, wx/datetimectrl.h) rides the
// native wxControl chain - its value and range are this class's own here, no base declares them; the events it sends -
// wxDateEvent with wxEVT_DATE_CHANGED, the calendar's wxCalendarEvent - stay wx's.
class DESKGUI_API ibDatePickerCtrl
    : public wxCompositeWindow< wxNavigationEnabled<ibControl> >
{
public:
    // creating the control
    ibDatePickerCtrl() { Init(); }
    ibDatePickerCtrl(wxWindow *parent,
                     wxWindowID id,
                     const wxDateTime& date = wxDefaultDateTime,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                     const wxValidator& validator = wxDefaultValidator,
                     const wxString& name = wxDatePickerCtrlNameStr)
    {
        Init();

        (void)Create(parent, id, date, pos, size, style, validator, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxDateTime& date = wxDefaultDateTime,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDP_DEFAULT | wxDP_SHOWCENTURY,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxDatePickerCtrlNameStr);

    virtual ~ibDatePickerCtrl();

    virtual bool Destroy() override;

    // the date: invalid for none, with wxDP_ALLOWNONE
    void SetValue(const wxDateTime& date);
    wxDateTime GetValue() const;

    // the range the date is kept in
    bool GetRange(wxDateTime *dt1, wxDateTime *dt2) const;
    void SetRange(const wxDateTime &dt1, const wxDateTime &dt2);

    bool SetDateRange(const wxDateTime& lowerdate = wxDefaultDateTime,
                      const wxDateTime& upperdate = wxDefaultDateTime);

    // extra methods available only in this (generic) implementation
    ibCalendarCtrl *GetCalendar() const;

protected:
    virtual wxSize DoGetBestSize() const override;

private:
    virtual wxWindowList GetCompositeWindowParts() const override;

    void Init();

    void OnText(wxCommandEvent &event);
    void OnSize(wxSizeEvent& event);

    ibComboCtrl* m_combo;
    ibCalendarComboPopup* m_popup;

    wxDECLARE_EVENT_TABLE();
    wxDECLARE_DYNAMIC_CLASS(ibDatePickerCtrl);
    wxDECLARE_NO_COPY_CLASS(ibDatePickerCtrl);
};

#endif // wxUSE_DATEPICKCTRL

#endif // _DESKGUI_UNIV_DATEPICKERCTRL_H_
