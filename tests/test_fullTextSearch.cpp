// =============================================================================
// FullTextSearch - the base's mode, kept in the base, and the maintenance verbs that succeed and do nothing while
// there is no index (backend/system/value/valueFullTextSearch.h). The base is an in-memory SQLite with sys_settings,
// the table a base's opening makes.
// =============================================================================

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/settings/settingsStorage.h"
#include "backend/system/systemManagerEnum.h"
#include "backend/system/value/valueFullTextSearch.h"

struct SettingsSchemaAccess {
	static void Create() { ibApplicationInstance::CreateTableSettings(); }
};

namespace {

struct FullTextSearch : ::testing::Test {
	wxInitializer m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> m_db;
	bool m_ownsApp = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (ibApplicationInstance::Get(false) == nullptr) {
			if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
				GTEST_SKIP() << "appData env unavailable headless";
			m_ownsApp = true;
		}
		ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr || ibApplicationInstance::GetSettingsStorage() == nullptr)
			GTEST_SKIP() << "no connection pool or settings storage after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!m_db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(m_db, /*maxSize=*/1, /*minIdle=*/0);
		SettingsSchemaAccess::Create();
	}

	void TearDown() override {
		if (m_ownsApp && ibApplicationInstance::Get(false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

ibValue Mode(ibFullTextSearchMode mode)
{
	return ibValue::CreateEnumObject<ibValueEnumFullTextSearchMode>(mode);
}

// A verb of the manager, called the way a script calls it - by its name.
ibValue Call(ibValueFullTextSearchManager& manager, const ibString& name, std::vector<ibValue> arguments = {})
{
	std::vector<ibValue*> params;
	for (ibValue& argument : arguments)
		params.push_back(&argument);
	const long method = manager.FindMethod(name);
	EXPECT_NE(method, wxNOT_FOUND) << name.ToStdString();
	ibValue result;
	if (manager.HasRetVal(method))
		EXPECT_TRUE(manager.CallAsFunc(method, result, params.data(), static_cast<long>(params.size())));
	else
		EXPECT_TRUE(manager.CallAsProc(method, params.data(), static_cast<long>(params.size())));
	return result;
}

} // namespace

TEST_F(FullTextSearch, ABaseWhereNobodySwitchedItOnHasNone)
{
	EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), ibFullTextSearchMode_Disable);
}

TEST_F(FullTextSearch, TheModeIsKeptInTheBase)
{
	ibValueFullTextSearchManager::SetMode(ibFullTextSearchMode_Enable);
	EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), ibFullTextSearchMode_Enable);

	// One row, the base's: no user in its address.
	const std::vector<ibSettingsEntry> rows =
		ibApplicationInstance::GetSettingsStorage()->List(ibSettingsCategory::Search,
			ibGuid(ibT("66756c6c-7465-7874-0000-000000000001")), wxNullGuid);
	EXPECT_EQ(rows.size(), 1u);

	ibValueFullTextSearchManager::SetMode(ibFullTextSearchMode_Disable);
	EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), ibFullTextSearchMode_Disable);
}

TEST_F(FullTextSearch, AScriptSwitchesTheModeWithAMember)
{
	ibValueFullTextSearchManager manager;
	Call(manager, ibT("SetFullTextSearchMode"), { Mode(ibFullTextSearchMode_Enable) });
	const ibValue mode = Call(manager, ibT("GetFullTextSearchMode"));
	EXPECT_EQ(mode.ConvertToEnumValue<ibFullTextSearchMode>(), ibFullTextSearchMode_Enable);

	Call(manager, ibT("SetFullTextSearchMode"), { Mode(ibFullTextSearchMode_Disable) });
	EXPECT_EQ(Call(manager, ibT("GetFullTextSearchMode")).ConvertToEnumValue<ibFullTextSearchMode>(),
		ibFullTextSearchMode_Disable);
}

TEST_F(FullTextSearch, AModeThatIsNotAMemberIsRefused)
{
	ibValueFullTextSearchManager manager;
	ibValue number(1);
	ibValue* params[] = { &number };
	const long method = manager.FindMethod(ibT("SetFullTextSearchMode"));
	EXPECT_THROW(manager.CallAsProc(method, params, 1), ibBackendException);
	EXPECT_THROW(manager.CallAsProc(method, params, 0), ibBackendException);
	EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), ibFullTextSearchMode_Disable);
}

TEST_F(FullTextSearch, WithNoIndexTheMaintenanceSucceedsAndChangesNothing)
{
	for (const ibFullTextSearchMode mode : { ibFullTextSearchMode_Enable, ibFullTextSearchMode_Disable }) {
		ibValueFullTextSearchManager::SetMode(mode);
		ibValueFullTextSearchManager manager;

		EXPECT_FALSE(Call(manager, ibT("IndexTrue")).GetBoolean());
		Call(manager, ibT("UpdateIndex"));
		Call(manager, ibT("UpdateIndex"), { ibValue(false), ibValue(true) });
		Call(manager, ibT("ClearIndex"));
		EXPECT_FALSE(Call(manager, ibT("IndexTrue")).GetBoolean());

		const ibValue updated = Call(manager, ibT("UpdateDate"));
		EXPECT_EQ(updated.GetType(), ibValueTypes::TYPE_DATE);
		EXPECT_TRUE(updated.GetDate().IsEmpty());

		// The mode is what it was: maintenance does not switch the search.
		EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), mode);
	}
}

TEST_F(FullTextSearch, AFlagThatIsNotABooleanIsRefused)
{
	ibValueFullTextSearchManager manager;
	ibValue number(1);
	ibValue* params[] = { &number };
	EXPECT_THROW(manager.CallAsProc(manager.FindMethod(ibT("UpdateIndex")), params, 1), ibBackendException);
}

TEST(FullTextSearchTypes, TheManagerAndTheModeAreRegistered)
{
	EXPECT_TRUE(ibValue::IsRegisterCtor(ibT("FullTextSearchManager")));
	EXPECT_TRUE(ibValue::IsRegisterCtor(ibT("FullTextSearchMode")));
}
