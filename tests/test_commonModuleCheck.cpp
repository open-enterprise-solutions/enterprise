// A common module that does not compile is reported as data, the same list
// config_apply attaches to a successful apply. The session will not start
// while that module stands, and the apply used to say nothing about it.
//
// The check reads the designer's compile cache. A file configuration does not
// build one (that cache belongs to the configuration the designer edits), so
// this fixture installs the same cache the designer installs, and the modules
// register into it when the configuration opens.

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/scriptCheck.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaModuleObject.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/moduleManager/moduleManager.h"

namespace {

// The designer's own cache: a compile-value cache whose manager is the
// lightweight designer holder. Built for this configuration whatever mode the
// process is in, so the check has a unit to compile.
struct CheckingConfiguration : ibMetaDataConfigurationFile {
	std::unique_ptr<ibCompileValueCache> CreateDesignerCache() override {
		auto cache = std::make_unique<ibCompileValueCache>();
		cache->SetModuleManager(new ibValueModuleManagerDesigner(this, GetCommonMetaObject()));
		return cache;
	}
};

struct CommonModuleCheck : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	std::unique_ptr<CheckingConfiguration> cfg;

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
		cfg = std::make_unique<CheckingConfiguration>();
	}

	void TearDown() override {
		if (cfg)
			cfg->CloseDatabase();
		cfg.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

// RunDatabase pins the process-wide style to the configuration's. The rest of
// this binary compiles VES, so the pin must not leak past the test.
struct CodeStyleRestore {
	short m_saved = ibCompileCode::GetCodeStyle();
	~CodeStyleRestore() { ibCompileCode::SetCodeStyle(m_saved); }
};

} // namespace

TEST_F(CommonModuleCheck, AModuleThatDoesNotCompileIsListedAndACleanOneIsNot)
{
	CodeStyleRestore style;

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	auto* clean = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, /*runObject*/ false));
	auto* broken = dynamic_cast<ibValueMetaObjectCommonModule*>(
		cfg->CreateMetaObject(g_metaCommonModuleCLSID, root, /*runObject*/ false));
	ASSERT_NE(clean, nullptr);
	ASSERT_NE(broken, nullptr);
	// A common module may contain only procedures and functions. An assignment
	// of a known literal compiles; a name that was never declared does not.
	clean->SetModuleText(wxT("Procedure Clean()\na = 1;\nEndProcedure\n"));
	broken->SetModuleText(wxT("Procedure Broken()\na = Nope;\nEndProcedure\n"));

	ASSERT_TRUE(cfg->RunDatabase());
	ibCompileCode::SetCodeStyle(style.m_saved);
	ASSERT_NE(cfg->GetCompileCache(), nullptr);

	const std::vector<ibDiagnostic> failed = ibCommonModulesThatDoNotCompile(cfg.get());
	ASSERT_EQ(failed.size(), 1u);
	EXPECT_TRUE(failed.front().m_moduleName.Contains(broken->GetName()));
	EXPECT_FALSE(failed.front().m_moduleName.Contains(clean->GetName()));
	EXPECT_EQ(failed.front().m_code, 39);
	EXPECT_TRUE(failed.front().m_message.Contains(wxT("Nope")));
}
