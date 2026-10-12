// A common module that does not compile is reported as data, the same list
// config_apply attaches to a successful apply. The session will not start
// while that module stands, and the apply used to say nothing about it.

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/scriptCheck.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"

namespace {

struct CommonModuleCheck : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;

	void SetUp() override {
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
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
	}

	void TearDown() override {
		if (cfg)
			cfg->CloseDatabase();
		cfg.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

} // namespace

TEST_F(CommonModuleCheck, AModuleThatDoesNotCompileIsListedAndACleanOneIsNot)
{
	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	auto* clean = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, /*runObject*/ false));
	auto* broken = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, /*runObject*/ false));
	ASSERT_NE(clean, nullptr);
	ASSERT_NE(broken, nullptr);
	clean->SetModuleText(wxT("a = 1;\n"));
	broken->SetModuleText(wxT("a = Nope;\n"));

	ASSERT_TRUE(cfg->RunDatabase());

	const std::vector<ibDiagnostic> failed = ibCommonModulesThatDoNotCompile(cfg.get());
	ASSERT_EQ(failed.size(), 1u);
	EXPECT_EQ(failed.front().m_moduleName, broken->GetName());
	EXPECT_EQ(failed.front().m_code, 39);
	EXPECT_TRUE(failed.front().m_message.Contains(wxT("Nope")));
}
