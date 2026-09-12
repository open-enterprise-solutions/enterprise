#ifndef __CALCULATION_H__
#define __CALCULATION_H__

// Calculation engine — the CALCULATION LOGIC of a calculation register, and nothing that is a metaobject.
//
// Everything here takes values in and gives values out: days, intervals, ordinals, the facts of a record.
// Which records are read, how a column is spelled, where an answer is written — those are the register's
// and the chart's business (calculationRegisterObject.cpp, calculationRegisterManager_impl.cpp,
// chartOfCalculationTypesMetadata.cpp); what is DONE with what they read is here, in one place, where it
// can be tested without a base (tests/test_calcDisplacement.cpp, tests/test_calcRecalculation.cpp).
//
//   DAYS          — a period is whole days, its end included; the rules count in half-open ticks.
//   INTERVALS     — overlapping and widening half-open spans.
//   RELATIONS     — a calculation type's place in a relation a chart was read into.
//   FACTS         — a record as the rules see it, and what a rewrite actually changed.
//   STORNO        — which records a correction of the current period has taken out of force.
//   DISPLACEMENT  — which days a record is actually in force, and which records a write can reach.
//   LEADING       — which already-computed records a change makes stale, and the marks that says.
//
// A base is not here: the database computes it (calculationRegisterManager_impl.cpp, ibCalcReadBase).

#include "backend/backend.h"         // BACKEND_API
#include "backend/compiler/value.h"  // ibValue — the facts a rule reads are the record's own values

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>   // std::pair — the relations' edges
#include <vector>

// ====================================================================================================
// DAYS
//
// ⭐ A PERIOD OF A CALCULATION RECORD IS A RUN OF WHOLE DAYS, ITS END INCLUDED: a sick leave 12.06–16.06
// is five days (Max's example, 2026-09-10). The rules read a half-open [start, end) in ticks, so a stored
// pair becomes [start of its first day, start of the day AFTER its last) — here, in one place for every
// reader — and a piece a rule hands back becomes [first day, last day] again. Reading the end as exclusive
// cut every period one day short: an absence 1–10 left the salary starting on the 10th instead of the 11th.
// ====================================================================================================

inline int64_t ibCalcDayStart(const ibValue& date)
{
	wxDateTime day = date.GetDateTime();
	if (!day.IsValid())
		return 0;
	day.ResetTime();
	return day.GetValue().GetValue();
}

inline int64_t ibCalcDayAfter(const ibValue& date)
{
	wxDateTime day = date.GetDateTime();
	if (!day.IsValid())
		return 0;
	day.ResetTime();
	day += wxDateSpan::Day();
	return day.GetValue().GetValue();
}

// The last day a half-open piece covers — the inverse of ibCalcDayAfter.
inline ibValue ibCalcLastDayOf(int64_t endExclusive)
{
	wxDateTime day{ wxLongLong(endExclusive) };
	day -= wxDateSpan::Day();
	return ibValue(day);
}

// ====================================================================================================
// INTERVALS
// ====================================================================================================

// A half-open interval [start, end). Ticks are engine date ticks (any monotonic int64 unit).
struct ibActionInterval {
	int64_t start;
	int64_t end;
};

// Two half-open spans overlap when they share a moment.
inline bool ibCalcOverlaps(int64_t s1, int64_t e1, int64_t s2, int64_t e2)
{
	return s1 < e2 && e1 > s2;
}

// Widen the half-open [start, end) to take in [s, e): an empty span takes the first period as it is, and
// an empty period widens nothing. The span of what a write touched is how far its effects can reach.
inline void ibCalcWiden(int64_t& start, int64_t& end, int64_t s, int64_t e)
{
	if (e <= s)
		return;
	if (end <= start) {
		start = s;
		end = e;
		return;
	}
	if (s < start) start = s;
	if (e > end)   end = e;
}

// ====================================================================================================
// RELATIONS — a chart's relation (Displacing, Base, Leading) is read into an INDEX that numbers each
// calculation type the first time it is met, and edges between those numbers
// (ibValueMetaObjectChartOfCalculationTypes::ReadRelation). The rules speak in the numbers.
// ====================================================================================================

// The ordinal a type holds in such an index, or -1 if no edge names it — a record of such a type
// neither cuts nor is cut, and leads nothing.
inline int ibCalcTypeOrdinal(const std::map<ibValue, int>& typeIndex, const ibValue& type)
{
	if (type.IsEmpty())
		return -1;
	const auto it = typeIndex.find(type);
	return it != typeIndex.end() ? it->second : -1;
}

// ====================================================================================================
// FACTS — ONE RECORD AS THE RULES READ IT, by column, from whatever holds it: the rows a recorder has
// stored, the set about to replace them, or the records a change may lead.
// ====================================================================================================

struct ibCalcRecordFacts {
	ibValue recorder;
	ibValue type;
	std::map<wxString, ibValue> dims;            // every dimension of the register, by upper-cased name
	int64_t actionStart = 0, actionEnd = -1;      // start > end = the register has no action period
	int64_t baseStart = 0, baseEnd = -1;          // the same for the base period
	int64_t registration = 0;
	ibValue actionPeriod;                         // the month the record is FOR — part of its position (STORNO)
	bool    storno = false;                       // the record reverses an earlier one of the same position
	std::vector<ibValue> line;                    // everything the line SAYS — what "the same line" is compared by
};

// ====================================================================================================
// STORNO — A CORRECTION IS A MOVEMENT OF THE CURRENT PERIOD, NOT AN EDIT OF A CLOSED ONE.
//
// A past month that was paid is not posted again: "what did we pay in June" must still have its answer.
// The next run registers, in ITS period, a STORNO of the June record — the same POSITION (dimensions,
// calculation type, action period and the month it is for), flagged, with the opposite amount — and the
// June record computed anew beside it. So a record can be taken out of force by a later one, and the
// engine has to know which:
//   * a storno record is never in force itself — it reverses, it does not act;
//   * it takes out of force the LATEST record of its position registered BEFORE it that nothing has
//     taken out yet: June's record by July's storno, and July's correction by August's, in that order;
//   * a record out of force has no actual action period, displaces nothing and is no base by its action
//     period. Summed by REGISTRATION period the two simply add up — the storno's minus against the
//     original's plus — which is the difference booked where it was paid.
// A record registered in the same period as the storno (the correction beside it) is not its target.
// ====================================================================================================

// result[i] pairs with records[i]: whether it is in force. `position` is what a record must share with its
// storno — whatever key the caller builds from the dimensions, the type, the action period and the month
// it is for; `registration` orders the records; `storno` flags the reversals.
struct ibCalcStornoFact {
	std::vector<ibValue> position;
	int64_t              registration = 0;
	bool                 storno = false;
};
BACKEND_API std::vector<bool> ibCalcInForce(const std::vector<ibCalcStornoFact>& records);

// The position a record's facts state, in one key: its dimensions (in the map's order), its type, its
// action period and the month it is for.
BACKEND_API std::vector<ibValue> ibCalcPositionOf(const ibCalcRecordFacts& facts);

// 🛑 A LINE REWRITTEN AS IT WAS IS NOT A CHANGE. The first version took "changed" as everything the
// recorder held and holds — MEASURED 2026-09-10: December rewritten with ONE employee's salary corrected
// marked BOTH employees' January bonuses, the untouched one included; re-posting a month in bulk would
// have marked every document that ever read it. So the two sides are compared as MULTISETS of what the
// lines say, and only what is on one side and not the other is returned: a removed line, an added line,
// and a changed line (which is both).
BACKEND_API std::vector<ibCalcRecordFacts> ibCalcWhatChanged(std::vector<ibCalcRecordFacts> before,
                                                             std::vector<ibCalcRecordFacts> after);

// ====================================================================================================
// DISPLACEMENT BY THE RELATION ITSELF — the semantics a chart of calculation types actually declares.
//
// A calculation record is in force over an INTERVAL [start, end) — its action period. Where records of
// competing types overlap, a record is only actually in force over the parts NOT covered by a record of a
// type that displaces it; that remainder is its ACTUAL action period.
//
// 🛑⭐⭐ A PRIORITY RANK IS NOT THE RELATION. The chart says, per calculation type, WHICH types displace
// it — a partial order. Folding that into one number per type (a longest-chain rank) makes it TOTAL:
// with "C displaces B, C displaces A, B displaces Z", B ranks 1 and A ranks 0, and a kernel that lets any
// higher number win would have B displace A — an edge nobody declared. Here a record is cut only by
// records whose type is one of ITS OWN type's declared displacers. (The rank kernel and the helper that
// computed the rank stood here until 2026-09-10, with no caller left in the product.)
//
// MEASURED 2026-09-10 on a live base: the record-set write passed "priority = line order", so the same
// two records gave opposite results depending on the order they were added — Absence displaced Salary,
// or Salary displaced Absence, with the chart's Displacing list never consulted.
// ====================================================================================================

struct ibActionPeriodTypedRecord {
	int     type;    // dense index of the record's calculation type, [0, typeCount)
	int64_t start;   // inclusive
	int64_t end;     // exclusive; caller guarantees end >= start
};

// displacedBy — edges {low, high}: type `high` DISPLACES type `low`, exactly as the chart declares it
// (one edge per row of a type's Displacing section). Out-of-range and self edges are ignored.
// result[i] pairs with records[i]: its action period minus the union of the action periods of every
// record whose type directly displaces records[i].type. Empty = fully displaced.
BACKEND_API std::vector<std::vector<ibActionInterval>>
ibComputeActionPeriodDisplacementByRelation(size_t typeCount,
                                            const std::vector<std::pair<int, int>>& displacedBy,
                                            const std::vector<ibActionPeriodTypedRecord>& records);

// ---- WITHIN THE WINDOW A WRITE CAN REACH -------------------------------------------------------------
//
// A record's pieces are made by the records that OVERLAP it, and a write only adds or removes records
// inside the span of what it wrote (the WINDOW, before and after together). So:
//   S1 — the records of the touched keys that meet the window: the only ones whose pieces can change;
//   W' — the span of S1 with the window (the REACH): everything that can cut an S1 record lies inside it;
//   S  — the records of the touched keys that meet W': the context S1 is displaced against.
// Only S1's pieces are written again; what lies beyond W' is never needed.

// One record as the window pass sees it: its values on the register's dimensions (the key displacement
// runs within — one employee's sick leave cuts that employee's salary only), its type's ordinal in the
// Displacing relation, and its action period in ticks.
struct ibCalcWindowRecord {
	std::vector<ibValue> key;
	int                  type = -1;
	int64_t              start = 0;
	int64_t              end = 0;
	// Its position and place in time for STORNO (see above): a record a later storno takes out of force,
	// and the storno itself, have no pieces and cut nothing. `kind` is the calculation type itself — a
	// type the Displacing relation never names still reverses and is reversed.
	ibValue              kind;
	ibValue              actionPeriod;
	int64_t              registration = 0;
	bool                 storno = false;
};

// W': the window widened by every one of `records` that meets it.
BACKEND_API void ibCalcReach(const std::vector<ibCalcWindowRecord>& records, int64_t windowStart, int64_t windowEnd,
                             int64_t& reachStart, int64_t& reachEnd);

// result[i] pairs with records[i]: the pieces of a record that meets the window (an S1 record), displaced
// against every record of its key in `records` that is IN FORCE; EMPTY for a record that only gives
// context — its pieces were not erased and are not to be written — and for one out of force (a storno, or
// what a storno reverses). Pass S, i.e. everything meeting the reach.
BACKEND_API std::vector<std::vector<ibActionInterval>>
ibCalcPiecesInWindow(size_t typeCount, const std::vector<std::pair<int, int>>& displacedBy,
                     const std::vector<ibCalcWindowRecord>& records, int64_t windowStart, int64_t windowEnd);

// ====================================================================================================
// LEADING — which already-computed records a change makes stale.
//
// A calculation record is computed FROM other records: its base, the displacement that shaped its action
// period. When one of those changes — a December salary corrected after the January bonus was computed
// over it — the bonus is wrong and nothing about it says so. The chart of calculation types states the
// dependency per type (its Leading section: "a change to the named type's records makes this type's
// records stale"); these rules apply it to records.
// ====================================================================================================

// One record, as the rule needs to see it.
struct ibRecalcFact {
	int     type;           // ordinal of the calculation type in the Leading relation's index; -1 = no edge names it
	int     key;            // ordinal of the record's values on the recalculation's dimensions
	int64_t actionStart;    // the action period, half-open as the displacement rule reads it;
	int64_t actionEnd;      //   actionStart > actionEnd = the register carries no action period
	int64_t baseStart;      // the base period, the same way; baseStart > baseEnd = no base period
	int64_t baseEnd;
	int64_t registration;   // the registration period — the only time a record has when it has no periods
};

// The indexes of `candidates` that some record of `changed` LEADS:
//   - the candidate's type names the changed record's type in its Leading section
//     (an edge {candidate type, changed type} in `leads`),
//   - both agree on every dimension of the recalculation (equal `key`),
//   - and they MEET IN TIME: the changed record's action period overlaps the candidate's action period
//     (it displaced or was displaced there), or the changed record falls into the candidate's base period
//     the way the candidate's BASE reads it (it was, or would have been, read as base). Where the changed
//     record has no action period, or the candidate has neither period, meeting is sharing the
//     registration period.
//
// ⚠ "THE WAY THE BASE READS IT" is the dependent chart's BaseDependence, and the rule must give the same
// answer GetBase gives. By action period (the default) a record is read by the days it is in force; by
// registration period (`baseByRegistration`) a record counts when it is REGISTERED inside the base period,
// and nothing else about it matters. Asked by action period, a tax whose base is June's registrations was
// marked stale by a June sick leave registered in July — a mark on a closed month that nothing would ever
// answer, since the leave belongs to July's base (2026-09-10).
// A candidate is listed once however many changed records lead it. `leads` holds {dependent, leading}.
BACKEND_API std::vector<size_t> ibFindLedRecords(const std::vector<ibRecalcFact>& changed,
                                                 const std::vector<ibRecalcFact>& candidates,
                                                 const std::vector<std::pair<int, int>>& leads,
                                                 bool baseByRegistration = false);

// The dependent types a change of `changedTypes` leads — the question asked BEFORE any candidate is read,
// so the read can be narrowed to those types. `leads` holds {dependent, leading}.
BACKEND_API std::set<int> ibCalcTypesLedBy(const std::set<int>& changedTypes,
                                           const std::vector<std::pair<int, int>>& leads);

// One mark a recalculation is to hold: the led candidate (an index into the candidates passed) and its
// values on the recalculation's dimensions, in their order.
struct ibCalcLedMark {
	size_t               candidate = 0;
	std::vector<ibValue> values;
};

// The marks a recalculation keyed by `dimensionNames` (upper-cased, its own order) is to hold after
// `changed`: every candidate the rule leads, ONCE per the table's own key (recorder, type, values, and
// the month the record is for — a mark names the position).
// A recalculation dimension is matched to the register's by NAME — the same rule GetBase uses between
// a register and its base register. `baseByRegistration` as ibFindLedRecords reads it.
BACKEND_API std::vector<ibCalcLedMark> ibCalcLedMarks(const std::vector<ibCalcRecordFacts>& changed,
                                                      const std::vector<ibCalcRecordFacts>& candidates,
                                                      const std::vector<wxString>& dimensionNames,
                                                      const std::map<ibValue, int>& typeIndex,
                                                      const std::vector<std::pair<int, int>>& leads,
                                                      bool baseByRegistration = false);

#endif
