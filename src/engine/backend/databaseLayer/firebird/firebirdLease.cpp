#include "firebirdLease.h"
#include "firebirdCommon.h"
#include "backend/diagnostics/journal.h"   // ibJournal — this TU does not pull in backend_core.h

#include <wx/file.h>
#include <wx/log.h>

#include <cstring>

#ifdef __WXMSW__
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <errno.h>
#endif

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

bool ReadLeaseFile(
#ifdef __WXMSW__
	HANDLE h,
#else
	int fd,
#endif
	LeaseFileV1& out)
{
#ifdef __WXMSW__
	LARGE_INTEGER zero{}; zero.QuadPart = 0;
	if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN))
		return false;
	DWORD got = 0;
	if (!ReadFile(h, &out, kLeaseFileSize, &got, nullptr))
		return false;
	return got == kLeaseFileSize;
#else
	if (lseek(fd, 0, SEEK_SET) < 0) return false;
	ssize_t got = read(fd, &out, kLeaseFileSize);
	return got == (ssize_t)kLeaseFileSize;
#endif
}

bool WriteLeaseFile(
#ifdef __WXMSW__
	HANDLE h,
#else
	int fd,
#endif
	const LeaseFileV1& in)
{
#ifdef __WXMSW__
	LARGE_INTEGER zero{}; zero.QuadPart = 0;
	if (!SetFilePointerEx(h, zero, nullptr, FILE_BEGIN))
		return false;
	DWORD wrote = 0;
	if (!WriteFile(h, &in, kLeaseFileSize, &wrote, nullptr))
		return false;
	if (!FlushFileBuffers(h)) {
		// Best-effort — slow on SMB but worth pushing through so
		// followers' next-read sees consistent state. Log to surface
		// SMB-disconnect / disk-full conditions that would otherwise
		// leave followers reading the prior generation forever.
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: FlushFileBuffers failed: %lu - ")
		             wxT("followers may see stale state until next write"),
		             (unsigned long)GetLastError());
	}
	return wrote == kLeaseFileSize;
#else
	if (lseek(fd, 0, SEEK_SET) < 0) return false;
	ssize_t wrote = write(fd, &in, kLeaseFileSize);
	if (fsync(fd) < 0) {
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: fsync failed: %d - followers ")
		             wxT("may see stale state until next write"), errno);
	}
	return wrote == (ssize_t)kLeaseFileSize;
#endif
}

bool LookValid(const LeaseFileV1& f) {
	return f.magic[0] == kMagic0 && f.magic[1] == kMagic1
	    && f.magic[2] == kMagic2 && f.magic[3] == kMagic3
	    && f.version  == kVersion;
}

} // namespace

ibFirebirdLease::ibFirebirdLease(const wxString& dbPath)
	: m_dbPath(dbPath)
	, m_leasePath(dbPath + wxT(".lease"))
{
}

ibFirebirdLease::~ibFirebirdLease() {
	Release();
}

ibFirebirdLease::AcquireResult ibFirebirdLease::TryAcquireExclusive() {
	if (m_holdsLock) return AcquireResult::AcquiredLock;

#ifdef __WXMSW__
	// CreateFileW with FILE_SHARE_READ | FILE_SHARE_WRITE. Both flags
	// matter — *exclusivity* between processes is enforced by the
	// byte-range LockFileEx call below, not by the file-handle
	// sharing mode. If the leader opens with FILE_SHARE_READ only,
	// follower processes calling TryAcquireExclusive (which also
	// requests GENERIC_WRITE) fail at CreateFile with ERROR_SHARING_
	// VIOLATION (32) before ever reaching LockFileEx — they can't
	// even probe whether someone else holds the lock. Allowing
	// shared write at the handle level lets every process open the
	// file; only one wins the byte-range lock.
	m_hFile = CreateFileW(
		m_leasePath.wc_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_ALWAYS,                        // create if missing
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (m_hFile == INVALID_HANDLE_VALUE) {
		const DWORD err = GetLastError();
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: CreateFile(%s) failed: %lu"),
		             m_leasePath, (unsigned long)err);
		return AcquireResult::IOError;
	}

	// LockFileEx EXCLUSIVE | FAIL_IMMEDIATELY on a SENTINEL byte
	// past the lease content. Locking the entire file or the content
	// range would block follower ReadFile calls with ERROR_LOCK_
	// VIOLATION (Windows treats exclusive byte-range lock as
	// preventing reads from non-locking processes on the locked
	// range — same on SMB clients). Sentinel byte outside the
	// 98-byte content lets followers read state freely while
	// exclusivity for leader-election still works (only one process
	// can lock the sentinel offset).
	OVERLAPPED ov{};
	ov.Offset     = kLockSentinelOffset;
	ov.OffsetHigh = 0;
	if (!LockFileEx(m_hFile,
	                LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
	                0,
	                1, 0,                  // length 1, offset from ov
	                &ov)) {
		const DWORD err = GetLastError();
		if (err == ERROR_LOCK_VIOLATION || err == ERROR_IO_PENDING) {
			// Standard "another holder" path. Close handle —
			// follower mode doesn't need the file open.
			CloseHandle(m_hFile);
			m_hFile = INVALID_HANDLE_VALUE;
			return AcquireResult::AnotherLeaderActive;
		}
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: LockFileEx failed: %lu"),
		             (unsigned long)err);
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
		return AcquireResult::IOError;
	}
	m_holdsLock = true;
	return AcquireResult::AcquiredLock;
#else
	// POSIX path — fcntl F_SETLK on the byte-range. On Linux this
	// honours NFS locks via lockd; samba-mounted CIFS works too.
	m_fd = open(m_leasePath.mb_str(), O_RDWR | O_CREAT, 0644);
	if (m_fd < 0) {
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: open(%s) failed: %d"),
		             m_leasePath, errno);
		return AcquireResult::IOError;
	}

	// Mirror the Windows sentinel-byte design (kLockSentinelOffset,
	// length 1) so leader-election semantics are identical cross-
	// platform. Even though POSIX fcntl is advisory by default (reads
	// aren't blocked), keeping the lock region small is sound hygiene
	// and guards against mandatory-locking filesystems (Linux ext
	// with -o mand) that would otherwise block follower reads.
	// OFD ("open file description") locks where the platform has them, plain POSIX locks
	// otherwise. This is not a detail: a classic fcntl lock belongs to the PROCESS, so a
	// second ibFirebirdLease inside the same process re-takes it happily instead of seeing
	// AnotherLeaderActive — leader election would not notice a second contender in its own
	// process, and two lease tests failed on Linux for exactly that reason. An OFD lock
	// belongs to the open file description, which is what Windows' LockFileEx does per
	// HANDLE, so the two platforms finally mean the same thing. Linux 3.15+; where the
	// constant is absent (older kernels, macOS) the old process-wide behaviour remains.
	struct flock fl{};
	fl.l_type   = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start  = kLockSentinelOffset;
	fl.l_len    = 1;
#ifdef F_OFD_SETLK
	fl.l_pid = 0;                        // required to be 0 for OFD commands
	const int lockCmd = F_OFD_SETLK;
#else
	const int lockCmd = F_SETLK;
#endif
	if (fcntl(m_fd, lockCmd, &fl) < 0) {
		if (errno == EAGAIN || errno == EACCES) {
			close(m_fd);
			m_fd = -1;
			return AcquireResult::AnotherLeaderActive;
		}
		ibJournalWarning(wxT("db.firebird"),wxT("ibFirebirdLease: fcntl F_SETLK failed: %d"), errno);
		close(m_fd);
		m_fd = -1;
		return AcquireResult::IOError;
	}
	m_holdsLock = true;
	return AcquireResult::AcquiredLock;
#endif
}

bool ibFirebirdLease::WriteSelfAsLeader(const wxString& host, uint16_t port) {
	if (!m_holdsLock) return false;

	// Read current state to preserve generation; bump on every
	// handoff (= every WriteSelfAsLeader call) so followers can
	// distinguish "same leader, refreshed heartbeat" from "new
	// leader took over".
	LeaseFileV1 cur{};
#ifdef __WXMSW__
	const bool hadOld = ReadLeaseFile(m_hFile, cur);
#else
	const bool hadOld = ReadLeaseFile(m_fd, cur);
#endif
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
	fresh.leaderPid        = (uint32_t)wxGetProcessId();
	fresh.leaderPort       = port;
	// host: zero-pad / truncate to 63 + NUL.
	const auto hostUtf8 = host.utf8_str();
	const size_t hostLen = std::min<size_t>(strlen(hostUtf8.data()), 63);
	std::memcpy(fresh.leaderHost, hostUtf8.data(), hostLen);
	// remainder already zero from value-init
	fresh.reserved = 0;

#ifdef __WXMSW__
	return WriteLeaseFile(m_hFile, fresh);
#else
	return WriteLeaseFile(m_fd, fresh);
#endif
}

bool ibFirebirdLease::WriteVacating() {
	if (!m_holdsLock) return false;

	LeaseFileV1 cur{};
#ifdef __WXMSW__
	if (!ReadLeaseFile(m_hFile, cur)) return false;
#else
	if (!ReadLeaseFile(m_fd, cur)) return false;
#endif
	if (!LookValid(cur)) return false;

	// pid = 0 sentinel — real OS pids never zero. Followers see this
	// as "leader gracefully gone" and self-promote on next tick
	// without waiting out the heartbeat staleness timeout.
	cur.leaderPid       = 0;
	cur.heartbeatUnixMs = NowUnixMs();
#ifdef __WXMSW__
	return WriteLeaseFile(m_hFile, cur);
#else
	return WriteLeaseFile(m_fd, cur);
#endif
}

bool ibFirebirdLease::RefreshHeartbeat() {
	if (!m_holdsLock) return false;

	LeaseFileV1 cur{};
#ifdef __WXMSW__
	if (!ReadLeaseFile(m_hFile, cur)) return false;
#else
	if (!ReadLeaseFile(m_fd, cur)) return false;
#endif
	if (!LookValid(cur)) return false;

	cur.heartbeatUnixMs = NowUnixMs();
#ifdef __WXMSW__
	return WriteLeaseFile(m_hFile, cur);
#else
	return WriteLeaseFile(m_fd, cur);
#endif
}

ibFirebirdLease::State ibFirebirdLease::ReadCurrentState() {
	State s;
	// Followers don't hold the lock; they open the file shared-read
	// just to peek at the current leader's coordinates.
#ifdef __WXMSW__
	HANDLE h = CreateFileW(
		m_leasePath.wc_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (h == INVALID_HANDLE_VALUE) return s;
	LeaseFileV1 f{};
	const bool ok = ReadLeaseFile(h, f);
	CloseHandle(h);
	if (!ok) return s;
#else
	int fd = open(m_leasePath.mb_str(), O_RDONLY);
	if (fd < 0) return s;
	LeaseFileV1 f{};
	const bool ok = ReadLeaseFile(fd, f);
	close(fd);
	if (!ok) return s;
#endif
	if (!LookValid(f)) return s;

	s.version         = f.version;
	s.generation      = f.generation;
	s.heartbeatUnixMs = f.heartbeatUnixMs;
	s.leaderPid       = f.leaderPid;
	s.leaderPort      = f.leaderPort;
	s.leaderHost      = wxString::FromUTF8(f.leaderHost,
		strnlen(f.leaderHost, sizeof(f.leaderHost)));
	s.valid           = true;
	return s;
}

void ibFirebirdLease::Release() {
	if (m_holdsLock) {
#ifdef __WXMSW__
		if (m_hFile != INVALID_HANDLE_VALUE) {
			// Match the sentinel-byte lock range used in
			// TryAcquireExclusive (kLockSentinelOffset, length 1).
			OVERLAPPED ov{};
			ov.Offset     = kLockSentinelOffset;
			ov.OffsetHigh = 0;
			UnlockFileEx(m_hFile, 0, 1, 0, &ov);
		}
#else
		if (m_fd >= 0) {
			// Released with the same command family it was taken with — see TryAcquireExclusive.
			struct flock fl{};
			fl.l_type   = F_UNLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start  = kLockSentinelOffset;
			fl.l_len    = 1;
#ifdef F_OFD_SETLK
			fl.l_pid = 0;
			fcntl(m_fd, F_OFD_SETLK, &fl);
#else
			fcntl(m_fd, F_SETLK, &fl);
#endif
		}
#endif
		m_holdsLock = false;
	}
#ifdef __WXMSW__
	if (m_hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}
#else
	if (m_fd >= 0) {
		close(m_fd);
		m_fd = -1;
	}
#endif
}
