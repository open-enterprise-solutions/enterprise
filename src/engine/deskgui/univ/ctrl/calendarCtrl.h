// FORKED from the wx GENERIC calendar (wxGenericCalendarCtrl -> ibCalendarCtrl, mechanical ib prefixes), with the
// parts of wxCalendarCtrlBase it stands on: the month of days the date field drops down and the date-and-time popup
// of a form's field shows. The generic calendar is already custom-drawn - the port is the usual deskgui seam work:
// ibControl base, theme colours, the client area inside the engine's border, the library's month choice and year spin.

///////////////////////////////////////////////////////////////////////////////
// Name:        wx/generic/calctrlg.h
// Purpose:     generic implementation of date-picker control
// Author:      Vadim Zeitlin
// Created:     29.12.99
// Copyright:   (c) 1999 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

#ifndef _DESKGUI_UNIV_CALENDARCTRL_H_
#define _DESKGUI_UNIV_CALENDARCTRL_H_

#include "deskgui/deskgui.h"

#if wxUSE_CALENDARCTRL

// wx's: the wxCAL_* styles, wxCalendarHitTestResult, wxCalendarDateAttr, wxCalendarEvent and its wxEVT_CALENDAR_*
#include <wx/calctrl.h>

#include "deskgui/univ/ctrl/control.h"

class ibChoice;
class ibSpinCtrl;
class ibStaticText;

class WXDLLIMPEXP_FWD_CORE wxSpinEvent;

// ----------------------------------------------------------------------------
// ibCalendarCtrl: a control showing one month of days
// ----------------------------------------------------------------------------

// SEAM vs wx: wxCalendarCtrlBase (wx/calctrl.h) rides the native wxControl chain - its members the generic calendar
// uses are folded in here (AllowMonthChange, EnableHolidayDisplay, GenerateEvent, the holidays and the first day of the
// week); the events it sends - wxCalendarEvent with wxEVT_CALENDAR_* - stay wx's.
class DESKGUI_API ibCalendarCtrl : public ibControl
{
public:
    // construction
    ibCalendarCtrl() { Init(); }
    ibCalendarCtrl(wxWindow *parent,
                   wxWindowID id,
                   const wxDateTime& date = wxDefaultDateTime,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   long style = wxCAL_SHOW_HOLIDAYS,
                   const wxString& name = wxASCII_STR(wxCalendarNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxDateTime& date = wxDefaultDateTime,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxCAL_SHOW_HOLIDAYS,
                const wxString& name = wxASCII_STR(wxCalendarNameStr));

    virtual ~ibCalendarCtrl();

    virtual bool Destroy() override;

    // forward these functions to all subcontrols
    virtual bool Enable(bool enable = true) override;
    virtual bool Show(bool show = true) override;

    virtual void SetWindowStyleFlag(long style) override;

    virtual wxVisualAttributes GetDefaultAttributes() const override
        { return GetClassDefaultAttributes(GetWindowVariant()); }

    // set/get the current date
    // ------------------------

    bool SetDate(const wxDateTime& date);
    wxDateTime GetDate() const { return m_date; }

    // set/get the range in which selection can occur
    // ---------------------------------------------

    bool SetDateRange(const wxDateTime& lowerdate = wxDefaultDateTime,
                      const wxDateTime& upperdate = wxDefaultDateTime);

    bool GetDateRange(wxDateTime *lowerdate, wxDateTime *upperdate) const;

    bool SetLowerDateLimit(const wxDateTime& date = wxDefaultDateTime);
    const wxDateTime& GetLowerDateLimit() const { return m_lowdate; }
    bool SetUpperDateLimit(const wxDateTime& date = wxDefaultDateTime);
    const wxDateTime& GetUpperDateLimit() const { return m_highdate; }

    // calendar mode
    // -------------

    // some calendar styles can't be changed after the control creation by
    // just using SetWindowStyle() and Refresh() and the functions below
    // should be used instead for them

    // corresponds to wxCAL_NO_MONTH_CHANGE bit
    bool AllowMonthChange() const { return !HasFlag(wxCAL_NO_MONTH_CHANGE); }
    bool EnableMonthChange(bool enable = true);

    // corresponds to wxCAL_NO_YEAR_CHANGE bit, deprecated, generic only
    void EnableYearChange(bool enable = true);

    // corresponds to wxCAL_SHOW_HOLIDAYS bit
    void EnableHolidayDisplay(bool display = true);

    // customization
    // -------------

    // mark or unmark the day
    void Mark(size_t day, bool mark);

    // header colours are used for painting the weekdays at the top
    void SetHeaderColours(const wxColour& colFg, const wxColour& colBg)
    {
        m_colHeaderFg = colFg;
        m_colHeaderBg = colBg;
    }

    const wxColour& GetHeaderColourFg() const { return m_colHeaderFg; }
    const wxColour& GetHeaderColourBg() const { return m_colHeaderBg; }

    // highlight colour is used for the currently selected date
    void SetHighlightColours(const wxColour& colFg, const wxColour& colBg)
    {
        m_colHighlightFg = colFg;
        m_colHighlightBg = colBg;
    }

    const wxColour& GetHighlightColourFg() const { return m_colHighlightFg; }
    const wxColour& GetHighlightColourBg() const { return m_colHighlightBg; }

    // holiday colour is used for the holidays (if style & wxCAL_SHOW_HOLIDAYS)
    void SetHolidayColours(const wxColour& colFg, const wxColour& colBg)
    {
        m_colHolidayFg = colFg;
        m_colHolidayBg = colBg;
    }

    const wxColour& GetHolidayColourFg() const { return m_colHolidayFg; }
    const wxColour& GetHolidayColourBg() const { return m_colHolidayBg; }

    // an item without custom attributes is drawn with the default colours and
    // font and without border, setting custom attributes allows to modify this
    //
    // the day parameter should be in 1..31 range, for days 29, 30, 31 the
    // corresponding attribute is just unused if there is no such day in the
    // current month

    wxCalendarDateAttr *GetAttr(size_t day) const
    {
        wxCHECK_MSG( day > 0 && day < 32, nullptr, wxT("invalid day") );

        return m_attrs[day - 1];
    }

    void SetAttr(size_t day, wxCalendarDateAttr *attr)
    {
        wxCHECK_RET( day > 0 && day < 32, wxT("invalid day") );

        delete m_attrs[day - 1];
        m_attrs[day - 1] = attr;
    }

    void ResetAttr(size_t day) { SetAttr(day, nullptr); }

    void SetHoliday(size_t day);

    // returns one of wxCAL_HITTEST_XXX constants and fills either date or wd
    // with the corresponding value (none for NOWHERE, the date for DAY and wd
    // for HEADER)
    wxCalendarHitTestResult HitTest(const wxPoint& pos,
                                    wxDateTime *date = nullptr,
                                    wxDateTime::WeekDay *wd = nullptr);

    // generate the given calendar event, return true if it was processed
    bool GenerateEvent(wxEventType type)
    {
        wxCalendarEvent event(this, GetDate(), type);
        return HandleWindowEvent(event);
    }

    // get the currently shown control for month/year
    ibControl *GetMonthControl() const;
    ibControl *GetYearControl() const;

    static wxVisualAttributes
    GetClassDefaultAttributes(wxWindowVariant variant = wxWINDOW_VARIANT_NORMAL);

protected:
    // override some base class virtuals
    virtual wxSize DoGetBestSize() const override;
    virtual void DoMoveWindow(int x, int y, int width, int height) override;

private:
    // common part of all ctors
    void Init();

    // startup colours and reinitialization after colour changes in system
    void InitColours();

    // event handlers
    void OnPaint(wxPaintEvent& event);
    void OnClick(wxMouseEvent& event);
    void OnDClick(wxMouseEvent& event);
    void OnWheel(wxMouseEvent& event);
    void OnChar(wxKeyEvent& event);
    void OnMonthChange(wxCommandEvent& event);
    void OnSysColourChanged(wxSysColourChangedEvent& event);

    void HandleYearChange(wxCommandEvent& event);
    void OnYearChange(wxSpinEvent& event);
    void OnYearTextChange(wxCommandEvent& event);

    // (re)calc m_widthCol and m_heightRow
    void RecalcGeometry();

    // set the date and send the notification
    void SetDateAndNotify(const wxDateTime& date);

    // generate the events for the selection, page (year or month) and the day changes (wxCalendarCtrlBase's), the
    // date was dateOld; return true if the page changed
    bool GenerateAllChangeEvents(const wxDateTime& dateOld);

    // get the week (row, in range 1..6) for the given date
    size_t GetWeek(const wxDateTime& date) const;

    // get the date from which we start drawing days
    wxDateTime GetStartDate() const;

    // the week starts on Monday: wxCAL_MONDAY_FIRST, wxCAL_SUNDAY_FIRST or, neither given, the locale's
    // (wxCalendarCtrlBase's)
    bool WeekStartsOnMonday() const;

    // get the first/last days of the week corresponding to the current style
    wxDateTime::WeekDay GetWeekStart() const
    {
        return WeekStartsOnMonday() ? wxDateTime::Mon
                                    : wxDateTime::Sun;
    }

    wxDateTime::WeekDay GetWeekEnd() const
    {
        return WeekStartsOnMonday() ? wxDateTime::Sun
                                    : wxDateTime::Sat;
    }

    // is this date shown?
    bool IsDateShown(const wxDateTime& date) const;

    // is this date in the given range?
    bool IsDateInRange(const wxDateTime& date) const;

    // adjust the date to the lower or upper limit if it's outside of it, return true if it was changed
    bool AdjustDateToRange(wxDateTime *date) const;

    // redraw the given date
    void RefreshDate(const wxDateTime& date);

    // change the date inside the same month/year
    void ChangeDay(const wxDateTime& date);

    // deprecated
    bool AllowYearChange() const
    {
        return !(GetWindowStyle() & wxCAL_NO_YEAR_CHANGE);
    }

    // show the correct controls
    void ShowCurrentControls();

    // create the month choice and year spin controls
    void CreateMonthChoice();
    void CreateYearSpinCtrl();

    // the holidays of the shown month marked, if wxCAL_SHOW_HOLIDAYS (wxCalendarCtrlBase's); return true if marked
    bool SetHolidayAttrs();

    // reset all holidays
    void ResetHolidayAttrs();

    // the holidays are drawn anew
    void RefreshHolidays() { Refresh(); }

    // OnPaint helper-methods

    // Highlight the [fromdate : todate] range using pen and brush
    void HighlightRange(wxDC* dc, const wxDateTime& fromdate, const wxDateTime& todate, const wxPen* pen, const wxBrush* brush);

    // Get the "coordinates" for the date relative to the month currently displayed.
    // using (day, week): upper left coord is (1, 1), lower right coord is (7, 6)
    // if the date isn't visible (-1, -1) is put in (day, week) and false is returned
    bool GetDateCoord(const wxDateTime& date, int *day, int *week) const;

    // Set the flag for SetDate(): otherwise it would overwrite the year
    // typed in by the user
    void SetUserChangedYear() { m_userChangedYear = true; }

    // the subcontrols
    ibStaticText *m_staticMonth;
    ibChoice *m_choiceMonth;

    ibStaticText *m_staticYear;
    ibSpinCtrl *m_spinYear;

    // the current selection
    wxDateTime m_date;

    // the date-range
    wxDateTime m_lowdate;
    wxDateTime m_highdate;

    // default attributes
    wxColour m_colHighlightFg,
             m_colHighlightBg,
             m_colHolidayFg,
             m_colHolidayBg,
             m_colHeaderFg,
             m_colHeaderBg,
             m_colBackground,
             m_colSurrounding;

    // the attributes for each of the month days
    wxCalendarDateAttr *m_attrs[31];

    // the width and height of one column/row in the calendar
    wxCoord m_widthCol,
            m_heightRow,
            m_rowOffset,
            m_calendarWeekWidth;

    wxRect m_leftArrowRect,
           m_rightArrowRect;

    // the week day names
    wxString m_weekdays[7];

    // true if SetDate() is being called as the result of changing the year in
    // the year control
    bool m_userChangedYear;

    wxDECLARE_DYNAMIC_CLASS(ibCalendarCtrl);
    wxDECLARE_EVENT_TABLE();
    wxDECLARE_NO_COPY_CLASS(ibCalendarCtrl);
};

#endif // wxUSE_CALENDARCTRL

#endif // _DESKGUI_UNIV_CALENDARCTRL_H_
