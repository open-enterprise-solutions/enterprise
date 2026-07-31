////////////////////////////////////////////////////////////////////////////
//	Description : ibJobSchedule — the calendar half of "is this job due?"
////////////////////////////////////////////////////////////////////////////

#include "jobSchedule.h"

namespace {

// wxDateTime numbers weekdays with Sunday = 0; the mask starts the week on
// Monday, because that is how a week is written when someone picks days in a
// dialog. One conversion, here, so nothing else has to know either convention.
std::uint8_t WeekDayBit(const wxDateTime& moment)
{
	const int wd = static_cast<int>(moment.GetWeekDay());   // Sun=0 .. Sat=6
	const int mondayBased = (wd == 0) ? 6 : (wd - 1);       // Mon=0 .. Sun=6
	return static_cast<std::uint8_t>(1u << mondayBased);
}

} // namespace

bool ibJobSchedule::IsInsideWindow(int startMinute, int endMinute, int nowMinute)
{
	if (startMinute < 0 || endMinute < 0)
		return true;   // no window declared — always inside

	// start <= end is the plain daytime range. start > end WRAPS midnight, which
	// is what a night window (22:00–05:00) means; read as a plain range it would
	// match no minute at all.
	if (startMinute <= endMinute)
		return nowMinute >= startMinute && nowMinute < endMinute;
	return nowMinute >= startMinute || nowMinute < endMinute;
}

bool ibJobSchedule::IsAllowed(const wxDateTime& moment) const
{
	if (!moment.IsValid())
		return false;

	// Validity range first — outside it nothing else matters.
	if (m_activeFrom.IsValid() && moment < m_activeFrom)
		return false;
	if (m_activeTo.IsValid() && moment > m_activeTo)
		return false;

	// Month. Bit 0 = January; wxDateTime::Jan is 0 as well.
	if (m_months != 0) {
		const int monthIndex = static_cast<int>(moment.GetMonth());
		if ((m_months & (1u << monthIndex)) == 0)
			return false;
	}

	// Day of month. Bit 0 = the 1st. A month with no 31st simply never matches
	// a "31st" schedule — the honest reading of "on the 31st", and better than
	// silently sliding to the 30th, which would make the job run in months the
	// author did not name.
	if (m_daysOfMonth != 0) {
		const int day = static_cast<int>(moment.GetDay());   // 1..31
		if ((m_daysOfMonth & (1u << (day - 1))) == 0)
			return false;
	}

	// Day of week. Zero is treated as "any" rather than "never": an empty mask
	// is what an untouched control produces, and a job that silently never runs
	// is the worst possible reading of "the user did not choose days".
	if (m_daysOfWeek != 0 && m_daysOfWeek != ibJobWeekDay_Any) {
		if ((m_daysOfWeek & WeekDayBit(moment)) == 0)
			return false;
	}

	// Time of day.
	const int nowMinute = moment.GetHour() * 60 + moment.GetMinute();
	return IsInsideWindow(m_startMinute, m_endMinute, nowMinute);
}

bool ibJobSchedule::IsValid() const
{
	if (m_intervalSeconds <= 0)
		return false;

	// A half-declared window is not a window — but a window with an out-of-range
	// bound is a mistake, and one that would quietly gate the job forever.
	const bool hasWindow = m_startMinute >= 0 || m_endMinute >= 0;
	if (hasWindow) {
		if (m_startMinute < 0 || m_endMinute < 0)
			return false;
		if (m_startMinute >= 24 * 60 || m_endMinute > 24 * 60)
			return false;
		if (m_startMinute == m_endMinute)
			return false;   // empty range — matches no minute
	}

	// A day-of-month mask that names only days beyond 31 can never match.
	if (m_daysOfMonth != 0 && (m_daysOfMonth & 0x7FFFFFFFu) == 0)
		return false;

	// Months beyond December, likewise.
	if (m_months != 0 && (m_months & 0x0FFFu) == 0)
		return false;

	// An inverted validity range names no moment at all.
	if (m_activeFrom.IsValid() && m_activeTo.IsValid() && m_activeTo < m_activeFrom)
		return false;

	return true;
}

namespace {

// "10:00" from minutes-from-midnight.
wxString FormatMinute(int minute)
{
	return wxString::Format(wxT("%02d:%02d"), minute / 60, minute % 60);
}

// "Every 10 minutes" / "Every 6 hours" / "Daily" — the coarsest wording the
// number allows, because "Every 86400 seconds" is a number, not a schedule.
wxString FormatInterval(int seconds)
{
	if (seconds % (24 * 3600) == 0) {
		const int days = seconds / (24 * 3600);
		return days == 1 ? wxString(_("Daily"))
		                 : wxString::Format(_("Every %d days"), days);
	}
	if (seconds % 3600 == 0) {
		const int hours = seconds / 3600;
		return hours == 1 ? wxString(_("Hourly"))
		                  : wxString::Format(_("Every %d hours"), hours);
	}
	if (seconds % 60 == 0) {
		const int minutes = seconds / 60;
		return minutes == 1 ? wxString(_("Every minute"))
		                    : wxString::Format(_("Every %d minutes"), minutes);
	}
	return wxString::Format(_("Every %d seconds"), seconds);
}

// Weekday names in Monday-first order, matching the mask's bit order.
wxString FormatWeekDays(std::uint8_t mask)
{
	static const wxDateTime::WeekDay order[7] = {
		wxDateTime::Mon, wxDateTime::Tue, wxDateTime::Wed,
		wxDateTime::Thu, wxDateTime::Fri, wxDateTime::Sat, wxDateTime::Sun
	};

	wxString out;
	for (int i = 0; i < 7; ++i) {
		if ((mask & (1u << i)) == 0) continue;
		if (!out.IsEmpty()) out += wxT(", ");
		out += wxDateTime::GetWeekDayName(order[i], wxDateTime::Name_Abbr);
	}
	return out;
}

wxString FormatMonths(std::uint16_t mask)
{
	wxString out;
	for (int i = 0; i < 12; ++i) {
		if ((mask & (1u << i)) == 0) continue;
		if (!out.IsEmpty()) out += wxT(", ");
		out += wxDateTime::GetMonthName(static_cast<wxDateTime::Month>(i),
		                                 wxDateTime::Name_Abbr);
	}
	return out;
}

wxString FormatMonthDays(std::uint32_t mask)
{
	wxString out;
	for (int i = 0; i < 31; ++i) {
		if ((mask & (1u << i)) == 0) continue;
		if (!out.IsEmpty()) out += wxT(", ");
		out += wxString::Format(wxT("%d"), i + 1);
	}
	return out;
}

} // namespace

wxString ibJobSchedule::ToString() const
{
	wxString out = FormatInterval(m_intervalSeconds);

	// Everything below is skipped when left at "any" — a description that
	// restates the defaults is one nobody finishes reading.
	if (m_startMinute >= 0 && m_endMinute >= 0) {
		out += wxString::Format(wxT(", %s-%s"),
			FormatMinute(m_startMinute), FormatMinute(m_endMinute));
	}
	if (m_daysOfWeek != 0 && m_daysOfWeek != ibJobWeekDay_Any)
		out += wxT(", ") + FormatWeekDays(m_daysOfWeek);
	if (m_daysOfMonth != 0)
		out += wxString::Format(_(", on day %s"), FormatMonthDays(m_daysOfMonth));
	if (m_months != 0)
		out += wxT(", ") + FormatMonths(m_months);
	if (m_activeFrom.IsValid())
		out += wxString::Format(_(", from %s"), m_activeFrom.FormatDate());
	if (m_activeTo.IsValid())
		out += wxString::Format(_(", until %s"), m_activeTo.FormatDate());

	return out;
}

wxDateTime ibJobSchedule::NextAllowedAfter(const wxDateTime& notBefore) const
{
	if (!notBefore.IsValid())
		return wxInvalidDateTime;

	// Whole seconds are noise here — the finest thing a schedule names is a
	// minute, so search minute by minute from the next whole one.
	wxDateTime moment = notBefore;
	moment.SetSecond(0);
	moment.SetMillisecond(0);
	if (moment < notBefore)
		moment += wxTimeSpan::Minutes(1);

	// BOUNDED BY COUNTED STEPS, not by comparing against a deadline date. A
	// schedule that names no moment inside a full cycle of its fields names none
	// at all — February 30th is the canonical case — and the search has to say so
	// rather than run on. Counting the steps makes termination a property of the
	// loop itself: whatever the calendar does, and whatever date arithmetic does
	// at a DST boundary or a month end, this cannot fail to end.
	constexpr int kMaxDays           = 366;    // one full year of day-level fields
	constexpr int kMinutesPerDay     = 24 * 60;

	// The day-level test with the time window removed: those fields cannot change
	// within a day, so a disallowed day is skipped whole rather than tested 1440
	// times for 1439 wasted answers.
	ibJobSchedule dayOnly = *this;
	dayOnly.m_startMinute = -1;
	dayOnly.m_endMinute   = -1;

	for (int day = 0; day < kMaxDays; ++day) {
		if (!dayOnly.IsAllowed(moment)) {
			// Next midnight. Recomputed from the date part so a partial first day
			// does not shift every following one.
			moment = moment.GetDateOnly() + wxTimeSpan::Days(1);
			continue;
		}

		// This day qualifies — walk its remaining minutes for the window.
		for (int i = 0; i < kMinutesPerDay; ++i) {
			if (IsAllowed(moment))
				return moment;

			const wxDateTime next = moment + wxTimeSpan::Minutes(1);
			if (next.GetDateOnly() != moment.GetDateOnly()) {
				moment = next;   // rolled into the next day — let the outer loop re-test it
				break;
			}
			moment = next;
		}
	}

	return wxInvalidDateTime;
}

ibJobSchedule ibJobSchedule::EverySeconds(int seconds)
{
	ibJobSchedule s;
	s.m_intervalSeconds = seconds;
	return s;
}

ibJobSchedule ibJobSchedule::Nightly(int startHour, int endHour)
{
	ibJobSchedule s;
	// Once a day, inside the window — the interval is what stops it from
	// re-running every tick for the whole window.
	s.m_intervalSeconds = 24 * 3600;
	s.m_startMinute     = AtTime(startHour);
	s.m_endMinute       = AtTime(endHour);
	return s;
}
