#include "backend/lock/lockFile.h"

#include "backend/backend_exception.h"
#include "core/diagnostics/crashGuard.h"   // CurrentPidPortable — whose slot it is
#include "core/diagnostics/journal.h"
#include "core/fileSystem.h"               // ibPathOf / ibTextOf — the file named once in the process
#include "core/hash.h"                     // KeyOf — the mixer a key's place in the index folds through

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

namespace {

#pragma pack(push, 1)
struct ibHeader {
	char          magic[4];
	std::uint32_t version;
	std::uint32_t regions;
	std::uint32_t slots;          // a page's
	std::uint64_t end;            // where the next room is made — a region's, a page of slots
	std::uint64_t left;           // the rooms the regions left behind as they grew (Reclaim)
	unsigned char reserved[32];
};
struct ibRegionEntry {
	std::uint64_t offset;         // 0 — not made yet
	std::uint32_t capacity;
	std::uint32_t used;           // the bytes its rows take
	std::uint64_t generation;     // moves with every write of the region — a process's copy of its rows is good while equal
	std::uint64_t name;           // the hash of the namespace the entry is its table's by (RegionOf); 0 — nobody's
};
struct ibSlot {
	std::uint8_t  used;
	std::uint8_t  reserved0[3];
	std::uint32_t pid;
	unsigned char owner[16];
	unsigned char process[16];
	std::int32_t  waitsFor;       // a slot; -1 — nobody
	char          user[80];       // UTF-8, zero-padded
	char          computer[64];
	unsigned char reserved1[4];
};
struct ibRowHead {
	std::uint16_t bytes;          // the row whole, this head and its three texts
	unsigned char lockGuid[16];
	unsigned char owner[16];
	std::int32_t  slot;
	std::uint8_t  mode;
	std::uint8_t  gone;           // 1 — let go: its bytes stay until the region is written again without it
	std::uint8_t  reserved0[2];
	std::int64_t  since;
	std::uint64_t fieldsHash;     // ibLockKey::FieldsHash of the key; 0 — not known
	std::uint16_t namespaceBytes;
	std::uint16_t keyHashBytes;
	std::uint16_t keyDataBytes;
	std::uint16_t reserved1;
};
#pragma pack(pop)

// ⭐ THE LAYOUT. Little-endian, as every platform the engine builds for is (x86, x64, arm64); the format is not frozen,
// so a file laid out otherwise is made anew rather than read. The header; the directory — a region a table, found by its
// name (RegionOf); where the pages of slots after the first were made; the first page of slots; then the rooms made as
// they are needed — the regions', the further pages'.
constexpr char          kMagic[4]         = { 'O', 'E', 'S', 'K' };
constexpr std::uint32_t kVersion          = 1;
constexpr int           kRegions          = 1024;
constexpr int           kSlotsPerPage     = 1024;
constexpr int           kSlotPages        = 256;    // the owners a base may have at once: 262 144
constexpr int           kMaxSlots         = kSlotsPerPage * kSlotPages;
constexpr std::uint64_t kDirectoryAt      = 64;
constexpr std::uint64_t kSlotPagesAt      = kDirectoryAt + kRegions * sizeof(ibRegionEntry);
constexpr std::uint64_t kSlotsAt          = (kSlotPagesAt + kSlotPages * sizeof(std::uint64_t) + 4095) / 4096 * 4096;
constexpr std::uint64_t kSlotBytes        = sizeof(ibSlot);
constexpr std::uint64_t kSlotPageBytes    = kSlotsPerPage * kSlotBytes;
constexpr std::uint64_t kRegionsAt        = kSlotsAt + kSlotPageBytes;
constexpr std::uint32_t kRegionFirstBytes = 16384;

// THE BYTES HELD — past anything read (on Windows a held byte refuses another handle's read of it), below 2 GB so a
// 32-bit off_t reaches them: the file's own (its header, a room made, an entry given a table), each region's, each slot's.
constexpr std::uint64_t kLockArea = 0x7F000000;
constexpr std::uint64_t kFileByte = kLockArea;
constexpr std::uint64_t RegionByte(int region) { return kLockArea + 1 + static_cast<std::uint64_t>(region); }
constexpr std::uint64_t SlotByte(int slot) { return kLockArea + 1 + kRegions + static_cast<std::uint64_t>(slot); }

// How long the file or a region is waited for: held for microseconds on this machine, a few exchanges through a share.
constexpr std::int32_t kHoldWaitMs = 30000;

// THE ROOM LEFT BEHIND worth taking back (Reclaim): a region that doubled left its old room, and past this much of it
// the file is cut back once nobody holds anything — asked at most once in kReclaimEvery by a process.
constexpr std::uint64_t              kReclaimPast  = 1024 * 1024;
constexpr std::chrono::seconds       kReclaimEvery{ 10 };

static_assert(sizeof(ibHeader) == 64);
static_assert(sizeof(ibRegionEntry) == 32);
static_assert(sizeof(ibSlot) == 192);

// One process, one id: a pid is used again by the next process, this never.
const ibGuid& ThisProcess()
{
	static const ibGuid s_process = ibNewUniqueGuid;
	return s_process;
}

bool IsValid(const ibHeader& header)
{
	return std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 && header.version == kVersion
		&& header.regions == kRegions && header.slots == kSlotsPerPage;
}

void PutText(char* field, std::size_t size, const ibString& text)
{
	std::memset(field, 0, size);
	const std::string utf8 = text.ToUtf8();
	std::size_t n = std::min(utf8.size(), size - 1);
	while (n > 0 && n < utf8.size() && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80)
		--n;   // never cut a character in two
	std::memcpy(field, utf8.data(), n);
}

ibString GetText(const char* field, std::size_t size)
{
	std::size_t n = 0;
	while (n < size && field[n] != '\0')
		++n;
	return ibString::FromUTF8(field, n);
}

ibGuid GuidOf(const unsigned char (&bytes)[16])
{
	std::array<unsigned char, 16> raw{};
	std::memcpy(raw.data(), bytes, raw.size());
	return ibGuid(raw);
}

// One row's bytes, put after `out`'s.
void Append(std::vector<unsigned char>& out, const ibLockFile::ibRow& row)
{
	const std::string ns = row.m_namespace.ToUtf8(), hash = row.m_keyHash.ToUtf8(), key = row.m_keyData.ToUtf8();
	const std::size_t bytes = sizeof(ibRowHead) + ns.size() + hash.size() + key.size();
	if (bytes > 0xFFFF)
		ibBackendCoreException::Error(_("A key of %s is too long to be held"), row.m_namespace);
	ibRowHead head{};
	head.bytes = static_cast<std::uint16_t>(bytes);
	std::memcpy(head.lockGuid, row.m_lockGuid.bytes().data(), 16);
	std::memcpy(head.owner, row.m_owner.bytes().data(), 16);
	head.slot           = row.m_slot;
	head.mode           = static_cast<std::uint8_t>(row.m_mode);
	head.since          = row.m_since.GetValue();
	head.fieldsHash     = row.m_fieldsHash;
	head.namespaceBytes = static_cast<std::uint16_t>(ns.size());
	head.keyHashBytes   = static_cast<std::uint16_t>(hash.size());
	head.keyDataBytes   = static_cast<std::uint16_t>(key.size());
	const std::size_t at = out.size();
	out.resize(at + bytes);
	std::memcpy(out.data() + at, &head, sizeof(head));
	std::memcpy(out.data() + at + sizeof(head), ns.data(), ns.size());
	std::memcpy(out.data() + at + sizeof(head) + ns.size(), hash.data(), hash.size());
	std::memcpy(out.data() + at + sizeof(head) + ns.size() + hash.size(), key.data(), key.size());
}

// A fresh region's first generation: no number a process may still hold for the region of a file made before this one.
std::uint64_t FirstGeneration()
{
	std::uint64_t generation = 0;
	std::memcpy(&generation, ibGuid(ibNewUniqueGuid).bytes().data(), sizeof(generation));
	return generation != 0 ? generation : 1;
}

// The rows as written, those let go too, where each one's bytes begin (one more: where the last ends) and which were
// let go; a row that does not add up ends the reading — what follows it cannot be told apart.
std::vector<ibLockFile::ibRow> Decode(const std::vector<unsigned char>& bytes, std::vector<std::size_t>& starts,
                                      std::vector<std::uint8_t>& gone)
{
	std::vector<ibLockFile::ibRow> rows;
	starts.clear();
	gone.clear();
	std::size_t at = 0;
	while (at + sizeof(ibRowHead) <= bytes.size()) {
		ibRowHead head{};
		std::memcpy(&head, bytes.data() + at, sizeof(head));
		const std::size_t texts = std::size_t(head.namespaceBytes) + head.keyHashBytes + head.keyDataBytes;
		if (head.bytes != sizeof(head) + texts || at + head.bytes > bytes.size())
			break;
		const char* text = reinterpret_cast<const char*>(bytes.data() + at + sizeof(head));
		ibLockFile::ibRow row;
		row.m_lockGuid  = GuidOf(head.lockGuid);
		row.m_owner     = GuidOf(head.owner);
		row.m_slot      = head.slot;
		row.m_mode      = static_cast<ibLockMode>(head.mode);
		row.m_since     = ibDateTime(static_cast<long long>(head.since));
		row.m_namespace = ibString::FromUTF8(text, head.namespaceBytes);
		row.m_keyHash   = ibString::FromUTF8(text + head.namespaceBytes, head.keyHashBytes);
		row.m_keyData   = ibString::FromUTF8(text + head.namespaceBytes + head.keyHashBytes, head.keyDataBytes);
		row.m_fieldsHash = head.fieldsHash;
		rows.push_back(std::move(row));
		starts.push_back(at);
		gone.push_back(head.gone != 0 ? 1 : 0);
		at += head.bytes;
	}
	starts.push_back(at);
	return rows;
}

// A key as the index knows it: its table and its text, folded as CmpNoCase compares (ibToLower, as Lower does).
std::uint64_t KeyOf(const ibString& ns, const ibString& keyData)
{
	return ibHashCombine(ns.GetHash(), keyData.Lower().GetHash());
}

// A guid's bytes are random already: its first eight are its hash.
struct ibGuidHash {
	std::size_t operator()(const ibGuid& guid) const noexcept
	{
		std::uint64_t hash = 0;
		std::memcpy(&hash, guid.bytes().data(), sizeof(hash));
		return static_cast<std::size_t>(hash ^ (hash >> 32));
	}
};

} // namespace

// ⭐ A REGION AS THIS PROCESS LAST READ OR WROTE IT — its rows and their bytes (row i's from m_starts[i] to m_starts[i+1]),
// good while the directory says its generation: nobody wrote it since, so it is neither read nor decoded again.
// (Measured 2026-11-26: every Acquire read, decoded and rewrote its table's region whole — at K=8 with the rows of
// twenty documents a session held, a second a document.) A row let go stays among them, its head saying so, until more
// than half the bytes are such and the region is written again without them (Compact).
//
// ⭐ AND ITS INDEX, over the rows held: a key's text (KeyOf), a row's guid, a table's rows by the fields they key by, the
// rows each slot holds here — a batch reads the rows its key may meet (ibBatch::EachOfKey), not every row of the table
// (2026-11-27). The key's and the fields' lists keep a row let go, and are read past it.
struct ibLockFile::ibCachedRegion {
	std::uint64_t              m_generation = 0;
	std::vector<ibRow>         m_rows;
	std::vector<std::uint8_t>  m_gone;
	std::vector<unsigned char> m_bytes;
	std::vector<std::size_t>   m_starts{ 0 };
	std::size_t                m_goneBytes = 0;

	std::unordered_multimap<std::uint64_t, std::size_t>                   m_byKey;
	std::unordered_map<ibGuid, std::size_t, ibGuidHash>                   m_byGuid;
	std::map<ibString, std::map<std::uint64_t, std::vector<std::size_t>>> m_byFields;
	std::map<int, std::size_t>                                            m_bySlot;

	// THE BATCH that holds it now (Change): where its bytes ended as it began — what follows is written whole — the bytes
	// changed in place before that, and whether anything changed.
	std::size_t              m_bytesAt = 0;
	std::vector<std::size_t> m_marks;
	bool                     m_changed = false;

	void Clear()
	{
		m_rows.clear();
		m_gone.clear();
		m_bytes.clear();
		m_starts.assign(1, 0);
		m_goneBytes = 0;
		m_byKey.clear();
		m_byGuid.clear();
		m_byFields.clear();
		m_bySlot.clear();
	}

	void Index(std::size_t i)
	{
		const ibRow& row = m_rows[i];
		m_byKey.emplace(KeyOf(row.m_namespace, row.m_keyData), i);
		m_byGuid[row.m_lockGuid] = i;
		m_byFields[row.m_namespace][row.m_fieldsHash].push_back(i);
		++m_bySlot[row.m_slot];
	}

	void Load(std::vector<unsigned char> bytes)
	{
		Clear();
		m_rows = Decode(bytes, m_starts, m_gone);
		bytes.resize(m_starts.back());   // what does not add up is not kept
		m_bytes = std::move(bytes);
		for (std::size_t i = 0; i < m_rows.size(); ++i) {
			if (m_gone[i])
				m_goneBytes += m_starts[i + 1] - m_starts[i];
			else
				Index(i);
		}
	}

	void Begin()
	{
		m_bytesAt = m_bytes.size();
		m_marks.clear();
		m_changed = false;
	}

	void Mark(std::size_t at)
	{
		if (at < m_bytesAt)
			m_marks.push_back(at);
		m_changed = true;
	}

	void Add(ibRow row)
	{
		Append(m_bytes, row);
		m_rows.push_back(std::move(row));
		m_gone.push_back(0);
		m_starts.push_back(m_bytes.size());
		Index(m_rows.size() - 1);
		m_changed = true;
	}

	// Row `i` let go: its head says so, one byte written in place — the bytes after it stay where they are.
	void Bury(std::size_t i)
	{
		if (m_gone[i])
			return;
		m_gone[i] = 1;
		const std::size_t at = m_starts[i] + offsetof(ibRowHead, gone);
		m_bytes[at] = 1;
		Mark(at);
		m_goneBytes += m_starts[i + 1] - m_starts[i];
		m_byGuid.erase(m_rows[i].m_lockGuid);
		if (const auto held = m_bySlot.find(m_rows[i].m_slot); held != m_bySlot.end() && --held->second == 0)
			m_bySlot.erase(held);
	}

	// Every row of `slot` let go — its process is gone.
	void BurySlot(int slot)
	{
		for (std::size_t i = 0; i < m_rows.size(); ++i)
			if (!m_gone[i] && m_rows[i].m_slot == slot)
				Bury(i);
	}

	void SetMode(std::size_t i, ibLockMode mode)
	{
		m_rows[i].m_mode = mode;
		const std::size_t at = m_starts[i] + offsetof(ibRowHead, mode);
		m_bytes[at] = static_cast<std::uint8_t>(mode);
		Mark(at);
	}

	// The rows held again, without those let go: their bytes as they were, the whole region to be written.
	void Compact()
	{
		std::vector<ibRow> rows;
		std::vector<unsigned char> bytes;
		std::vector<std::size_t> starts{ 0 };
		for (std::size_t i = 0; i < m_rows.size(); ++i) {
			if (m_gone[i])
				continue;
			bytes.insert(bytes.end(), m_bytes.begin() + static_cast<std::ptrdiff_t>(m_starts[i]),
				m_bytes.begin() + static_cast<std::ptrdiff_t>(m_starts[i + 1]));
			starts.push_back(bytes.size());
			rows.push_back(std::move(m_rows[i]));
		}
		Clear();
		m_rows   = std::move(rows);
		m_bytes  = std::move(bytes);
		m_starts = std::move(starts);
		m_gone.assign(m_rows.size(), 0);
		for (std::size_t i = 0; i < m_rows.size(); ++i)
			Index(i);
		m_bytesAt = 0;
		m_marks.clear();
		m_changed = true;
	}

	std::vector<ibRow> LiveRows() const
	{
		std::vector<ibRow> rows;
		rows.reserve(m_rows.size());
		for (std::size_t i = 0; i < m_rows.size(); ++i)
			if (!m_gone[i])
				rows.push_back(m_rows[i]);
		return rows;
	}
};

// THIS FILE IN THIS PROCESS — one mutex for every call on it, whichever lock file object asks (a held byte keeps
// processes apart, and on macOS it does not keep two objects of one process apart), the slots the process holds:
// alive without asking a byte, which on macOS the process would be handed again — and the regions as it last saw them.
struct ibLockFile::ibShare {
	std::recursive_mutex                  m_mtx;
	std::set<int>                         m_slots;
	std::map<int, ibCachedRegion>         m_regions;
	std::map<ibString, int>               m_regionOf;      // a table's region as RegionOf found it: an entry keeps its table
	std::chrono::steady_clock::time_point m_reclaimAt{};   // not asked again before (Reclaim)
	int                                   m_nextSlot = -1;  // where TakeSlot looks first; -1 — by the pid
};

ibLockFile::ibLockFile(const ibString& path) : m_file(path)
{
	static std::mutex s_mtx;
	static std::map<ibString, std::weak_ptr<ibShare>> s_shares;
	const ibString key = ibTextOf(ibPathOf(path).lexically_normal()).Lower();
	{
		std::lock_guard<std::mutex> lock(s_mtx);
		std::weak_ptr<ibShare>& known = s_shares[key];
		m_share = known.lock();
		if (m_share == nullptr) {
			m_share = std::make_shared<ibShare>();
			known = m_share;
		}
	}
	Open();
}

ibLockFile::~ibLockFile()
{
	// What is still held stays written: its slot's byte goes with this, and whoever reads its rows next finds nobody
	// holding the slot and sweeps them — as after a process that died.
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	for (const auto& taken : m_slots)
		m_share->m_slots.erase(taken.first);
	m_slots.clear();
	m_dead.clear();
}

void ibLockFile::Open()
{
	if (!m_file.IsOpen())
		ibBackendCoreException::Error(_("The lock file %s cannot be opened"), m_file.GetPath());
	ibHeader header{};
	if (m_file.ReadAt(0, &header, sizeof(header)) && IsValid(header))
		return;
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	const ibFileByteLock whole(m_file, kFileByte, ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
	if (!whole.HoldsByte())
		ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
	if (m_file.ReadAt(0, &header, sizeof(header)) && IsValid(header))
		return;   // another process made it meanwhile
	// A new file, or one laid out otherwise: made anew — the directory and the slots empty, no region made yet.
	m_share->m_regions.clear();
	m_share->m_regionOf.clear();
	header = {};
	std::memcpy(header.magic, kMagic, sizeof(kMagic));
	header.version = kVersion;
	header.regions = kRegions;
	header.slots   = kSlotsPerPage;
	header.end     = kRegionsAt;
	const std::vector<unsigned char> zeros(static_cast<std::size_t>(kRegionsAt - kDirectoryAt), 0);
	if (!m_file.WriteAt(kDirectoryAt, zeros.data(), zeros.size()) || !m_file.WriteAt(0, &header, sizeof(header)))
		ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
	m_file.Truncate(kRegionsAt);   // what the file of before held past it is nobody's
}

// ⭐ A TABLE'S OWN REGION — its entry in the directory, found from where its name's hash points and on, and given to it
// (under the file's byte: another process may be giving the same entry to another table) the first time it is asked
// for. An entry keeps its table for as long as the file is (Reclaim empties a region, not its name), so the process
// asks the directory once a table. Two tables share a region only once the directory is full: then the one the hash
// points at, its rows told apart by their namespace. (It was the hash alone, 256 regions — and a compile's key a
// namespace of its own a module.)
int ibLockFile::RegionOf(const ibString& ns)
{
	if (const auto known = m_share->m_regionOf.find(ns); known != m_share->m_regionOf.end())
		return known->second;
	const std::uint64_t name = ns.GetHash() != 0 ? ns.GetHash() : 1;   // the same in every process and build; 0 — nobody's
	const int first = static_cast<int>(name % kRegions);
	for (int probe = 0; probe < kRegions; ++probe) {
		const int region = (first + probe) % kRegions;
		const std::uint64_t at = kDirectoryAt + region * sizeof(ibRegionEntry);
		ibRegionEntry entry{};
		if (!m_file.ReadAt(at, &entry, sizeof(entry)))
			ibBackendCoreException::Error(_("The lock file %s cannot be read"), m_file.GetPath());
		if (entry.name == name)
			return m_share->m_regionOf[ns] = region;
		if (entry.name != 0)
			continue;
		const ibFileByteLock whole(m_file, kFileByte, ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
		if (!whole.HoldsByte())
			ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
		m_file.ReadAt(at, &entry, sizeof(entry));
		if (entry.name == name)
			return m_share->m_regionOf[ns] = region;
		if (entry.name != 0)
			continue;   // another table's since
		entry.name = name;
		if (!m_file.WriteAt(at, &entry, sizeof(entry)))
			ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
		return m_share->m_regionOf[ns] = region;
	}
	return m_share->m_regionOf[ns] = first;
}

ibLockFile::ibCachedRegion& ibLockFile::ReadRegion(int region)
{
	ibCachedRegion& cached = m_share->m_regions[region];
	ibRegionEntry entry{};
	if (!m_file.ReadAt(kDirectoryAt + region * sizeof(ibRegionEntry), &entry, sizeof(entry)))
		ibBackendCoreException::Error(_("The lock file %s cannot be read"), m_file.GetPath());
	if (entry.offset == 0) {
		cached.Clear();
		cached.m_generation = 0;
		return cached;
	}
	if (entry.generation != 0 && cached.m_generation == entry.generation)
		return cached;   // nobody wrote it since this process did, or read it
	std::vector<unsigned char> bytes(entry.used);
	if (!bytes.empty() && !m_file.ReadAt(entry.offset, bytes.data(), bytes.size()))
		ibBackendCoreException::Error(_("The lock file %s cannot be read"), m_file.GetPath());
	cached.Load(std::move(bytes));
	cached.m_generation = entry.generation;
	return cached;
}

// The region whole, as `rows` are — what a change of every row (ChangeEach) leaves.
void ibLockFile::WriteRegion(int region, const std::vector<ibRow>& rows)
{
	ibCachedRegion& cached = m_share->m_regions[region];
	cached.Clear();
	for (const ibRow& row : rows)
		cached.Add(row);
	cached.m_bytesAt = 0;
	cached.m_marks.clear();
	WriteRegion(region, cached);
}

// ⭐ WHAT A BATCH CHANGED, written: the bytes it changed in place (a row let go, a mode raised) and after them what it
// added — or, past half the region let go, the region again without those (Compact), or, past its room, all of it in a
// room twice as big.
void ibLockFile::WriteRegion(int region, ibCachedRegion& cached)
{
	const std::uint64_t at = kDirectoryAt + region * sizeof(ibRegionEntry);
	ibRegionEntry entry{};
	if (!m_file.ReadAt(at, &entry, sizeof(entry)))
		ibBackendCoreException::Error(_("The lock file %s cannot be read"), m_file.GetPath());
	if (cached.m_goneBytes > 0 && cached.m_goneBytes * 2 > cached.m_bytes.size())
		cached.Compact();
	if (cached.m_marks.size() > 8) {
		// Many bytes in place — a release of a transaction's rows: one write from the first of them on.
		cached.m_bytesAt = *std::min_element(cached.m_marks.begin(), cached.m_marks.end());
		cached.m_marks.clear();
	}

	if (cached.m_bytes.size() > entry.capacity) {
		// ⭐ A REGION ONLY GROWS, by doubling, into room made at the end of the file: its old room is left where it was
		// (Reclaim takes it back once nobody holds anything).
		std::uint32_t capacity = std::max(kRegionFirstBytes, entry.capacity * 2);
		while (capacity < cached.m_bytes.size())
			capacity *= 2;
		const ibFileByteLock whole(m_file, kFileByte, ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
		ibHeader header{};
		if (!whole.HoldsByte() || !m_file.ReadAt(0, &header, sizeof(header)))
			ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
		header.left   += entry.offset != 0 ? entry.capacity : 0;
		entry.offset   = header.end;
		entry.capacity = capacity;
		header.end    += capacity;
		cached.m_bytesAt = 0;   // a new room: all of it written
		cached.m_marks.clear();
		if (!m_file.WriteAt(0, &header, sizeof(header)))
			ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
	}
	for (const std::size_t mark : cached.m_marks)
		if (!m_file.WriteAt(entry.offset + mark, cached.m_bytes.data() + mark, 1))
			ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
	const std::size_t from = cached.m_bytesAt;
	entry.used       = static_cast<std::uint32_t>(cached.m_bytes.size());
	entry.generation = entry.generation != 0 ? entry.generation + 1 : FirstGeneration();
	if ((cached.m_bytes.size() > from
	     && !m_file.WriteAt(entry.offset + from, cached.m_bytes.data() + from, cached.m_bytes.size() - from))
	    || !m_file.WriteAt(at, &entry, sizeof(entry)))
		ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
	cached.m_generation = entry.generation;
	cached.Begin();
}

// ⭐ THE FILE MADE SHORT AGAIN WHEN NOBODY HOLDS ANYTHING. A region that doubles takes new room at the end and leaves its
// old room behind, so a file stays near twice the most it ever held (2026-11-26: 4.3 MB, nothing held). Asked as this
// process lets go of its last slot, with more than kReclaimPast left behind, at most once in kReclaimEvery. Every byte
// is taken WITHOUT WAITING and held to the end — whoever is changing the file keeps it as it is: the file's (no room made
// meanwhile), every region's (no row written), every slot's made (no owner taking one: it would write into a page the cut
// takes away), and a slot in use only by a process that is gone (a row is only ever written by an owner holding a slot).
// Then the regions are emptied — each keeps its table: a process that found it a moment ago writes there still — the
// pages of slots after the first forgotten, the slots of the gone cleared, and the file cut back to where the regions
// begin. Best-effort: it runs where a lock is let go.
void ibLockFile::Reclaim()
{
	try {
		const auto now = std::chrono::steady_clock::now();
		if (now < m_share->m_reclaimAt)
			return;
		ibHeader header{};
		if (!m_file.ReadAt(0, &header, sizeof(header)) || header.left < kReclaimPast)
			return;   // what the regions use now is not taken back: they would only grow into it again
		m_share->m_reclaimAt = now + kReclaimEvery;

		std::vector<ibFileByteLock> held;
		held.emplace_back(m_file, kFileByte, ibFileByteLock::ibMode::Exclusive);
		std::vector<ibRegionEntry> directory(kRegions);
		if (!held.back().HoldsByte() || !m_file.ReadAt(0, &header, sizeof(header))
		    || !m_file.ReadAt(kDirectoryAt, directory.data(), directory.size() * sizeof(ibRegionEntry)))
			return;
		const int made = SlotsMade();
		held.reserve(1 + kRegions + made);
		for (int region = 0; region < kRegions; ++region) {
			held.emplace_back(m_file, RegionByte(region), ibFileByteLock::ibMode::Exclusive);
			if (!held.back().HoldsByte())
				return;
		}
		std::vector<int> gone;
		for (int slot = 0; slot < made; ++slot) {
			held.emplace_back(m_file, SlotByte(slot), ibFileByteLock::ibMode::Exclusive);
			ibSlot record{};
			const std::uint64_t at = SlotAt(slot);
			if (!held.back().HoldsByte() || at == 0 || !m_file.ReadAt(at, &record, sizeof(record)))
				return;   // an owner holds it: its rows stand, and its page
			if (record.used != 0)
				gone.push_back(slot);
		}

		for (ibRegionEntry& entry : directory)
			entry = { 0, 0, 0, 0, entry.name };
		const std::vector<unsigned char> pages(static_cast<std::size_t>(kSlotPages * sizeof(std::uint64_t)), 0);
		const std::uint64_t was = header.end;
		header.end  = kRegionsAt;
		header.left = 0;
		for (const int slot : gone)
			if (slot < kSlotsPerPage) {
				const ibSlot empty{};
				m_file.WriteAt(SlotAt(slot), &empty, sizeof(empty));   // the first page stays; the others go with the cut
			}
		if (!m_file.WriteAt(kDirectoryAt, directory.data(), directory.size() * sizeof(ibRegionEntry))
		    || !m_file.WriteAt(kSlotPagesAt, pages.data(), pages.size()) || !m_file.WriteAt(0, &header, sizeof(header)))
			return;
		m_share->m_regions.clear();
		m_file.Truncate(kRegionsAt);
		ibJournalInfo(ibT("lock"), ibT("the lock file %s cut back from %llu bytes: nothing was held"), m_file.GetPath(),
			static_cast<unsigned long long>(was));
	}
	catch (...) {}   // a lock let go is let go; the room is taken back another time
}

bool ibLockFile::IsAlive(int slot)
{
	if (slot < 0 || slot >= kMaxSlots || m_dead.contains(slot))
		return false;
	if (m_share->m_slots.contains(slot))
		return true;
	ibFileByteLock probe(m_file, SlotByte(slot), ibFileByteLock::ibMode::Exclusive);
	if (!probe.HoldsByte())
		return true;   // its process holds it
	// Nobody does: the process that wrote its rows is gone. The byte kept until its rows are swept, so no other process
	// sweeps them as well, nor takes the slot meanwhile.
	m_dead.emplace(slot, std::move(probe));
	return false;
}

void ibLockFile::Change(const std::vector<ibString>& namespaces, const std::function<void(ibBatch&)>& change)
{
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	SweepDead();

	std::map<ibString, int> regionOf;
	std::vector<int> regions;
	for (const ibString& ns : namespaces) {
		const int region = RegionOf(ns);
		regionOf[ns] = region;
		regions.push_back(region);
	}
	std::sort(regions.begin(), regions.end());
	regions.erase(std::unique(regions.begin(), regions.end()), regions.end());
	{
		// In the order of their numbers, so two batches cannot each hold what the other asks for next.
		std::vector<ibFileByteLock> held;
		held.reserve(regions.size());
		ibBatch batch;
		try {
			for (const int region : regions) {
				held.emplace_back(m_file, RegionByte(region), ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
				if (!held.back().HoldsByte())
					ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
				ibCachedRegion& cached = ReadRegion(region);
				cached.Begin();
				std::vector<int> dead;   // a slot asked once a change, not once a row
				for (const auto& [slot, count] : cached.m_bySlot)
					if (!IsAlive(slot))
						dead.push_back(slot);
				for (const int slot : dead)
					cached.BurySlot(slot);
				batch.m_held.push_back(&cached);
			}
			for (const auto& [ns, region] : regionOf)
				batch.m_regionOf[ns] = &m_share->m_regions[region];
			change(batch);
			for (std::size_t i = 0; i < regions.size(); ++i)
				if (batch.m_held[i]->m_changed)
					WriteRegion(regions[i], *batch.m_held[i]);
		}
		catch (...) {
			// What the batch changed is only the process's copy, or half written: read again by the next.
			for (std::size_t i = 0; i < batch.m_held.size(); ++i)
				if (batch.m_held[i]->m_changed)
					m_share->m_regions.erase(regions[i]);
			throw;
		}
	}
	SweepDead();
}

void ibLockFile::ibBatch::EachOfKey(const ibString& ns, std::uint64_t fieldsHash, const ibString& keyData,
                                   const std::function<void(const ibRow&)>& each) const
{
	const auto known = m_regionOf.find(ns);
	if (known == m_regionOf.end())
		ibBackendCoreException::Error(ibT("ibLockFile: the rows of '%s' were asked of a batch that did not name it"), ns);
	const ibCachedRegion& cached = *known->second;
	const auto [from, to] = cached.m_byKey.equal_range(KeyOf(ns, keyData));
	for (auto at = from; at != to; ++at) {
		const ibRow& row = cached.m_rows[at->second];
		if (!cached.m_gone[at->second] && row.m_fieldsHash == fieldsHash && row.m_namespace.Cmp(ns) == 0
		    && row.m_keyData.CmpNoCase(keyData) == 0)
			each(row);
	}
	const auto table = cached.m_byFields.find(ns);
	if (table == cached.m_byFields.end())
		return;
	for (const auto& [fields, list] : table->second) {
		if (fields == fieldsHash)
			continue;   // of the same fields only the same text meets, read above
		for (const std::size_t i : list)
			if (!cached.m_gone[i])
				each(cached.m_rows[i]);
	}
}

void ibLockFile::ibBatch::Add(ibRow row)
{
	const auto known = m_regionOf.find(row.m_namespace);
	if (known == m_regionOf.end())
		ibBackendCoreException::Error(ibT("ibLockFile: a row of '%s' was added to a batch that did not name it"), row.m_namespace);
	known->second->Add(std::move(row));
}

void ibLockFile::ibBatch::Remove(const ibGuid& lockGuid)
{
	for (ibCachedRegion* const cached : m_held) {
		const auto row = cached->m_byGuid.find(lockGuid);
		if (row == cached->m_byGuid.end())
			continue;
		cached->Bury(row->second);
		return;
	}
}

void ibLockFile::ibBatch::SetMode(const ibGuid& lockGuid, ibLockMode mode)
{
	for (ibCachedRegion* const cached : m_held) {
		const auto row = cached->m_byGuid.find(lockGuid);
		if (row == cached->m_byGuid.end())
			continue;
		cached->SetMode(row->second, mode);
		return;
	}
}

void ibLockFile::ChangeEach(const std::function<bool(std::vector<ibRow>&)>& change)
{
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	SweepDead();
	EachRegion(change);
	SweepDead();
}

// Every region made, one at a time, each held while its rows are read and written; a dead owner's rows dropped on the
// way, as in Change.
void ibLockFile::EachRegion(const std::function<bool(std::vector<ibRow>&)>& change)
{
	std::vector<ibRegionEntry> directory(kRegions);
	if (!m_file.ReadAt(kDirectoryAt, directory.data(), directory.size() * sizeof(ibRegionEntry)))
		return;
	for (int region = 0; region < kRegions; ++region) {
		if (directory[region].offset == 0)
			continue;
		const ibFileByteLock held(m_file, RegionByte(region), ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
		if (!held.HoldsByte())
			ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
		try {
			std::vector<ibRow> rows = ReadRegion(region).LiveRows();
			const bool dropped = std::erase_if(rows, [this](const ibRow& row) { return !IsAlive(row.m_slot); }) > 0;
			if (change(rows) || dropped)
				WriteRegion(region, rows);
		}
		catch (...) {
			m_share->m_regions.erase(region);   // read again by the next
			throw;
		}
	}
}

void ibLockFile::SweepDead()
{
	while (!m_dead.empty())
		SweepSlot(m_dead.begin()->first);
}

void ibLockFile::SweepSlot(int slot)
{
	try {
		EachRegion([slot](std::vector<ibRow>& rows) {
			return std::erase_if(rows, [slot](const ibRow& row) { return row.m_slot == slot; }) > 0;
		});
		const ibSlot record{};
		if (const std::uint64_t at = SlotAt(slot); at != 0)
			m_file.WriteAt(at, &record, sizeof(record));   // free first, then its byte
	}
	catch (const ibCoreException& err) {
		ibJournalInfo(ibT("lock"), ibT("the rows of a dead owner (slot %d) were not swept: %s"), slot, err.GetErrorDescription());
	}
	m_dead.erase(slot);
}

std::vector<ibLockFile::ibRow> ibLockFile::ReadAll()
{
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	std::vector<ibRow> rows;
	std::vector<ibRegionEntry> directory(kRegions);
	if (!m_file.ReadAt(kDirectoryAt, directory.data(), directory.size() * sizeof(ibRegionEntry)))
		return rows;
	for (int region = 0; region < kRegions; ++region) {
		if (directory[region].offset == 0)
			continue;
		const ibFileByteLock held(m_file, RegionByte(region), ibFileByteLock::ibMode::Shared, kHoldWaitMs);
		if (!held.HoldsByte())
			continue;
		std::vector<ibRow> part = ReadRegion(region).LiveRows();
		rows.insert(rows.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
	}
	return rows;
}

// Where `slot` is in the file: the first page's in its place, a further page's where the page was made; 0 — its page
// is not made.
std::uint64_t ibLockFile::SlotAt(int slot)
{
	if (slot < 0 || slot >= kMaxSlots)
		return 0;
	const int page = slot / kSlotsPerPage;
	const std::uint64_t within = static_cast<std::uint64_t>(slot % kSlotsPerPage) * kSlotBytes;
	if (page == 0)
		return kSlotsAt + within;
	std::uint64_t offset = 0;
	if (!m_file.ReadAt(kSlotPagesAt + page * sizeof(std::uint64_t), &offset, sizeof(offset)) || offset == 0)
		return 0;
	return offset + within;
}

// The slots made: the first page's and those of the pages made after it, in order.
int ibLockFile::SlotsMade()
{
	std::array<std::uint64_t, kSlotPages> pages{};
	if (!m_file.ReadAt(kSlotPagesAt, pages.data(), sizeof(pages)))
		return kSlotsPerPage;
	int made = 1;
	while (made < kSlotPages && pages[made] != 0)
		++made;
	return made * kSlotsPerPage;
}

// ⭐ A PAGE OF SLOTS MORE, at the end of the file, once every slot made is held: the owners a base has at once are not
// counted by a number written into the layout (2026-11-26: 1024, and the next one refused). Under the file's byte; a
// page another process made meanwhile is the one asked for.
void ibLockFile::MakeSlotPage(int page)
{
	if (page <= 0 || page >= kSlotPages)
		return;
	const ibFileByteLock whole(m_file, kFileByte, ibFileByteLock::ibMode::Exclusive, kHoldWaitMs);
	ibHeader header{};
	std::uint64_t made = 0;
	const std::uint64_t entry = kSlotPagesAt + page * sizeof(std::uint64_t);
	if (!whole.HoldsByte() || !m_file.ReadAt(0, &header, sizeof(header)) || !m_file.ReadAt(entry, &made, sizeof(made)))
		ibBackendCoreException::Error(_("The lock file %s stayed held by another process"), m_file.GetPath());
	if (made != 0)
		return;
	const std::vector<unsigned char> zeros(static_cast<std::size_t>(kSlotPageBytes), 0);
	const std::uint64_t at = header.end;
	header.end += kSlotPageBytes;
	if (!m_file.WriteAt(at, zeros.data(), zeros.size()) || !m_file.WriteAt(entry, &at, sizeof(at))
	    || !m_file.WriteAt(0, &header, sizeof(header)))
		ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
}

int ibLockFile::TakeSlot(const ibGuid& owner, const ibString& user, const ibString& computer)
{
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	const unsigned int pid = ibCrashGuard::CurrentPidPortable();
	if (m_share->m_nextSlot < 0)
		m_share->m_nextSlot = static_cast<int>(pid % kSlotsPerPage);   // processes start apart, and seldom ask a slot another holds
	for (;;) {
		const int made = SlotsMade();
		for (int step = 0; step < made; ++step) {
			const int slot = (m_share->m_nextSlot + step) % made;
			if (m_share->m_slots.contains(slot) || m_dead.contains(slot))
				continue;
			ibFileByteLock hold(m_file, SlotByte(slot), ibFileByteLock::ibMode::Exclusive);
			if (!hold.HoldsByte())
				continue;
			const std::uint64_t at = SlotAt(slot);
			if (at == 0)
				continue;
			ibSlot record{};
			m_file.ReadAt(at, &record, sizeof(record));
			if (record.used != 0) {
				// A slot a dead process left: its rows are nobody's, swept before the slot is anybody's again.
				m_dead.emplace(slot, std::move(hold));
				SweepSlot(slot);
				hold = ibFileByteLock(m_file, SlotByte(slot), ibFileByteLock::ibMode::Exclusive);
				if (!hold.HoldsByte())
					continue;
			}
			record = {};
			record.used     = 1;
			record.pid      = pid;
			record.waitsFor = -1;
			std::memcpy(record.owner, owner.bytes().data(), 16);
			std::memcpy(record.process, ThisProcess().bytes().data(), 16);
			PutText(record.user, sizeof(record.user), user);
			PutText(record.computer, sizeof(record.computer), computer);
			if (!m_file.WriteAt(at, &record, sizeof(record)))
				ibBackendCoreException::Error(_("The lock file %s cannot be written"), m_file.GetPath());
			m_share->m_slots.insert(slot);
			m_slots.emplace(slot, std::move(hold));
			m_share->m_nextSlot = slot + 1;   // the next owner looks past it, not again through what is held
			return slot;
		}
		if (made >= kMaxSlots)
			break;
		MakeSlotPage(made / kSlotsPerPage);
	}
	ibBackendCoreException::Error(_("The lock file %s has no free slot for another owner"), m_file.GetPath());
	return -1;
}

void ibLockFile::FreeSlot(int slot)
{
	std::lock_guard<std::recursive_mutex> serialised(m_share->m_mtx);
	const auto taken = m_slots.find(slot);
	if (taken == m_slots.end())
		return;
	// Free first, then the byte: whoever finds the byte free finds the slot free too.
	const ibSlot record{};
	if (const std::uint64_t at = SlotAt(slot); at != 0)
		m_file.WriteAt(at, &record, sizeof(record));
	m_share->m_slots.erase(slot);
	m_slots.erase(taken);
	if (m_share->m_slots.empty())
		Reclaim();
}

ibLockFile::ibOwner ibLockFile::ReadSlot(int slot)
{
	ibOwner owner;
	ibSlot record{};
	const std::uint64_t at = SlotAt(slot);
	if (at == 0 || !m_file.ReadAt(at, &record, sizeof(record)))
		return owner;
	owner.m_used     = record.used != 0;
	owner.m_owner    = GuidOf(record.owner);
	owner.m_user     = GetText(record.user, sizeof(record.user));
	owner.m_computer = GetText(record.computer, sizeof(record.computer));
	owner.m_waitsFor = record.waitsFor;
	return owner;
}

void ibLockFile::SetWaitsFor(int slot, int holder)
{
	const std::uint64_t at = SlotAt(slot);
	if (at == 0)
		return;
	const std::int32_t waitsFor = holder;
	m_file.WriteAt(at + offsetof(ibSlot, waitsFor), &waitsFor, sizeof(waitsFor));
}

void ibLockFile::WaitForRelease(int slot, std::int32_t waitMs)
{
	if (slot < 0 || slot >= kMaxSlots || waitMs <= 0)
		return;
	// Got — its owner let go of all it held, or its process went. Let go at once: it is the owner's to take again.
	const ibFileByteLock probe(m_file, SlotByte(slot), ibFileByteLock::ibMode::Shared, waitMs);
}
