////////////////////////////////////////////////////////////////////////////
//	Description : ibJobScheduleDescription — the calendar half of "is this job due?"
////////////////////////////////////////////////////////////////////////////

#include "jobSchedule.h"

#include "backend/serialize/dataBuilder.h"   // ibDataNode — the storage door (ReadData / WriteData)

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

bool ibJobScheduleDescription::IsInsideWindow(int startMinute, int endMinute, int nowMinute)
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

bool ibJobScheduleRules::IsAllowed(const ibJobScheduleDescription& self, const wxDateTime& moment)
{
	if (!moment.IsValid())
		return false;

	// Validity range first — outside it nothing else matters.
	if (self.m_activeFrom.IsValid() && moment < self.m_activeFrom)
		return false;
	if (self.m_activeTo.IsValid() && moment > self.m_activeTo)
		return false;

	// Month. Bit 0 = January; wxDateTime::Jan is 0 as well.
	if (self.m_months != 0) {
		const int monthIndex = static_cast<int>(moment.GetMonth());
		if ((self.m_months & (1u << monthIndex)) == 0)
			return false;
	}

	// Day of month, from the START and from the END. Bit 0 is the 1st in one mask and the LAST day
	// in the other; naming both means either matches, which is what "the 1st and the last day" is.
	// A month with no 31st simply never matches a "31st" schedule — the honest reading, and better
	// than sliding to the 30th, which would make the job run in months the author did not name.
	if (self.m_daysOfMonth != 0 || self.m_daysOfMonthFromEnd != 0) {
		const int day        = static_cast<int>(moment.GetDay());              // 1..31
		const int daysInMonth = static_cast<int>(wxDateTime::GetNumberOfDays(moment.GetMonth(), moment.GetYear()));
		const int fromEnd    = daysInMonth - day;                              // 0 = the last day

		const bool byStart = self.m_daysOfMonth != 0
		                  && (self.m_daysOfMonth & (1u << (day - 1))) != 0;
		const bool byEnd   = self.m_daysOfMonthFromEnd != 0
		                  && fromEnd < 32
		                  && (self.m_daysOfMonthFromEnd & (1u << fromEnd)) != 0;
		if (!byStart && !byEnd)
			return false;
	}

	// Day of week. Zero is treated as "any" rather than "never": an empty mask
	// is what an untouched control produces, and a job that silently never runs
	// is the worst possible reading of "the user did not choose days".
	if (self.m_daysOfWeek != 0 && self.m_daysOfWeek != ibJobWeekDay_Any) {
		if ((self.m_daysOfWeek & WeekDayBit(moment)) == 0)
			return false;
	}

	// WHICH occurrence of that weekday — "the second Tuesday", "the last Friday". Counted within the
	// month, so the arithmetic is the day number, not a calendar walk: the Nth occurrence covers days
	// 7(N-1)+1 .. 7N, and the last one is whatever falls in the final seven days.
	if (self.m_weekdayOrdinal != ibJobOrdinal_None) {
		const int day         = static_cast<int>(moment.GetDay());
		const int daysInMonth = static_cast<int>(wxDateTime::GetNumberOfDays(moment.GetMonth(), moment.GetYear()));
		if (self.m_weekdayOrdinal == ibJobOrdinal_Last) {
			if (day + 7 <= daysInMonth)
				return false;   // another one of this weekday follows — so this is not the last
		}
		else {
			const int occurrence = (day - 1) / 7 + 1;
			if (occurrence != static_cast<int>(self.m_weekdayOrdinal))
				return false;
		}
	}

	// HOW OFTEN, when the masks above said only WHICH. Counted from a fixed anchor so a late run
	// cannot shift every later one: the phase belongs to the calendar, not to our history.
	if (self.m_everyNWeeks > 1 || self.m_everyNMonths > 1) {
		const wxDateTime anchor = self.m_periodAnchor.IsValid() ? self.m_periodAnchor
		                        : (self.m_activeFrom.IsValid() ? self.m_activeFrom
		                                                  : wxDateTime(1, wxDateTime::Jan, 1970, 0, 0, 0));
		if (self.m_everyNWeeks > 1) {
			// Whole weeks between the two dates, floor — both ends read at midnight so a time of day
			// cannot move a run into the neighbouring period.
			const wxDateTime a = anchor.GetDateOnly();
			const wxDateTime m = moment.GetDateOnly();
			const long days = (m - a).GetDays();
			const long weeks = (days >= 0 ? days : days - 6) / 7;   // floor for negatives too
			if (weeks % static_cast<long>(self.m_everyNWeeks) != 0)
				return false;
		}
		if (self.m_everyNMonths > 1) {
			const long months = (static_cast<long>(moment.GetYear()) - anchor.GetYear()) * 12
			                  + (static_cast<long>(moment.GetMonth()) - anchor.GetMonth());
			if (months % static_cast<long>(self.m_everyNMonths) != 0)
				return false;
		}
	}

	// Time of day.
	const int nowMinute = moment.GetHour() * 60 + moment.GetMinute();
	if (!ibJobScheduleDescription::IsInsideWindow(self.m_startMinute, self.m_endMinute, nowMinute))
		return false;

	// TOO LATE TO BEGIN. The window says when work may happen; this says when starting something
	// that runs long stops being a good idea. Gates the START only — a pass already under way is
	// stopped, if at all, through the cancel token, by whoever knows what half-done means for it.
	if (self.m_stopAfterMinute >= 0 && nowMinute >= self.m_stopAfterMinute)
		return false;

	return true;
}

bool ibJobScheduleDescription::IsValid() const
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
	if (m_daysOfMonthFromEnd != 0 && (m_daysOfMonthFromEnd & 0x7FFFFFFFu) == 0)
		return false;

	// An ordinal counts occurrences of ONE weekday; with several named there is nothing to count,
	// and with none the ordinal has no subject at all.
	if (m_weekdayOrdinal != ibJobOrdinal_None) {
		if (m_weekdayOrdinal > ibJobOrdinal_Fifth && m_weekdayOrdinal != ibJobOrdinal_Last)
			return false;
		const std::uint8_t days = (m_daysOfWeek == 0) ? ibJobWeekDay_Any : m_daysOfWeek;
		const bool exactlyOne = days != 0 && (days & (days - 1)) == 0;
		if (!exactlyOne)
			return false;
	}

	// "Every N" with a stop-after that precedes the window start would gate the job forever.
	if (m_stopAfterMinute >= 0) {
		if (m_stopAfterMinute > 24 * 60)
			return false;
		if (m_startMinute >= 0 && m_stopAfterMinute <= m_startMinute)
			return false;
	}

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

wxString ibJobScheduleRules::Describe(const ibJobScheduleDescription& self)
{
	wxString out = FormatInterval(self.m_intervalSeconds);

	// Everything below is skipped when left at "any" — a description that
	// restates the defaults is one nobody finishes reading.
	if (self.m_startMinute >= 0 && self.m_endMinute >= 0) {
		out += wxString::Format(wxT(", %s-%s"),
			FormatMinute(self.m_startMinute), FormatMinute(self.m_endMinute));
	}
	if (self.m_daysOfWeek != 0 && self.m_daysOfWeek != ibJobWeekDay_Any)
		out += wxT(", ") + FormatWeekDays(self.m_daysOfWeek);
	if (self.m_daysOfMonth != 0)
		out += wxString::Format(_(", on day %s"), FormatMonthDays(self.m_daysOfMonth));
	if (self.m_months != 0)
		out += wxT(", ") + FormatMonths(self.m_months);
	if (self.m_activeFrom.IsValid())
		out += wxString::Format(_(", from %s"), self.m_activeFrom.FormatDate());
	if (self.m_activeTo.IsValid())
		out += wxString::Format(_(", until %s"), self.m_activeTo.FormatDate());

	return out;
}

wxDateTime ibJobScheduleRules::NextAllowedAfter(const ibJobScheduleDescription& self, const wxDateTime& notBefore)
{
	if (!notBefore.IsValid())
		return wxInvalidDateTime;

	// SECOND-LEVEL precision wherever the calendar permits it. A moment that already qualifies IS
	// the answer — that is the "not before, never only at" contract — so it comes back untouched,
	// seconds and all. The minute-by-minute walk below is only for the case where the calendar
	// REFUSES this moment, and minutes are all the calendar's own fields can name anyway.
	//
	// Without this, any interval shorter than a minute was silently rounded up to the next whole
	// one: "every 4 seconds" ran once a minute on the :00, which reads as a job ignoring its own
	// schedule rather than as a documented limit.
	if (IsAllowed(self, notBefore))
		return notBefore;

	// From here the moment is disallowed, so the search starts at the next whole minute.
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
	ibJobScheduleDescription dayOnly = self;
	dayOnly.m_startMinute = -1;
	dayOnly.m_endMinute   = -1;

	for (int day = 0; day < kMaxDays; ++day) {
		if (!IsAllowed(dayOnly, moment)) {
			// Next midnight. Recomputed from the date part so a partial first day
			// does not shift every following one.
			moment = moment.GetDateOnly() + wxTimeSpan::Days(1);
			continue;
		}

		// This day qualifies — walk its remaining minutes for the window.
		for (int i = 0; i < kMinutesPerDay; ++i) {
			if (IsAllowed(self, moment))
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

bool ibJobScheduleDescription::operator==(const ibJobScheduleDescription& o) const
{
	return m_intervalSeconds    == o.m_intervalSeconds
	    && m_startMinute        == o.m_startMinute
	    && m_endMinute          == o.m_endMinute
	    && m_stopAfterMinute    == o.m_stopAfterMinute
	    && m_daysOfWeek         == o.m_daysOfWeek
	    && m_daysOfMonth        == o.m_daysOfMonth
	    && m_daysOfMonthFromEnd == o.m_daysOfMonthFromEnd
	    && m_months             == o.m_months
	    && m_weekdayOrdinal     == o.m_weekdayOrdinal
	    && m_everyNWeeks        == o.m_everyNWeeks
	    && m_everyNMonths       == o.m_everyNMonths
	    && m_periodAnchor       == o.m_periodAnchor
	    && m_activeFrom         == o.m_activeFrom
	    && m_activeTo           == o.m_activeTo;
}

// ---------------------------------------------------------------------------
// ibJobScheduleDescriptionMemory — the serialiser. Node shape documented at the declaration.
// ---------------------------------------------------------------------------

bool ibJobScheduleDescriptionMemory::WriteNode(ibDataValue& value, const ibJobScheduleDescription& s)
{
	auto root = std::make_shared<ibDataNode>();
	root->SetValue(wxT("IntervalSeconds"),    (s32)s.m_intervalSeconds);
	root->SetValue(wxT("StartMinute"),        (s32)s.m_startMinute);
	root->SetValue(wxT("EndMinute"),          (s32)s.m_endMinute);
	root->SetValue(wxT("StopAfterMinute"),    (s32)s.m_stopAfterMinute);
	root->SetValue(wxT("DaysOfWeek"),         (s32)s.m_daysOfWeek);
	root->SetValue(wxT("DaysOfMonth"),        (s32)s.m_daysOfMonth);
	root->SetValue(wxT("DaysOfMonthFromEnd"), (s32)s.m_daysOfMonthFromEnd);
	root->SetValue(wxT("Months"),             (s32)s.m_months);
	root->SetValue(wxT("WeekdayOrdinal"),     (s32)s.m_weekdayOrdinal);
	root->SetValue(wxT("EveryNWeeks"),        (s32)s.m_everyNWeeks);
	root->SetValue(wxT("EveryNMonths"),       (s32)s.m_everyNMonths);
	root->SetValue(wxT("PeriodAnchor"),       s.m_periodAnchor);
	root->SetValue(wxT("ActiveFrom"),         s.m_activeFrom);
	root->SetValue(wxT("ActiveTo"),           s.m_activeTo);
	value = ibDataValue::Child(root);
	return true;
}

bool ibJobScheduleDescriptionMemory::ReadNode(const ibDataValue& value, ibJobScheduleDescription& s)
{
	const std::shared_ptr<ibDataNode>& root = value.AsChild();
	if (!root)
		return false;

	// FindField before reading, so an absent name leaves the member at its default. That is the
	// whole forward-compatibility story: a blob written before a field existed must load as "not
	// restricted", never as "restricted to nothing".
	const auto readInt = [&root](const wxString& name, auto& target) {
		if (root->FindField(name) != nullptr)
			target = static_cast<typename std::remove_reference<decltype(target)>::type>(root->GetValue<s32>(name));
	};

	readInt(wxT("IntervalSeconds"),    s.m_intervalSeconds);
	readInt(wxT("StartMinute"),        s.m_startMinute);
	readInt(wxT("EndMinute"),          s.m_endMinute);
	readInt(wxT("StopAfterMinute"),    s.m_stopAfterMinute);
	readInt(wxT("DaysOfWeek"),         s.m_daysOfWeek);
	readInt(wxT("DaysOfMonth"),        s.m_daysOfMonth);
	readInt(wxT("DaysOfMonthFromEnd"), s.m_daysOfMonthFromEnd);
	readInt(wxT("Months"),             s.m_months);
	readInt(wxT("WeekdayOrdinal"),     s.m_weekdayOrdinal);
	readInt(wxT("EveryNWeeks"),        s.m_everyNWeeks);
	readInt(wxT("EveryNMonths"),       s.m_everyNMonths);

	root->GetValue<wxDateTime>(wxT("PeriodAnchor"), s.m_periodAnchor);
	root->GetValue<wxDateTime>(wxT("ActiveFrom"),   s.m_activeFrom);
	root->GetValue<wxDateTime>(wxT("ActiveTo"),     s.m_activeTo);
	return true;
}

ibJobScheduleDescription ibJobScheduleDescription::EverySeconds(int seconds)
{
	ibJobScheduleDescription s;
	s.m_intervalSeconds = seconds;
	return s;
}

ibJobScheduleDescription ibJobScheduleDescription::Nightly(int startHour, int endHour)
{
	ibJobScheduleDescription s;
	// Once a day, inside the window — the interval is what stops it from
	// re-running every tick for the whole window.
	s.m_intervalSeconds = 24 * 3600;
	s.m_startMinute     = AtTime(startHour);
	s.m_endMinute       = AtTime(endHour);
	return s;
}
