#include "userInfo.h"

#include "appData.h"   // user_table macro
#include "databaseLayer/databaseQueryBuilder.h"   // L2 door — the whole sys_user DAO rides this (no raw ibDatabaseLayer / result set)
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
void FillFromRow(ibQueryResult& row, ibUserInfo& info)
{
	info.m_strUserGuid     = row.GetResultString(wxT("guid"));
	info.m_strUserName     = row.GetResultString(wxT("name"));
	info.m_strUserFullName = row.GetResultString(wxT("fullName"));

	wxMemoryBuffer buffer;
	row.GetResultBlob(wxT("binaryData"), buffer);
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

// Project exactly the columns FillFromRow consumes — guid / name / fullName / binaryData.
static ibQueryIR SysUserRowQuery(ibQueryExprPtr where)
{
	return ibQueryIR(ibProject(
		where ? ibFilter(ibScan(user_table), where) : ibScan(user_table),
		{ { ibCol(wxT("guid")),       wxEmptyString },
		  { ibCol(wxT("name")),       wxEmptyString },
		  { ibCol(wxT("fullName")),   wxEmptyString },
		  { ibCol(wxT("binaryData")), wxEmptyString } }));
}

ibUserInfo ibUserInfo::Read(const ibGuid& userGuid)
{
	ibUserInfo info;
	if (!userGuid.isValid())
		return info;

	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult result = q.ExecuteIR(SysUserRowQuery(
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("guid")), ibConst(ibValue(userGuid.str())))));
		if (result.Next())
			FillFromRow(result, info);
	}
	catch (...) { /* no table / passive scope — empty info */ }

	return info;
}

ibUserInfo ibUserInfo::Read(const wxString& userName)
{
	ibUserInfo info;
	if (userName.IsEmpty())
		return info;

	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult result = q.ExecuteIR(SysUserRowQuery(
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("name")), ibConst(ibValue(userName)))));
		if (result.Next())
			FillFromRow(result, info);
	}
	catch (...) { /* no table / passive scope — empty info */ }

	return info;
}

bool ibUserInfo::HasAny()
{
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult result = q.ExecuteIR(
			ibQueryIR(ibProject(ibScan(user_table), { { ibCol(wxT("name")), wxEmptyString } })));
		return result.Next();
	}
	catch (...) { return false; }
}

std::vector<ibUserInfo::Brief> ibUserInfo::ListAll()
{
	std::vector<Brief> list;
	try {
		ibDatabaseQueryBuilder q;
		ibQueryResult result = q.ExecuteIR(ibQueryIR(ibProject(ibScan(user_table),
			{ { ibCol(wxT("guid")),     wxEmptyString },
			  { ibCol(wxT("name")),     wxEmptyString },
			  { ibCol(wxT("fullName")), wxEmptyString } })));

		while (result.Next()) {
			Brief entry;
			entry.m_strUserGuid     = result.GetResultString(wxT("guid"));
			entry.m_strUserName     = result.GetResultString(wxT("name"));
			entry.m_strUserFullName = result.GetResultString(wxT("fullName"));
			list.emplace_back(std::move(entry));
		}
	}
	catch (...) { /* best-effort — empty list on failure */ }

	return list;
}

bool ibUserInfo::Save(const ibUserInfo& info)
{
	// The default-ctor builder resolves to CurrentHolder = the db_query channel's thread holder —
	// the dedicated channel for infra-level writes that must stay out of any user session's TX
	// (the pool never resolves a session holder here; session work passes its holder explicitly).
	ibWriterMemory writer;
	writer.w_chunk(eBlockPswd, WritePasswordChunk(info));
	writer.w_chunk(eBlockRole, WriteRoleChunk    (info));
	writer.w_chunk(eBlockLang, WriteLanguageChunk(info));

	// One UPSERT, match on guid — the door renders ON CONFLICT (PG/SQLite/MySQL) vs
	// UPDATE OR INSERT … MATCHING (FB), so the per-driver fork is gone. The blob binds via ibConstBlob.
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(user_table, {
			{ wxT("guid"),       ibConst(ibValue(info.m_strUserGuid)) },
			{ wxT("name"),       ibConst(ibValue(info.m_strUserName)) },
			{ wxT("fullName"),   ibConst(ibValue(info.m_strUserFullName)) },
			{ wxT("changed"),    ibConst(ibValue(wxDateTime::Now())) },
			{ wxT("dataSize"),   ibConst(ibValue(static_cast<unsigned int>(writer.size()))) },
			{ wxT("binaryData"), ibConstBlob(writer.pointer(), writer.size()) },
		}, { wxT("guid") }));
		return true;   // a real failure THROWS (caught below); the affected-row count is not an error
	}
	catch (...) { return false; }
}

bool ibUserInfo::Delete(const ibGuid& userGuid)
{
	if (!userGuid.isValid())
		return false;
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(user_table,
			ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("guid")), ibConst(ibValue(userGuid.str())))));
		return true;   // deleting an absent user (0 rows) is success; a real failure THROWS
	}
	catch (...) { return false; }
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
