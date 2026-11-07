// =============================================================================
// FullTextSearch - the base's mode, kept in the base; the words of a text (ibFullTextTokenizer); and the index over a
// catalog (ibFullTextIndex): the build after a flag or a clear, a record's words replaced and dropped, the search by
// prefix and fuzzily, and what is refused. The base is SQLite with the tables a base's opening makes.
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
#include "backend/fullTextSearch/fullTextIndex.h"
#include "backend/fullTextSearch/fullTextTokenizer.h"
#include "backend/compiler/compileCode.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/propertyManager/property/propertyBoolean.h"
#include "backend/query/columnLayout.h"
#include "backend/query/queryProvider.h"
#include "core/clsid.h"
#include "core/diagnostics/journal.h"
#include "backend/databaseLayer/connectionHolder.h"

#include <cstdio>

#include <set>

#include <wx/filename.h>

#include "activeMetaDataAccess.h"

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
		ibFullTextIndex::CreateTables();
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

TEST_F(FullTextSearch, WithTheSearchSwitchedOffThereIsNothingToBuild)
{
	ibValueFullTextSearchManager manager;
	EXPECT_FALSE(Call(manager, ibT("IndexTrue")).GetBoolean());
	EXPECT_THROW(ibValueFullTextSearchManager::UpdateIndex(false), ibBackendException);
	// Clearing what is not there is no error.
	Call(manager, ibT("ClearIndex"));

	const ibValue updated = Call(manager, ibT("UpdateDate"));
	EXPECT_EQ(updated.GetType(), ibValueTypes::TYPE_DATE);
	EXPECT_TRUE(updated.GetDate().IsEmpty());
	EXPECT_EQ(ibValueFullTextSearchManager::GetMode(), ibFullTextSearchMode_Disable);
}

TEST_F(FullTextSearch, UpdateIndexWithTwoArgumentsIsRefusedAndSaysWhy)
{
	// A call ported from where the first argument was the merge: its second would be lost, its first read as portion.
	ibValueFullTextSearchManager manager;
	ibValue first(false), second(true);
	ibValue* params[] = { &first, &second };
	try {
		manager.CallAsProc(manager.FindMethod(ibT("UpdateIndex")), params, 2);
		ADD_FAILURE() << "two arguments were taken";
	}
	catch (const ibBackendException& err) {
		EXPECT_NE(err.GetErrorDescription().Find(ibT("portion")), wxNOT_FOUND);
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

// =============================================================================
// The words of a text
// =============================================================================

TEST(FullTextTokenizer, AWordIsLettersAndDigitsLowerCased)
{
	const std::vector<ibString> words = ibFullTextTokenizer::Split(ibT("Wheel, BRAKE-disc; wheel"));
	ASSERT_EQ(words.size(), 4u);
	EXPECT_EQ(words[0], ibT("wheel"));
	EXPECT_EQ(words[1], ibT("brake"));
	EXPECT_EQ(words[2], ibT("disc"));
	EXPECT_EQ(words[3], ibT("wheel"));
}

TEST(FullTextTokenizer, ASizeBetweenDigitsStaysOneWord)
{
	const std::vector<ibString> words = ibFullTextTokenizer::Split(ibT("tyre 205/55 R16, oil 1.6"));
	ASSERT_EQ(words.size(), 5u);
	EXPECT_EQ(words[1], ibT("205/55"));
	EXPECT_EQ(words[2], ibT("r16"));
	EXPECT_EQ(words[4], ibT("1.6"));
}

TEST(FullTextTokenizer, CyrillicIsFoldedWhateverTheLocale)
{
	const std::vector<ibString> words = ibFullTextTokenizer::Split(ibString::FromUTF8("\xD0\x81\xD0\x96 \xD0\xA8\xD0\x98\xD0\x9D\xD0\x90"));
	ASSERT_EQ(words.size(), 2u);
	EXPECT_EQ(words[0], ibString::FromUTF8("\xD0\xB5\xD0\xB6"));   // ё folded to е
	EXPECT_EQ(words[1], ibString::FromUTF8("\xD1\x88\xD0\xB8\xD0\xBD\xD0\xB0"));
}

TEST(FullTextTokenizer, TheDistanceStopsPastItsLimit)
{
	EXPECT_EQ(ibFullTextTokenizer::Distance(ibT("wheel"), ibT("wheel"), 2), 0u);
	EXPECT_EQ(ibFullTextTokenizer::Distance(ibT("wheel"), ibT("wehel"), 2), 2u);
	EXPECT_EQ(ibFullTextTokenizer::Distance(ibT("wheel"), ibT("wheell"), 2), 1u);
	EXPECT_EQ(ibFullTextTokenizer::Distance(ibT("wheel"), ibT("brake"), 1), 2u);
}

// =============================================================================
// The index over a catalog
// =============================================================================

namespace {

ibString SqliteTypeOf(const ibString& field)
{
	if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
	if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
	if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
	return wxT(" INTEGER");
}

// A base with one catalog, Goods: its Description and a string attribute Note are indexed, a string attribute Code
// of its own is not. Its table is made the way the acceptance tests make theirs; the index's tables as a base makes them.
struct FullTextIndexBase : ::testing::Test {
	wxInitializer m_wxInit;
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> m_db;
	ibString m_dbPath;
	std::shared_ptr<ibMetaDataConfigurationFile> m_cfg;
	ibValueMetaObjectCatalog* m_goods = nullptr;
	ibValueMetaObjectAttribute* m_note = nullptr;
	ibValueMetaObjectAttribute* m_article = nullptr;
	bool m_ownsApp = false;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		m_ownsApp = true;
		ibConnectionPool* const pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr || ibApplicationInstance::GetSettingsStorage() == nullptr)
			GTEST_SKIP() << "no connection pool or settings storage after CreateAppDataEnv";
		m_db = std::make_shared<ibDatabaseLayerSQLite>();
		m_dbPath = wxFileName::CreateTempFileName(wxT("oesfts"));
		if (!m_db->Open(m_dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(m_db, /*maxSize=*/4, /*minIdle=*/0);
		SettingsSchemaAccess::Create();
		ibFullTextIndex::CreateTables();

		m_cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
		ActiveMetaDataAccess::Replace(m_cfg);
		ibValueMetaObjectConfiguration* const root = m_cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		m_goods = dynamic_cast<ibValueMetaObjectCatalog*>(m_cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		ASSERT_NE(m_goods, nullptr);
		m_goods->SetName(wxT("Goods"));
		m_goods->SetHierarchyType(ibHierarchyType::eNone);
		m_note = AddString(wxT("Note"), true);
		m_article = AddString(wxT("Article"), false);
		m_goods->GetDataDescription()->SetFullTextSearch(true);

		ASSERT_TRUE(m_cfg->RunDatabase()) << "the configuration in memory did not run";
		CreateTable();
		ibFullTextIndex::SetMode(ibFullTextSearchMode_Enable);
	}

	void TearDown() override {
		if (m_cfg) m_cfg->CloseDatabase();
		m_cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (m_ownsApp && ibApplicationInstance::Get(false) != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (m_db) m_db->Close();
		m_db.reset();
		if (!m_dbPath.IsEmpty()) {
			for (const wxChar* suffix : { wxT(""), wxT("-wal"), wxT("-shm") })
				if (wxFileExists(m_dbPath + suffix)) wxRemoveFile(m_dbPath + suffix);
		}
	}

	ibValueMetaObjectAttribute* AddString(const ibString& name, bool indexed) {
		auto* const attribute = dynamic_cast<ibValueMetaObjectAttribute*>(
			m_cfg->CreateMetaObject(metadata_to_clsid(wxT("MD_ATTR")), m_goods, false));
		EXPECT_NE(attribute, nullptr);
		if (attribute == nullptr)
			return nullptr;
		m_cfg->RenameMetaObject(attribute, name);
		attribute->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		SetIndexed(attribute, indexed);
		return attribute;
	}

	static void SetIndexed(ibValueMetaObjectAttribute* attribute, bool indexed) {
		auto* const flag = dynamic_cast<ibPropertyBoolean*>(attribute->GetProperty(ibT("FullTextSearch")));
		ASSERT_NE(flag, nullptr) << "the attribute has no Full-text search property";
		flag->SetValue(indexed);
	}

	void CreateTable() {
		ibString list;
		std::set<ibString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : m_goods->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const ibString& field : ColumnFieldNames(attribute->GetQueryColumn())) {
				if (!seen.insert(field).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
			}
		}
		ASSERT_FALSE(list.IsEmpty());
		m_db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + m_goods->GetQueryable()->GetQueryTableName() + wxT(" (") + list + wxT(")"));
	}

	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, m_cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	// A record written before the index knew of it - straight into the table, as one written while the search was off.
	ibGuid Insert(const ibString& description, const ibString& note, const ibString& article = ibString()) {
		const ibGuid guid(ibGuid::newGuid());
		std::vector<ibDmlAssign> row;
		Assign(row, m_goods->GetDataReference()->GetQueryColumn(), ibValue(ibValueReferenceDataObject::Create(m_cfg.get(), m_goods->GetMetaID(), guid)));
		Assign(row, m_goods->GetDataDeletionMark()->GetQueryColumn(), ibValue(false));
		Assign(row, m_goods->GetDataCode()->GetQueryColumn(), ibValue(guid.str().Left(9)));
		Assign(row, m_goods->GetDataDescription()->GetQueryColumn(), ibValue(description));
		Assign(row, m_goods->GetDataPredefinedName()->GetQueryColumn(), ibValue(ibString()));
		Assign(row, m_note->GetQueryColumn(), ibValue(note));
		Assign(row, m_article->GetQueryColumn(), ibValue(article));
		ibDatabaseQueryBuilder insert;
		EXPECT_GE(insert.Execute(ibInsert(m_goods->GetQueryable()->GetQueryTableName(), row)), 0);
		return guid;
	}

	void SetNote(const ibGuid& guid, const ibString& note) {
		std::vector<ibDmlAssign> key, value;
		Assign(key, m_goods->GetDataReference()->GetQueryColumn(), ibValue(ibValueReferenceDataObject::Create(m_cfg.get(), m_goods->GetMetaID(), guid)));
		Assign(value, m_note->GetQueryColumn(), ibValue(note));
		ibQueryExprPtr where;
		for (const ibDmlAssign& one : key) {
			const ibQueryExprPtr term = ibBinOp(ibQueryBinOp::Eq, ibCol(one.m_column), one.m_value);
			where = where ? ibBinOp(ibQueryBinOp::And, where, term) : term;
		}
		ibDatabaseQueryBuilder update;
		EXPECT_GE(update.Execute(ibUpdate(m_goods->GetQueryable()->GetQueryTableName(), value, where)), 0);
	}

	std::vector<ibGuid> Found(const ibString& text, int threshold = ibFullTextIndex::kDefaultFuzzyThreshold) const {
		std::vector<ibGuid> keys;
		for (const ibFullTextIndex::ibHit& hit : ibFullTextIndex::Search(m_cfg.get(), text, threshold))
			keys.push_back(hit.m_key);
		return keys;
	}

	std::set<ibGuid> FoundSet(const ibString& text) const {
		const std::vector<ibGuid> keys = Found(text);
		return std::set<ibGuid>(keys.begin(), keys.end());
	}
};

} // namespace

TEST_F(FullTextIndexBase, RecordsWrittenBeforeTheFlagAreBuiltInPortions)
{
	Insert(wxT("Front wheel"), wxT("steel rim"));
	Insert(wxT("Brake disc"), wxT("ventilated"));
	EXPECT_FALSE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	EXPECT_TRUE(ibFullTextIndex::GetUpdateDate(m_cfg.get()).IsEmpty());

	const ibFullTextIndex::ibProgress progress = ibFullTextIndex::Update(m_cfg.get(), /*portion=*/true);
	EXPECT_EQ(progress.m_total, 2);
	EXPECT_EQ(progress.m_indexed, 2);
	EXPECT_TRUE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	EXPECT_FALSE(ibFullTextIndex::GetUpdateDate(m_cfg.get()).IsEmpty());
}

TEST_F(FullTextIndexBase, EveryWordIsABeginningOrAMisspelling)
{
	const ibGuid wheel = Insert(wxT("Front wheel"), wxT("steel rim"));
	const ibGuid disc = Insert(wxT("Brake disc"), wxT("for the front axle"));
	ibFullTextIndex::Update(m_cfg.get(), false);

	EXPECT_EQ(FoundSet(wxT("whe")), std::set<ibGuid>{ wheel });          // a beginning
	EXPECT_EQ(FoundSet(wxT("wheal")), std::set<ibGuid>{ wheel });        // one letter wrong
	EXPECT_EQ(FoundSet(wxT("wheell")), std::set<ibGuid>{ wheel });       // one letter too many
	EXPECT_EQ(FoundSet(wxT("WHEEL steel")), std::set<ibGuid>{ wheel });  // every word, any case
	EXPECT_EQ(FoundSet(wxT("front")), (std::set<ibGuid>{ wheel, disc }));
	EXPECT_TRUE(FoundSet(wxT("wheel axle")).empty());                    // no record holds both
	EXPECT_TRUE(Found(wxT("wheal"), 0).empty());                         // no misspelling at threshold 0
	EXPECT_TRUE(FoundSet(wxT("wehel")).empty());                         // two edits are more than a five-letter word's one
}

TEST_F(FullTextIndexBase, OnlyTheFlaggedFieldsAreIndexed)
{
	Insert(wxT("Front wheel"), wxT("steel rim"), wxT("ZX-4410"));
	ibFullTextIndex::Update(m_cfg.get(), false);
	EXPECT_TRUE(FoundSet(wxT("zx")).empty());
	EXPECT_EQ(FoundSet(wxT("rim")).size(), 1u);
}

TEST_F(FullTextIndexBase, TheExactWordRanksAboveTheMisspeltOne)
{
	const ibGuid fuzzy = Insert(wxT("Wheal"), wxT(""));
	const ibGuid exact = Insert(wxT("Wheel"), wxT("wheel"));
	ibFullTextIndex::Update(m_cfg.get(), false);
	const std::vector<ibGuid> found = Found(wxT("wheel"));
	ASSERT_EQ(found.size(), 2u);
	EXPECT_EQ(found[0], exact);
	EXPECT_EQ(found[1], fuzzy);
}

TEST_F(FullTextIndexBase, ARecordWrittenAgainReplacesItsWordsAndADeleteDropsThem)
{
	const ibGuid guid = Insert(wxT("Front wheel"), wxT("steel rim"));
	ibFullTextIndex::Update(m_cfg.get(), false);

	// The record written again: its note changed in the table, its words replaced from what is stored.
	SetNote(guid, wxT("alloy rim"));
	ibFullTextIndex::IndexStoredRecord(m_goods, guid);
	EXPECT_TRUE(FoundSet(wxT("steel")).empty());
	EXPECT_EQ(FoundSet(wxT("alloy")), std::set<ibGuid>{ guid });

	ibFullTextIndex::DropRecord(m_goods, guid);
	EXPECT_TRUE(FoundSet(wxT("alloy")).empty());
	EXPECT_TRUE(FoundSet(wxT("wheel")).empty());
}

TEST_F(FullTextIndexBase, AFlagSwitchedOffMakesTheIndexUntrueUntilItIsBuilt)
{
	Insert(wxT("Front wheel"), wxT("steel rim"));
	ibFullTextIndex::Update(m_cfg.get(), false);
	ASSERT_TRUE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));

	SetIndexed(m_note, false);
	EXPECT_FALSE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	ibFullTextIndex::Update(m_cfg.get(), false);
	EXPECT_TRUE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	EXPECT_TRUE(FoundSet(wxT("steel")).empty());
	EXPECT_EQ(FoundSet(wxT("wheel")).size(), 1u);
}

TEST_F(FullTextIndexBase, ClearEmptiesAndTheModeOffClearsToo)
{
	Insert(wxT("Front wheel"), wxT("steel rim"));
	ibFullTextIndex::Update(m_cfg.get(), false);

	ibFullTextIndex::Clear();
	EXPECT_FALSE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	EXPECT_TRUE(FoundSet(wxT("wheel")).empty());
	EXPECT_EQ(ibFullTextIndex::GetProgress(m_cfg.get()).m_indexed, 0);

	ibFullTextIndex::Update(m_cfg.get(), false);
	ibFullTextIndex::SetMode(ibFullTextSearchMode_Disable);
	EXPECT_THROW(ibFullTextIndex::Search(m_cfg.get(), wxT("wheel")), ibBackendException);
	ibFullTextIndex::SetMode(ibFullTextSearchMode_Enable);
	EXPECT_FALSE(ibFullTextIndex::IsIndexTrue(m_cfg.get()));
	EXPECT_TRUE(FoundSet(wxT("wheel")).empty());
}

TEST_F(FullTextIndexBase, ATextWithNoWordAndAThresholdOutOfRangeAreRefused)
{
	EXPECT_THROW(ibFullTextIndex::Search(m_cfg.get(), wxT(" , - ")), ibBackendException);
	EXPECT_THROW(ibFullTextIndex::Search(m_cfg.get(), wxT("wheel"), 51), ibBackendException);
	EXPECT_THROW(ibFullTextIndex::Search(m_cfg.get(), wxT("wheel"), -1), ibBackendException);
}

TEST_F(FullTextIndexBase, AListIsReadAPartAtATime)
{
	for (int idx = 0; idx < 5; idx++)
		Insert(ibString::Format(wxT("Wheel %d"), idx), wxT(""));
	ibFullTextIndex::Update(m_cfg.get(), false);

	ibValueFullTextSearchList list(wxT("wheel"), 2);
	EXPECT_THROW(list.Count(), ibBackendException);   // nothing searched yet
	list.FirstPart();
	EXPECT_EQ(list.TotalCount(), 5);
	EXPECT_EQ(list.Count(), 2);
	EXPECT_THROW(list.PreviousPart(), ibBackendException);
	list.NextPart();
	list.NextPart();
	EXPECT_EQ(list.StartIndex(), 4);
	EXPECT_EQ(list.Count(), 1);
	EXPECT_THROW(list.NextPart(), ibBackendException);
	EXPECT_THROW(list.Get(1), ibBackendException);
	EXPECT_NE(list.Get(0).GetString().Find(wxT("Wheel")), wxNOT_FOUND);
}

TEST_F(FullTextIndexBase, TheFlagsAreSavedWithTheConfiguration)
{
	ibMemoryBuffer buffer;
	ASSERT_TRUE(m_cfg->SaveConfigToBuffer(buffer));
	ibMetaDataConfigurationFile loaded;
	ASSERT_TRUE(loaded.LoadConfigFromBuffer(buffer));

	const ibValueMetaObjectCatalog* goods = nullptr;
	for (const ibValueMetaObjectCatalog* catalog : loaded.GetAnyArrayObject<ibValueMetaObjectCatalog>(g_metaCatalogCLSID))
		if (catalog->GetName() == wxT("Goods")) goods = catalog;
	ASSERT_NE(goods, nullptr);
	EXPECT_TRUE(goods->GetDataDescription()->GetFullTextSearch());
	std::set<ibString> indexed;
	for (const ibValueMetaObjectAttributeBase* attribute : ibFullTextIndex::GetIndexedAttributes(goods))
		indexed.insert(attribute->GetName());
	EXPECT_EQ(indexed, (std::set<ibString>{ wxT("Note"), wxT("Description") }));
}

// ⏱ ONE CATALOG WRITE WITH THE INDEX AND WITHOUT - DISABLED, run on demand in a Debug build (ibJournalStopwatch is
// Debug-only):  oes_tests --gtest_also_run_disabled_tests --gtest_filter=*FullTextIndexBase.DISABLED_Bench*
// Each record in a transaction of its own, as a write is: the row alone; then the record read back as the base keeps
// it and indexed (what SaveData adds is the indexing - IndexStoredRecord also reads, so the read is timed alone too,
// with the mode switched off, and taken away).
TEST_F(FullTextIndexBase, DISABLED_BenchOneWriteWithTheIndexAndWithout)
{
	constexpr int kRecords = 2000;
	auto record = [](int i) {
		return std::make_pair(wxString::Format(wxT("Front wheel %d steel"), i), wxString::Format(wxT("rim 205/55 R16 batch %d"), i));
	};
	std::vector<ibGuid> keys;
	keys.reserve(kRecords);
	ibJournalStopwatch row, readOnly, readAndIndex;
	for (int i = 0; i < kRecords; ++i) {
		const auto [description, note] = record(i);
		ibConnectionScope scope;
		scope.SafeBeginTransaction();
		row.Resume();
		keys.push_back(Insert(description, note));
		row.Pause();
		scope.SafeCommitTransaction();
	}
	ibFullTextIndex::SetMode(ibFullTextSearchMode_Disable);
	for (const ibGuid& key : keys) {
		ibConnectionScope scope;
		scope.SafeBeginTransaction();
		readOnly.Resume();
		ibFullTextIndex::IndexStoredRecord(m_goods, key);
		readOnly.Pause();
		scope.SafeCommitTransaction();
	}
	ibFullTextIndex::SetMode(ibFullTextSearchMode_Enable);
	for (const ibGuid& key : keys) {
		ibConnectionScope scope;
		scope.SafeBeginTransaction();
		readAndIndex.Resume();
		ibFullTextIndex::IndexStoredRecord(m_goods, key);
		readAndIndex.Pause();
		scope.SafeCommitTransaction();
	}
	EXPECT_EQ(Found(wxT("wheel")).size(), static_cast<size_t>(kRecords));
	const double perRow = double(row.Ms()) * 1000.0 / kRecords;
	const double perIndex = double(readAndIndex.Ms() - readOnly.Ms()) * 1000.0 / kRecords;
	std::printf("[ BENCH    ] %d records: row %lld ms (%.0f us each); read %lld ms; read+index %lld ms -> index %.0f us each, "
		"a write with the index %.2fx the row alone\n", kRecords, row.Ms(), perRow, readOnly.Ms(), readAndIndex.Ms(), perIndex,
		perRow > 0 ? (perRow + perIndex) / perRow : 0.0);
}
