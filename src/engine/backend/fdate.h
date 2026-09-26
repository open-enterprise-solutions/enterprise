#ifndef __FDATE_H__
#define __FDATE_H__

#include <wx/defs.h>            // wxLongLong_t
#include <wx/datetime.h>        // wxDateTime - the bridge at the edges, below

#include "backend/backend.h"    // BACKEND_API

// THE CALENDAR WITHOUT A CLOCK.
//
// A date the engine keeps is a WALL-CLOCK READING: what a calendar and a clock on the wall show,
// with no zone in it - the reading a TIMESTAMP holds, and the reading the reference system's Date
// is. Here it is counted as milliseconds from 1970-01-01 00:00:00 of that same wall calendar, by
// the proleptic Gregorian arithmetic below, and nothing in this file asks the machine what its
// clock says or which zone it stands in: the same parts give the same number everywhere, and the
// same number gives the same parts.
//
// The empty date is 0001-01-01 00:00:00 read this way - see emptyDate (backend_core.h) - and it is
// therefore one number on every machine, which is the point.
//
// Two ways to look at the reading, and the day count both go through:
//   ibWallFromParts  - year, month, day, hour, minute, second, millisecond -> milliseconds (constexpr)
//   ibWallToParts    - milliseconds -> the parts, with the day's place in the week and the year
//   ibDaysFromCivil  - year, month, day -> days since 1970-01-01, the integer heart of both
//   ibDaysInMonth    - how many days a month has, the one calendar fact a validating door needs
// The twins of the SQL calendar functions (ibTruncateToPeriod and its family, databaseLayer.h)
// count over these milliseconds so that a period folded in memory is the period the server folds.
//
// ⭐ THE BRIDGE TO wxDateTime, and where it stands. wx keeps an INSTANT - milliseconds of real time,
// read through the machine's zone - and the engine still meets one at its edges: a driver hands a
// TIMESTAMP over as a wxDateTime, a picker in a window holds one, a job schedule counts in them.
// The two functions at the end carry a reading across by its LOCAL PARTS and by nothing else: the
// wall reading 10:30 becomes the wxDateTime whose local time is 10:30, on whichever machine, and
// back. No difference of instants is ever taken between the two sides. The one place the bridge is
// not an identity is a local time that does not exist on the machine (02:30 on the morning its
// clocks go forward): wx moves that instant an hour on, and a reading carried across and back has
// moved with it. The doors that need not cross - the value, the SQL twins, the text - do not.

struct ibDateParts {
	int      m_year = 1970;
	unsigned m_month = 1;         // 1..12
	unsigned m_day = 1;           // 1..31
	unsigned m_hour = 0;          // 0..23
	unsigned m_minute = 0;        // 0..59
	unsigned m_second = 0;        // 0..59
	unsigned m_millisecond = 0;   // 0..999
	unsigned m_weekDay = 4;       // 1..7, Monday first - ISO; 1970-01-01 was a Thursday
	unsigned m_yearDay = 1;       // 1..366
	unsigned m_isoWeek = 1;       // 1..53, ISO 8601: the week that holds the year's first Thursday is 1
};

constexpr wxLongLong_t ibWallMsPerDay = 86400000ll;

// Days since 1970-01-01 of a calendar day, proleptic Gregorian, any year. The standard
// days-from-civil arithmetic: whole integers over (year, month, day), nothing for a zone or a
// leap second to reach.
constexpr wxLongLong_t ibDaysFromCivil(long long year, unsigned month, unsigned day) noexcept
{
	year -= month <= 2 ? 1 : 0;
	const long long era = (year >= 0 ? year : year - 399) / 400;
	const unsigned long long yoe = static_cast<unsigned long long>(year - era * 400);                 // [0, 399]
	const unsigned long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;           // [0, 365]
	const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                              // [0, 146096]
	return era * 146097 + static_cast<long long>(doe) - 719468;
}

// The wall-clock reading of these parts, as milliseconds. The parts are taken as written: a 31st of
// April is not refused here (the doors that make a date from what a person typed refuse it, the
// way Date() does), it is simply the day after the 30th.
constexpr wxLongLong_t ibWallFromParts(int year, unsigned month, unsigned day,
	unsigned hour = 0, unsigned minute = 0, unsigned second = 0, unsigned millisecond = 0) noexcept
{
	return ibDaysFromCivil(year, month, day) * ibWallMsPerDay
		+ ((static_cast<wxLongLong_t>(hour) * 60 + minute) * 60 + second) * 1000 + millisecond;
}

// The days a month has, leap years included - the one calendar fact a door that refuses a 30th of
// February has to ask. Months outside 1..12 have none.
constexpr unsigned ibDaysInMonth(long long year, unsigned month) noexcept
{
	return month == 2 ? ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0 ? 29u : 28u)
	     : (month == 4 || month == 6 || month == 9 || month == 11) ? 30u
	     : (month >= 1 && month <= 12) ? 31u : 0u;
}

// The parts of a wall-clock reading. Defined for any number: a reading before year 1 or past 9999
// gets the year the calendar gives it (0, -1, 10000), so a caller printing it sees where it is.
BACKEND_API void ibWallToParts(wxLongLong_t wall, ibDateParts& parts) noexcept;

// The bridge (see the top of the file): a reading as the wxDateTime with the same LOCAL parts, and
// back. An invalid wxDateTime carries over as the empty date (0001-01-01 00:00:00) - the value has no
// "invalid" of its own, its empty date is that reading.
BACKEND_API wxDateTime   ibDateTimeOfWall(wxLongLong_t wall);
BACKEND_API wxLongLong_t ibWallOfDateTime(const wxDateTime& moment);

#endif
