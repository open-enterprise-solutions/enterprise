// What a button runs is a command binding — an id path and the item type (ibCommandDescription). #579: MCP form_set,
// asked to set a button's Command to `{name: …}`, answered with a binding nobody asked for: the reader took any node,
// kept the hops the button already had, and read the missing `commandType` as 0, which no member carries.
//
// Two halves. The reader reads the shape it writes, whole, or refuses and leaves the binding as it was. And form_set,
// the general property door, refuses a name and says which verb binds a command by it: form_bind.
//
// The configuration is the one a designer session opens, as in test_mcpPictureSet.cpp.

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_mainFrame.h"   // a frame so the form can be born
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "core/description/commandDescription.h"
#include "core/serialize/dataBuilder.h"
#include "frmserver/mcp/mcpTool.h"
#include "frmserver/visualView/ctrl/form.h"   // ibValueForm — what the frame makes
#include "framedSession.h"                     // KeyFrame, FramedSession — a designer's session has a frame

namespace {

ibDataValue CommandNode(std::initializer_list<std::int64_t> hops, std::int64_t type)
{
	auto node = std::make_shared<ibDataNode>();
	std::vector<ibDataValue> ids;
	for (const std::int64_t id : hops)
		ids.push_back(ibDataValue::Int(id));
	node->AddField(ibT("hops"), ibDataValue::Array(ids));
	node->AddField(ibT("commandType"), ibDataValue::Int(type));
	return ibDataValue::Child(node);
}

} // namespace

TEST(CommandDescriptionMemory, ReadNode_WrittenShape_ReplacesWhatWasHeld)
{
	ibCommandDescription held(std::vector<ibMetaID>{ 7 });

	ASSERT_TRUE(ibCommandDescriptionMemory::ReadNode(CommandNode({ 5, 9 }, ibInterfaceCommandType_Create), held));

	ASSERT_EQ(held.GetHopCount(), 2u);
	EXPECT_EQ(held.GetFirst(), 5);
	EXPECT_EQ(held.GetLeaf(), 9);
	EXPECT_EQ(held.GetCommandType(), ibInterfaceCommandType_Create);
}

TEST(CommandDescriptionMemory, ReadNode_RoundTripsWhatWriteNodeWrites)
{
	ibCommandDescription written(std::vector<ibMetaID>{ 12, 34 });
	written.SetCommandType(ibInterfaceCommandType_Select);
	ibDataValue value;
	ASSERT_TRUE(ibCommandDescriptionMemory::WriteNode(value, written));

	ibCommandDescription read;
	ASSERT_TRUE(ibCommandDescriptionMemory::ReadNode(value, read));
	EXPECT_TRUE(read == written);
}

TEST(CommandDescriptionMemory, ReadNode_AnotherShape_IsRefusedAndTheBindingStays)
{
	ibCommandDescription held(std::vector<ibMetaID>{ 7 });

	// A name is not a binding.
	auto named = std::make_shared<ibDataNode>();
	named->SetValue(ibT("name"), ibString(ibT("Act")));
	EXPECT_FALSE(ibCommandDescriptionMemory::ReadNode(ibDataValue::Child(named), held));

	// …nor hops without the item type, nor a type no member carries.
	auto typeless = std::make_shared<ibDataNode>();
	typeless->AddField(ibT("hops"), ibDataValue::Array({ ibDataValue::Int(5) }));
	EXPECT_FALSE(ibCommandDescriptionMemory::ReadNode(ibDataValue::Child(typeless), held));
	EXPECT_FALSE(ibCommandDescriptionMemory::ReadNode(CommandNode({ 5 }, 0), held));

	ASSERT_EQ(held.GetHopCount(), 1u);
	EXPECT_EQ(held.GetFirst(), 7);
	EXPECT_EQ(held.GetCommandType(), ibInterfaceCommandType_Default);
}

TEST(CommandDescriptionMemory, ReadNode_Nothing_IsWiredToNothing)
{
	ibCommandDescription held;
	EXPECT_TRUE(ibCommandDescriptionMemory::ReadNode(ibDataValue(), held));
	EXPECT_FALSE(held.IsOk());
}

namespace {

struct McpFormSetCommand : ::testing::Test {
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

} // namespace

TEST_F(McpFormSetCommand, ANameForAButtonsCommand_IsRefusedWithTheVerbThatBindsIt_AndTheBindingStays)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	auto framed = std::make_shared<FramedSession>(wxT("form-set-command"), ibSessionKind::Designer);
	ibSession* const session = framed.get();
	KeyFrame frame(registry->MintUnlisted(framed));
	framed->Attach(&frame);
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

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	auto* creator = dynamic_cast<ibValueMetaObjectFormBase*>(cfg->CreateMetaObject(g_metaCommonFormCLSID, root, false));
	ASSERT_NE(creator, nullptr);
	cfg->RenameMetaObject(creator, wxT("Panel"));
	ASSERT_TRUE(cfg->RunDatabase());

	const std::int64_t formId = (std::int64_t)creator->GetMetaID();

	// A form command and a button, bound to it the way the designer binds one.
	{
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.SetValue(ibT("command"), ibString(wxT("Act")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("form_add"), params, result, refusal)) << refusal.ToStdString();
	}
	std::int64_t buttonId = 0;
	{
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.SetValue(ibT("class"), ibString(wxT("Button")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("form_add"), params, result, refusal)) << refusal.ToStdString();
		buttonId = result.GetValue<std::int32_t>(ibT("controlId"));
	}
	ASSERT_GT(buttonId, 0);
	{
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.AddField(ibT("control"), ibDataValue::Int(buttonId));
		params.SetValue(ibT("property"), ibString(wxT("Command")));
		params.SetValue(ibT("path"), ibString(wxT("Act")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("form_bind"), params, result, refusal)) << refusal.ToStdString();
		ASSERT_TRUE(result.GetValue<bool>(ibT("bound")));
	}

	const ibMemoryBuffer bound = creator->GetFormData();
	ASSERT_GT(bound.GetDataLen(), 0u);

	// The name, through the general door: refused, the refusal names form_bind, and the form is not stored again.
	{
		ibDataNode params, result;
		params.AddField(ibT("form"), ibDataValue::Int(formId));
		params.AddField(ibT("control"), ibDataValue::Int(buttonId));
		params.SetValue(ibT("property"), ibString(wxT("Command")));
		auto named = std::make_shared<ibDataNode>();
		named->SetValue(ibT("name"), ibString(wxT("Act")));
		params.AddField(ibT("value"), ibDataValue::Child(named));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("form_set"), params, result, refusal));
		EXPECT_TRUE(refusal.Contains(wxT("form_bind"))) << refusal.ToStdString();
	}

	const ibMemoryBuffer after = creator->GetFormData();
	ASSERT_EQ(after.GetDataLen(), bound.GetDataLen());
	EXPECT_EQ(0, memcmp(after.GetData(), bound.GetData(), bound.GetDataLen()));
}
