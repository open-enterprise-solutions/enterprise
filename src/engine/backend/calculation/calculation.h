#ifndef __CALCULATION_H__
#define __CALCULATION_H__

// Calculation engine — the CALCULATION RULES of a calculation register, and nothing that is a metaobject.
//
// Everything here takes values in and gives values out: intervals, ordinals, the facts of a record. The database
// applies these rules to the register's records — the fact (calculationRegisterMetadataTotals.cpp) and the
// recalculations (calculationRegisterRecalculationMetadata.cpp) are relations it computes — and here they are
// stated once, plainly: what the tests hold those relations to (tests/test_calcViews.cpp,
// tests/test_calcRecalculation.cpp), and tested on their own without a base (tests/test_calcDisplacement.cpp).
//
//   DISPLACEMENT  — which days a record is actually in force.
//   LEADING       — which already-computed records a change makes stale.
//   SCHEDULE      — what a schedule gives over a record's days (tests/test_calcSchedule.cpp).
//
// ⭐ A PERIOD OF A CALCULATION RECORD IS A RUN OF WHOLE DAYS, ITS END INCLUDED: a sick leave 12.06–16.06 is five
// days (Max's example, 2026-09-10). The rules read a half-open [start, end) in ticks, so a stored pair is
// [start of its first day, start of the day AFTER its last) to them, and a piece they hand back is [first day,
// last day] again. Reading the end as exclusive cut every period one day short: an absence 1–10 left the salary
// starting on the 10th instead of the 11th.

#include "backend/backend.h"         // BACKEND_API
#include "backend/fnumber.h"         // ibNumber — a schedule's figures

#include <cstddef>
#include <cstdint>
#include <utility>   // std::pair — the relations' edges
#include <vector>

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

// A half-open interval [start, end). Ticks are engine date ticks (any monotonic int64 unit).
struct ibActionInterval {
	int64_t start;
	int64_t end;
};

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

// ====================================================================================================
// LEADING — which already-computed records a change makes stale.
//
// A calculation record is computed FROM other records: its base, the displacement that shaped its action
// period. When one of those changes — a December salary corrected after the January bonus was computed
// over it — the bonus is wrong and nothing about it says so. The chart of calculation types states the
// dependency per type (its Leading section: "a change to the named type's records makes this type's
// records stale"); this rule applies it to records. WHEN either was registered does not matter: the marks
// are left by the write of the leading record, whenever it comes (calculationRegister.h, "The
// recalculation's marks").
// ====================================================================================================

// One record, as the rule needs to see it.
struct ibRecalcFact {
	int     type;           // ordinal of the calculation type in the Leading relation's index; -1 = no edge names it
	int     key;            // ordinal of the record's values on the dimensions the two registers share
	int64_t actionStart;    // the action period, half-open as the displacement rule reads it;
	int64_t actionEnd;      //   actionStart > actionEnd = the register carries no action period
	int64_t baseStart;      // the base period, the same way; baseStart > baseEnd = no base period
	int64_t baseEnd;
	int64_t registration;   // the registration period — the only time a record has when it has no periods
};

// The indexes of `candidates` that some record of `changed` LEADS:
//   - the candidate's type names the changed record's type in its Leading section
//     (an edge {candidate type, changed type} in `leads`),
//   - both agree on every dimension the two registers share (equal `key`),
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

// ====================================================================================================
// SCHEDULE — what a schedule gives over a record's days.
//
// A schedule is a number per day per key: the hours and the work days of one employee's calendar. The schedule
// data sums each of them over every period of every record — its action period, its pieces, its base period, its
// registration period. The reading asks the DATABASE for those sums (a join of the records to the schedule, summed
// by the record — calculationRegisterMetadataTotals.cpp); this states the rule they keep plainly, the way the
// displacement rule above states what the fact's relation keeps: a key's schedule as a RUNNING TOTAL, a sum over
// any run of days two lookups, a day given twice adding up.
// ====================================================================================================

// One key's schedule. Days are whole days as numbers; `resource` numbers the figures a day carries.
class BACKEND_API ibScheduleSeries {
public:
	explicit ibScheduleSeries(size_t resources = 0) : m_before(1, std::vector<ibNumber>(resources)) {}

	// Adds a figure of a day. Days come in ascending order — the reading asks the database for them so — and a day
	// given again adds to what it already has (two rows of one date, a date carrying a time).
	void Add(int64_t day, size_t resource, const ibNumber& value) {
		if (m_days.empty() || m_days.back() != day) {
			m_days.push_back(day);
			m_before.push_back(m_before.back());
		}
		if (resource < m_before.back().size())
			m_before.back()[resource] += value;
	}

	// Figure `resource` summed over the days [from, to], both included. Zero where no day of the schedule falls
	// inside, and for a run that ends before it starts.
	ibNumber Sum(int64_t from, int64_t to, size_t resource) const;

private:
	std::vector<int64_t>               m_days;     // the days that carry figures, ascending
	std::vector<std::vector<ibNumber>> m_before;   // m_before[i][k] — figure k summed over the days before m_days[i];
	                                               // one more at the end, the whole of it
};

#endif
