///////////////////////////////////////////////////////////////////////////
// Name:        core/fdatetimeParse.cpp
// Purpose:     ibDateTime::FromFreeForm - a date and a time as a person writes them, in words or digits
// Author:      wxWidgets' reader (src/common/datetimefmt.cpp: wxDateTime::ParseDateTime, ParseDate, ParseTime
//              and the helpers they use - Vadim Zeitlin), moved onto the engine's reading: its parts, its
//              names (ibDateTime::MonthName and the rest, in the thread's language rather than the C
//              locale's), its dictionary for the words
// Copyright:   (c) 1999 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr> for the parts taken from wxWidgets
// Licence:     wxWindows licence for those parts - NOTICE.md
///////////////////////////////////////////////////////////////////////////

#include "core/fdatetime.h"
#include "core/fstring.h"

// What it reads, as wx read it: a date of numbers and names in any order wx could guess ("27 Oct 2026",
// "October 27, 2026", "Tue 27.10.2026"), "today", "yesterday" and "tomorrow", an ordinal for the day
// ("the first of May"); a time of hours, minutes and seconds, 12-hour with AM/PM or 24-hour, "noon" and
// "midnight"; a date and a time in either order. What follows what was read is left as wx left it: unread.
//
// What it does not do that wx did: read a time zone after the time - the date is a wall-clock reading
// (fdatetime.h), and a zone would move it; read the locale's own time format (%X), which on every locale
// the engine runs under is one of the forms above; take a 60th or 61st second.

namespace {

using ibCursor = ibString::const_iterator;

const int kNoMonth = 0;
const int kNoWeekDay = 0;

// THE WORDS, marked for the catalog and read in the thread's language (core/localization/dictionary.h).
struct ibLiteralDate { const char* m_text; int m_daysFromToday; };
const ibLiteralDate s_literalDates[] = {
	{ ibTRANSLATE_IN_CONTEXT("date", "today"),      0 },
	{ ibTRANSLATE_IN_CONTEXT("date", "yesterday"), -1 },
	{ ibTRANSLATE_IN_CONTEXT("date", "tomorrow"),   1 },
};
struct ibLiteralTime { const char* m_text; unsigned m_hour; };
const ibLiteralTime s_literalTimes[] = {
	{ ibTRANSLATE_IN_CONTEXT("time", "noon"),     12 },
	{ ibTRANSLATE_IN_CONTEXT("time", "midnight"),  0 },
};
// That's enough - otherwise there would be problems with composite (or not) ordinals.
const char* const s_ordinals[] = {
	ibTRANSLATE_IN_CONTEXT("day of month", "first"),       ibTRANSLATE_IN_CONTEXT("day of month", "second"),
	ibTRANSLATE_IN_CONTEXT("day of month", "third"),       ibTRANSLATE_IN_CONTEXT("day of month", "fourth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "fifth"),       ibTRANSLATE_IN_CONTEXT("day of month", "sixth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "seventh"),     ibTRANSLATE_IN_CONTEXT("day of month", "eighth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "ninth"),       ibTRANSLATE_IN_CONTEXT("day of month", "tenth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "eleventh"),    ibTRANSLATE_IN_CONTEXT("day of month", "twelfth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "thirteenth"),  ibTRANSLATE_IN_CONTEXT("day of month", "fourteenth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "fifteenth"),   ibTRANSLATE_IN_CONTEXT("day of month", "sixteenth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "seventeenth"), ibTRANSLATE_IN_CONTEXT("day of month", "eighteenth"),
	ibTRANSLATE_IN_CONTEXT("day of month", "nineteenth"),  ibTRANSLATE_IN_CONTEXT("day of month", "twentieth"),
};
const char* const kOrdinalContext = "day of month";

// The English names, read whatever the thread speaks - wx read both too.
const ibChar* const s_englishMonths[12] = {
	ibT("January"), ibT("February"), ibT("March"), ibT("April"), ibT("May"), ibT("June"),
	ibT("July"), ibT("August"), ibT("September"), ibT("October"), ibT("November"), ibT("December")
};
const ibChar* const s_englishMonthsShort[12] = {
	ibT("Jan"), ibT("Feb"), ibT("Mar"), ibT("Apr"), ibT("May"), ibT("Jun"), ibT("Jul"), ibT("Aug"), ibT("Sep"), ibT("Oct"), ibT("Nov"), ibT("Dec")
};
const ibChar* const s_englishWeekDays[7] = {
	ibT("Monday"), ibT("Tuesday"), ibT("Wednesday"), ibT("Thursday"), ibT("Friday"), ibT("Saturday"), ibT("Sunday")
};
const ibChar* const s_englishWeekDaysShort[7] = { ibT("Mon"), ibT("Tue"), ibT("Wed"), ibT("Thu"), ibT("Fri"), ibT("Sat"), ibT("Sun") };

// The text from `p` that is `word`, case aside: the cursor past it, or false and the cursor where it was.
bool TakeWordNoCase(ibCursor& p, ibCursor end, const ibString& word)
{
	const std::size_t length = word.length();
	if (length == 0 || static_cast<std::size_t>(end - p) < length)
		return false;
	if (ibString(p, length).CmpNoCase(word) != 0)
		return false;
	p += length;
	return true;
}

// All digits (but no more than `most`, 0 - any) and the number they make.
bool GetNumericToken(std::size_t most, ibCursor& p, ibCursor end, unsigned long& number)
{
	const ibCursor begin = p;
	unsigned long long value = 0;
	while (p != end && ibIsDigit(*p)) {
		value = value * 10 + static_cast<unsigned>(*p - ibT('0'));
		++p;
		if (most != 0 && static_cast<std::size_t>(p - begin) >= most)
			break;
	}
	if (p == begin || value > 0xFFFFFFFFull)
		return false;
	number = static_cast<unsigned long>(value);
	return true;
}

// All letters - a name's candidate.
ibString GetAlphaToken(ibCursor& p, ibCursor end)
{
	const ibCursor begin = p;
	while (p != end && ibIsAlpha(*p))
		++p;
	return ibString(begin, static_cast<std::size_t>(p - begin));
}

// The month a name is - full or short, English or the thread's own - 1..12, or kNoMonth and the cursor where it was.
int GetMonthFromName(ibCursor& p, ibCursor end)
{
	const ibCursor origin = p;
	const ibString name = GetAlphaToken(p, end);
	if (!name.IsEmpty())
		for (unsigned month = 1; month <= 12; ++month)
			if (name.CmpNoCase(s_englishMonths[month - 1]) == 0 || name.CmpNoCase(ibDateTime::MonthName(month)) == 0
				|| name.CmpNoCase(s_englishMonthsShort[month - 1]) == 0 || name.CmpNoCase(ibDateTime::MonthShortName(month)) == 0)
				return static_cast<int>(month);
	p = origin;
	return kNoMonth;
}

// The weekday a name is, 1..7 Monday first, or kNoWeekDay and the cursor where it was.
int GetWeekDayFromName(ibCursor& p, ibCursor end)
{
	const ibCursor origin = p;
	const ibString name = GetAlphaToken(p, end);
	if (!name.IsEmpty())
		for (unsigned weekDay = 1; weekDay <= 7; ++weekDay)
			if (name.CmpNoCase(s_englishWeekDays[weekDay - 1]) == 0 || name.CmpNoCase(ibDateTime::WeekDayName(weekDay)) == 0
				|| name.CmpNoCase(s_englishWeekDaysShort[weekDay - 1]) == 0 || name.CmpNoCase(ibDateTime::WeekDayShortName(weekDay)) == 0)
				return static_cast<int>(weekDay);
	p = origin;
	return kNoWeekDay;
}

// The day this weekday falls on in the week of `day`, the week starting on Monday.
ibDateTime WeekDayInSameWeek(const ibDateTime& day, int weekDay)
{
	ibDateTimeParts parts;
	day.ToParts(parts);
	return day.AddDays(weekDay - static_cast<int>(parts.m_weekDay));
}

// A DATE at `p`: the day it names, its cursor past what was read. wx's ParseDate - a token at a time, each a
// number, a month's or a weekday's name or an ordinal, guessed into the day, the month and the year, with a guess
// taken back when a later token says otherwise.
bool ReadDate(ibCursor& cursor, ibCursor end, ibDateTime& date)
{
	ibCursor p = cursor;
	while (p != end && ibIsSpace(*p))
		++p;

	// Some special cases.
	for (const ibLiteralDate& literal : s_literalDates) {
		ibCursor q = p;
		if (TakeWordNoCase(q, end, ibGetTranslation(ibString(literal.m_text), ibT("date")))) {
			// Nothing can follow this, so stop here.
			date = ibDateTime::Now().GetDayStart().AddDays(literal.m_daysFromToday);
			cursor = q;
			return true;
		}
	}

	// We try to guess what we have here: for each new (numeric) token, we determine if it can be a month, day or
	// a year. Of course, there is an ambiguity as some numbers may be days as well as months, so we also have the
	// ability to back track.
	bool haveDay = false, haveWDay = false, haveMon = false, haveYear = false;
	bool monWasNumeric = false;   // was the month given as a number?
	int wday = kNoWeekDay, mon = kNoMonth, year = 0;
	unsigned day = 0;

	while (p != end) {
		// A copy of the cursor: the next token may not be part of the date at all.
		ibCursor pCopy = p;

		// Skip white space and date delimiters.
		while (pCopy != end && (ibIsSpace(*pCopy) || *pCopy == ibT('.') || *pCopy == ibT(',') || *pCopy == ibT('/') || *pCopy == ibT('-')))
			++pCopy;
		if (pCopy == end)
			break;

		// Either an alphabetic or a numeric token; the latter first.
		unsigned long val = 0;
		if (GetNumericToken(10, pCopy, end, val)) {
			// Guess what this number is.
			bool isDay = false, isMonth = false, isYear = false;
			if (!haveMon && val > 0 && val <= 12)
				isMonth = true;   // assume it is the month
			else if (haveDay)
				isYear = true;    // this can only be the year
			else {
				// Either the day or the year. A leap year if there is no year yet, so 2/29/1976 is taken.
				const unsigned maxDays = haveMon ? ibDateTime::DaysInMonth(haveYear ? year : 1976, static_cast<unsigned>(mon)) : 31u;
				if (val == 0 || val > maxDays)
					isYear = true;
				else
					isDay = true;
			}

			if (isYear) {
				if (haveYear)
					break;
				haveYear = true;
				year = static_cast<int>(val);
			}
			else if (isDay) {
				if (haveDay)
					break;
				haveDay = true;
				day = static_cast<unsigned>(val);
			}
			else if (isMonth) {
				haveMon = true;
				monWasNumeric = true;
				mon = static_cast<int>(val);
			}
		}
		else {
			// Not a number - careful not to overwrite the month there is.
			const int mon2 = GetMonthFromName(pCopy, end);
			if (mon2 != kNoMonth) {
				if (haveMon) {
					// But there is a month already - perhaps the number taken for it was the day?
					if (haveDay || !monWasNumeric)
						break;
					// Assume it was: the month's value is the day (a month is always a valid day).
					day = static_cast<unsigned>(mon);
					haveDay = true;
				}
				mon = mon2;
				haveMon = true;
			}
			else {
				const int wday2 = GetWeekDayFromName(pCopy, end);
				if (wday2 != kNoWeekDay) {
					if (haveWDay)
						break;
					wday = wday2;
					haveWDay = true;
				}
				else {
					// The ordinals.
					std::size_t n = 0;
					for (; n < sizeof(s_ordinals) / sizeof(s_ordinals[0]); ++n) {
						const ibString ord = ibGetTranslation(ibString(s_ordinals[n]), ibString(kOrdinalContext));
						if (!ord.IsEmpty() && static_cast<std::size_t>(end - pCopy) >= ord.length()
							&& ibString(pCopy, ord.length()) == ord) {
							pCopy += ord.length();
							break;
						}
					}
					if (n == sizeof(s_ordinals) / sizeof(s_ordinals[0]))
						break;   // something unknown: stop here

					// A day - and the symbolic day comes before the month and the year, so no second guess.
					if (haveDay)
						break;
					haveDay = true;
					day = static_cast<unsigned>(n + 1);
				}
			}
		}

		// Past a token read.
		p = pCopy;
	}

	// No more tokens, or one not understood: whether what there is makes a date.
	if (!haveDay && !haveWDay)
		return false;
	// A day number alone is no date, without a weekday or a month.
	if (!haveWDay && !haveMon)
		return false;
	// Without adjectives a weekday makes sense alone or with the full date ("Wed 1999" means nothing).
	if (haveWDay && (haveMon || haveYear || haveDay) && !(haveDay && haveMon && haveYear))
		return false;

	const ibDateTime today = ibDateTime::Now().GetDayStart();
	ibDateTimeParts now;
	today.ToParts(now);

	if (!haveWDay && haveYear && !(haveDay && haveMon)) {
		// Perhaps the month and the day rather than the day and the year?
		if (haveDay && !haveMon && day <= 12) {
			const int swapped = static_cast<int>(day);
			// In the current year then.
			if (year > 0 && year <= static_cast<int>(ibDateTime::DaysInMonth(now.m_year, static_cast<unsigned>(swapped)))) {
				mon = swapped;
				day = static_cast<unsigned>(year);
				haveMon = true;
				haveYear = false;
			}
			// else: no, cannot exchange, leave haveMon false
		}
		if (!haveMon)
			return false;
	}

	if (!haveMon)
		mon = static_cast<int>(now.m_month);
	if (!haveYear)
		year = static_cast<int>(now.m_year);

	if (haveDay) {
		// The check above is optimistic when the day comes before its month and year: made again now.
		if (day > ibDateTime::DaysInMonth(year, static_cast<unsigned>(mon)))
			return false;
		date = ibDateTime(year, static_cast<unsigned>(mon), day);
		if (haveWDay) {
			// It must really be the same.
			ibDateTimeParts parts;
			date.ToParts(parts);
			if (static_cast<int>(parts.m_weekDay) != wday)
				return false;
		}
	}
	else {
		date = WeekDayInSameWeek(today, wday);
	}

	cursor = p;
	return true;
}

// One of the forms of the time wx tried, from the longest: `%I:%M:%S %p`, `%H:%M:%S`, `%I:%M %p`, `%H:%M`,
// `%I %p`, `%H` - hours of one or two digits, minutes and seconds of two at most, a space before AM/PM taken
// as any spaces or none. `parts` says how many of hours, minutes, seconds; `twelve` - the 12-hour form.
bool ReadTimeForm(ibCursor& cursor, ibCursor end, int parts, bool twelve, unsigned& hour, unsigned& minute, unsigned& second)
{
	ibCursor p = cursor;
	unsigned long num = 0;
	if (!GetNumericToken(2, p, end, num))
		return false;
	if (twelve ? (num == 0 || num > 12) : num > 23)
		return false;
	unsigned h = static_cast<unsigned>(twelve ? num % 12 : num);   // 12 AM is 0
	unsigned m = 0, s = 0;
	if (parts >= 2) {
		if (p == end || *p != ibT(':') || !GetNumericToken(2, ++p, end, num) || num > 59)
			return false;
		m = static_cast<unsigned>(num);
	}
	if (parts >= 3) {
		if (p == end || *p != ibT(':') || !GetNumericToken(2, ++p, end, num) || num > 59)
			return false;
		s = static_cast<unsigned>(num);
	}
	if (twelve) {
		while (p != end && ibIsSpace(*p))
			++p;
		if (static_cast<std::size_t>(end - p) >= 2 && p[0] == ibT('P') && p[1] == ibT('M'))
			h += 12;
		else if (!(static_cast<std::size_t>(end - p) >= 2 && p[0] == ibT('A') && p[1] == ibT('M')))
			return false;
		p += 2;
	}
	hour = h;
	minute = m;
	second = s;
	cursor = p;
	return true;
}

// A TIME at `cursor`, wx's ParseTime: "noon", "midnight", else the forms above in their order.
bool ReadTime(ibCursor& cursor, ibCursor end, unsigned& hour, unsigned& minute, unsigned& second)
{
	for (const ibLiteralTime& literal : s_literalTimes) {
		ibCursor q = cursor;
		if (TakeWordNoCase(q, end, ibGetTranslation(ibString(literal.m_text), ibT("time")))) {
			hour = literal.m_hour;
			minute = second = 0;
			cursor = q;
			return true;
		}
	}
	static const struct { int m_parts; bool m_twelve; } s_forms[] = {
		{ 3, true }, { 3, false }, { 2, true }, { 2, false }, { 1, true }, { 1, false },
	};
	for (const auto& form : s_forms)
		if (ReadTimeForm(cursor, end, form.m_parts, form.m_twelve, hour, minute, second))
			return true;
	return false;
}

// A DATE AND A TIME, wx's ParseDateTime: the date and then the time (spaces between, a `T` allowed), or the time
// and then the date. Both, or nothing.
bool ReadDateTime(ibCursor begin, ibCursor end, ibDateTime& result)
{
	ibDateTime date;
	unsigned hour = 0, minute = 0, second = 0;
	ibCursor p = begin;
	if (ReadDate(p, end, date)) {
		// Spaces skipped, as the time is not read over them; then a possible `T` in front of the time.
		while (p != end && ibIsSpace(*p))
			++p;
		if (p != end && *p == ibT('T'))
			++p;
		if (!ReadTime(p, end, hour, minute, second))
			return false;
	}
	else {
		// A time followed by a date?
		p = begin;
		if (!ReadTime(p, end, hour, minute, second))
			return false;
		while (p != end && ibIsSpace(*p))
			++p;
		if (!ReadDate(p, end, date))
			return false;
	}
	result = date.AddMilliseconds(((static_cast<long long>(hour) * 60 + minute) * 60 + second) * 1000);
	return true;
}

} // namespace

bool ibDateTime::FromFreeForm(const ibString& text)
{
	const ibCursor begin = text.begin();
	const ibCursor end = text.end();
	ibDateTime read;
	if (ReadDateTime(begin, end, read)) {
		*this = read;
		return true;
	}
	ibCursor p = begin;
	if (ReadDate(p, end, read)) {
		*this = read;
		return true;
	}
	return false;
}
