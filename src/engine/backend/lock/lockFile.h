/////////////////////////////////////////////////////////////////////////////
// ibLockFile — the locks of a base, in a file every process of the base
// holds open: `sys.locks` in the base's folder (ResolveLockFile). What the
// sys_lock and sys_lock_key tables were, with no transaction and no commit.
//
//   - An OWNER (a session, a job, a compile) takes a SLOT while it holds a
//     row: who it is, its process, whom it waits for. Its process holds the
//     slot's byte for as long — a process that dies lets it go, so its rows
//     are known to be nobody's, and a waiter of another process wakes on it.
//     The slots are made a page at a time, as the owners need them.
//   - A TABLE's held keys are ROWS in a REGION of its own (the directory
//     gives each namespace its entry): read, checked and rewritten under the
//     region's byte, so the tables do not wait for one another.
//
// The layout, little-endian: the header and the regions' directory, the
// first page of slots, then the regions and the further pages as they are
// made, at the end of the file. The bytes held lie past all of it
// (kLockArea, lockFile.cpp), where nothing is read. One per base per
// process; every call is serialised in the process, and a region nobody
// else wrote since is not read again: its generation, in the directory,
// says so, and the process keeps its rows.
//
// See docs/private/record-locks.md (stage 19).
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_LOCK_FILE_H_
#define _IB_LOCK_FILE_H_

#include "backend/backend.h"
#include "backend/lock/lockTypes.h"
#include "core/guid.h"
#include "core/sharedFile.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

class BACKEND_API ibLockFile {
public:
	// One held key, as a region keeps it.
	struct ibRow {
		ibGuid     m_lockGuid;
		ibGuid     m_owner;
		int        m_slot = -1;
		ibLockMode m_mode = ibLockMode::Exclusive;
		ibDateTime m_since;
		ibString   m_namespace;
		ibString   m_keyHash;
		ibString   m_keyData;
		std::uint64_t m_fieldsHash = 0;   // the names m_keyData keys by (ibLockKey::FieldsHash); 0 — not known
	};

	// An owner as its slot says it.
	struct ibOwner {
		bool     m_used = false;
		ibGuid   m_owner;
		ibString m_user;
		ibString m_computer;
		int      m_waitsFor = -1;   // a slot; -1 — nobody
	};

	// A batch: the rows of the regions it holds, found by their key (below).
	class ibBatch;

	explicit ibLockFile(const ibString& path);
	~ibLockFile();

	ibLockFile(const ibLockFile&) = delete;
	ibLockFile& operator=(const ibLockFile&) = delete;

	// The rows of the regions the namespaces fall in, those regions held exclusive while `change` runs — a batch's,
	// so its keys are checked and written as one — and what it changed written when it returns. An owner whose process
	// is gone is found on the way: its rows are dropped here, and from every other region once these are let go.
	// What `change` throws comes out, nothing written.
	void Change(const std::vector<ibString>& namespaces, const std::function<void(ibBatch&)>& change);
	// The same over every region made, one at a time: a release that does not know its rows' tables, a session's end.
	void ChangeEach(const std::function<bool(std::vector<ibRow>&)>& change);
	// Every row, each region read under its byte held shared.
	std::vector<ibRow> ReadAll();

	// An owner's slot, its byte held by this process until FreeSlot — taken before its first row is written, let go
	// after its last is gone. A slot a dead process left is cleared of its rows first.
	int TakeSlot(const ibGuid& owner, const ibString& user, const ibString& computer);
	void FreeSlot(int slot);
	ibOwner ReadSlot(int slot);
	// Whom the owner of `slot` waits for (-1 — nobody): what another process reads a deadlock from.
	void SetWaitsFor(int slot, int holder);
	// Waits up to `waitMs` for the owner of another process's `slot` to let go of all it holds.
	void WaitForRelease(int slot, std::int32_t waitMs);

private:
	struct ibShare;
	struct ibCachedRegion;

	void Open();
	void EachRegion(const std::function<bool(std::vector<ibRow>&)>& change);
	bool IsAlive(int slot);
	void SweepSlot(int slot);
	void SweepDead();
	std::uint64_t SlotAt(int slot);
	int SlotsMade();
	void MakeSlotPage(int page);
	void Reclaim();
	int RegionOf(const ibString& ns);
	ibCachedRegion& ReadRegion(int region);
	void WriteRegion(int region, const std::vector<ibRow>& rows);
	void WriteRegion(int region, ibCachedRegion& cached);

	ibSharedFile                   m_file;
	std::shared_ptr<ibShare>       m_share;     // this file in this process: its mutex and the slots its process holds
	std::map<int, ibFileByteLock>  m_slots;     // the slots taken here, each its byte
	std::map<int, ibFileByteLock>  m_dead;      // slots found dead, their bytes held until their rows are swept
};

// ⭐ THE ROWS OF THE REGIONS A BATCH HOLDS, found by their key — what Change hands its callback instead of a copy of them
// all: a check reads the rows its key may meet, not every row of the table (2026-11-27). What it is told is the
// process's copy until the callback returns, and written then; what the callback throws leaves the file as it was.
class BACKEND_API ibLockFile::ibBatch {
public:
	// The rows `keyData` of `ns` may meet: those of the same fields with the same text (the owner's own among them),
	// and every row of other fields. `each` reads them; it changes nothing while it does.
	void EachOfKey(const ibString& ns, std::uint64_t fieldsHash, const ibString& keyData,
	               const std::function<void(const ibRow&)>& each) const;
	void Add(ibRow row);
	void Remove(const ibGuid& lockGuid);
	void SetMode(const ibGuid& lockGuid, ibLockMode mode);   // S → X on a row held

private:
	friend class ibLockFile;
	ibBatch() = default;

	std::vector<ibCachedRegion*>        m_held;       // the regions the batch holds, as the process keeps them
	std::map<ibString, ibCachedRegion*> m_regionOf;   // the batch's tables, each its region
};

#endif // _IB_LOCK_FILE_H_
