// =============================================================================
// A walk through a COMPOSITE reference in the query language — live, on SQLite.
//
// A register field may hold a reference to one of several document types. A query walks it like any
// reference — T.Doc.Date — and the field is read from whichever type the row holds (#410).
// =============================================================================

#include <gtest/gtest.h>

#include <map>
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
#include "backend/metaCollection/attribute/metaAttributeObject.h"
#include "backend/metaCollection/partial/accumulationRegister.h"
#include "backend/metaCollection/partial/accumulationRegisterEnum.h"
#include "backend/databaseLayer/databaseMaterializeBuilder.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/columnLayout.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/query/queryable.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/session/session.h"
#include "backend/system/value/valueQuery.h"
#include "backend/system/value/valueTable.h"
#include "core/clsid.h"

#include "activeMetaDataAccess.h"

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

struct QueryCompositeWalk : ::testing::Test {
	wxInitializer m_wxInit;
	// RunDatabase sets this thread's code style to the configuration's; TearDown puts it back.
	const short m_codeStyle = ibCompileCode::GetCodeStyle();
	std::shared_ptr<ibDatabaseLayerSQLite> db;
	ibString dbPath;
	std::shared_ptr<ibSession> session;
	std::unique_ptr<ibSessionScope> scope;
	std::shared_ptr<ibMetaDataConfigurationFile> cfg;
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
		dbPath = wxFileName::CreateTempFileName(wxT("oeswalk"));
		if (!db->Open(dbPath))
			GTEST_SKIP() << "SQLite open failed";
		pool->Init(db, /*maxSize=*/2, /*minIdle=*/0);
		// The manager reads through the session's connection (ses_query).
		session = std::make_shared<ibSession>(wxT("query-composite-walk"), ibSessionKind::Enterprise);
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

	ibValueMetaObjectRecordDataRecorderRef* docA = nullptr;
	ibValueMetaObjectAccumulationRegister* settlements = nullptr;
	ibValueMetaObjectDimension* settled = nullptr;
	ibValueMetaObjectDimension* counterparty = nullptr;
	ibValueMetaObjectRecordDataRef* contracts = nullptr;
	ibValueMetaObjectAttribute* contractOf = nullptr;
	ibValueMetaObjectAttribute* contractCounterparty = nullptr;
	ibValueMetaObjectRecordDataRecorderRef* docB = nullptr;

	// Two document types and an independent register, Payments: dimension Key (a string), resource Doc
	// holding a reference to EITHER document — a composite reference, the shape a settlement register's
	// "credit document" has.
	void Build(ibPeriodicity periodicity = ibPeriodicity::eNonPeriodic) {
		cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
		// TYPE(<name>) names a type in the application's configuration.
		ActiveMetaDataAccess::Replace(cfg);
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		docA = dynamic_cast<ibValueMetaObjectRecordDataRecorderRef*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		docB = dynamic_cast<ibValueMetaObjectRecordDataRecorderRef*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		ASSERT_NE(docA, nullptr);
		ASSERT_NE(docB, nullptr);
		docA->SetName(wxT("Invoice"));
		docB->SetName(wxT("Receipt"));
		reg = dynamic_cast<ibValueMetaObjectInformationRegister*>(
			cfg->CreateMetaObject(g_metaInformationRegisterCLSID, root, false));
		ASSERT_NE(reg, nullptr);
		reg->SetName(wxT("Payments"));
		reg->GetProperty(ibT("Periodicity"))->SetValue(ibVariant(static_cast<long>(periodicity)));
		key = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, reg, false));
		resource = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, reg, false));
		ASSERT_NE(key, nullptr);
		ASSERT_NE(resource, nullptr);
		key->SetName(wxT("Key"));
		resource->SetName(wxT("Doc"));
		key->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		resource->GetTypeDesc().SetDefaultMetaType(reference_to_clsid(docA->GetMetaID(), clsid_metaclass(docA->GetClassType())));
		resource->GetTypeDesc().AppendMetaType(reference_to_clsid(docB->GetMetaID(), clsid_metaclass(docB->GetClassType())));
		ASSERT_EQ(resource->GetTypeDesc().GetClsidCount(), 2u);
		ASSERT_TRUE(cfg->RunDatabase());

		const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
		for (const ibSchemaTable& table : snapshot.Tables())
			if (!table.m_derived)
				CreateTable(table);
	}

	// An accumulation register of balances, Settlements: dimension Doc holding a reference to EITHER
	// document, resource Amount; the invoice is the recorder. Its totals are installed as an apply does.
	void BuildSettlements() {
		cfg = ibMetaData::MakeShared<ibMetaDataConfigurationFile>();
		ActiveMetaDataAccess::Replace(cfg);
		ibValueMetaObjectConfiguration* root = cfg->GetCommonMetaObject();
		ASSERT_NE(root, nullptr);
		docA = dynamic_cast<ibValueMetaObjectRecordDataRecorderRef*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		docB = dynamic_cast<ibValueMetaObjectRecordDataRecorderRef*>(cfg->CreateMetaObject(g_metaDocumentCLSID, root, false));
		ASSERT_NE(docA, nullptr);
		ASSERT_NE(docB, nullptr);
		docA->SetName(wxT("Invoice"));
		docB->SetName(wxT("Receipt"));
		// Only the invoice has a contract; the contract names its counterparty.
		contracts = dynamic_cast<ibValueMetaObjectRecordDataRef*>(cfg->CreateMetaObject(g_metaCatalogCLSID, root, false));
		ASSERT_NE(contracts, nullptr);
		contracts->SetName(wxT("Contracts"));
		contractCounterparty = dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, contracts, false));
		contractOf = dynamic_cast<ibValueMetaObjectAttribute*>(cfg->CreateMetaObject(g_metaAttributeCLSID, docA, false));
		ASSERT_NE(contractCounterparty, nullptr);
		ASSERT_NE(contractOf, nullptr);
		contractCounterparty->SetName(wxT("Counterparty"));
		contractCounterparty->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		contractOf->SetName(wxT("Contract"));
		contractOf->GetTypeDesc().SetDefaultMetaType(reference_to_clsid(contracts->GetMetaID(), clsid_metaclass(contracts->GetClassType())));
		settlements = dynamic_cast<ibValueMetaObjectAccumulationRegister*>(
			cfg->CreateMetaObject(g_metaAccumulationRegisterCLSID, root, false));
		ASSERT_NE(settlements, nullptr);
		settlements->SetName(wxT("Settlements"));
		settled = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, settlements, false));
		auto* amount = dynamic_cast<ibValueMetaObjectResource*>(cfg->CreateMetaObject(g_metaResourceCLSID, settlements, false));
		ASSERT_NE(settled, nullptr);
		ASSERT_NE(amount, nullptr);
		counterparty = dynamic_cast<ibValueMetaObjectDimension*>(cfg->CreateMetaObject(g_metaDimensionCLSID, settlements, false));
		ASSERT_NE(counterparty, nullptr);
		settled->SetName(wxT("Doc"));
		amount->SetName(wxT("Amount"));
		counterparty->SetName(wxT("Counterparty"));
		counterparty->GetTypeDesc().SetDefaultMetaType(ibValueTypes::TYPE_STRING);
		settled->GetTypeDesc().SetDefaultMetaType(reference_to_clsid(docA->GetMetaID(), clsid_metaclass(docA->GetClassType())));
		settled->GetTypeDesc().AppendMetaType(reference_to_clsid(docB->GetMetaID(), clsid_metaclass(docB->GetClassType())));
		settlements->GetRegisterRecorder()->GetTypeDesc().AppendMetaType(
			reference_to_clsid(docA->GetMetaID(), clsid_metaclass(docA->GetClassType())));
		ASSERT_TRUE(cfg->RunDatabase());

		const ibSchemaSnapshot snapshot = cfg->BuildSchemaSnapshot();
		for (const ibSchemaTable& table : snapshot.Tables())
			CreateTable(table);
		for (const ibSchemaTable& table : snapshot.Tables()) {
			if (!table.m_derived || table.m_name != settlements->GetRegisterTableNameDB())
				continue;
			const ibMaterializeSql sql = RenderMaterialization(table.m_materialize.ToRenderSpec(table.m_name),
				&ibDatabaseLayerSQLite::MaterializationDialect(), ibDatabaseLayerSQLite::Dialect());
			ASSERT_TRUE(sql.Apply(*db)) << "the register's totals were not installed";
		}
	}

	// One receipt line of the recorder, settling `doc`.
	void Settle(const ibValue& recorder, int line, const ibValue& doc, int amount, const ibString& who = wxT("Acme")) {
		ibDataQueryBuilder q;
		q.From(settlements->GetQueryable());
		q.SetValue(settlements->GetRegisterActive()->GetQueryColumn(), ibValue(true));
		q.SetValue(settlements->GetRegisterPeriod()->GetQueryColumn(), ibValue(ibDateTime(2026, 3, 10)));
		q.SetValue(settlements->GetRegisterRecorder()->GetQueryColumn(), recorder);
		q.SetValue(settlements->GetRegisterLineNumber()->GetQueryColumn(), ibValue(ibNumber(line)));
		q.SetValue(settlements->GetRegisterRecordType()->GetQueryColumn(),
			ibValue::CreateEnumObject<ibValueEnumAccumulationRegisterRecordType>(ibRecordType::eReceipt));
		q.SetValue(settled->GetQueryColumn(), doc);
		q.SetValue(counterparty->GetQueryColumn(), ibValue(who));
		q.SetValue(settlements->GetResourceArrayObject().front()->GetQueryColumn(), ibValue(ibNumber(amount)));
		ASSERT_TRUE(q.Insert()) << line;
	}

	ibValue NewContract(const ibString& who) {
		const ibValue ref(ibValueReferenceDataObject::Create(cfg.get(), contracts->GetMetaID(), ibGuid(ibGuid::newGuid())));
		ibDataQueryBuilder q;
		q.From(contracts->GetQueryable());
		q.SetValue(contracts->GetDataReference()->GetQueryColumn(), ref);
		q.SetValue(contractCounterparty->GetQueryColumn(), ibValue(who));
		EXPECT_TRUE(q.Insert()) << who;
		return ref;
	}

	// Settled for Acme: an invoice under Acme's contract, a receipt (no contract at all), and an
	// invoice under another counterparty's contract.
	void FillSettlements() {
		BuildSettlements();
		const ibValue invoice = NewDocument(docA, ibDateTime(2026, 3, 2), wxT("A-1"), NewContract(wxT("Acme")));
		const ibValue receipt = NewDocument(docB, ibDateTime(2026, 3, 1), wxT("B-1"));
		const ibValue foreign = NewDocument(docA, ibDateTime(2026, 3, 3), wxT("A-2"), NewContract(wxT("Beta")));
		Settle(invoice, 1, invoice, 10);
		Settle(invoice, 2, receipt, 5);
		Settle(foreign, 3, foreign, 7);
	}

	std::map<ibString, ibValue> Interval() const {
		return { { wxT("From"), ibValue(ibDateTime(2026, 3, 1)) }, { wxT("To"), ibValue(ibDateTime(2026, 3, 31)) } };
	}

	// A document row: its reference, its date and its number, through the door a write stands on.
	ibValue NewDocument(ibValueMetaObjectRecordDataRecorderRef* doc, const ibDateTime& date, const ibString& number,
	                    const ibValue& contract = ibValue()) {
		const ibValue ref(ibValueReferenceDataObject::Create(cfg.get(), doc->GetMetaID(), ibGuid(ibGuid::newGuid())));
		ibDataQueryBuilder q;
		q.From(doc->GetQueryable());
		q.SetValue(doc->GetDataReference()->GetQueryColumn(), ref);
		q.SetValue(doc->GetDocumentDate()->GetQueryColumn(), ibValue(date));
		q.SetValue(doc->GetDocumentNumber()->GetQueryColumn(), ibValue(number));
		if (contractOf != nullptr && doc == docA && !contract.IsEmpty())
			q.SetValue(contractOf->GetQueryColumn(), contract);
		EXPECT_TRUE(q.Insert()) << number;
		return ref;
	}

	void CreateTable(const ibSchemaTable& table) {
		if (table.m_name.IsEmpty())
			return;
		ibString list;
		std::set<ibString> seen;
		auto addNamed = [&](const ibString& field, const ibString& sqlType) {
			if (field.IsEmpty() || !seen.insert(field).second)
				return;
			list << (list.IsEmpty() ? wxT("") : wxT(", ")) << field << sqlType;
		};
		auto addColumn = [&](const ibBackendQueryColumn* column) {
			if (column == nullptr)
				return;
			for (const ibColumnSlot& slot : DescribeColumnLayout(column))
				addNamed(slot.m_name, SqliteTypeOf(slot.m_type));
		};
		for (const ibBackendQueryColumn* column : table.m_scaffold)
			addColumn(column);
		for (const ibSchemaColumn& column : table.m_columns)
			addColumn(column.m_column);
		// The trigger names the period by the string the declaration stored, which is the
		// movement's date field. A scaffold date does not always expand to that same name.
		// An accumulator is a raw number: its name does not end in _N, and an integer
		// column would drop the fraction the oracle is checking.
		if (table.m_derived) {
			addNamed(table.m_materialize.m_periodColumn, wxT(" TEXT"));
			addNamed(table.m_materialize.m_keyHashColumn, wxT(" TEXT"));
			if (table.m_materialize.m_shards > 1)
				addNamed(ibString(ShardColumnName()), wxT(" INTEGER"));
			for (const ibBackendQueryColumn* key : table.m_materialize.m_keys)
				addColumn(key);
			for (const ibSchemaDelta& delta : table.m_materialize.m_deltas)
				addColumn(delta.m_column);
		}
		if (list.IsEmpty())
			return;
		db->RunQuery(wxT("%s"), wxT("CREATE TABLE ") + table.m_name + wxT(" (") + list + wxT(")"));
		if (!table.m_derived)
			return;
		// ON CONFLICT names this key. The trigger does not create the index; the table has to.
		const ibMaterializeSpec render = table.m_materialize.ToRenderSpec(table.m_name);
		ibString keys;
		auto addKey = [&](const ibString& field) {
			if (field.IsEmpty())
				return;
			keys << (keys.IsEmpty() ? wxT("") : wxT(", ")) << field;
		};
		// The conflict target is the hash only when this engine can digest one. SQLite has no
		// digest, so the trigger names the period and the dimensions even if the declaration
		// also grew a hash column for a wider engine.
		const bool hashed = !render.m_keyHashColumn.IsEmpty()
			&& !ibDatabaseLayerSQLite::MaterializationDialect().m_keyHashDigest.IsEmpty();
		if (hashed)
			addKey(render.m_keyHashColumn);
		else {
			addKey(render.m_periodColumn);
			for (const ibString& field : render.m_keyColumns)
				addKey(field);
			// The declaration may ask for several shards. SQLite keeps one: the trigger's
			// conflict target leaves the shard column out, and the index has to name the same columns.
			if (!ibDatabaseLayerSQLite::MaterializationDialect().m_connectionIdExpr.IsEmpty()
				&& render.m_shards > 1)
				addKey(ibString(ShardColumnName()));
		}
		if (!keys.IsEmpty())
			db->RunQuery(wxT("%s"), wxT("CREATE UNIQUE INDEX ") + table.m_name
				+ wxT("_PK ON ") + table.m_name + wxT(" (") + keys + wxT(")"));
	}

	// The door a record manager's Write stands on: one row, by column, into the register.
	void Insert(const ibString& keyValue, const ibValue& value, const ibDateTime& period = ibDateTime()) {
		ibDataQueryBuilder q;
		q.From(reg->GetQueryable());
		if (reg->HasPeriod())
			q.SetValue(reg->GetRegisterPeriod()->GetQueryColumn(), ibValue(period));
		q.SetValue(key->GetQueryColumn(), ibValue(keyValue));
		q.SetValue(resource->GetQueryColumn(), value);
		ASSERT_TRUE(q.Insert()) << keyValue;
	}

	// The rows a query text gives, as a script reads them: New Query(text).Execute().Unload().
	long Count(const ibString& text) const {
		// A query resolves its sources in the configuration it runs on behalf of.
		const ibSourceMetaDataScope configuration(cfg.get());
		ibValuePtr<ibValueQueryExec> query(new ibValueQueryExec());
		ibValue textValue(text);
		ibValue* init[] = { &textValue };
		EXPECT_TRUE(query->Init(init, 1)) << text;
		ibValue result;
		EXPECT_TRUE(query->CallAsFunc(query->FindMethod(wxT("Execute")), result, nullptr, 0)) << text;
		ibValueQueryResult* rows = nullptr;
		if (!result.ConvertToValue(rows) || rows == nullptr) {
			ADD_FAILURE() << "Execute answered no QueryResult";
			return -1;
		}
		ibValue unloaded;
		EXPECT_TRUE(rows->CallAsFunc(rows->FindMethod(wxT("Unload")), unloaded, nullptr, 0));
		ibValueModelTable* table = nullptr;
		if (!unloaded.ConvertToValue(table) || table == nullptr) {
			ADD_FAILURE() << "Unload answered no table";
			return -1;
		}
		return table->GetRowCount();
	}

	// The Key column of the rows a query text gives, in their order, as a script walks them:
	// s = New Query(text).Execute().Select(); while (s.Next()) { s.Key }.
	std::vector<ibString> Keys(const ibString& text) const { return Column(text, wxT("Key")); }

	std::vector<ibString> Column(const ibString& text, const ibString& column,
	                             const std::map<ibString, ibValue>& parameters = {}) const {
		std::vector<ibString> keys;
		// A query resolves its sources in the configuration it runs on behalf of.
		const ibSourceMetaDataScope configuration(cfg.get());
		ibValuePtr<ibValueQueryExec> query(new ibValueQueryExec());
		ibValue textValue(text);
		ibValue* init[] = { &textValue };
		EXPECT_TRUE(query->Init(init, 1)) << text;
		for (const auto& [name, value] : parameters) {
			ibValue parameterName(name), parameterValue(value);
			ibValue* parameter[] = { &parameterName, &parameterValue };
			EXPECT_TRUE(query->CallAsProc(query->FindMethod(wxT("SetParameter")), parameter, 2));
		}
		ibValue result;
		EXPECT_TRUE(query->CallAsFunc(query->FindMethod(wxT("Execute")), result, nullptr, 0)) << text;
		ibValueQueryResult* rows = nullptr;
		if (!result.ConvertToValue(rows) || rows == nullptr) {
			ADD_FAILURE() << "Execute answered no QueryResult";
			return keys;
		}
		ibValue selection;
		EXPECT_TRUE(rows->CallAsFunc(rows->FindMethod(wxT("Select")), selection, nullptr, 0));
		ibValueQuerySelect* cursor = nullptr;
		if (!selection.ConvertToValue(cursor) || cursor == nullptr) {
			ADD_FAILURE() << "Select answered no QuerySelect";
			return keys;
		}
		const long next = cursor->FindMethod(wxT("Next"));
		for (;;) {
			ibValue more;
			EXPECT_TRUE(cursor->CallAsFunc(next, more, nullptr, 0));
			if (!more.GetBoolean())
				break;
			ibValue keyValue;
			EXPECT_TRUE(cursor->GetPropVal(cursor->FindProp(column), keyValue));
			keys.push_back(keyValue.GetString());
		}
		return keys;
	}

	// An invoice dated after a receipt: ordered by the document's date, the receipt's row comes first.
	void Fill(ibPeriodicity periodicity = ibPeriodicity::eNonPeriodic) {
		Build(periodicity);
		const ibValue invoice = NewDocument(docA, ibDateTime(2026, 3, 2), wxT("A-1"));
		const ibValue receipt = NewDocument(docB, ibDateTime(2026, 3, 1), wxT("B-1"));
		const ibDateTime period(2026, 3, 5);
		Insert(wxT("invoice"), invoice, period);
		Insert(wxT("receipt"), receipt, period);
	}
};

const std::vector<ibString> kReceiptFirst = { wxT("receipt"), wxT("invoice") };

} // namespace

// A field every document type has — the date — read through a reference that may point at either.
TEST_F(QueryCompositeWalk, OrderByTheDateOfACompositeReference)
{
	Fill();
	EXPECT_EQ(kReceiptFirst, Keys(wxT("SELECT T.Key FROM InformationRegister.Payments AS T ORDER BY T.Doc.Date")));
}

TEST_F(QueryCompositeWalk, FilterByTheNumberOfACompositeReference)
{
	Fill();
	EXPECT_EQ(std::vector<ibString>{ wxT("invoice") },
		Keys(wxT("SELECT T.Key FROM InformationRegister.Payments AS T WHERE T.Doc.Number = \"A-1\"")));
}

TEST_F(QueryCompositeWalk, SelectTheDateOfACompositeReference)
{
	Fill();
	EXPECT_EQ(kReceiptFirst,
		Keys(wxT("SELECT T.Key, T.Doc.Date AS D FROM InformationRegister.Payments AS T ORDER BY D")));
}

// The same walk over a register's VIRTUAL table: rows the engine computes rather than reads.
TEST_F(QueryCompositeWalk, OrderByTheDateOfACompositeReferenceOverASlice)
{
	Fill(ibPeriodicity::eWithinDay);
	EXPECT_EQ(kReceiptFirst,
		Keys(wxT("SELECT T.Key FROM InformationRegister.Payments.SliceLast() AS T ORDER BY T.Doc.Date")));
}

// The register's virtual table, ordered by a field of the composite dimension.
TEST_F(QueryCompositeWalk, OrderByTheDateOfACompositeDimensionOverBalanceAndTurnovers)
{
	FillSettlements();
	EXPECT_EQ((std::vector<ibString>{ wxT("B-1"), wxT("A-1"), wxT("A-2") }), Column(
		wxT("SELECT T.Doc.Number AS N FROM AccumulationRegister.Settlements.BalanceAndTurnovers(&From, &To, Recorder) AS T ")
		wxT("ORDER BY T.Doc.Date"), wxT("N"), Interval()));
}

// …and filtered, inside the virtual table's own condition, by a field of the composite dimension.
TEST_F(QueryCompositeWalk, AConditionOfBalanceAndTurnoversWalksTheCompositeDimension)
{
	FillSettlements();
	EXPECT_EQ((std::vector<ibString>{ wxT("A-1") }), Column(
		wxT("SELECT T.Doc.Number AS N FROM AccumulationRegister.Settlements.BalanceAndTurnovers(&From, &To, Recorder, , ")
		wxT("Doc.Number = \"A-1\") AS T"), wxT("N"), Interval()));
}

// Grouped by the composite dimension and ordered by a field of it: the key fixes the document, so the
// sort walks on from the key. The sort was dropped without a word, and the rows came back in the
// order the server liked (#410).
TEST_F(QueryCompositeWalk, AGroupedQueryOrdersByTheDateOfItsCompositeKey)
{
	FillSettlements();
	const ibString query =
		wxT("SELECT T.Doc AS Doc, T.Doc.Number AS N, SUM(T.AmountClosingBalance) AS S ")
		wxT("FROM AccumulationRegister.Settlements.BalanceAndTurnovers(&From, &To, Recorder) AS T ")
		wxT("GROUP BY T.Doc ORDER BY T.Doc.Date");
	EXPECT_EQ((std::vector<ibString>{ wxT("B-1"), wxT("A-1"), wxT("A-2") }), Column(query, wxT("N"), Interval()));
	EXPECT_EQ((std::vector<ibString>{ wxT("A-2"), wxT("A-1"), wxT("B-1") }),
		Column(query + wxT(" DESC"), wxT("N"), Interval()));
}

// …and the same through a key of ONE type, the recorder.
TEST_F(QueryCompositeWalk, AGroupedQueryOrdersByTheDateOfItsReferenceKey)
{
	FillSettlements();
	const ibString query =
		wxT("SELECT T.Recorder AS R, T.Recorder.Number AS N, SUM(T.AmountClosingBalance) AS S ")
		wxT("FROM AccumulationRegister.Settlements.BalanceAndTurnovers(&From, &To, Recorder) AS T ")
		wxT("GROUP BY T.Recorder ORDER BY T.Recorder.Date");
	EXPECT_EQ((std::vector<ibString>{ wxT("A-1"), wxT("A-2") }), Column(query, wxT("N"), Interval()));
	EXPECT_EQ((std::vector<ibString>{ wxT("A-2"), wxT("A-1") }),
		Column(query + wxT(" DESC"), wxT("N"), Interval()));
}

// The door's own rule, asked of the door: a sort's walk is grouped by only where it goes on from a key. The query
// language refuses the other sort itself; the door is everyone's — a report's levels, a list, a script — and a walk no
// key fixes, grouped by, would split the groups: one total a row per document.
TEST_F(QueryCompositeWalk, ASortThatWalksOnFromNoKeyKeepsTheGroups)
{
	BuildSettlements();
	const ibBackendQueryColumn* const doc = settled->GetQueryColumn();
	const ibBackendQueryColumn* const recorder = settlements->GetRegisterRecorder()->GetQueryColumn();
	const ibBackendQueryColumn* const amount = settlements->GetResourceArrayObject().front()->GetQueryColumn();
	const std::vector<const ibBackendQueryColumn*> docDate{ doc, docA->GetDocumentDate()->GetQueryColumn() };

	ibDataQueryBuilder onFromTheKey;
	onFromTheKey.From(settlements->GetQueryable());
	onFromTheKey.GroupBy(doc);
	onFromTheKey.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, amount, wxT("S"));
	onFromTheKey.OrderBy(docDate, true);
	EXPECT_EQ(onFromTheKey.GetGroupBy().size(), 2u) << "the walk on from the key is a key as well";

	ibDataQueryBuilder onFromAnother;
	onFromAnother.From(settlements->GetQueryable());
	onFromAnother.GroupBy(recorder);
	onFromAnother.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, amount, wxT("S"));
	onFromAnother.OrderBy(docDate, true);
	EXPECT_EQ(onFromAnother.GetGroupBy().size(), 1u) << "a walk on from what is not a key adds no group";

	ibDataQueryBuilder noKey;
	noKey.From(settlements->GetQueryable());
	noKey.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, amount, wxT("S"));
	noKey.OrderBy(docDate, true);
	EXPECT_TRUE(noKey.GetGroupBy().empty()) << "one total stays one";
}
