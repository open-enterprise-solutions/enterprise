#ifndef __QUERY_BALANCE_ROLE_H__
#define __QUERY_BALANCE_ROLE_H__

////////////////////////////////////////////////////////////////////////////
//	Description : WHAT A FIELD IS IN A BALANCE - the moment, a key, or one of its two edges
////////////////////////////////////////////////////////////////////////////
//
// ⭐⭐ SAID BY WHOEVER KNOWS, AND READ BY WHOEVER FOLDS. A register's balance-and-turnovers view knows
// which of its columns is the moment a row stands at, which keep one balance apart from another, and
// which are the balance's two edges — it publishes them on its columns (L3, ibBackendSourceColumn).
// A composition fills a field in with that answer and lets a person overrule it (its Fields page, the
// description's field entry). The query says it in its own words — `<expr> ROLE OPENING AS x` — and a
// `TOTALS SUM(x)` over such a field is taken at each key's first moment (a closing one at its last).
//
// ONE VOCABULARY for the column, the description and the fold, and it lives alone for the reason
// queryUnfold.h does: the description must not pull the column tier in to name it.

#include <cstdint>

enum class ibBalanceRole : uint8_t {
	None,        // an ordinary value
	Moment,      // the time a row stands at — what "first" and "last" are counted by
	Dimension,   // what keeps one balance apart from another: an item, a warehouse, an account
	Opening,     // the balance BEFORE the row's moment — a group takes it at its first moment
	Closing,     // the balance AFTER it — a group takes it at its last
};

// ⭐ A PERIOD'S SENIORITY AS ITS SOURCE ORDERS IT — counted AFTER every seniority a query states itself (Max,
// 2026-09-29): `ROLE PERIOD 1` written on a field puts it first on purpose, and every field nobody numbered
// keeps the order it had — its source's (a period, then a recorder, then a line number), then the order read.
// So roles may be given with no numbers at all, numbered in part, or ordered whole, and each means what it says.
inline constexpr int ibSourcePeriodRank(int order) { return 1000 + order; }

// …AND THE WORD THE QUERY WRITES IT WITH — `<expr> ROLE PERIOD AS Period` (the parser reads these five back).
inline const wchar_t* ibBalanceRoleWord(ibBalanceRole role)
{
	switch (role) {
	case ibBalanceRole::Moment:    return L"PERIOD";
	case ibBalanceRole::Dimension: return L"DIMENSION";
	case ibBalanceRole::Opening:   return L"OPENING";
	case ibBalanceRole::Closing:   return L"CLOSING";
	default:                       return L"NONE";
	}
}

#endif // __QUERY_BALANCE_ROLE_H__
