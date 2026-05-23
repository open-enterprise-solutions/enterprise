#include "firebirdHlc.h"
#include "firebirdCommon.h"

#include <wx/datetime.h>

namespace {

using ibFb::NowUnixMs;

// Pack the three components into a single 64-bit HLC value per the
// layout described in the header. `millis` is masked to the 48-bit
// field — the mask wraps silently around year ~11 000 000 AD, which
// is so far past the JIRA closure date this codebase tracks that no
// assertion is needed; the mask is the canonical truncation.
uint64_t Pack(uint64_t millis, uint16_t counter, uint8_t nodeId) {
	return ((millis  & ibHlcClock::kMillisMask)  << ibHlcClock::kMillisShift)
	     | ((uint64_t(counter) & ibHlcClock::kCounterMask) << ibHlcClock::kCounterShift)
	     |  (uint64_t(nodeId)  & ibHlcClock::kNodeIdMask);
}

} // namespace

ibHlcClock::ibHlcClock(uint8_t nodeId)
	: m_nodeId(nodeId & static_cast<uint8_t>(kNodeIdMask))
{
}

uint64_t ibHlcClock::Next() {
	// Note: `prev` is intentionally non-const. compare_exchange_weak
	// takes a mutable lvalue reference and writes back the actual
	// observed value on CAS failure so the loop can retry against
	// fresh state. A const + const_cast pattern is formal UB and
	// can be optimised into an infinite loop under -O2.
	for (;;) {
		uint64_t prev = m_state.load(std::memory_order_acquire);
		const uint64_t prevMs      = ExtractMillis(prev);
		const uint64_t prevCounter = ExtractCounter(prev);

		const uint64_t nowMs = NowUnixMs();
		uint64_t newMs;
		uint16_t newCounter;

		if (nowMs > prevMs) {
			// Wall clock advanced — reset counter.
			newMs      = nowMs;
			newCounter = 0;
		} else {
			// Same millisecond (or wall clock went backward — NTP
			// step). Keep prevMs, bump counter. If counter overflows
			// the 12-bit field, advance prevMs by one as if a
			// millisecond ticked — gives us 4096 events per
			// "logical ms" with bounded forward drift.
			newMs = prevMs;
			if (prevCounter + 1 > kCounterMask) {
				newMs      = prevMs + 1;
				newCounter = 0;
			} else {
				newCounter = static_cast<uint16_t>(prevCounter + 1);
			}
		}

		const uint64_t next = Pack(newMs, newCounter, m_nodeId);
		if (m_state.compare_exchange_weak(
			    prev, next,
			    std::memory_order_acq_rel,
			    std::memory_order_acquire)) {
			return next;
		}
		// Contention — retry. compare_exchange_weak refreshed `prev`
		// with the actual current state; spurious failures also retry.
	}
}

void ibHlcClock::Observe(uint64_t incoming) {
	// Advance our state so that subsequent `Next()` results strictly
	// exceed `incoming`. We push the high bits (millis + counter)
	// past the incoming value, keeping our own node-id at emit.
	const uint64_t incomingHighBits = incoming & ~kNodeIdMask;

	for (;;) {
		uint64_t prev = m_state.load(std::memory_order_acquire);
		const uint64_t prevHighBits = prev & ~kNodeIdMask;
		if (prevHighBits >= incomingHighBits) {
			// Already past it — nothing to do.
			return;
		}
		if (m_state.compare_exchange_weak(
			    prev, incomingHighBits | m_nodeId,
			    std::memory_order_acq_rel,
			    std::memory_order_acquire)) {
			return;
		}
	}
}

int ibHlcClock::Compare(uint64_t lhs, uint64_t rhs) {
	if (lhs == rhs) return 0;
	return (lhs < rhs) ? -1 : 1;
}

uint64_t ibHlcClock::ExtractMillis(uint64_t hlc) {
	return (hlc >> kMillisShift) & kMillisMask;
}

uint16_t ibHlcClock::ExtractCounter(uint64_t hlc) {
	return static_cast<uint16_t>((hlc >> kCounterShift) & kCounterMask);
}

uint8_t ibHlcClock::ExtractNodeId(uint64_t hlc) {
	return static_cast<uint8_t>(hlc & kNodeIdMask);
}

wxString ibHlcClock::Format(uint64_t hlc) {
	const uint64_t ms = ExtractMillis(hlc);
	const uint16_t c  = ExtractCounter(hlc);
	const uint8_t  n  = ExtractNodeId(hlc);

	// FromUNIX in wx returns a UTC wxDateTime directly, no manual
	// .ToUTC() pass needed. Sub-second precision is appended as a
	// literal — wxDateTime's own millisecond field isn't shown by
	// the `%H:%M:%S` format spec.
	const wxDateTime dt = wxDateTime((time_t)(ms / 1000)).ToUTC();
	return wxString::Format(
		wxT("%s.%03lluZ#%u/%u"),
		dt.Format(wxT("%Y-%m-%dT%H:%M:%S")),
		(unsigned long long)(ms % 1000),
		(unsigned)c,
		(unsigned)n);
}
