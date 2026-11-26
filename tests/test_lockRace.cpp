// Two lock managers over one lock file (ibLockFile).
//
// A base's locks are held in its lock file, which every process of the base opens: two managers over one file are two
// processes as the file sees them — each its own handle, its own slots, its own bytes. What one holds the other is
// refused, or waits for; a slot whose owner went without letting go is swept by the next to read it; a wait that comes
// back to the waiter is a deadlock, refused at once.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <thread>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/lock/lockFile.h"
#include "backend/lock/lockHolder.h"
#include "backend/lock/lockKeyHash.h"
#include "backend/lock/lockManager.h"
#include "core/fileSystem.h"

namespace {

// A table of its own for every key, so no test meets another's rows.
ibLockItem Item(const ibString& key, ibLockMode mode)
{
	ibLockItem item;
	item.namespaceName = ibString(ibT("LockRace.")) + key;
	item.keyFields.push_back({ ibT("Key"), ibValue(key) });
	item.lockMode = mode;
	return item;
}

// A key of the stock table, as a LOCK writes one.
ibLockItem StockKey(std::vector<std::pair<ibString, ibValue>> fields, ibLockMode mode)
{
	ibLockItem item;
	item.namespaceName = ibT("LockRace.Stock");
	item.keyFields = std::move(fields);
	item.lockMode = mode;
	return item;
}

// The rows of one key, as the file holds them.
std::vector<ibLockSnapshotRow> RowsOf(ibLockManager* manager, const ibLockItem& item)
{
	std::vector<ibLockSnapshotRow> rows;
	for (ibLockSnapshotRow& row : manager->GetSnapshot())
		if (row.namespaceName.Cmp(item.namespaceName) == 0 && row.keyData.Cmp(item.KeyData()) == 0)
			rows.push_back(std::move(row));
	return rows;
}

enum class OutcomeKind { Granted, Conflict, Deadlock, Other };

struct Outcome {
	OutcomeKind  kind = OutcomeKind::Other;
	ibString     error;
	ibLockHandle handle;
};

// The worker has no session: the holder is given, and the bound base is what lets the handle release on this thread.
Outcome TryAcquire(ibApplicationInstance* app, ibLockManager* manager, ibLockHolder* holder,
                   const ibLockItem& item, const ibLockOptions& opts)
{
	ibApplicationInstanceScope bound(app);
	Outcome out;
	try {
		out.handle = manager->Acquire({ item }, opts, holder);
		out.kind = OutcomeKind::Granted;
	}
	catch (const ibBackendLockException& err) {
		out.kind = err.GetKind() == ibBackendLockException::Kind::Deadlock ? OutcomeKind::Deadlock : OutcomeKind::Conflict;
		out.error = err.GetErrorDescription();   // what the refusal tells the person refused
	}
	catch (const ibCoreException& err) {
		out.kind = OutcomeKind::Other;
		out.error = err.GetErrorDescription();
	}
	catch (...) {
		out.kind = OutcomeKind::Other;
		out.error = ibT("acquire failed");
	}
	return out;
}

struct PairResult {
	Outcome first;
	Outcome second;
};

// Both calls run together, each on its own manager: one manager's mutex would hide the race the file has to close.
PairResult Race(ibApplicationInstance* app,
                ibLockManager* a, ibLockHolder* holderA, const ibLockItem& itemA,
                ibLockManager* b, ibLockHolder* holderB, const ibLockItem& itemB)
{
	std::atomic<int> arrived{ 0 };
	std::atomic<bool> go{ false };
	PairResult result;

	auto run = [&](ibLockManager* manager, ibLockHolder* holder, const ibLockItem& item, Outcome* slot) {
		arrived.fetch_add(1);
		while (!go.load())
			std::this_thread::yield();
		*slot = TryAcquire(app, manager, holder, item, {});
	};

	std::thread ta(run, a, holderA, itemA, &result.first);
	std::thread tb(run, b, holderB, itemB, &result.second);
	while (arrived.load() < 2)
		std::this_thread::yield();
	go.store(true);
	ta.join();
	tb.join();
	return result;
}

// One base with no folder: its lock file is this process's own (ResolveLockFile), the second manager opens the same.
class LockRace : public ::testing::Test {
protected:
	static ibApplicationInstance* s_app;
	static std::unique_ptr<ibLockManager> s_peer;
	static ibString s_path;

	static void SetUpTestSuite()
	{
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			return;
		s_app = ibApplicationInstance::Get();
		if (s_app == nullptr)
			return;
		s_path = s_app->ResolveLockFile();
		s_peer = std::make_unique<ibLockManager>(ib::AppDataCtorToken{ s_app });
	}

	static void TearDownTestSuite()
	{
		s_peer.reset();
		if (s_app != nullptr && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		s_app = nullptr;
		std::error_code error;
		if (!s_path.IsEmpty())
			std::filesystem::remove(ibPathOf(s_path), error);
		s_path.clear();
	}

	void SetUp() override
	{
		ASSERT_TRUE(s_app != nullptr);
		ASSERT_TRUE(s_peer != nullptr);
	}

	ibApplicationInstance* App() const { return s_app; }
	ibLockManager* Mine() const { return ibApplicationInstance::GetLockManager(s_app); }
	ibLockManager* Peer() const { return s_peer.get(); }
};

ibApplicationInstance* LockRace::s_app = nullptr;
std::unique_ptr<ibLockManager> LockRace::s_peer;
ibString LockRace::s_path;

TEST_F(LockRace, Acquire_SecondShared_Granted)
{
	const ibLockItem shared = Item(ibT("shared-1"), ibLockMode::Shared);
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	const Outcome first = TryAcquire(App(), Mine(), &left, shared, {});
	const Outcome second = TryAcquire(App(), Peer(), &right, shared, {});
	ASSERT_EQ(first.kind, OutcomeKind::Granted) << first.error;
	ASSERT_EQ(second.kind, OutcomeKind::Granted) << second.error;
	EXPECT_EQ(RowsOf(Mine(), shared).size(), 2u);
}

TEST_F(LockRace, Acquire_ExclusiveWhileSharedHeld_Refused)
{
	const ibLockItem shared = Item(ibT("shared-2"), ibLockMode::Shared);
	ibLockItem exclusive = shared;
	exclusive.lockMode = ibLockMode::Exclusive;
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	const Outcome held = TryAcquire(App(), Mine(), &left, shared, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;
	const Outcome blocked = TryAcquire(App(), Peer(), &right, exclusive, {});
	EXPECT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;
	EXPECT_EQ(RowsOf(Peer(), shared).size(), 1u);
}

// ⭐ A CONFLICT NAMES WHAT IS LOCKED, WHO HOLDS IT AND SINCE WHEN (2026-11-19). It said "<namespace> is locked by
// another session": a metaobject's guid, and no holder a person could find. Read across the file: the holder is the
// other manager's, named by its slot.
TEST_F(LockRace, Acquire_Conflict_NamesTheObjectTheHolderAndSince)
{
	ibLockItem item = Item(ibT("named"), ibLockMode::Exclusive);
	item.objectName = ibT("Goods receipt");
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	const Outcome held = TryAcquire(App(), Mine(), &left, item, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;
	const Outcome blocked = TryAcquire(App(), Peer(), &right, item, {});
	ASSERT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;
	EXPECT_TRUE(blocked.error.Contains(ibT("Goods receipt is locked by left since "))) << blocked.error;
}

TEST_F(LockRace, Acquire_UpgradeOfOwnShared_KeepsGuid)
{
	const ibLockItem shared = Item(ibT("upgrade"), ibLockMode::Shared);
	ibLockItem exclusive = shared;
	exclusive.lockMode = ibLockMode::Exclusive;
	ibSingleLockHolder left(ibT("left"));
	Outcome held = TryAcquire(App(), Mine(), &left, shared, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;
	const ibGuid guid = held.handle.LockGuids().front();
	const Outcome upgrade = TryAcquire(App(), Mine(), &left, exclusive, {});
	EXPECT_EQ(upgrade.kind, OutcomeKind::Granted) << upgrade.error;
	const std::vector<ibLockSnapshotRow> rows = RowsOf(Peer(), shared);
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_TRUE(rows.front().lockGuid == guid);
	EXPECT_EQ(rows.front().lockMode, ibLockMode::Exclusive);
	{
		ibApplicationInstanceScope bound(App());
		held.handle.Release();
	}
	EXPECT_TRUE(RowsOf(Peer(), shared).empty());
}

// Two managers asking for one key at once: one gets it, the other is refused — every round.
TEST_F(LockRace, Acquire_TogetherOnTwoManagers_OneExclusive)
{
	for (int round = 0; round < 20; ++round) {
		const ibLockItem item = Item(ibString::Format(ibT("race-%d"), round), ibLockMode::Exclusive);
		ibSingleLockHolder left(ibT("left"));
		ibSingleLockHolder right(ibT("right"));
		const PairResult race = Race(App(), Mine(), &left, item, Peer(), &right, item);
		const int granted = (race.first.kind == OutcomeKind::Granted ? 1 : 0)
			+ (race.second.kind == OutcomeKind::Granted ? 1 : 0);
		EXPECT_EQ(granted, 1) << race.first.error << " / " << race.second.error;
		EXPECT_TRUE(race.first.kind == OutcomeKind::Conflict || race.second.kind == OutcomeKind::Conflict)
			<< race.first.error << " / " << race.second.error;
	}
}

TEST_F(LockRace, SweepOrphans_OwnerNotLive_DropsItsRows)
{
	const ibLockItem item = Item(ibT("orphan"), ibLockMode::Exclusive);
	ibSingleLockHolder left(ibT("left"));
	const Outcome held = TryAcquire(App(), Mine(), &left, item, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;
	Mine()->SweepOrphans({ left.Identity() });
	EXPECT_EQ(RowsOf(Mine(), item).size(), 1u);
	Mine()->SweepOrphans({});
	EXPECT_TRUE(RowsOf(Mine(), item).empty());
}

// ⭐ AN OWNER WHOSE PROCESS WENT HOLDS NOTHING: its slot's byte went with it, and the next to read its rows drops them.
// The file object gone without letting go is what a process that died leaves.
TEST_F(LockRace, Acquire_OwnerOfAGoneFile_IsSwept)
{
	const ibLockItem item = Item(ibT("gone"), ibLockMode::Exclusive);
	{
		ibLockFile gone(s_path);
		const ibGuid owner = ibNewUniqueGuid;
		const int slot = gone.TakeSlot(owner, ibT("gone"), ibT("nowhere"));
		gone.Change({ item.namespaceName }, [&](ibLockFile::ibBatch& batch) {
			ibLockFile::ibRow row;
			row.m_lockGuid  = ibNewUniqueGuid;
			row.m_owner     = owner;
			row.m_slot      = slot;
			row.m_mode      = ibLockMode::Exclusive;
			row.m_since     = ibDateTime::Now();
			row.m_namespace = item.namespaceName;
			row.m_keyHash   = item.KeyHash();
			row.m_keyData   = item.KeyData();
			batch.Add(std::move(row));
		});
		ibSingleLockHolder right(ibT("right"));
		const Outcome blocked = TryAcquire(App(), Peer(), &right, item, {});
		EXPECT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;
	}
	ibSingleLockHolder right(ibT("right"));
	const Outcome after = TryAcquire(App(), Peer(), &right, item, {});
	EXPECT_EQ(after.kind, OutcomeKind::Granted) << after.error;
	EXPECT_EQ(RowsOf(Peer(), item).size(), 1u);
}

// ⭐ TWO KEYS OF ONE TABLE MEET WHEN THEY AGREE ON EVERY FIELD BOTH NAME (2026-11-26, the query's LOCK): a field only
// one of them names is any value to the other, and no field is the whole table.
TEST(LockKey, Overlap_AgreeOnTheCommonFields_Meet)
{
	const ibString productA = ibLockKey::Canonicalize({ { ibT("Product"), ibValue(ibString(ibT("A"))) } });
	const ibString productB = ibLockKey::Canonicalize({ { ibT("Product"), ibValue(ibString(ibT("B"))) } });
	const ibString productAInW = ibLockKey::Canonicalize({ { ibT("Warehouse"), ibValue(ibString(ibT("W"))) },
	                                                       { ibT("Product"),   ibValue(ibString(ibT("A"))) } });
	const ibString inV = ibLockKey::Canonicalize({ { ibT("Warehouse"), ibValue(ibString(ibT("V"))) } });

	EXPECT_TRUE(ibLockKey::Overlap(productAInW, productA));
	EXPECT_TRUE(ibLockKey::Overlap(productA, productAInW));
	EXPECT_FALSE(ibLockKey::Overlap(productAInW, productB));
	EXPECT_FALSE(ibLockKey::Overlap(productAInW, inV));
	EXPECT_TRUE(ibLockKey::Overlap(productB, inV));          // no field in common: they may name one record
	EXPECT_TRUE(ibLockKey::Overlap(ibString(), productA));   // the whole table
}

// Keys of the same names share one signature whatever their values, the names' case aside; other names, another.
TEST(LockKey, FieldsHash_TheNamesNotTheValues)
{
	const ibString productA = ibLockKey::Canonicalize({ { ibT("Product"), ibValue(ibString(ibT("A"))) } });
	const ibString productB = ibLockKey::Canonicalize({ { ibT("Product"), ibValue(ibString(ibT("B"))) } });
	const ibString productLower = ibLockKey::Canonicalize({ { ibT("product"), ibValue(ibString(ibT("A"))) } });
	const ibString productInW = ibLockKey::Canonicalize({ { ibT("Product"),   ibValue(ibString(ibT("A"))) },
	                                                      { ibT("Warehouse"), ibValue(ibString(ibT("W"))) } });

	EXPECT_EQ(ibLockKey::FieldsHash(productA), ibLockKey::FieldsHash(productB));
	EXPECT_EQ(ibLockKey::FieldsHash(productA), ibLockKey::FieldsHash(productLower));
	EXPECT_NE(ibLockKey::FieldsHash(productA), ibLockKey::FieldsHash(productInW));
	EXPECT_NE(ibLockKey::FieldsHash(ibString()), 0u);   // 0 stays "not known"
}

// ⭐ A FILE THAT GREW IS CUT BACK ONCE NOBODY HOLDS ANYTHING (2026-11-26: 4.3 MB, nothing held): a region grown by
// doubling leaves its old rooms behind, and the last slot let go takes them back.
TEST(LockFile, FreeSlot_LastOne_CutsTheFileBack)
{
	std::error_code error;
	const ibString path = ibTextOf(std::filesystem::temp_directory_path(error)
		/ ibPathOf(ibT("oes-reclaim-") + ibNewUniqueGuid.str() + ibT(".locks")));
	{
		ibLockFile file(path);
		const std::uintmax_t empty = std::filesystem::file_size(ibPathOf(path), error);
		const ibGuid owner = ibNewUniqueGuid;
		const int slot = file.TakeSlot(owner, ibT("reclaim"), ibT("here"));
		const ibString ns = ibT("LockRace.Reclaim");
		for (int round = 0; round < 32; ++round) {   // ~1.6 MB of rows: the rooms left behind pass a megabyte
			file.Change({ ns }, [&](ibLockFile::ibBatch& batch) {
				for (int n = 0; n < 500; ++n) {
					ibLockFile::ibRow row;
					row.m_lockGuid  = ibNewUniqueGuid;
					row.m_owner     = owner;
					row.m_slot      = slot;
					row.m_since     = ibDateTime::Now();
					row.m_namespace = ns;
					row.m_keyHash   = ibString::Format(ibT("%d-%d"), round, n);
					row.m_keyData   = ibString::Format(ibT("Key=%08x"), round * 1000 + n);
					batch.Add(std::move(row));
				}
			});
		}
		EXPECT_GT(std::filesystem::file_size(ibPathOf(path), error), empty + 2 * 1024 * 1024);
		file.ChangeEach([](std::vector<ibLockFile::ibRow>& rows) { rows.clear(); return true; });
		file.FreeSlot(slot);
		EXPECT_EQ(std::filesystem::file_size(ibPathOf(path), error), empty);
		EXPECT_TRUE(file.ReadAll().empty());
	}
	std::filesystem::remove(ibPathOf(path), error);
}

// ⭐ THE OWNERS A BASE HAS AT ONCE ARE NOT A NUMBER IN THE LAYOUT (2026-11-27: 1024 slots, and the next owner refused):
// once every slot made is held, a page more is made.
TEST(LockFile, TakeSlot_PastOnePage_MakesAnother)
{
	std::error_code error;
	const ibString path = ibTextOf(std::filesystem::temp_directory_path(error)
		/ ibPathOf(ibT("oes-slots-") + ibNewUniqueGuid.str() + ibT(".locks")));
	{
		ibLockFile file(path);
		std::set<int> taken;
		std::map<int, ibGuid> owners;
		for (int n = 0; n < 1500; ++n) {
			const ibGuid owner = ibNewUniqueGuid;
			const int slot = file.TakeSlot(owner, ibT("slots"), ibT("here"));
			EXPECT_TRUE(taken.insert(slot).second) << slot;
			owners[slot] = owner;
		}
		ASSERT_GE(*taken.rbegin(), 1024);
		const int far = *taken.rbegin();
		const ibLockFile::ibOwner read = file.ReadSlot(far);
		EXPECT_TRUE(read.m_used);
		EXPECT_TRUE(read.m_owner == owners[far]);
		for (const int slot : taken)
			file.FreeSlot(slot);
		EXPECT_FALSE(file.ReadSlot(far).m_used);
	}
	std::filesystem::remove(ibPathOf(path), error);
}

// ⭐ A REGION KEPT BY THE PROCESS IS THE FILE'S: rows added, raised and let go across two managers read back as the
// file holds them — the second manager's view is the share's copy, the first's writes move its generation.
TEST_F(LockRace, Acquire_ManyHeldRows_ReadBackAsWritten)
{
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	std::vector<Outcome> held;
	for (int n = 0; n < 50; ++n) {
		held.push_back(TryAcquire(App(), n % 2 == 0 ? Mine() : Peer(), n % 2 == 0 ? &left : &right,
			StockKey({ { ibT("Product"), ibValue(ibString::Format(ibT("many-%d"), n)) } }, ibLockMode::Shared), {}));
		ASSERT_EQ(held.back().kind, OutcomeKind::Granted) << held.back().error;
	}
	const ibLockItem raised = StockKey({ { ibT("Product"), ibValue(ibString(ibT("many-10"))) } }, ibLockMode::Exclusive);
	const Outcome upgrade = TryAcquire(App(), Mine(), &left, raised, {});
	ASSERT_EQ(upgrade.kind, OutcomeKind::Granted) << upgrade.error;
	{
		ibApplicationInstanceScope bound(App());
		for (int n = 0; n < 50; n += 3)
			held[n].handle.Release();
	}
	int kept = 0;
	for (const ibLockSnapshotRow& row : Peer()->GetSnapshot())
		if (row.namespaceName.Cmp(ibT("LockRace.Stock")) == 0 && row.keyData.Contains(ibT("Product=")))
			++kept;
	EXPECT_EQ(kept, 50 - 17);
	const std::vector<ibLockSnapshotRow> rows = RowsOf(Peer(), raised);
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_EQ(rows.front().lockMode, ibLockMode::Exclusive);
	const Outcome blocked = TryAcquire(App(), Peer(), &right, raised, {});
	EXPECT_EQ(blocked.kind, OutcomeKind::Conflict) << blocked.error;
}

// ⭐ A BATCH REFUSED BY ONE KEY HOLDS NONE OF THEM (2026-11-27, the batch's keys asked before any row is written): the
// process's copy of the region is the file's after it, and the first key is free for the other owner.
TEST_F(LockRace, Acquire_BatchRefusedByItsSecondKey_HoldsNoneOfIt)
{
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	const ibLockItem first  = StockKey({ { ibT("Product"), ibValue(ibString(ibT("batch-1"))) } }, ibLockMode::Exclusive);
	const ibLockItem second = StockKey({ { ibT("Product"), ibValue(ibString(ibT("batch-2"))) } }, ibLockMode::Exclusive);
	const Outcome held = TryAcquire(App(), Peer(), &right, second, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;
	{
		ibApplicationInstanceScope bound(App());
		EXPECT_THROW(Mine()->Acquire({ first, second }, {}, &left), ibBackendLockException);
	}
	EXPECT_TRUE(RowsOf(Mine(), first).empty());
	EXPECT_TRUE(RowsOf(Peer(), first).empty());
	const Outcome after = TryAcquire(App(), Peer(), &right, first, {});
	EXPECT_EQ(after.kind, OutcomeKind::Granted) << after.error;
}

// ⭐ A REGION MOSTLY LET GO IS WRITTEN AGAIN WITHOUT WHAT WAS (2026-11-27, a row let go stays in place until then): the
// rows left read back as held, and still refuse.
TEST_F(LockRace, Release_MostOfARegion_TheRestStillHeld)
{
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	std::vector<Outcome> held;
	for (int n = 0; n < 40; ++n) {
		held.push_back(TryAcquire(App(), Mine(), &left,
			StockKey({ { ibT("Product"), ibValue(ibString::Format(ibT("compact-%d"), n)) } }, ibLockMode::Exclusive), {}));
		ASSERT_EQ(held.back().kind, OutcomeKind::Granted) << held.back().error;
	}
	{
		ibApplicationInstanceScope bound(App());
		for (int n = 0; n < 40; ++n)
			if (n % 10 != 0)
				held[n].handle.Release();
	}
	for (int n = 0; n < 40; ++n) {
		const Outcome other = TryAcquire(App(), Peer(), &right,
			StockKey({ { ibT("Product"), ibValue(ibString::Format(ibT("compact-%d"), n)) } }, ibLockMode::Exclusive), {});
		EXPECT_EQ(other.kind, n % 10 == 0 ? OutcomeKind::Conflict : OutcomeKind::Granted) << n << ": " << other.error;
	}
}

TEST_F(LockRace, Acquire_KeysOfOneTable_ConflictWhenTheyMeet)
{
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	const Outcome held = TryAcquire(App(), Mine(), &left,
		StockKey({ { ibT("Product"),   ibValue(ibString(ibT("A"))) },
		           { ibT("Warehouse"), ibValue(ibString(ibT("W"))) } }, ibLockMode::Exclusive), {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;

	const Outcome sameProduct = TryAcquire(App(), Peer(), &right, StockKey({ { ibT("Product"), ibValue(ibString(ibT("A"))) } }, ibLockMode::Shared), {});
	EXPECT_EQ(sameProduct.kind, OutcomeKind::Conflict) << sameProduct.error;
	const Outcome wholeTable = TryAcquire(App(), Peer(), &right, StockKey({}, ibLockMode::Shared), {});
	EXPECT_EQ(wholeTable.kind, OutcomeKind::Conflict) << wholeTable.error;
	const Outcome otherProduct = TryAcquire(App(), Peer(), &right, StockKey({ { ibT("Product"), ibValue(ibString(ibT("B"))) } }, ibLockMode::Exclusive), {});
	EXPECT_EQ(otherProduct.kind, OutcomeKind::Granted) << otherProduct.error;
}

// ⭐ A TIMEOUT WAITS A HELD LOCK OUT (2026-11-26): asked again until the holder lets go — here an owner of the other
// manager, woken by its slot's byte — and refused once the time is up.
TEST_F(LockRace, Acquire_WithTimeout_WaitsForTheHolder)
{
	const ibLockItem item = StockKey({ { ibT("Product"), ibValue(ibString(ibT("C"))) } }, ibLockMode::Exclusive);
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	Outcome held = TryAcquire(App(), Mine(), &left, item, {});
	ASSERT_EQ(held.kind, OutcomeKind::Granted) << held.error;

	ibLockOptions patient;
	patient.timeoutMs = 5000;
	std::thread releaser([&] {
		std::this_thread::sleep_for(std::chrono::milliseconds(400));
		ibApplicationInstanceScope bound(App());
		held.handle.Release();
	});
	const Outcome waited = TryAcquire(App(), Peer(), &right, item, patient);
	releaser.join();
	EXPECT_EQ(waited.kind, OutcomeKind::Granted) << waited.error;

	ibLockOptions brief;
	brief.timeoutMs = 300;
	const auto began = std::chrono::steady_clock::now();
	const Outcome refused = TryAcquire(App(), Mine(), &left, item, brief);
	EXPECT_EQ(refused.kind, OutcomeKind::Conflict) << refused.error;
	EXPECT_GE(std::chrono::steady_clock::now() - began, std::chrono::milliseconds(300));
}

// ⭐ A WAIT THAT COMES BACK TO THE WAITER IS REFUSED AT ONCE, across managers: each writes whom it waits for in its slot,
// and the second to wait reads the cycle there.
TEST_F(LockRace, Acquire_WaitingForEachOther_IsADeadlock)
{
	const ibLockItem first = Item(ibT("deadlock-a"), ibLockMode::Exclusive);
	const ibLockItem second = Item(ibT("deadlock-b"), ibLockMode::Exclusive);
	ibSingleLockHolder left(ibT("left"));
	ibSingleLockHolder right(ibT("right"));
	Outcome leftHolds = TryAcquire(App(), Mine(), &left, first, {});
	const Outcome rightHolds = TryAcquire(App(), Peer(), &right, second, {});
	ASSERT_EQ(leftHolds.kind, OutcomeKind::Granted) << leftHolds.error;
	ASSERT_EQ(rightHolds.kind, OutcomeKind::Granted) << rightHolds.error;

	ibLockOptions patient;
	patient.timeoutMs = 5000;
	Outcome rightWaited;
	std::thread waiter([&] { rightWaited = TryAcquire(App(), Peer(), &right, first, patient); });
	std::this_thread::sleep_for(std::chrono::milliseconds(300));   // right waits for left by now

	const auto began = std::chrono::steady_clock::now();
	const Outcome leftWaited = TryAcquire(App(), Mine(), &left, second, patient);
	EXPECT_EQ(leftWaited.kind, OutcomeKind::Deadlock) << leftWaited.error;
	EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::milliseconds(2000));
	{
		ibApplicationInstanceScope bound(App());
		leftHolds.handle.Release();
	}
	waiter.join();
	EXPECT_EQ(rightWaited.kind, OutcomeKind::Granted) << rightWaited.error;
}

} // namespace
