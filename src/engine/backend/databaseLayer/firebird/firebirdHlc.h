#ifndef __FIREBIRD_HLC_H__
#define __FIREBIRD_HLC_H__

// Hybrid Logical Clock — globally-unique, causally-ordered timestamp
// for distributed conflict resolution in mesh-replicated FB.
//
// Combines a wall-clock millisecond timestamp with a local counter
// so multiple events in the same millisecond still produce
// monotonically increasing values. Cross-node ordering relies on
// loose NTP sync (±100 ms typical on LAN); ties broken by
// node-id appended as low bits.
//
// Format (64-bit):
//
//   bits 63..16  — physical wall-clock millis (lower 48 bits — covers
//                  ~8900 years past Unix epoch, enough for forever)
//   bits 15..4   — logical counter (4096 max events per ms per node)
//   bits  3..0   — node-id low nibble (4 bits — up to 16 peers
//                  distinguished at the bit level; broader mesh
//                  topologies must combine HLC with explicit node
//                  identification, see ibHlcStamp).
//
// Last-writer-wins conflict resolution is straightforward
// `lhs.value < rhs.value`. Producer-side guarantee: `Next()` always
// returns strictly greater than any value previously returned by
// `Next()` on the same node within this process.
//
// Cross-node: when applying a replicated update with `incoming.ms`
// higher than our wall clock, we advance our wall-clock pointer to
// match — keeps the HLC monotonic even when the system clock is
// slow / lags an upstream peer.
//
// NOT a substitute for a vector clock. Concurrent writes on two
// nodes within the same millisecond resolve by node-id tiebreak,
// not by causal relationship. That's the "eventual consistency
// with deterministic resolution" trade-off documented in
// docs/firebird-mesh-driver.md §"HLC clock for conflict resolution".

#include "backend/backend.h"

#include <atomic>
#include <cstdint>

class BACKEND_API ibHlcClock {
public:
	// Construct with a node-id (0..15 — see top-of-file format
	// note). Distinct values across peers required for correct
	// tiebreak; producer of `mesh.conf` assigns them at peer-join
	// time.
	explicit ibHlcClock(uint8_t nodeId);

	// Generate the next HLC value for an event happening NOW on
	// this node. Strictly monotonic per-instance. Thread-safe.
	uint64_t Next();

	// Update our internal wall-clock pointer in response to an
	// incoming replicated event with a higher HLC. Called by the
	// replication apply path right before recording the foreign
	// update locally. Keeps `Next()` from ever returning a value
	// less than `incoming` for our own subsequent events.
	void Observe(uint64_t incoming);

	// Tiebreaker comparator for two HLC values from possibly
	// different nodes. Used by conflict resolver:
	//
	//     if (Compare(local, incoming) < 0)  → apply incoming
	//     else                                → keep local
	//
	// Strict ordering: equal physical ms → counter wins; equal
	// counter → node-id wins (deterministic across cluster).
	static int Compare(uint64_t lhs, uint64_t rhs);

	// Extract the physical-ms component (for debug / monitoring).
	static uint64_t ExtractMillis(uint64_t hlc);

	// Extract counter + node id (for debug / monitoring).
	static uint16_t ExtractCounter(uint64_t hlc);
	static uint8_t  ExtractNodeId (uint64_t hlc);

	// Format an HLC value as `<ISO-timestamp>#<counter>/<nodeId>`
	// for log lines.
	static wxString Format(uint64_t hlc);

	// Bit layout — exposed for tests and external packing.
	static constexpr int kNodeIdBits   = 4;
	static constexpr int kCounterBits  = 12;
	static constexpr int kMillisBits   = 48;
	static constexpr int kCounterShift = kNodeIdBits;
	static constexpr int kMillisShift  = kNodeIdBits + kCounterBits;
	static constexpr uint64_t kNodeIdMask  = (uint64_t(1) << kNodeIdBits)  - 1;
	static constexpr uint64_t kCounterMask = (uint64_t(1) << kCounterBits) - 1;
	static constexpr uint64_t kMillisMask  = (uint64_t(1) << kMillisBits)  - 1;

private:
	const uint8_t m_nodeId;

	// Combined state: high bits = last physical ms returned, low
	// bits = last counter. Updated atomically via CAS so concurrent
	// `Next()` calls on different threads can't produce duplicates.
	std::atomic<uint64_t> m_state{0};
};

#endif // __FIREBIRD_HLC_H__
