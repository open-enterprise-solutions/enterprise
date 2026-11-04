// =============================================================================
// THE BREAKPOINTS' ONE MAP: the debugger client of the designer's session (backend/debugger/debugClient.cpp). A
// breakpoint is set at an editor line. The edits that come with the module's text move it (PatchModule). The save
// writes the map into the base's sys_dbg and reads it back from there (SaveModule). The next designer draws what the
// base keeps.
//
// 2026-11-04: the save moved its rows one by one, in ascending order. 5→7 wrote over the row at 7, and 7→9 then
// took it out: of two breakpoints moved by two lines, the base kept one.
// =============================================================================

#include <gtest/gtest.h>

#include <map>
#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/debugger/debugClient.h"

// A debugger client with no configuration around it: its breakpoints and their store, and no runtime. With no
// connection, what it would tell a runtime goes nowhere (SendCommand). In production only
// ibMetaDataConfigurationStorage makes one.
class ibStandaloneDebuggerClient {
public:
	static std::unique_ptr<ibDebuggerClient> Make() {
		return std::unique_ptr<ibDebuggerClient>(new ibDebuggerClient(nullptr));
	}
};

namespace {

using ibLines = std::map<unsigned int, ibString>;   // editor line → condition

const wxChar* const s_module = wxT("ae1f2c8e-0000-4000-8000-000000000001");   // a module's address
const unsigned int s_lineCount = 20;

struct DebugClientFix : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;

	void SetUp() override
	{
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	}

	void TearDown() override
	{
		if (ibApplicationInstance::Get(/*required*/ false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// The module opened, as a designer opens it (OnBeforeRunMetaObject): its lines, and its breakpoints read from the
	// base.
	static std::unique_ptr<ibDebuggerClient> Open()
	{
		std::unique_ptr<ibDebuggerClient> client = ibStandaloneDebuggerClient::Make();
		client->InitializeModule(s_module, s_lineCount);
		return client;
	}

	// A breakpoint set at an editor line, with its condition. A refusal says why.
	static void Set(ibDebuggerClient& client, unsigned int line, const ibString& condition)
	{
		ibString refusal;
		ASSERT_TRUE(client.ToggleBreakpoint(s_module, line, &refusal, condition)) << refusal.ToUtf8();
	}
};

} // namespace

// Two lines inserted above two breakpoints: both move by two, and both are kept in the base, each with its own
// condition. The one above the edit stays where it was.
TEST_F(DebugClientFix, SaveKeepsEveryBreakpointTheEditMoved)
{
	std::unique_ptr<ibDebuggerClient> client = Open();
	Set(*client, 1, wxT("above"));
	Set(*client, 5, wxT("first"));
	Set(*client, 7, wxT("second"));

	client->PatchModule(s_module, 3, 2, /*atLineStart*/ true);
	const ibLines moved = { { 1, wxT("above") }, { 7, wxT("first") }, { 9, wxT("second") } };
	EXPECT_EQ(client->GetDebugList(s_module), moved);

	ASSERT_TRUE(client->SaveModule(s_module, s_lineCount + 2));
	EXPECT_EQ(client->GetDebugList(s_module), moved) << "read back from the base, as the save leaves it";
}

// Two lines deleted above two breakpoints: both move up by two.
TEST_F(DebugClientFix, SaveKeepsEveryBreakpointADeletionMoved)
{
	std::unique_ptr<ibDebuggerClient> client = Open();
	Set(*client, 5, wxT("first"));
	Set(*client, 9, wxT("second"));

	client->PatchModule(s_module, 2, -2, /*atLineStart*/ false);
	const ibLines moved = { { 3, wxT("first") }, { 7, wxT("second") } };
	EXPECT_EQ(client->GetDebugList(s_module), moved);

	ASSERT_TRUE(client->SaveModule(s_module, s_lineCount - 2));
	EXPECT_EQ(client->GetDebugList(s_module), moved);
}

// What the save wrote is what the next designer opening the module draws.
TEST_F(DebugClientFix, TheNextDesignerDrawsWhatTheSaveWrote)
{
	{
		std::unique_ptr<ibDebuggerClient> client = Open();
		Set(*client, 5, wxT("first"));
		Set(*client, 7, wxT("second"));
		client->PatchModule(s_module, 0, 2, /*atLineStart*/ true);
		ASSERT_TRUE(client->SaveModule(s_module, s_lineCount + 2));
	}

	std::unique_ptr<ibDebuggerClient> next = Open();
	const ibLines saved = { { 7, wxT("first") }, { 9, wxT("second") } };
	EXPECT_EQ(next->GetDebugList(s_module), saved);
}

// A breakpoint is removed at the editor line it moved to: the client finds the committed line behind it.
TEST_F(DebugClientFix, RemoveFindsTheBreakpointAtItsEditorLine)
{
	std::unique_ptr<ibDebuggerClient> client = Open();
	Set(*client, 5, wxT("first"));
	Set(*client, 7, wxT("second"));
	client->PatchModule(s_module, 0, 2, /*atLineStart*/ true);

	ASSERT_TRUE(client->RemoveBreakpoint(s_module, 7));   // the one set at 5
	const ibLines left = { { 9, wxT("second") } };
	EXPECT_EQ(client->GetDebugList(s_module), left);

	ASSERT_TRUE(client->SaveModule(s_module, s_lineCount + 2));
	EXPECT_EQ(client->GetDebugList(s_module), left);
}
