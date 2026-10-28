#ifndef __QUERY_PERIOD_UNITS_H__
#define __QUERY_PERIOD_UNITS_H__

////////////////////////////////////////////////////////////////////////////
//	Description : THE PERIOD UNITS, AS WORDS - one table for every side that says a periodicity
////////////////////////////////////////////////////////////////////////////
//
// Two places in the language say a periodicity: a register's `Turnovers(&From, &To, Month)` and a
// totals level's `BY Period PERIODS(Month, …)`. They are the same question, so they read the same
// vocabulary; a second copy is how two parts of a program come to disagree about what "Quarter"
// means. (It lived under the registers while they were the only ones asking. They are not — and
// the windows that offer a unit, the server's and the client's, read this one table too.)
//
// ⚠ The ORDER matters and is not alphabetical: it is coarseness, ascending. A totals schema offers
// only the projections COARSER than what it stores (an hour cannot be recovered from a day already
// summed), and that comparison is on the enum — so the enum's order IS the meaning.

#include "core/fdatetime.h"   // ibTotalsPeriod


#include <utility>
#include <vector>

inline const std::vector<std::pair<ibTotalsPeriod, ibString>>& ibPeriodUnits()
{
	static const std::vector<std::pair<ibTotalsPeriod, ibString>> s_units = {
		{ ibTotalsPeriod::Second,   ibT("Second")   }, { ibTotalsPeriod::Minute,   ibT("Minute")   },
		{ ibTotalsPeriod::Hour,     ibT("Hour")     }, { ibTotalsPeriod::Day,      ibT("Day")      },
		{ ibTotalsPeriod::Week,     ibT("Week")     }, { ibTotalsPeriod::TenDays,  ibT("TenDays")  },
		{ ibTotalsPeriod::Month,    ibT("Month")    }, { ibTotalsPeriod::Quarter,  ibT("Quarter")  },
		{ ibTotalsPeriod::HalfYear, ibT("HalfYear") }, { ibTotalsPeriod::Year,     ibT("Year")     },
	};
	return s_units;
}

// The word a unit is written as — a lookup in that one table, never a second switch over it.
inline ibString ibPeriodUnitWord(ibTotalsPeriod unit)
{
	for (const std::pair<ibTotalsPeriod, ibString>& u : ibPeriodUnits())
		if (u.first == unit)
			return u.second;
	return ibString();
}

// …and back: the word -> the unit. False = not one of them, and the CALLER says so in its own words
// (a query names the level, a register names the argument) rather than this deciding how to complain.
inline bool ibReadPeriodUnit(const ibString& word, ibTotalsPeriod& unit)
{
	for (const std::pair<ibTotalsPeriod, ibString>& u : ibPeriodUnits())
		if (word.IsSameAs(u.second, /*caseSensitive*/ false)) { unit = u.first; return true; }
	return false;
}

#endif // __QUERY_PERIOD_UNITS_H__
