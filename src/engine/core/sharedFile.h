#ifndef _IB_SHARED_FILE_H_
#define _IB_SHARED_FILE_H_

#include "core/core.h"
#include "core/fstring.h"

#include <cstddef>
#include <cstdint>

// ⭐ A FILE SEVERAL PROCESSES HOLD OPEN AT ONCE — on this machine or through a share: read and written at a place, and
// a byte of it held (ibFileByteLock). What a base's lock file (backend/lock/lockFile.h) and a file base's lease
// (firebirdLease.h) stand on.
class CORE_API ibSharedFile {
public:
	// Opens the file for reading and writing, making it when there is none; IsOpen says whether that worked.
	explicit ibSharedFile(const ibString& path);
	~ibSharedFile();

	ibSharedFile(const ibSharedFile&) = delete;
	ibSharedFile& operator=(const ibSharedFile&) = delete;

	bool IsOpen() const { return m_handle != -1; }
	const ibString& GetPath() const { return m_path; }

	// `size` bytes at `offset` — false when fewer are there: a file shorter than asked is not read as zeros.
	bool ReadAt(std::uint64_t offset, void* data, std::size_t size) const;
	bool WriteAt(std::uint64_t offset, const void* data, std::size_t size);
	// What was written reaches the disk — through a share, the share's server — before this returns.
	bool Flush();
	// The file `size` bytes long: what lay past it is gone (a held byte past the end stays held).
	bool Truncate(std::uint64_t size);

private:
	friend class ibFileByteLock;

	ibString      m_path;
	std::intptr_t m_handle = -1;   // a HANDLE on Windows, a descriptor elsewhere; -1 — not open
};

// ONE BYTE OF A SHARED FILE HELD, shared or exclusive, until this goes or Release. The hold is the OS's: it goes with its
// process — through a share, when the share's server notices (~30 s, firebird-mesh-driver.md). On Windows a held byte
// refuses another handle's read of it, so what is held lies past what is read, as the lease's sentinel does. Two holds
// of one process on one file meet as two handles' do on Windows and on Linux (OFD locks); on macOS, which has no OFD
// locks, a process meets only other processes.
class CORE_API ibFileByteLock {
public:
	enum class ibMode { Shared, Exclusive };

	ibFileByteLock() = default;   // holds nothing
	// Waits up to `waitMs` for the byte, asking again as it waits (0 — asks once); HoldsByte says whether it got it.
	ibFileByteLock(ibSharedFile& file, std::uint64_t offset, ibMode mode, std::int32_t waitMs = 0);
	~ibFileByteLock();

	ibFileByteLock(ibFileByteLock&& other) noexcept;
	ibFileByteLock& operator=(ibFileByteLock&& other) noexcept;
	ibFileByteLock(const ibFileByteLock&) = delete;
	ibFileByteLock& operator=(const ibFileByteLock&) = delete;

	bool HoldsByte() const { return m_file != nullptr; }
	void Release();

private:
	ibSharedFile* m_file = nullptr;   // whose byte is held; null — none
	std::uint64_t m_offset = 0;
};

#endif // _IB_SHARED_FILE_H_
