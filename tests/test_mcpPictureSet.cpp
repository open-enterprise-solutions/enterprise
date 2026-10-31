// picture_set fills a configuration's Picture object (#190). A common picture
// is declared with metadata_create, and until now nothing over MCP could give
// it an image: picture_set refused it as having "no picture of its own", and
// its Picture property is a {Name, Buffer, Width, Height} structure nobody
// assembles by hand. It now takes `data` (base64 PNG) or `svg`, and what it
// stored is what the configuration saves.
//
// The configuration is the one a designer session opens, as in
// test_commonModuleCheck.cpp.

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "frmserver/mcp/mcpTool.h"
#include "backend/metaCollection/metaPictureObject.h"
#include "backend/propertyManager/property/propertyPicture.h"
#include "backend/metadataConfiguration.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "core/utils/base64.h"

namespace {

struct McpPictureSet : ::testing::Test {
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

const wxChar* const kSquare =
	wxT("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'>")
	wxT("<rect x='2' y='2' width='12' height='12' fill='#3070c0'/></svg>");

// The image as the object keeps it - GetValueAsPicture answers at the size a picture is DRAWN at, 16 x 16.
const ibExternalPictureDescription* Held(const ibValueMetaObject* object)
{
	for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
		if (auto* image = dynamic_cast<const ibPropertyExternalPicture*>(object->GetProperty(idx)))
			return &image->GetValueAsPictureDesc();
	}
	return nullptr;
}

ibSize HeldSize(const ibValueMetaObject* object)
{
	const ibExternalPictureDescription* held = Held(object);
	return held != nullptr ? ibSize((int)held->m_width, (int)held->m_height) : ibSize();
}

bool CallTool(const wxChar* name, const ibDataNode& params, ibDataNode& result, ibString& refusal)
{
	const ibMcpTool* tool = ibFindMcpTool(name);
	return tool != nullptr && tool->Call(params, result, refusal);
}

} // namespace

TEST_F(McpPictureSet, FillsAConfigurationPictureFromDataOrSvg_AndTheConfigurationKeepsIt)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("picture-set"), ibSessionKind::Designer));
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
		ASSERT_TRUE(cfg->CloseDatabase(forceCloseFlag));
	}

	ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
	ASSERT_NE(root, nullptr);

	auto* fromData = dynamic_cast<ibValueMetaObjectPicture*>(
		cfg->CreateMetaObject(g_metaPictureCLSID, root, false));
	auto* fromSvg = dynamic_cast<ibValueMetaObjectPicture*>(
		cfg->CreateMetaObject(g_metaPictureCLSID, root, false));
	ASSERT_NE(fromData, nullptr);
	ASSERT_NE(fromSvg, nullptr);
	cfg->RenameMetaObject(fromData, wxT("Square"));
	cfg->RenameMetaObject(fromSvg, wxT("Drawn"));

	ASSERT_TRUE(cfg->RunDatabase());

	// The bytes a caller has: a PNG, as base64.
	const ibPicture png = ibPicture::FromSvg(kSquare, ibSize(24, 20));
	ASSERT_TRUE(png.IsOk());
	ibString base64;
	ibBase64::Append(base64, png.GetData().GetData(), png.GetData().GetDataLen());

	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int((std::int64_t)fromData->GetMetaID()));
		params.SetValue(ibT("data"), base64);
		params.SetValue(ibT("name"), ibString(wxT("Square")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("picture_set"), params, result, refusal)) << refusal.ToStdString();
		EXPECT_TRUE(result.GetValue<bool>(ibT("set")));
		EXPECT_FALSE(result.GetValue<bool>(ibT("empty")));
	}
	EXPECT_EQ(HeldSize(fromData), ibSize(24, 20));

	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int((std::int64_t)fromSvg->GetMetaID()));
		params.SetValue(ibT("svg"), ibString(kSquare));
		params.SetValue(ibT("name"), ibString(wxT("Drawn")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("picture_set"), params, result, refusal)) << refusal.ToStdString();
	}
	EXPECT_EQ(HeldSize(fromSvg), ibSize(64, 64));

	// A common picture holds an image; naming an engine one is refused, not copied.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int((std::int64_t)fromData->GetMetaID()));
		params.SetValue(ibT("engine"), ibString(wxT("Print")));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("picture_set"), params, result, refusal));
		EXPECT_TRUE(refusal.Contains(wxT("`data`"))) << refusal.ToStdString();
	}
	EXPECT_EQ(HeldSize(fromData), ibSize(24, 20));

	// …and bytes that are no PNG leave it as it was.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int((std::int64_t)fromData->GetMetaID()));
		params.SetValue(ibT("data"), ibString(wxT("bm90IGEgcG5n")));
		params.SetValue(ibT("name"), ibString(wxT("Square")));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("picture_set"), params, result, refusal));
	}
	EXPECT_EQ(HeldSize(fromData), ibSize(24, 20));

	// It is listed as the configuration's own…
	{
		ibDataNode params, result;
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("picture_list"), params, result, refusal)) << refusal.ToStdString();
		auto declared = result.FindField(ibT("configuration"));
		ASSERT_TRUE(declared);
		bool listed = false;
		for (const ibDataValue& line : declared->AsArray())
			listed |= line.Kind() == ibDataKind::Child && line.AsChild()->GetValue<ibString>(ibT("name")) == wxT("Square");
		EXPECT_TRUE(listed);
	}

	// …and the configuration keeps the bytes it was given.
	ibDataNode files;
	ASSERT_TRUE(cfg->SaveConfigToFiles(files));
	ibMetaDataConfigurationFile reread;
	ASSERT_TRUE(reread.LoadConfigFromFiles(files));
	const ibValueMetaObjectPicture* kept =
		reread.FindAnyObjectByFilter<ibValueMetaObjectPicture>(ibGuid(fromData->GetGuid()));
	ASSERT_NE(kept, nullptr);
	const ibExternalPictureDescription* keptImage = Held(kept);
	ASSERT_NE(keptImage, nullptr);
	EXPECT_EQ(keptImage->m_img_buffer,
		std::string(static_cast<const char*>(png.GetData().GetData()), png.GetData().GetDataLen()));
	EXPECT_EQ(HeldSize(kept), ibSize(24, 20));
}
