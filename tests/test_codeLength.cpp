// The code and the description of a catalog or a chart have a shape the configuration chooses:
// CodeType (String / Number), CodeLength and DescriptionLength (open-enterprise-solutions/enterprise#274).
//
// The declaration is restated on the two attributes, so the column, the generator and every check read
// it off the attribute. It survives a save and a load, a length the column cannot have is refused where
// it is typed and raised where it is read, and a predefined item that does not fit is refused naming the
// item - by AppendPredefinedValue / SetPredefinedValue themselves, and again by the apply, before any DDL,
// instead of the engine's "string right truncation". So is a length lowered below the codes stored.

#include <gtest/gtest.h>

#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <chrono>

#include "backend/appData.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/firebird/firebirdDatabaseLayer.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/compiler/compileCode.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/query/columnLayout.h"
#include "backend/query/schemaBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "core/clsid.h"
#include "core/exception.h"

namespace {

ibValueMetaObjectCatalog* NewCatalog(ibMetaDataConfigurationFile& cfg, const ibString& name)
{
	auto* catalog = dynamic_cast<ibValueMetaObjectCatalog*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, cfg.GetCommonMetaObject(), /*runObject*/ false));
	if (catalog != nullptr)
		catalog->SetName(name);
	return catalog;
}

// As the inspector and metadata_set place a value: the owner is asked first and may refuse.
bool Set(ibValueMetaObjectCatalog* catalog, const ibString& property, const ibVariant& value)
{
	return ibPropertyGate::SetValue(catalog, catalog->GetProperty(property), value);
}

ibVariant CodeTypeValue(ibCodeType type)
{
	return ibVariant(ibNumber((long)type));
}

const ibTypeDescription& CodeType(const ibValueMetaObjectCatalog* catalog)
{
	return catalog->GetDataCode()->GetTypeDesc();
}

const ibTypeDescription& DescriptionType(const ibValueMetaObjectCatalog* catalog)
{
	return catalog->GetDataDescription()->GetTypeDesc();
}

} // namespace

TEST(CodeLength, ANewCatalogKeepsTheShapeTheCodeHadBefore)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	EXPECT_EQ(firms->GetCodeType(), ibCodeType_String);
	EXPECT_EQ(firms->GetCodeLength(), 8u);
	EXPECT_EQ(firms->GetDescriptionLength(), 150u);
	EXPECT_TRUE(CodeType(firms).ContainType(ibValueTypes::TYPE_STRING));
	EXPECT_EQ(CodeType(firms).GetLength(), 8u);
	EXPECT_EQ(DescriptionType(firms).GetLength(), 150u);
}

TEST(CodeLength, TheDeclarationRetypesTheCodeAndTheDescription)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	ASSERT_TRUE(Set(firms, wxT("CodeLength"), ibVariant(9u)));
	EXPECT_EQ(CodeType(firms).GetLength(), 9u);

	ASSERT_TRUE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	EXPECT_TRUE(CodeType(firms).ContainType(ibValueTypes::TYPE_NUMBER));
	EXPECT_FALSE(CodeType(firms).ContainType(ibValueTypes::TYPE_STRING)) << "a number code holds nothing else";
	EXPECT_EQ(CodeType(firms).GetPrecision(), 9u);
	EXPECT_EQ(CodeType(firms).GetScale(), 0u);

	ASSERT_TRUE(Set(firms, wxT("DescriptionLength"), ibVariant(250u)));
	EXPECT_EQ(DescriptionType(firms).GetLength(), 250u);
}

TEST(CodeLength, ALengthTheColumnCannotHaveIsRefused)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	EXPECT_FALSE(Set(firms, wxT("CodeLength"), ibVariant(0u)));
	EXPECT_FALSE(Set(firms, wxT("CodeLength"), ibVariant((unsigned)MAX_LENGTH_STRING + 1)));
	EXPECT_FALSE(Set(firms, wxT("DescriptionLength"), ibVariant(0u)));
	EXPECT_EQ(firms->GetCodeLength(), 8u) << "a refused value leaves the one that stood";

	// A number holds 32 digits: the length asks the type that stands, the type the length that stands.
	ASSERT_TRUE(Set(firms, wxT("CodeLength"), ibVariant(50u)));
	EXPECT_FALSE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	ASSERT_TRUE(Set(firms, wxT("CodeLength"), ibVariant(9u)));
	ASSERT_TRUE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	EXPECT_FALSE(Set(firms, wxT("CodeLength"), ibVariant((unsigned)MAX_PRECISION_NUMBER + 1)));
}

TEST(CodeLength, TheShapeSurvivesASaveAndALoad)
{
	ibMetaDataConfigurationFile cfg1;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg1, wxT("Firms"));
	ASSERT_NE(firms, nullptr);
	ASSERT_TRUE(Set(firms, wxT("CodeLength"), ibVariant(9u)));
	ASSERT_TRUE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	ASSERT_TRUE(Set(firms, wxT("DescriptionLength"), ibVariant(100u)));

	ibMemoryBuffer buffer;
	ASSERT_TRUE(cfg1.SaveConfigToBuffer(buffer));
	ibMetaDataConfigurationFile cfg2;
	ASSERT_TRUE(cfg2.LoadConfigFromBuffer(buffer));

	auto* loaded = cfg2.FindAnyObjectByFilter<ibValueMetaObjectCatalog>(firms->GetMetaID());
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->GetCodeType(), ibCodeType_Number);
	EXPECT_EQ(loaded->GetCodeLength(), 9u);
	EXPECT_EQ(loaded->GetDescriptionLength(), 100u);
	// …and restated on the attributes at the load, not only at the next edit.
	EXPECT_TRUE(CodeType(loaded).ContainType(ibValueTypes::TYPE_NUMBER));
	EXPECT_EQ(CodeType(loaded).GetPrecision(), 9u);
	EXPECT_EQ(DescriptionType(loaded).GetLength(), 100u);
}

TEST(CodeLength, APredefinedItemThatDoesNotFitIsRefusedByName)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	ibString refusal;
	EXPECT_TRUE(firms->CheckPredefinedValue(wxT("Main"), wxT("00000001"), wxT("Main firm"), &refusal));
	EXPECT_TRUE(firms->CheckPredefinedValue(wxT("Main"), wxEmptyString, wxT("Main firm"), &refusal)) << "an empty code is generated";

	ASSERT_FALSE(firms->CheckPredefinedValue(wxT("Main"), wxT("000000001"), wxT("Main firm"), &refusal));
	EXPECT_NE(refusal.Find(wxT("Firms")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(wxT("Main")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(wxT("CodeLength")), wxNOT_FOUND) << refusal;

	ibString longDescription;
	for (int i = 0; i < 151; ++i)
		longDescription += wxT("x");
	ASSERT_FALSE(firms->CheckPredefinedValue(wxT("Main"), wxT("1"), longDescription, &refusal));
	EXPECT_NE(refusal.Find(wxT("DescriptionLength")), wxNOT_FOUND) << refusal;

	// A number code: digits only, and leading zeros are not digits it stores.
	ASSERT_TRUE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	EXPECT_TRUE(firms->CheckPredefinedValue(wxT("Main"), wxT("000000001"), wxT("Main firm"), &refusal)) << refusal;
	EXPECT_FALSE(firms->CheckPredefinedValue(wxT("Main"), wxT("123456789"), wxT("Main firm"), &refusal));
	EXPECT_FALSE(firms->CheckPredefinedValue(wxT("Main"), wxT("12A"), wxT("Main firm"), &refusal));
	EXPECT_NE(refusal.Find(wxT("digits only")), wxNOT_FOUND) << refusal;
}

TEST(CodeLength, TheEngineRefusesAPredefinedItemThatDoesNotFit)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	// Every road that declares an item - predefined_add, the designer's list, a caller of its own - ends here.
	try {
		firms->AppendPredefinedValue(wxT("Main"), wxT("000000001"), wxT("Main firm"));
		FAIL() << "a 9-character code was declared on a code of 8";
	}
	catch (const ibCoreException& err) {
		EXPECT_NE(err.GetErrorDescription().Find(wxT("Main")), wxNOT_FOUND) << err.GetErrorDescription();
	}
	EXPECT_TRUE(firms->GetPredefinedValueArray().empty()) << "nothing was added";

	firms->AppendPredefinedValue(wxT("Main"), wxT("00000001"), wxT("Main firm"));
	ASSERT_EQ(firms->GetPredefinedValueArray().size(), 1u);
	const ibGuid guid = firms->GetPredefinedValueArray().front()->GetPredefinedGuid();
	EXPECT_THROW(firms->SetPredefinedValue(guid, wxT("Main"), wxT("000000001"), wxT("Main firm")), ibCoreException);
	EXPECT_EQ(firms->GetPredefinedValueArray().front()->GetPredefinedCode(), wxT("00000001")) << "the item stands as it was";
}

TEST(CodeLength, ASavedLengthTheColumnCannotHaveRaisesNamingTheObject)
{
	ibMetaDataConfigurationFile cfg1;
	ibValueMetaObjectCatalog* firms = NewCatalog(cfg1, wxT("Firms"));
	ASSERT_NE(firms, nullptr);

	const auto refused = [&](const ibString& property, const ibDataValue& value) -> ibString {
		ibDataNode node;
		if (!firms->SaveNode(node))
			return wxT("not saved");
		node.SetField(property, value);   // replaced: SetProperty appends, and the first one is read
		ibMetaDataConfigurationFile cfg2;
		ibValueMetaObjectCatalog* loaded = NewCatalog(cfg2, wxT("Other"));
		try {
			loaded->LoadNode(node);
		}
		catch (const ibCoreException& err) {
			return err.GetErrorDescription();
		}
		return ibString();
	};

	// 70000 used to become a column of 4464 (an unsigned short), 300 digits a number of 44.
	ibString refusal = refused(wxT("CodeLength"), ibDataValue::UInt(70000));
	EXPECT_NE(refusal.Find(wxT("Firms")), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(wxT("CodeLength")), wxNOT_FOUND) << refusal;
	EXPECT_FALSE(refused(wxT("CodeLength"), ibDataValue::UInt(0)).IsEmpty());
	EXPECT_FALSE(refused(wxT("DescriptionLength"), ibDataValue::UInt(2000)).IsEmpty());
	EXPECT_TRUE(refused(wxT("CodeLength"), ibDataValue::UInt(30)).IsEmpty()) << "a length it can have reads";

	ASSERT_TRUE(Set(firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	refusal = refused(wxT("CodeLength"), ibDataValue::UInt(300));
	EXPECT_NE(refusal.Find(wxT("number")), wxNOT_FOUND) << refusal;
}

// ─── the apply ────────────────────────────────────────────────────────────────────────────────────────

class CodeLengthApply : public ::testing::Test {
protected:
	wxInitializer m_wx;
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayer> m_db;
	ibString m_path;
	std::unique_ptr<ibMetaDataConfigurationFile> m_cfg;
	ibValueMetaObjectCatalog* m_firms = nullptr;

	// The engine the apply meets. SQLite here: it is in every job.
	virtual std::shared_ptr<ibDatabaseLayer> OpenEngine()
	{
		auto db = std::make_shared<ibDatabaseLayerSQLite>();
		m_path = wxFileName::CreateTempFileName(wxT("oescode"));
		return db->Open(m_path) ? db : nullptr;
	}

	void SetUp() override
	{
		if (!m_wx.IsOk())
			GTEST_SKIP() << "wxBase init failed";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		ASSERT_NE(pool, nullptr);
		m_db = OpenEngine();
		if (!m_db)
			GTEST_SKIP() << "the engine is not available on this machine";
		pool->Init(m_db, /*maxSize=*/2, /*minIdle=*/0);

		m_cfg = std::make_unique<ibMetaDataConfigurationFile>();
		m_firms = NewCatalog(*m_cfg, wxT("Firms"));
		ASSERT_NE(m_firms, nullptr);
	}

	void TearDown() override
	{
		if (m_cfg) m_cfg->CloseDatabase();
		m_cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (m_db) m_db->Close();
		m_db.reset();
		if (!m_path.IsEmpty() && wxFileExists(m_path))
			wxRemoveFile(m_path);
	}

	// The whole configuration created, as the first apply creates it. The ledger lines it said.
	int Create(ibRestructureInfo& ledger)
	{
		return Apply(nullptr, m_cfg->BuildSchemaSnapshot(), ledger);
	}

	// was -> next, as an apply runs it.
	static int Apply(const ibSchemaSnapshot* was, const ibSchemaSnapshot& next, ibRestructureInfo& ledger)
	{
		const int ret = DiffSnapshots(was, next, nullptr, &ledger);
		if (db_query->IsActiveTransaction())
			db_query->Commit();
		ibSchemaBuilder(nullptr).Flush();
		if (db_query->IsActiveTransaction())
			db_query->Commit();
		return ret;
	}

	ibString CodeField(ibColumnRole role) const
	{
		for (const ibColumnSlot& slot : DescribeColumnLayout(m_firms->GetDataCode()->GetQueryColumn()))
			if (slot.m_role == role)
				return slot.m_name;
		return ibString();
	}
};

TEST_F(CodeLengthApply, APredefinedCodeTooLongIsRefusedBeforeAnyTableNamingTheItem)
{
	// Declared while it fitted, then the length lowered under it - the one road the engine cannot refuse.
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(9u)));
	m_firms->AppendPredefinedValue(wxT("Main"), wxT("000000001"), wxT("Main firm"));
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(8u)));
	ASSERT_TRUE(m_cfg->RunDatabase());

	ibRestructureInfo ledger;
	EXPECT_NE(Create(ledger), 1);
	ASSERT_TRUE(ledger.HasErrors());
	bool named = false;
	for (const ibRestructureInfo::Entry& entry : ledger)
		if (entry.type == ibRestructure::error && entry.descr.Find(wxT("Firms")) != wxNOT_FOUND
			&& entry.descr.Find(wxT("Main")) != wxNOT_FOUND)
			named = true;
	EXPECT_TRUE(named) << "the refusal names the object and the item";
	EXPECT_FALSE(ibDatabaseQueryBuilder().TableExists(m_firms->GetPhysicalTableName())) << "refused before any DDL";
}

TEST_F(CodeLengthApply, ALongerCodeIsStoredWhole)
{
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(9u)));
	ASSERT_TRUE(m_cfg->RunDatabase());
	m_firms->AppendPredefinedValue(wxT("Main"), wxT("000000001"), wxT("Main firm"));

	ibRestructureInfo ledger;
	ASSERT_EQ(Create(ledger), 1);
	EXPECT_FALSE(ledger.HasErrors());

	const ibString field = CodeField(ibColumnRole::String);
	ASSERT_FALSE(field.IsEmpty());
	ibQueryResult rows = ibDatabaseQueryBuilder().From(m_firms->GetPhysicalTableName()).Select({ field }).Execute();
	ASSERT_TRUE(rows.Next());
	EXPECT_EQ(rows.GetResultString(field), wxT("000000001"));
}

TEST_F(CodeLengthApply, ANumberCodeIsStoredAsANumber)
{
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(9u)));
	ASSERT_TRUE(Set(m_firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	ASSERT_TRUE(m_cfg->RunDatabase());
	m_firms->AppendPredefinedValue(wxT("Main"), wxT("000000042"), wxT("Main firm"));

	ibRestructureInfo ledger;
	ASSERT_EQ(Create(ledger), 1);

	const ibString field = CodeField(ibColumnRole::Number);
	ASSERT_FALSE(field.IsEmpty());
	EXPECT_TRUE(CodeField(ibColumnRole::String).IsEmpty()) << "a number code has no text field";
	ibQueryResult rows = ibDatabaseQueryBuilder().From(m_firms->GetPhysicalTableName()).Select({ field }).Execute();
	ASSERT_TRUE(rows.Next());
	EXPECT_EQ(rows.GetResultNumber(field).ToInt(), 42);
}

// ─── a length lowered below what is stored ───────────────────────────────────────────────────────────

class CodeLengthLowered : public CodeLengthApply {
protected:
	// The table created with the shape as it is, a row with `code` in it, and the declaration as it was
	// saved - what the next apply compares with.
	void CreateHolding(const ibString& code, ibColumnRole role)
	{
		ASSERT_TRUE(m_cfg->RunDatabase());
		ibRestructureInfo created;
		ASSERT_EQ(Create(created), 1);

		const ibString field = CodeField(role);
		ASSERT_FALSE(field.IsEmpty());
		const ibString literal = role == ibColumnRole::String ? wxT("'") + code + wxT("'") : code;
		ASSERT_GE(db_query->RunQuery(wxT("%s"), ibString::Format(wxT("INSERT INTO %s (%s) VALUES (%s)"),
			m_firms->GetPhysicalTableName(), field, literal)), 0);
		if (db_query->IsActiveTransaction())
			db_query->Commit();

		ibMemoryBuffer saved;
		ASSERT_TRUE(m_cfg->SaveConfigToBuffer(saved));
		m_was = std::make_unique<ibMetaDataConfigurationFile>();
		ASSERT_TRUE(m_was->LoadConfigFromBuffer(saved));
		ASSERT_TRUE(m_was->RunDatabase());
	}

	// The apply refused, before any DDL, in words that name the object and `property`.
	void ExpectRefusedNaming(const ibString& property)
	{
		const ibSchemaSnapshot before = m_was->BuildSchemaSnapshot();
		ibRestructureInfo ledger;
		EXPECT_NE(Apply(&before, m_cfg->BuildSchemaSnapshot(), ledger), 1);
		bool named = false;
		for (const ibRestructureInfo::Entry& entry : ledger)
			if (entry.type == ibRestructure::error && entry.descr.Find(wxT("Firms")) != wxNOT_FOUND
				&& entry.descr.Find(property) != wxNOT_FOUND)
				named = true;
		EXPECT_TRUE(named) << "the refusal names the object and " << property;
	}

	void TearDown() override
	{
		if (m_was) m_was->CloseDatabase();
		m_was.reset();
		CodeLengthApply::TearDown();
	}

	std::unique_ptr<ibMetaDataConfigurationFile> m_was;
};

TEST_F(CodeLengthLowered, AStringCodeLongerThanTheNewLengthIsRefusedByName)
{
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(30u)));
	CreateHolding(wxT("ABCDEFGHIJ"), ibColumnRole::String);

	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(8u)));
	ExpectRefusedNaming(wxT("CodeLength"));
}

TEST_F(CodeLengthLowered, ANumberCodeOfMoreDigitsThanTheNewLengthIsRefusedByName)
{
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(9u)));
	ASSERT_TRUE(Set(m_firms, wxT("CodeType"), CodeTypeValue(ibCodeType_Number)));
	CreateHolding(wxT("12345"), ibColumnRole::Number);

	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(4u)));
	ExpectRefusedNaming(wxT("CodeLength"));
}

TEST_F(CodeLengthLowered, AnApplyThatLowersNothingReadsNoRows)
{
	// The same declaration on both sides: the rule compares with the table as it stands and passes.
	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(30u)));
	CreateHolding(wxT("ABCDEFGHIJ"), ibColumnRole::String);

	const ibSchemaSnapshot before = m_was->BuildSchemaSnapshot();
	ibRestructureInfo ledger;
	EXPECT_EQ(Apply(&before, m_cfg->BuildSchemaSnapshot(), ledger), 1);
	EXPECT_FALSE(ledger.HasErrors());
}

// ─── a longer code on a table that holds codes — the restructuring itself ─────────────────────────────
//
// SQLite cannot change a column's type in place, so the ALTER is met where it is real: Firebird, in the
// job that carries the kit (its filter is `Firebird*`, and a skip there fails the job).

class FirebirdCodeLength : public CodeLengthApply {
protected:
	std::shared_ptr<ibDatabaseLayer> OpenEngine() override
	{
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		m_path = wxFileName(wxStandardPaths::Get().GetTempDir(),
			ibString::Format(wxT("oes_fb_code_%lld.fdb"), (long long)now)).GetFullPath();
		try {
			auto db = std::make_shared<ibDatabaseLayerFirebird>();
			db->SetUser(wxT("SYSDBA"));
			db->SetPassword(wxT("masterkey"));
			return db->Open(m_path) ? db : nullptr;
		}
		catch (...) {
			return nullptr;   // no client on this machine
		}
	}
};

TEST_F(FirebirdCodeLength, ALongerCodeLengthAltersTheColumnAndKeepsTheCodes)
{
	ASSERT_TRUE(m_cfg->RunDatabase());
	ibRestructureInfo created;
	ASSERT_EQ(Create(created), 1);

	const ibString table = m_firms->GetPhysicalTableName();
	const ibString field = CodeField(ibColumnRole::String);
	ASSERT_FALSE(field.IsEmpty());
	ASSERT_GE(db_query->RunQuery(wxT("%s"),
		ibString::Format(wxT("INSERT INTO %s (%s) VALUES ('00000001')"), table, field)), 0);
	if (db_query->IsActiveTransaction())
		db_query->Commit();

	// The declaration as it was saved, and as it is now: the snapshot holds the live columns, so the
	// one it was is read off a copy of the configuration.
	ibMemoryBuffer saved;
	ASSERT_TRUE(m_cfg->SaveConfigToBuffer(saved));
	ibMetaDataConfigurationFile was;
	ASSERT_TRUE(was.LoadConfigFromBuffer(saved));
	ASSERT_TRUE(was.RunDatabase());   // run, as the saved one is: its references are typed on the run
	const ibSchemaSnapshot before = was.BuildSchemaSnapshot();

	ASSERT_TRUE(Set(m_firms, wxT("CodeLength"), ibVariant(9u)));
	ibRestructureInfo ledger;
	ASSERT_EQ(Apply(&before, m_cfg->BuildSchemaSnapshot(), ledger), 1);
	EXPECT_FALSE(ledger.HasErrors());

	ASSERT_GE(db_query->RunQuery(wxT("%s"),
		ibString::Format(wxT("INSERT INTO %s (%s) VALUES ('000000002')"), table, field)), 0);
	if (db_query->IsActiveTransaction())
		db_query->Commit();

	std::vector<ibString> codes;
	ibQueryResult rows = ibDatabaseQueryBuilder().From(table).Select({ field }).Execute();
	while (rows.Next())
		codes.push_back(rows.GetResultString(field));
	ASSERT_EQ(codes.size(), 2u);
	EXPECT_NE(std::find(codes.begin(), codes.end(), ibString(wxT("00000001"))), codes.end()) << "the stored code is kept";
	EXPECT_NE(std::find(codes.begin(), codes.end(), ibString(wxT("000000002"))), codes.end()) << "the longer one fits";
}
