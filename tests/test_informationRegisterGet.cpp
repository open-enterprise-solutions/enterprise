// =============================================================================
// InformationRegister.Get(Period, Filter) with no period given — live, on SQLite.
//
// A period omitted or passed as Undefined names no period: the filter alone decides, as Get(Filter),
// on every register. Keyed on it, a non-periodic register answered its empty table and a periodic one
// looked for the empty date — "no record" for a record that is there (#396).
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
#include "backend/metaCollection/partial/informationRegister.h"
#include "backend/metaCollection/partial/informationRegisterManager.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/session/session.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
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

struct InformationRegisterGet : ::testing::Test {
	wxInitializer m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's; TearDown puts it back.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::shared_ptr<ibSession> session;
	std::unique_ptr<ibSessionScope> scope;
	std::unique_ptr<ibMetaDataConfigurationFile> cfg;
	ibValueMetaObjectInformationRegister* reg = nullptr;
	ibValueMetaObjectDimension* key = nullptr;
	ibValueMetaObjectResource* resource = nullptr;

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
		dbPath = wxFileName::CreateTempFileName(wxT("oesreg"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/2, /*minIdle=*/0);
		// The manager reads through the session's connection (ses_query).
		session = std::make_shared<ibSession>(wxT("register-get"), ibSessionKind::Enterprise);
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

	// An independent register, Settings: dimension Key (a string), resource Value (a number).
	void Build(ibPeriodicity periodicity) {
		cfg = std::make_unique<ibMetaDataConfigurationFile>();
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		reg = dynamic_cast<ibValueMetaObjectInformationRegister*>(
			cfg->CreateMetaObject(g_metaInformationRegisterCLSID, root, false));
		ASSERT_NE(reg, nullptr);
		reg->SetName(wxT("Settings"));
		reg->GetProperty(ibT("Periodicity"))->SetValue(ibVariant(static_cast<long>(periodicity)));
		ASSERT_EQ(reg->GetPeriodicity(), periodicity);
		key = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		resource = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, reg, false));
		ASSERT_NE(key, nullptr);
		ASSERT_NE(resource, nullptr);
		key->SetName(wxT("Key"));
		resource->SetName(wxT("Value"));
		key->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
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

	// The door a record manager's Write stands on: one row, by column, into the register.
	void Insert(const ibString& keyValue, int value, const ibDateTime& period = ibDateTime()) {
		ibDataQueryBuilder q;
		q.From(reg->GetQueryable());
		if (reg->HasPeriod())
			q.SetValue(reg->GetRegisterPeriod()->GetQueryColumn(), ibValue(period));
		q.SetValue(key->GetQueryColumn(), ibValue(keyValue));
		q.SetValue(resource->GetQueryColumn(), ibValue(ibNumber(value)));
		ASSERT_TRUE(q.Insert()) << keyValue;
	}

	ibValue KeyFilter(const ibString& keyValue) const {
		ibValuePtr<ibValueStructure> filter(new ibValueStructure());
		filter->Insert(ibValue(ibT("Key")), ibValue(keyValue));
		return ibValue(filter);
	}

	// Get as a script calls it: the manager the register hands out, its method by name.
	ibValue Get(std::vector<ibValue> args) const {
		const ibValueMetaObjectGenericData& data = *reg;
		const ibValuePtr<ibValueManagerDataObject> manager = data.CreateManagerDataObjectValue();
		std::vector<ibValue*> params;
		for (ibValue& arg : args)
			params.push_back(&arg);
		ibValue answer;
		EXPECT_TRUE(manager->CallAsFunc(manager->FindMethod(wxT("Get")), answer, params.data(), static_cast<long>(params.size())));
		return answer;
	}

	long RowsOf(const ibValue& answer) const {
		ibValueModelTable* table = nullptr;
		if (!answer.ConvertToValue(table) || table == nullptr) {
			ADD_FAILURE() << "Get answered no table";
			return -1;
		}
		return table->GetRowCount();
	}
};

} // namespace

TEST_F(InformationRegisterGet, AnUndefinedPeriodOnANonPeriodicRegisterReadsByTheFilter)
{
	Build(ibPeriodicity::eNonPeriodic);
	Insert(wxT("a"), 1);
	Insert(wxT("b"), 2);

	EXPECT_EQ(1, RowsOf(Get({ ibValue(), KeyFilter(wxT("a")) })));
	EXPECT_EQ(RowsOf(Get({ KeyFilter(wxT("a")) })), RowsOf(Get({ ibValue(), KeyFilter(wxT("a")) })))
		<< "Get(Undefined, Filter) is Get(Filter)";
}

TEST_F(InformationRegisterGet, AnUndefinedPeriodOnAPeriodicRegisterReadsEveryPeriod)
{
	Build(ibPeriodicity::eWithinMonth);
	Insert(wxT("a"), 1, ibDateTime(2026, 3, 1));
	Insert(wxT("a"), 2, ibDateTime(2026, 4, 1));
	Insert(wxT("b"), 3, ibDateTime(2026, 4, 1));

	EXPECT_EQ(2, RowsOf(Get({ ibValue(), KeyFilter(wxT("a")) })))
		<< "no period given is no period's condition, not the empty date";
	EXPECT_EQ(1, RowsOf(Get({ ibValue(ibDateTime(2026, 4, 1)), KeyFilter(wxT("a")) })))
		<< "a period given still keys the read";
}
