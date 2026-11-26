#include "firebirdLease.h"
#include "firebirdCommon.h"
#include "core/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h
#include "core/diagnostics/crashGuard.h"   // ibCrashGuard::CurrentPidPortable

#include <algorithm>
#include <cstring>

namespace {

// Fixed binary layout — matches the byte-offset table in the header
// comment. Packed so the in-memory image matches the on-disk image
// exactly; no platform-specific padding tricks (only int + char
// fields, all naturally aligned).
#pragma pack(push, 1)
struct LeaseFileV1 {
	char     magic[4];               // 'O','E','S','L'
	uint32_t version;                // 1
	uint64_t generation;             // monotonic, bumps per handoff
	uint64_t heartbeatUnixMs;        // refreshed periodically
	uint32_t leaderPid;
	uint16_t leaderPort;
	char     leaderHost[64];         // ASCII, zero-padded
	uint32_t reserved;               // future flags
};
#pragma pack(pop)

static_assert(sizeof(LeaseFileV1) == 4 + 4 + 8 + 8 + 4 + 2 + 64 + 4,
              "LeaseFileV1 layout drifted - readers and writers must agree");

constexpr uint32_t kLeaseFileSize = sizeof(LeaseFileV1);

// Magic constants.
constexpr char kMagic0 = 'O';
constexpr char kMagic1 = 'E';
constexpr char kMagic2 = 'S';
constexpr char kMagic3 = 'L';
constexpr uint32_t kVersion = 1;

using ibFb::NowUnixMs;

bool ReadLeaseFile(const ibSharedFile& file, LeaseFileV1& out)
{
	return file.ReadAt(0, &out, kLeaseFileSize);
}

bool WriteLeaseFile(ibSharedFile& file, const LeaseFileV1& in)
{
	if (!file.WriteAt(0, &in, kLeaseFileSize))
		return false;
	if (!file.Flush()) {
		// Best-effort — slow on SMB but worth pushing through so
		// followers' next-read sees consistent state. Log to surface
		// SMB-disconnect / disk-full conditions that would otherwise
		// leave followers reading the prior generation forever.
		ibJournalWarning(ibT("db.firebird"), ibT("ibFirebirdLease: the flush of %s failed - ")
		             ibT("followers may see stale state until next write"), file.GetPath());
	}
	return true;
}

bool LookValid(const LeaseFileV1& f) {
	return f.magic[0] == kMagic0 && f.magic[1] == kMagic1
	    && f.magic[2] == kMagic2 && f.magic[3] == kMagic3
	    && f.version  == kVersion;
}

} // namespace

ibFirebirdLease::ibFirebirdLease(const ibString& dbPath)
	: m_dbPath(dbPath)
	, m_leasePath(dbPath + ibT(".lease"))
{
}

ibFirebirdLease::~ibFirebirdLease() {
	Release();
}

ibFirebirdLease::AcquireResult ibFirebirdLease::TryAcquireExclusive() {
	if (HoldsLock()) return AcquireResult::AcquiredLock;

	// Every process opens the file for reading and writing, shared: exclusivity between them is the sentinel byte's, not
	// the handle's — a handle opened for reading only would refuse the others' opening, and they could not even probe
	// whether somebody holds it.
	auto file = std::make_unique<ibSharedFile>(m_leasePath);
	if (!file->IsOpen()) {
		ibJournalWarning(ibT("db.firebird"), ibT("ibFirebirdLease: %s cannot be opened"), m_leasePath);
		return AcquireResult::IOError;
	}

	// EXCLUSIVE, NEVER WAITING, on a SENTINEL byte past the lease content: a held byte refuses another handle's read of it
	// on Windows (on a share as well), so the content stays readable to the followers while only one process holds the
	// sentinel. The byte is the open file's, not the process's (ibFileByteLock): a second lease in the same process meets
	// the first as another process's does — two lease tests failed on Linux while it was the process's.
	ibFileByteLock lock(*file, kLockSentinelOffset, ibFileByteLock::ibMode::Exclusive);
	if (!lock.HoldsByte())
		return AcquireResult::AnotherLeaderActive;   // the follower keeps no file open
	m_file = std::move(file);
	m_lock = std::move(lock);
	return AcquireResult::AcquiredLock;
}

bool ibFirebirdLease::WriteSelfAsLeader(const ibString& host, uint16_t port) {
	if (!HoldsLock()) return false;

	// Read current state to preserve generation; bump on every
	// handoff (= every WriteSelfAsLeader call) so followers can
	// distinguish "same leader, refreshed heartbeat" from "new
	// leader took over".
	LeaseFileV1 cur{};
	const bool hadOld = ReadLeaseFile(*m_file, cur);
	uint64_t nextGen = 1;
	if (hadOld && LookValid(cur))
		nextGen = cur.generation + 1;

	LeaseFileV1 fresh{};
	fresh.magic[0] = kMagic0;
	fresh.magic[1] = kMagic1;
	fresh.magic[2] = kMagic2;
	fresh.magic[3] = kMagic3;
	fresh.version          = kVersion;
	fresh.generation       = nextGen;
	fresh.heartbeatUnixMs  = NowUnixMs();
	fresh.leaderPid        = (uint32_t)ibCrashGuard::CurrentPidPortable();
	fresh.leaderPort       = port;
	// host: zero-pad / truncate to 63 + NUL.
	const auto hostUtf8 = host.utf8_str();
	const size_t hostLen = std::min<size_t>(strlen(hostUtf8.data()), 63);
	std::memcpy(fresh.leaderHost, hostUtf8.data(), hostLen);
	// remainder already zero from value-init
	fresh.reserved = 0;

	return WriteLeaseFile(*m_file, fresh);
}

bool ibFirebirdLease::WriteVacating() {
	if (!HoldsLock()) return false;

	LeaseFileV1 cur{};
	if (!ReadLeaseFile(*m_file, cur)) return false;
	if (!LookValid(cur)) return false;

	// pid = 0 sentinel — real OS pids never zero. Followers see this
	// as "leader gracefully gone" and self-promote on next tick
	// without waiting out the heartbeat staleness timeout.
	cur.leaderPid       = 0;
	cur.heartbeatUnixMs = NowUnixMs();
	return WriteLeaseFile(*m_file, cur);
}

bool ibFirebirdLease::RefreshHeartbeat() {
	if (!HoldsLock()) return false;

	LeaseFileV1 cur{};
	if (!ReadLeaseFile(*m_file, cur)) return false;
	if (!LookValid(cur)) return false;

	cur.heartbeatUnixMs = NowUnixMs();
	return WriteLeaseFile(*m_file, cur);
}

ibFirebirdLease::State ibFirebirdLease::ReadCurrentState() {
	State s;
	// The leader reads through its own file; a follower opens it for the read alone, to peek at the leader's coordinates.
	LeaseFileV1 f{};
	if (m_file != nullptr) {
		if (!ReadLeaseFile(*m_file, f)) return s;
	}
	else {
		const ibSharedFile peek(m_leasePath);
		if (!peek.IsOpen() || !ReadLeaseFile(peek, f)) return s;
	}
	if (!LookValid(f)) return s;

	s.version         = f.version;
	s.generation      = f.generation;
	s.heartbeatUnixMs = f.heartbeatUnixMs;
	s.leaderPid       = f.leaderPid;
	s.leaderPort      = f.leaderPort;
	s.leaderHost      = ibString::FromUTF8(f.leaderHost,
		strnlen(f.leaderHost, sizeof(f.leaderHost)));
	s.valid           = true;
	return s;
}

void ibFirebirdLease::Release() {
	m_lock.Release();   // the byte before its file
	m_file.reset();
}
