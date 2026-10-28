#include "tempStorage.h"

#include "backend/appData.h"                              // file_table, the base's pool, its temporary storage
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — DML + typed row reads
#include "core/diagnostics/journal.h"                  // a part that did not go down is a line
#include "core/fdatetime.h"                            // ibDateTime — when a part was written
#include "backend/session/session.h"                      // ibTempFile — the session working

#include <algorithm>
#include <set>

#include <wx/filename.h>

///////////////////////////////////////////////////////////////////////////////
//								ibTempStorage
///////////////////////////////////////////////////////////////////////////////

namespace {

// Part 0 is the file's header — its name and owner; the content parts follow it from 1.
constexpr int kHeaderPart = 0;

// The row of the file's part — its own key in the table.
ibString PartKey(const ibString& id, int partIndex)
{
	return ibString::Format(ibT("%s:%d"), id, partIndex);
}

ibQueryExprPtr IsSessionsFile(const ibString& id, const ibGuid& session)
{
	return ibBinOp(ibQueryBinOp::And,
		ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("fileKey")), ibConst(ibValue(id))),
		ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("sessionGuid")), ibConst(ibValue(session.str()))));
}

} // namespace

ibTempStorage::ibTempStorage(ib::AppDataCtorToken owner)
{
	// Its holder takes connections from ITS base's pool — named once, here, as the lock manager's is.
	m_holder.SetPool(ibApplicationInstance::GetConnectionPool(owner.GetApplicationInstance()));
}

ibTempStorage::~ibTempStorage() = default;

ibString ibTempStorage::Create(const ibGuid& session, const ibString& name)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	const ibString fileName = wxFileNameFromPath(name);
	if (fileName.IsEmpty() || !session.isValid())
		return ibString();

	const ibString id = ibGuid(ibGuid::newGuid()).str();
	try {
		ibDatabaseQueryBuilder q(&m_holder);
		q.Execute(ibInsert(file_table, {
			{ ibT("partKey"),     ibConst(ibValue(PartKey(id, kHeaderPart))) },
			{ ibT("fileKey"),     ibConst(ibValue(id)) },
			{ ibT("partIndex"),   ibConst(ibValue(kHeaderPart)) },
			{ ibT("sessionGuid"), ibConst(ibValue(session.str())) },
			{ ibT("fileName"),    ibConst(ibValue(fileName)) },
			{ ibT("changed"),     ibConst(ibValue(ibDateTime::Now())) },
			{ ibT("dataSize"),    ibConst(ibValue(0)) },
		}));
		return id;
	}
	catch (...) {
		ibJournalWarning(ibT("file"), ibT("the temporary file '%s' was not made"), fileName);
		return ibString();
	}
}

ibString ibTempStorage::GetName(const ibGuid& session, const ibString& id) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ ibT("fileName") })
			.Where(ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
				ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("partIndex")), ibParam(0))))
			.Execute({ ibValue(kHeaderPart) });
		return rs.Next() ? ibString(rs.GetResultString(ibT("fileName"))) : ibString();
	}
	catch (...) {
		return ibString();
	}
}

bool ibTempStorage::WritePart(const ibGuid& session, const ibString& id, int index, const void* data,
	std::size_t size)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (index < 0 || size > kPartSize || GetName(session, id).IsEmpty())
		return false;

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		// The part's key is the primary key: a part written twice is refused by the table, not overwritten.
		q.Execute(ibInsert(file_table, {
			{ ibT("partKey"),     ibConst(ibValue(PartKey(id, index + 1))) },
			{ ibT("fileKey"),     ibConst(ibValue(id)) },
			{ ibT("partIndex"),   ibConst(ibValue(index + 1)) },
			{ ibT("sessionGuid"), ibConst(ibValue(session.str())) },
			{ ibT("changed"),     ibConst(ibValue(ibDateTime::Now())) },
			{ ibT("dataSize"),    ibConst(ibValue(static_cast<unsigned int>(size))) },
			{ ibT("binaryData"),  ibConstBlob(data, size) },
		}));
		return true;
	}
	catch (...) {
		ibJournalWarning(ibT("file"), ibT("part %d of the temporary file %s was not written"), index, id);
		return false;
	}
}

bool ibTempStorage::ReadPart(const ibGuid& session, const ibString& id, int index, ibMemoryBuffer& data) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	data.SetDataLen(0);
	if (index < 0)
		return false;

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ ibT("binaryData") })
			.Where(ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
				ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("partIndex")), ibParam(0))))
			.Execute({ ibValue(index + 1) });
		if (!rs.Next())
			return false;
		rs.GetResultBlob(ibT("binaryData"), data);
		return true;
	}
	catch (...) {
		return false;
	}
}

int ibTempStorage::PartCount(const ibGuid& session, const ibString& id) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		// The last part's number — the content parts are 1…n after the header.
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ ibT("partIndex") })
			.Where(IsSessionsFile(id, session))
			.OrderBy(ibT("partIndex"), ibQuerySortDir::Desc)
			.Limit(1)
			.Execute();
		return rs.Next() ? rs.GetResultInt(ibT("partIndex")) : 0;
	}
	catch (...) {
		return 0;
	}
}

bool ibTempStorage::Clear(const ibGuid& session, const ibString& id)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		q.Execute(ibDelete(file_table, ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
			ibBinOp(ibQueryBinOp::Gt, ibCol(ibT("partIndex")), ibConst(ibValue(kHeaderPart))))));
		return true;   // clearing what has no content (0 rows) is success; a real failure THROWS
	}
	catch (...) {
		return false;
	}
}

void ibTempStorage::OnSessionEnd(const ibGuid& session)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (!session.isValid())
		return;

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		if (!q.IsOpen())
			return;
		// Called from the registry thread. A waiting delete sits in front of Add.
		ibDbTxOptions opts;
		opts.noWait = true;
		q.BeginTransaction(opts);
		q.Execute(ibDelete(file_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("sessionGuid")), ibConst(ibValue(session.str())))));
		q.Commit();
	}
	catch (...) {
		// Left for the sweep, which hands over who is alive and takes the rest.
	}
}

void ibTempStorage::SweepOrphans(const std::vector<ibGuid>& liveSessionGuids)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	std::set<ibString> live;
	for (const ibGuid& guid : liveSessionGuids)
		live.insert(guid.str());

	// The owners first, the deletes after — one per owner, through OnSessionEnd, as the lock manager does.
	std::set<ibString> orphans;
	try {
		ibDatabaseQueryBuilder q(&m_holder);
		if (!q.IsOpen())
			return;
		// The read is on the registry thread. noWait so it cannot sit out a metadata lock.
		ibDbTxOptions opts;
		opts.noWait = true;
		q.BeginTransaction(opts);
		{
			ibQueryResult rs = q.From(file_table)
				.Select({ ibT("sessionGuid") })
				.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(ibT("partIndex")), ibParam(0)))
				.Execute({ ibValue(kHeaderPart) });
			while (rs.Next()) {
				const ibString owner = rs.GetResultString(ibT("sessionGuid"));
				if (!owner.IsEmpty() && live.find(owner) == live.end())
					orphans.insert(owner);
			}
		}
		q.Commit();
	}
	catch (...) {
		return;   // transient DB error, or the table is locked — the next sweep retries
	}

	for (const ibString& owner : orphans)
		OnSessionEnd(ibGuid(owner));
}

///////////////////////////////////////////////////////////////////////////////
//								ibTempFile
///////////////////////////////////////////////////////////////////////////////

bool ibTempFile::Open(const ibString& id)
{
	m_storage = nullptr;
	ibSession* const session = ibSession::Current();
	ibTempStorage* const storage = session != nullptr
		? ibApplicationInstance::GetTempStorage(session->GetApplicationInstance()) : nullptr;
	if (storage == nullptr || storage->GetName(session->Identity().m_guid, id).IsEmpty())
		return false;

	m_storage = storage;
	m_session = session->Identity().m_guid;
	m_id = id;
	m_written = 0;
	return true;
}

bool ibTempFile::Create(const ibString& id, bool overwrite)
{
	if (!Open(id))
		return false;
	if (!overwrite && m_storage->PartCount(m_session, m_id) != 0) {
		m_storage = nullptr;
		return false;
	}
	if (!m_storage->Clear(m_session, m_id)) {
		m_storage = nullptr;
		return false;
	}
	return true;
}

ibString ibTempFile::GetName() const
{
	return m_storage != nullptr ? m_storage->GetName(m_session, m_id) : ibString();
}

bool ibTempFile::ReadPart(int index, ibMemoryBuffer& data) const
{
	return m_storage != nullptr && m_storage->ReadPart(m_session, m_id, index, data);
}

bool ibTempFile::ReadAll(ibString* str, const wxMBConv& conv)
{
	if (m_storage == nullptr || str == nullptr)
		return false;

	ibMemoryBuffer content, part;
	for (int index = 0; ReadPart(index, part); ++index)
		content.AppendData(part.GetData(), part.GetDataLen());

	*str = wxString(static_cast<const char*>(content.GetData()), conv, content.GetDataLen());
	// A conversion that failed comes back empty — the file was not text in `conv`.
	return content.GetDataLen() == 0 || !str->IsEmpty();
}

bool ibTempFile::Write(const ibString& str, const wxMBConv& conv)
{
	// The converter's own buffer: a wxString's mb_str() points into that string, here a temporary (valueHttp.cpp,
	// ibHttpEncode, says what it cost).
	size_t length = 0;
	const wxCharBuffer bytes = conv.cWC2MB(str.wc_str(), wxNO_LEN, &length);
	if (length == wxCONV_FAILED || (!str.IsEmpty() && length == 0))
		return false;   // `str` cannot be said in `conv`
	return Write(bytes.data(), length);
}

bool ibTempFile::Write(const void* data, std::size_t size)
{
	if (m_storage == nullptr)
		return false;

	const char* bytes = static_cast<const char*>(data);
	while (size != 0) {
		const std::size_t part = std::min(size, ibTempStorage::kPartSize);
		if (!m_storage->WritePart(m_session, m_id, m_written++, bytes, part))
			return false;
		bytes += part;
		size -= part;
	}
	return true;
}
