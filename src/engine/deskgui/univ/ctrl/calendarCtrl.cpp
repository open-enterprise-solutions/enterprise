// FORKED from the wx GENERIC calendar (src/generic/calctrlg.cpp, and of src/common/calctrlcmn.cpp the members of
// wxCalendarCtrlBase it stands on) - see calendarCtrl.h.

///////////////////////////////////////////////////////////////////////////////
// Name:        src/generic/calctrlg.cpp
// Purpose:     implementation of the wxGenericCalendarCtrl
// Author:      Vadim Zeitlin
// Created:     29.12.99
// Copyright:   (c) 1999 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
//              (c) 2026 wxWidgets development team
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include <wx/wxprec.h>

#include "deskgui/univ/ctrl/calendarCtrl.h"

#if wxUSE_CALENDARCTRL

#ifndef WX_PRECOMP
    #include <wx/dcclient.h>
    #include <wx/settings.h>
    #include <wx/brush.h>
#endif //WX_PRECOMP

#include <wx/dcbuffer.h>

#include "deskgui/univ/theme.h"
#include "deskgui/univ/colourScheme.h"
#include "deskgui/univ/ctrl/choice.h"
#include "deskgui/univ/ctrl/spinCtrl.h"
#include "deskgui/univ/ctrl/staticText.h"

#define DEBUG_PAINT 0

// ----------------------------------------------------------------------------
// wxWin macros
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibCalendarCtrl, ibControl);

wxBEGIN_EVENT_TABLE(ibCalendarCtrl, ibControl)
    EVT_PAINT(ibCalendarCtrl::OnPaint)

    EVT_CHAR(ibCalendarCtrl::OnChar)

    EVT_LEFT_DOWN(ibCalendarCtrl::OnClick)
    EVT_LEFT_DCLICK(ibCalendarCtrl::OnDClick)
    EVT_MOUSEWHEEL(ibCalendarCtrl::OnWheel)

    EVT_SYS_COLOUR_CHANGED(ibCalendarCtrl::OnSysColourChanged)
wxEND_EVENT_TABLE()

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// helper functions
// ----------------------------------------------------------------------------

namespace
{

// add attributes that are set in attr
void AddAttr(wxCalendarDateAttr *self, const wxCalendarDateAttr& attr)
{
    if (attr.HasTextColour())
        self->SetTextColour(attr.GetTextColour());
    if (attr.HasBackgroundColour())
        self->SetBackgroundColour(attr.GetBackgroundColour());
    if (attr.HasBorderColour())
        self->SetBorderColour(attr.GetBorderColour());
    if (attr.HasFont())
        self->SetFont(attr.GetFont());
    if (attr.HasBorder())
        self->SetBorder(attr.GetBorder());
    if (attr.IsHoliday())
        self->SetHoliday(true);
}

// remove attributes that are set in attr
void DelAttr(wxCalendarDateAttr *self, const wxCalendarDateAttr &attr)
{
    if (attr.HasTextColour())
        self->SetTextColour(wxNullColour);
    if (attr.HasBackgroundColour())
        self->SetBackgroundColour(wxNullColour);
    if (attr.HasBorderColour())
        self->SetBorderColour(wxNullColour);
    if (attr.HasFont())
        self->SetFont(wxNullFont);
    if (attr.HasBorder())
        self->SetBorder(wxCAL_BORDER_NONE);
    if (attr.IsHoliday())
        self->SetHoliday(false);
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// ibCalendarCtrl
// ----------------------------------------------------------------------------

ibCalendarCtrl::ibCalendarCtrl(wxWindow *parent,
                               wxWindowID id,
                               const wxDateTime& date,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxString& name)
{
    Init();

    (void)Create(parent, id, date, pos, size, style, name);
}

void ibCalendarCtrl::Init()
{
    m_choiceMonth = nullptr;
    m_spinYear = nullptr;
    m_staticYear = nullptr;
    m_staticMonth = nullptr;

    m_userChangedYear = false;

    m_widthCol =
    m_heightRow =
    m_calendarWeekWidth = 0;

    wxDateTime::WeekDay wd;
    for ( wd = wxDateTime::Sun; wd < wxDateTime::Inv_WeekDay; wxNextWDay(wd) )
    {
        m_weekdays[wd] = wxDateTime::GetWeekDayName(wd, wxDateTime::NameForm().Abbr().Standalone());
    }

    for ( auto*& attr : m_attrs )
    {
        attr = nullptr;
    }

    InitColours();
}

// SEAM vs wx: the theme's colours, not the system's
void ibCalendarCtrl::InitColours()
{
    m_colHighlightFg = wxTHEME_COLOUR(HIGHLIGHT_TEXT);
    m_colHighlightBg = wxTHEME_COLOUR(HIGHLIGHT);
    m_colBackground = wxTHEME_COLOUR(WINDOW);
    m_colSurrounding = wxTHEME_COLOUR(CONTROL_TEXT_DISABLED);

    m_colHolidayFg = *wxRED;
    // don't set m_colHolidayBg - by default, same as our bg colour
}

bool ibCalendarCtrl::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxDateTime& date,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxString& name)
{
    if ( !ibControl::Create(parent, id, pos, size,
                            style | wxCLIP_CHILDREN | wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE,
                            wxDefaultValidator, name) )
    {
        return false;
    }

    // needed to get the arrow keys normally used for the dialog navigation
    SetWindowStyle(style | wxWANTS_CHARS);

    m_date = date.IsValid() ? date : wxDateTime::Today();

    m_lowdate = wxDefaultDateTime;
    m_highdate = wxDefaultDateTime;

    if ( !HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        CreateYearSpinCtrl();
        m_staticYear = new ibStaticText(GetParent(), wxID_ANY, m_date.Format(wxT("%Y")),
                                        wxDefaultPosition, wxDefaultSize,
                                        wxALIGN_CENTRE);
        CreateMonthChoice();
        m_staticMonth = new ibStaticText(GetParent(), wxID_ANY, m_date.Format(wxT("%B")),
                                         wxDefaultPosition, wxDefaultSize,
                                         wxALIGN_CENTRE);
    }

    ShowCurrentControls();

    // we need to set the position as well because the main control position
    // is not the same as the one specified in pos if we have the controls
    // above it
    SetInitialSize(size);
    SetPosition(pos);

    // SEAM vs wx: OnPaint clears the client area with it - ibWindow's wxBG_STYLE_PAINT has no erase pass
    SetBackgroundColour(m_colBackground);

    SetHolidayAttrs();

    return true;
}

ibCalendarCtrl::~ibCalendarCtrl()
{
    for ( auto*& attr : m_attrs )
    {
        delete attr;
    }

    if ( !HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        delete m_choiceMonth;
        delete m_staticMonth;
        delete m_spinYear;
        delete m_staticYear;
    }
}

void ibCalendarCtrl::SetWindowStyleFlag(long style)
{
    // changing this style doesn't work because the controls are not
    // created/shown/hidden accordingly
    wxASSERT_MSG( (style & wxCAL_SEQUENTIAL_MONTH_SELECTION) ==
                    (m_windowStyle & wxCAL_SEQUENTIAL_MONTH_SELECTION),
                  wxT("wxCAL_SEQUENTIAL_MONTH_SELECTION can't be changed after creation") );

    wxASSERT_MSG( !((style & wxCAL_SUNDAY_FIRST) &&
                   (style & wxCAL_MONDAY_FIRST)),
                 "wxCAL_SUNDAY_FIRST and wxCAL_MONDAY_FIRST cannot be both used" );

    ibControl::SetWindowStyleFlag(style);
}

// ----------------------------------------------------------------------------
// Create the month choice and the year spin control
// ----------------------------------------------------------------------------

void ibCalendarCtrl::CreateMonthChoice()
{
    m_choiceMonth = new ibChoice(GetParent(), wxID_ANY,
                                  wxDefaultPosition,
                                  wxDefaultSize,
                                  0, nullptr);

    wxDateTime::Month m;
    for ( m = wxDateTime::Jan; m < wxDateTime::Inv_Month; wxNextMonth(m) )
    {
        m_choiceMonth->Append(wxDateTime::GetMonthName(m, wxDateTime::NameForm().Full().Standalone()));
    }

    m_choiceMonth->SetSelection(GetDate().GetMonth());
    m_choiceMonth->SetSize(wxDefaultCoord,
                          wxDefaultCoord,
                          wxDefaultCoord,
                          wxDefaultCoord,
                          wxSIZE_AUTO_WIDTH|wxSIZE_AUTO_HEIGHT);

    m_choiceMonth->Bind(wxEVT_CHOICE, &ibCalendarCtrl::OnMonthChange, this);
}

void ibCalendarCtrl::CreateYearSpinCtrl()
{
    m_spinYear = new ibSpinCtrl(GetParent(), wxID_ANY,
                                GetDate().Format(wxT("%Y")),
                                wxDefaultPosition,
                                wxDefaultSize,
                                wxSP_ARROW_KEYS | wxCLIP_SIBLINGS,
                                -4300, 10000, GetDate().GetYear());

    m_spinYear->Bind(wxEVT_TEXT, &ibCalendarCtrl::OnYearTextChange, this);
    m_spinYear->Bind(wxEVT_SPINCTRL, &ibCalendarCtrl::OnYearChange, this);
}

// ----------------------------------------------------------------------------
// forward wxWin functions to subcontrols
// ----------------------------------------------------------------------------

bool ibCalendarCtrl::Destroy()
{
    if ( m_staticYear )
        m_staticYear->Destroy();
    if ( m_spinYear )
        m_spinYear->Destroy();
    if ( m_choiceMonth )
        m_choiceMonth->Destroy();
    if ( m_staticMonth )
        m_staticMonth->Destroy();

    m_staticYear = nullptr;
    m_spinYear = nullptr;
    m_choiceMonth = nullptr;
    m_staticMonth = nullptr;

    return ibControl::Destroy();
}

bool ibCalendarCtrl::Show(bool show)
{
    if ( !ibControl::Show(show) )
    {
        return false;
    }

    if ( !(GetWindowStyle() & wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        if ( GetMonthControl() )
        {
            GetMonthControl()->Show(show);
            GetYearControl()->Show(show);
        }
    }

    return true;
}

bool ibCalendarCtrl::Enable(bool enable)
{
    if ( !ibControl::Enable(enable) )
    {
        return false;
    }

    if ( !(GetWindowStyle() & wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        GetMonthControl()->Enable(enable);
        GetYearControl()->Enable(enable);
    }

    return true;
}

// ----------------------------------------------------------------------------
// enable/disable month/year controls
// ----------------------------------------------------------------------------

void ibCalendarCtrl::ShowCurrentControls()
{
    if ( !HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        if ( AllowMonthChange() )
        {
            m_choiceMonth->Show();
            m_staticMonth->Hide();

            if ( AllowYearChange() )
            {
                m_spinYear->Show();
                m_staticYear->Hide();

                // skip the rest
                return;
            }
        }
        else
        {
            m_choiceMonth->Hide();
            m_staticMonth->Show();
        }

        // year change not allowed here
        m_spinYear->Hide();
        m_staticYear->Show();
    }
    //else: these controls are not even created, don't show/hide them
}

ibControl *ibCalendarCtrl::GetMonthControl() const
{
    return AllowMonthChange() ? (ibControl *)m_choiceMonth : (ibControl *)m_staticMonth;
}

ibControl *ibCalendarCtrl::GetYearControl() const
{
    return AllowYearChange() ? (ibControl *)m_spinYear : (ibControl *)m_staticYear;
}

void ibCalendarCtrl::EnableYearChange(bool enable)
{
    if ( enable != AllowYearChange() )
    {
        long style = GetWindowStyle();
        if ( enable )
            style &= ~wxCAL_NO_YEAR_CHANGE;
        else
            style |= wxCAL_NO_YEAR_CHANGE;
        SetWindowStyle(style);

        ShowCurrentControls();
        if ( GetWindowStyle() & wxCAL_SEQUENTIAL_MONTH_SELECTION )
        {
            Refresh();
        }
    }
}

bool ibCalendarCtrl::EnableMonthChange(bool enable)
{
    // wxCalendarCtrlBase's part: the style bit
    const long styleOrig = GetWindowStyle();
    long style = enable ? styleOrig & ~wxCAL_NO_MONTH_CHANGE
                        : styleOrig | wxCAL_NO_MONTH_CHANGE;
    if ( style == styleOrig )
        return false;

    SetWindowStyle(style);

    ShowCurrentControls();
    if ( GetWindowStyle() & wxCAL_SEQUENTIAL_MONTH_SELECTION )
        Refresh();

    return true;
}

// ----------------------------------------------------------------------------
// changing date
// ----------------------------------------------------------------------------

bool ibCalendarCtrl::SetDate(const wxDateTime& date)
{
    wxCHECK_MSG( date.IsValid(), false, "invalid date" );

    bool retval = true;

    bool sameMonth = m_date.GetMonth() == date.GetMonth(),
         sameYear = m_date.GetYear() == date.GetYear();

    if ( IsDateInRange(date) )
    {
        if ( sameMonth && sameYear )
        {
            // just change the day
            ChangeDay(date);
        }
        else
        {
            if ( AllowMonthChange() && (AllowYearChange() || sameYear) )
            {
                // change everything
                m_date = date;

                if ( !(GetWindowStyle() & wxCAL_SEQUENTIAL_MONTH_SELECTION) )
                {
                    // update the controls
                    m_choiceMonth->SetSelection(m_date.GetMonth());

                    if ( AllowYearChange() )
                    {
                        if ( !m_userChangedYear )
                            m_spinYear->SetValue(m_date.Format(wxT("%Y")));
                    }
                }

                // as the month changed, holidays did too
                SetHolidayAttrs();

                // update the calendar
                Refresh();
            }
            else
            {
                // forbidden
                retval = false;
            }
        }
    }

    m_userChangedYear = false;

    return retval;
}

void ibCalendarCtrl::ChangeDay(const wxDateTime& date)
{
    if ( m_date != date )
    {
        // we need to refresh the row containing the old date and the one
        // containing the new one
        wxDateTime dateOld = m_date;
        m_date = date;

        RefreshDate(dateOld);

        // if the date is in the same row, it was already drawn correctly
        if ( GetWeek(m_date) != GetWeek(dateOld) )
        {
            RefreshDate(m_date);
        }
    }
}

void ibCalendarCtrl::SetDateAndNotify(const wxDateTime& date)
{
    const wxDateTime dateOld = GetDate();
    if ( date != dateOld && SetDate(date) )
    {
        GenerateAllChangeEvents(dateOld);
    }
}

bool ibCalendarCtrl::GenerateAllChangeEvents(const wxDateTime& dateOld)
{
    const wxDateTime::Tm tm1 = dateOld.GetTm(),
                         tm2 = GetDate().GetTm();

    bool pageChanged = false;

    GenerateEvent(wxEVT_CALENDAR_SEL_CHANGED);
    if ( tm1.year != tm2.year || tm1.mon != tm2.mon )
    {
        GenerateEvent(wxEVT_CALENDAR_PAGE_CHANGED);

        pageChanged = true;
    }

    // send also one of the deprecated events
    if ( tm1.year != tm2.year )
        GenerateEvent(wxEVT_CALENDAR_YEAR_CHANGED);
    else if ( tm1.mon != tm2.mon )
        GenerateEvent(wxEVT_CALENDAR_MONTH_CHANGED);
    else
        GenerateEvent(wxEVT_CALENDAR_DAY_CHANGED);

    return pageChanged;
}

// ----------------------------------------------------------------------------
// date range
// ----------------------------------------------------------------------------

bool ibCalendarCtrl::SetLowerDateLimit(const wxDateTime& date /* = wxDefaultDateTime */)
{
    bool retval = true;

    if ( !(date.IsValid()) || ( ( m_highdate.IsValid() ) ? ( date <= m_highdate ) : true ) )
    {
        m_lowdate = date;
    }
    else
    {
        retval = false;
    }

    return retval;
}

bool ibCalendarCtrl::SetUpperDateLimit(const wxDateTime& date /* = wxDefaultDateTime */)
{
    bool retval = true;

    if ( !(date.IsValid()) || ( ( m_lowdate.IsValid() ) ? ( date >= m_lowdate ) : true ) )
    {
        m_highdate = date;
    }
    else
    {
        retval = false;
    }

    return retval;
}

bool ibCalendarCtrl::SetDateRange(const wxDateTime& lowerdate /* = wxDefaultDateTime */, const wxDateTime& upperdate /* = wxDefaultDateTime */)
{
    bool retval = true;

    if (
        ( !( lowerdate.IsValid() ) || ( ( upperdate.IsValid() ) ? ( lowerdate <= upperdate ) : true ) ) &&
        ( !( upperdate.IsValid() ) || ( ( lowerdate.IsValid() ) ? ( upperdate >= lowerdate ) : true ) ) )
    {
        m_lowdate = lowerdate;
        m_highdate = upperdate;
    }
    else
    {
        retval = false;
    }

    return retval;
}

bool ibCalendarCtrl::GetDateRange(wxDateTime *lowerdate,
                                  wxDateTime *upperdate) const
{
    if ( lowerdate )
        *lowerdate = m_lowdate;
    if ( upperdate )
        *upperdate = m_highdate;

    return m_lowdate.IsValid() || m_highdate.IsValid();
}

// ----------------------------------------------------------------------------
// date helpers
// ----------------------------------------------------------------------------

wxDateTime ibCalendarCtrl::GetStartDate() const
{
    wxDateTime::Tm tm = m_date.GetTm();

    wxDateTime date = wxDateTime(1, tm.mon, tm.year);

    // rewind back
    date.SetToPrevWeekDay(GetWeekStart());

    if ( GetWindowStyle() & wxCAL_SHOW_SURROUNDING_WEEKS )
    {
        // We want to offset the calendar if we start on the first..
        if ( date.GetDay() == 1 )
        {
            date -= wxDateSpan::Week();
        }
    }

    return date;
}

bool ibCalendarCtrl::WeekStartsOnMonday() const
{
    if ( HasFlag(wxCAL_MONDAY_FIRST) )
    {
        return true;
    }
    else if ( HasFlag(wxCAL_SUNDAY_FIRST) )
    {
        return false;
    }
    else
    {
        // Neither flag was explicitly given, let's make a best guess
        // based on locale and/or OS settings.
        wxDateTime::WeekDay firstDay;
        wxDateTime::GetFirstWeekDay(&firstDay);
        return firstDay == wxDateTime::Mon;
    }
}

bool ibCalendarCtrl::IsDateShown(const wxDateTime& date) const
{
    if ( !(GetWindowStyle() & wxCAL_SHOW_SURROUNDING_WEEKS) )
    {
        return date.GetMonth() == m_date.GetMonth();
    }
    else
    {
        return true;
    }
}

bool ibCalendarCtrl::IsDateInRange(const wxDateTime& date) const
{
    // Check if the given date is in the range specified
    return ( ( ( m_lowdate.IsValid() ) ? ( date >= m_lowdate ) : true )
        && ( ( m_highdate.IsValid() ) ? ( date <= m_highdate ) : true ) );
}

bool ibCalendarCtrl::AdjustDateToRange(wxDateTime *date) const
{
    if ( m_lowdate.IsValid() && *date < m_lowdate )
    {
        *date = m_lowdate;
        return true;
    }

    if ( m_highdate.IsValid() && *date > m_highdate )
    {
        *date = m_highdate;
        return true;
    }

    return false;
}

size_t ibCalendarCtrl::GetWeek(const wxDateTime& date) const
{
    size_t retval = date.GetWeekOfMonth(WeekStartsOnMonday()
                                   ? wxDateTime::Monday_First
                                   : wxDateTime::Sunday_First);

    if ( (GetWindowStyle() & wxCAL_SHOW_SURROUNDING_WEEKS) )
    {
        // we need to offset an extra week if we "start" on the 1st of the month
        wxDateTime::Tm tm = date.GetTm();

        wxDateTime datetest = wxDateTime(1, tm.mon, tm.year);

        // rewind back
        datetest.SetToPrevWeekDay(GetWeekStart());

        if ( datetest.GetDay() == 1 )
        {
            retval += 1;
        }
    }

    return retval;
}

// ----------------------------------------------------------------------------
// size management
// ----------------------------------------------------------------------------

// this is a composite control and it must arrange its parts each time its
// size or position changes: the choice and spinctrl are along the top of
// the available area and the calendar takes up the rest of the space

// the static controls are supposed to be always smaller than choice/spin so we
// always use the latter for size calculations and position the static to take
// the same space

// the constants used for the layout
constexpr auto VERT_MARGIN  =  5;  // distance between choice and calendar
constexpr auto HORZ_MARGIN  =  5;  // distance between choice and spin
constexpr auto DAY_MARGIN   =  2;  // distance between day rows and column
constexpr auto WEEK_MARGIN  =  4;  // extra width of the week column

wxSize ibCalendarCtrl::DoGetBestSize() const
{
    // calc the size of the calendar
    const_cast<ibCalendarCtrl *>(this)->RecalcGeometry();

    wxCoord width = 7*m_widthCol + m_calendarWeekWidth,
            height = 7*m_heightRow + m_rowOffset + FromDIP(VERT_MARGIN);

    if ( !HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        const wxSize bestSizeChoice = m_choiceMonth->GetBestSize();
        const wxSize bestSizeSpin = m_spinYear->GetBestSize();

        height += wxMax(bestSizeChoice.y, bestSizeSpin.y)
                    + FromDIP(VERT_MARGIN);

        wxCoord w2 = bestSizeChoice.x + FromDIP(HORZ_MARGIN) + bestSizeSpin.x;
        if ( width < w2 )
            width = w2;
    }

    wxSize best(width, height);
    if ( !HasFlag(wxBORDER_NONE) )
    {
        best += GetWindowBorderSize();
    }

    return best;
}

void ibCalendarCtrl::DoMoveWindow(int x, int y, int width, int height)
{
    int yDiff;

    if ( !HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) && m_staticMonth )
    {
        wxSize sizeChoice = m_choiceMonth->GetEffectiveMinSize();
        wxSize sizeStatic = m_staticMonth->GetEffectiveMinSize();
        wxSize sizeSpin = m_spinYear->GetEffectiveMinSize();

        int maxHeight = wxMax(sizeSpin.y, sizeChoice.y);
        int dy = (maxHeight - sizeStatic.y) / 2;
        m_choiceMonth->SetSize(x, y + (maxHeight - sizeChoice.y)/2, sizeChoice.x, -1);
        m_staticMonth->SetSize(x, y + dy, sizeChoice.x, -1);

        int xDiff = sizeChoice.x + FromDIP(HORZ_MARGIN);

        m_spinYear->SetSize(x + xDiff, y + (maxHeight - sizeSpin.y)/2, width - xDiff, maxHeight);
        m_staticYear->SetSize(x + xDiff, y + dy, width - xDiff, sizeStatic.y);

        yDiff = maxHeight + FromDIP(VERT_MARGIN);
    }
    else // no controls on the top
    {
        yDiff = 0;
    }

    ibControl::DoMoveWindow(x, y + yDiff, width, height - yDiff);
}

void ibCalendarCtrl::RecalcGeometry()
{
    wxInfoDC dc(this);

    // determine the column width (weekday names are not necessarily wider
    // than the numbers (in some languages), so let's not assume that they are)
    m_widthCol = 0;
    for ( int day = 10; day <= 31; day++)
    {
        wxCoord width;
        dc.GetTextExtent(wxString::Format(wxT("%d"), day), &width, &m_heightRow);
        if ( width > m_widthCol )
        {
            // 1.5 times the width gives nice margins even if the weekday
            // names are short
            m_widthCol = width+width/2;
        }
    }
    wxDateTime::WeekDay wd;
    for ( wd = wxDateTime::Sun; wd < wxDateTime::Inv_WeekDay; wxNextWDay(wd) )
    {
        wxCoord width;
        dc.GetTextExtent(m_weekdays[wd], &width, &m_heightRow);
        if ( width > m_widthCol )
        {
            m_widthCol = width;
        }
    }

    m_calendarWeekWidth = HasFlag( wxCAL_SHOW_WEEK_NUMBERS )
        ? dc.GetTextExtent( wxString::Format( wxT( "%d" ), 42 )).GetWidth() + FromDIP(WEEK_MARGIN) : 0;

    // leave some margins
    m_widthCol += FromDIP(DAY_MARGIN);
    m_heightRow += FromDIP(DAY_MARGIN);

    m_rowOffset = HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) ? m_heightRow : 0; // conditional in relation to style
}

// ----------------------------------------------------------------------------
// drawing
// ----------------------------------------------------------------------------

// SEAM vs wx: the paint DC is the window's - the client area starts at its origin (inside the engine's border), the
// buffer, the client area's size, is blitted there with the window's font and colours, the update rectangle is moved
// into the client area; the background is cleared here (wx left it to the platform's erase); the header's and the
// lines' colours are the theme's
void ibCalendarCtrl::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    const wxPoint origin = GetClientAreaOrigin();

    wxPaintDC dcPaint(this);
    dcPaint.SetDeviceOrigin(origin.x, origin.y);
    wxBufferedDC dc(&dcPaint, GetClientSize());
    dc.GetImpl()->InheritAttributes(this);

    wxRect rectUpdate = GetUpdateRegion().GetBox();
    rectUpdate.Offset(-origin);
    const auto isExposed = [&rectUpdate](wxCoord x, wxCoord y, wxCoord w, wxCoord h)
    {
        return rectUpdate.Intersects(wxRect(x, y, w, h));
    };

    dc.SetBackground(GetBackgroundColour());
    dc.Clear();

    RecalcGeometry();

#if DEBUG_PAINT
    wxLogDebug("--- starting to paint, selection: %s, week %u\n",
           m_date.Format("%a %d-%m-%Y %H:%M:%S").c_str(),
           GetWeek(m_date));
#endif

    wxCoord y = 0;
    wxCoord x0 = m_calendarWeekWidth;

    const wxColour colHeaderBg = m_colHeaderBg.IsOk() ? m_colHeaderBg :
        wxTHEME_COLOUR(WINDOW);
    const wxColour colHeaderFg = m_colHeaderFg.IsOk() ? m_colHeaderFg :
        wxTHEME_COLOUR(CONTROL_TEXT);

    if ( HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        // draw the sequential month-selector

        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.SetTextForeground(colHeaderFg);
        dc.SetBrush(wxBrush(colHeaderBg));
        dc.SetPen(wxPen(colHeaderBg));
        dc.DrawRectangle(0, y, GetClientSize().x, m_heightRow);

        // Get extent of month-name + year
        wxCoord monthw, monthh;
        wxString headertext = m_date.Format(wxT("%B %Y"));
        dc.GetTextExtent(headertext, &monthw, &monthh);

        // draw month-name centered above weekdays
        wxCoord monthx = ((m_widthCol * 7) - monthw) / 2 + x0;
        wxCoord monthy = ((m_heightRow - monthh) / 2) + y;
        dc.DrawText(headertext, monthx,  monthy);

        // calculate the "month-arrows"
        wxPoint leftarrow[3];
        wxPoint rightarrow[3];

        int arrowheight = monthh / 2;

        leftarrow[0] = wxPoint(0, arrowheight / 2);
        leftarrow[1] = wxPoint(arrowheight / 2, 0);
        leftarrow[2] = wxPoint(arrowheight / 2, arrowheight - 1);

        rightarrow[0] = wxPoint(0,0);
        rightarrow[1] = wxPoint(arrowheight / 2, arrowheight / 2);
        rightarrow[2] = wxPoint(0, arrowheight - 1);

        // draw the "month-arrows"

        wxCoord arrowy = (m_heightRow - arrowheight) / 2;
        wxCoord larrowx = (m_widthCol - (arrowheight / 2)) / 2 + x0;
        wxCoord rarrowx = ((m_widthCol - (arrowheight / 2)) / 2) + m_widthCol*6 + x0;
        m_leftArrowRect = m_rightArrowRect = wxRect(0,0,0,0);

        if ( AllowMonthChange() )
        {
            wxDateTime ldpm = wxDateTime(1,m_date.GetMonth(), m_date.GetYear()) - wxDateSpan::Day(); // last day prev month
            int rectx = FromDIP(4);
            int recty = FromDIP(3);
            // Check if range permits change
            if ( IsDateInRange(ldpm) && ( ( ldpm.GetYear() == m_date.GetYear() ) ? true : AllowYearChange() ) )
            {
                m_leftArrowRect = wxRect(larrowx - rectx + 1, arrowy - recty, (arrowheight / 2) + 2 * rectx, (arrowheight + 2 * recty));
                dc.SetBrush(wxBrush(colHeaderFg));
                dc.SetPen(wxPen(colHeaderFg));
                dc.DrawPolygon(3, leftarrow, larrowx , arrowy, wxWINDING_RULE);
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(m_leftArrowRect);
            }
            wxDateTime fdnm = wxDateTime(1,m_date.GetMonth(), m_date.GetYear()) + wxDateSpan::Month(); // first day next month
            if ( IsDateInRange(fdnm) && ( ( fdnm.GetYear() == m_date.GetYear() ) ? true : AllowYearChange() ) )
            {
                m_rightArrowRect = wxRect(rarrowx - rectx, arrowy - recty, (arrowheight / 2) + 2 * rectx, (arrowheight + 2 * recty));
                dc.SetBrush(wxBrush(colHeaderFg));
                dc.SetPen(wxPen(colHeaderFg));
                dc.DrawPolygon(3, rightarrow, rarrowx , arrowy, wxWINDING_RULE);
                dc.SetBrush(*wxTRANSPARENT_BRUSH);
                dc.DrawRectangle(m_rightArrowRect);
            }
        }

        y += m_heightRow;
    }

    // first draw the week days
    if ( isExposed(x0, y, x0 + 7*m_widthCol, m_heightRow) )
    {
#if DEBUG_PAINT
        wxLogDebug("painting the header");
#endif

        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.SetTextForeground(colHeaderFg);
        dc.SetBrush(wxBrush(colHeaderBg));
        dc.SetPen(wxPen(colHeaderBg));
        dc.DrawRectangle(0, y, GetClientSize().x, m_heightRow);

        bool startOnMonday = WeekStartsOnMonday();
        for ( int wd = 0; wd < 7; wd++ )
        {
            size_t n;
            if ( startOnMonday )
                n = wd == 6 ? 0 : wd + 1;
            else
                n = wd;
            wxCoord dayw, dayh;
            dc.GetTextExtent(m_weekdays[n], &dayw, &dayh);
            int dayx = x0 + (wd * m_widthCol) + ((m_widthCol - dayw) / 2);
            int dayy = y + m_heightRow / 2 - dayh / 2;
            dc.DrawText(m_weekdays[n], dayx, dayy); // center the day-name
        }
    }

    //dc.SetFont(*wxNORMAL_FONT);

    y += m_heightRow;

    // Draw a horizontal line under the header.
    dc.SetPen(wxPen(wxTHEME_COLOUR(SHADOW_IN)));
    dc.DrawLine(0, y, GetClientSize().x, y);

    // draw column with calendar week nr
    if ( HasFlag( wxCAL_SHOW_WEEK_NUMBERS ) && isExposed( 0, y, m_calendarWeekWidth, m_heightRow * 6 ))
    {
        dc.SetTextForeground(colHeaderFg);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.SetBrush(wxBrush(colHeaderBg));
        dc.SetPen(wxPen(colHeaderBg));
        dc.DrawRectangle( 0, y, m_calendarWeekWidth, m_heightRow * 6 );

        // Draw a vertical line to the right of week numbers.
        dc.SetPen(wxPen(wxTHEME_COLOUR(SHADOW_IN)));
        dc.DrawLine(m_calendarWeekWidth, y, m_calendarWeekWidth, m_heightRow * 6);

        wxDateTime date = GetStartDate();
        for ( size_t i = 0; i < 6; ++i )
        {
            const int weekNr = date.GetWeekOfYear();
            wxString text = wxString::Format( wxT( "%d" ), weekNr );
            wxCoord weekw, weekh;
            dc.GetTextExtent(text, &weekw, &weekh);
            int weekx = m_calendarWeekWidth - weekw - (FromDIP(WEEK_MARGIN) / 2);
            int weeky = (i * m_heightRow) + (y + m_heightRow / 2 - weekh / 2);
            dc.DrawText(text, weekx, weeky);
            date += wxDateSpan::Week();
        }
    }

    // then the calendar itself
    const wxColour colDefaultFg = GetForegroundColour();
    const wxColour colDefaultBg = GetBackgroundColour();

    dc.SetTextForeground(colDefaultFg);

    wxDateTime date = GetStartDate();

#if DEBUG_PAINT
    wxLogDebug("starting calendar from %s\n",
            date.Format("%a %d-%m-%Y %H:%M:%S").c_str());
#endif

    dc.SetBackgroundMode(wxBRUSHSTYLE_SOLID);
    for ( size_t nWeek = 1; nWeek <= 6; nWeek++, y += m_heightRow )
    {
        // if the update region doesn't intersect this row, don't paint it
        if ( !isExposed(x0, y, x0 + 7*m_widthCol, m_heightRow - 1) )
        {
            date += wxDateSpan::Week();

            continue;
        }

#if DEBUG_PAINT
        wxLogDebug("painting week %d at y = %d\n", nWeek, y);
#endif

        for ( int wd = 0; wd < 7; wd++ )
        {
            dc.SetTextForeground(colDefaultFg);
            dc.SetTextBackground(colDefaultBg);

            if ( IsDateShown(date) )
            {
                // don't use wxDate::Format() which prepends 0s
                unsigned int day = date.GetDay();
                wxString dayStr = wxString::Format(wxT("%u"), day);
                wxCoord width, height;
                dc.GetTextExtent(dayStr, &width, &height);

                bool changedColours = false,
                     changedFont = false;

                bool isSel = false;
                wxCalendarDateAttr *attr = nullptr;

                if ( date.GetMonth() != m_date.GetMonth() || !IsDateInRange(date) )
                {
                    // draw the days of adjacent months in different colour
                    dc.SetTextForeground(m_colSurrounding);
                    changedColours = true;
                }
                else
                {
                    isSel = date.IsSameDate(m_date);
                    attr = m_attrs[day - 1];

                    if ( isSel )
                    {
                        dc.SetTextForeground(m_colHighlightFg);
                        dc.SetTextBackground(m_colHighlightBg);

                        changedColours = true;
                    }
                    else if ( attr )
                    {
                        wxColour colFg, colBg;

                        if ( attr->IsHoliday() )
                        {
                            colFg = m_colHolidayFg;
                            colBg = m_colHolidayBg;
                        }
                        else
                        {
                            colFg = attr->GetTextColour();
                            colBg = attr->GetBackgroundColour();
                        }

                        if ( colFg.IsOk() )
                        {
                            dc.SetTextForeground(colFg);
                            changedColours = true;
                        }

                        if ( colBg.IsOk() )
                        {
                            dc.SetTextBackground(colBg);
                            changedColours = true;
                        }

                        if ( attr->HasFont() )
                        {
                            dc.SetFont(attr->GetFont());
                            changedFont = true;
                        }
                    }
                }

                wxCoord x = wd*m_widthCol + (m_widthCol - width) / 2 + x0;
                dc.DrawText(dayStr, x, y + m_heightRow / 2 - height / 2);

                if ( !isSel && attr && attr->HasBorder() )
                {
                    wxColour colBorder;
                    if ( attr->HasBorderColour() )
                    {
                        colBorder = attr->GetBorderColour();
                    }
                    else
                    {
                        colBorder = colDefaultFg;
                    }

                    wxPen pen(colBorder, 1, wxPENSTYLE_SOLID);
                    dc.SetPen(pen);
                    dc.SetBrush(*wxTRANSPARENT_BRUSH);

                    int shapeSize = wxMin(m_widthCol, m_heightRow) - 1;
                    wxRect shapeRect(x + width / 2 - shapeSize / 2, y, shapeSize, shapeSize);

                    switch ( attr->GetBorder() )
                    {
                        case wxCAL_BORDER_SQUARE:
                            dc.DrawRectangle(shapeRect);
                            break;

                        case wxCAL_BORDER_ROUND:
                            dc.DrawEllipse(shapeRect);
                            break;

                        default:
                            wxFAIL_MSG(wxT("unknown border type"));
                    }
                }

                if ( changedColours )
                {
                    dc.SetTextForeground(colDefaultFg);
                    dc.SetTextBackground(colDefaultBg);
                }

                if ( changedFont )
                {
                    dc.SetFont(GetFont());
                }
            }
            //else: just don't draw it

            date += wxDateSpan::Day();
        }
    }

    // Greying out out-of-range background
    bool showSurrounding = (GetWindowStyle() & wxCAL_SHOW_SURROUNDING_WEEKS) != 0;

    date = ( showSurrounding ) ? GetStartDate() : wxDateTime(1, m_date.GetMonth(), m_date.GetYear());
    if ( !IsDateInRange(date) )
    {
        wxDateTime firstOOR = GetLowerDateLimit() - wxDateSpan::Day(); // first out-of-range

        wxBrush oorbrush = *wxLIGHT_GREY_BRUSH;
        oorbrush.SetStyle(wxBRUSHSTYLE_FDIAGONAL_HATCH);

        HighlightRange(&dc, date, firstOOR, wxTRANSPARENT_PEN, &oorbrush);
    }

    date = ( showSurrounding ) ? GetStartDate() + wxDateSpan::Weeks(6) - wxDateSpan::Day() : wxDateTime().SetToLastMonthDay(m_date.GetMonth(), m_date.GetYear());
    if ( !IsDateInRange(date) )
    {
        wxDateTime firstOOR = GetUpperDateLimit() + wxDateSpan::Day(); // first out-of-range

        wxBrush oorbrush = *wxLIGHT_GREY_BRUSH;
        oorbrush.SetStyle(wxBRUSHSTYLE_FDIAGONAL_HATCH);

        HighlightRange(&dc, firstOOR, date, wxTRANSPARENT_PEN, &oorbrush);
    }

#if DEBUG_PAINT
    wxLogDebug("+++ finished painting");
#endif
}

void ibCalendarCtrl::RefreshDate(const wxDateTime& date)
{
    RecalcGeometry();

    wxRect rect;

    // always refresh the whole row at once because our OnPaint() will draw
    // the whole row anyhow - and this allows the small optimization in
    // OnClick() below to work
    rect.x = m_calendarWeekWidth;

    rect.y = (m_heightRow * GetWeek(date)) + m_rowOffset;

    rect.width = 7*m_widthCol;
    rect.height = m_heightRow;

#if DEBUG_PAINT
    wxLogDebug("*** refreshing week %d at (%d, %d)-(%d, %d)\n",
           GetWeek(date),
           rect.x, rect.y,
           rect.x + rect.width, rect.y + rect.height);
#endif

    Refresh(true, &rect);
}

void ibCalendarCtrl::HighlightRange(wxDC* pDC, const wxDateTime& fromdate, const wxDateTime& todate, const wxPen* pPen, const wxBrush* pBrush)
{
    // Highlights the given range using pen and brush
    // Does nothing if todate < fromdate


#if DEBUG_PAINT
    wxLogDebug("+++ HighlightRange: (%s) - (%s) +++", fromdate.Format("%d %m %Y"), todate.Format("%d %m %Y"));
#endif

    if ( todate >= fromdate )
    {
        // do stuff
        // date-coordinates
        int fd, fw;
        int td, tw;

        // implicit: both dates must be currently shown - checked by GetDateCoord
        if ( GetDateCoord(fromdate, &fd, &fw) && GetDateCoord(todate, &td, &tw) )
        {
#if DEBUG_PAINT
            wxLogDebug("Highlight range: (%i, %i) - (%i, %i)", fd, fw, td, tw);
#endif
            if ( ( (tw - fw) == 1 ) && ( td < fd ) )
            {
                // special case: interval 7 days or less not in same week
                // split in two separate intervals
                wxDateTime tfd = fromdate + wxDateSpan::Days(7-fd);
                wxDateTime ftd = tfd + wxDateSpan::Day();
#if DEBUG_PAINT
                wxLogDebug("Highlight: Separate segments");
#endif
                // draw separately
                HighlightRange(pDC, fromdate, tfd, pPen, pBrush);
                HighlightRange(pDC, ftd, todate, pPen, pBrush);
            }
            else
            {
                int numpoints;
                wxPoint corners[8]; // potentially 8 corners in polygon
                wxCoord x0 = m_calendarWeekWidth;

                if ( fw == tw )
                {
                    // simple case: same week
                    numpoints = 4;
                    corners[0] = wxPoint(x0 + (fd - 1) * m_widthCol, (fw * m_heightRow) + m_rowOffset);
                    corners[1] = wxPoint(x0 + (fd - 1) * m_widthCol, ((fw + 1 ) * m_heightRow) + m_rowOffset);
                    corners[2] = wxPoint(x0 + td * m_widthCol, ((tw + 1) * m_heightRow) + m_rowOffset);
                    corners[3] = wxPoint(x0 + td * m_widthCol, (tw * m_heightRow) + m_rowOffset);
                }
                else
                {
                    int cidx = 0;
                    // "complex" polygon
                    corners[cidx] = wxPoint(x0 + (fd - 1) * m_widthCol, (fw * m_heightRow) + m_rowOffset); cidx++;

                    if ( fd > 1 )
                    {
                        corners[cidx] = wxPoint(x0 + (fd - 1) * m_widthCol, ((fw + 1) * m_heightRow) + m_rowOffset); cidx++;
                        corners[cidx] = wxPoint(x0, ((fw + 1) * m_heightRow) + m_rowOffset); cidx++;
                    }

                    corners[cidx] = wxPoint(x0, ((tw + 1) * m_heightRow) + m_rowOffset); cidx++;
                    corners[cidx] = wxPoint(x0 + td * m_widthCol, ((tw + 1) * m_heightRow) + m_rowOffset); cidx++;

                    if ( td < 7 )
                    {
                        corners[cidx] = wxPoint(x0 + td * m_widthCol, (tw * m_heightRow) + m_rowOffset); cidx++;
                        corners[cidx] = wxPoint(x0 + 7 * m_widthCol, (tw * m_heightRow) + m_rowOffset); cidx++;
                    }

                    corners[cidx] = wxPoint(x0 + 7 * m_widthCol, (fw * m_heightRow) + m_rowOffset); cidx++;

                    numpoints = cidx;
                }

                // draw the polygon
                pDC->SetBrush(*pBrush);
                pDC->SetPen(*pPen);
                pDC->DrawPolygon(numpoints, corners);
            }
        }
    }
    // else do nothing
#if DEBUG_PAINT
    wxLogDebug("--- HighlightRange ---");
#endif
}

bool ibCalendarCtrl::GetDateCoord(const wxDateTime& date, int *day, int *week) const
{
    bool retval = true;

#if DEBUG_PAINT
    wxLogDebug("+++ GetDateCoord: (%s) +++", date.Format("%d %m %Y"));
#endif

    if ( IsDateShown(date) )
    {
        bool startOnMonday = WeekStartsOnMonday();

        // Find day
        *day = date.GetWeekDay();

        if ( *day == 0 ) // sunday
        {
            *day = ( startOnMonday ) ? 7 : 1;
        }
        else
        {
            *day += ( startOnMonday ) ? 0 : 1;
        }

        int targetmonth = date.GetMonth() + (12 * date.GetYear());
        int thismonth = m_date.GetMonth() + (12 * m_date.GetYear());

        // Find week
        if ( targetmonth == thismonth )
        {
            *week = GetWeek(date);
        }
        else
        {
            if ( targetmonth < thismonth )
            {
                *week = 1; // trivial
            }
            else // targetmonth > thismonth
            {
                wxDateTime ldcm;
                int lastweek;
                int lastday;

                // get the datecoord of the last day in the month currently shown
#if DEBUG_PAINT
                wxLogDebug("     +++ LDOM +++");
#endif
                GetDateCoord(ldcm.SetToLastMonthDay(m_date.GetMonth(), m_date.GetYear()), &lastday, &lastweek);
#if DEBUG_PAINT
                wxLogDebug("     --- LDOM ---");
#endif

                wxTimeSpan span = date - ldcm;

                int daysfromlast = span.GetDays();
#if DEBUG_PAINT
                wxLogDebug("daysfromlast: %i", daysfromlast);
#endif
                if ( daysfromlast + lastday > 7 ) // past week boundary
                {
                    int wholeweeks = (daysfromlast / 7);
                    *week = wholeweeks + lastweek;
                    if ( (daysfromlast - (7 * wholeweeks) + lastday) > 7 )
                    {
                        *week += 1;
                    }
                }
                else
                {
                    *week = lastweek;
                }
            }
        }
    }
    else
    {
        *day = -1;
        *week = -1;
        retval = false;
    }

#if DEBUG_PAINT
    wxLogDebug("--- GetDateCoord: (%s) = (%i, %i) ---", date.Format("%d %m %Y"), *day, *week);
#endif

    return retval;
}

// ----------------------------------------------------------------------------
// mouse handling
// ----------------------------------------------------------------------------

void ibCalendarCtrl::OnDClick(wxMouseEvent& event)
{
    wxDateTime date;
    switch ( HitTest(event.GetPosition(), &date) )
    {
        case wxCAL_HITTEST_DAY:
            GenerateEvent(wxEVT_CALENDAR_DOUBLECLICKED);
            break;

        case wxCAL_HITTEST_DECMONTH:
        case wxCAL_HITTEST_INCMONTH:
            // Consecutive simple clicks result in a series of simple and
            // double click events, so handle them in the same way.
            SetDateAndNotify(date);
            break;

        case wxCAL_HITTEST_WEEK:
        case wxCAL_HITTEST_HEADER:
        case wxCAL_HITTEST_SURROUNDING_WEEK:
        case wxCAL_HITTEST_NOWHERE:
            event.Skip();
            break;
    }
}

void ibCalendarCtrl::OnClick(wxMouseEvent& event)
{
    wxDateTime date;
    wxDateTime::WeekDay wday;
    switch ( HitTest(event.GetPosition(), &date, &wday) )
    {
        case wxCAL_HITTEST_DAY:
            if ( IsDateInRange(date) )
            {
                ChangeDay(date);

                GenerateEvent(wxEVT_CALENDAR_SEL_CHANGED);

                // we know that the month/year never change when the user
                // clicks on the control so there is no need to call
                // GenerateAllChangeEvents() here, we know which event to send
                GenerateEvent(wxEVT_CALENDAR_DAY_CHANGED);
            }
            break;

        case wxCAL_HITTEST_WEEK:
            {
                wxCalendarEvent send( this, date, wxEVT_CALENDAR_WEEK_CLICKED );
                HandleWindowEvent( send );
            }
            break;

        case wxCAL_HITTEST_HEADER:
            {
                wxCalendarEvent eventWd(this, GetDate(),
                                        wxEVT_CALENDAR_WEEKDAY_CLICKED);
                eventWd.SetWeekDay(wday);
                (void)GetEventHandler()->ProcessEvent(eventWd);
            }
            break;

        case wxCAL_HITTEST_DECMONTH:
        case wxCAL_HITTEST_INCMONTH:
        case wxCAL_HITTEST_SURROUNDING_WEEK:
            SetDateAndNotify(date); // we probably only want to refresh the control. No notification.. (maybe as an option?)
            break;

        default:
            wxFAIL_MSG(wxT("unknown hittest code"));
            wxFALLTHROUGH;

        case wxCAL_HITTEST_NOWHERE:
            event.Skip();
            break;
    }

    // as we don't (always) skip the message, we're not going to receive the
    // focus on click by default if we don't do it ourselves
    SetFocus();
}

wxCalendarHitTestResult ibCalendarCtrl::HitTest(const wxPoint& pos,
                                                wxDateTime *date,
                                                wxDateTime::WeekDay *wd)
{
    RecalcGeometry();

    // the position where the calendar really begins
    wxCoord x0 = m_calendarWeekWidth;

    if ( HasFlag(wxCAL_SEQUENTIAL_MONTH_SELECTION) )
    {
        // Header: month

        // we need to find out if the hit is on left arrow, on month or on right arrow
        // left arrow?
        if ( m_leftArrowRect.Contains(pos) )
        {
            if ( date )
            {
                if ( IsDateInRange(m_date - wxDateSpan::Month()) )
                {
                    *date = m_date - wxDateSpan::Month();
                }
                else
                {
                    *date = GetLowerDateLimit();
                }
            }

            return wxCAL_HITTEST_DECMONTH;
        }

        if ( m_rightArrowRect.Contains(pos) )
        {
            if ( date )
            {
                if ( IsDateInRange(m_date + wxDateSpan::Month()) )
                {
                    *date = m_date + wxDateSpan::Month();
                }
                else
                {
                    *date = GetUpperDateLimit();
                }
            }

            return wxCAL_HITTEST_INCMONTH;
        }
    }

    if ( pos.x - x0 < 0 )
    {
        if ( pos.x >= 0 && pos.y > m_rowOffset + m_heightRow && pos.y <= m_rowOffset + m_heightRow * 7 )
        {
            if ( date )
            {
                *date = GetStartDate();
                *date += wxDateSpan::Week() * (( pos.y - m_rowOffset ) / m_heightRow - 1 );
            }
            if ( wd )
                *wd = GetWeekStart();
            return wxCAL_HITTEST_WEEK;
        }
        else    // early exit -> the rest of the function checks for clicks on days
            return wxCAL_HITTEST_NOWHERE;
    }

    // header: week days
    int wday = (pos.x - x0) / m_widthCol;
    if ( wday > 6 )
        return wxCAL_HITTEST_NOWHERE;
    if ( pos.y < (m_heightRow + m_rowOffset))
    {
        if ( pos.y > m_rowOffset )
        {
            if ( wd )
            {
                if ( WeekStartsOnMonday() )
                {
                    wday = wday == 6 ? 0 : wday + 1;
                }

                *wd = (wxDateTime::WeekDay)wday;
            }

            return wxCAL_HITTEST_HEADER;
        }
        else
        {
            return wxCAL_HITTEST_NOWHERE;
        }
    }

    int week = (pos.y - (m_heightRow + m_rowOffset)) / m_heightRow;
    if ( week >= 6 || wday >= 7 )
    {
        return wxCAL_HITTEST_NOWHERE;
    }

    wxDateTime dt = GetStartDate() + wxDateSpan::Days(7*week + wday);

    if ( IsDateShown(dt) )
    {
        if ( date )
            *date = dt;

        if ( dt.GetMonth() == m_date.GetMonth() )
        {

            return wxCAL_HITTEST_DAY;
        }
        else
        {
            return wxCAL_HITTEST_SURROUNDING_WEEK;
        }
    }
    else
    {
        return wxCAL_HITTEST_NOWHERE;
    }
}

void ibCalendarCtrl::OnWheel(wxMouseEvent& event)
{
    wxDateSpan span;
    switch ( event.GetWheelAxis() )
    {
        case wxMOUSE_WHEEL_VERTICAL:
            // For consistency with the native controls, scrolling upwards
            // should go to the past, even if the rotation is positive and
            // could be normally expected to increase the date.
            span = -wxDateSpan::Month();
            break;

        case wxMOUSE_WHEEL_HORIZONTAL:
            span = wxDateSpan::Year();
            break;
    }

    // Currently we only take into account the rotation direction, not its
    // magnitude.
    if ( event.GetWheelRotation() < 0 )
        span = -span;

    SetDateAndNotify(m_date + span);
}

// ----------------------------------------------------------------------------
// subcontrols events handling
// ----------------------------------------------------------------------------

void ibCalendarCtrl::OnMonthChange(wxCommandEvent& event)
{
    wxDateTime::Tm tm = m_date.GetTm();

    wxDateTime::Month mon = (wxDateTime::Month)event.GetInt();
    if ( tm.mday > wxDateTime::GetNumberOfDays(mon, tm.year) )
    {
        tm.mday = wxDateTime::GetNumberOfDays(mon, tm.year);
    }

    wxDateTime dt(tm.mday, mon, tm.year);
    if ( AdjustDateToRange(&dt) )
    {
        // The date must have been changed to ensure it's in valid range,
        // reflect this in the month choice control.
        m_choiceMonth->SetSelection(dt.GetMonth());
    }

    SetDateAndNotify(dt);
}

void ibCalendarCtrl::HandleYearChange(wxCommandEvent& event)
{
    int year = (int)event.GetInt();
    if ( year == INT_MIN )
    {
        // invalid year in the spin control, ignore it
        return;
    }

    wxDateTime::Tm tm = m_date.GetTm();

    if ( tm.mday > wxDateTime::GetNumberOfDays(tm.mon, year) )
    {
        tm.mday = wxDateTime::GetNumberOfDays(tm.mon, year);
    }

    wxDateTime dt(tm.mday, tm.mon, year);
    if ( AdjustDateToRange(&dt) )
    {
        // As above, if the date was changed to keep it in valid range, its
        // possibly changed year must be shown in the GUI.
        m_spinYear->SetValue(dt.GetYear());
    }

    SetDateAndNotify(dt);
}

void ibCalendarCtrl::OnYearChange(wxSpinEvent& event)
{
    HandleYearChange( event );
}

void ibCalendarCtrl::OnYearTextChange(wxCommandEvent& event)
{
    SetUserChangedYear();
    HandleYearChange(event);
}

void ibCalendarCtrl::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    // reinit colours
    InitColours();

    event.Skip();

    // Redraw control area
    SetBackgroundColour(m_colBackground);
    Refresh();
}

// ----------------------------------------------------------------------------
// keyboard interface
// ----------------------------------------------------------------------------

void ibCalendarCtrl::OnChar(wxKeyEvent& event)
{
    switch ( event.GetKeyCode() )
    {
        case wxT('+'):
        case WXK_ADD:
            SetDateAndNotify(m_date + wxDateSpan::Year());
            break;

        case wxT('-'):
        case WXK_SUBTRACT:
            SetDateAndNotify(m_date - wxDateSpan::Year());
            break;

        case WXK_PAGEUP:
            SetDateAndNotify(m_date - wxDateSpan::Month());
            break;

        case WXK_PAGEDOWN:
            SetDateAndNotify(m_date + wxDateSpan::Month());
            break;

        case WXK_RIGHT:
            if ( event.ControlDown() )
            {
                wxDateTime target = m_date.SetToNextWeekDay(GetWeekEnd());
                AdjustDateToRange(&target);
                SetDateAndNotify(target);
            }
            else
                SetDateAndNotify(m_date + wxDateSpan::Day());
            break;

        case WXK_LEFT:
            if ( event.ControlDown() )
            {
                wxDateTime target = m_date.SetToPrevWeekDay(GetWeekStart());
                AdjustDateToRange(&target);
                SetDateAndNotify(target);
            }
            else
                SetDateAndNotify(m_date - wxDateSpan::Day());
            break;

        case WXK_UP:
            SetDateAndNotify(m_date - wxDateSpan::Week());
            break;

        case WXK_DOWN:
            SetDateAndNotify(m_date + wxDateSpan::Week());
            break;

        case WXK_HOME:
            if ( event.ControlDown() )
                SetDateAndNotify(wxDateTime::Today());
            else
                SetDateAndNotify(wxDateTime(1, m_date.GetMonth(), m_date.GetYear()));
            break;

        case WXK_END:
            SetDateAndNotify(wxDateTime(m_date).SetToLastMonthDay());
            break;

        case WXK_RETURN:
            GenerateEvent(wxEVT_CALENDAR_DOUBLECLICKED);
            break;

        default:
            event.Skip();
    }
}

// ----------------------------------------------------------------------------
// holidays handling
// ----------------------------------------------------------------------------

void ibCalendarCtrl::EnableHolidayDisplay(bool display)
{
    long style = GetWindowStyle();
    if ( display )
        style |= wxCAL_SHOW_HOLIDAYS;
    else
        style &= ~wxCAL_SHOW_HOLIDAYS;

    if ( style == GetWindowStyle() )
        return;

    SetWindowStyle(style);

    if ( display )
        SetHolidayAttrs();
    else
        ResetHolidayAttrs();

    RefreshHolidays();
}

bool ibCalendarCtrl::SetHolidayAttrs()
{
    if ( !HasFlag(wxCAL_SHOW_HOLIDAYS) )
        return false;

    ResetHolidayAttrs();

    wxDateTime::Tm tm = GetDate().GetTm();
    wxDateTime dtStart(1, tm.mon, tm.year),
               dtEnd = dtStart.GetLastMonthDay();

    wxDateTimeArray hol;
    wxDateTimeHolidayAuthority::GetHolidaysInRange(dtStart, dtEnd, hol);

    for ( const auto dt : hol )
    {
        SetHoliday(dt.GetDay());
    }

    return true;
}

void ibCalendarCtrl::SetHoliday(size_t day)
{
    wxCHECK_RET( day > 0 && day < 32, wxT("invalid day in SetHoliday") );

    wxCalendarDateAttr *attr = GetAttr(day);
    if ( !attr )
    {
        attr = new wxCalendarDateAttr;
    }

    attr->SetHoliday(true);

    // can't use SetAttr() because it would delete this pointer
    m_attrs[day - 1] = attr;
}

void ibCalendarCtrl::ResetHolidayAttrs()
{
    for ( auto*& attr : m_attrs )
    {
        if ( attr )
        {
            attr->SetHoliday(false);
        }
    }
}

void ibCalendarCtrl::Mark(size_t day, bool mark)
{
    wxCHECK_RET( day > 0 && day < 32, wxT("invalid day in Mark") );

    const wxCalendarDateAttr& m = wxCalendarDateAttr::GetMark();
    if (mark) {
        if ( m_attrs[day - 1] )
            AddAttr(m_attrs[day - 1], m);
        else
            SetAttr(day, new wxCalendarDateAttr(m));
    }
    else {
        if ( m_attrs[day - 1] )
            DelAttr(m_attrs[day - 1], m);
    }
}

//static
wxVisualAttributes
ibCalendarCtrl::GetClassDefaultAttributes(wxWindowVariant WXUNUSED(variant))
{
    // SEAM vs wx: the same colours as the library's lists (ibVListBox), the theme's - wx took wxListBox's, the system's
    wxVisualAttributes attr;
    attr.colFg = wxTHEME_COLOUR(CONTROL_TEXT);
    attr.colBg = wxTHEME_COLOUR(WINDOW);
    attr.font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    return attr;
}

#endif // wxUSE_CALENDARCTRL
