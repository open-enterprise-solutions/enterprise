// report_select HANGS THE FIELDS IN GROUPS (docs/private/selected-field-groups.md §5.8): a group added, a row moved
// into it by its position, re-placed and retitled, taken out — and the answer's `shows` is the tree; a group's title is a
// caption, written in every language the application declares. The rows here are
// moved and changed, never added by path, so no query has to offer a field: the composer's table is set first, as a
// settings window would have left it.
//
// The application is the one a studio session opens, as in test_mcpPictureSet.cpp.

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "frmserver/mcp/mcpTool.h"
#include "backend/metaCollection/metaTemplateObject.h"   // a report's composer — a template of that kind
#include "backend/metaCollection/metaLanguageObject.h"   // the languages a caption is written in
#include "backend/metadataApplication.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"

namespace {

struct McpReportSelect : ::testing::Test {
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
		ibMetaDataApplicationStorage::CreateApplicationTable();
		ibMetaDataApplicationStorage::CreateApplicationSaveTable();
		ibMetaDataApplicationStorage::CreateApplicationSequence();
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

// THE TABLE AS THE TOOL LEFT IT — the composer's variant zero, the one it edits when no variant is named.
std::vector<ibSelectedFieldDescription> Selected(const ibValueMetaObjectTemplate* composer)
{
	return composer->GetTemplateDesc().GetCompositionDesc().m_variants[0].m_settings.m_selected;
}

} // namespace

TEST_F(McpReportSelect, AGroupIsAdded_ARowMovedIntoIt_RePlaced_TakenOut_TitledInEveryLanguage)
{
	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("report-select"), ibSessionKind::Studio));
	ibSession* const session = holder.Get();
	ASSERT_NE(session, nullptr);
	ibSessionScope scope(session);

	registry->NotifyAuthenticated(session);
	struct Leave {
		ibSessionRegistry* m_registry;
		ibSession* m_session;
		~Leave() { m_registry->NotifyDisconnect(m_session); }
	} leave{ registry, session };

	ibMetaDataApplicationBase* cfg = session->GetMetaData();
	ASSERT_NE(cfg, nullptr);
	if (cfg->IsOpen()) {
		ASSERT_TRUE(cfg->CloseDatabase(ibRuntimeRequest(forceCloseFlag)));
	}

	ibValueMetaObject* report = cfg->CreateMetaObject(g_metaReportCLSID, cfg->GetRootMetaObject(), false);
	ASSERT_NE(report, nullptr);
	ibValueMetaObject* created = cfg->CreateMetaObject(g_metaTemplateCLSID, report, false);
	ibValueMetaObjectTemplate* composer = nullptr;
	ASSERT_TRUE(created != nullptr && created->ConvertToValue(composer));
	composer->SetTemplateDesc(ibTemplateDescription(ibTemplateType::Composer));
	ASSERT_TRUE(cfg->RunDatabase());

	// The table a settings window would have left: two fields.
	ibCompositionDescription composition = composer->GetTemplateDesc().GetCompositionDesc();
	composition.m_variants[0].m_settings.m_selected = { ibSelectedFieldDescription::Field(wxT("Code")),
	                                                    ibSelectedFieldDescription::Field(wxT("Sum")) };
	composer->SetTemplateDesc(composition);
	const std::int64_t id = (std::int64_t)composer->GetMetaID();

	// A GROUP — at the end of the top level, laid side by side.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("group"), ibString(wxT("Amounts")));
		params.SetValue(ibT("placement"), ibString(wxT("horizontal")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("report_select"), params, result, refusal)) << refusal.ToStdString();
	}
	std::vector<ibSelectedFieldDescription> rows = Selected(composer);
	ASSERT_EQ(3u, rows.size());
	ASSERT_TRUE(rows[2].IsGroup());
	EXPECT_EQ(wxT("Amounts"), rows[2].m_title);
	EXPECT_EQ(ibColumnGroupHorizontal, rows[2].m_placement);

	// ROW 2 MOVED INTO THE GROUP that stood after it — the group's position read against the tree without the row.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2")));
		params.SetValue(ibT("into"), ibString(wxT("3")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("report_select"), params, result, refusal)) << refusal.ToStdString();
	}
	rows = Selected(composer);
	ASSERT_EQ(2u, rows.size());
	EXPECT_EQ(wxT("Code"), rows[0].m_path);
	ASSERT_TRUE(rows[1].IsGroup());
	ASSERT_EQ(1u, rows[1].m_children.size());
	EXPECT_EQ(wxT("Sum"), rows[1].m_children[0].m_path);

	// RE-PLACED AND RETITLED by its position — an empty title is a choice: no heading band.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2")));
		params.SetValue(ibT("group"), ibString());
		params.SetValue(ibT("placement"), ibString(wxT("incell")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("report_select"), params, result, refusal)) << refusal.ToStdString();
	}
	rows = Selected(composer);
	ASSERT_EQ(2u, rows.size());
	EXPECT_EQ(ibColumnGroupInCell, rows[1].m_placement);
	EXPECT_TRUE(rows[1].m_title.IsEmpty());

	// A FIELD HAS NO PLACEMENT, and a group never goes into itself — each refused, the table as it was.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("1")));
		params.SetValue(ibT("placement"), ibString(wxT("vertical")));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("report_select"), params, result, refusal));
		EXPECT_FALSE(refusal.IsEmpty());
	}
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2")));
		params.SetValue(ibT("into"), ibString(wxT("2")));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("report_select"), params, result, refusal));
		EXPECT_FALSE(refusal.IsEmpty());
	}
	EXPECT_EQ(rows, Selected(composer));

	// A ROW INSIDE A GROUP TAKEN OUT by its dotted position — and `shows` answers the tree: the field by its path, the
	// group as what it is and what it holds.
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2.1")));
		params.SetValue(ibT("remove"), true);
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("report_select"), params, result, refusal)) << refusal.ToStdString();

		auto shows = result.FindField(ibT("shows"));
		ASSERT_TRUE(shows);
		ASSERT_EQ(ibDataKind::Array, shows->Kind());
		const ibDataRange<ibDataValue> shown = shows->AsArray();
		ASSERT_EQ(2u, shown.size());
		ASSERT_EQ(ibDataKind::String, shown[0].Kind());
		EXPECT_EQ(wxT("Code"), shown[0].AsString());
		ASSERT_EQ(ibDataKind::Child, shown[1].Kind());
		EXPECT_EQ(wxT("incell"), shown[1].AsChild()->GetValue<ibString>(ibT("placement")));
		auto held = shown[1].AsChild()->FindField(ibT("rows"));
		ASSERT_TRUE(held);
		EXPECT_TRUE(held->AsArray().empty());
	}
	rows = Selected(composer);
	ASSERT_EQ(2u, rows.size());
	EXPECT_TRUE(rows[1].m_children.empty());

	// …AND ONCE THE APPLICATION SPEAKS TWO LANGUAGES, A TITLE IS A CAPTION LIKE ANY OTHER: written in one it is refused —
	// it would leave the other blank — and written in both it is kept as written (ibMcpCaptionInEveryLanguage).
	for (const wxChar* code : { wxT("en"), wxT("uk") }) {
		ibValueMetaObject* declared = cfg->CreateMetaObject(g_metaLanguageCLSID, cfg->GetRootMetaObject(), false);
		ibValueMetaObjectLanguage* language = nullptr;
		ASSERT_TRUE(declared != nullptr && declared->ConvertToValue(language));
		language->SetLangCode(code);
	}
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2")));
		params.SetValue(ibT("group"), ibString(wxT("Amounts")));
		ibString refusal;
		EXPECT_FALSE(CallTool(wxT("report_select"), params, result, refusal));
		EXPECT_FALSE(refusal.IsEmpty());
	}
	EXPECT_TRUE(Selected(composer)[1].m_title.IsEmpty());
	{
		ibDataNode params, result;
		params.AddField(ibT("id"), ibDataValue::Int(id));
		params.SetValue(ibT("row"), ibString(wxT("2")));
		params.SetValue(ibT("group"), ibString(wxT("en = 'Amounts'; uk = 'Sumy';")));
		ibString refusal;
		ASSERT_TRUE(CallTool(wxT("report_select"), params, result, refusal)) << refusal.ToStdString();
	}
	EXPECT_EQ(wxT("en = 'Amounts'; uk = 'Sumy';"), Selected(composer)[1].m_title);
}
