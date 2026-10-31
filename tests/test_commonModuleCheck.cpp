// config_check asks whether the common modules compile, beside the questions
// it already asked. The answer is a problem whose diagnostics are the
// compiler's, and a module that was never compiled is still a problem:
// checked false, with an empty diagnostics list, is not a clean module.
//
// The configuration is the one a designer session opens. The compile cache
// is the one that session builds. Nothing here substitutes its own.

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/mcp/mcpTool.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

namespace {

struct CommonModuleCheck : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> m_db;
	bool m_ownsApp = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (ibApplicationInstance::Get() == nullptr) {
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				GTEST_SKIP() << "appData env unavailable headless";
			m_ownsApp = true;
		}
		// The configuration the designer opens lives in the base. An
		// in-memory SQLite is that base for this test.
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!m_db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(m_db, /*maxSize=*/1, /*minIdle=*/0);
		// …and the configuration's tables, which a base's opening makes (ibApplicationInstance::Open) and this base
		// never went through.
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
	}

	void TearDown() override {
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

struct CodeStyleRestore {
	short m_saved = ibCompileCode::GetCodeStyle();
	~CodeStyleRestore() { ibCompileCode::SetCodeStyle(m_saved); }
};

} // namespace

TEST_F(CommonModuleCheck, ConfigCheckReportsABrokenModuleAndOneThatWasNeverCompiled)
{
	CodeStyleRestore style;
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	// The base's session, unlisted: this base has no system tables to list it in. A session of no base makes a
	// configuration of none. A session on the stack is not current: the scope keeps a weak pointer, and a designer
	// that is not current builds no compile cache.
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("module-check"), ibSessionKind::Designer));
	ibSession* const session = holder.Get();
	ASSERT_NE(session, nullptr);
	ibSessionScope scope(session);

	// The configuration a designer edits is its session's own, made as the
	// session is let in (NotifyAuthenticated → ibSession::AcquireMetaData) and
	// let go as it leaves. The base's active one is not it.
	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{ registry, session };

	ibMetaDataConfigurationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	ASSERT_EQ(activeMetaData, cfg);
	if (cfg->IsConfigOpen()) {
		ASSERT_TRUE(cfg->CloseDatabase(forceCloseFlag));
	}

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	auto* clean = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	auto* broken = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	ASSERT_NE(clean, nullptr);
	ASSERT_NE(broken, nullptr);
	clean->SetModuleText(wxT("Procedure Clean()\nvar a;\na = 1;\nEndProcedure\n"));
	broken->SetModuleText(wxT("Procedure Broken()\nvar a;\na = Nope;\nEndProcedure\n"));

	ASSERT_TRUE(cfg->RunDatabase());

	auto* never = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, false));
	ASSERT_NE(never, nullptr);
	never->SetModuleText(wxT("Procedure Never()\nvar a;\na = 1;\nEndProcedure\n"));

	// RunDatabase pins the configuration's own syntax, which is CES. The
	// modules are written with Procedure/EndProcedure, and under CES those
	// words are identifiers.
	ibCompileCode::SetCodeStyle(CODE_VES);

	const ibMcpTool* check = ibFindMcpTool(wxT("config_check"));
	ASSERT_NE(check, nullptr);
	ibDataNode params;
	ibDataNode result;
	ibString refusal;
	ASSERT_TRUE(check->Call(params, result, refusal)) << refusal.ToStdString();

	std::optional<ibDataValue> brokenProblem;
	std::optional<ibDataValue> neverProblem;
	auto problems = result.FindField(ibT("problems"));
	ASSERT_TRUE(problems);
	ASSERT_EQ(problems->Kind(), ibDataKind::Array);
	for (const ibDataValue& one : problems->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;
		const ibString text = one.AsChild()->GetValue<ibString>(ibT("name"))
			+ one.AsChild()->GetValue<ibString>(ibT("says"));
		auto diagnostics = one.AsChild()->FindField(ibT("diagnostics"));
		if (diagnostics && diagnostics->Kind() == ibDataKind::Array) {
			for (const ibDataValue& line : diagnostics->AsArray()) {
				if (line.Kind() == ibDataKind::Child && line.AsChild()
					&& line.AsChild()->GetValue<ibString>(ibT("message")).Contains(wxT("Nope")))
					brokenProblem = one;
			}
		}
		if (one.AsChild()->GetValue<ibString>(ibT("name")) == never->GetName()
			|| text.Contains(never->GetName()))
			neverProblem = one;
		EXPECT_FALSE(text.Contains(clean->GetName()) && one.AsChild()->Has(ibT("diagnostics"))
			&& one.AsChild()->GetValue<ibString>(ibT("says")).Contains(wxT("does not compile")));
	}

	ASSERT_TRUE(brokenProblem);
	auto diagnostics = brokenProblem->AsChild()->FindField(ibT("diagnostics"));
	ASSERT_TRUE(diagnostics);
	EXPECT_EQ(diagnostics->Kind(), ibDataKind::Array);
	EXPECT_FALSE(diagnostics->AsArray().empty());
	EXPECT_TRUE(brokenProblem->AsChild()->GetValue<bool>(ibT("checked")));

	ASSERT_TRUE(neverProblem);
	EXPECT_FALSE(neverProblem->AsChild()->GetValue<bool>(ibT("checked")));
	auto empty = neverProblem->AsChild()->FindField(ibT("diagnostics"));
	ASSERT_TRUE(empty);
	EXPECT_TRUE(empty->Kind() == ibDataKind::Array && empty->AsArray().empty());
}
