// A restore used to drop the configuration tables and every user, then open
// the zip. An empty archive, a file that is not a zip, a missing or repeated
// member, data written before the configuration, or a configuration this
// build cannot read must leave the base as it was.
//
// The sentinel is a sys_user row. ClearTableUser is the clear that runs even
// when no metadata skeleton has been created, so a row that survives is a
// restore that returned before either clear.

#include <gtest/gtest.h>

#include <cstring>
#include <utility>
#include <vector>

#include <wx/init.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/userInfo.h"
#include "core/memory/memoryStream.h"

namespace {

ibMemoryBuffer Bytes(const char* text)
{
	return ibMemoryBuffer(text, std::strlen(text));
}

ibMemoryBuffer ConfigBlob(std::uint64_t sign, bool withBody)
{
	ibWriterMemory header;
	header.w_u64(sign);
	header.w_stringZ("cfg");

	ibWriterMemory all;
	all.w_chunk(ibMetaData::eHeaderBlock, header.pointer(), header.size());
	if (withBody) {
		ibWriterMemory body;
		body.w_u8(1);
		all.w_chunk(g_metaCommonMetadataCLSID, body.pointer(), body.size());
	}
	return all.buffer();
}

struct TempFile {
	ibString path;
	TempFile() : path(wxFileName::CreateTempFileName(wxT("oesbak"))) {}
	~TempFile()
	{
		if (!path.IsEmpty())
			wxRemoveFile(path);
	}
};

bool WriteArchive(const ibString& path, const std::vector<std::pair<ibString, ibMemoryBuffer>>& entries)
{
	wxFFileOutputStream out(path);
	if (!out.IsOk())
		return false;
	wxZipOutputStream zip(out);
	for (const auto& entry : entries) {
		if (!zip.PutNextEntry(entry.first))
			return false;
		if (entry.second.GetDataLen() > 0) {
			wxMemoryInputStream in(entry.second.GetData(), entry.second.GetDataLen());
			zip.Write(in);
		}
	}
	return zip.Close();
}

struct BackupRestore : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	bool ready = false;

	void SetUp() override
	{
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);

		ibDatabaseQueryBuilder q;
		q.Execute(ibCreateTable(user_table, {
			{ wxT("guid"),       ibTypeString(36),  false, true,  wxEmptyString },
			{ wxT("name"),       ibTypeString(64),  true,  false, wxEmptyString },
			{ wxT("fullName"),   ibTypeString(128), true,  false, wxEmptyString },
			{ wxT("changed"),    ibTypeDate(),      true,  false, wxEmptyString },
			{ wxT("dataSize"),   ibTypeInteger(),   true,  false, wxEmptyString },
			{ wxT("binaryData"), ibTypeBlob(),      true,  false, wxEmptyString },
		}));
		ready = true;
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	void KeepUser()
	{
		ibUserInfo info;
		info.m_strUserGuid = wxT("11111111-1111-1111-1111-111111111111");
		info.m_strUserName = wxT("keeper");
		info.m_strUserFullName = wxT("Keeper");
		ASSERT_TRUE(ibUserInfo::Save(info));
	}

	void ExpectUserKept()
	{
		const ibUserInfo again = ibUserInfo::Read(wxT("keeper"));
		EXPECT_TRUE(again.IsOk());
		EXPECT_EQ(again.m_strUserName, wxT("keeper"));
	}
};

TEST_F(BackupRestore, EmptyArchiveLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_FALSE(file.path.IsEmpty());
	ASSERT_TRUE(WriteArchive(file.path, {}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, PlainFileLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_FALSE(file.path.IsEmpty());
	{
		wxFFileOutputStream out(file.path);
		ASSERT_TRUE(out.IsOk());
		const char text[] = "not a zip";
		out.Write(text, sizeof(text) - 1);
	}
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, MissingMemberLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_TRUE(WriteArchive(file.path, {
		{ wxT("user"), Bytes("user") },
		{ wxT("data"), Bytes("data") },
	}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, UnreadableConfigLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_TRUE(WriteArchive(file.path, {
		{ wxT("config"), Bytes("not-a-config") },
		{ wxT("user"), ibMemoryBuffer() },
		{ wxT("data"), ibMemoryBuffer() },
	}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, DataBeforeConfigLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_TRUE(WriteArchive(file.path, {
		{ wxT("data"), ibMemoryBuffer() },
		{ wxT("config"), ConfigBlob(sign_metadata, true) },
		{ wxT("user"), ibMemoryBuffer() },
	}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, RepeatedConfigLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_TRUE(WriteArchive(file.path, {
		{ wxT("config"), ConfigBlob(sign_metadata, true) },
		{ wxT("config"), ConfigBlob(sign_metadata, true) },
		{ wxT("user"), ibMemoryBuffer() },
		{ wxT("data"), ibMemoryBuffer() },
	}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

TEST_F(BackupRestore, ReadableArchiveWithoutMetadataLeavesTheUser)
{
	if (!ready) GTEST_SKIP();
	KeepUser();
	TempFile file;
	ASSERT_TRUE(WriteArchive(file.path, {
		{ wxT("config"), ConfigBlob(sign_metadata, true) },
		{ wxT("user"), ibMemoryBuffer() },
		{ wxT("data"), ibMemoryBuffer() },
	}));
	EXPECT_FALSE(appData->LoadDatabase(file.path));
	ExpectUserKept();
}

} // namespace
