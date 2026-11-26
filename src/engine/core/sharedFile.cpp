#include "core/sharedFile.h"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef __IBMSW__
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifdef __IBMSW__
HANDLE HandleOf(std::intptr_t handle) { return reinterpret_cast<HANDLE>(handle); }

OVERLAPPED At(std::uint64_t offset)
{
	OVERLAPPED ov{};
	ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFFu);
	ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
	return ov;
}
#else
// OFD locks where there are (Linux): a lock of the open file, as Windows' is of the handle. A plain fcntl lock is the
// process's, and a second open of the file in the process would take it again unasked (firebirdLease.cpp).
#ifdef F_OFD_SETLK
constexpr int kSetLock = F_OFD_SETLK;
#else
constexpr int kSetLock = F_SETLK;
#endif

bool SetLock(std::intptr_t handle, std::uint64_t offset, short type)
{
	struct flock fl{};
	fl.l_type   = type;
	fl.l_whence = SEEK_SET;
	fl.l_start  = static_cast<off_t>(offset);
	fl.l_len    = 1;
	return fcntl(static_cast<int>(handle), kSetLock, &fl) == 0;
}
#endif

bool TryHold(std::intptr_t handle, std::uint64_t offset, ibFileByteLock::ibMode mode)
{
#ifdef __IBMSW__
	OVERLAPPED ov = At(offset);
	const DWORD flags = LOCKFILE_FAIL_IMMEDIATELY | (mode == ibFileByteLock::ibMode::Exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0);
	return LockFileEx(HandleOf(handle), flags, 0, 1, 0, &ov) != FALSE;
#else
	return SetLock(handle, offset, mode == ibFileByteLock::ibMode::Exclusive ? F_WRLCK : F_RDLCK);
#endif
}

void LetGo(std::intptr_t handle, std::uint64_t offset)
{
#ifdef __IBMSW__
	OVERLAPPED ov = At(offset);
	UnlockFileEx(HandleOf(handle), 0, 1, 0, &ov);
#else
	SetLock(handle, offset, F_UNLCK);
#endif
}

} // namespace

ibSharedFile::ibSharedFile(const ibString& path) : m_path(path)
{
#ifdef __IBMSW__
	// Shared for reading and writing: what keeps two processes apart is a held byte, not the open (firebirdLease.cpp —
	// a share for reading alone refused the second process's open before it could ask for the byte).
	const HANDLE handle = CreateFileW(path.wc_str(), GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (handle != INVALID_HANDLE_VALUE)
		m_handle = reinterpret_cast<std::intptr_t>(handle);
#else
	const int fd = open(path.ToUtf8().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (fd >= 0)
		m_handle = fd;
#endif
}

ibSharedFile::~ibSharedFile()
{
	if (!IsOpen())
		return;
#ifdef __IBMSW__
	CloseHandle(HandleOf(m_handle));
#else
	close(static_cast<int>(m_handle));
#endif
}

bool ibSharedFile::ReadAt(std::uint64_t offset, void* data, std::size_t size) const
{
	if (!IsOpen())
		return false;
#ifdef __IBMSW__
	OVERLAPPED ov = At(offset);
	DWORD got = 0;
	return ReadFile(HandleOf(m_handle), data, static_cast<DWORD>(size), &got, &ov) && got == size;
#else
	std::size_t done = 0;
	while (done < size) {
		const ssize_t n = pread(static_cast<int>(m_handle), static_cast<char*>(data) + done, size - done,
			static_cast<off_t>(offset + done));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		done += static_cast<std::size_t>(n);
	}
	return true;
#endif
}

bool ibSharedFile::WriteAt(std::uint64_t offset, const void* data, std::size_t size)
{
	if (!IsOpen())
		return false;
#ifdef __IBMSW__
	OVERLAPPED ov = At(offset);
	DWORD wrote = 0;
	return WriteFile(HandleOf(m_handle), data, static_cast<DWORD>(size), &wrote, &ov) && wrote == size;
#else
	std::size_t done = 0;
	while (done < size) {
		const ssize_t n = pwrite(static_cast<int>(m_handle), static_cast<const char*>(data) + done, size - done,
			static_cast<off_t>(offset + done));
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		done += static_cast<std::size_t>(n);
	}
	return true;
#endif
}

bool ibSharedFile::Flush()
{
	if (!IsOpen())
		return false;
#ifdef __IBMSW__
	return FlushFileBuffers(HandleOf(m_handle)) != FALSE;
#else
	return fsync(static_cast<int>(m_handle)) == 0;
#endif
}

bool ibSharedFile::Truncate(std::uint64_t size)
{
	if (!IsOpen())
		return false;
#ifdef __IBMSW__
	FILE_END_OF_FILE_INFO end{};
	end.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
	return SetFileInformationByHandle(HandleOf(m_handle), FileEndOfFileInfo, &end, sizeof(end)) != FALSE;
#else
	return ftruncate(static_cast<int>(m_handle), static_cast<off_t>(size)) == 0;
#endif
}

ibFileByteLock::ibFileByteLock(ibSharedFile& file, std::uint64_t offset, ibMode mode, std::int32_t waitMs)
{
	if (!file.IsOpen())
		return;
	// Asked again while it waits, more seldom as it goes on: a byte is held for microseconds as a rule, and a wait the
	// OS would block in has no timeout on either side (LockFileEx on a handle opened for waiting, F_SETLKW).
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max<std::int32_t>(waitMs, 0));
	std::chrono::milliseconds pause{ 1 };
	for (;;) {
		if (TryHold(file.m_handle, offset, mode)) {
			m_file   = &file;
			m_offset = offset;
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now >= deadline)
			return;
		std::this_thread::sleep_for((std::min)(pause, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) + std::chrono::milliseconds(1)));
		pause = (std::min)(pause * 2, std::chrono::milliseconds(16));
	}
}

ibFileByteLock::~ibFileByteLock()
{
	Release();
}

ibFileByteLock::ibFileByteLock(ibFileByteLock&& other) noexcept : m_file(other.m_file), m_offset(other.m_offset)
{
	other.m_file = nullptr;
}

ibFileByteLock& ibFileByteLock::operator=(ibFileByteLock&& other) noexcept
{
	if (this != &other) {
		Release();
		m_file   = other.m_file;
		m_offset = other.m_offset;
		other.m_file = nullptr;
	}
	return *this;
}

void ibFileByteLock::Release()
{
	if (m_file == nullptr)
		return;
	if (m_file->IsOpen())
		LetGo(m_file->m_handle, m_offset);
	m_file = nullptr;
}
