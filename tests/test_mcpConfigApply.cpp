// config_apply says what it carried (#311). Its answer counted the restructure
// ledger - the CREATE / ALTER / DROP an apply needed - and called that `changes`.
// A data processor, a report, a module or a form lives in no table, so an apply
// of only those came back as `changes: 0` with an empty ledger while
// database_diff had listed every one of them a moment before and was empty a
// moment after: the caller could not tell whether its work got in.
//
// `changes` and `objects` are now the objects the apply carried - the list
// database_diff gave just before it - and the ledger is kept beside them as
// `structureChanges`.
//
// The configuration is the one a designer session opens, as in
// test_mcpPictureSet.cpp.

#include <gtest/gtest.h>

#include <memory>
#include <optional>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "frmserver/mcp/mcpTool.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

namespace {

struct McpConfigApply : ::testing::Test {
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
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		if (!m_db->Open(wxT(":memory:")))
			GTEST_SKIP() << "in-memory SQLite open failed";
		pool->Init(m_db, /*maxSize=*/1, /*minIdle=*/0);
		ibMetaDataConfigurationStorage::CreateConfigTable();
		ibMetaDataConfigurationStorage::CreateConfigSaveTable();
		ibMetaDataConfigurationStorage::CreateConfigSequence();
	}

	void TearDown() override {
		if (m_ownsApp && ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}
};

bool CallTool(const wxChar* name, const ibDataNode& params, ibDataNode& result, ibString& refusal)
{
	const ibMcpTool* tool = ibFindMcpTool(name);
	return tool != nullptr && tool->Call(params, result, refusal);
}

bool Apply(ibDataNode& result, ibString& refusal)
{
	ibDataNode params;
	params.AddField(ibT("confirm"), ibDataValue::Bool(true));
	return CallTool(wxT("config_apply"), params, result, refusal);
}

std::int64_t Whole(const ibDataNode& node, const ibDataKey& name)
{
	auto field = node.FindField(name);
	return field && field->Kind() == ibDataKind::Number ? field->AsInt() : -1;
}

// The entry `objects` gives for the object by this name, or null.
std::optional<ibDataNode> Listed(const ibDataNode& result, const wxChar* name)
{
	auto objects = result.FindField(ibT("objects"));
	if (!objects || objects->Kind() != ibDataKind::Array)
		return std::nullopt;
	for (const ibDataValue& line : objects->AsArray()) {
		if (line.Kind() == ibDataKind::Child && line.AsChild()->GetValue<ibString>(ibT("name")) == name)
			return line.AsChild();
	}
	return std::nullopt;
}

} // namespace

TEST_F(McpConfigApply, AnApplyOfObjectsWithNoTableCountsThem_AndKeepsTheLedgerApart)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("config-apply"), ibSessionKind::Designer));
	ibSession* const session = holder.Get();
	ASSERT_NE(session, nullptr);
	ibSessionScope scope(session);

	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{ registry, session };

	ibMetaDataConfigurationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	if (cfg->IsConfigOpen()) {
		ASSERT_TRUE(cfg->CloseDatabase(ibRuntimeRequest(forceCloseFlag)));
	}
	ASSERT_NE(cfg->GetConfiguration(), nullptr) << "the designer's configuration has the base's copy beside it";

	ASSERT_TRUE(cfg->RunDatabase());

	// The base takes the configuration as it is first, so what follows is one change and nothing else.
	{
		ibDataNode result;
		ibString refusal;
		ASSERT_TRUE(Apply(result, refusal)) << refusal.ToStdString();
		ASSERT_TRUE(result.GetValue<bool>(ibT("applied")));
	}

	// A data processor: an object of the configuration, and not a table.
	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ibValueMetaObject* const loader = cfg->CreateMetaObject(g_metaDataProcessorCLSID, root, false);
	ASSERT_NE(loader, nullptr);
	cfg->RenameMetaObject(loader, wxT("Loader"));

	ibDataNode result;
	ibString refusal;
	ASSERT_TRUE(Apply(result, refusal)) << refusal.ToStdString();
	EXPECT_TRUE(result.GetValue<bool>(ibT("applied")));

	// It is what the apply carried, in database_diff's words...
	EXPECT_GE(Whole(result, ibT("changes")), 1);
	const std::optional<ibDataNode> entry = Listed(result, wxT("Loader"));
	ASSERT_TRUE(entry.has_value()) << "the data processor is named among the objects applied";
	EXPECT_EQ(entry->GetValue<ibString>(ibT("status")), wxT("added"));

	// ...and the ledger stays the structure, which a data processor does not touch.
	auto ledger = result.FindField(ibT("ledger"));
	ASSERT_TRUE(ledger);
	ASSERT_EQ(ledger->Kind(), ibDataKind::Array);
	std::int64_t lines = 0;
	for (const ibDataValue& line : ledger->AsArray()) {
		(void)line;
		++lines;
	}
	EXPECT_EQ(Whole(result, ibT("structureChanges")), lines);

	// The base now holds it: nothing differs, and applying again carries nothing.
	{
		ibDataNode diff;
		ibString said;
		ASSERT_TRUE(CallTool(wxT("database_diff"), ibDataNode(), diff, said)) << said.ToStdString();
		EXPECT_EQ(Whole(diff, ibT("differences")), 0);
	}
	{
		ibDataNode again;
		ibString said;
		ASSERT_TRUE(Apply(again, said)) << said.ToStdString();
		EXPECT_EQ(Whole(again, ibT("changes")), 0);
	}
}
