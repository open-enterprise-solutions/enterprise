////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accounting register manager - the runtime entrances to the five readings
////////////////////////////////////////////////////////////////////////////
//
// THIN ON PURPOSE. A reading lives on the METAOBJECT (accountingRegisterMetadataTotals.cpp); this file
// only builds the call-scoped companion from the script's arguments, reads it through the L3 door, and
// dresses the rows as a value table. The manager holds no aggregate knowledge of its own — that is
// what made the previous version 1074 lines.
//
// ⛔ WHAT WAS HERE BEFORE, AND WHY IT IS GONE. Four aggregates built as concatenated SQL strings with
// hand-bound positional parameters, and the EXECUTION half of every one of them disabled by `#if 0` —
// the text was composed, never run, an empty table returned, nothing reported. Six years of accounting
// logic that answered "no rows" to every question. It bypassed the access policy, the dialect layer
// and paging, and on PostgreSQL it met the binary-result hazard besides. Reviving it would have opened
// territory rather than closed it, so it is deleted and the readings stand on the same door every
// other register reads through.
//
// ⭐ ONE ARGUMENT LIST FOR BOTH ENTRANCES. A script calling `Balance(...)` and a query naming
// `<Register>.Balance` are two doors into one reading, and they read their arguments with the SAME
// function (ibAcctParseCall) against the SAME layout (ibAcctArgs). The alternative — a parser per door
// — agrees until the day a parameter is added to one of them.
//
// ⚠ AND EVERY ARGUMENT IT PARSED IS HANDED ON. The condition arrives in TWO forms — a predicate over
// the register's dimensions, and the raw structure whose breakdown half is asked of the slots per side
// — and passing only the first left the script door quietly answering a wider question than the query
// door for the same call. A defaulted parameter is exactly the kind of omission nothing reports.

#include "accountingRegisterManager.h"

#include "backend/system/value/valueTable.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/appData.h"
#include "backend/session/session.h"

namespace {

// (The dressing helper moved to `ibRegSelectionToTable`, registerQueryLowering.h — this file had the
//  shape the other two registers wanted (ask the source what it publishes) and they now share it. The
//  one thing it lacked came from the copies it replaced: a column's CAPTION. It handed `GetName()` in
//  as the presentation, so an accounting reading showed `Resource1BalanceDr` where the accumulation
//  one showed "Amount Balance"; the surface carries the caption now and both doors read it.)

// (The check itself is ibRequireOpenBase, in registerQueryLowering.h — this register had NAMED the
// question while its two neighbours spelled it out in three other ways. The name was the right half;
// the wrong half was that it was named here.)

// Read one companion through the door and dress the result. The companion is built on the stack: a
// runtime call owns its reading for exactly the length of the call.
template <typename TCompanion, typename... TArgs>
ibValue ReadThrough(const ibValueMetaObjectAccountingRegister* meta, ibAcctShape shape,
                    const ibRegFold& fold, const std::vector<ibValue>& kindsDr,
                    const std::vector<ibValue>& kindsCr, TArgs&&... args)
{
	ibRequireOpenBase();

	TCompanion companion(meta, std::forward<TArgs>(args)...);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&companion).Execute(ibReadPageRequest{});
	return ibRegSelectionToTable(selection, meta->GetShapeQueryable(shape, kindsDr, kindsCr, fold));
}

} // namespace

ibValue ibValueManagerDataObjectAccountingRegister::Balance(ibValue** paParams, const long lSizeArray)
{
	const ibAcctCallArgs call = ibAcctParseCall(m_metaObject, ibAcctShape::Balance, paParams, lSizeArray);
	return ReadThrough<ibAcctBalanceQueryable>(m_metaObject, ibAcctShape::Balance, ibRegFold(),
		call.m_kindsDr, call.m_kindsCr,
		call.m_begin, call.m_accountDr, call.m_accountCr, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
}

ibValue ibValueManagerDataObjectAccountingRegister::Turnovers(ibValue** paParams, const long lSizeArray)
{
	const ibAcctCallArgs call = ibAcctParseCall(m_metaObject, ibAcctShape::Turnovers, paParams, lSizeArray);
	return ReadThrough<ibAcctTurnoverQueryable>(m_metaObject, ibAcctShape::Turnovers, call.m_fold,
		call.m_kindsDr, call.m_kindsCr,
		call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
		call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition);
}

// ⚠ CORRESPONDENCE ONLY, and it says so rather than answering with an empty table. A one-sided
// register never wrote down which debit answered which credit, so there is no pairing to report; a
// silent empty result would read as "these accounts never corresponded", which is a different — and
// false — statement.
ibValue ibValueManagerDataObjectAccountingRegister::DrCrTurnovers(ibValue** paParams, const long lSizeArray)
{
	if (m_metaObject == nullptr || !m_metaObject->IsCorrespondence())
		ibBackendCoreException::Error(_("DrCrTurnovers requires a register in correspondence mode: a one-sided line does not record which debit answered which credit"));

	const ibAcctCallArgs call = ibAcctParseCall(m_metaObject, ibAcctShape::DrCrTurnovers, paParams, lSizeArray);
	return ReadThrough<ibAcctDrCrTurnoverQueryable>(m_metaObject, ibAcctShape::DrCrTurnovers, ibRegFold(),
		call.m_kindsDr, call.m_kindsCr,
		call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
		call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
}

ibValue ibValueManagerDataObjectAccountingRegister::BalanceAndTurnovers(ibValue** paParams, const long lSizeArray)
{
	const ibAcctCallArgs call = ibAcctParseCall(m_metaObject, ibAcctShape::BalanceAndTurnovers, paParams, lSizeArray);
	return ReadThrough<ibAcctBalanceAndTurnoverQueryable>(m_metaObject, ibAcctShape::BalanceAndTurnovers, call.m_fold,
		call.m_kindsDr, call.m_kindsCr,
		call.m_begin, call.m_end, call.m_accountDr, call.m_accountCr,
		call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_fold, call.m_condition);
}


ibValue ibValueManagerDataObjectAccountingRegister::RecordsWithAccountDimensions(ibValue** paParams, const long lSizeArray)
{
	const ibAcctCallArgs call = ibAcctParseCall(m_metaObject, ibAcctShape::Records, paParams, lSizeArray);

	ibRegFold recordFold;
	recordFold.m_kind = ibRegGranularity::Record;   // a row IS a movement line: period, document, line

	return ReadThrough<ibAcctRecordsQueryable>(m_metaObject, ibAcctShape::Records, recordFold,
		call.m_kindsDr, call.m_kindsCr,
		call.m_begin, call.m_end, call.m_kindsDr, call.m_kindsCr, call.m_filter, call.m_condition);
}
