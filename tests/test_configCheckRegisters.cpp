// config_check asks every register what the save would refuse it for - in the save's own words, before
// anybody saves. A register the save refuses takes the WHOLE configuration with it, so a half-built one
// held every other change of a session behind it, and the first anybody heard of it was the refusal
// (open-enterprise-solutions/enterprise#189). A finished register, a sequence with no dimensions among
// them, is not a problem at all.

#include <gtest/gtest.h>

#include <memory>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/session/session.h"
#include "backend/session/sessionRegistry.h"
#include "frmserver/mcp/mcpTool.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/sequence.h"
#include "backend/restructureInfo.h"


namespace {

// What config_check says about one object, by its name; empty when it says nothing.
ibString Says(const ibDataNode& result, const ibString& name)
{
	ibString says;
	auto problems = result.FindField(ibT("problems"));
	if (!problems || problems->Kind() != ibDataKind::Array)
		return says;
	for (const ibDataValue& one : problems->AsArray())
		if (one.Kind() == ibDataKind::Child && one.AsChild()
			&& one.AsChild()->GetValue<ibString>(ibT("name")) == name)
			says << one.AsChild()->GetValue<ibString>(ibT("says"));
	return says;
}

} // namespace

TEST(ConfigCheckRegisters, NamesWhatTheSaveWouldRefuseAndOnlyThat) {
	wxInitializer wx;
	if (!wx.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";
	if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
		GTEST_SKIP() << "appData env unavailable headless";
	struct Release {
		short m_codeStyle = ibCompileCode::GetCodeStyle();
		~Release() {
			ibCompileCode::SetCodeStyle(m_codeStyle);
			ibApplicationInstance::DestroyAppDataEnv();
		}
	} release;

	// The base is an in-memory SQLite, and the configuration is the one its designer session edits - run, as
	// config_check finds it (test_commonModuleCheck.cpp says why each step is there).
	ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
	ASSERT_NE(pool, nullptr);
	std::shared_ptr<ibDatabaseLayerSQLite> db = std::make_shared<ibDatabaseLayerSQLite>();
	ASSERT_TRUE(db->Open(wxT(":memory:")));
	pool->Init(db, /*maxSize=*/1, /*minIdle=*/0);
	ibMetaDataConfigurationStorage::CreateConfigTable();
	ibMetaDataConfigurationStorage::CreateConfigSaveTable();
	ibMetaDataConfigurationStorage::CreateConfigSequence();

	ibSessionRegistry* const registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	ibSessionHolder holder = registry->MintUnlisted(
		std::make_shared<ibSession>(wxT("register-check"), ibSessionKind::Designer));
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

	ibValueMetaObject* document = cfg->CreateMetaObject(g_metaDocumentCLSID, root, false);
	ASSERT_NE(document, nullptr);
	const ibClassID documentRef = reference_to_clsid(document->GetMetaID(), clsid_metaclass(document->GetClassType()));

	// An information register with nothing in it: its table would have no column.
	auto* empty = dynamic_cast<ibValueMetaObjectInformationRegister*>(cfg->CreateMetaObject(g_metaInformationRegisterCLSID, root, false));
	// An accumulation register with a resource and no document posting into it.
	auto* unposted = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(cfg->CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
	// A sequence no document registers in yet - and one that a document does, with no dimensions: one border.
	auto* unbound = dynamic_cast<ibValueMetaObjectSequence*>(cfg->CreateMetaObject(g_metaSequenceCLSID, root, false));
	auto* whole = dynamic_cast<ibValueMetaObjectSequence*>(cfg->CreateMetaObject(g_metaSequenceCLSID, root, false));
	ASSERT_NE(empty, nullptr);
	ASSERT_NE(unposted, nullptr);
	ASSERT_NE(unbound, nullptr);
	ASSERT_NE(whole, nullptr);
	empty->SetName(wxT("Empty"));
	unposted->SetName(wxT("Unposted"));
	unbound->SetName(wxT("Unbound"));
	whole->SetName(wxT("Whole"));

	ASSERT_NE(cfg->CreateMetaObject(g_metaResourceCLSID, unposted, false), nullptr);
	whole->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(documentRef);
	ASSERT_TRUE(whole->GetDimensionArrayObject().empty());

	ASSERT_TRUE(cfg->RunDatabase());

	const ibMcpTool* check = ibFindMcpTool(wxT("config_check"));
	ASSERT_NE(check, nullptr);
	ibDataNode params;
	ibDataNode result;
	ibString refusal;
	ASSERT_TRUE(check->Call(params, result, refusal)) << refusal.ToStdString();

	EXPECT_TRUE(Says(result, wxT("Empty")).Contains(wxT("Doesn't have any dimension"))) << Says(result, wxT("Empty")).ToStdString();
	EXPECT_TRUE(Says(result, wxT("Unposted")).Contains(wxT("no recorder"))) << Says(result, wxT("Unposted")).ToStdString();
	EXPECT_TRUE(Says(result, wxT("Unbound")).Contains(wxT("no recorder"))) << Says(result, wxT("Unbound")).ToStdString();
	EXPECT_TRUE(Says(result, wxT("Whole")).IsEmpty()) << "a sequence with no dimensions is one border: " << Says(result, wxT("Whole")).ToStdString();

	// …and it is the save's question, asked in the save's words: what config_check said is what the save
	// refuses with, and what it did not say is saved.
	ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
	ledger.Clear();
	EXPECT_FALSE(unposted->OnSaveMetaObject(0));
	ASSERT_TRUE(ledger.HasErrors());
	bool same = false;
	for (const ibRestructureInfo::Entry& entry : ledger)
		same = same || (entry.type == ibRestructure::error && Says(result, wxT("Unposted")).Contains(entry.descr));
	EXPECT_TRUE(same);

	ledger.Clear();
	EXPECT_TRUE(whole->OnSaveMetaObject(0));
	EXPECT_FALSE(ledger.HasErrors());

	// Once a document posts into it, the register says nothing either.
	unposted->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(documentRef);
	ibDataNode again;
	ASSERT_TRUE(check->Call(params, again, refusal)) << refusal.ToStdString();
	EXPECT_TRUE(Says(again, wxT("Unposted")).IsEmpty()) << Says(again, wxT("Unposted")).ToStdString();
}
