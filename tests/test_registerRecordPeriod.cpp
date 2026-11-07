// =============================================================================
// A register line written with no period takes its recorder's date — live, on SQLite.
//
// A document's movements are written as Doc.RegisterRecords.X: the set is keyed by the document, and code that
// fills it names the item and the quantity and leaves the period to the document. The set refused that write with
// "The Period is required on line 1" (#421): the period is filled from the recorder's date before the fill check,
// and before an information register truncates it to its periodicity. A period the code put is kept.
// =============================================================================

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include <wx/filename.h>
#include <wx/init.h>

#include "backend/appData.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/value.h"
#include "backend/databaseLayer/connectionPool.h"
#include "backend/databaseLayer/sqllite/sqliteDatabaseLayer.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/dimension/metaDimensionObject.h"
#include "backend/metaCollection/resource/metaResourceObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/session/session.h"
#include "core/clsid.h"

namespace {

ibString SqliteTypeOf(const ibColumnType& type)
{
	switch (type.m_kind) {
	case ibCanonicalKind::Number: return wxT(" NUMERIC");
	case ibCanonicalKind::Date:
	case ibCanonicalKind::String: return wxT(" TEXT");
	case ibCanonicalKind::Blob:
	case ibCanonicalKind::Binary:
	case ibCanonicalKind::Guid:   return wxT(" BLOB");
	default:                      return wxT(" INTEGER");
	}
}

const ibDateTime kDocumentDate(2026, 3, 17, 14, 30, 0);

struct RegisterRecordPeriod : ::testing::Test {
	wxInitializer m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's; TearDown puts it back.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::shared_ptr<ibSession> session;
	std::unique_ptr<ibSessionScope> scope;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectRecordDataRecorderRef* document = nullptr;   // a document: what a recorder is
	ibValueMetaObjectRegisterData* reg = nullptr;
	ibValueMetaObjectDimension* item = nullptr;
	ibValueMetaObjectResource* quantity = nullptr;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (!ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";
		ibConnectionPool* pool = ibApplicationInstance::GetConnectionPool();
		if (pool == nullptr)
			GTEST_SKIP() << "no connection pool after CreateAppDataEnv";
		db = std::make_shared<ibDatabaseLayerSQLite>();
		// A file, so a connection checked out of the pool sees the same rows.
		dbPath = wxFileName::CreateTempFileName(wxT("oesrec"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/2, /*minIdle=*/0);
		// The set writes through the session's connection scope.
		session = std::make_shared<ibSession>(wxT("register-record-period"), ibSessionKind::Enterprise);
		scope = std::make_unique<ibSessionScope>(session.get());
	}

	void TearDown() override {
		if (cfg) cfg->CloseDatabase();
		cfg.reset();
		scope.reset();
		session.reset();
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

	// A document, and a register of its movements: dimension Item (a string), resource Quantity.
	void Build(const ibClassID& registerKind) {
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		document = dynamic_cast<ibValueMetaObjectRecordDataRecorderRef*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		ASSERT_NE(document, nullptr);
		document->SetName(wxT("Receipt"));
		reg = dynamic_cast<ibValueMetaObjectRegisterData*>(cfg->CreateMetaObject(registerKind, root, false));
		ASSERT_NE(reg, nullptr);
		reg->SetName(wxT("Movements"));
		if (registerKind == g_metaInformationRegisterCLSID) {
			reg->GetProperty(ibT("WriteMode"))->SetValue(ibVariant(static_cast<long>(ibWriteRegisterMode::eSubordinateRecorder)));
			reg->GetProperty(ibT("Periodicity"))->SetValue(ibVariant(static_cast<long>(ibPeriodicity::eWithinMonth)));
		}
		ASSERT_TRUE(reg->HasRecorder());
		ASSERT_TRUE(reg->HasPeriod());
		item = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		quantity = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, reg, false));
		ASSERT_NE(item, nullptr);
		ASSERT_NE(quantity, nullptr);
		item->SetName(wxT("Item"));
		quantity->SetName(wxT("Quantity"));
		item->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		reg->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(
			reference_to_clsid(document->GetMetaID(), clsid_metaclass(document->GetClassType())));
		ASSERT_TRUE(cfg->RunDatabase());

		const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
		for (const ibSchemaTable& table : snapshot.Tables())
			if (!table.m_derived)
				CreateTable(table);
	}

	void CreateTable(const ibSchemaTable& table) {
		if (table.m_name.IsEmpty())
			return;
		ibString list;
		std::set<ibString> seen;
		auto addColumn = [&](const ibBackendQueryColumn* column) {
			if (column == nullptr)
				return;
			for (const ibColumnSlot& slot : DescribeColumnLayout(column)) {
				if (slot.m_name.IsEmpty() || !seen.insert(slot.m_name).second)
					continue;
				list << (list.IsEmpty() ? wxT("") : wxT(", ")) << slot.m_name << SqliteTypeOf(slot.m_type);
			}
		};
		for (const ibBackendQueryColumn* column : table.m_scaffold)
			addColumn(column);
		for (const ibSchemaColumn& column : table.m_columns)
			addColumn(column.m_column);
		if (!list.IsEmpty())
			db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + table.m_name + wxT(" (") + list + wxT(")"));
	}

	// A stored document of that date: its row, by column, as its own write stores it.
	ibValue StoredDocument(const ibDateTime& date) {
		const ibValue ref(ibValueReferenceDataObject::Create(cfg.get(), document->GetMetaID(), ibGuid(ibGuid::newGuid())));
		ibDataQueryBuilder q;
		q.From(document->GetQueryable());
		q.SetValue(document->GetDataReference()->GetQueryColumn(), ref);
		q.SetValue(document->GetDocumentDate()->GetQueryColumn(), ibValue(date));
		EXPECT_TRUE(q.Insert());
		return ref;
	}

	// The document's set as RegisterRecords hands it out: keyed by the document.
	ibValuePtr<ibValueRecordSetObject> RecordsOf(const ibValue& recorder) const {
		ibValuePtr<ibValueRecordSetObject> set = reg->CreateRecordSetObjectValue(/*needInitialize*/ false);
		EXPECT_TRUE(set != nullptr);
		if (set != nullptr)
			set->SetKeyValue(reg->GetRegisterRecorder()->GetMetaID(), ibValue(recorder));
		return set;
	}

	// A line as `R = Set.Add(); R.Item = …; R.Quantity = …;` fills it — the period only when given.
	static void AddLine(ibValueRecordSetObject& set, const ibValueMetaObjectRegisterData& meta,
		const ibValueMetaObjectDimension& item, const ibValueMetaObjectResource& quantity,
		const ibString& itemName, int count, const ibDateTime& period = ibDateTime()) {
		const long row = set.AppendRow();
		ibComposerNode* node = set.GetViewData<ibComposerNode>(set.GetItem(row));
		ASSERT_NE(node, nullptr);
		node->SetValue(item.GetMetaID(), ibValue(itemName));
		node->SetValue(quantity.GetMetaID(), ibValue(ibNumber(count)));
		if (!period.IsEmpty())
			node->SetValue(meta.GetRegisterPeriod()->GetMetaID(), ibValue(period));
	}

	ibValue PeriodOfLine(const ibValueRecordSetObject& set, long row) const {
		const ibComposerNode* node = set.GetViewData<ibComposerNode>(set.GetItem(row));
		const ibMetaID period = reg->GetRegisterPeriod()->GetMetaID();
		return node != nullptr && node->HasColumnValue(period) ? node->GetTableValue(period) : ibValue();
	}

	// The periods stored for the recorder, read back from the table.
	std::vector<ibDateTime> StoredPeriods(const ibValue& recorder) const {
		std::vector<ibDateTime> periods;
		ibDataQueryBuilder q;
		q.From(reg->GetQueryable());
		q.Where(reg->GetRegisterRecorder()->GetQueryColumn(), recorder);
		ibReadPageRequest page;
		page.m_count = 0;
		ibDataQueryResult rows = q.Execute(page);
		while (rows.Next())
			periods.push_back(rows.GetValue(reg->GetRegisterPeriod()->GetQueryColumn()).GetDate());
		return periods;
	}
};

} // namespace

TEST_F(RegisterRecordPeriod, AnAccumulationLineWithNoPeriodIsDatedByItsRecorder)
{
	Build(g_metaAccumulationRegisterCLSID);
	const ibValue receipt = StoredDocument(kDocumentDate);
	ibValuePtr<ibValueRecordSetObject> set = RecordsOf(receipt);
	ASSERT_TRUE(set != nullptr);
	AddLine(*set, *reg, *item, *quantity, wxT("Bolt"), 5);

	ASSERT_NO_THROW(set->WriteRecordSet(/*replace*/ true, /*clearTable*/ false));
	EXPECT_EQ(PeriodOfLine(*set, 0).GetDate(), kDocumentDate) << "the line shows the period it was written with";
	const std::vector<ibDateTime> stored = StoredPeriods(receipt);
	ASSERT_EQ(stored.size(), 1u);
	EXPECT_EQ(stored.front(), kDocumentDate);
}

TEST_F(RegisterRecordPeriod, APeriodTheCodePutIsKept)
{
	Build(g_metaAccumulationRegisterCLSID);
	const ibValue receipt = StoredDocument(kDocumentDate);
	ibValuePtr<ibValueRecordSetObject> set = RecordsOf(receipt);
	ASSERT_TRUE(set != nullptr);
	const ibDateTime own(2026, 1, 5, 9, 0, 0);
	AddLine(*set, *reg, *item, *quantity, wxT("Bolt"), 5, own);
	AddLine(*set, *reg, *item, *quantity, wxT("Nut"), 7);

	ASSERT_NO_THROW(set->WriteRecordSet(true, false));
	EXPECT_EQ(PeriodOfLine(*set, 0).GetDate(), own);
	EXPECT_EQ(PeriodOfLine(*set, 1).GetDate(), kDocumentDate);
}

// A periodic information register truncates the period to its own unit: the recorder's date is put in first, so
// the month it lands in is the document's.
TEST_F(RegisterRecordPeriod, ASubordinateInformationLineTakesTheRecordersDateTruncated)
{
	Build(g_metaInformationRegisterCLSID);
	const ibValue receipt = StoredDocument(kDocumentDate);
	ibValuePtr<ibValueRecordSetObject> set = RecordsOf(receipt);
	ASSERT_TRUE(set != nullptr);
	AddLine(*set, *reg, *item, *quantity, wxT("Bolt"), 5);

	ASSERT_NO_THROW(set->WriteRecordSet(true, false));
	const std::vector<ibDateTime> stored = StoredPeriods(receipt);
	ASSERT_EQ(stored.size(), 1u);
	EXPECT_EQ(stored.front(), ibDateTime(2026, 3, 1));
}

// No recorder in the set and none on the line: nothing to date it by, and the fill check still refuses it.
TEST_F(RegisterRecordPeriod, ALineWithNeitherPeriodNorRecorderIsStillRefused)
{
	Build(g_metaAccumulationRegisterCLSID);
	ibValuePtr<ibValueRecordSetObject> set = reg->CreateRecordSetObjectValue(false);
	ASSERT_TRUE(set != nullptr);
	AddLine(*set, *reg, *item, *quantity, wxT("Bolt"), 5);

	EXPECT_THROW(set->WriteRecordSet(true, false), ibCoreException);
}
