#include "calculation.h"

#include <algorithm>
#include <set>

namespace {

// Sort `iv` and compact it IN PLACE into a minimal non-overlapping cover; returns the logical size of
// the merged prefix [0, N). Adjacent intervals ([a,b)+[b,c)=[a,c)) merge too — a gap needs a strictly
// positive width to survive, which is what makes "the parts NOT covered" come out right. In place so no
// second vector is allocated per record.
size_t MergeInPlace(std::vector<ibActionInterval>& iv)
{
	// Drop empty intervals, compacting toward the front.
	size_t w = 0;
	for (size_t r = 0; r < iv.size(); ++r)
		if (iv[r].end > iv[r].start)
			iv[w++] = iv[r];
	if (w <= 1)
		return w;

	std::sort(iv.begin(), iv.begin() + w,
		[](const ibActionInterval& a, const ibActionInterval& b) {
			return a.start != b.start ? a.start < b.start : a.end < b.end;
		});

	size_t m = 0;   // last kept merged index
	for (size_t r = 1; r < w; ++r) {
		if (iv[r].start <= iv[m].end) {            // overlap or touch -> extend
			if (iv[r].end > iv[m].end) iv[m].end = iv[r].end;
		} else {
			iv[++m] = iv[r];
		}
	}
	return m + 1;
}

// Subtract the merged cover in cover[0, coverN) from base [s, e), APPENDING the remaining sub-intervals
// straight into `out` (no temporary vector, no return copy).
void SubtractCoverInto(int64_t s, int64_t e, const std::vector<ibActionInterval>& cover, size_t coverN,
                       std::vector<ibActionInterval>& out)
{
	if (e <= s)
		return;   // empty base -> nothing remains
	int64_t cur = s;
	for (size_t k = 0; k < coverN; ++k) {
		const ibActionInterval& c = cover[k];
		if (c.end <= cur)          continue;   // entirely before the cursor
		if (c.start >= e)          break;      // sorted -> the rest are past the base
		if (c.start > cur)
			out.push_back({ cur, (c.start < e ? c.start : e) });
		if (c.end > cur) cur = c.end;
		if (cur >= e)              break;      // base fully consumed
	}
	if (cur < e)
		out.push_back({ cur, e });
}

bool HasSpan(int64_t start, int64_t end) { return start <= end; }

// Two half-open periods meet when they share a moment. Equal starts always meet, so a zero-length
// period (a record in force for no time at all — fully displaced, or dated to one instant) still meets
// a period that begins at the same moment rather than being invisible to every rule.
bool Meet(int64_t s1, int64_t e1, int64_t s2, int64_t e2)
{
	return s1 == s2 || std::max(s1, s2) < std::min(e1, e2);
}

bool MeetInTime(const ibRecalcFact& candidate, const ibRecalcFact& changed, bool baseByRegistration)
{
	bool compared = false;
	if (HasSpan(changed.actionStart, changed.actionEnd)) {
		if (HasSpan(candidate.actionStart, candidate.actionEnd)) {
			compared = true;
			if (Meet(candidate.actionStart, candidate.actionEnd, changed.actionStart, changed.actionEnd))
				return true;
		}
		if (!baseByRegistration && HasSpan(candidate.baseStart, candidate.baseEnd)) {
			compared = true;
			if (Meet(candidate.baseStart, candidate.baseEnd, changed.actionStart, changed.actionEnd))
				return true;
		}
	}
	// A base read by registration period takes a record REGISTERED inside it — the day it was registered
	// on is the whole of the question (calculation.h, ibFindLedRecords).
	if (baseByRegistration && HasSpan(candidate.baseStart, candidate.baseEnd)) {
		compared = true;
		if (candidate.baseStart <= changed.registration && changed.registration < candidate.baseEnd)
			return true;
	}
	// No period on one side to weigh against the other: the registration period is the only time both
	// records have.
	return !compared && candidate.registration == changed.registration;
}

} // namespace

// ---- DISPLACEMENT ---------------------------------------------------------------------------------------

std::vector<std::vector<ibActionInterval>>
ibComputeActionPeriodDisplacementByRelation(size_t typeCount,
                                            const std::vector<std::pair<int, int>>& displacedBy,
                                            const std::vector<ibActionPeriodTypedRecord>& records)
{
	const size_t n = records.size();
	std::vector<std::vector<ibActionInterval>> out(n);
	if (n == 0)
		return out;

	// displacers[t] — the types that DIRECTLY displace type t, as declared. No closure is taken: the
	// chart names each displacer on the displaced type's own row, and a displacer that is not named
	// there does not cut, however "higher" some rank would place it.
	std::vector<std::vector<char>> displaces(typeCount, std::vector<char>(typeCount, 0));
	for (const auto& e : displacedBy) {
		const int low = e.first, high = e.second;
		if (low < 0 || high < 0 || (size_t)low >= typeCount || (size_t)high >= typeCount || low == high)
			continue;
		displaces[(size_t)low][(size_t)high] = 1;   // high cuts low
	}

	// Per record: the union of the action periods of every record whose type displaces this one, then
	// its own span minus that union. The sets this runs on are one key's records — tens of rows at most —
	// so the direct form is the right one.
	std::vector<ibActionInterval> cover;
	for (size_t i = 0; i < n; ++i) {
		const ibActionPeriodTypedRecord& r = records[i];
		if (r.end <= r.start)
			continue;   // empty action period — nothing to keep
		cover.clear();
		const bool typed = r.type >= 0 && (size_t)r.type < typeCount;
		if (typed) {
			for (size_t k = 0; k < n; ++k) {
				if (k == i)
					continue;
				const ibActionPeriodTypedRecord& d = records[k];
				if (d.end <= d.start || d.type < 0 || (size_t)d.type >= typeCount)
					continue;
				if (displaces[(size_t)r.type][(size_t)d.type])
					cover.push_back({ d.start, d.end });
			}
		}
		const size_t coverN = MergeInPlace(cover);
		SubtractCoverInto(r.start, r.end, cover, coverN, out[i]);
	}
	return out;
}

// ---- LEADING --------------------------------------------------------------------------------------------

std::vector<size_t> ibFindLedRecords(const std::vector<ibRecalcFact>& changed,
                                     const std::vector<ibRecalcFact>& candidates,
                                     const std::vector<std::pair<int, int>>& leads,
                                     bool baseByRegistration)
{
	std::vector<size_t> led;
	if (changed.empty() || candidates.empty() || leads.empty())
		return led;

	const std::set<std::pair<int, int>> edge(leads.begin(), leads.end());   // {dependent, leading}

	for (size_t c = 0; c < candidates.size(); ++c) {
		const ibRecalcFact& candidate = candidates[c];
		if (candidate.type < 0)
			continue;   // a type no edge names depends on nothing
		for (const ibRecalcFact& y : changed) {
			if (y.type < 0 || y.key != candidate.key)
				continue;
			if (edge.count({ candidate.type, y.type }) == 0)
				continue;
			if (!MeetInTime(candidate, y, baseByRegistration))
				continue;
			led.push_back(c);
			break;   // listed once, however many changes lead it
		}
	}
	return led;
}
