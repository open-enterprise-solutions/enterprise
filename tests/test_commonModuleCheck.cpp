// config_check asks whether the common modules compile, beside the questions
// it already asked. The answer is a problem whose diagnostics are the
// compiler's, and a module that was never compiled is still a problem:
// checked false, with an empty diagnostics list, is not a clean module.
//
// The configuration is the one a designer session opens. The compile cache
// is the one that session builds. Nothing here substitutes its own.

#include <gtest/gtest.h>

#include <memory>
#include <filesystem>
#include "core/fileSystem.h"
#include "backend/metaCollection/metaDiff.h"

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
	std::filesystem::path m_databasePath = std::filesystem::temp_directory_path()
		/ ("oes-module-check-" + ibGuid(ibGuid::newGuid()).str().ToStdString() + ".db");
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
		if (!m_db->Open(wxString::FromUTF8(m_databasePath.u8string())))
			GTEST_SKIP() << "SQLite open failed";
		// A designer holds one connection while login reads MCP settings through
		// another holder. Clones must see the same configuration tables.
		pool->Init(m_db, /*maxSize=*/4, /*minIdle=*/0);
		// …and the configuration's tables, which a base's opening makes (ibApplicationInstance::Open) and this base
		// never went through.
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
	}

	void TearDown() override {
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		else if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::GetConnectionPool()->Shutdown();
		m_db.reset();
		std::error_code error;
		std::filesystem::remove(m_databasePath, error);
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
	wxString refusal;
	ASSERT_TRUE(check->Call(params, result, refusal)) << refusal.ToStdString();

	const ibDataValue* brokenProblem = nullptr;
	const ibDataValue* neverProblem = nullptr;
	const ibDataValue* problems = result.FindField(wxT("problems"));
	ASSERT_NE(problems, nullptr);
	ASSERT_EQ(problems->Kind(), ibDataKind::Array);
	for (const ibDataValue& one : problems->AsArray()) {
		if (one.Kind() != ibDataKind::Child || !one.AsChild())
			continue;
		const wxString text = one.AsChild()->GetValue<wxString>(wxT("name"))
			+ one.AsChild()->GetValue<wxString>(wxT("says"));
		const ibDataValue* diagnostics = one.AsChild()->FindField(wxT("diagnostics"));
		if (diagnostics != nullptr && diagnostics->Kind() == ibDataKind::Array) {
			for (const ibDataValue& line : diagnostics->AsArray()) {
				if (line.Kind() == ibDataKind::Child && line.AsChild()
					&& line.AsChild()->GetValue<wxString>(wxT("message")).Contains(wxT("Nope")))
					brokenProblem = &one;
			}
		}
		if (one.AsChild()->GetValue<wxString>(wxT("name")) == never->GetName()
			|| text.Contains(never->GetName()))
			neverProblem = &one;
		EXPECT_FALSE(text.Contains(clean->GetName()) && one.AsChild()->FindField(wxT("diagnostics")) != nullptr
			&& one.AsChild()->GetValue<wxString>(wxT("says")).Contains(wxT("does not compile")));
	}

	ASSERT_NE(brokenProblem, nullptr);
	const ibDataValue* diagnostics = brokenProblem->AsChild()->FindField(wxT("diagnostics"));
	ASSERT_NE(diagnostics, nullptr);
	EXPECT_EQ(diagnostics->Kind(), ibDataKind::Array);
	EXPECT_FALSE(diagnostics->AsArray().empty());
	EXPECT_TRUE(brokenProblem->AsChild()->GetValue<bool>(wxT("checked")));

	ASSERT_NE(neverProblem, nullptr);
	EXPECT_FALSE(neverProblem->AsChild()->GetValue<bool>(wxT("checked")));
	const ibDataValue* empty = neverProblem->AsChild()->FindField(wxT("diagnostics"));
	ASSERT_NE(empty, nullptr);
	EXPECT_TRUE(empty->Kind() == ibDataKind::Array && empty->AsArray().empty());
}

TEST_F(CommonModuleCheck, ConfigFiles_ImportIntoFreshDatabase_PassesCheckAndDiff)
{
	CodeStyleRestore style;
	ibSessionRegistry* registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("configuration-files"), ibSessionKind::Designer));
	ibSession* session = holder.Get();
	ASSERT_NE(session, nullptr);
	ibSessionScope scope(session);
	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{registry, session};
	ibMetaDataConfigurationBase* configuration = session->GetMetaData();
	ASSERT_NE(configuration, nullptr);

	struct Directory {
		std::filesystem::path m_path = std::filesystem::temp_directory_path()
			/ ibGuid(ibGuid::newGuid()).str().ToStdString();
		~Directory() { std::error_code error; std::filesystem::remove_all(m_path, error); }
	} directory;
	const wxString source = wxString::FromUTF8((directory.m_path / "source").u8string());
	const wxString exported = wxString::FromUTF8((directory.m_path / "exported").u8string());
	ibMetaDataConfigurationFile original;
	auto* module = static_cast<ibValueMetaObjectCommonModule*>(
		original.CreateMetaObject(g_metaCommonModuleCLSID, original.GetCommonMetaObject(), false));
	ASSERT_NE(module, nullptr);
	module->SetModuleText(wxT("Function Answer() {\nReturn 42;\n}\n"));
	ASSERT_TRUE(original.SaveConfigToFiles(source));

	const ibMcpTool* importTool = ibFindMcpTool(wxT("config_import_files"));
	ASSERT_NE(importTool, nullptr);
	ibDataNode params, result;
	wxString refusal;
	params.SetValue("directory", source);
	params.SetValue("confirm", false);
	EXPECT_FALSE(importTool->Call(params, result, refusal));
	params.SetField("confirm", ibDataValue::Bool(true));
	refusal.clear();
	ASSERT_TRUE(importTool->Call(params, result, refusal)) << refusal.ToStdString();
	EXPECT_TRUE(configuration->IsConfigOpen());
	for (const ibMetaDiffRecord& difference : ibMetaDiffWalker::Walk(
		original.GetCommonMetaObject(), configuration->GetCommonMetaObject()))
		EXPECT_EQ(difference.m_status, ibMetaDiffStatus::Same);

	const ibMcpTool* check = ibFindMcpTool(wxT("config_check"));
	ASSERT_NE(check, nullptr);
	ibDataNode checkParams, checkResult;
	ASSERT_TRUE(check->Call(checkParams, checkResult, refusal)) << refusal.ToStdString();
	const ibDataValue* problems = checkResult.FindField("problems");
	ASSERT_NE(problems, nullptr);
	EXPECT_TRUE(problems->AsArray().empty());

	const ibMcpTool* exportTool = ibFindMcpTool(wxT("config_export_files"));
	ASSERT_NE(exportTool, nullptr);
	ibDataNode exportParams, exportResult;
	exportParams.SetValue("directory", exported);
	ASSERT_TRUE(exportTool->Call(exportParams, exportResult, refusal)) << refusal.ToStdString();
	ibMetaDataConfigurationFile imported;
	ASSERT_TRUE(imported.LoadConfigFromFiles(exported));
	for (const ibMetaDiffRecord& difference : ibMetaDiffWalker::Walk(
		original.GetCommonMetaObject(), imported.GetCommonMetaObject()))
		EXPECT_EQ(difference.m_status, ibMetaDiffStatus::Same);
}
