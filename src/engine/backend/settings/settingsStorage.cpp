#include "settingsStorage.h"

#include "backend/appData.h"                              // settings_table
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — DML + typed row reads
#include "backend/lock/lockKeyHash.h"                     // the deterministic address hash (sys_lock's own)
#include "backend/serialize/dataBuilder.h"                // ibDataNode + ibBinaryProvider
#include <algorithm>

///////////////////////////////////////////////////////////////////////////////
//								ibSettingsStorage
///////////////////////////////////////////////////////////////////////////////

namespace {
// ⚠ ONE TRANSLATOR, and it exists because there is no ibValue(ibGuid): a guid reaches a bound
// parameter through its string form, and C++ will not chain two user-defined conversions to get
// there. Twenty call sites in the tree spell `ibValue(x.str())` for the same reason; here it is
// said once so the statements below read as addresses rather than as conversions.
inline ibValue Text(const ibGuid& guid) { return ibValue(guid.str()); }
} // namespace

ibSettingsStorage::ibSettingsStorage(ib::AppDataCtorToken)
{
	// Nothing to bring up. The table is created by ibApplicationData with the
	// other app-tables (one owner of the schema, as sys_job and sys_lock have),
	// and every call below opens its own builder on the infra channel.
}

ibSettingsStorage::~ibSettingsStorage() = default;

// ONE COLUMN CARRYING A COMPOUND ADDRESS. The DDL renderer spells PRIMARY KEY per
// column, so four key columns would emit four PRIMARY KEY clauses and no driver
// would take the CREATE. sys_lock met this first and answered it the same way:
// a deterministic hash for the lookup, the readable parts beside it for a person
// reading the table and for "everything this user saved" queries.
wxString ibSettingsStorage::HashKey(const ibSettingsKey& key)
{
	return ibLockKey::Hash({
		{ wxT("category"),   ibValue(static_cast<signed int>(key.m_category)) },
		// The guids as TEXT for hashing — a guid renders one way and only one way, so the hash of an
		// address is the same on every driver, every platform and every process.
		{ wxT("objectKey"),  Text(key.m_objectKey)  },
		{ wxT("settingKey"), Text(key.m_settingKey) },
		{ wxT("userKey"),    Text(key.m_userKey)    },
	});
}

bool ibSettingsStorage::Save(const ibSettingsKey& key, const ibDataNode& node)
{
	if (!key.IsOk())
		return false;

	// Node -> bytes through the binary provider, exactly as a form's control tree
	// and a metadata node are written. Which format the bytes are in stays the
	// provider's business: a JSON dump of the same node is one substitution away.
	ibWriterMemory writer;
	if (!ibBinaryProvider().Write(node, writer))
		return false;

	try {
		// ONE UPSERT, matched on the address hash — re-saving a setting updates
		// its row rather than growing a second one, and the per-driver spelling
		// (ON CONFLICT / UPDATE OR INSERT … MATCHING) is closed by the door.
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(settings_table, {
			{ wxT("entryKey"),   ibConst(ibValue(HashKey(key))) },
			{ wxT("category"),   ibConst(ibValue(static_cast<signed int>(key.m_category))) },
			{ wxT("objectKey"),  ibConst(Text(key.m_objectKey))  },
			{ wxT("settingKey"), ibConst(Text(key.m_settingKey)) },
			{ wxT("userKey"),    ibConst(Text(key.m_userKey))    },
			{ wxT("changed"),    ibConst(ibValue(wxDateTime::Now())) },
			{ wxT("dataSize"),   ibConst(ibValue(static_cast<unsigned int>(writer.size()))) },
			// Opaque bytes bound as a blob constant — L2 never interprets them,
			// which is right for a payload whose format belongs to the value.
			{ wxT("binaryData"), ibConstBlob(writer.pointer(), writer.size()) },
		}, { wxT("entryKey") }));
		return true;   // a real failure THROWS; the affected-row count is not an error
	}
	catch (...) {
		// A SETTING THAT DID NOT SAVE IS NOT A REASON TO TAKE THE CALLER DOWN — but
		// it is not silence either: the caller is told false, and the journal keeps
		// what happened, because a settings write failing quietly is exactly the
		// bug people report as "it forgets what I set".
		ibJournalWarning(wxT("settings"), wxT("save failed: category %d, object %s, setting %s"),
			static_cast<int>(key.m_category), key.m_objectKey.str(), key.m_settingKey.str());
		return false;
	}
}

bool ibSettingsStorage::Restore(const ibSettingsKey& key, ibDataNode& node) const
{
	if (!key.IsOk())
		return false;

	wxMemoryBuffer blob;
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(settings_table)
			.Select({ wxT("binaryData") })
			.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("entryKey")), ibParam(0)))
			.Execute({ ibValue(HashKey(key)) });

		if (!rs.Next())
			return false;   // nobody saved anything here — the caller keeps what it had

		rs.GetResultBlob(wxT("binaryData"), blob);
	}
	catch (...) {
		// No table, no base, a passive scope: NO OPINION, the same answer as an
		// absent row. What "no setting" means belongs to the caller — an author's
		// variant stands, a window opens with its defaults.
		return false;
	}

	if (blob.GetDataLen() == 0)
		return false;

	// ⚠ THE BUFFER IS HELD IN A NAMED VARIABLE, deliberately: ibReaderMemory
	// BORROWS the bytes, so a temporary would leave it reading freed memory.
	ibReaderMemory reader(blob);
	return ibBinaryProvider().Read(reader, node);
}

std::vector<ibSettingsEntry> ibSettingsStorage::List(ibSettingsCategory category, const ibGuid& objectKey,
                                                    const ibGuid& userKey) const
{
	std::vector<ibSettingsEntry> entries;
	if (!objectKey.isValid())
		return entries;

	try {
		// The readable address, which is what settings_index_1 is for — the hash
		// answers "this one setting", this answers "everything about this object".
		ibDatabaseQueryBuilder q;
		ibQueryResult rs = q.From(settings_table)
			.Select({ wxT("settingKey"), wxT("changed") })
			.Where(ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("category")),  ibParam(0)),
				ibBinOp(ibQueryBinOp::And,
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("objectKey")), ibParam(1)),
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("userKey")),   ibParam(2)))))
			.Execute({
				ibValue(static_cast<signed int>(category)),
				Text(objectKey),
				Text(userKey),
			});

		while (rs.Next()) {
			ibSettingsEntry entry;
			const ibValue name = rs.GetValue(1);
			if (!name.IsNull())
				entry.m_settingKey = ibGuid(name.GetString());

			const ibValue changed = rs.GetValue(2);
			if (!changed.IsNull() && !changed.IsEmpty())
				entry.m_changed = wxDateTime(wxLongLong(changed.GetDate()));

			entries.push_back(entry);
		}
	}
	catch (...) {
		// No table, no base: nothing saved as far as anyone here can tell. A menu
		// with no entries is the honest answer, and it is also the true one on a
		// base where nobody has saved a setting yet.
		entries.clear();
	}

	// NEWEST FIRST, sorted HERE rather than in the query: ORDER BY on a nullable
	// date sorts differently per dialect (NULLS FIRST / LAST), and a row written
	// before this column meant anything must not decide where a menu opens.
	std::stable_sort(entries.begin(), entries.end(),
		[](const ibSettingsEntry& a, const ibSettingsEntry& b) {
			if (!a.m_changed.IsValid()) return false;
			if (!b.m_changed.IsValid()) return true;
			return a.m_changed > b.m_changed;
		});
	return entries;
}

bool ibSettingsStorage::Remove(const ibSettingsKey& key)
{
	if (!key.IsOk())
		return false;

	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(settings_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("entryKey")), ibConst(ibValue(HashKey(key))))));
		return true;   // removing an absent setting (0 rows) is success; a real failure THROWS
	}
	catch (...) {
		return false;
	}
}
