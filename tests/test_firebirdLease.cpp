// =============================================================================
// OES Enterprise — Firebird lease primitive tests (Phase 4 foundations)
//
// Covers the SMB-byte-range-lock + heartbeat sidecar that drives
// leader-election. Two `ibFirebirdLease` instances on the same path
// simulate two would-be leaders in the same office. The OS-level
// lock guarantees only one wins; the runner-up reads the loser's
// state and watches the generation counter for handoff.
//
// What's intentionally NOT covered here:
//   - Full `ibFirebirdLeaderMode` self-promote loop. That orchestrator
//     runs a 5 s heartbeat thread and a 60 s staleness threshold;
//     observing it requires waiting >60 real seconds or refactoring
//     the loop to accept an injected clock. Lease-level coverage
//     of the WriteSelfAsLeader / ReadCurrentState round-trip is the
//     foundation any orchestrator-level test would build on.
//   - Cross-process file locking. Lock holders are validated within
//     a single process. SMB-share locking across multiple machines
//     is a manual smoke-test concern.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/databaseLayer/firebird/firebirdLease.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

#include <chrono>
#include <thread>

namespace {

// Generate a unique temp path for the (faked) `.fdb` that the lease
// sidecar attaches to. The lease writes `<dbPath>.lease`, so we
// don't need the .fdb itself to exist — the lease class only cares
// about the sidecar.
wxString MakeUniqueDbPath(const char* testName) {
	const wxString base = wxStandardPaths::Get().GetTempDir();
	const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	return wxFileName(base, wxString::Format(
		wxT("oes_test_lease_%s_%lld.fdb"),
		testName, (long long)now)).GetFullPath();
}

// Test fixture — owns a unique dbPath and cleans up the `.lease`
// sidecar in TearDown so re-runs don't inherit stale state.
class FirebirdLeaseTest : public ::testing::Test {
protected:
	wxString dbPath;

	void SetUp() override {
		dbPath = MakeUniqueDbPath(::testing::UnitTest::GetInstance()
		                          ->current_test_info()->name());
	}

	void TearDown() override {
		const wxString leasePath = dbPath + wxT(".lease");
		if (wxFileExists(leasePath))
			wxRemoveFile(leasePath);
	}
};

} // namespace

TEST_F(FirebirdLeaseTest, FreshAcquire_Succeeds) {
	ibFirebirdLease lease(dbPath);
	const auto result = lease.TryAcquireExclusive();
	EXPECT_EQ(result, ibFirebirdLease::AcquireResult::AcquiredLock);
	EXPECT_TRUE(lease.HoldsLock());
}

TEST_F(FirebirdLeaseTest, SecondAcquire_OnSamePath_FailsWithAnotherActive) {
	ibFirebirdLease leaseA(dbPath);
	ASSERT_EQ(leaseA.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);

	ibFirebirdLease leaseB(dbPath);
	EXPECT_EQ(leaseB.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AnotherLeaderActive);
	EXPECT_FALSE(leaseB.HoldsLock());
}

TEST_F(FirebirdLeaseTest, ReleaseThenReacquire_BySecondInstance_Succeeds) {
	ibFirebirdLease leaseA(dbPath);
	ASSERT_EQ(leaseA.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
	leaseA.Release();
	EXPECT_FALSE(leaseA.HoldsLock());

	ibFirebirdLease leaseB(dbPath);
	EXPECT_EQ(leaseB.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
}

TEST_F(FirebirdLeaseTest, WriteThenReadState_RoundtripsHostPortAndGeneration) {
	ibFirebirdLease lease(dbPath);
	ASSERT_EQ(lease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);

	ASSERT_TRUE(lease.WriteSelfAsLeader(wxT("test-host"), 3055));

	const auto state = lease.ReadCurrentState();
	EXPECT_TRUE(state.valid);
	EXPECT_EQ(state.leaderHost, wxT("test-host"));
	EXPECT_EQ(state.leaderPort, 3055);
	EXPECT_GT(state.generation, 0u);
	EXPECT_GT(state.heartbeatUnixMs, 0u);
}

TEST_F(FirebirdLeaseTest, WriteSelfAsLeader_TwiceBumpsGeneration) {
	ibFirebirdLease lease(dbPath);
	ASSERT_EQ(lease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);

	ASSERT_TRUE(lease.WriteSelfAsLeader(wxT("host-1"), 3055));
	const auto first = lease.ReadCurrentState();
	ASSERT_TRUE(first.valid);

	ASSERT_TRUE(lease.WriteSelfAsLeader(wxT("host-2"), 3066));
	const auto second = lease.ReadCurrentState();
	ASSERT_TRUE(second.valid);

	// Generation must strictly increase across handoffs so followers
	// can detect "leader changed" by simple inequality.
	EXPECT_GT(second.generation, first.generation);
	EXPECT_EQ(second.leaderHost, wxT("host-2"));
	EXPECT_EQ(second.leaderPort, 3066);
}

TEST_F(FirebirdLeaseTest, RefreshHeartbeat_UpdatesTimestampWithoutBumpingGeneration) {
	ibFirebirdLease lease(dbPath);
	ASSERT_EQ(lease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
	ASSERT_TRUE(lease.WriteSelfAsLeader(wxT("host"), 3055));

	const auto before = lease.ReadCurrentState();
	ASSERT_TRUE(before.valid);

	// Sleep long enough that the millisecond clock advances. 30 ms
	// is comfortably more than system-clock resolution on every
	// supported platform.
	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	ASSERT_TRUE(lease.RefreshHeartbeat());

	const auto after = lease.ReadCurrentState();
	ASSERT_TRUE(after.valid);

	EXPECT_GT(after.heartbeatUnixMs, before.heartbeatUnixMs);
	// Heartbeat refresh is NOT a handoff — generation stays put.
	EXPECT_EQ(after.generation, before.generation);
}

TEST_F(FirebirdLeaseTest, ReadCurrentState_FromNonHolder_StillSeesLeaderInfo) {
	ibFirebirdLease leaderLease(dbPath);
	ASSERT_EQ(leaderLease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
	ASSERT_TRUE(leaderLease.WriteSelfAsLeader(wxT("the-leader"), 4055));

	// Second instance — represents a follower observing the lease
	// without holding the lock. Must still be able to read the
	// leader's identity.
	ibFirebirdLease followerLease(dbPath);
	ASSERT_EQ(followerLease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AnotherLeaderActive);

	const auto state = followerLease.ReadCurrentState();
	EXPECT_TRUE(state.valid);
	EXPECT_EQ(state.leaderHost, wxT("the-leader"));
	EXPECT_EQ(state.leaderPort, 4055);
}

TEST_F(FirebirdLeaseTest, AfterLeaderRelease_FollowerAcquiresAndSeesPriorGenerationBumped) {
	uint64_t genBeforeRelease = 0;
	{
		ibFirebirdLease leaderLease(dbPath);
		ASSERT_EQ(leaderLease.TryAcquireExclusive(),
		          ibFirebirdLease::AcquireResult::AcquiredLock);
		ASSERT_TRUE(leaderLease.WriteSelfAsLeader(wxT("old-leader"), 5055));
		genBeforeRelease = leaderLease.ReadCurrentState().generation;
		// dtor releases the lock — explicit Release here is belt-and-
		// suspenders to keep the intent obvious.
		leaderLease.Release();
	}

	ibFirebirdLease newLeaderLease(dbPath);
	ASSERT_EQ(newLeaderLease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
	ASSERT_TRUE(newLeaderLease.WriteSelfAsLeader(wxT("new-leader"), 6055));

	const auto state = newLeaderLease.ReadCurrentState();
	EXPECT_TRUE(state.valid);
	EXPECT_EQ(state.leaderHost, wxT("new-leader"));
	EXPECT_EQ(state.leaderPort, 6055);
	// Generation persists across leader handoff — strictly greater
	// than the prior leader's last value. This is the property the
	// `HeartbeatLoop` relies on to detect handoff via inequality.
	EXPECT_GT(state.generation, genBeforeRelease);
}

TEST_F(FirebirdLeaseTest, HeartbeatStaleness_DetectableAtLeaseLevel) {
	ibFirebirdLease lease(dbPath);
	ASSERT_EQ(lease.TryAcquireExclusive(),
	          ibFirebirdLease::AcquireResult::AcquiredLock);
	ASSERT_TRUE(lease.WriteSelfAsLeader(wxT("host"), 3055));

	const auto state = lease.ReadCurrentState();
	ASSERT_TRUE(state.valid);

	// The orchestrator considers a leader dead when
	// (now - heartbeatUnixMs) > kHeartbeatStaleMs. Right after
	// WriteSelfAsLeader, age should be ~0 — verify the predicate
	// in its "fresh" state. Self-promote-on-staleness is exercised
	// at the orchestrator level; this is the lease-level invariant
	// that the staleness measurement is well-defined.
	const uint64_t now = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()
		).count());
	const uint64_t age = (now > state.heartbeatUnixMs)
		? (now - state.heartbeatUnixMs) : 0;
	EXPECT_LT(age, ibFirebirdLease::kHeartbeatStaleMs);
}
