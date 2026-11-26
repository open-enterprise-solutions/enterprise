/////////////////////////////////////////////////////////////////////////////
// ibLockManager implementation — the base's lock file (ibLockFile).
//
// Acquire algorithm:
//
//   1. The owner's slot — its own already, or one taken for its first row.
//   2. The regions of the batch's tables, held exclusive in the order of
//      their numbers (ibLockFile::Change), their rows read once:
//        - same owner, same key → re-entrant (upgrade or no-op)
//        - other owner, either side Exclusive, and the keys meet
//          (ibLockKey::Overlap) → a conflict
//   3. The caller's rows written, the regions let go.
//
// A conflict writes nothing (atomic batch semantics), and is refused at
// once, or — when the caller gave a timeout — asked again until it frees
// or the time is up, outside m_mtx: woken by a release in this process,
// or by the holder's slot byte when the holder is another process's.
//
// See docs/private/record-locks.md for the full design.
/////////////////////////////////////////////////////////////////////////////

#include "backend/lock/lockManager.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/lock/lockFile.h"
#include "backend/lock/lockKeyHash.h"        // two keys meeting
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"   // HolderOf - the program a holder without a user is named by
#include "backend/session/sessionSnapshot.h"
#include "backend/userInfo.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <set>

namespace {

// Another owner holds a lock the asked one meets — what a refusal names, and the slot a wait is for.
struct HeldConflict {
	ibString   object;
	ibString   user;
	ibString   session;
	ibString   computer;
	ibDateTime since;
	int        slot = -1;
};

// WHO HOLDS A LOCK, in the words a person finds them by: the user, else the program on its computer ("Application
// Workspace on OFFICE-7") - a session nobody logged into has no user, and "another session" left the reader nothing
// to look for (2026-11-19). The program is the cluster snapshot's own word for the session (GetProgram), a copy taken
// under a read lock: no base is read and no lock is asked for while this holds the file.
ibString HolderOf(const ibString& user, const ibString& session, const ibString& computer)
{
	if (!user.IsEmpty())
		return user;
	ibString program;
	if (ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry(ibApplicationInstance::Get(false))) {
		const ibSessionSnapshot snapshot = registry->GetClusterSnapshot();
		for (unsigned int idx = 0; idx < snapshot.GetSessionCount(); idx++) {
			if (snapshot.GetSession(idx).CmpNoCase(session) == 0) {
				program = snapshot.GetProgram(idx);
				break;
			}
		}
	}
	if (program.IsEmpty())
		return computer.IsEmpty() ? ibString() : ibString::Format(_("a session on %s"), computer);
	return computer.IsEmpty() ? program : ibString::Format(_("%s on %s"), program, computer);
}

} // namespace

// No Instance() — ibApplicationInstance owns the only instance. Callers
// reach it through ibApplicationInstance::GetLockManager().

ibLockManager::ibLockManager(ib::AppDataCtorToken owner) : m_applicationInstance(owner.GetApplicationInstance())
{
}

ibLockManager::~ibLockManager() = default;

ibLockFile& ibLockManager::File() const
{
	if (m_file == nullptr)
		m_file = std::make_unique<ibLockFile>(m_applicationInstance->ResolveLockFile());
	return *m_file;
}

ibLockHandle ibLockManager::Acquire(const std::vector<ibLockItem>& items,
                                     const ibLockOptions&           opts,
                                     ibLockHolder*                  customHolder)
{
	if (items.empty())
		return ibLockHandle();   // empty batch → empty handle, no-op

	// Resolve owner identity. Custom holder wins when supplied; else
	// fall back to the active session. Either is required — no
	// "anonymous" lock owners.
	ibGuid    ownerGuid;
	ibString  ownerName;
	ibString  ownerComputer;

	if (customHolder != nullptr) {
		ownerGuid     = customHolder->Identity();
		ownerName     = customHolder->DisplayName();
		ownerComputer = customHolder->Computer();
	}
	else {
		ibSession* const session = ibSession::Current();
		if (session == nullptr) {
			ibBackendCoreException::Error(
				_("ibLockManager::Acquire requires an active session or a custom holder."));
		}
		ownerGuid     = session->Identity().m_guid;
		ownerName     = session->Identity().m_userName;
		ownerComputer = session->Identity().m_computer;
	}
	const ibString owner = ownerGuid.str();

	std::vector<ibString> tables;
	for (const ibLockItem& item : items)
		tables.push_back(item.namespaceName);

	// The owner's slot while it waits, and its wait forgotten however this ends.
	int waiting = -1;
	struct ibWaitEnds {
		ibLockManager& m_manager;
		const int&     m_slot;
		~ibWaitEnds() {
			if (m_slot < 0)
				return;
			try {
				std::lock_guard<std::recursive_mutex> serialised(m_manager.m_mtx);
				m_manager.File().SetWaitsFor(m_slot, -1);
			}
			catch (...) {}
		}
	} const waitEnds{ *this, waiting };
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(opts.timeoutMs > 0 ? opts.timeoutMs : 0);
	constexpr std::chrono::milliseconds kAskAgain{ 200 };   // how late a held lock is asked for again at most

	// ⭐ A LOCK ANOTHER OWNER HOLDS IS ASKED FOR AGAIN UNTIL IT FREES, when the caller said how long (timeoutMs) — and
	// the asking holds m_mtx while the waiting does not: the release it waits for takes the same mutex.
	for (;;) {
		HeldConflict met;
		bool conflicted = false;
		bool heldHere   = false;   // the holder is an owner of this process: its release wakes m_releaseWake
		int  ownSlot    = -1;      // this owner's slot when it holds something already — what a deadlock passes through
		std::uint64_t releasesSeen = 0;
		ibLockFile* file = nullptr;
		{
			std::lock_guard<std::recursive_mutex> serialised(m_mtx);
			file = &File();

			const auto known = m_owners.find(owner);
			const bool first = known == m_owners.end();
			const int slot = first ? file->TakeSlot(ownerGuid, ownerName, ownerComputer) : known->second.first;

			std::vector<ibGuid> acquired;
			std::vector<std::pair<ibString, ibString>> added;   // (lockGuid, namespace)
			try {
				file->Change(tables, [&](ibLockFile::ibBatch& batch) {
					// EVERY KEY ASKED FIRST, the rows written after: a key another owner holds refuses the batch before it
					// changed anything, and the process keeps its copy of the regions as it was. Only the rows a key may
					// meet are read (EachOfKey), not every row of its table.
					std::vector<ibString> keyHashes, keyDatas;
					std::vector<std::uint64_t> fieldsOf;
					std::vector<std::optional<ibLockFile::ibRow>> owns(items.size());
					for (std::size_t n = 0; n < items.size(); ++n) {
						const ibLockItem& item = items[n];
						keyHashes.push_back(item.KeyHash());
						keyDatas.push_back(item.KeyData());
						fieldsOf.push_back(ibLockKey::FieldsHash(keyDatas.back()));
						const ibString& keyHash = keyHashes.back();
						const ibString& keyData = keyDatas.back();
						const std::uint64_t fields = fieldsOf.back();
						batch.EachOfKey(item.namespaceName, fields, keyData, [&](const ibLockFile::ibRow& row) {
							// The owner's own row of the SAME key is re-entry; its other keys never stand in its way.
							if (row.m_owner == ownerGuid) {
								if (!owns[n] && row.m_keyHash.Cmp(keyHash) == 0)
									owns[n] = row;
								return;
							}
							// Keys of the same fields — every write of one table — meet only as one text: two integers and
							// a compare that parts at the first value, where Overlap read both texts.
							if ((row.m_mode == ibLockMode::Exclusive || item.lockMode == ibLockMode::Exclusive)
							    && (row.m_fieldsHash == fields ? row.m_keyData.CmpNoCase(keyData) == 0
							                                   : ibLockKey::Overlap(keyData, row.m_keyData))) {
								const ibLockFile::ibOwner holder = file->ReadSlot(row.m_slot);
								throw HeldConflict{ item.objectName.IsEmpty() ? item.namespaceName : item.objectName,
								                    holder.m_user, row.m_owner.str(), holder.m_computer, row.m_since, row.m_slot };
							}
						});
					}

					const ibDateTime now = ibDateTime::Now();
					std::map<std::pair<ibString, ibString>, std::pair<ibGuid, ibLockMode>> written;   // (table, key hash) → its row
					for (std::size_t n = 0; n < items.size(); ++n) {
						const ibLockItem& item = items[n];
						const auto same = written.find({ item.namespaceName, keyHashes[n] });
						if (owns[n] || same != written.end()) {
							// The owner's own row of that key — held before, or written for this batch: the same key asked
							// twice in one batch is one row.
							std::pair<ibGuid, ibLockMode> own = owns[n] ? std::make_pair(owns[n]->m_lockGuid, owns[n]->m_mode)
							                                            : same->second;
							if (item.lockMode > own.second && opts.allowUpgrade) {
								batch.SetMode(own.first, item.lockMode);   // S → X on the row already held, its guid kept
								own.second = item.lockMode;
							}
							written[{ item.namespaceName, keyHashes[n] }] = own;
							continue;
						}
						ibLockFile::ibRow mine;
						mine.m_lockGuid  = ibNewUniqueGuid;
						mine.m_owner     = ownerGuid;
						mine.m_slot      = slot;
						mine.m_mode      = item.lockMode;
						mine.m_since     = now;
						mine.m_namespace = item.namespaceName;
						mine.m_keyHash   = keyHashes[n];
						mine.m_keyData   = keyDatas[n];
						mine.m_fieldsHash = fieldsOf[n];
						acquired.push_back(mine.m_lockGuid);
						added.emplace_back(mine.m_lockGuid.str(), item.namespaceName);
						written[{ item.namespaceName, keyHashes[n] }] = { mine.m_lockGuid, mine.m_mode };
						batch.Add(std::move(mine));
					}
				});
			}
			catch (const HeldConflict& conflict) {
				if (first)
					file->FreeSlot(slot);
				met          = conflict;
				conflicted   = true;
				heldHere     = m_owners.contains(conflict.session);
				ownSlot      = first ? -1 : slot;
				releasesSeen = m_releaseCount;
			}
			catch (...) {
				if (first)
					file->FreeSlot(slot);
				throw;
			}

			if (!conflicted) {
				for (const auto& [guid, ns] : added)
					m_rows[guid] = { owner, ns };
				if (!first || !added.empty()) {
					std::pair<int, std::size_t>& held = m_owners[owner];
					held.first   = slot;
					held.second += added.size();
				}
				else
					file->FreeSlot(slot);
				return ibLockHandle(std::move(acquired), ownerGuid);
			}
		}

		if (std::chrono::steady_clock::now() >= deadline)
			ibBackendLockException::LockConflictThrow(met.object, HolderOf(met.user, met.session, met.computer), met.since);
		if (ownSlot >= 0) {
			waiting = ownSlot;
			if (WaitsForItself(ownSlot, met.slot))
				ibBackendLockException::DeadlockThrow(met.object, HolderOf(met.user, met.session, met.computer));
		}
		const auto until = std::min(deadline, std::chrono::steady_clock::now() + kAskAgain);
		if (heldHere) {
			std::unique_lock<std::recursive_mutex> asleep(m_mtx);
			m_releaseWake.wait_until(asleep, until, [this, releasesSeen] { return m_releaseCount != releasesSeen; });
		}
		else {
			const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
			file->WaitForRelease(met.slot, static_cast<std::int32_t>(std::max<long long>(left.count(), 1)));
		}
	}
}

bool ibLockManager::WaitsForItself(int waiter, int holder)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);
	ibLockFile& file = File();
	file.SetWaitsFor(waiter, holder);
	// Down the chain from the holder: whom it waits for, and whom that one waits for — back to the waiter is a cycle.
	// A slot met twice ends it: a cycle that does not pass the waiter cannot loop here.
	std::set<int> met;
	for (int at = holder; at >= 0 && met.insert(at).second;) {
		if (at == waiter) {
			ibJournalInfo(ibT("lock"), ibT("deadlock: slot %d waits for slot %d, which waits for it - refused at once"), waiter, holder);
			return true;
		}
		const ibLockFile::ibOwner next = file.ReadSlot(at);
		if (!next.m_used)
			return false;
		at = next.m_waitsFor;
	}
	return false;
}

void ibLockManager::Forget(const std::set<ibString>& lockGuids)
{
	for (const ibString& guid : lockGuids) {
		const auto row = m_rows.find(guid);
		if (row == m_rows.end())
			continue;
		const auto held = m_owners.find(row->second.first);
		m_rows.erase(row);
		if (held != m_owners.end() && --held->second.second == 0) {
			File().FreeSlot(held->second.first);
			m_owners.erase(held);
		}
	}
}

void ibLockManager::ForgetOwner(const ibString& owner)
{
	std::erase_if(m_rows, [&owner](const auto& row) { return row.second.first == owner; });
	const auto held = m_owners.find(owner);
	if (held == m_owners.end())
		return;
	File().FreeSlot(held->second.first);
	m_owners.erase(held);
}

void ibLockManager::ReleaseRows(const std::vector<ibGuid>& lockGuids)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (lockGuids.empty())
		return;

	// Swallowing is the contract, not a shortcut: releasing a lock is best-effort — a row that survives goes with its
	// owner's end or its process's — and this runs from destructors, where an exception is std::terminate (a shutdown
	// with a document still open crashed the web server so twice on 2026-09-07).
	try {
		std::set<ibString> guids;
		std::vector<ibString> tables;
		bool unknown = false;   // a row this process does not hold: an administrator's release — every region asked
		for (const ibGuid& g : lockGuids) {
			const ibString guid = g.str();
			guids.insert(guid);
			const auto row = m_rows.find(guid);
			if (row == m_rows.end())
				unknown = true;
			else
				tables.push_back(row->second.second);
		}
		const auto drop = [&guids](std::vector<ibLockFile::ibRow>& rows) {
			return std::erase_if(rows, [&guids](const ibLockFile::ibRow& row) { return guids.contains(row.m_lockGuid.str()); }) > 0;
		};
		if (unknown)
			File().ChangeEach(drop);
		else
			File().Change(tables, [&lockGuids](ibLockFile::ibBatch& batch) {
				for (const ibGuid& guid : lockGuids)
					batch.Remove(guid);
			});
		Forget(guids);
		++m_releaseCount;
		m_releaseWake.notify_all();
	}
	catch (const ibCoreException& err) {
		ibJournalInfo(ibT("lock"), ibT("release of %d row(s) did not complete: %s"),
			static_cast<int>(lockGuids.size()), err.GetErrorDescription());
	}
	catch (...) {
		ibJournalInfo(ibT("lock"), ibT("release of %d row(s) did not complete"),
			static_cast<int>(lockGuids.size()));
	}
}

void ibLockManager::OnSessionEnd(const ibGuid& sessionGuid)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (!sessionGuid.isValid())
		return;
	try {
		File().ChangeEach([&sessionGuid](std::vector<ibLockFile::ibRow>& rows) {
			return std::erase_if(rows, [&sessionGuid](const ibLockFile::ibRow& row) { return row.m_owner == sessionGuid; }) > 0;
		});
		ForgetOwner(sessionGuid.str());
		++m_releaseCount;
		m_releaseWake.notify_all();
	}
	catch (...) { /* best-effort: the next sweep tick finds the rows again */ }
}

void ibLockManager::SweepOrphans(const std::vector<ibGuid>& liveSessionGuids)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	std::set<ibString> live;
	for (const ibGuid& g : liveSessionGuids)
		live.insert(g.str());

	std::set<ibString> orphans;
	try {
		File().ChangeEach([&live, &orphans](std::vector<ibLockFile::ibRow>& rows) {
			return std::erase_if(rows, [&live, &orphans](const ibLockFile::ibRow& row) {
				const ibString owner = row.m_owner.str();
				if (live.contains(owner))
					return false;
				orphans.insert(owner);
				return true;
			}) > 0;
		});
	}
	catch (...) {
		return;   // the file held or failing — the next sweep tick retries
	}
	for (const ibString& owner : orphans)
		ForgetOwner(owner);
	if (!orphans.empty()) {
		++m_releaseCount;
		m_releaseWake.notify_all();
	}
}

std::vector<ibLockSnapshotRow> ibLockManager::GetSnapshot() const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	std::vector<ibLockSnapshotRow> rows;
	try {
		ibLockFile& file = File();
		std::map<int, ibLockFile::ibOwner> owners;   // a slot read once
		for (const ibLockFile::ibRow& row : file.ReadAll()) {
			auto holder = owners.find(row.m_slot);
			if (holder == owners.end())
				holder = owners.emplace(row.m_slot, file.ReadSlot(row.m_slot)).first;
			ibLockSnapshotRow r;
			r.lockGuid      = row.m_lockGuid;
			r.sessionGuid   = row.m_owner;
			r.namespaceName = row.m_namespace;
			r.keyData       = row.m_keyData;
			r.lockMode      = row.m_mode;
			r.acquiredAt    = row.m_since;
			r.userName      = holder->second.m_user;
			r.computer      = holder->second.m_computer;
			rows.push_back(std::move(r));
		}
	}
	catch (...) { /* best-effort snapshot — empty/partial on failure */ }
	return rows;
}
