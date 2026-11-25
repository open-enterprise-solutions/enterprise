// An account holds its roles and its language by id and guid: sys_user keeps no names, and the
// application may rename what it points at. What an assistant reads about the person is asked of
// the application of the session it is asked in. Taken from the account, every role read "".
//
// A role with no right to the data refusing a tool is not here: the refusal stands behind chat_say,
// and chat_say reaches its server through a client host, which this test does not have.

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataApplication.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/userInfo.h"
#include "core/clsid.h"
#include "core/serialize/dataBuilder.h"
#include "core/serialize/provider/jsonProvider.h"
#include "frmserver/mcp/mcpServer.h"
#include "frmserver/mcp/mcpTool.h"

#include "activeMetaDataAccess.h"

namespace {

const wxChar* const kInitialize = wxT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{")
	wxT("\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1\"}}}");

struct McpPersonNames : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> m_db;
	bool m_ownsApp = false;
	// RunDatabase sets this thread's code style to the application's.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();

	ibSessionHolder m_holder;
	std::optional<ibSessionScope> m_scope;

	ibRoleID m_clerk = wxNOT_FOUND;
	ibRoleID m_auditor = wxNOT_FOUND;
	ibString m_languageGuid;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (ibApplicationInstance::Get() == nullptr) {
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				GTEST_SKIP() << "appData env unavailable headless";
			m_ownsApp = true;
		}
		// The application a studio opens lives in the base. An in-memory SQLite is that base here.
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!m_db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(m_db, /*maxSize=*/1, /*minIdle=*/0);
		ibMetaDataApplicationStorage::CreateApplicationTable();
		ibMetaDataApplicationStorage::CreateApplicationSaveTable();
		ibMetaDataApplicationStorage::CreateApplicationSequence();
	}

	void TearDown() override {
		if (m_holder)
			ibApplicationInstance::GetSessionRegistry()->NotifyDisconnect(m_holder.Get());
		m_scope.reset();
		m_holder.Reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	void Fill(ibMetaDataApplicationBase* cfg) {
		ibValueMetaObjectApplication* root = cfg->GetRootMetaObject();
		ASSERT_NE(root, nullptr);
		ibValueMetaObject* clerk = cfg->CreateMetaObject(g_metaRoleCLSID, root, false);
		ibValueMetaObject* auditor = cfg->CreateMetaObject(g_metaRoleCLSID, root, false);
		ibValueMetaObject* language = cfg->CreateMetaObject(g_metaLanguageCLSID, root, false);
		ASSERT_NE(clerk, nullptr);
		ASSERT_NE(auditor, nullptr);
		ASSERT_NE(language, nullptr);
		ASSERT_TRUE(cfg->RenameMetaObject(clerk, wxT("Clerk")));
		ASSERT_TRUE(cfg->RenameMetaObject(auditor, wxT("Auditor")));
		ASSERT_TRUE(cfg->RenameMetaObject(language, wxT("Ukrainian")));
		m_clerk = clerk->GetMetaID();
		m_auditor = auditor->GetMetaID();
		m_languageGuid = language->GetDocPath();
	}

	// A session of no base makes an application of none, so it is minted as the base's, unlisted: this base has no
	// system tables. On the stack it would not be current: the scope keeps a weak pointer.
	void LetIn(ibSessionKind kind) {
		ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
		ASSERT_NE(registry, nullptr);
		m_holder = registry->MintUnlisted(std::make_shared<ibSession>(wxT("person-names"), kind));
		ASSERT_NE(m_holder.Get(), nullptr);
		m_scope.emplace(m_holder.Get());
		registry->NotifyAuthenticated(m_holder.Get());
	}

	// A session of the running application takes the base's active application.
	void EnterApplication() {
		std::shared_ptr<ibMetaDataApplicationFile> cfg = ibMetaData::MakeShared<ibMetaDataApplicationFile>();
		ActiveMetaDataAccess::Replace(cfg);
		ASSERT_NO_FATAL_FAILURE(Fill(cfg.get()));
		ASSERT_TRUE(cfg->RunDatabase());
		ASSERT_NO_FATAL_FAILURE(LetIn(ibSessionKind::Workspace));
		ASSERT_EQ(m_holder->GetMetaData(), cfg.get());
	}

	// A studio's session makes its own as it is let in.
	void EnterStudio() {
		ASSERT_NO_FATAL_FAILURE(LetIn(ibSessionKind::Studio));
		ibMetaDataApplicationBase* const cfg = m_holder->GetMetaData();
		ASSERT_NE(cfg, nullptr);
		if (cfg->IsOpen()) {
			ASSERT_TRUE(cfg->CloseDatabase(ibRuntimeRequest(forceCloseFlag)));
		}
		ASSERT_NO_FATAL_FAILURE(Fill(cfg));
		ASSERT_TRUE(cfg->RunDatabase());
	}

	// As the base reads an account: ids, the guid and the code, and no name of a role or a language.
	ibUserInfo Account() const {
		ibUserInfo who;
		who.m_strUserGuid = wxT("11111111-1111-1111-1111-111111111111");
		who.m_strUserName = wxT("clerk");

		ibUserInfo::ibUserRole clerk;
		clerk.m_miRoleId = m_clerk;
		who.m_roleArray.push_back(clerk);

		ibUserInfo::ibUserRole auditor;
		auditor.m_miRoleId = m_auditor;
		auditor.m_mode = ibRoleCompositionMode_Intersection;
		who.m_roleArray.push_back(auditor);

		who.m_strLanguageGuid = m_languageGuid;
		who.m_strLanguageCode = wxT("uk");
		return who;
	}

	void LogIn(const ibUserInfo& who) {
		ibApplicationInstance::GetSessionRegistry()->InstallUser(m_holder.Get(), who, wxEmptyString);
	}

	ibString Greeting() {
		ibMcpServer server(ib::ClientHostCtorToken{ ibApplicationInstance::Get() }, m_holder.Get());
		std::int32_t status = 0;
		const ibString answer = server.Call(kInitialize, wxEmptyString, wxEmptyString, wxEmptyString, status);
		EXPECT_EQ(status, 200);
		return answer;
	}

	// What the handshake tells a client, out of the answer's JSON.
	ibString Instructions() {
		ibDataNode answer;
		EXPECT_TRUE(ibJsonProvider().Read(Greeting(), answer));
		const auto result = answer.FindChild(ibT("result"));
		EXPECT_TRUE(result);
		return result ? result->GetValue<ibString>(ibT("instructions")) : ibString();
	}
};

// A client keeps only the start of the instructions - Claude Code the first 2048 characters.
const size_t kInstructionsKept = 2048;

} // namespace

TEST_F(McpPersonNames, ARole_IsCalledWhatTheApplicationCallsIt)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());

	const std::vector<ibString> names = ibMcpRoleNames(Account());

	ASSERT_EQ(names.size(), 2u);
	EXPECT_EQ(names[0].ToStdString(), "Clerk");
	EXPECT_EQ(names[1].ToStdString(), "Auditor (restricting)");
}

TEST_F(McpPersonNames, ARoleTheApplicationNoLongerHas_IsSaidToBeGone)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());

	ibUserInfo who;
	ibUserInfo::ibUserRole gone;
	gone.m_miRoleId = 99999;
	who.m_roleArray.push_back(gone);

	const std::vector<ibString> names = ibMcpRoleNames(who);

	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0].ToStdString(), "role 99999, no longer in the application");
}

TEST_F(McpPersonNames, ALanguage_IsCalledWhatTheApplicationCallsIt)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());

	EXPECT_EQ(ibMcpLanguageName(Account()).ToStdString(), "Ukrainian");
}

TEST_F(McpPersonNames, ALanguageTheApplicationNoLongerHas_IsItsCode)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());

	ibUserInfo who = Account();
	who.m_strLanguageGuid = wxT("22222222-2222-2222-2222-222222222222");

	EXPECT_EQ(ibMcpLanguageName(who).ToStdString(), "uk");
}

TEST_F(McpPersonNames, PlatformState_SaysThem)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());
	LogIn(Account());

	ibDataNode state;
	ibMcpDescribePlatform(ibApplicationInstance::Get(), state);

	const auto user = state.FindChild(ibT("user"));
	ASSERT_TRUE(user);
	EXPECT_EQ(user->GetValue<ibString>(ibT("language")).ToStdString(), "Ukrainian");

	const auto roles = user->FindField(ibT("roles"));
	ASSERT_TRUE(roles);
	ASSERT_EQ(roles->Kind(), ibDataKind::Array);
	std::vector<std::string> names;
	for (const ibDataValue& role : roles->AsArray())
		names.push_back(role.AsString().ToStdString());
	EXPECT_EQ(names, (std::vector<std::string>{ "Clerk", "Auditor (restricting)" }));
}

TEST_F(McpPersonNames, TheApplicationsGreeting_SaysThem)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());
	LogIn(Account());

	const ibString greeting = Greeting();

	EXPECT_TRUE(greeting.Contains(wxT("Roles: Clerk, Auditor (restricting)."))) << greeting.ToStdString();
	EXPECT_TRUE(greeting.Contains(wxT("The person is working in Ukrainian"))) << greeting.ToStdString();
}

TEST_F(McpPersonNames, TheStudiosGreeting_SaysThem)
{
	ASSERT_NO_FATAL_FAILURE(EnterStudio());
	LogIn(Account());

	const ibString greeting = Greeting();

	EXPECT_TRUE(greeting.Contains(wxT("Roles: Clerk, Auditor (restricting)."))) << greeting.ToStdString();
	EXPECT_TRUE(greeting.Contains(wxT("The person is working in Ukrainian"))) << greeting.ToStdString();
}

TEST_F(McpPersonNames, AGreeting_NamesNoLanguageForAPersonWithoutOne)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());
	ibUserInfo who = Account();
	who.m_strLanguageGuid = ibString();
	who.m_strLanguageCode = ibString();
	LogIn(who);

	EXPECT_FALSE(Greeting().Contains(wxT("The person is working in")));
}

// The studio's text ends on where the rest is, and the whole of it is kept.
TEST_F(McpPersonNames, TheStudiosInstructions_FitWhatAClientKeeps)
{
	ASSERT_NO_FATAL_FAILURE(EnterStudio());
	LogIn(Account());

	const ibString instructions = Instructions();

	EXPECT_LE(instructions.length(), kInstructionsKept) << instructions.ToStdString();
	EXPECT_TRUE(instructions.Contains(wxT("\"where-to-start\""))) << instructions.ToStdString();
}

// The application's text may run past it only by the note written on its root, said last; this root has none.
TEST_F(McpPersonNames, TheApplicationsInstructions_FitWhatAClientKeeps)
{
	ASSERT_NO_FATAL_FAILURE(EnterApplication());
	LogIn(Account());

	const ibString instructions = Instructions();

	EXPECT_LE(instructions.length(), kInstructionsKept) << instructions.ToStdString();
}
