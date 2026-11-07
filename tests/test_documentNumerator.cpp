// A DOCUMENT NUMERATOR (#622): one number series several kinds of document take their numbers from.
//
// The numerator is kept when the configuration is saved, a document that names it answers the
// numerator's number type and periodicity, and the documents that name it share one counter: the
// next number of either kind continues the other's, the first number of a series reads every member
// table, and a number a document of the other kind already has is refused with that document named.
// A numerator a document numbers from is not deleted.

#include <gtest/gtest.h>

#include <set>

#include <wx/filename.h>
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/partial/document.h"
#include "backend/metaCollection/partial/documentNumerator.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/propertyManager/property/propertyMetaObjectChoice.h"
#include "backend/query/columnLayout.h"
#include "backend/query/queryProvider.h"
#include "core/serialize/dataBuilder.h"

namespace {

ibValueMetaObjectDocumentNumerator* MakeNumerator(ibMetaDataConfigurationFile& cfg)
{
	return dynamic_cast<ibValueMetaObjectDocumentNumerator*>(
		cfg.CreateMetaObject(g_metaDocumentNumeratorCLSID, cfg.GetCommonMetaObject(), /*runObject*/ false));
}

ibValueMetaObjectDocument* MakeDocument(ibMetaDataConfigurationFile& cfg, const ibString& name)
{
	auto* doc = dynamic_cast<ibValueMetaObjectDocument*>(
		cfg.CreateMetaObject(g_metaDocumentCLSID, cfg.GetCommonMetaObject(), /*runObject*/ false));
	if (doc != nullptr)
		doc->SetName(name);
	return doc;
}

// The way the inspector and metadata_bind set it: the property's own value, a metadata description.
void Bind(ibValueMetaObjectDocument* doc, const ibValueMetaObjectDocumentNumerator* numerator)
{
	auto* prop = dynamic_cast<ibPropertyMetaObjectChoice*>(doc->GetProperty(ibT("Numerator")));
	ASSERT_NE(prop, nullptr);
	prop->SetValue(numerator != nullptr ? ibMetaDescription(numerator->GetMetaID()) : ibMetaDescription());
}

void SetPeriod(ibValueMetaObjectDocumentNumerator* numerator, ibNumberPeriodicity period)
{
	ibProperty* prop = numerator->GetProperty(ibT("NumberPeriodicity"));
	ASSERT_NE(prop, nullptr);
	ASSERT_TRUE(prop->SetDataValue(ibValue::CreateEnumObject<ibValueEnumNumberPeriodicity>(period)));
}

void SetCheckUnique(ibValueMetaObjectDocumentNumerator* numerator, bool check)
{
	ibProperty* prop = numerator->GetProperty(ibT("CheckUnique"));
	ASSERT_NE(prop, nullptr);
	ASSERT_TRUE(prop->SetDataValue(ibValue(check)));
}

ibString SqliteTypeOf(const ibString& field)
{
	if (field.EndsWith(wxT("_RRRef"))) return wxT(" BLOB");
	if (field.EndsWith(wxT("_D")) || field.EndsWith(wxT("_S"))) return wxT(" TEXT");
	if (field.EndsWith(wxT("_N"))) return wxT(" NUMERIC");
	return wxT(" INTEGER");
}

// Reaches the protected number generator and the uniqueness check a write runs before it saves.
class NumberProbe : public ibValueRecordDataObjectDocument {
public:
	explicit NumberProbe(const ibValueMetaObjectDocument* meta)
		: ibValueRecordDataObjectDocument(meta) {}
	void Prepare() { PrepareEmptyObject(); }
	void SetWhen(const ibDateTime& when) {
		SetValueByMetaID(GetMetaObject()->GetDocumentDate()->GetMetaID(), ibValue(when));
	}
	void PutNumber(const ibString& number) {
		SetValueByMetaID(GetMetaObject()->GetDocumentNumber()->GetMetaID(), ibValue(number));
	}
	ibString TakeNumber() {
		EXPECT_TRUE(GenerateUniqueIdentifier(wxEmptyString));
		ibValue number;
		EXPECT_TRUE(GetValueByMetaID(GetMetaObject()->GetDocumentNumber()->GetMetaID(), number));
		return number.GetString();
	}
	void Refuse(bool generated) { RefuseTakenNumber(generated); }
};

} // namespace

// ---------------------------------------------------------------------------------------------------
// The metadata: kept, and what a bound document answers.
// ---------------------------------------------------------------------------------------------------

TEST(DocumentNumerator, ANewNumeratorIsOneSeriesThatChecksNumbers)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectDocumentNumerator* numerator = MakeNumerator(cfg);
	ASSERT_NE(numerator, nullptr);
	EXPECT_EQ(ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical, numerator->GetNumberPeriodicity());
	EXPECT_TRUE(numerator->IsCheckUnique());
	ASSERT_NE(numerator->GetNumberAttribute(), nullptr);
	EXPECT_TRUE(numerator->GetNumberAttribute()->ContainType(ibValueTypes::TYPE_STRING));
	EXPECT_TRUE(numerator->GetMembers().empty());
}

TEST(DocumentNumerator, TheSettingsAndTheBindingRoundTrip)
{
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectDocumentNumerator* numerator = MakeNumerator(cfg);
	ibValueMetaObjectDocument* doc = MakeDocument(cfg, wxT("Invoice"));
	ASSERT_NE(numerator, nullptr);
	ASSERT_NE(doc, nullptr);
	SetPeriod(numerator, ibNumberPeriodicity::ibNumberPeriodicity_Year);
	SetCheckUnique(numerator, false);
	numerator->GetNumberAttribute()->GetTypeDesc().SetString(6);
	Bind(doc, numerator);

	ibDataNode savedNumerator, savedDoc;
	ASSERT_TRUE(numerator->SaveNode(savedNumerator));
	ASSERT_TRUE(doc->SaveNode(savedDoc));

	SetPeriod(numerator, ibNumberPeriodicity::ibNumberPeriodicity_Nonperiodical);
	SetCheckUnique(numerator, true);
	numerator->GetNumberAttribute()->GetTypeDesc().SetString(11);
	Bind(doc, nullptr);
	EXPECT_EQ(doc->GetNumerator(), nullptr);

	ASSERT_TRUE(numerator->LoadNode(savedNumerator));
	ASSERT_TRUE(doc->LoadNode(savedDoc));
	EXPECT_EQ(ibNumberPeriodicity::ibNumberPeriodicity_Year, numerator->GetNumberPeriodicity());
	EXPECT_FALSE(numerator->IsCheckUnique());
	EXPECT_EQ(6, numerator->GetNumberAttribute()->GetTypeDesc().GetLength());
	EXPECT_EQ(numerator, doc->GetNumerator());
	ASSERT_EQ(1u, numerator->GetMembers().size());
	EXPECT_EQ(doc, numerator->GetMembers().front());
}

// Through the whole configuration file, as the designer saves and opens it.
TEST(DocumentNumerator, TheConfigurationFileKeepsTheNumeratorAndItsMembers)
{
	const ibString path = wxFileName::CreateTempFileName(wxT("oesnum"));
	{
		ibMetaDataConfigurationFile cfg;
		ibValueMetaObjectDocumentNumerator* numerator = MakeNumerator(cfg);
		ASSERT_NE(numerator, nullptr);
		numerator->SetName(wxT("Sales"));
		Bind(MakeDocument(cfg, wxT("Invoice")), numerator);
		Bind(MakeDocument(cfg, wxT("Receipt")), numerator);
		MakeDocument(cfg, wxT("Order"));
		ASSERT_TRUE(cfg.SaveConfigToFile(path));
	}
	ibMetaDataConfigurationFile cfg;
	ASSERT_TRUE(cfg.LoadConfigFromFile(path));
	wxRemoveFile(path);

	std::vector<ibValueMetaObjectDocumentNumerator*> numerators =
		cfg.GetAnyArrayObject<ibValueMetaObjectDocumentNumerator>(g_metaDocumentNumeratorCLSID);
	ASSERT_EQ(1u, numerators.size());
	EXPECT_EQ(wxT("Sales"), numerators.front()->GetName());

	std::vector<ibString> members;
	for (const ibValueMetaObjectDocument* member : numerators.front()->GetMembers())
		members.push_back(member->GetName());
	EXPECT_EQ((std::vector<ibString>{ wxT("Invoice"), wxT("Receipt") }), members);
}

// ---------------------------------------------------------------------------------------------------
// The series, on a running configuration.
// ---------------------------------------------------------------------------------------------------

namespace {

struct NumeratorSeries : ::testing::Test {
	wxInitializer m_wxInit;
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectDocumentNumerator* numerator = nullptr;
	ibValueMetaObjectDocument* invoice = nullptr;
	ibValueMetaObjectDocument* receipt = nullptr;
	ibValueMetaObjectDocument* order = nullptr;     // numbers on its own

	// What the test asks of the configuration before it runs.
	virtual void Configure() {}

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		// A file: the uniqueness check reads through a connection of its own, and a clone of
		// :memory: is a different database.
		dbPath = wxFileName::CreateTempFileName(wxT("oesnum"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/4, /*minIdle=*/0);

		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		numerator = MakeNumerator(*cfg);
		invoice = MakeDocument(*cfg, wxT("Invoice"));
		receipt = MakeDocument(*cfg, wxT("Receipt"));
		order = MakeDocument(*cfg, wxT("Order"));
		ASSERT_NE(numerator, nullptr);
		ASSERT_NE(invoice, nullptr);
		ASSERT_NE(receipt, nullptr);
		ASSERT_NE(order, nullptr);
		numerator->SetName(wxT("Sales"));
		numerator->GetNumberAttribute()->GetTypeDesc().SetString(6);
		Bind(invoice, numerator);
		Bind(receipt, numerator);
		Configure();
		ASSERT_TRUE(cfg->RunDatabase()) << "the configuration in memory did not run";

		CreateTable(invoice);
		CreateTable(receipt);
		CreateTable(order);
		db->RunQuery(wxT("%s"),
			wxT("CREATE TABLE sys_sequence (interval INTEGER, meta_guid TEXT, prefix TEXT, number INTEGER)"));
		db->RunQuery(wxT("%s"),
			wxT("CREATE UNIQUE INDEX sequence_index ON sys_sequence (interval, meta_guid, prefix)"));
	}

	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		ibCompileCode::SetCodeStyle(m_codeStyle);
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
		if (db) db->Close();
		db.reset();
		if (!dbPath.IsEmpty()) {
			if (wxFileExists(dbPath)) wxRemoveFile(dbPath);
			if (wxFileExists(dbPath + wxT("-wal"))) wxRemoveFile(dbPath + wxT("-wal"));
			if (wxFileExists(dbPath + wxT("-shm"))) wxRemoveFile(dbPath + wxT("-shm"));
		}
	}

	void CreateTable(ibValueMetaObjectDocument* doc) {
		ibString list;
		std::set<ibString> seen;
		for (const ibValueMetaObjectAttributeBase* attribute : doc->GetGenericAttributeArrayObject()) {
			if (attribute == nullptr || attribute->GetQueryColumn() == nullptr)
				continue;
			for (const ibString& field : ColumnFieldNames(attribute->GetQueryColumn())) {
				if (!seen.insert(field).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << SqliteTypeOf(field);
			}
		}
		ASSERT_FALSE(list.IsEmpty());
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + doc->GetQueryable()->GetQueryTableName()
			+ wxT(" (") + list + wxT(")"));
	}

	void Assign(std::vector<ibDmlAssign>& out, const ibBackendQueryColumn* column, const ibValue& value) const {
		const std::vector<ibString> fields = ColumnFieldNames(column);
		ibQueryStatement capture(ibQueryStatement::Kind::Delete, ibString(), fields);
		int position = 1;
		ibColumnCodec::WriteValue(column, cfg.get(), value, &capture, position);
		const std::vector<ibQueryExprPtr>& consts = capture.CapturedValues();
		for (size_t i = 0; i < fields.size(); ++i)
			out.push_back(ibDmlAssign{ fields[i], (i < consts.size() && consts[i]) ? consts[i] : ibConst(ibValue()) });
	}

	ibString Padded(const ibValueMetaObjectDocument* doc, int n) const {
		const int width = doc->GetDocumentNumber()->GetTypeDesc().GetLength();
		return ibString::Format(wxT("%0*d"), width, n);
	}

	// A document already written: its reference, its date, its number.
	void Plant(ibValueMetaObjectDocument* doc, const ibDateTime& when, int n) {
		std::vector<ibDmlAssign> row;
		Assign(row, doc->GetDataReference()->GetQueryColumn(),
			ibValue(ibValueReferenceDataObject::Create(cfg.get(), doc->GetMetaID(), ibGuid(ibGuid::newGuid()))));
		Assign(row, doc->GetDocumentDate()->GetQueryColumn(), ibValue(when));
		Assign(row, doc->GetDocumentNumber()->GetQueryColumn(), ibValue(Padded(doc, n)));
		ibDatabaseQueryBuilder insert;
		ASSERT_GE(insert.Execute(ibInsert(doc->GetQueryable()->GetQueryTableName(), row)), 1);
	}

	ibString Next(ibValueMetaObjectDocument* doc, const ibDateTime& when) {
		NumberProbe probe(doc);
		probe.Prepare();
		probe.SetWhen(when);
		return probe.TakeNumber();
	}

	// A new document with a number typed by hand, checked as its write checks it. Empty: accepted.
	ibString Refusal(ibValueMetaObjectDocument* doc, const ibDateTime& when, int n) {
		NumberProbe probe(doc);
		probe.Prepare();
		probe.SetWhen(when);
		probe.PutNumber(Padded(doc, n));
		try {
			probe.Refuse(/*generated*/ false);
		}
		catch (const ibCoreException& err) {
			return err.what() != nullptr ? ibString::FromUTF8(err.what()) : ibString(wxT("?"));
		}
		return ibString();
	}
};

struct NumeratorSeriesByYear : NumeratorSeries {
	void Configure() override { SetPeriod(numerator, ibNumberPeriodicity::ibNumberPeriodicity_Year); }
};

struct NumeratorSeriesUnchecked : NumeratorSeries {
	void Configure() override { SetCheckUnique(numerator, false); }
};

} // namespace

// The number is the numerator's: its type and length, whatever the document's own Number said.
TEST_F(NumeratorSeries, ABoundDocumentHasTheNumeratorsNumberAndUnboundItsOwn)
{
	EXPECT_EQ(6, invoice->GetDocumentNumber()->GetTypeDesc().GetLength());
	EXPECT_EQ(6, receipt->GetDocumentNumber()->GetTypeDesc().GetLength());
	EXPECT_NE(6, order->GetDocumentNumber()->GetTypeDesc().GetLength());
	numerator->GetNumberAttribute()->GetTypeDesc().SetString(8);
	EXPECT_EQ(8, invoice->GetDocumentNumber()->GetTypeDesc().GetLength());
}

TEST_F(NumeratorSeries, TwoKindsShareOneSeries)
{
	EXPECT_EQ(Padded(invoice, 1), Next(invoice, ibDateTime(2026, 1, 10)));
	EXPECT_EQ(Padded(receipt, 2), Next(receipt, ibDateTime(2026, 1, 11)));
	EXPECT_EQ(Padded(invoice, 3), Next(invoice, ibDateTime(2026, 1, 12)));
	// The document that does not name the numerator keeps a counter of its own.
	EXPECT_EQ(Padded(order, 1), Next(order, ibDateTime(2026, 1, 12)));
}

// The first number of a series reads what every member table already has.
TEST_F(NumeratorSeries, TheFirstNumberReadsEveryMember)
{
	Plant(invoice, ibDateTime(2026, 1, 10), 4);
	Plant(receipt, ibDateTime(2026, 1, 11), 9);
	EXPECT_EQ(Padded(invoice, 10), Next(invoice, ibDateTime(2026, 2, 1)));
	EXPECT_EQ(Padded(receipt, 11), Next(receipt, ibDateTime(2026, 2, 1)));
}

TEST_F(NumeratorSeriesByYear, ANewYearStartsTheSharedSeriesAgain)
{
	Plant(receipt, ibDateTime(2025, 12, 30), 7);
	EXPECT_EQ(Padded(invoice, 8), Next(invoice, ibDateTime(2025, 12, 31)));
	EXPECT_EQ(Padded(receipt, 1), Next(receipt, ibDateTime(2026, 1, 2)));
	EXPECT_EQ(Padded(invoice, 2), Next(invoice, ibDateTime(2026, 1, 3)));
}

TEST_F(NumeratorSeries, ANumberTheOtherKindHasIsRefusedWithItsHolderNamed)
{
	Plant(receipt, ibDateTime(2026, 1, 11), 5);
	const ibString refusal = Refusal(invoice, ibDateTime(2026, 1, 12), 5);
	ASSERT_FALSE(refusal.IsEmpty()) << "the number 5 stands on a receipt already";
	EXPECT_NE(refusal.Find(receipt->GetSynonym()), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(Padded(invoice, 5)), wxNOT_FOUND) << refusal;
	EXPECT_NE(refusal.Find(wxT("Sales")), wxNOT_FOUND) << refusal;
	EXPECT_TRUE(Refusal(invoice, ibDateTime(2026, 1, 12), 6).IsEmpty());
}

TEST_F(NumeratorSeriesByYear, TheSameNumberInAnotherPeriodIsAccepted)
{
	Plant(receipt, ibDateTime(2025, 6, 1), 5);
	EXPECT_TRUE(Refusal(invoice, ibDateTime(2026, 1, 12), 5).IsEmpty());
	EXPECT_FALSE(Refusal(invoice, ibDateTime(2025, 7, 1), 5).IsEmpty());
}

TEST_F(NumeratorSeriesUnchecked, WithTheCheckOffANumberMayStandTwice)
{
	Plant(receipt, ibDateTime(2026, 1, 11), 5);
	EXPECT_TRUE(Refusal(invoice, ibDateTime(2026, 1, 12), 5).IsEmpty());
}

// A document of its own series is not checked against the numerator's.
TEST_F(NumeratorSeries, AnUnboundDocumentIsNotChecked)
{
	Plant(invoice, ibDateTime(2026, 1, 11), 5);
	EXPECT_TRUE(Refusal(order, ibDateTime(2026, 1, 12), 5).IsEmpty());
}

TEST_F(NumeratorSeries, ANumeratorDocumentsNumberFromIsNotDeleted)
{
	try {
		cfg->RemoveMetaObject(numerator);
		FAIL() << "the numerator was removed while two documents number from it";
	}
	catch (const ibCoreException& err) {
		const ibString text = ibString::FromUTF8(err.what());
		EXPECT_NE(text.Find(wxT("Sales")), wxNOT_FOUND) << text;
		EXPECT_NE(text.Find(wxT("Invoice")), wxNOT_FOUND) << text;
	}
	EXPECT_FALSE(numerator->IsDeleted());
	EXPECT_EQ(numerator, invoice->GetNumerator());
}
