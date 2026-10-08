#include "tempStorage.h"

#include "backend/appData.h"                              // file_table, the base's pool, its temporary storage
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door — DML + typed row reads
#include "backend/diagnostics/journal.h"                  // a part that did not go down is a line
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
wxString PartKey(const wxString& id, int partIndex)
{
	return wxString::Format(wxT("%s:%d"), id, partIndex);
}

ibQueryExprPtr IsSessionsFile(const wxString& id, const ibGuid& session)
{
	return ibBinOp(ibQueryBinOp::And,
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("fileKey")), ibConst(ibValue(id))),
		ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("sessionGuid")), ibConst(ibValue(session.str()))));
}

} // namespace

ibTempStorage::ibTempStorage(ib::AppDataCtorToken owner)
{
	// Its holder takes connections from ITS base's pool — named once, here, as the lock manager's is.
	m_holder.SetPool(ibApplicationInstance::GetConnectionPool(owner.GetApplicationInstance()));
}

ibTempStorage::~ibTempStorage() = default;

wxString ibTempStorage::Create(const ibGuid& session, const wxString& name)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	const wxString fileName = wxFileNameFromPath(name);
	if (fileName.IsEmpty() || !session.isValid())
		return wxString();

	const wxString id = ibGuid(ibGuid::newGuid()).str();
	try {
		ibDatabaseQueryBuilder q(&m_holder);
		q.Execute(ibInsert(file_table, {
			{ wxT("partKey"),     ibConst(ibValue(PartKey(id, kHeaderPart))) },
			{ wxT("fileKey"),     ibConst(ibValue(id)) },
			{ wxT("partIndex"),   ibConst(ibValue(kHeaderPart)) },
			{ wxT("sessionGuid"), ibConst(ibValue(session.str())) },
			{ wxT("fileName"),    ibConst(ibValue(fileName)) },
			{ wxT("changed"),     ibConst(ibValue(ibDateTime::Now())) },
			{ wxT("dataSize"),    ibConst(ibValue(0)) },
		}));
		return id;
	}
	catch (...) {
		ibJournalWarning(wxT("file"), wxT("the temporary file '%s' was not made"), fileName);
		return wxString();
	}
}

wxString ibTempStorage::GetName(const ibGuid& session, const wxString& id) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ wxT("fileName") })
			.Where(ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("partIndex")), ibParam(0))))
			.Execute({ ibValue(kHeaderPart) });
		return rs.Next() ? wxString(rs.GetResultString(wxT("fileName"))) : wxString();
	}
	catch (...) {
		return wxString();
	}
}

bool ibTempStorage::WritePart(const ibGuid& session, const wxString& id, int index, const void* data,
	std::size_t size)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	if (index < 0 || size > kPartSize || GetName(session, id).IsEmpty())
		return false;

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		// The part's key is the primary key: a part written twice is refused by the table, not overwritten.
		q.Execute(ibInsert(file_table, {
			{ wxT("partKey"),     ibConst(ibValue(PartKey(id, index + 1))) },
			{ wxT("fileKey"),     ibConst(ibValue(id)) },
			{ wxT("partIndex"),   ibConst(ibValue(index + 1)) },
			{ wxT("sessionGuid"), ibConst(ibValue(session.str())) },
			{ wxT("changed"),     ibConst(ibValue(ibDateTime::Now())) },
			{ wxT("dataSize"),    ibConst(ibValue(static_cast<unsigned int>(size))) },
			{ wxT("binaryData"),  ibConstBlob(data, size) },
		}));
		return true;
	}
	catch (...) {
		ibJournalWarning(wxT("file"), wxT("part %d of the temporary file %s was not written"), index, id);
		return false;
	}
}

bool ibTempStorage::ReadPart(const ibGuid& session, const wxString& id, int index, wxMemoryBuffer& data) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	data.SetDataLen(0);
	if (index < 0)
		return false;

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ wxT("binaryData") })
			.Where(ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("partIndex")), ibParam(0))))
			.Execute({ ibValue(index + 1) });
		if (!rs.Next())
			return false;
		rs.GetResultBlob(wxT("binaryData"), data);
		return true;
	}
	catch (...) {
		return false;
	}
}

int ibTempStorage::PartCount(const ibGuid& session, const wxString& id) const
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		// The last part's number — the content parts are 1…n after the header.
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ wxT("partIndex") })
			.Where(IsSessionsFile(id, session))
			.OrderBy(wxT("partIndex"), ibQuerySortDir::Desc)
			.Limit(1)
			.Execute();
		return rs.Next() ? rs.GetResultInt(wxT("partIndex")) : 0;
	}
	catch (...) {
		return 0;
	}
}

bool ibTempStorage::Clear(const ibGuid& session, const wxString& id)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	try {
		ibDatabaseQueryBuilder q(&m_holder);
		q.Execute(ibDelete(file_table, ibBinOp(ibQueryBinOp::And, IsSessionsFile(id, session),
			ibBinOp(ibQueryBinOp::Gt, ibCol(wxT("partIndex")), ibConst(ibValue(kHeaderPart))))));
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
		q.Execute(ibDelete(file_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("sessionGuid")), ibConst(ibValue(session.str())))));
	}
	catch (...) {
		// Left for the sweep, which hands over who is alive and takes the rest.
	}
}

void ibTempStorage::SweepOrphans(const std::vector<ibGuid>& liveSessionGuids)
{
	std::lock_guard<std::recursive_mutex> serialised(m_mtx);

	std::set<wxString> live;
	for (const ibGuid& guid : liveSessionGuids)
		live.insert(guid.str());

	// The owners first, the deletes after — one per owner, through OnSessionEnd, as the lock manager does.
	std::set<wxString> orphans;
	try {
		ibDatabaseQueryBuilder q(&m_holder);
		ibQueryResult rs = q.From(file_table)
			.Select({ wxT("sessionGuid") })
			.Where(ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("partIndex")), ibParam(0)))
			.Execute({ ibValue(kHeaderPart) });
		while (rs.Next()) {
			const wxString owner = rs.GetResultString(wxT("sessionGuid"));
			if (!owner.IsEmpty() && live.find(owner) == live.end())
				orphans.insert(owner);
		}
	}
	catch (...) {
		return;   // transient DB error — the next sweep retries
	}

	for (const wxString& owner : orphans)
		OnSessionEnd(ibGuid(owner));
}

///////////////////////////////////////////////////////////////////////////////
//								ibTempFile
///////////////////////////////////////////////////////////////////////////////

bool ibTempFile::Open(const wxString& id)
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

bool ibTempFile::Create(const wxString& id, bool overwrite)
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

wxString ibTempFile::GetName() const
{
	return m_storage != nullptr ? m_storage->GetName(m_session, m_id) : wxString();
}

bool ibTempFile::ReadPart(int index, wxMemoryBuffer& data) const
{
	return m_storage != nullptr && m_storage->ReadPart(m_session, m_id, index, data);
}

bool ibTempFile::ReadAll(wxString* str, const wxMBConv& conv)
{
	if (m_storage == nullptr || str == nullptr)
		return false;

	wxMemoryBuffer content, part;
	for (int index = 0; ReadPart(index, part); ++index)
		content.AppendData(part.GetData(), part.GetDataLen());

	*str = wxString(static_cast<const char*>(content.GetData()), conv, content.GetDataLen());
	// A conversion that failed comes back empty — the file was not text in `conv`.
	return content.GetDataLen() == 0 || !str->IsEmpty();
}

bool ibTempFile::Write(const wxString& str, const wxMBConv& conv)
{
	const wxScopedCharBuffer bytes = str.mb_str(conv);
	if (!str.IsEmpty() && bytes.length() == 0)
		return false;   // `str` cannot be said in `conv`
	return Write(bytes.data(), bytes.length());
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
