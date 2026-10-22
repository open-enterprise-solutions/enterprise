// A document's NumberPeriodicity: readable on the metadata object, kept when the configuration
// is saved, and the series the next number is taken from. Nonperiodical stays on the bucket every
// number allocated before the setting existed (20000101). Year, quarter, month and day each start
// again when that period of the document's date changes.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/query/columnLayout.h"
#include "core/serialize/dataBuilder.h"

namespace {

ibValueMetaObjectDocument* MakeDocument(ibMetaDataConfigurationFile& cfg)
{
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	return dynamic_cast<ibValueMetaObjectDocument*>(
		cfg.CreateMetaObject(g_metaDocumentCLSID, root, /*runObject*/ false));
}

void SetPeriod(ibValueMetaObjectDocument* doc, ibNumberPeriodicity period)
{
	ibProperty* prop = doc->GetProperty(wxT("NumberPeriodicity"));
	ASSERT_NE(prop, nullptr);
	ASSERT_TRUE(prop->SetDataValue(
		ibValue::CreateEnumObject<ibValueEnumNumberPeriodicity>(period)));
}

ibValue ReadPeriod(ibValueMetaObjectDocument* doc)
{
	const long idx = doc->FindProp(wxT("NumberPeriodicity"));
	EXPECT_GE(idx, 0);
	ibValue value;
	EXPECT_TRUE(doc->GetPropVal(idx, value));
	return value;
}

bool SameEnum(const ibValue& value, ibNumberPeriodicity period)
{
	return value.CompareValueEQ(
		ibValue::CreateEnumObject<ibValueEnumNumberPeriodicity>(period));
}

} // namespace

TEST(NumberPeriodicity, TheWordsFoldToTheBucketsAboveTheOldSeries)
{
	const ibDateTime march(2026, 3, 31);
	const ibDateTime april(2026, 4, 1);
	EXPECT_EQ(ibNumberSeriesNonperiodical, ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical, march));
	EXPECT_EQ(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Year, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Year, april));
	EXPECT_NE(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Year, march), ibNumberSeriesNonperiodical);
	EXPECT_EQ(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Quarter, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Quarter, ibDateTime(2026, 1, 1)));
	EXPECT_NE(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Quarter, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Quarter, april));
	EXPECT_EQ(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Month, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Month, ibDateTime(2026, 3, 1)));
	EXPECT_NE(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Month, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Month, april));
	EXPECT_EQ(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Day, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Day, ibDateTime(2026, 3, 31, 23, 59, 59)));
	EXPECT_NE(ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Day, march),
	          ibDocumentNumberInterval(ibNumberPeriodicity::ibNumberPeriodicity_Day, april));
}

TEST(NumberPeriodicity, ANewDocumentReadsNonperiodical)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectDocument* doc = MakeDocument(cfg);
	ASSERT_NE(doc, nullptr);
	EXPECT_EQ(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical, doc->GetNumberPeriodicity());
	EXPECT_TRUE(SameEnum(ReadPeriod(doc), ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical));
}

TEST(NumberPeriodicity, TheSettingRoundTrips)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectDocument* doc = MakeDocument(cfg);
	ASSERT_NE(doc, nullptr);
	SetPeriod(doc, ibNumberPeriodicity::ibNumberPeriodicity_Year);

	ibDataNode saved;
	ASSERT_TRUE(doc->SaveNode(saved));
	SetPeriod(doc, ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical);
	ASSERT_TRUE(doc->LoadNode(saved));
	EXPECT_EQ(ibNumberPeriodicity::ibNumberPeriodicity_Year, doc->GetNumberPeriodicity());
	EXPECT_TRUE(SameEnum(ReadPeriod(doc), ibNumberPeriodicity::ibNumberPeriodicity_Year));

	// Absent from a configuration saved before the setting existed. Loaded into a new document,
	// whose constructor default is nonperiodical: an empty property does not write over that.
	ibDataNode older;
	for (const auto& field : saved.Fields())
		older.AddField(field.first, field.second);
	for (const auto& prop : saved.Properties()) {
		if (prop.first != wxT("NumberPeriodicity"))
			older.SetProperty(prop.first, prop.second);
	}
	ibValueMetaObjectDocument* fresh = MakeDocument(cfg);
	ASSERT_NE(fresh, nullptr);
	ASSERT_TRUE(fresh->LoadNode(older));
	EXPECT_EQ(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical, fresh->GetNumberPeriodicity());
}

namespace {

// Reaches the protected number generator. A full Write would also build every column of the
// document; the series itself only needs the sequence table and the date/number columns.
class NumberProbe : public ibValueRecordDataObjectDocument {
public:
	explicit NumberProbe(const ibValueMetaObjectDocument* meta)
		: ibValueRecordDataObjectDocument(meta) {}
	void Prepare() { PrepareEmptyObject(); }
	void SetWhen(const ibDateTime& when) {
		SetValueByMetaID(GetMetaObject()->GetDocumentDate()->GetMetaID(), ibValue(when));
	}
	wxString TakeNumber() {
		EXPECT_TRUE(GenerateUniqueIdentifier(wxEmptyString));
		ibValue number;
		EXPECT_TRUE(GetValueByMetaID(GetMetaObject()->GetDocumentNumber()->GetMetaID(), number));
		return number.GetString();
	}
};

struct NumberSeries : ::testing::Test {
	wxInitializer                          m_wxInit;
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectDocument* doc = nullptr;
	wxString numberField;
	wxString dateField;
	wxString table;

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
		doc = MakeDocument(*cfg);
		ASSERT_NE(doc, nullptr);
		ASSERT_TRUE(cfg->RunDatabase());

		for (const ibColumnSlot& slot : DescribeColumnLayout(doc->GetDocumentNumber()->GetQueryColumn()))
			if (slot.m_role == ibColumnRole::String) numberField = slot.m_name;
		for (const ibColumnSlot& slot : DescribeColumnLayout(doc->GetDocumentDate()->GetQueryColumn()))
			if (slot.m_role == ibColumnRole::Date) dateField = slot.m_name;
		ASSERT_FALSE(numberField.IsEmpty());
		ASSERT_FALSE(dateField.IsEmpty());
		table = doc->GetPhysicalTableName();

		db->RunQuery(wxT("%s"), wxString::Format(
			wxT("CREATE TABLE %s (%s TEXT, %s TEXT)"), table, numberField, dateField));
		db->RunQuery(wxT("%s"),
			wxT("CREATE TABLE sys_sequence (interval INTEGER, meta_guid TEXT, prefix TEXT, number INTEGER)"));
		db->RunQuery(wxT("%s"),
			wxT("CREATE UNIQUE INDEX sequence_index ON sys_sequence (interval, meta_guid, prefix)"));
	}
	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	void Use(ibNumberPeriodicity period) { SetPeriod(doc, period); }

	wxString Padded(int n) const {
		const int width = doc->GetDocumentNumber()->GetTypeDesc().GetLength();
		return wxString::Format(wxT("%0*d"), width, n);
	}
	void Plant(const ibDateTime& when, int n) {
		ibDatabaseQueryBuilder insert;
		ASSERT_GE(insert.Execute(ibInsert(table, {
			{ numberField, ibConst(ibValue(Padded(n))) },
			{ dateField,   ibConst(ibValue(when)) },
		})), 1);
	}
	wxString Next(const ibDateTime& when) {
		NumberProbe probe(doc);
		probe.Prepare();
		probe.SetWhen(when);
		return probe.TakeNumber();
	}
};

} // namespace

// The row already in this year is seen; the next year starts again, and then continues on its own.
TEST_F(NumberSeries, AYearStartsAgainAndThenContinues)
{
	Use(ibNumberPeriodicity::ibNumberPeriodicity_Year);
	Plant(ibDateTime(2024, 6, 1), 5);
	EXPECT_EQ(Padded(6), Next(ibDateTime(2024, 8, 1)));
	EXPECT_EQ(Padded(7), Next(ibDateTime(2024, 9, 1)));
	EXPECT_EQ(Padded(1), Next(ibDateTime(2025, 1, 15)));
	EXPECT_EQ(Padded(2), Next(ibDateTime(2025, 2, 1)));
}

TEST_F(NumberSeries, AQuarterStartsAgainInApril)
{
	Use(ibNumberPeriodicity::ibNumberPeriodicity_Quarter);
	Plant(ibDateTime(2026, 3, 15), 4);
	EXPECT_EQ(Padded(5), Next(ibDateTime(2026, 3, 20)));
	EXPECT_EQ(Padded(1), Next(ibDateTime(2026, 4, 1)));
}

// No period: a later year continues the same series, including a number already stored.
TEST_F(NumberSeries, NonperiodicalKeepsOneSeriesAcrossYears)
{
	Use(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical);
	Plant(ibDateTime(2024, 6, 1), 5);
	EXPECT_EQ(Padded(6), Next(ibDateTime(2026, 1, 1)));
	EXPECT_EQ(Padded(7), Next(ibDateTime(2024, 1, 1)));
}
