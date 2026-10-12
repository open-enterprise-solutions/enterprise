#ifndef _FRMCLIENT_BACKEND_QUERYABLE_H__
#define _FRMCLIENT_BACKEND_QUERYABLE_H__

// THE DATA-NAVIGATION INTERFACE — the engine's ibBackendQueryable (backend/query/queryable.h): the server's, which reads
// a metaobject through it. What the client's windows take of it is the vocabulary beside it — the period units a
// totals level and a register's turnovers are said in — and that is copied here whole.

#include "frmclient/backend/backend_core.h"   // ibTotalsPeriod (core/fdatetime.h)

#include <vector>

// ⭐⭐ THE PERIOD UNITS, AS WORDS — one table, beside the expression that truncates by them.
//
// Two places in the language say a periodicity: a register's `Turnovers(&From, &To, Month)` and a
// totals level's `BY Period PERIODS(Month, …)`. They are the same question, so they read the same
// vocabulary; a second copy is how two parts of a program come to disagree about what "Quarter"
// means. (It lived under the registers while they were the only ones asking. They are not.)
//
// ⚠ The ORDER matters and is not alphabetical: it is coarseness, ascending. A totals schema offers
// only the projections COARSER than what it stores (an hour cannot be recovered from a day already
// summed), and that comparison is on the enum — so the enum's order IS the meaning.
inline const std::vector<std::pair<ibTotalsPeriod, wxString>>& ibPeriodUnits()
{
	static const std::vector<std::pair<ibTotalsPeriod, wxString>> s_units = {
		{ ibTotalsPeriod::Second,   wxT("Second")   }, { ibTotalsPeriod::Minute,   wxT("Minute")   },
		{ ibTotalsPeriod::Hour,     wxT("Hour")     }, { ibTotalsPeriod::Day,      wxT("Day")      },
		{ ibTotalsPeriod::Week,     wxT("Week")     }, { ibTotalsPeriod::TenDays,  wxT("TenDays")  },
		{ ibTotalsPeriod::Month,    wxT("Month")    }, { ibTotalsPeriod::Quarter,  wxT("Quarter")  },
		{ ibTotalsPeriod::HalfYear, wxT("HalfYear") }, { ibTotalsPeriod::Year,     wxT("Year")     },
	};
	return s_units;
}

// The word a unit is written as — a lookup in that one table, never a second switch over it.
inline wxString ibPeriodUnitWord(ibTotalsPeriod unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibPeriodUnits())
		if (u.first == unit)
			return u.second;
	return wxString();
}

// …and back: the word -> the unit. False = not one of them, and the CALLER says so in its own words
// (a query names the level, a register names the argument) rather than this deciding how to complain.
inline bool ibReadPeriodUnit(const wxString& word, ibTotalsPeriod& unit)
{
	for (const std::pair<ibTotalsPeriod, wxString>& u : ibPeriodUnits())
		if (word.IsSameAs(u.second, /*caseSensitive*/ false)) { unit = u.first; return true; }
	return false;
}

#endif
