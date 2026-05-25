#include "userInfo.h"

#include "appData.h"   // db_query macro, user_table
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "fileSystem/fs.h"
#include "guid.h"

namespace {

// Chunk tags inside binaryData (DB) and the per-record blob (export buffer).
// Same numeric values for both — the on-disk format is shared so import
// from old configurations keeps working.
constexpr unsigned int eBlockPswd = 0x0234530;
constexpr unsigned int eBlockRole = 0x0234540;
constexpr unsigned int eBlockLang = 0x0234550;

void ReadPasswordChunk(const wxMemoryBuffer& buffer, ibUserInfo& info)
{
	ibReaderMemory reader(buffer);
	// Atomic-on-success: a malformed chunk (length header corrupt past
	// field N) used to assign N-1 garbage strings into `info` before
	// the throw — the partial state defeated FillFromRow's own
	// chunk-level try/catch (info already had a non-empty password
	// from chunk garbage, so Verify against the user-typed password
	// failed even for users created without a password).  Read into
	// locals first, commit only after all four reads succeed.
	wxString g = reader.r_stringZ();
	wxString n = reader.r_stringZ();
	wxString f = reader.r_stringZ();
	wxString p = reader.r_stringZ();
	info.m_strUserGuid     = std::move(g);
	info.m_strUserName     = std::move(n);
	info.m_strUserFullName = std::move(f);
	info.m_strUserPassword = std::move(p);
}

void ReadRoleChunk(const wxMemoryBuffer& buffer, ibUserInfo& info)
{
	ibReaderMemory reader(buffer);
	const unsigned int count = reader.r_u32();
	info.m_roleArray.reserve(count);
	for (unsigned int idx = 0; idx < count; idx++) {
		ibUserInfo::ibUserRole entry;
		// Read defensively — a sys_user row written before a metadata
		// version that removed the role's class will have stale guid /
		// truncated payload. Catch reader exceptions per-entry so one
		// bad role doesn't brick the whole sys_user Read (and therefore
		// the login flow).  Subsequent loop turns try the next role;
		// downstream code resolves roles from m_roleArray, so unknown
		// guids just degrade to "no access" instead of throwing.
		try {
			entry.m_strRoleGuid = reader.r_stringZ();
			entry.m_miRoleId    = reader.r_s32();
			info.m_roleArray.emplace_back(std::move(entry));
		} catch (...) {
			// Buffer truncated or malformed past this point — stop
			// reading roles; whatever was already collected stays.
			break;
		}
	}
}

void ReadLanguageChunk(const wxMemoryBuffer& buffer, ibUserInfo& info)
{
	ibReaderMemory reader(buffer);
	wxString g = reader.r_stringZ();
	wxString c = reader.r_stringZ();
	info.m_strLanguageGuid = std::move(g);
	info.m_strLanguageCode = std::move(c);
}

wxMemoryBuffer WritePasswordChunk(const ibUserInfo& info)
{
	ibWriterMemory writer;
	writer.w_stringZ(info.m_strUserGuid);
	writer.w_stringZ(info.m_strUserName);
	writer.w_stringZ(info.m_strUserFullName);
	writer.w_stringZ(info.m_strUserPassword);
	return writer.buffer();
}

wxMemoryBuffer WriteRoleChunk(const ibUserInfo& info)
{
	ibWriterMemory writer;
	writer.w_u32(info.m_roleArray.size());
	for (const auto& role : info.m_roleArray) {
		writer.w_stringZ(role.m_strRoleGuid);
		writer.w_s32(role.m_miRoleId);
	}
	return writer.buffer();
}

wxMemoryBuffer WriteLanguageChunk(const ibUserInfo& info)
{
	ibWriterMemory writer;
	writer.w_stringZ(info.m_strLanguageGuid);
	writer.w_stringZ(info.m_strLanguageCode);
	return writer.buffer();
}

// Common: populate identity columns from a sys_user result row, then
// crack open the binaryData blob into the same chunks Serialize/Deserialize
// use. Caller has already advanced the cursor onto the row of interest.
void FillFromRow(ibDatabaseResultSet* row, ibUserInfo& info)
{
	info.m_strUserGuid     = row->GetResultString(wxT("guid"));
	info.m_strUserName     = row->GetResultString(wxT("name"));
	info.m_strUserFullName = row->GetResultString(wxT("fullName"));

	wxMemoryBuffer buffer;
	row->GetResultBlob(wxT("binaryData"), buffer);
	ibReaderMemory reader(buffer);

	wxMemoryBuffer chunk;
	// Each chunk reader is wrapped so a malformed / version-mismatched
	// section (e.g. role chunk for a metadata version that has moved on)
	// doesn't bring down the whole sys_user Read — login still gets
	// identity + password and the affected chunk's fields stay default.
	try { if (reader.r_chunk(eBlockPswd, chunk)) ReadPasswordChunk(chunk, info); } catch (...) {}
	try { if (reader.r_chunk(eBlockRole, chunk)) ReadRoleChunk    (chunk, info); } catch (...) {}
	try { if (reader.r_chunk(eBlockLang, chunk)) ReadLanguageChunk(chunk, info); } catch (...) {}
}

} // namespace

ibUserInfo ibUserInfo::Read(const ibGuid& userGuid)
{
	ibUserInfo info;
	if (!userGuid.isValid())
		return info;

	ibStatementGuard stmt(db_query,
		db_query->PrepareStatement(wxT("SELECT * FROM %s WHERE guid = ?;"), user_table));
	if (stmt)
		stmt->SetParamString(1, userGuid.str());

	ibResultSetGuard result(db_query,
		stmt ? stmt->RunQueryWithResults() : nullptr);

	if (result && result->Next())
		FillFromRow(result.get(), info);

	return info;
}

ibUserInfo ibUserInfo::Read(const wxString& userName)
{
	ibUserInfo info;
	if (userName.IsEmpty())
		return info;

	ibStatementGuard stmt(db_query,
		db_query->PrepareStatement(wxT("SELECT * FROM %s WHERE name = ?;"), user_table));
	if (stmt)
		stmt->SetParamString(1, userName);

	ibResultSetGuard result(db_query,
		stmt ? stmt->RunQueryWithResults() : nullptr);

	if (result && result->Next())
		FillFromRow(result.get(), info);

	return info;
}

bool ibUserInfo::HasAny()
{
	ibResultSetGuard result(db_query,
		db_query->RunQueryWithResults(wxT("SELECT name FROM %s;"), user_table));

	if (!result) return false;
	return result->Next();
}

std::vector<ibUserInfo::Brief> ibUserInfo::ListAll()
{
	ibResultSetGuard result(db_query,
		db_query->RunQueryWithResults(wxT("SELECT guid, name, fullName FROM %s;"), user_table));

	std::vector<Brief> list;
	if (!result)
		return list;

	while (result->Next()) {
		Brief entry;
		entry.m_strUserGuid     = result->GetResultString(wxT("guid"));
		entry.m_strUserName     = result->GetResultString(wxT("name"));
		entry.m_strUserFullName = result->GetResultString(wxT("fullName"));
		list.emplace_back(std::move(entry));
	}

	return list;
}

bool ibUserInfo::Save(const ibUserInfo& info)
{
	// db_query routes through the process-wide singleton holder —
	// the dedicated channel for DDL and infra-level writes that must
	// stay out of any user session's TX.
	ibStatementGuard stmt(db_query,
		db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD
			? db_query->PrepareStatement(
				wxT("INSERT INTO %s (guid, name, fullName, changed, dataSize, binaryData) VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT (guid) DO UPDATE SET guid = excluded.guid, name = excluded.name, fullName = excluded.fullName, changed = excluded.changed, dataSize = excluded.dataSize, binaryData = excluded.binaryData; "), user_table)
			: db_query->PrepareStatement(
				wxT("UPDATE OR INSERT INTO %s (guid, name, fullName, changed, dataSize, binaryData) VALUES(?, ?, ?, ?, ?, ?) MATCHING (guid);"), user_table)
	);

	if (!stmt)
		return false;

	stmt->SetParamString(1, info.m_strUserGuid);
	stmt->SetParamString(2, info.m_strUserName);
	stmt->SetParamString(3, info.m_strUserFullName);
	stmt->SetParamDate  (4, wxDateTime::Now());

	ibWriterMemory writer;
	writer.w_chunk(eBlockPswd, WritePasswordChunk(info));
	writer.w_chunk(eBlockRole, WriteRoleChunk    (info));
	writer.w_chunk(eBlockLang, WriteLanguageChunk(info));

	stmt->SetParamNumber(5, writer.size());
	stmt->SetParamBlob  (6, writer.pointer(), writer.size());

	const int result = stmt->RunQuery();
	return result != DATABASE_LAYER_QUERY_RESULT_ERROR;
}

void ibUserInfo::Serialize(ibWriterMemory& writer) const
{
	writer.w_stringZ(m_strUserGuid);
	writer.w_stringZ(m_strUserName);
	writer.w_stringZ(m_strUserFullName);
	writer.w_chunk(eBlockPswd, WritePasswordChunk(*this));
	writer.w_chunk(eBlockRole, WriteRoleChunk    (*this));
	writer.w_chunk(eBlockLang, WriteLanguageChunk(*this));
}

ibUserInfo ibUserInfo::Deserialize(ibReaderMemory& reader)
{
	ibUserInfo info;
	info.m_strUserGuid     = reader.r_stringZ();
	info.m_strUserName     = reader.r_stringZ();
	info.m_strUserFullName = reader.r_stringZ();

	wxMemoryBuffer chunk;
	if (reader.r_chunk(eBlockPswd, chunk)) ReadPasswordChunk(chunk, info);
	if (reader.r_chunk(eBlockRole, chunk)) ReadRoleChunk    (chunk, info);
	if (reader.r_chunk(eBlockLang, chunk)) ReadLanguageChunk(chunk, info);
	return info;
}
